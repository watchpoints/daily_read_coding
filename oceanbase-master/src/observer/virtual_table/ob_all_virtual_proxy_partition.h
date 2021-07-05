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

#ifndef OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITOIN_
#define OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITOIN_

#include "ob_all_virtual_proxy_base.h"

namespace oceanbase {
namespace share {
namespace schema {
class ObPartition;
class ObPartIteratorV2;
}  // namespace schema
}  // namespace share
namespace observer {
class ObAllVirtualProxyPartition : public ObAllVirtualProxyBaseIterator {
  enum ALL_VIRTUAL_PROXY_PARTITOIN_TABLE_COLUMNS {
    TABLE_ID = oceanbase::common::OB_APP_MIN_COLUMN_ID,
    PART_ID,

    TENANT_ID,
    PART_NAME,
    STATUS,
    LOW_BOUND_VAL,
    LOW_BOUND_VAL_BIN,
    HIGH_BOUND_VAL,
    HIGH_BOUND_VAL_BIN,
    PART_IDX,

    SUB_PART_NUM,
    SUB_PART_SPACE,
    SUB_PART_INTERVAL,
    SUB_PART_INTERVAL_BIN,
    SUB_INTERVAL_START,
    SUB_INTERVAL_START_BIN,

    SPARE1,
    SPARE2,
    SPARE3,
    SPARE4,
    SPARE5,
    SPARE6,
  };
  enum ALL_VIRTUAL_PROXY_PARTITOIN_TABLE_ROWKEY_IDX {
    TABLE_ID_IDX = 0,
    PART_ID_IDX,
    ROW_KEY_COUNT,
  };

public:
  ObAllVirtualProxyPartition();
  virtual ~ObAllVirtualProxyPartition();

  virtual int inner_open();
  virtual int inner_get_next_row();

  int fill_cells(const share::schema::ObPartition& table_schema);

private:
  share::schema::ObPartIteratorV2* iter_;
  share::schema::ObPartitionFuncType part_func_type_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualProxyPartition);
};

}  // end of namespace observer
}  // end of namespace oceanbase
#endif /* OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITOIN_ */
