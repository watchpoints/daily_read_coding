/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "memcached.h"
#include "bipbuffer.h"
#include "slab_automove.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

/* Forward Declarations */
static void item_link_q(item *it);   // 将 item 插入到 LRU 链表
static void item_unlink_q(item *it); // 将 item 从 LRU 链表中删除

// 四种 LRU 双向链表的类型
static unsigned int lru_type_map[4] = {HOT_LRU, WARM_LRU, COLD_LRU, TEMP_LRU};

// POWER_LARGEST 为 256，实际为 255
#define LARGEST_ID POWER_LARGEST

// items 统计
typedef struct {
  uint64_t evicted;
  uint64_t evicted_nonzero;
  uint64_t reclaimed;
  uint64_t outofmemory;
  uint64_t tailrepairs;
  uint64_t expired_unfetched; /* items reclaimed but never touched */
  uint64_t evicted_unfetched; /* items evicted but never touched */
  uint64_t evicted_active;    /* items evicted that should have been shuffled */
  uint64_t crawler_reclaimed;
  uint64_t crawler_items_checked;
  uint64_t lrutail_reflocked;
  uint64_t moves_to_cold;
  uint64_t moves_to_warm;
  uint64_t moves_within_lru;
  uint64_t direct_reclaims;
  uint64_t hits_to_hot;
  uint64_t hits_to_warm;
  uint64_t hits_to_cold;
  uint64_t hits_to_temp;
  rel_time_t evicted_time;
} itemstats_t;

// 重要: LRU 链表使用头插法
static item *heads[LARGEST_ID]; // 指向每一个 LRU 链表的头
static item *tails[LARGEST_ID]; // 指向每一个 LRU 链表的尾 //指针数组

static itemstats_t itemstats[LARGEST_ID]; // 每一个 LRU 链表的 item 信息统计
static unsigned int sizes[LARGEST_ID]; // 每一个 LRU 链表的 item 个数
static uint64_t sizes_bytes[LARGEST_ID]; // 每一个 LRU 链表的所有 item 大小
static unsigned int *stats_sizes_hist = NULL;
static uint64_t stats_sizes_cas_min = 0;
static int stats_sizes_buckets = 0;

static volatile int do_run_lru_maintainer_thread = 0;
static int lru_maintainer_initialized = 0;
static pthread_mutex_t lru_maintainer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cas_id_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stats_sizes_lock = PTHREAD_MUTEX_INITIALIZER;

// item 统计清空
void item_stats_reset(void) {
  int i;
  for (i = 0; i < LARGEST_ID; i++) {
    pthread_mutex_lock(&lru_locks[i]);
    memset(&itemstats[i], 0, sizeof(itemstats_t));
    pthread_mutex_unlock(&lru_locks[i]);
  }
}

/* called with class lru lock held */
// LRU 爬虫统计
void do_item_stats_add_crawl(const int i, const uint64_t reclaimed,
                             const uint64_t unfetched, const uint64_t checked) {
  itemstats[i].crawler_reclaimed += reclaimed;
  itemstats[i].expired_unfetched += unfetched;
  itemstats[i].crawler_items_checked += checked;
}

typedef struct _lru_bump_buf {
  struct _lru_bump_buf *prev;
  struct _lru_bump_buf *next;
  pthread_mutex_t mutex;
  bipbuf_t *buf;
  uint64_t dropped;
} lru_bump_buf;

typedef struct {
  item *it;
  uint32_t hv;
} lru_bump_entry;

static lru_bump_buf *bump_buf_head = NULL;
static lru_bump_buf *bump_buf_tail = NULL;
static pthread_mutex_t bump_buf_lock = PTHREAD_MUTEX_INITIALIZER;
/* TODO: tunable? Need bench results */
#define LRU_BUMP_BUF_SIZE 8192

static bool lru_bump_async(lru_bump_buf *b, item *it, uint32_t hv);
static uint64_t lru_total_bumps_dropped(void);

/* Get the next CAS id for a new item. */
/* TODO: refactor some atomics for this. */
// cas id
uint64_t get_cas_id(void) {
  static uint64_t cas_id = 0; //全局静态变量
  pthread_mutex_lock(&cas_id_lock);
  uint64_t next_id = ++cas_id;
  pthread_mutex_unlock(&cas_id_lock);
  return next_id;
}

// 比较当前 item 的时间与 flush 清理缓存命令的时间
int item_is_flushed(item *it) {
  rel_time_t oldest_live = settings.oldest_live;
  uint64_t cas = ITEM_get_cas(it);
  uint64_t oldest_cas = settings.oldest_cas;
  if (oldest_live == 0 || oldest_live > current_time)
    return 0;
  // 该 item 创建在 flush 清理缓存命令之前，说明该 item 已失效
  if ((it->time <= oldest_live) ||
      (oldest_cas != 0 && cas != 0 && cas < oldest_cas)) {
    return 1;
  }
  return 0;
}

// 当前LRU 链表已有 item 大小
static unsigned int temp_lru_size(int slabs_clsid) {
  int id = CLEAR_LRU(slabs_clsid);
  id |= TEMP_LRU;
  unsigned int ret;
  pthread_mutex_lock(&lru_locks[id]);
  ret = sizes_bytes[id];
  pthread_mutex_unlock(&lru_locks[id]);
  return ret;
}

/* Enable this for reference-count debugging. */
#if 0
#define DEBUG_REFCNT(it, op)                                                   \
  fprintf(stderr, "item %x refcnt(%c) %d %c%c%c\n", it, op, it->refcount,      \
          (it->it_flags & ITEM_LINKED) ? 'L' : ' ',                            \
          (it->it_flags & ITEM_SLABBED) ? 'S' : ' ')
#else
#define DEBUG_REFCNT(it, op) while (0)
#endif

/**
 * Generates the variable-sized part of the header for an object.
 *
 * key     - The key
 * nkey    - The length of the key
 * flags   - key flags
 * nbytes  - Number of bytes to hold value and addition CRLF terminator
 * suffix  - Buffer for the "VALUE" line suffix (flags, size).
 * nsuffix - The length of the suffix is stored here.
 *
 * Returns the total size of the header.
 */
// 计算存储一个 item 的大小
static size_t item_make_header(const uint8_t nkey, const unsigned int flags,
                               const int nbytes, char *suffix,
                               uint8_t *nsuffix) {
  if (settings.inline_ascii_response) {
    /* suffix is defined at 40 chars elsewhere.. */
    // nbytes 为value 大小加'\r\n'
    *nsuffix = (uint8_t)snprintf(suffix, 40, " %u %d\r\n", flags, nbytes - 2);
  } else {
    if (flags == 0) {
      *nsuffix = 0;
    } else {
      *nsuffix = sizeof(flags);
    }
  }
  return sizeof(item) + nkey + *nsuffix + nbytes;
}

// 分配 item 机制
item *do_item_alloc_pull(const size_t ntotal, const unsigned int id) {
  item *it = NULL;
  int i;
  /* If no memory is available, attempt a direct LRU juggle/eviction */
  /* This is a race in order to simplify lru_pull_tail; in cases where
   * locked items are on the tail, you want them to fall out and cause
   * occasional OOM's, rather than internally work around them.
   * This also gives one fewer code path for slab alloc/free
   */
  // 为什么 10 次循环?
  for (i = 0; i < 10; i++) {
    uint64_t total_bytes;
    /* Try to reclaim memory first */
    // 默认settings.lru_segmented = true
    if (!settings.lru_segmented) {
      lru_pull_tail(id, COLD_LRU, 0, 0, 0, NULL);
    }
    it = slabs_alloc(ntotal, id, &total_bytes, 0); // 分配item

    // 默认settings.temp_lru = false
    if (settings.temp_lru)
      total_bytes -= temp_lru_size(id);

    // 当slab 内存管理器分配 item 失败
    if (it == NULL) {
      // 从该 item 所属的 LRU 链表尾部开始，进行 LRU 淘汰
      if (lru_pull_tail(id, COLD_LRU, total_bytes, LRU_PULL_EVICT, 0, NULL) <=
          0) {
        if (settings.lru_segmented) {
          lru_pull_tail(id, HOT_LRU, total_bytes, 0, 0, NULL);
        } else {
          break;
        }
      }
    } else {
      break; // slab 内存管理器分配 item 成功，直接跳出循环
    }
  }

  if (i > 0) {
    pthread_mutex_lock(&lru_locks[id]);
    itemstats[id].direct_reclaims += i;
    pthread_mutex_unlock(&lru_locks[id]);
  }

  return it;
}

