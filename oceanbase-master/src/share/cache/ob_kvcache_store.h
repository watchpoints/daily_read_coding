/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_CACHE_OB_KVCACHE_STORE_H_
#define OCEANBASE_CACHE_OB_KVCACHE_STORE_H_

#include "lib/allocator/ob_retire_station.h"
#include "lib/resource/ob_cache_washer.h"
#include "ob_kvcache_struct.h"
#include "ob_kvcache_inst_map.h"
#include "ob_cache_utils.h"

namespace oceanbase {
namespace observer {
class ObServerMemoryCutter;
}
namespace common {
template <typename MBWrapper>
class ObIKVCacheStore {
public:
  int store(ObKVCacheInst& inst, const ObIKVCacheKey& key, const ObIKVCacheValue& value, ObKVCachePair*& kvpair,
      MBWrapper*& mb_wrapper, const enum ObKVCachePolicy policy = LRU);
  int alloc_kvpair(ObKVCacheInst& inst, const int64_t key_size, const int64_t value_size, ObKVCachePair*& kvpair,
      MBWrapper*& mb_wrapper, const enum ObKVCachePolicy policy = LRU);

protected:
  virtual bool add_handle_ref(MBWrapper* mb_wrapper) = 0;
  virtual void de_handle_ref(MBWrapper* mb_wrapper) = 0;
  virtual int alloc(
      ObKVCacheInst& inst, const enum ObKVCachePolicy policy, const int64_t block_size, MBWrapper*& mb_wrapper) = 0;
  virtual int free(MBWrapper* mb_wrapper) = 0;
  virtual MBWrapper*& get_curr_mb(ObKVCacheInst& inst, const enum ObKVCachePolicy policy) = 0;
  virtual bool mb_status_match(ObKVCacheInst& inst, const enum ObKVCachePolicy policy, MBWrapper* mb_wrapper) = 0;
  virtual int64_t get_block_size() const = 0;
};

class ObKVCacheStore : public ObIKVCacheStore<ObKVMemBlockHandle>, public ObIMBHandleAllocator {
  friend class observer::ObServerMemoryCutter;

public:
  ObKVCacheStore();
  virtual ~ObKVCacheStore();
  int init(
      ObKVCacheInstMap& insts, const int64_t max_cache_size, const int64_t block_size, const ObITenantMgr& tenant_mgr);
  void destroy();
  int set_priority(const int64_t cache_id, const int64_t old_priority, const int64_t new_priority);
  void refresh_score();
  void wash();
  int get_avg_cache_item_size(const uint64_t tenant_id, const int64_t cache_id, int64_t& avg_cache_item_size);
  int sync_wash_mbs(const uint64_t tenant_id, const int64_t wash_size, const bool wash_single_mb,
      lib::ObICacheWasher::ObCacheMemBlock*& wash_blocks);

  virtual int alloc_mbhandle(ObKVCacheInst& inst, const int64_t block_size, ObKVMemBlockHandle*& mb_handle);
  virtual int alloc_mbhandle(ObKVCacheInst& inst, ObKVMemBlockHandle*& mb_handle);
  virtual int alloc_mbhandle(const ObKVCacheInstKey& inst_key, ObKVMemBlockHandle*& mb_handle);
  virtual int free_mbhandle(ObKVMemBlockHandle* mb_handle);
  virtual int mark_washable(ObKVMemBlockHandle* mb_handle);

