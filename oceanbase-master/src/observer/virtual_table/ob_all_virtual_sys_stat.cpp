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

#define USING_LOG_PREFIX SERVER

#include "ob_all_virtual_sys_stat.h"
#include "lib/ob_running_mode.h"
#include "observer/ob_server_struct.h"
#include "observer/ob_server.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/cache/ob_kv_storecache.h"
#include "election/ob_election_async_log.h"

namespace oceanbase {
namespace observer {

ObAllVirtualSysStat::ObAllVirtualSysStat()
    : ObVirtualTableScannerIterator(),
      tenant_dis_(),
      addr_(NULL),
      ipstr_(),
      port_(0),
      iter_(0),
      stat_iter_(0),
      tenant_id_(OB_INVALID_ID)
{}

ObAllVirtualSysStat::~ObAllVirtualSysStat()
{
  reset();
}

void ObAllVirtualSysStat::reset()
{
  ObVirtualTableScannerIterator::reset();
  addr_ = NULL;
  port_ = 0;
  ipstr_.reset();
  iter_ = 0;
  stat_iter_ = 0;
  tenant_id_ = OB_INVALID_ID;
  tenant_dis_.reset();
}

int ObAllVirtualSysStat::set_ip(common::ObAddr* addr)
{
  int ret = OB_SUCCESS;
  char ipbuf[common::OB_IP_STR_BUFF];
  if (NULL == addr) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (!addr_->ip_to_string(ipbuf, sizeof(ipbuf))) {
    SERVER_LOG(ERROR, "ip to string failed");
    ret = OB_ERR_UNEXPECTED;
  } else {
    ipstr_ = ObString::make_string(ipbuf);
    if (OB_FAIL(ob_write_string(*allocator_, ipstr_, ipstr_))) {
      SERVER_LOG(WARN, "failed to write string", K(ret));
    }
    port_ = addr_->get_port();
  }
  return ret;
}

int ObAllVirtualSysStat::get_all_diag_info()
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS !=
      (ret = common::ObDIGlobalTenantCache::get_instance().get_all_stat_event(*allocator_, tenant_dis_))) {
    SERVER_LOG(WARN, "Fail to get tenant status, ", K(ret));
  }
  return ret;
}