/* Chain another chunk onto this chunk. */
/* slab mover: if it finds a chunk without ITEM_CHUNK flag, and no ITEM_LINKED
 * flag, it counts as busy and skips.
 * I think it might still not be safe to do linking outside of the slab lock
 */
item_chunk *do_item_alloc_chunk(item_chunk *ch, const size_t bytes_remain) {
  // TODO: Should be a cleaner way of finding real size with slabber calls
  size_t size = bytes_remain + sizeof(item_chunk);
  if (size > settings.slab_chunk_size_max)
    size = settings.slab_chunk_size_max;
  unsigned int id = slabs_clsid(size);

  item_chunk *nch = (item_chunk *)do_item_alloc_pull(size, id);
  if (nch == NULL)
    return NULL;

  // link in.
  // ITEM_CHUNK[ED] bits need to be protected by the slabs lock.
  slabs_mlock();
  nch->head = ch->head;
  ch->next = nch;
  nch->prev = ch;
  nch->next = 0;
  nch->used = 0;
  nch->slabs_clsid = id;
  nch->size = size - sizeof(item_chunk);
  nch->it_flags |= ITEM_CHUNK;
  slabs_munlock();
  return nch;
}

/***
 * 函数功能: 分配一个item
 * key、flags、exptime 三个参数是用户在使用set、add 命令存储一条数据时输入的参数。  
 *  nkey: key 字符串的长度 nbytes:
 * 用户要存储的data 长度+2, 因为在data 的结尾处还要加上"\r\n"   cur_hv:
 * 根据键值key 计算得到的哈希值
 * **/
item *do_item_alloc(char *key, const size_t nkey, const unsigned int flags,
                    const rel_time_t exptime, const int nbytes) {
  uint8_t nsuffix;
  item *it = NULL;
  char suffix[40];
  // Avoid potential underflows.
  if (nbytes < 2) // '\r\n'
    return 0;

  // 计算要存储item 的大小
  /*计算需要分配的空间*/
  size_t ntotal = item_make_header(nkey + 1, flags, nbytes, suffix, &nsuffix);
  if (settings.use_cas) { // 开启 CAS
    ntotal += sizeof(uint64_t);
  }

  // 根据一个 item 的大小来选择合适的 slab class
  // /*该方法返回要保存该数据的SlabClass的ID*/
  unsigned int id = slabs_clsid(ntotal);
  unsigned int hdr_id = 0;
  if (id == 0)
    return 0;

  /* This is a large item. Allocate a header object now, lazily allocate
   *  chunks while reading the upload.
   */
  // 假设申请的 item 大于chunk size(最大值为slab 内存页的一半)
  if (ntotal > settings.slab_chunk_size_max) {
    /* We still link this item into the LRU for the larger slab class, but
     * we're pulling a header from an entirely different slab class. The
     * free routines handle large items specifically.
     */
    int htotal = nkey + 1 + nsuffix + sizeof(item) + sizeof(item_chunk);
    if (settings.use_cas) {
      htotal += sizeof(uint64_t);
    }
    hdr_id = slabs_clsid(htotal);
    it = do_item_alloc_pull(htotal, hdr_id);
    /* setting ITEM_CHUNKED is fine here because we aren't LINKED yet. */
    if (it != NULL)
      it->it_flags |= ITEM_CHUNKED;
  } else {
    it = do_item_alloc_pull(ntotal, id); // 重要
  }

  if (it == NULL) {
    pthread_mutex_lock(&lru_locks[id]);
    itemstats[id].outofmemory++;
    pthread_mutex_unlock(&lru_locks[id]);
    return NULL;
  }

  assert(it->slabs_clsid == 0);
  // assert(it != heads[id]);

  /* Refcount is seeded to 1 by slabs_alloc() */
  it->next = it->prev = 0;

  /* Items are initially loaded into the HOT_LRU. This is '0' but I want at
   * least a note here. Compiler (hopefully?) optimizes this out.
   */
  // 下面就是对item 结构体赋值
  if (settings.temp_lru && exptime - current_time <= settings.temporary_ttl) {
    id |= TEMP_LRU;
  } else if (settings.lru_segmented) {
    id |= HOT_LRU;
  } else {
    /* There is only COLD in compat-mode */
    id |= COLD_LRU;
  }
  it->slabs_clsid = id;

  DEBUG_REFCNT(it, '*');
  it->it_flags |= settings.use_cas ? ITEM_CAS : 0;
  it->nkey = nkey;
  it->nbytes = nbytes;
  memcpy(ITEM_key(it), key, nkey);
  it->exptime = exptime;
  if (settings.inline_ascii_response) {
    memcpy(ITEM_suffix(it), suffix, (size_t)nsuffix);
  } else if (nsuffix > 0) {
    memcpy(ITEM_suffix(it), &flags, sizeof(flags));
  }
  it->nsuffix = nsuffix;

  /* Initialize internal chunk. */
  if (it->it_flags & ITEM_CHUNKED) {
    item_chunk *chunk = (item_chunk *)ITEM_data(it);

    chunk->next = 0;
    chunk->prev = 0;
    chunk->used = 0;
    chunk->size = 0;
    chunk->head = it;
    chunk->orig_clsid = hdr_id;
  }
  it->h_next = 0;

  return it;
}

// 释放一个item，真正调用slabs_free
void item_free(item *it) {
  size_t ntotal = ITEM_ntotal(it);
  unsigned int clsid;
  assert((it->it_flags & ITEM_LINKED) == 0);
  assert(it != heads[it->slabs_clsid]);
  assert(it != tails[it->slabs_clsid]);
  assert(it->refcount == 0);

  /* so slab size changer can tell later if item is already free or not */
  clsid = ITEM_clsid(it);
  DEBUG_REFCNT(it, 'F');
  slabs_free(it, ntotal, clsid);
}

/**
 * Returns true if an item will fit in the cache (its size does not exceed
 * the maximum for a cache entry.)
 */
// 如果item 选中合适的slab class，返回true
bool item_size_ok(const size_t nkey, const int flags, const int nbytes) {
  char prefix[40];
  uint8_t nsuffix;
  if (nbytes < 2)
    return false;

  size_t ntotal = item_make_header(nkey + 1, flags, nbytes, prefix, &nsuffix);
  if (settings.use_cas) {
    ntotal += sizeof(uint64_t);
  }

  return slabs_clsid(ntotal) != 0;
}

// 插入item 到LRU 链表，头插法
static void do_item_link_q(item *it) { /* item is the new head */
  item **head, **tail;
  assert((it->it_flags & ITEM_SLABBED) == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  assert(it != *head);
  assert((*head && *tail) || (*head == 0 && *tail == 0));
  it->prev = 0; // 头插法
  it->next = *head;
  if (it->next)
    it->next->prev = it;
  *head = it; // new item 为对应LRU 链表的第一个节点
  if (*tail == 0)
    *tail = it; // LRU 链表上只有一个new item ，所以尾指针也指向new item
  sizes[it->slabs_clsid]++; // LRU 链表上的item 个数加1
  sizes_bytes[it->slabs_clsid] +=
      ITEM_ntotal(it); // LRU 链表上的增加新增item 大小
  return;
}

// 对item 操作，先对item 所属的slab class 上锁
static void item_link_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_link_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

static void item_link_q_warm(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_link_q(it);
  itemstats[it->slabs_clsid].moves_to_warm++;
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

// 将item 从所属LRU 链表中删除
// 链表操作O(1)
static void do_item_unlink_q(item *it) {
  item **head, **tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next)
    it->next->prev = it->prev;
  if (it->prev)
    it->prev->next = it->next;
  sizes[it->slabs_clsid]--;
  sizes_bytes[it->slabs_clsid] -= ITEM_ntotal(it);
  return;
}

// 删除item 前上锁
static void item_unlink_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_unlink_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

// 将item 插入哈希表和LRU 链表
// hv 哈希值
//哈希表是什么？？？
int do_item_link(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_LINK(ITEM_key(it), it->nkey, it->nbytes);
  assert((it->it_flags & (ITEM_LINKED | ITEM_SLABBED)) ==
         0); // item 已经分配，并没有插入到LRU 链表
  it->it_flags |= ITEM_LINKED;
  it->time = current_time;

  STATS_LOCK();
  stats_state.curr_bytes += ITEM_ntotal(it);
  stats_state.curr_items += 1;
  stats.total_items += 1;
  STATS_UNLOCK();

  /* Allocate a new CAS ID on link. */
  // 为item 分配一个CAS ID
  ITEM_set_cas(it, (settings.use_cas) ? get_cas_id() : 0);
  assoc_insert(it, hv); // 将item 插入哈希表
  item_link_q(it);      // 将item 插入到LRU 链表
  refcount_incr(it);    // 引用计数加1  
  item_stats_sizes_add(it);

  return 1;
}

// 将item 从哈希表和LRU 链表中删除
void do_item_unlink(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_UNLINK(ITEM_key(it), it->nkey, it->nbytes);
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    STATS_LOCK();
    stats_state.curr_bytes -= ITEM_ntotal(it);
    stats_state.curr_items -= 1;
    STATS_UNLOCK();
    item_stats_sizes_remove(it);
    assoc_delete(ITEM_key(it), it->nkey, hv); // 将item 从哈希表删除
    item_unlink_q(it);                        //从LRU 链表中删除该item  
    do_item_remove(it); // item 引用计数为0，归还slab 空间链表
  }
}

