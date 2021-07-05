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

#include "ob_gmemstore_allocator.h"
#include "ob_memstore_allocator_mgr.h"
#include "storage/memtable/ob_memtable.h"
#include "lib/utility/ob_print_utils.h"
#include "observer/omt/ob_multi_tenant.h"
#include "observer/ob_server_struct.h"
#include "share/ob_tenant_mgr.h"

namespace oceanbase {
namespace common {
int FrozenMemstoreInfoLogger::operator()(ObDLink* link)
{
  int ret = OB_SUCCESS;
  ObGMemstoreAllocator::AllocHandle* handle = CONTAINER_OF(link, typeof(*handle), total_list_);
  memtable::ObMemtable& mt = handle->mt_;
  if (mt.is_frozen_memtable()) {
    if (OB_FAIL(databuff_print_obj(buf_, limit_, pos_, mt))) {
    } else {
      ret = databuff_printf(buf_, limit_, pos_, ",");
    }
  }
  return ret;
}

int ObGMemstoreAllocator::AllocHandle::init(uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  ObGMemstoreAllocator* host = NULL;
  if (OB_FAIL(ObMemstoreAllocatorMgr::get_instance().get_tenant_memstore_allocator(tenant_id, host))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NULL == host) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    host->init_handle(*this, tenant_id);
    if (0 == (last_freeze_timestamp_ = host->get_last_freeze_timestamp())) {
      COMMON_LOG(ERROR, "unexpected value", K(last_freeze_timestamp_));
    }
  }
  return ret;
}

void ObGMemstoreAllocator::init_handle(AllocHandle& handle, uint64_t tenant_id)
{
  handle.do_reset();
  handle.set_host(this);
  {
    int64_t nway = nway_per_group();
    LockGuard guard(lock_);
    hlist_.init_handle(handle);
    arena_.update_nway_per_group(nway);
    set_memstore_threshold_without_lock(tenant_id);
  }
  COMMON_LOG(TRACE, "MTALLOC.init", KP(&handle.mt_));
}

void ObGMemstoreAllocator::destroy_handle(AllocHandle& handle)
{
  COMMON_LOG(TRACE, "MTALLOC.destroy", KP(&handle.mt_));
  arena_.free(handle.arena_handle_);
  {
    LockGuard guard(lock_);
    hlist_.destroy_handle(handle);
    if (hlist_.is_empty()) {
      arena_.reset();
    }
  }
  handle.do_reset();
}

void* ObGMemstoreAllocator::alloc(AllocHandle& handle, int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t align_size = upper_align(size, sizeof(int64_t));
  uint64_t tenant_id = arena_.get_tenant_id();
  bool is_out_of_mem = false;
  if (!handle.is_id_valid()) {
    COMMON_LOG(TRACE, "MTALLOC.first_alloc", KP(&handle.mt_));
    LockGuard guard(lock_);
    if (!handle.is_id_valid()) {
      handle.set_clock(arena_.retired());
      hlist_.set_active(handle);
    }
  }
  if (handle.mt_.is_inner_table()) {
    // inner table memory not limited by memstore
  } else if (OB_FAIL(ObTenantManager::get_instance().check_tenant_out_of_memstore_limit(tenant_id, is_out_of_mem))) {
    COMMON_LOG(ERROR, "fail to check tenant out of mem limit", K(ret), K(tenant_id));
    is_out_of_mem = true;
  } else if (is_out_of_mem && REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
    STORAGE_LOG(WARN, "this tenant is already out of memstore limit", K(tenant_id));
  }
  return is_out_of_mem ? nullptr : arena_.alloc(handle.id_, handle.arena_handle_, align_size);
}

void ObGMemstoreAllocator::set_frozen(AllocHandle& handle)
{
  COMMON_LOG(TRACE, "MTALLOC.set_frozen", KP(&handle.mt_));
  LockGuard guard(lock_);
  hlist_.set_frozen(handle);
}

static int64_t calc_nway(int64_t cpu, int64_t mem)
{
  return std::min(cpu, mem / 20 / ObFifoArena::PAGE_SIZE);
}

int64_t ObGMemstoreAllocator::nway_per_group()
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = arena_.get_tenant_id();
  double min_cpu = 0;
  double max_cpu = 0;
  int64_t min_memory = 0;
  int64_t max_memory = 0;
  omt::ObMultiTenant* omt = GCTX.omt_;
  if (NULL == omt) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(omt->get_tenant_cpu(tenant_id, min_cpu, max_cpu))) {
  } else if (OB_FAIL(ObTenantManager::get_instance().get_tenant_mem_limit(tenant_id, min_memory, max_memory))) {
  }
  return OB_SUCCESS == ret ? calc_nway((int64_t)max_cpu, min_memory) : 0;
}

int ObGMemstoreAllocator::set_memstore_threshold(uint64_t tenant_id)
{
  LockGuard guard(lock_);
  int ret = set_memstore_threshold_without_lock(tenant_id);
  return ret;
}

int ObGMemstoreAllocator::set_memstore_threshold_without_lock(uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  int64_t memstore_threshold = INT64_MAX;
  if (OB_FAIL(ObTenantManager::get_instance().get_tenant_memstore_limit(tenant_id, memstore_threshold))) {
    COMMON_LOG(WARN, "failed to get_tenant_memstore_limit", K(tenant_id), K(ret));
  } else {
    arena_.set_memstore_threshold(memstore_threshold);
  }
  return ret;
}

};  // end namespace common
};  // end namespace oceanbase
