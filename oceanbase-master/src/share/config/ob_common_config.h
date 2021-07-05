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

#ifndef OCEANBASE_SHARE_CONFIG_OB_COMMON_CONFIG_H_
#define OCEANBASE_SHARE_CONFIG_OB_COMMON_CONFIG_H_

#include "lib/net/ob_addr.h"
#include "share/config/ob_config.h"

namespace oceanbase {
namespace common {

class ObInitConfigContainer {
public:
  const ObConfigContainer& get_container();

protected:
  ObInitConfigContainer();
  virtual ~ObInitConfigContainer()
  {}
  static ObConfigContainer*& local_container();
  ObConfigContainer container_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObInitConfigContainer);
};

// derive from ObInitConfigContainer to make sure config container inited before config item.
class ObCommonConfig : public ObInitConfigContainer {
public:
  ObCommonConfig();
  virtual ~ObCommonConfig();

  virtual int check_all() const = 0;
  virtual void print() const = 0;
  virtual void print_need_reboot_config() const {/*do nothing*/};
  virtual ObServerRole get_server_type() const = 0;
  virtual int add_extra_config(const char* config_str, const int64_t version = 0, const bool check_name = false);
  virtual bool is_debug_sync_enabled() const
  {
    return false;
  }

  OB_UNIS_VERSION_V(1);

protected:
  static const int64_t MIN_LENGTH = 20;

private:
  DISALLOW_COPY_AND_ASSIGN(ObCommonConfig);
};

}  // end of namespace common
}  // end of namespace oceanbase

#endif  // OCEANBASE_SHARE_CONFIG_OB_COMMON_CONFIG_H_