/* FIXME: Is it necessary to keep this copy/pasted code? */
void do_item_unlink_nolock(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_UNLINK(ITEM_key(it), it->nkey, it->nbytes);
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    STATS_LOCK();
    stats_state.curr_bytes -= ITEM_ntotal(it);
    stats_state.curr_items -= 1;
    STATS_UNLOCK();
    item_stats_sizes_remove(it);
    assoc_delete(ITEM_key(it), it->nkey, hv);
    do_item_unlink_q(it);
    do_item_remove(it);
  }
}

// 当item 引用计数为0，将item 归还到空闲链表
// 引用计数代表是否有worker 线程在使用这个item
void do_item_remove(item *it) {
  MEMCACHED_ITEM_REMOVE(ITEM_key(it), it->nkey, it->nbytes);
  assert((it->it_flags & ITEM_SLABBED) == 0);
  assert(it->refcount > 0);

  if (refcount_decr(it) == 0) {
    item_free(it);
  }
}

/* Copy/paste to avoid adding two extra branches for all common calls, since
 * _nolock is only used in an uncommon case where we want to relink. */
void do_item_update_nolock(item *it) {
  MEMCACHED_ITEM_UPDATE(ITEM_key(it), it->nkey, it->nbytes);
  if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
    assert((it->it_flags & ITEM_SLABBED) == 0);

    if ((it->it_flags & ITEM_LINKED) != 0) {
      do_item_unlink_q(it);
      it->time = current_time;
      do_item_link_q(it);
    }
  }
}

/* Bump the last accessed time, or relink if we're in compat mode */
// 更新item 操作
void do_item_update(item *it) {
  MEMCACHED_ITEM_UPDATE(ITEM_key(it), it->nkey, it->nbytes);

  /* Hits to COLD_LRU immediately move to WARM. */
  if (settings.lru_segmented) {
    assert((it->it_flags & ITEM_SLABBED) == 0);
    if ((it->it_flags & ITEM_LINKED) != 0) {
      if (ITEM_lruid(it) == COLD_LRU && (it->it_flags & ITEM_ACTIVE)) {
        it->time = current_time;
        item_unlink_q(it);
        it->slabs_clsid = ITEM_clsid(it);
        it->slabs_clsid |= WARM_LRU;
        it->it_flags &= ~ITEM_ACTIVE;
        item_link_q_warm(it);
      } else if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
        it->time = current_time;
      }
    }
  } else if (it->time <
             current_time - ITEM_UPDATE_INTERVAL) { // 大于1分钟才更新item
    assert((it->it_flags & ITEM_SLABBED) == 0);

    // item 在LRU 链表中，更新
    if ((it->it_flags & ITEM_LINKED) != 0) {
      it->time = current_time; // 更新访问时间
      item_unlink_q(it);       // 从LRU 链表中删除item
      item_link_q(it);         // 然后重新插入到LRU 链表
    }
  }
}

// 替换一个新的item
int do_item_replace(item *it, item *new_it, const uint32_t hv) {
  MEMCACHED_ITEM_REPLACE(ITEM_key(it), it->nkey, it->nbytes, ITEM_key(new_it),
                         new_it->nkey, new_it->nbytes);
  assert((it->it_flags & ITEM_SLABBED) == 0);

  do_item_unlink(it, hv);
  return do_item_link(new_it, hv);
}

/*@null@*/
/* This is walking the line of violating lock order, but I think it's safe.
 * If the LRU lock is held, an item in the LRU cannot be wiped and freed.
 * The data could possibly be overwritten, but this is only accessing the
 * headers.
 * It may not be the best idea to leave it like this, but for now it's safe.
 */
char *item_cachedump(const unsigned int slabs_clsid, const unsigned int limit,
                     unsigned int *bytes) {
  unsigned int memlimit = 2 * 1024 * 1024; /* 2MB max response size */
  char *buffer;
  unsigned int bufcurr;
  item *it;
  unsigned int len;
  unsigned int shown = 0;
  char key_temp[KEY_MAX_LENGTH + 1];
  char temp[512];
  unsigned int id = slabs_clsid;
  id |= COLD_LRU;

  pthread_mutex_lock(&lru_locks[id]);
  it = heads[id];

  buffer = malloc((size_t)memlimit);
  if (buffer == 0) {
    return NULL;
  }
  bufcurr = 0;

  while (it != NULL && (limit == 0 || shown < limit)) {
    assert(it->nkey <= KEY_MAX_LENGTH);
    if (it->nbytes == 0 && it->nkey == 0) {
      it = it->next;
      continue;
    }
    /* Copy the key since it may not be null-terminated in the struct */
    strncpy(key_temp, ITEM_key(it), it->nkey);
    key_temp[it->nkey] = 0x00; /* terminate */
    len = snprintf(temp, sizeof(temp), "ITEM %s [%d b; %llu s]\r\n", key_temp,
                   it->nbytes - 2,
                   it->exptime == 0
                       ? 0
                       : (unsigned long long)it->exptime + process_started);
    if (bufcurr + len + 6 > memlimit) /* 6 is END\r\n\0 */
      break;
    memcpy(buffer + bufcurr, temp, len);
    bufcurr += len;
    shown++;
    it = it->next;
  }

  memcpy(buffer + bufcurr, "END\r\n", 6);
  bufcurr += 5;

  *bytes = bufcurr;
  pthread_mutex_unlock(&lru_locks[id]);
  return buffer;
}

/* With refactoring of the various stats code the automover won't need a
 * custom function here.
 */
void fill_item_stats_automove(item_stats_automove *am) {
  int n;
  for (n = 0; n < MAX_NUMBER_OF_SLAB_CLASSES; n++) {
    item_stats_automove *cur = &am[n];

    // outofmemory records into HOT
    int i = n | HOT_LRU;
    pthread_mutex_lock(&lru_locks[i]);
    cur->outofmemory = itemstats[i].outofmemory;
    pthread_mutex_unlock(&lru_locks[i]);

    // evictions and tail age are from COLD
    i = n | COLD_LRU;
    pthread_mutex_lock(&lru_locks[i]);
    cur->evicted = itemstats[i].evicted;
    if (tails[i]) {
      cur->age = current_time - tails[i]->time;
    } else {
      cur->age = 0;
    }
    pthread_mutex_unlock(&lru_locks[i]);
  }
}

void item_stats_totals(ADD_STAT add_stats, void *c) {
  itemstats_t totals;
  memset(&totals, 0, sizeof(itemstats_t));
  int n;
  for (n = 0; n < MAX_NUMBER_OF_SLAB_CLASSES; n++) {
    int x;
    int i;
    for (x = 0; x < 4; x++) {
      i = n | lru_type_map[x];
      pthread_mutex_lock(&lru_locks[i]);
      totals.expired_unfetched += itemstats[i].expired_unfetched;
      totals.evicted_unfetched += itemstats[i].evicted_unfetched;
      totals.evicted_active += itemstats[i].evicted_active;
      totals.evicted += itemstats[i].evicted;
      totals.reclaimed += itemstats[i].reclaimed;
      totals.crawler_reclaimed += itemstats[i].crawler_reclaimed;
      totals.crawler_items_checked += itemstats[i].crawler_items_checked;
      totals.lrutail_reflocked += itemstats[i].lrutail_reflocked;
      totals.moves_to_cold += itemstats[i].moves_to_cold;
      totals.moves_to_warm += itemstats[i].moves_to_warm;
      totals.moves_within_lru += itemstats[i].moves_within_lru;
      totals.direct_reclaims += itemstats[i].direct_reclaims;
      pthread_mutex_unlock(&lru_locks[i]);
    }
  }
  APPEND_STAT("expired_unfetched", "%llu",
              (unsigned long long)totals.expired_unfetched);
  APPEND_STAT("evicted_unfetched", "%llu",
              (unsigned long long)totals.evicted_unfetched);
  if (settings.lru_maintainer_thread) {
    APPEND_STAT("evicted_active", "%llu",
                (unsigned long long)totals.evicted_active);
  }
  APPEND_STAT("evictions", "%llu", (unsigned long long)totals.evicted);
  APPEND_STAT("reclaimed", "%llu", (unsigned long long)totals.reclaimed);
  APPEND_STAT("crawler_reclaimed", "%llu",
              (unsigned long long)totals.crawler_reclaimed);
  APPEND_STAT("crawler_items_checked", "%llu",
              (unsigned long long)totals.crawler_items_checked);
  APPEND_STAT("lrutail_reflocked", "%llu",
              (unsigned long long)totals.lrutail_reflocked);
  if (settings.lru_maintainer_thread) {
    APPEND_STAT("moves_to_cold", "%llu",
                (unsigned long long)totals.moves_to_cold);
    APPEND_STAT("moves_to_warm", "%llu",
                (unsigned long long)totals.moves_to_warm);
    APPEND_STAT("moves_within_lru", "%llu",
                (unsigned long long)totals.moves_within_lru);
    APPEND_STAT("direct_reclaims", "%llu",
                (unsigned long long)totals.direct_reclaims);
    APPEND_STAT("lru_bumps_dropped", "%llu",
                (unsigned long long)lru_total_bumps_dropped());
  }
}