  virtual bool add_handle_ref(ObKVMemBlockHandle* mb_handle, const uint32_t seq_num);
  virtual bool add_handle_ref(ObKVMemBlockHandle* mb_handle);
  virtual void de_handle_ref(ObKVMemBlockHandle* mb_handle);
  int64_t get_handle_ref_cnt(ObKVMemBlockHandle* mb_handle);
  virtual int64_t get_block_size() const
  {
    return block_size_;
  }
  // implement functions of ObIMBWrapperMgr
  virtual int alloc(
      ObKVCacheInst& inst, const enum ObKVCachePolicy policy, const int64_t block_size, ObKVMemBlockHandle*& mb_handle);
  virtual int free(ObKVMemBlockHandle* mb_handle);
  virtual ObKVMemBlockHandle*& get_curr_mb(ObKVCacheInst& inst, const enum ObKVCachePolicy policy);
  virtual bool mb_status_match(ObKVCacheInst& inst, const enum ObKVCachePolicy policy, ObKVMemBlockHandle* mb_handle);

private:
  static const int64_t SYNC_WASH_MB_TIMEOUT_US = 100 * 1000;  // 100ms
  static const int64_t RETIRE_LIMIT = 16;
  static const int64_t WASH_THREAD_RETIRE_LIMIT = 2048;
  static const int64_t SUPPLY_MB_NUM_ONCE = 128;
  struct StoreMBHandleCmp {
    bool operator()(const ObKVMemBlockHandle* a, const ObKVMemBlockHandle* b) const;
  };
  struct WashHeap {
  public:
    WashHeap();
    virtual ~WashHeap();
    ObKVMemBlockHandle* add(ObKVMemBlockHandle* mb_handle);
    void reset();
    ObKVMemBlockHandle** heap_;
    int64_t heap_size_;
    int64_t mb_cnt_;
  };
  struct TenantWashInfo {
  public:
    TenantWashInfo();
    int add(ObKVMemBlockHandle* mb_handle);
    // put wash memblocks in cache_wash_heaps_ to wash_heap_
    void normalize();
    int64_t cache_size_;
    int64_t lower_limit_;
    int64_t upper_limit_;
    int64_t max_wash_size_;
    int64_t min_wash_size_;
    int64_t wash_size_;
    WashHeap wash_heap_;
    WashHeap cache_wash_heaps_[MAX_CACHE_NUM];
  };
  typedef ObFixedHashMap<uint64_t, TenantWashInfo*> WashMap;

private:
  ObKVMemBlockHandle* alloc_mbhandle(ObKVCacheInst& inst, const enum ObKVCachePolicy policy, const int64_t block_size);
  int alloc_mbhandle(
      ObKVCacheInst& inst, const enum ObKVCachePolicy policy, const int64_t block_size, ObKVMemBlockHandle*& mb_handle);
  void compute_tenant_wash_size();
  void wash_mb(ObKVMemBlockHandle* mb_handle);
  void wash_mbs(WashHeap& heap);
  bool try_wash_mb(ObKVMemBlockHandle* mb_handle, const uint64_t tenant_id, void*& buf, int64_t& mb_size);
  int do_wash_mb(ObKVMemBlockHandle* mb_handle, void*& buf, int64_t& mb_size);
  int init_wash_heap(WashHeap& heap, const int64_t heap_size);
  int prepare_wash_structs();
  void reuse_wash_structs();
  void destroy_wash_structs();

  void* alloc_mb(lib::ObTenantResourceMgrHandle& resource_handle, const uint64_t tenant_id, const int64_t block_size);
  void free_mb(lib::ObTenantResourceMgrHandle& resource_handle, const uint64_t tenant_id, void* ptr);

  static QClock& get_qclock()
  {
    static QClock qclock;
    return qclock;
  }
  static RetireStation& get_retire_station()
  {
    static RetireStation retire_station(get_qclock(), RETIRE_LIMIT);
    return retire_station;
  }

  int insert_mb_handle(common::ObDLink* head, ObKVMemBlockHandle* mb_handle);
  int remove_mb_handle(ObKVMemBlockHandle* mb_handle);
  void retire_mb_handle(ObKVMemBlockHandle* mb_handle);
  void retire_mb_handles(HazardList& retire_list);
  void reuse_mb_handles(HazardList& reclaim_list);
  bool try_supply_mb(const int64_t mb_count);

private:
  bool inited_;
  ObKVCacheInstMap* insts_;
  // data structures for store
  int64_t cur_mb_num_;
  int64_t max_mb_num_;
  int64_t block_size_;
  int64_t aligned_block_size_;
  int64_t block_payload_size_;
  ObKVMemBlockHandle* mb_handles_;
  ObFixedQueue<ObKVMemBlockHandle> mb_handles_pool_;

  // data structures for wash
  lib::ObMutex wash_out_lock_;
  ObSimpleFixedArray<uint64_t> tenant_ids_;
  ObSimpleFixedArray<ObKVCacheInstHandle> inst_handles_;
  ObFreeHeap<TenantWashInfo> wash_info_free_heap_;
  WashMap tenant_wash_map_;
  ObFreeHeap<ObKVMemBlockHandle*> mb_ptr_free_heap_;
  double tenant_reserve_mem_ratio_;

