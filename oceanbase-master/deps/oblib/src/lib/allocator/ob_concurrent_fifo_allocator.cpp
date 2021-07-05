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

#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/utility/utility.h"
#include "lib/cpu/ob_cpu_topology.h"

namespace oceanbase {
namespace common {
ObConcurrentFIFOAllocator::ObConcurrentFIFOAllocator() : inner_allocator_()
{}

ObConcurrentFIFOAllocator::~ObConcurrentFIFOAllocator()
{
  destroy();
}

int ObConcurrentFIFOAllocator::init(const int64_t total_limit, const int64_t hold_limit, const int64_t page_size)
{
  UNUSED(hold_limit);
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_allocator_.init(page_size,
          ObModIds::OB_CON_FIFO_ALLOC,
          OB_SERVER_TENANT_ID,
          get_cpu_count() * STORAGE_SIZE_TIMES,
          total_limit))) {
    LIB_LOG(WARN, "fail to init inner allocator", K(ret));
  }
  return ret;
}

int ObConcurrentFIFOAllocator::init(const int64_t total_limit, const int64_t hold_limit, const int64_t tenant_id,
    const lib::ObLabel& label, const int64_t page_size)
{
  UNUSED(hold_limit);
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_allocator_.init(page_size, label, tenant_id, get_cpu_count() * STORAGE_SIZE_TIMES, total_limit))) {
    LIB_LOG(WARN, "fail to init inner allocator", K(ret));
  }
  return ret;
}

int ObConcurrentFIFOAllocator::init(
    const int64_t page_size, const lib::ObLabel& label, const uint64_t tenant_id, const int64_t total_limit)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_allocator_.init(page_size, label, tenant_id, get_cpu_count() * STORAGE_SIZE_TIMES, total_limit))) {
    LIB_LOG(WARN, "failed to init inner allocator", K(ret));
  }
  return ret;
}

int ObConcurrentFIFOAllocator::set_hold_limit(int64_t hold_limit)
{
  UNUSED(hold_limit);
  return OB_SUCCESS;
}

void ObConcurrentFIFOAllocator::set_total_limit(int64_t total_limit)
{
  inner_allocator_.set_total_limit(total_limit);
}

void ObConcurrentFIFOAllocator::destroy()
{
  inner_allocator_.destroy();
}

void ObConcurrentFIFOAllocator::set_label(const lib::ObLabel& label)
{
  inner_allocator_.set_label(label);
}

int64_t ObConcurrentFIFOAllocator::allocated()
{
  return inner_allocator_.allocated();
}

void* ObConcurrentFIFOAllocator::alloc(const int64_t size, const ObMemAttr& attr)
{
  return inner_allocator_.alloc(size, attr);
}

void* ObConcurrentFIFOAllocator::alloc(const int64_t size)
{
  return inner_allocator_.alloc(size);
}

void ObConcurrentFIFOAllocator::free(void* ptr)
{
  inner_allocator_.free(ptr);
  ptr = NULL;
}

}  // namespace common
}  // namespace oceanbase