void item_stats(ADD_STAT add_stats, void *c) {
  struct thread_stats thread_stats;
  threadlocal_stats_aggregate(&thread_stats);
  itemstats_t totals;
  int n;
  for (n = 0; n < MAX_NUMBER_OF_SLAB_CLASSES; n++) {
    memset(&totals, 0, sizeof(itemstats_t));
    int x;
    int i;
    unsigned int size = 0;
    unsigned int age = 0;
    unsigned int age_hot = 0;
    unsigned int age_warm = 0;
    unsigned int lru_size_map[4];
    const char *fmt = "items:%d:%s";
    char key_str[STAT_KEY_LEN];
    char val_str[STAT_VAL_LEN];
    int klen = 0, vlen = 0;
    for (x = 0; x < 4; x++) {
      i = n | lru_type_map[x];
      pthread_mutex_lock(&lru_locks[i]);
      totals.evicted += itemstats[i].evicted;
      totals.evicted_nonzero += itemstats[i].evicted_nonzero;
      totals.outofmemory += itemstats[i].outofmemory;
      totals.tailrepairs += itemstats[i].tailrepairs;
      totals.reclaimed += itemstats[i].reclaimed;
      totals.expired_unfetched += itemstats[i].expired_unfetched;
      totals.evicted_unfetched += itemstats[i].evicted_unfetched;
      totals.evicted_active += itemstats[i].evicted_active;
      totals.crawler_reclaimed += itemstats[i].crawler_reclaimed;
      totals.crawler_items_checked += itemstats[i].crawler_items_checked;
      totals.lrutail_reflocked += itemstats[i].lrutail_reflocked;
      totals.moves_to_cold += itemstats[i].moves_to_cold;
      totals.moves_to_warm += itemstats[i].moves_to_warm;
      totals.moves_within_lru += itemstats[i].moves_within_lru;
      totals.direct_reclaims += itemstats[i].direct_reclaims;
      size += sizes[i];
      lru_size_map[x] = sizes[i];
      if (lru_type_map[x] == COLD_LRU && tails[i] != NULL) {
        age = current_time - tails[i]->time;
      } else if (lru_type_map[x] == HOT_LRU && tails[i] != NULL) {
        age_hot = current_time - tails[i]->time;
      } else if (lru_type_map[x] == WARM_LRU && tails[i] != NULL) {
        age_warm = current_time - tails[i]->time;
      }
      if (lru_type_map[x] == COLD_LRU)
        totals.evicted_time = itemstats[i].evicted_time;
      switch (lru_type_map[x]) {
      case HOT_LRU:
        totals.hits_to_hot = thread_stats.lru_hits[i];
        break;
      case WARM_LRU:
        totals.hits_to_warm = thread_stats.lru_hits[i];
        break;
      case COLD_LRU:
        totals.hits_to_cold = thread_stats.lru_hits[i];
        break;
      case TEMP_LRU:
        totals.hits_to_temp = thread_stats.lru_hits[i];
        break;
      }
      pthread_mutex_unlock(&lru_locks[i]);
    }
    if (size == 0)
      continue;
    APPEND_NUM_FMT_STAT(fmt, n, "number", "%u", size);
    if (settings.lru_maintainer_thread) {
      APPEND_NUM_FMT_STAT(fmt, n, "number_hot", "%u", lru_size_map[0]);
      APPEND_NUM_FMT_STAT(fmt, n, "number_warm", "%u", lru_size_map[1]);
      APPEND_NUM_FMT_STAT(fmt, n, "number_cold", "%u", lru_size_map[2]);
      if (settings.temp_lru) {
        APPEND_NUM_FMT_STAT(fmt, n, "number_temp", "%u", lru_size_map[3]);
      }
      APPEND_NUM_FMT_STAT(fmt, n, "age_hot", "%u", age_hot);
      APPEND_NUM_FMT_STAT(fmt, n, "age_warm", "%u", age_warm);
    }
    APPEND_NUM_FMT_STAT(fmt, n, "age", "%u", age);
    APPEND_NUM_FMT_STAT(fmt, n, "evicted", "%llu",
                        (unsigned long long)totals.evicted);
    APPEND_NUM_FMT_STAT(fmt, n, "evicted_nonzero", "%llu",
                        (unsigned long long)totals.evicted_nonzero);
    APPEND_NUM_FMT_STAT(fmt, n, "evicted_time", "%u", totals.evicted_time);
    APPEND_NUM_FMT_STAT(fmt, n, "outofmemory", "%llu",
                        (unsigned long long)totals.outofmemory);
    APPEND_NUM_FMT_STAT(fmt, n, "tailrepairs", "%llu",
                        (unsigned long long)totals.tailrepairs);
    APPEND_NUM_FMT_STAT(fmt, n, "reclaimed", "%llu",
                        (unsigned long long)totals.reclaimed);
    APPEND_NUM_FMT_STAT(fmt, n, "expired_unfetched", "%llu",
                        (unsigned long long)totals.expired_unfetched);
    APPEND_NUM_FMT_STAT(fmt, n, "evicted_unfetched", "%llu",
                        (unsigned long long)totals.evicted_unfetched);
    if (settings.lru_maintainer_thread) {
      APPEND_NUM_FMT_STAT(fmt, n, "evicted_active", "%llu",
                          (unsigned long long)totals.evicted_active);
    }
    APPEND_NUM_FMT_STAT(fmt, n, "crawler_reclaimed", "%llu",
                        (unsigned long long)totals.crawler_reclaimed);
    APPEND_NUM_FMT_STAT(fmt, n, "crawler_items_checked", "%llu",
                        (unsigned long long)totals.crawler_items_checked);
    APPEND_NUM_FMT_STAT(fmt, n, "lrutail_reflocked", "%llu",
                        (unsigned long long)totals.lrutail_reflocked);
    if (settings.lru_maintainer_thread) {
      APPEND_NUM_FMT_STAT(fmt, n, "moves_to_cold", "%llu",
                          (unsigned long long)totals.moves_to_cold);
      APPEND_NUM_FMT_STAT(fmt, n, "moves_to_warm", "%llu",
                          (unsigned long long)totals.moves_to_warm);
      APPEND_NUM_FMT_STAT(fmt, n, "moves_within_lru", "%llu",
                          (unsigned long long)totals.moves_within_lru);
      APPEND_NUM_FMT_STAT(fmt, n, "direct_reclaims", "%llu",
                          (unsigned long long)totals.direct_reclaims);
      APPEND_NUM_FMT_STAT(fmt, n, "hits_to_hot", "%llu",
                          (unsigned long long)totals.hits_to_hot);

      APPEND_NUM_FMT_STAT(fmt, n, "hits_to_warm", "%llu",
                          (unsigned long long)totals.hits_to_warm);

      APPEND_NUM_FMT_STAT(fmt, n, "hits_to_cold", "%llu",
                          (unsigned long long)totals.hits_to_cold);

      APPEND_NUM_FMT_STAT(fmt, n, "hits_to_temp", "%llu",
                          (unsigned long long)totals.hits_to_temp);
    }
  }

  /* getting here means both ascii and binary terminators fit */
  add_stats(NULL, 0, NULL, 0, c);
}