  int64_t wash_itid_;
  const ObITenantMgr* tenant_mgr_;
};

template <typename MBWrapper>
int ObIKVCacheStore<MBWrapper>::store(ObKVCacheInst& inst, const ObIKVCacheKey& key, const ObIKVCacheValue& value,
    ObKVCachePair*& kvpair, MBWrapper*& mb_wrapper, const enum ObKVCachePolicy policy)
{
  int ret = common::OB_SUCCESS;
  const int64_t key_size = key.size();
  const int64_t value_size = value.size();
  if (OB_FAIL(alloc_kvpair(inst, key_size, value_size, kvpair, mb_wrapper, policy))) {
    COMMON_LOG(WARN, "failed to alloc", K(ret), K(key_size), K(value_size));
  } else {
    if (OB_FAIL(key.deep_copy(reinterpret_cast<char*>(kvpair->key_), key_size, kvpair->key_))) {
      COMMON_LOG(WARN, "failed to deep copy key", K(ret));
    } else if (OB_FAIL(value.deep_copy(reinterpret_cast<char*>(kvpair->value_), value_size, kvpair->value_))) {
      COMMON_LOG(WARN, "failed to deep copy value", K(ret));
    }
    if (OB_FAIL(ret)) {
      if (nullptr != mb_wrapper) {
        de_handle_ref(mb_wrapper);
        mb_wrapper = nullptr;
      }
      kvpair = nullptr;
    }
  }
  return ret;
}

template <typename MBWrapper>
int ObIKVCacheStore<MBWrapper>::alloc_kvpair(ObKVCacheInst& inst, const int64_t key_size, const int64_t value_size,
    ObKVCachePair*& kvpair, MBWrapper*& mb_wrapper, const enum ObKVCachePolicy policy)
{
  int ret = common::OB_SUCCESS;
  const int64_t block_size = get_block_size();
  const int64_t block_payload_size = block_size - sizeof(ObKVStoreMemBlock);
  int64_t align_kv_size = ObKVStoreMemBlock::get_align_size(key_size, value_size);
  kvpair = NULL;
  mb_wrapper = NULL;

  if (align_kv_size > block_payload_size) {
    // large kv
    const int64_t big_block_size = align_kv_size + sizeof(ObKVStoreMemBlock);
    if (OB_FAIL(alloc(inst, policy, big_block_size, mb_wrapper))) {
      COMMON_LOG(ERROR, "alloc failed", K(ret), K(big_block_size));
    } else {
      if (add_handle_ref(mb_wrapper)) {
        if (OB_FAIL(mb_wrapper->alloc(key_size, value_size, align_kv_size, kvpair))) {
          // Fail to alloc kvpair, deref the reference
          de_handle_ref(mb_wrapper);
          COMMON_LOG(WARN, "alloc failed", K(ret));
        } else {
          // success to alloc kv
          mb_wrapper->set_full(inst.status_.base_mb_score_);
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "add_handle_ref failed", K(ret));
      }
    }
  } else {
    // small kv
    do {
      mb_wrapper = get_curr_mb(inst, policy);
      if (NULL != mb_wrapper) {
        if (add_handle_ref(mb_wrapper)) {
          if (mb_status_match(inst, policy, mb_wrapper)) {
            if (OB_FAIL(mb_wrapper->alloc(key_size, value_size, align_kv_size, kvpair))) {
              if (OB_BUF_NOT_ENOUGH != ret) {
                COMMON_LOG(WARN, "alloc failed", K(ret));
              } else {
                ret = OB_SUCCESS;
              }
            } else {
              break;
            }
          }
          de_handle_ref(mb_wrapper);
        }
      }

      if (OB_SUCC(ret)) {
        MBWrapper* new_mb_wrapper = NULL;
        if (OB_FAIL(alloc(inst, policy, block_size, new_mb_wrapper))) {
          COMMON_LOG(ERROR, "alloc failed", K(ret), K(block_size));
        } else if (ATOMIC_BCAS(
                       (uint64_t*)(&get_curr_mb(inst, policy)), (uint64_t)mb_wrapper, (uint64_t)new_mb_wrapper)) {
          if (NULL != mb_wrapper) {
            mb_wrapper->set_full(inst.status_.base_mb_score_);
          }
        } else if (OB_FAIL(free(new_mb_wrapper))) {
          COMMON_LOG(ERROR, "free failed", K(ret));
        }
      }
    } while (OB_SUCC(ret));
  }

  if (OB_FAIL(ret)) {
    kvpair = NULL;
    mb_wrapper = NULL;
  }
  return ret;
}

}  // end namespace common
}  // end namespace oceanbase

#endif  // OCEANBASE_CACHE_OB_KVCACHE_STORE_H_
