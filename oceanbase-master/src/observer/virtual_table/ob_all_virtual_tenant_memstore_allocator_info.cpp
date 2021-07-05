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

#include "ob_all_virtual_tenant_memstore_allocator_info.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "share/allocator/ob_memstore_allocator_mgr.h"
#include "storage/memtable/ob_memtable.h"

namespace oceanbase {
using namespace common;
using namespace share;

namespace observer {
class MemstoreInfoFill {
public:
  typedef ObMemstoreAllocatorInfo Item;
  typedef ObArray<Item> ItemArray;
  typedef ObGMemstoreAllocator::AllocHandle Handle;
  MemstoreInfoFill(ItemArray& array) : array_(array)
  {}
  ~MemstoreInfoFill()
  {}
  int operator()(ObDLink* link)
  {
    Item item;
    Handle* handle = CONTAINER_OF(link, Handle, total_list_);
    memtable::ObMemtable& mt = handle->mt_;
    item.protection_clock_ = handle->get_protection_clock();
    item.is_frozen_ = mt.is_frozen_memtable();
    item.pkey_ = mt.get_partition_key();
    item.trans_version_range_ = mt.get_version_range();
    item.version_ = mt.get_version();
    return array_.push_back(item);
  }
  ItemArray& array_;
};

ObAllVirtualTenantMemstoreAllocatorInfo::ObAllVirtualTenantMemstoreAllocatorInfo()
    : ObVirtualTableIterator(),
      allocator_mgr_(ObMemstoreAllocatorMgr::get_instance()),
      tenant_ids_(),
      memstore_infos_(),
      memstore_infos_idx_(0),
      tenant_ids_idx_(0),
      col_count_(0),
      retire_clock_(INT64_MAX)
{}

ObAllVirtualTenantMemstoreAllocatorInfo::~ObAllVirtualTenantMemstoreAllocatorInfo()
{
  reset();
}

int ObAllVirtualTenantMemstoreAllocatorInfo::inner_open()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(tenant_ids_.reserve(OB_MAX_RESERVED_TENANT_ID - OB_INVALID_TENANT_ID))) {
    SERVER_LOG(WARN, "failed to reserve tenant_ids_", K(ret));
  } else if (OB_FAIL(fill_tenant_ids())) {
    SERVER_LOG(WARN, "fail to fill tenant ids", K(ret));
  } else if (tenant_ids_.count() < 1) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "got tenant ids is empty", K(ret));
  } else if (OB_FAIL(fill_memstore_infos(tenant_ids_.at(0)))) {
    SERVER_LOG(WARN, "fail to fill memstore info", K(ret));
  } else {
    tenant_ids_idx_ = 0;
    col_count_ = output_column_ids_.count();
  }
  return ret;
}

void ObAllVirtualTenantMemstoreAllocatorInfo::reset()

{
  tenant_ids_.reset();
  memstore_infos_.reset();
  tenant_ids_idx_ = 0;
  memstore_infos_idx_ = 0;
  col_count_ = 0;
}

int ObAllVirtualTenantMemstoreAllocatorInfo::fill_tenant_ids()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == GCTX.omt_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "GCTX.omt_ shouldn't be NULL", K_(GCTX.omt), K(GCTX), K(ret));
  } else {
    omt::TenantIdList ids(NULL, ObModIds::OMT_VIRTUAL_TABLE);
    GCTX.omt_->get_tenant_ids(ids);
    for (int64_t i = 0; OB_SUCC(ret) && i < ids.size(); i++) {
      if (OB_FAIL(tenant_ids_.push_back(ids[i]))) {
        SERVER_LOG(WARN, "failed to push back tenant_id", K(ret));
      }
    }
  }
  return ret;
}

int ObAllVirtualTenantMemstoreAllocatorInfo::fill_memstore_infos(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  ObGMemstoreAllocator* ta = NULL;
  memstore_infos_.reset();
  if (tenant_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid tenant_id", K(tenant_id), K(ret));
  } else if (OB_FAIL(allocator_mgr_.get_tenant_memstore_allocator(tenant_id, ta))) {
    SERVER_LOG(WARN, "failed to get tenant memstore allocator", K(tenant_id), K(ret));
  } else if (OB_ISNULL(ta)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "got tenant memstore allocator is NULL", K(tenant_id), K(ret));
  } else {
    MemstoreInfoFill fill_func(memstore_infos_);
    if (OB_FAIL(ta->for_each(fill_func))) {
      SERVER_LOG(WARN, "fill memstore info fail", K(ret));
    } else {
      retire_clock_ = ta->get_retire_clock();
      memstore_infos_idx_ = 0;
    }
  }
  return ret;
}

int ObAllVirtualTenantMemstoreAllocatorInfo::inner_get_next_row(ObNewRow*& row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(ret));
  } else {
    while (OB_SUCC(ret) && memstore_infos_idx_ >= memstore_infos_.count()) {
      if (tenant_ids_idx_ >= tenant_ids_.count() - 1) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(fill_memstore_infos(tenant_ids_.at(++tenant_ids_idx_)))) {
        SERVER_LOG(WARN, "fail to fill_memstore_infos", K(ret));
      } else { /*do nothing*/
      }
    }

    if (OB_SUCC(ret)) {
      ObObj* cells = cur_row_.cells_;
      const uint64_t tenant_id = tenant_ids_.at(tenant_ids_idx_);
      if (OB_ISNULL(cells)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
      } else {
        ObString ipstr;
        MemstoreInfo& info = memstore_infos_.at(memstore_infos_idx_);
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          const uint64_t col_id = output_column_ids_.at(i);
          switch (col_id) {
            case SVR_IP: {
              ipstr.reset();
              if (OB_FAIL(ObServerUtils::get_server_ip(allocator_, ipstr))) {
                SERVER_LOG(ERROR, "get server ip failed", K(ret));
              } else {
                cells[i].set_varchar(ipstr);
                cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
              }
              break;
            }
            case SVR_PORT: {
              cells[i].set_int(GCONF.self_addr_.get_port());
              break;
            }
            case TENANT_ID: {
              cells[i].set_int(static_cast<int64_t>(tenant_id));
              break;
            }
            case TABLE_ID: {
              cells[i].set_int(info.pkey_.get_table_id());
              break;
            }
            case PARTITION_ID: {
              cells[i].set_int(info.pkey_.get_partition_id());
              break;
            }
            case MT_BASE_VERSION: {
              cells[i].set_int(info.trans_version_range_.base_version_);
              break;
            }
            case RETIRE_CLOCK: {
              cells[i].set_int(retire_clock_);
              break;
            }
            case MT_IS_FROZEN: {
              cells[i].set_int(info.is_frozen_);
              break;
            }
            case MT_PROTECTION_CLOCK: {
              cells[i].set_int(info.protection_clock_);
              break;
            }
            case MT_SNAPSHOT_VERSION: {
              cells[i].set_int(info.trans_version_range_.snapshot_version_);
              break;
            }
            default: {
              ret = OB_ERR_UNEXPECTED;
              SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
              break;
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        row = &cur_row_;
        memstore_infos_idx_++;
      }
    }
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