bool item_stats_sizes_status(void) {
  bool ret = false;
  mutex_lock(&stats_sizes_lock);
  if (stats_sizes_hist != NULL)
    ret = true;
  mutex_unlock(&stats_sizes_lock);
  return ret;
}

void item_stats_sizes_init(void) {
  if (stats_sizes_hist != NULL)
    return;
  stats_sizes_buckets = settings.item_size_max / 32 + 1;
  stats_sizes_hist = calloc(stats_sizes_buckets, sizeof(int));
  stats_sizes_cas_min = (settings.use_cas) ? get_cas_id() : 0;
}

void item_stats_sizes_enable(ADD_STAT add_stats, void *c) {
  mutex_lock(&stats_sizes_lock);
  if (!settings.use_cas) {
    APPEND_STAT("sizes_status", "error", "");
    APPEND_STAT("sizes_error", "cas_support_disabled", "");
  } else if (stats_sizes_hist == NULL) {
    item_stats_sizes_init();
    if (stats_sizes_hist != NULL) {
      APPEND_STAT("sizes_status", "enabled", "");
    } else {
      APPEND_STAT("sizes_status", "error", "");
      APPEND_STAT("sizes_error", "no_memory", "");
    }
  } else {
    APPEND_STAT("sizes_status", "enabled", "");
  }
  mutex_unlock(&stats_sizes_lock);
}

void item_stats_sizes_disable(ADD_STAT add_stats, void *c) {
  mutex_lock(&stats_sizes_lock);
  if (stats_sizes_hist != NULL) {
    free(stats_sizes_hist);
    stats_sizes_hist = NULL;
  }
  APPEND_STAT("sizes_status", "disabled", "");
  mutex_unlock(&stats_sizes_lock);
}

void item_stats_sizes_add(item *it) {
  if (stats_sizes_hist == NULL || stats_sizes_cas_min > ITEM_get_cas(it))
    return;
  int ntotal = ITEM_ntotal(it);
  int bucket = ntotal / 32;
  if ((ntotal % 32) != 0)
    bucket++;
  if (bucket < stats_sizes_buckets)
    stats_sizes_hist[bucket]++;
}

/* I think there's no way for this to be accurate without using the CAS value.
 * Since items getting their time value bumped will pass this validation.
 */
void item_stats_sizes_remove(item *it) {
  if (stats_sizes_hist == NULL || stats_sizes_cas_min > ITEM_get_cas(it))
    return;
  int ntotal = ITEM_ntotal(it);
  int bucket = ntotal / 32;
  if ((ntotal % 32) != 0)
    bucket++;
  if (bucket < stats_sizes_buckets)
    stats_sizes_hist[bucket]--;
}

/** dumps out a list of objects of each size, with granularity of 32 bytes */
/*@null@*/
/* Locks are correct based on a technicality. Holds LRU lock while doing the
 * work, so items can't go invalid, and it's only looking at header sizes
 * which don't change.
 */
void item_stats_sizes(ADD_STAT add_stats, void *c) {
  mutex_lock(&stats_sizes_lock);

  if (stats_sizes_hist != NULL) {
    int i;
    for (i = 0; i < stats_sizes_buckets; i++) {
      if (stats_sizes_hist[i] != 0) {
        char key[8];
        snprintf(key, sizeof(key), "%d", i * 32);
        APPEND_STAT(key, "%u", stats_sizes_hist[i]);
      }
    }
  } else {
    APPEND_STAT("sizes_status", "disabled", "");
  }

  add_stats(NULL, 0, NULL, 0, c);
  mutex_unlock(&stats_sizes_lock);
}

/** wrapper around assoc_find which does the lazy expiration logic */
// LRU -懒惰删除: Memcached 并不主动去检查item 是否失效，而是客户端再次请求该item 才
// 
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//  断失效(没有数量限制，如果满了扩容，leetcode算法上不是这样的)
item *do_item_get(const char *key, const size_t nkey, const uint32_t hv,
                  conn *c, const bool do_update) {
  item *it = assoc_find(key, nkey, hv); // 在哈希表上查找item
  ////从Hash表中获取相应的结构
  if (it != NULL) {
    refcount_incr(it); ////item的引用次数+1
    /* Optimization for slab reassignment. prevents popular items from
     * jamming in busy wait. Can only do this here to satisfy lock order
     * of item_lock, slabs_lock. */
    /* This was made unsafe by removal of the cache_lock:
     * slab_rebalance_signal and slab_rebal.* are modified in a separate
     * thread under slabs_lock. If slab_rebalance_signal = 1, slab_start =
     * NULL (0), but slab_end is still equal to some value, this would end
     * up unlinking every item fetched.
     * This is either an acceptable loss, or if slab_rebalance_signal is
     * true, slab_start/slab_end should be put behind the slabs_lock.
     * Which would cause a huge potential slowdown.
     * Could also use a specific lock for slab_rebal.* and
     * slab_rebalance_signal (shorter lock?)
     */
    /*if (slab_rebalance_signal &&
        ((void *)it >= slab_rebal.slab_start && (void *)it <
    slab_rebal.slab_end)) { do_item_unlink(it, hv); do_item_remove(it); it =
    NULL;
    }*/
  }
  int was_found = 0;

  if (settings.verbose > 2) {
    int ii;
    if (it == NULL) {
      fprintf(stderr, "> NOT FOUND ");
    } else {
      fprintf(stderr, "> FOUND KEY ");
    }
    for (ii = 0; ii < nkey; ++ii) {
      fprintf(stderr, "%c", key[ii]);
    }
  }

  if (it != NULL) {
    was_found = 1;
    // 该item 已经被flush 清理
    if (item_is_flushed(it)) {
      do_item_unlink(it, hv); // 在LRU 链表和哈希表上删除
      do_item_remove(it);     // slab 内存中删除item
      it = NULL;
      pthread_mutex_lock(&c->thread->stats.mutex);
      c->thread->stats.get_flushed++;
      pthread_mutex_unlock(&c->thread->stats.mutex);
      if (settings.verbose > 2) {
        fprintf(stderr, " -nuked by flush");
      }
      was_found = 2;
    } else if (it->exptime != 0 &&
               it->exptime <= current_time) { // 检查item 是否过期
      do_item_unlink(it, hv);                 // item 过期，删除
      do_item_remove(it);
      it = NULL;
      pthread_mutex_lock(&c->thread->stats.mutex);
      c->thread->stats.get_expired++;
      pthread_mutex_unlock(&c->thread->stats.mutex);
      if (settings.verbose > 2) {
        fprintf(stderr, " -nuked by expire");
      }
      was_found = 3;
    } else {
      if (do_update) {
        /* We update the hit markers only during fetches.
         * An item needs to be hit twice overall to be considered
         * ACTIVE, but only needs a single hit to maintain activity
         * afterward.
         * FETCHED tells if an item has ever been active.
         */
        if (settings.lru_segmented) {
          if ((it->it_flags & ITEM_ACTIVE) == 0) {
            if ((it->it_flags & ITEM_FETCHED) == 0) {
              it->it_flags |= ITEM_FETCHED;
            } else {
              it->it_flags |= ITEM_ACTIVE;
              if (ITEM_lruid(it) != COLD_LRU) {
                do_item_update(it); // bump LA time
              } else if (!lru_bump_async(c->thread->lru_bump_buf, it, hv)) {
                // add flag before async bump to avoid race.
                it->it_flags &= ~ITEM_ACTIVE;
              }
            }
          }
        } else {
          it->it_flags |= ITEM_FETCHED;
          do_item_update(it);
        }
      }
      DEBUG_REFCNT(it, '+');
    }
  }

  if (settings.verbose > 2)
    fprintf(stderr, "\n");
  /* For now this is in addition to the above verbose logging. */
  LOGGER_LOG(c->thread->l, LOG_FETCHERS, LOGGER_ITEM_GET, NULL, was_found, key,
             nkey, (it) ? ITEM_clsid(it) : 0);

  return it;
}

// 将该item 重新更新
item *do_item_touch(const char *key, size_t nkey, uint32_t exptime,
                    const uint32_t hv, conn *c) {
  item *it = do_item_get(key, nkey, hv, c, DO_UPDATE);
  if (it != NULL) {
    it->exptime = exptime;
  }
  return it;
}

/*** LRU MAINTENANCE THREAD ***/
/* Returns number of items remove, expired, or evicted.
 * Callable from worker threads or the LRU maintainer thread */