int ObAllVirtualSysStat::inner_get_next_row(ObNewRow*& row)
{
  int ret = OB_SUCCESS;
  ObObj* cells = cur_row_.cells_;
  if (OB_UNLIKELY(NULL == allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator is NULL", K(ret));
  } else {
    const int64_t col_count = output_column_ids_.count();

    if (0 == iter_ && 0 == stat_iter_) {
      if (OB_SUCCESS != (ret = set_ip(addr_))) {
        SERVER_LOG(WARN, "can't get ip", K(ret));
      } else if (OB_SUCCESS != (ret = get_all_diag_info())) {
        SERVER_LOG(WARN, "Fail to get tenant status", K(ret));
      }
    }

    if (iter_ >= tenant_dis_.count()) {
      ret = OB_ITER_END;
    }

    if (OB_SUCCESS == ret && 0 == stat_iter_) {
      tenant_id_ = tenant_dis_.at(iter_).first;
      ObStatEventSetStatArray& stat_events = tenant_dis_.at(iter_).second->get_set_stat_stats();
      if (OB_FAIL(get_cache_size(stat_events))) {
        SERVER_LOG(WARN, "Fail to get cache size", K(ret));
      } else {
        ObTenantManager& tenant_mgr = common::ObTenantManager::get_instance();
        int64_t unused = 0;
        // ignore ret

        tenant_mgr.get_tenant_memstore_cond(tenant_id_,
            stat_events.get(ObStatEventIds::ACTIVE_MEMSTORE_USED - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_,
            stat_events.get(ObStatEventIds::TOTAL_MEMSTORE_USED - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_,
            stat_events.get(ObStatEventIds::MAJOR_FREEZE_TRIGGER - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_,
            stat_events.get(ObStatEventIds::MEMSTORE_LIMIT - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_,
            unused);
        tenant_mgr.get_tenant_mem_limit(tenant_id_,
            stat_events.get(ObStatEventIds::MIN_MEMORY_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_,
            stat_events.get(ObStatEventIds::MAX_MEMORY_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_);
        tenant_mgr.get_tenant_disk_total_used(tenant_id_,
            stat_events.get(ObStatEventIds::DISK_USAGE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_);

        stat_events.get(ObStatEventIds::OBLOGGER_WRITE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_write_size() : 0;
        stat_events.get(ObStatEventIds::ELECTION_WRITE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_elec_write_size() : 0;
        stat_events.get(ObStatEventIds::ROOTSERVICE_WRITE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_rs_write_size() : 0;
        stat_events.get(ObStatEventIds::OBLOGGER_TOTAL_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_total_write_count() : 0;
        stat_events.get(ObStatEventIds::ELECTION_TOTAL_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_elec_total_write_count() : 0;
        stat_events.get(ObStatEventIds::ROOTSERVICE_TOTAL_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_rs_total_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_ERROR_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_error_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_WARN_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_warn_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_INFO_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_info_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_TRACE_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_trace_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_DEBUG_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_debug_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_LOG_FLUSH_SPEED - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_async_flush_log_speed() : 0;

        stat_events.get(ObStatEventIds::ASYNC_GENERIC_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_generic_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_USER_REQUEST_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_user_request_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_DATA_MAINTAIN_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_data_maintain_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_ROOT_SERVICE_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_root_service_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_SCHEMA_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_schema_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_FORCE_ALLOW_LOG_WRITE_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_force_allow_log_write_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_GENERIC_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_generic_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_USER_REQUEST_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_user_request_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_DATA_MAINTAIN_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_data_maintain_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_ROOT_SERVICE_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_root_service_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_SCHEMA_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_schema_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_FORCE_ALLOW_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_force_allow_log_dropped_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_ERROR_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_error_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_WARN_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_warn_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_INFO_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_info_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_TRACE_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_trace_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_DEBUG_LOG_DROPPED_COUNT - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_dropped_debug_log_count() : 0;
        stat_events.get(ObStatEventIds::ASYNC_LOG_FLUSH_SPEED - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? OB_LOGGER.get_async_flush_log_speed() : 0;
        stat_events.get(ObStatEventIds::MEMORY_HOLD_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? lib::AChunkMgr::instance().get_hold() : 0;
        stat_events.get(ObStatEventIds::MEMORY_USED_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? lib::AChunkMgr::instance().get_used() : 0;
        stat_events.get(ObStatEventIds::MEMORY_FREE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? lib::AChunkMgr::instance().get_freelist_hold() : 0;
        stat_events.get(ObStatEventIds::IS_MINI_MODE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            (OB_SYS_TENANT_ID == tenant_id_) ? (lib::is_mini_mode() ? 1 : 0) : -1;

        int ret_bk = ret;
        if (NULL != GCTX.omt_) {
          double cpu_usage = .0;
          if (!OB_FAIL(GCTX.omt_->get_tenant_cpu_usage(tenant_id_, cpu_usage))) {
            stat_events.get(ObStatEventIds::CPU_USAGE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
                static_cast<int64_t>(cpu_usage * 100);
            stat_events.get(ObStatEventIds::MEMORY_USAGE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
                get_tenant_memory_usage(tenant_id_);
          } else {
            // it is ok to not have any records
          }

          double min_cpu = .0;
          double max_cpu = .0;
          if (!OB_FAIL(GCTX.omt_->get_tenant_cpu(tenant_id_, min_cpu, max_cpu))) {
            stat_events.get(ObStatEventIds::MIN_CPUS - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
                static_cast<int64_t>(min_cpu * 100);
            stat_events.get(ObStatEventIds::MAX_CPUS - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
                static_cast<int64_t>(max_cpu * 100);
          } else {
            // it is ok to not have any records
          }
        }
        if (NULL != GCTX.ob_service_) {
          stat_events
              .get(ObStatEventIds::OBSERVER_PARTITION_TABLE_UPATER_USER_QUEUE_SIZE -
                   ObStatEventIds::STAT_EVENT_ADD_END - 1)
              ->stat_value_ = GCTX.ob_service_->get_partition_table_updater_user_queue_size();
          stat_events
              .get(ObStatEventIds::OBSERVER_PARTITION_TABLE_UPATER_SYS_QUEUE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END -
                   1)
              ->stat_value_ = GCTX.ob_service_->get_partition_table_updater_sys_queue_size();
          stat_events
              .get(ObStatEventIds::OBSERVER_PARTITION_TABLE_UPATER_CORE_QUEUE_SIZE -
                   ObStatEventIds::STAT_EVENT_ADD_END - 1)
              ->stat_value_ = GCTX.ob_service_->get_partition_table_updater_core_queue_size();
        }
        ret = ret_bk;
      }
    }

    if (OB_SUCC(ret)) {
      uint64_t cell_idx = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
          case TENANT_ID: {
            cells[cell_idx].set_int(tenant_id_);
            break;
          }
          case SVR_IP: {
            cells[cell_idx].set_varchar(ipstr_);
            cells[cell_idx].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SVR_PORT: {
            cells[cell_idx].set_int(port_);
            break;
          }
          case STATISTIC: {
            if (stat_iter_ < ObStatEventIds::STAT_EVENT_ADD_END) {
              cells[cell_idx].set_int(stat_iter_);
            } else {
              cells[cell_idx].set_int(stat_iter_ - 1);
            }
            break;
          }
          case VALUE: {
            if (stat_iter_ < ObStatEventIds::STAT_EVENT_ADD_END) {
              ObStatEventAddStat* stat = tenant_dis_.at(iter_).second->get_add_stat_stats().get(stat_iter_);
              if (NULL == stat) {
                ret = OB_INVALID_ARGUMENT;
                SERVER_LOG(WARN, "The argument is invalid, ", K(stat_iter_), K(ret));
              } else {
                cells[cell_idx].set_int(stat->stat_value_);
              }
            } else {
              ObStatEventSetStat* stat = tenant_dis_.at(iter_).second->get_set_stat_stats().get(
                  stat_iter_ - ObStatEventIds::STAT_EVENT_ADD_END - 1);
              if (NULL == stat) {
                ret = OB_INVALID_ARGUMENT;
                SERVER_LOG(WARN, "The argument is invalid, ", K(stat_iter_), K(ret));
              } else {
                cells[cell_idx].set_int(stat->stat_value_);
              }
            }
            break;
          }
          case STAT_ID: {
            cells[cell_idx].set_int(OB_STAT_EVENTS[stat_iter_].stat_id_);
            break;
          }
          case NAME: {
            cells[cell_idx].set_varchar(OB_STAT_EVENTS[stat_iter_].name_);
            cells[cell_idx].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case CLASS: {
            cells[cell_idx].set_int(OB_STAT_EVENTS[stat_iter_].stat_class_);
            break;
          }
          case CAN_VISIBLE: {
            cells[cell_idx].set_bool(OB_STAT_EVENTS[stat_iter_].can_visible_);
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx), K(output_column_ids_), K(col_id));
            break;
          }
        }
        if (OB_SUCC(ret)) {
          cell_idx++;
        }
      }
    }

    if (OB_SUCC(ret)) {
      stat_iter_++;
      row = &cur_row_;
      if (ObStatEventIds::STAT_EVENT_ADD_END == stat_iter_) {
        stat_iter_++;
      }
      if (stat_iter_ >= ObStatEventIds::STAT_EVENT_SET_END) {
        stat_iter_ = 0;
        iter_++;
      }
    }
  }
  return ret;
}

int64_t ObAllVirtualSysStat::get_tenant_memory_usage(int64_t tenant_id) const
{
  return lib::get_tenant_memory_hold(tenant_id);
}

int ObAllVirtualSysStat::get_cache_size(ObStatEventSetStatArray& stat_events)
{
  int ret = OB_SUCCESS;
  ObArray<ObKVCacheInstHandle> inst_handles;
  if (OB_FAIL(ObKVGlobalCache::get_instance().get_tenant_cache_info(tenant_id_, inst_handles))) {
    SERVER_LOG(WARN, "Fail to get tenant cache infos, ", K(ret));
  } else {
    ObKVCacheInst* inst = NULL;
    for (int64_t i = 0; i < inst_handles.count(); ++i) {
      inst = inst_handles.at(i).get_inst();
      if (OB_ISNULL(inst)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "ObKVCacheInstHandle with NULL ObKVCacheInst", K(ret));
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "location_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::LOCATION_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "clog_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::CLOG_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "index_clog_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::INDEX_CLOG_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "user_tab_col_stat_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::USER_TABLE_COL_STAT_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)
            ->stat_value_ = inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "block_index_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::INDEX_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "user_block_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::USER_BLOCK_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "user_row_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::USER_ROW_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else if (0 == STRNCMP(inst->status_.config_->cache_name_, "bf_cache", MAX_CACHE_NAME_LENGTH)) {
        stat_events.get(ObStatEventIds::BLOOM_FILTER_CACHE_SIZE - ObStatEventIds::STAT_EVENT_ADD_END - 1)->stat_value_ =
            inst->status_.map_size_ + inst->status_.store_size_;
      } else {
        // do nothing
      }
    }
  }
  return ret;
}

int ObAllVirtualSysStatI1::get_all_diag_info()
{
  int ret = OB_SUCCESS;
  void* buf = NULL;
  int64_t index_id = -1;
  uint64_t key = 0;
  std::pair<uint64_t, common::ObDiagnoseTenantInfo*> pair;
  for (int64_t i = 0; OB_SUCC(ret) && i < get_index_ids().count(); ++i) {
    index_id = get_index_ids().at(i);
    if (0 < index_id) {
      key = static_cast<uint64_t>(index_id);
      pair.first = key;
      if (NULL == (buf = allocator_->alloc(sizeof(common::ObDiagnoseTenantInfo)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        pair.second = new (buf) common::ObDiagnoseTenantInfo();
        if (OB_SUCCESS !=
            (ret = common::ObDIGlobalTenantCache::get_instance().get_the_diag_info(key, *(pair.second)))) {
          if (OB_ENTRY_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
          } else {
            SERVER_LOG(WARN, "Fail to get tenant status, ", K(ret));
          }
        } else {
          if (OB_SUCCESS != (ret = tenant_dis_.push_back(pair))) {
            SERVER_LOG(WARN, "Fail to push diag info value to array, ", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

} /* namespace observer */
} /* namespace oceanbase */
