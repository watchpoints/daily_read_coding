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

#include "lib/stat/ob_latch_define.h"
namespace oceanbase {
namespace common {
const ObLatchDesc OB_LATCHES[] = {
#define LATCH_DEF(def, id, name, policy, max_spin_cnt, max_yield_cnt, wait_event, display_name) \
  {id, name, ObLatchPolicy::policy, max_spin_cnt, max_yield_cnt, ObWaitEventIds::wait_event, display_name},
#include "lib/stat/ob_latch_define.h"
#undef LATCH_DEF
};

}  // namespace common
}  // namespace oceanbase