// 从LRU 链表尾部开始搜索item
int lru_pull_tail(const int orig_id, const int cur_lru,
                  const uint64_t total_bytes, const uint8_t flags,
                  const rel_time_t max_age,
                  struct lru_pull_tail_return *ret_it) {
  item *it = NULL;
  int id = orig_id;
  int removed = 0;
  if (id == 0)
    return 0;

  int tries = 5; // 尝试5 次
  item *search;
  item *next_it;
  void *hold_lock = NULL;
  unsigned int move_to_lru = 0;
  ; //表示要移动的LRU类型
  uint64_t limit = 0;

  id |= cur_lru;
  pthread_mutex_lock(&lru_locks[id]);
  search = tails[id]; // LRU 链表的尾部地址 
  /* We walk up *only* for locked items, and if bottom is expired. */
  /* 从尾部开始搜索，尝试5次，只走查可以被locked的item，一旦尾部的Item满足条件，跳出循环 */
   

  for (; tries > 0 && search != NULL; tries--, search = next_it) {
    /* we might relink search mid-loop, so search->prev isn't reliable */
    next_it = search->prev;
    if (search->nbytes == 0 && search->nkey == 0 && search->it_flags == 1) {
      /* We are a crawler, ignore it. */
      if (flags & LRU_PULL_CRAWL_BLOCKS) {
        pthread_mutex_unlock(&lru_locks[id]);
        return 0;
      }
      tries++;
      continue;
    }
    uint32_t hv = hash(ITEM_key(search), search->nkey); // 哈希值
    /* Attempt to hash item lock the "search" item. If locked, no
     * other callers can incr the refcount. Also skip ourselves. */
    if ((hold_lock = item_trylock(hv)) == NULL)
      continue;
    /* Now see if the item is refcount locked */
    //一般情况下search->refcount为1，如果增加了refcount之后，不等于2，说明item被其它的worker线程锁定  
    // refcount往上加1，是锁定当前的item，如果不等于2，说明锁定失败  
    if (refcount_incr(search) != 2) {
      /* Note pathological case with ref'ed items in tail.
       * Can still unlink the item, but it won't be reusable yet */
      itemstats[id].lrutail_reflocked++;
      /* In case of refcount leaks, enable for quick workaround. */
      /* WARNING: This can cause terrible corruption */
      if (settings.tail_repair_time &&
          search->time + settings.tail_repair_time < current_time) {
        itemstats[id].tailrepairs++;
        search->refcount = 1;
        /* This will call item_remove -> item_free since refcnt is 1 */
        do_item_unlink_nolock(search, hv);
        item_trylock_unlock(hold_lock);
        continue;
      }
    }

    /* Expired or flushed */
    // 如果该item 过期或者被flush 清理掉
    if ((search->exptime != 0 && search->exptime < current_time) ||
        item_is_flushed(search)) {
      itemstats[id].reclaimed++; // 回收统计
      if ((search->it_flags & ITEM_FETCHED) == 0) {
        itemstats[id].expired_unfetched++;
      }
      /* refcnt 2 -> 1 */
      do_item_unlink_nolock(search, hv);
      /* refcnt 1 -> 0 -> item_free */
      do_item_remove(search);
      item_trylock_unlock(hold_lock);
      removed++;

      /* If all we're finding are expired, can keep going */
      continue;
    }

    /* If we're HOT_LRU or WARM_LRU and over size limit, send to COLD_LRU.
     * If we're COLD_LRU, send to WARM_LRU unless we need to evict
     */
    switch (cur_lru) {
    case HOT_LRU:
      limit = total_bytes * settings.hot_lru_pct / 100;
    case WARM_LRU:
      if (limit == 0)
        limit = total_bytes * settings.warm_lru_pct / 100;
      /* Rescue ACTIVE items aggressively */
      if ((search->it_flags & ITEM_ACTIVE) != 0) {
        search->it_flags &= ~ITEM_ACTIVE;
        removed++;
        if (cur_lru == WARM_LRU) {
          itemstats[id].moves_within_lru++;
          do_item_update_nolock(search);
          do_item_remove(search);
          item_trylock_unlock(hold_lock);
        } else {
          /* Active HOT_LRU items flow to WARM */
          itemstats[id].moves_to_warm++;
          move_to_lru = WARM_LRU;
          do_item_unlink_q(search);
          it = search;
        }
      } else if (sizes_bytes[id] > limit ||
                 current_time - search->time > max_age) {
        itemstats[id].moves_to_cold++;
        move_to_lru = COLD_LRU;
        do_item_unlink_q(search);
        it = search;
        removed++;
        break;
      } else {
        /* Don't want to move to COLD, not active, bail out */
        it = search;
      }
      break;
    case COLD_LRU:
      it = search; /* No matter what, we're stopping */
                   // flag = LRU_PULL_EVICT
                   // 如果遍历item 没有过期，执行LRU 淘汰
      if (flags & LRU_PULL_EVICT) {
        //如果设置了不允许LRU 淘汰，则返回ERROR  
        if (settings.evict_to_free == 0) {
          /* Don't think we need a counter for this. It'll OOM.  */
          break;
        }
        itemstats[id].evicted++;
        itemstats[id].evicted_time = current_time - search->time; // < 0  ??
        if (search->exptime != 0)
          itemstats[id].evicted_nonzero++;
        if ((search->it_flags & ITEM_FETCHED) == 0) {
          itemstats[id].evicted_unfetched++;
        }
        if ((search->it_flags & ITEM_ACTIVE)) {
          itemstats[id].evicted_active++;
        }
        LOGGER_LOG(NULL, LOG_EVICTIONS, LOGGER_EVICTION, search);
        do_item_unlink_nolock(search, hv); // LRU 淘汰，就是回收内存
        removed++;
        if (settings.slab_automove == 2) {
          slabs_reassign(-1, orig_id);
        }
      } else if (flags & LRU_PULL_RETURN_ITEM) {
        /* Keep a reference to this item and return it. */
        ret_it->it = it;
        ret_it->hv = hv;
      } else if ((search->it_flags & ITEM_ACTIVE) != 0 &&
                 settings.lru_segmented) {
        itemstats[id].moves_to_warm++;
        search->it_flags &= ~ITEM_ACTIVE;
        move_to_lru = WARM_LRU;
        do_item_unlink_q(search);
        removed++;
      }
      break;
    case TEMP_LRU:
      it = search; /* Kill the loop. Parent only interested in reclaims */
      break;
    }
    if (it != NULL)
      break;
  }

  pthread_mutex_unlock(&lru_locks[id]);

  if (it != NULL) {
    if (move_to_lru) {
      it->slabs_clsid = ITEM_clsid(it);
      it->slabs_clsid |= move_to_lru; // slab_clsid 改变了
      item_link_q(it);                //
    }
    if ((flags & LRU_PULL_RETURN_ITEM) == 0) {
      do_item_remove(it);
      item_trylock_unlock(hold_lock);
    }
  }

  return removed; // 返回回收item 的个数
}

/* TODO: Third place this code needs to be deduped */
static void lru_bump_buf_link_q(lru_bump_buf *b) {
  pthread_mutex_lock(&bump_buf_lock);
  assert(b != bump_buf_head);

  b->prev = 0;
  b->next = bump_buf_head;
  if (b->next)
    b->next->prev = b;
  bump_buf_head = b;
  if (bump_buf_tail == 0)
    bump_buf_tail = b;
  pthread_mutex_unlock(&bump_buf_lock);
  return;
}

void *item_lru_bump_buf_create(void) {
  lru_bump_buf *b = calloc(1, sizeof(lru_bump_buf));
  if (b == NULL) {
    return NULL;
  }

  b->buf = bipbuf_new(sizeof(lru_bump_entry) * LRU_BUMP_BUF_SIZE);
  if (b->buf == NULL) {
    free(b);
    return NULL;
  }

  pthread_mutex_init(&b->mutex, NULL);

  lru_bump_buf_link_q(b);
  return b;
}

static bool lru_bump_async(lru_bump_buf *b, item *it, uint32_t hv) {
  bool ret = true;
  refcount_incr(it);
  pthread_mutex_lock(&b->mutex);
  lru_bump_entry *be =
      (lru_bump_entry *)bipbuf_request(b->buf, sizeof(lru_bump_entry));
  if (be != NULL) {
    be->it = it;
    be->hv = hv;
    if (bipbuf_push(b->buf, sizeof(lru_bump_entry)) == 0) {
      ret = false;
      b->dropped++;
    }
  } else {
    ret = false;
    b->dropped++;
  }
  if (!ret) {
    refcount_decr(it);
  }
  pthread_mutex_unlock(&b->mutex);
  return ret;
}

