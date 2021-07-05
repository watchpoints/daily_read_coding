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

#ifndef _OB_2D_ARRAY_H
#define _OB_2D_ARRAY_H 1
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_template_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase {
namespace common {
// Specify max_block_size here. True block size is the largest block size <=
// max_block_size and is sizeof(T) * power of 2.
const int64_t OB_BLOCK_POINTER_ARRAY_SIZE = 64;
template <typename T, int max_block_size = OB_MALLOC_BIG_BLOCK_SIZE, typename BlockAllocatorT = ModulePageAllocator,
    bool auto_free = false,
    typename BlockPointerArrayT = ObSEArray<T*, OB_BLOCK_POINTER_ARRAY_SIZE, BlockAllocatorT, auto_free> >
class Ob2DArray {
public:
  Ob2DArray(const BlockAllocatorT& alloc = BlockAllocatorT(ObModIds::OB_2D_ARRAY));
  virtual ~Ob2DArray();

  virtual int init(const int64_t new_size);
  virtual int push_back(const T& obj);
  virtual void pop_back();
  virtual int pop_back(T& obj);
  virtual int remove(int64_t idx);

  int assign(const ObIArray<T>& other)
  {
    return inner_assign(other);
  }
  int assign(const Ob2DArray& other)
  {
    return inner_assign(other);
  }

  T& at(int64_t idx);
  const T& at(int64_t idx) const;
  virtual int at(int64_t idx, T& obj) const;

  int64_t count() const
  {
    return count_;
  };
  virtual void reuse()
  {
    destruct_objs();
  }
  virtual void reset()
  {
    destroy();
  };
  virtual void destroy();
  virtual int reserve(int64_t capacity);
  virtual int set_all(const T& value);
  int64_t mem_used() const;
  int64_t get_capacity() const;
  int prepare_allocate(int64_t capacity);
  virtual int64_t to_string(char* buffer, int64_t length) const;
  inline int64_t get_block_size() const
  {
    return LOCAL_BLOCK_SIZE;
  }
  inline const BlockAllocatorT& get_block_allocator() const
  {
    return block_alloc_;
  }

  virtual bool empty() const;

  static uint32_t magic_offset_bits()
  {
    return offsetof(Ob2DArray, magic_) * 8;
  }
  static uint32_t block_alloc_offset_bits()
  {
    return offsetof(Ob2DArray, block_alloc_) * 8;
  }
  static uint32_t blocks_offset_bits()
  {
    return offsetof(Ob2DArray, blocks_) * 8;
  }
  static uint32_t count_offset_bits()
  {
    return offsetof(Ob2DArray, count_) * 8;
  }
  static uint32_t capacity_offset_bits()
  {
    return offsetof(Ob2DArray, capacity_) * 8;
  }

  NEED_SERIALIZE_AND_DESERIALIZE;

private:
  // function members
  template <typename U>
  int inner_assign(const U& other);

  void construct_items(T* ptr, int64_t cnt);
  int set_default(const int64_t new_size);
  T* get_obj_pos(int64_t i) const;
  int new_block(const int64_t block_capacity = BLOCK_CAPACITY);
  int realloc_first_block(const int64_t new_capacity);
  void destruct_objs();
  DISALLOW_COPY_AND_ASSIGN(Ob2DArray);

  inline int64_t get_block_count() const
  {
    return blocks_.count();
  }
  inline T* alloc_block(const int64_t capacity)
  {
    return reinterpret_cast<T*>(block_alloc_.alloc(capacity * sizeof(T)));
  }
  inline void free_block(T* block)
  {
    return block_alloc_.free(block);
  }

public:
  static const int64_t BLOCK_CAPACITY =
      1 << (8 * sizeof(int64_t) - __builtin_clzll(static_cast<int64_t>(max_block_size / sizeof(T))) - 1);

private:
  static const int64_t LOCAL_BLOCK_SIZE = sizeof(T) * BLOCK_CAPACITY;
  int32_t magic_;
  BlockAllocatorT block_alloc_;
  BlockPointerArrayT blocks_;
  int64_t count_;
  int64_t capacity_;
  static const int64_t INITIAL_FIRST_BLOCK_CAPACITY = 128 < BLOCK_CAPACITY ? 128 : BLOCK_CAPACITY;
};
template <typename T, int max_block_size = OB_MALLOC_BIG_BLOCK_SIZE, typename BlockAllocatorT = ModulePageAllocator,
    bool auto_free = false, typename BlockPointerArrayT = ObSEArray<T*, 64, BlockAllocatorT, auto_free> >
using ObSegmentArray = Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>;

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::Ob2DArray(const BlockAllocatorT& alloc)
    : magic_(0x2D2D2D2D), block_alloc_(alloc), blocks_(OB_MALLOC_NORMAL_BLOCK_SIZE, alloc), count_(0), capacity_(0)
{
  STATIC_ASSERT(max_block_size >= sizeof(T), "max_block_size < sizeof(T)");
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::~Ob2DArray()
{
  destroy();
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
template <typename U>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::inner_assign(const U& other)
{
  int ret = OB_SUCCESS;
  if ((void*)this != (void*)&other) {
    reuse();
    int64_t N = other.count();
    reserve(N);
    for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
      if (OB_FAIL(this->push_back(other.at(i)))) {
        LIB_LOG(WARN, "failed to push back item", K(ret));
        break;
      }
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline int64_t Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::mem_used() const
{
  // TODO: inaccurate: didn't count BlockPointerArrayT
  return (sizeof(T) * capacity_);
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline int64_t Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::get_capacity() const
{
  return capacity_;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::realloc_first_block(
    const int64_t new_capacity)
{
  int ret = OB_SUCCESS;
  OB_ASSERT(get_block_count() == 1);
  if (new_capacity == capacity_) {
    // do nothing
  } else {
    if (new_capacity < capacity_) {
      OB_ASSERT(new_capacity >= count());
    }

    T* new_data = alloc_block(new_capacity);
    T* old_data = blocks_.at(0);

    if (OB_NOT_NULL(new_data)) {
      if (std::is_trivial<T>::value) {
        memcpy(new_data, old_data, count() * sizeof(T));
      } else {
        int64_t fail_pos = -1;
        for (int64_t i = 0; OB_SUCC(ret) && i < count(); i++) {
          if (OB_FAIL(construct_assign(new_data[i], old_data[i]))) {
            LIB_LOG(WARN, "failed to copy new_data", K(ret));
            fail_pos = i;
          }
        }
        if (std::is_trivially_destructible<T>::value) {
        } else {
          // if construct assign failed, destruct the copied data
          if (OB_FAIL(ret)) {
            for (int64_t i = 0; i < fail_pos; i++) {
              new_data[i].~T();
            }
          } else {
            // if construct assign successfully, destruct the old_data
            for (int64_t i = 0; i < count(); i++) {
              old_data[i].~T();
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        free_block(old_data);
        blocks_.at(0) = new_data;
        capacity_ = new_capacity;
      } else {
        // construct_assign failed, free the allocated memory, and keep the old_data
        // like never calling reserve
        free_block(new_data);
      }
    } else {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LIB_LOG(ERROR, "no memory", K(ret), K(new_capacity), K(count()), K(get_capacity()));
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::new_block(
    const int64_t block_capacity)
{
  if (block_capacity != BLOCK_CAPACITY) {
    OB_ASSERT(get_block_count() == 0);
  }
  int ret = OB_SUCCESS;
  T* blk = alloc_block(block_capacity);
  if (OB_ISNULL(blk)) {
    LIB_LOG(WARN, "no memory");
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(blocks_.push_back(blk))) {
    LIB_LOG(WARN, "failed to add block", K(ret));
    free_block(blk);
    blk = NULL;
  } else {
    capacity_ += block_capacity;
  }
  return ret;
}
template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline T* Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::get_obj_pos(int64_t i) const
{
  OB_ASSERT(i >= 0);
  OB_ASSERT(i < capacity_);
  T* block = blocks_.at(i / BLOCK_CAPACITY);
  T* obj_buf = block + (i % BLOCK_CAPACITY);
  return obj_buf;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
void Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::construct_items(T* ptr, int64_t cnt)
{
  if (std::is_trivial<T>::value) {
    MEMSET(ptr, 0, sizeof(T) * cnt);
    LIB_LOG(DEBUG, "construct trivial items", K(cnt));
  } else {
    new (ptr) T[cnt];
    LIB_LOG(DEBUG, "construct non-trivial items", K(cnt));
  }
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::set_default(const int64_t new_size)
{
  int ret = OB_SUCCESS;
  if (new_size > capacity_) {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(WARN, "failed: capacity is less than size", K(ret), K(new_size), K(capacity_));
  } else {
    const int64_t need_blocks = (new_size - 1) / BLOCK_CAPACITY + 1;
    int64_t left_count = new_size;
    LIB_LOG(TRACE, "trace init 2darray", K(new_size), K(need_blocks), K(capacity_));
    for (int64_t i = 0; OB_SUCC(ret) && i < need_blocks; ++i) {
      if (i != need_blocks - 1) {
        if (BLOCK_CAPACITY >= left_count) {
          ret = OB_ERR_UNEXPECTED;
          LIB_LOG(WARN, "failed: left count is not match", K(left_count), K(need_blocks), K(i));
        } else {
          construct_items(blocks_.at(i), BLOCK_CAPACITY);
          count_ += BLOCK_CAPACITY;
          left_count -= BLOCK_CAPACITY;
        }
      } else {
        // last one
        if (left_count > BLOCK_CAPACITY) {
          ret = OB_ERR_UNEXPECTED;
          LIB_LOG(WARN, "failed: left count is not match", K(left_count), K(need_blocks), K(i));
        } else {
          construct_items(blocks_.at(i), left_count);
          count_ += left_count;
          left_count -= left_count;
        }
      }
    }
    if (0 != left_count || count_ != new_size) {
      ret = OB_ERR_UNEXPECTED;
      LIB_LOG(WARN, "failed: left count is not 0", K(left_count), K(need_blocks), K(count_), K(new_size));
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::init(const int64_t new_size)
{
  int ret = OB_SUCCESS;
  if (new_size == 0) {
    reset();
  } else if (0 != count_) {
    // do not force capacity = 0, because it will init when reuse
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(WARN, "failed: capacity or count is 0", K(capacity_), K(count_));
  } else {
    if (OB_FAIL(reserve(new_size))) {
      LIB_LOG(WARN, "failed: failed to reserve(new_size)", K(ret), K(new_size));
    } else if (OB_FAIL(set_default(new_size))) {
      LIB_LOG(WARN, "failed to set default value", K(new_size), K(ret));
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::push_back(const T& obj)
{
  int ret = OB_SUCCESS;
  if (count_ >= capacity_) {
    if (get_block_count() == 0) {
      if (OB_FAIL(new_block(INITIAL_FIRST_BLOCK_CAPACITY))) {
        LIB_LOG(WARN, "failed: creating first block", K(ret));
      }
    } else if (capacity_ < BLOCK_CAPACITY) {
      OB_ASSERT(get_block_count() == 1);
      int64_t new_capacity = BLOCK_CAPACITY < 2 * capacity_ ? BLOCK_CAPACITY : 2 * capacity_;
      if (OB_FAIL(realloc_first_block(new_capacity))) {
        LIB_LOG(WARN, "failed: realloc_first_block(new_capacity)", K(ret));
      }
    } else {
      if (OB_FAIL(new_block())) {
        LIB_LOG(WARN, "failed: new_block()", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(count_ >= capacity_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      T* obj_buf = get_obj_pos(count_);
      if (OB_FAIL(construct_assign(*obj_buf, obj))) {
        LIB_LOG(WARN, "failed to copy data", K(ret));
      } else {
        ++count_;
      }
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
void Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::pop_back()
{
  if (OB_LIKELY(0 < count_)) {
    T* obj_buf = get_obj_pos(count_ - 1);
    obj_buf->~T();
    --count_;
  }
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::pop_back(T& obj)
{
  int ret = OB_ENTRY_NOT_EXIST;
  if (OB_LIKELY(0 < count_)) {
    T* obj_ptr = get_obj_pos(count_ - 1);
    // assign
    if (OB_FAIL(copy_assign(obj, *obj_ptr))) {
      LIB_LOG(WARN, "failed to copy data", K(ret));
    } else {
      obj_ptr->~T();
      --count_;
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::remove(int64_t idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 > idx || idx >= count_)) {
    ret = OB_ARRAY_OUT_OF_RANGE;
  } else {
    T* obj_ptr = get_obj_pos(idx);
    for (int64_t i = idx; OB_SUCC(ret) && i < count_ - 1; ++i) {
      if (OB_FAIL(copy_assign(at(i), at(i + 1)))) {
        LIB_LOG(WARN, "failed to copy data", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      // destruct the last object
      obj_ptr = get_obj_pos(count_ - 1);
      obj_ptr->~T();
      --count_;
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::at(int64_t idx, T& obj) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 > idx || idx >= count_)) {
    ret = OB_ARRAY_OUT_OF_RANGE;
  } else {
    T* obj_ptr = get_obj_pos(idx);
    if (OB_FAIL(copy_assign(obj, *obj_ptr))) {
      LIB_LOG(WARN, "failed to copy data", K(ret));
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline T& Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::at(int64_t idx)
{
  if (OB_UNLIKELY(0 > idx || idx >= count_)) {
    LIB_LOG(ERROR, "invalid idx. Fatal!!!", K(idx), K_(count));
  }
  return *get_obj_pos(idx);
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
inline const T& Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::at(int64_t idx) const
{
  if (OB_UNLIKELY(0 > idx || idx >= count_)) {
    LIB_LOG(ERROR, "invalid idx. Fatal!!!", K(idx), K_(count));
  }
  return *get_obj_pos(idx);
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
void Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::destruct_objs()
{
  if (std::is_trivially_destructible<T>::value) {
    // do nothing
  } else {
    // destruct all objects
    for (int64_t i = 0; i < count_; ++i) {
      T* obj_buf = get_obj_pos(i);
      obj_buf->~T();
    }
  }
  count_ = 0;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
void Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::destroy()
{
  destruct_objs();
  int64_t block_count = get_block_count();
  for (int64_t i = 0; i < block_count; ++i) {
    T* block = blocks_.at(i);
    free_block(block);
    block = NULL;
  }
  capacity_ = 0;
  blocks_.destroy();
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::reserve(int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (capacity > get_capacity()) {
    if (capacity <= BLOCK_CAPACITY) {
      OB_ASSERT(get_block_count() <= 1);
      // initialize or expand first block
      if (get_block_count() == 0) {
        if (OB_FAIL(new_block(capacity))) {
          LIB_LOG(WARN, "failed: creating first block", K(ret));
        }
      } else {
        if (OB_FAIL(realloc_first_block(capacity))) {
          LIB_LOG(WARN, "failed: realloc_first_block(capacity)", K(ret));
        }
      }
    } else {
      if (get_block_count() == 1 && capacity_ < BLOCK_CAPACITY) {
        if (OB_FAIL(realloc_first_block(BLOCK_CAPACITY))) {
          LIB_LOG(WARN, "failed: realloc_first_block(BLOCK_CAPACITY)", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        OB_ASSERT(capacity > 0);
        const int64_t need_blocks = (capacity - 1) / BLOCK_CAPACITY + 1;
        const int64_t current_blocks = get_block_count();
        for (int64_t i = current_blocks; OB_SUCC(ret) && i < need_blocks; ++i) {
          if (OB_FAIL(new_block())) {
            LIB_LOG(WARN, "failed: new_block()", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

// use it carefully, when it is trivial !!!
// otherwise it may cause lack of destruction
template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::set_all(const T& value)
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < count_; ++i) {
    if (OB_FAIL(construct_assign(at(i), value))) {
      LIB_LOG(WARN, "failed: assign", K(ret), K(value));
    }
  }

  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int64_t Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::to_string(
    char* buf, int64_t buf_len) const
{
  int64_t pos = 0;
  J_ARRAY_START();
  for (int64_t index = 0; index < count_ - 1; ++index) {
    BUF_PRINTO(at(index));
    J_COMMA();
  }
  if (0 < count_) {
    BUF_PRINTO(at(count_ - 1));
  }
  J_ARRAY_END();
  return pos;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
int Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::prepare_allocate(int64_t capacity)
{
  int ret = OB_SUCCESS;
  int64_t N = get_capacity();
  if (OB_UNLIKELY(N < capacity)) {
    ret = reserve(capacity);
  }
  if (OB_SUCC(ret)) {
    if (std::is_trivial<T>::value) {  // memcpy safe
      // do nothing
    } else {
      for (int64_t i = count_; i < capacity; i++) {
        new (get_obj_pos(i)) T();
      }
    }
    if (capacity > count_) {
      count_ = capacity;
    }
  }
  return ret;
}

template <typename T, int max_block_size, typename BlockAllocatorT, bool auto_free, typename BlockPointerArrayT>
bool Ob2DArray<T, max_block_size, BlockAllocatorT, auto_free, BlockPointerArrayT>::empty() const
{
  return 0 == count_;
}

}  // end namespace common
}  // end namespace oceanbase

#endif /* _OB_2D_ARRAY_H */