/* TODO: Might be worth a micro-optimization of having bump buffers link
 * themselves back into the central queue when queue goes from zero to
 * non-zero, then remove from list if zero more than N times.
 * If very few hits on cold this would avoid extra memory barriers from LRU
 * maintainer thread. If many hits, they'll just stay in the list.
 */
static bool lru_maintainer_bumps(void) {
  lru_bump_buf *b;
  lru_bump_entry *be;
  unsigned int size;
  unsigned int todo;
  bool bumped = false;
  pthread_mutex_lock(&bump_buf_lock);
  for (b = bump_buf_head; b != NULL; b = b->next) {
    pthread_mutex_lock(&b->mutex);
    be = (lru_bump_entry *)bipbuf_peek_all(b->buf, &size);
    pthread_mutex_unlock(&b->mutex);

    if (be == NULL) {
      continue;
    }
    todo = size;
    bumped = true;

    while (todo) {
      item_lock(be->hv);
      do_item_update(be->it);
      do_item_remove(be->it);
      item_unlock(be->hv);
      be++;
      todo -= sizeof(lru_bump_entry);
    }

    pthread_mutex_lock(&b->mutex);
    be = (lru_bump_entry *)bipbuf_poll(b->buf, size);
    pthread_mutex_unlock(&b->mutex);
  }
  pthread_mutex_unlock(&bump_buf_lock);
  return bumped;
}

static uint64_t lru_total_bumps_dropped(void) {
  uint64_t total = 0;
  lru_bump_buf *b;
  pthread_mutex_lock(&bump_buf_lock);
  for (b = bump_buf_head; b != NULL; b = b->next) {
    pthread_mutex_lock(&b->mutex);
    total += b->dropped;
    pthread_mutex_unlock(&b->mutex);
  }
  pthread_mutex_unlock(&bump_buf_lock);
  return total;
}

/* Loop up to N times:
 * If too many items are in HOT_LRU, push to COLD_LRU
 * If too many items are in WARM_LRU, push to COLD_LRU
 * If too many items are in COLD_LRU, poke COLD_LRU tail
 * 1000 loops with 1ms min sleep gives us under 1m items shifted/sec. The
 * locks can't handle much more than that. Leaving a TODO for how to
 * autoadjust in the future.
 */
static int lru_maintainer_juggle(const int slabs_clsid) {
  int i;
  int did_moves = 0;
  uint64_t total_bytes = 0;
  unsigned int chunks_perslab = 0;
  // unsigned int chunks_free = 0;
  /* TODO: if free_chunks below high watermark, increase aggressiveness */
  slabs_available_chunks(slabs_clsid, NULL, &total_bytes, &chunks_perslab);
  if (settings.temp_lru) {
    /* Only looking for reclaims. Run before we size the LRU. */
    for (i = 0; i < 500; i++) {
      if (lru_pull_tail(slabs_clsid, TEMP_LRU, 0, 0, 0, NULL) <= 0) {
        break;
      } else {
        did_moves++;
      }
    }
    total_bytes -= temp_lru_size(slabs_clsid);
  }

  rel_time_t cold_age = 0;
  rel_time_t hot_age = 0;
  rel_time_t warm_age = 0;
  /* If LRU is in flat mode, force items to drain into COLD via max age */
  if (settings.lru_segmented) {
    pthread_mutex_lock(&lru_locks[slabs_clsid | COLD_LRU]);
    if (tails[slabs_clsid | COLD_LRU]) {
      cold_age = current_time - tails[slabs_clsid | COLD_LRU]->time;
    }
    pthread_mutex_unlock(&lru_locks[slabs_clsid | COLD_LRU]);
    hot_age = cold_age * settings.hot_max_factor;
    warm_age = cold_age * settings.warm_max_factor;
  }

  /* Juggle HOT/WARM up to N times */
  for (i = 0; i < 500; i++) {
    int do_more = 0;
    if (lru_pull_tail(slabs_clsid, HOT_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS,
                      hot_age, NULL) ||
        lru_pull_tail(slabs_clsid, WARM_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS,
                      warm_age, NULL)) {
      do_more++;
    }
    if (settings.lru_segmented) {
      do_more += lru_pull_tail(slabs_clsid, COLD_LRU, total_bytes,
                               LRU_PULL_CRAWL_BLOCKS, 0, NULL);
    }
    if (do_more == 0)
      break;
    did_moves++;
  }
  return did_moves;
}

/* Will crawl all slab classes a minimum of once per hour */
#define MAX_MAINTCRAWL_WAIT 60 * 60

/* Hoping user input will improve this function. This is all a wild guess.
 * Operation: Kicks crawler for each slab id. Crawlers take some statistics as
 * to items with nonzero expirations. It then buckets how many items will
 * expire per minute for the next hour.
 * This function checks the results of a run, and if it things more than 1% of
 * expirable objects are ready to go, kick the crawler again to reap.
 * It will also kick the crawler once per minute regardless, waiting a minute
 * longer for each time it has no work to do, up to an hour wait time.
 * The latter is to avoid newly started daemons from waiting too long before
 * retrying a crawl.
 */
static void lru_maintainer_crawler_check(struct crawler_expired_data *cdata,
                                         logger *l) {
  int i;
  static rel_time_t next_crawls[POWER_LARGEST];
  static rel_time_t next_crawl_wait[POWER_LARGEST];
  uint8_t todo[POWER_LARGEST];
  memset(todo, 0, sizeof(uint8_t) * POWER_LARGEST);
  bool do_run = false;
  unsigned int tocrawl_limit = 0;

  // TODO: If not segmented LRU, skip non-cold
  for (i = POWER_SMALLEST; i < POWER_LARGEST; i++) {
    crawlerstats_t *s = &cdata->crawlerstats[i];
    /* We've not successfully kicked off a crawl yet. */
    if (s->run_complete) {
      char *lru_name = "na";
      pthread_mutex_lock(&cdata->lock);
      int x;
      /* Should we crawl again? */
      uint64_t possible_reclaims = s->seen - s->noexp;
      uint64_t available_reclaims = 0;
      /* Need to think we can free at least 1% of the items before
       * crawling. */
      /* FIXME: Configurable? */
      uint64_t low_watermark = (possible_reclaims / 100) + 1;
      rel_time_t since_run = current_time - s->end_time;
      /* Don't bother if the payoff is too low. */
      for (x = 0; x < 60; x++) {
        available_reclaims += s->histo[x];
        if (available_reclaims > low_watermark) {
          if (next_crawl_wait[i] < (x * 60)) {
            next_crawl_wait[i] += 60;
          } else if (next_crawl_wait[i] >= 60) {
            next_crawl_wait[i] -= 60;
          }
          break;
        }
      }

      if (available_reclaims == 0) {
        next_crawl_wait[i] += 60;
      }

      if (next_crawl_wait[i] > MAX_MAINTCRAWL_WAIT) {
        next_crawl_wait[i] = MAX_MAINTCRAWL_WAIT;
      }

      next_crawls[i] = current_time + next_crawl_wait[i] + 5;
      switch (GET_LRU(i)) {
      case HOT_LRU:
        lru_name = "hot";
        break;
      case WARM_LRU:
        lru_name = "warm";
        break;
      case COLD_LRU:
        lru_name = "cold";
        break;
      case TEMP_LRU:
        lru_name = "temp";
        break;
      }
      LOGGER_LOG(l, LOG_SYSEVENTS, LOGGER_CRAWLER_STATUS, NULL, CLEAR_LRU(i),
                 lru_name, (unsigned long long)low_watermark,
                 (unsigned long long)available_reclaims,
                 (unsigned int)since_run, next_crawls[i] - current_time,
                 s->end_time - s->start_time, s->seen, s->reclaimed);
      // Got our calculation, avoid running until next actual run.
      s->run_complete = false;
      pthread_mutex_unlock(&cdata->lock);
    }
    if (current_time > next_crawls[i]) {
      pthread_mutex_lock(&lru_locks[i]);
      if (sizes[i] > tocrawl_limit) {
        tocrawl_limit = sizes[i];
      }
      pthread_mutex_unlock(&lru_locks[i]);
      todo[i] = 1;
      do_run = true;
      next_crawls[i] = current_time + 5; // minimum retry wait.
    }
  }
  if (do_run) {
    if (settings.lru_crawler_tocrawl &&
        settings.lru_crawler_tocrawl < tocrawl_limit) {
      tocrawl_limit = settings.lru_crawler_tocrawl;
    }
    lru_crawler_start(todo, tocrawl_limit, CRAWLER_AUTOEXPIRE, cdata, NULL, 0);
  }
}

static pthread_t lru_maintainer_tid;

#define MAX_LRU_MAINTAINER_SLEEP 1000000
#define MIN_LRU_MAINTAINER_SLEEP 1000

static void *lru_maintainer_thread(void *arg) {
  int i;
  useconds_t to_sleep = MIN_LRU_MAINTAINER_SLEEP;
  useconds_t last_sleep = MIN_LRU_MAINTAINER_SLEEP;
  rel_time_t last_crawler_check = 0;
  rel_time_t last_automove_check = 0;
  useconds_t next_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  useconds_t backoff_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  struct crawler_expired_data *cdata =
      calloc(1, sizeof(struct crawler_expired_data));
  if (cdata == NULL) {
    fprintf(stderr,
            "Failed to allocate crawler data for LRU maintainer thread\n");
    abort();
  }
  pthread_mutex_init(&cdata->lock, NULL);
  cdata->crawl_complete = true; // kick off the crawler.
  logger *l = logger_create();
  if (l == NULL) {
    fprintf(stderr, "Failed to allocate logger for LRU maintainer thread\n");
    abort();
  }

  double last_ratio = settings.slab_automove_ratio;
  void *am = slab_automove_init(settings.slab_automove_window,
                                settings.slab_automove_ratio);

  pthread_mutex_lock(&lru_maintainer_lock);
  if (settings.verbose > 2)
    fprintf(stderr, "Starting LRU maintainer background thread\n");
  while (do_run_lru_maintainer_thread) {
    pthread_mutex_unlock(&lru_maintainer_lock);
    if (to_sleep)
      usleep(to_sleep);
    pthread_mutex_lock(&lru_maintainer_lock);
    /* A sleep of zero counts as a minimum of a 1ms wait */
    last_sleep = to_sleep > 1000 ? to_sleep : 1000;
    to_sleep = MAX_LRU_MAINTAINER_SLEEP;

    STATS_LOCK();
    stats.lru_maintainer_juggles++;
    STATS_UNLOCK();

    /* Each slab class gets its own sleep to avoid hammering locks */
    for (i = POWER_SMALLEST; i < MAX_NUMBER_OF_SLAB_CLASSES; i++) {
      next_juggles[i] =
          next_juggles[i] > last_sleep ? next_juggles[i] - last_sleep : 0;

      if (next_juggles[i] > 0) {
        // Sleep the thread just for the minimum amount (or not at all)
        if (next_juggles[i] < to_sleep)
          to_sleep = next_juggles[i];
        continue;
      }

      int did_moves = lru_maintainer_juggle(i);
      if (did_moves == 0) {
        if (backoff_juggles[i] != 0) {
          backoff_juggles[i] += backoff_juggles[i] / 8;
        } else {
          backoff_juggles[i] = MIN_LRU_MAINTAINER_SLEEP;
        }
        if (backoff_juggles[i] > MAX_LRU_MAINTAINER_SLEEP)
          backoff_juggles[i] = MAX_LRU_MAINTAINER_SLEEP;
      } else if (backoff_juggles[i] > 0) {
        backoff_juggles[i] /= 2;
        if (backoff_juggles[i] < MIN_LRU_MAINTAINER_SLEEP) {
          backoff_juggles[i] = 0;
        }
      }
      next_juggles[i] = backoff_juggles[i];
      if (next_juggles[i] < to_sleep)
        to_sleep = next_juggles[i];
    }

    /* Minimize the sleep if we had async LRU bumps to process */
    if (settings.lru_segmented && lru_maintainer_bumps() && to_sleep > 1000) {
      to_sleep = 1000;
    }

    /* Once per second at most */
    if (settings.lru_crawler && last_crawler_check != current_time) {
      lru_maintainer_crawler_check(cdata, l);
      last_crawler_check = current_time;
    }

    if (settings.slab_automove == 1 && last_automove_check != current_time) {
      if (last_ratio != settings.slab_automove_ratio) {
        slab_automove_free(am);
        am = slab_automove_init(settings.slab_automove_window,
                                settings.slab_automove_ratio);
        last_ratio = settings.slab_automove_ratio;
      }
      int src, dst;
      slab_automove_run(am, &src, &dst);
      if (src != -1 && dst != -1) {
        slabs_reassign(src, dst);
        LOGGER_LOG(l, LOG_SYSEVENTS, LOGGER_SLAB_MOVE, NULL, src, dst);
      }
      // dst == 0 means reclaim to global pool, be more aggressive
      if (dst != 0) {
        last_automove_check = current_time;
      } else if (dst == 0) {
        // also ensure we minimize the thread sleep
        to_sleep = 1000;
      }
    }
  }
  pthread_mutex_unlock(&lru_maintainer_lock);
  slab_automove_free(am);
  // LRU crawler *must* be stopped.
  free(cdata);
  if (settings.verbose > 2)
    fprintf(stderr, "LRU maintainer thread stopping\n");

  return NULL;
}

int stop_lru_maintainer_thread(void) {
  int ret;
  pthread_mutex_lock(&lru_maintainer_lock);
  /* LRU thread is a sleep loop, will die on its own */
  do_run_lru_maintainer_thread = 0;
  pthread_mutex_unlock(&lru_maintainer_lock);
  if ((ret = pthread_join(lru_maintainer_tid, NULL)) != 0) {
    fprintf(stderr, "Failed to stop LRU maintainer thread: %s\n",
            strerror(ret));
    return -1;
  }
  settings.lru_maintainer_thread = false;
  return 0;
}

int start_lru_maintainer_thread(void *arg) {
  int ret;

  pthread_mutex_lock(&lru_maintainer_lock);
  do_run_lru_maintainer_thread = 1;
  settings.lru_maintainer_thread = true;
  if ((ret = pthread_create(&lru_maintainer_tid, NULL, lru_maintainer_thread,
                            arg)) != 0) {
    fprintf(stderr, "Can't create LRU maintainer thread: %s\n", strerror(ret));
    pthread_mutex_unlock(&lru_maintainer_lock);
    return -1;
  }
  pthread_mutex_unlock(&lru_maintainer_lock);

  return 0;
}

/* If we hold this lock, crawler can't wake up or move */
void lru_maintainer_pause(void) { pthread_mutex_lock(&lru_maintainer_lock); }

void lru_maintainer_resume(void) { pthread_mutex_unlock(&lru_maintainer_lock); }

// LRU maintainer 初始化
// lru_maintainer_lock 互斥锁初始化
int init_lru_maintainer(void) {
  if (lru_maintainer_initialized == 0) {
    pthread_mutex_init(&lru_maintainer_lock, NULL);
    lru_maintainer_initialized = 1;
  }
  return 0;
}

/* Tail linkers and crawler for the LRU crawler. */
void do_item_linktail_q(item *it) { /* item is the new tail */
  item **head, **tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  // assert(*tail != 0);
  assert(it != *tail);
  assert((*head && *tail) || (*head == 0 && *tail == 0));
  it->prev = *tail;
  it->next = 0;
  if (it->prev) {
    assert(it->prev->next == 0);
    it->prev->next = it;
  }
  *tail = it;
  if (*head == 0)
    *head = it;
  return;
}

void do_item_unlinktail_q(item *it) {
  item **head, **tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next)
    it->next->prev = it->prev;
  if (it->prev)
    it->prev->next = it->next;
  return;
}

/* This is too convoluted, but it's a difficult shuffle. Try to rewrite it
 * more clearly. */
item *do_item_crawl_q(item *it) {
  item **head, **tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  /* We've hit the head, pop off */
  if (it->prev == 0) {
    assert(*head == it);
    if (it->next) {
      *head = it->next;
      assert(it->next->prev == it);
      it->next->prev = 0;
    }
    return NULL; /* Done */
  }

  /* Swing ourselves in front of the next item */
  /* NB: If there is a prev, we can't be the head */
  assert(it->prev != it);
  if (it->prev) {
    if (*head == it->prev) {
      /* Prev was the head, now we're the head */
      *head = it;
    }
    if (*tail == it) {
      /* We are the tail, now they are the tail */
      *tail = it->prev;
    }
    assert(it->next != it);
    if (it->next) {
      assert(it->prev->next == it);
      it->prev->next = it->next;
      it->next->prev = it->prev;
    } else {
      /* Tail. Move this above? */
      it->prev->next = 0;
    }
    /* prev->prev's next is it->prev */
    it->next = it->prev;
    it->prev = it->next->prev;
    it->next->prev = it;
    /* New it->prev now, if we're not at the head. */
    if (it->prev) {
      it->prev->next = it;
    }
  }
  assert(it->next != it);
  assert(it->prev != it);

  return it->next; /* success */
}
