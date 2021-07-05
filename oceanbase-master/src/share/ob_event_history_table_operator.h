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

#ifndef OCEANBASE_SHARE_OB_EVENT_HISTORY_TABLE_OPERATOR_H_
#define OCEANBASE_SHARE_OB_EVENT_HISTORY_TABLE_OPERATOR_H_

#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "lib/string/ob_sql_string.h"
#include "lib/queue/ob_dedup_queue.h"
#include "lib/thread/ob_work_queue.h"
#include "lib/lock/ob_mutex.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/ob_task_define.h"

namespace oceanbase {
namespace common {
class ObMySQLProxy;
}
namespace share {
class ObEventHistoryTableOperator;
class ObEventTableClearTask : public common::ObAsyncTimerTask {
public:
  ObEventTableClearTask(ObEventHistoryTableOperator& rs_event_operator,
      ObEventHistoryTableOperator& server_event_operator, common::ObWorkQueue& work_queue);
  virtual ~ObEventTableClearTask()
  {}

  // interface of AsyncTask
  virtual int process() override;
  virtual int64_t get_deep_copy_size() const override
  {
    return sizeof(*this);
  }
  virtual ObAsyncTask* deep_copy(char* buf, const int64_t buf_size) const override;

private:
  ObEventHistoryTableOperator& rs_event_operator_;
  ObEventHistoryTableOperator& server_event_operator_;
};

class ObEventHistoryTableOperator {
public:
  static const int64_t EVENT_TABLE_CLEAR_INTERVAL = 2L * 3600L * 1000L * 1000L;  // 2 Hours
  class ObEventTableUpdateTask : public common::IObDedupTask {
  public:
    ObEventTableUpdateTask(ObEventHistoryTableOperator& table_operator, const bool is_delete);
    virtual ~ObEventTableUpdateTask()
    {}
    int init(const char* ptr, const int64_t buf_size);
    bool is_valid() const;
    virtual int64_t hash() const;
    virtual bool operator==(const common::IObDedupTask& other) const;
    virtual int64_t get_deep_copy_size() const
    {
      return sizeof(*this) + sql_.length();
    }
    virtual common::IObDedupTask* deep_copy(char* buf, const int64_t buf_size) const;
    virtual int64_t get_abs_expired_time() const
    {
      return 0;
    }
    virtual int process();

  public:
    void assign_ptr(char* ptr, const int64_t buf_size)
    {
      sql_.assign_ptr(ptr, static_cast<int32_t>(buf_size));
    }

    TO_STRING_KV(K_(sql), K_(is_delete));

  private:
    ObEventHistoryTableOperator& table_operator_;
    common::ObString sql_;
    bool is_delete_;

    DISALLOW_COPY_AND_ASSIGN(ObEventTableUpdateTask);
  };

  virtual ~ObEventHistoryTableOperator();
  int init(common::ObMySQLProxy& proxy);
  bool is_inited() const
  {
    return inited_;
  }
  void destroy();
  template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1, const char* name2,
      const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4, const char* name5,
      const T5& value5, const char* name6, const T6& value6, const T7& extra_value);
  template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1, const char* name2,
      const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4, const char* name5,
      const T5& value5, const char* name6, const T6& value6);
  template <typename T1, typename T2, typename T3, typename T4, typename T5>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1, const char* name2,
      const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4, const char* name5,
      const T5& value5);
  template <typename T1, typename T2, typename T3, typename T4>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1, const char* name2,
      const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4);
  template <typename T1, typename T2, typename T3>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1, const char* name2,
      const T2& value2, const char* name3, const T3& value3);
  template <typename T1, typename T2>
  int add_event(
      const char* module, const char* event, const char* name1, const T1& value1, const char* name2, const T2& value2);
  template <typename T1>
  int add_event(const char* module, const char* event, const char* name1, const T1& value1);
  int add_event(const char* module, const char* event);

  virtual int async_delete() = 0;

protected:
  void set_addr(const common::ObAddr self_addr, bool is_rs_ev)
  {
    self_addr_ = self_addr;
    is_rootservice_event_history_ = is_rs_ev;
  }
  const common::ObAddr& get_addr() const
  {
    return self_addr_;
  }
  void set_event_table(const char* tname)
  {
    event_table_name_ = tname;
  }
  const char* get_event_table() const
  {
    return event_table_name_;
  }
  int add_task(const common::ObSqlString& sql, const bool is_delete = false);
  int gen_event_ts(int64_t& event_ts);

protected:
  static const int64_t TOTAL_LIMIT = 320L * 1024L * 1024L;  // 320MB
  static const int64_t HOLD_LIMIT = 160L * 1024L * 1024L;   // 160MB
  static const int64_t PAGE_SIZE = common::OB_MALLOC_NORMAL_BLOCK_SIZE;
  static const int64_t TASK_MAP_SIZE = 20 * 1024;
  static const int64_t TASK_QUEUE_SIZE = 20 * 1024;
  static const int64_t RS_EVENT_HISTORY_DELETE_TIME = 7L * 24L * 3600L * 1000L * 1000L;      // 7DAY
  static const int64_t SERVER_EVENT_HISTORY_DELETE_TIME = 2L * 24L * 3600L * 1000L * 1000L;  // 2DAY
  static const int64_t UNIT_LOAD_HISTORY_DELETE_TIME = 7L * 24L * 3600L * 1000L * 1000L;     // 2DAY

  virtual int process_task(const common::ObString& sql, const bool is_delete);

private:
  bool inited_;
  volatile bool stopped_;
  int64_t last_event_ts_;
  lib::ObMutex lock_;
  common::ObMySQLProxy* proxy_;
  common::ObDedupQueue event_queue_;
  const char* event_table_name_;
  common::ObAddr self_addr_;
  bool is_rootservice_event_history_;

protected:
  ObEventHistoryTableOperator();

private:
  DISALLOW_COPY_AND_ASSIGN(ObEventHistoryTableOperator);
};

template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1,
    const char* name2, const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4,
    const char* name5, const T5& value5, const char* name6, const T6& value6, const T7& extra_info_value)
{
  int ret = common::OB_SUCCESS;
  int64_t event_ts = 0;
  common::ObSqlString sql;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event) {
    // name[1-6] can be null, don't need to check them
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "neither module or event can be NULL", KP(module), KP(event), K(ret));
  } else if (OB_FAIL(gen_event_ts(event_ts))) {
    SHARE_LOG(WARN, "gen_event_ts failed", K(ret));
  } else {
    share::ObDMLSqlSplicer dml;
    if (OB_FAIL(dml.add_gmt_create(event_ts)) || OB_FAIL(dml.add_column("module", module)) ||
        OB_FAIL(dml.add_column("event", event))) {
      SHARE_LOG(WARN, "add column failed", K(ret));
    }
    if (common::OB_SUCCESS == ret && NULL != name1) {
      if (OB_FAIL(dml.add_column("name1", name1)) || OB_FAIL(dml.add_column("value1", value1))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    if (common::OB_SUCCESS == ret && NULL != name2) {
      if (OB_FAIL(dml.add_column("name2", name2)) || OB_FAIL(dml.add_column("value2", value2))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    if (common::OB_SUCCESS == ret && NULL != name3) {
      if (OB_FAIL(dml.add_column("name3", name3)) || OB_FAIL(dml.add_column("value3", value3))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    if (common::OB_SUCCESS == ret && NULL != name4) {
      if (OB_FAIL(dml.add_column("name4", name4)) || OB_FAIL(dml.add_column("value4", value4))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    if (common::OB_SUCCESS == ret && NULL != name5) {
      if (OB_FAIL(dml.add_column("name5", name5)) || OB_FAIL(dml.add_column("value5", value5))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    if (common::OB_SUCCESS == ret && NULL != name6) {
      if (OB_FAIL(dml.add_column("name6", name6)) || OB_FAIL(dml.add_column("value6", value6)) ||
          OB_FAIL(dml.add_column("extra_info", extra_info_value))) {
        SHARE_LOG(WARN, "add column failed", K(ret));
      }
    }
    char ip_buf[common::MAX_IP_ADDR_LENGTH];
    if (common::OB_SUCCESS == ret && self_addr_.is_valid()) {
      if (is_rootservice_event_history_) {
        // Add rs_svr_ip, rs_svr_port to the __all_rootservice_event_history table
        (void)self_addr_.ip_to_string(ip_buf, common::MAX_IP_ADDR_LENGTH);
        if (OB_FAIL(dml.add_column("rs_svr_ip", ip_buf)) ||
            OB_FAIL(dml.add_column("rs_svr_port", self_addr_.get_port()))) {
          SHARE_LOG(WARN, "add column failed", K(ret));
        }
      } else {
        // __all_server_event_history has svr_ip and svr_port, but __all_rootservice_event_history hasn't.
        (void)self_addr_.ip_to_string(ip_buf, common::MAX_IP_ADDR_LENGTH);
        if (OB_FAIL(dml.add_column("svr_ip", ip_buf)) || OB_FAIL(dml.add_column("svr_port", self_addr_.get_port()))) {
          SHARE_LOG(WARN, "add column failed", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(dml.splice_insert_sql(event_table_name_, sql))) {
        SHARE_LOG(WARN, "splice_insert_sql failed", K(ret));
      } else if (OB_FAIL(add_task(sql))) {
        SHARE_LOG(WARN, "add_task failed", K(sql), K(ret));
      }
    }
  }
  ObTaskController::get().allow_next_syslog();
  SHARE_LOG(INFO, "event table add task", K(ret), K_(event_table_name), K(sql));
  return ret;
}

template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1,
    const char* name2, const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4,
    const char* name5, const T5& value5, const char* name6, const T6& value6)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1 || NULL == name2 || NULL == name3 || NULL == name4 ||
             NULL == name5 || NULL == name6) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN,
        "invalid argument",
        KP(module),
        KP(event),
        KP(name1),
        KP(name2),
        KP(name3),
        KP(name4),
        KP(name5),
        KP(name6),
        K(ret));
  } else if (OB_FAIL(add_event(module,
                 event,
                 name1,
                 value1,
                 name2,
                 value2,
                 name3,
                 value3,
                 name4,
                 value4,
                 name5,
                 value5,
                 name6,
                 value6,
                 ""))) {
    SHARE_LOG(WARN,
        "add event failed",
        KP(module),
        KP(event),
        KP(name1),
        K(value1),
        KP(name2),
        K(value2),
        KP(name3),
        K(value3),
        KP(name4),
        K(value4),
        KP(name5),
        K(value5),
        KP(name6),
        K(value6),
        K(ret));
  }
  return ret;
}

template <typename T1, typename T2, typename T3, typename T4, typename T5>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1,
    const char* name2, const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4,
    const char* name5, const T5& value5)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1 || NULL == name2 || NULL == name3 || NULL == name4 ||
             NULL == name5) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(
        WARN, "invalid argument", KP(module), KP(event), KP(name1), KP(name2), KP(name3), KP(name4), KP(name5), K(ret));
  } else if (OB_FAIL(add_event(module,
                 event,
                 name1,
                 value1,
                 name2,
                 value2,
                 name3,
                 value3,
                 name4,
                 value4,
                 name5,
                 value5,
                 NULL,
                 "",
                 ""))) {
    SHARE_LOG(WARN,
        "add event failed",
        KP(module),
        KP(event),
        KP(name1),
        K(value1),
        KP(name2),
        K(value2),
        KP(name3),
        K(value3),
        KP(name4),
        K(value4),
        KP(name5),
        K(value5),
        K(ret));
  }
  return ret;
}

template <typename T1, typename T2, typename T3, typename T4>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1,
    const char* name2, const T2& value2, const char* name3, const T3& value3, const char* name4, const T4& value4)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1 || NULL == name2 || NULL == name3 || NULL == name4) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KP(module), KP(event), KP(name1), KP(name2), KP(name3), KP(name4), K(ret));
  } else if (OB_FAIL(add_event(
                 module, event, name1, value1, name2, value2, name3, value3, name4, value4, NULL, "", NULL, "", ""))) {
    SHARE_LOG(WARN,
        "add event failed",
        KP(module),
        KP(event),
        KP(name1),
        K(value1),
        KP(name2),
        K(value2),
        KP(name3),
        K(value3),
        KP(name4),
        K(value4),
        K(ret));
  }
  return ret;
}

template <typename T1, typename T2, typename T3>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1,
    const char* name2, const T2& value2, const char* name3, const T3& value3)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1 || NULL == name2 || NULL == name3) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KP(module), KP(event), KP(name1), KP(name2), KP(name3), K(ret));
  } else if (OB_FAIL(add_event(
                 module, event, name1, value1, name2, value2, name3, value3, NULL, "", NULL, "", NULL, "", ""))) {
    SHARE_LOG(WARN,
        "add event failed",
        KP(module),
        KP(event),
        KP(name1),
        K(value1),
        KP(name2),
        K(value2),
        KP(name3),
        K(value3),
        K(ret));
  }
  return ret;
}

template <typename T1, typename T2>
int ObEventHistoryTableOperator::add_event(
    const char* module, const char* event, const char* name1, const T1& value1, const char* name2, const T2& value2)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1 || NULL == name2) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KP(module), KP(event), KP(name1), KP(name2), K(ret));
  } else if (OB_FAIL(
                 add_event(module, event, name1, value1, name2, value2, NULL, "", NULL, "", NULL, "", NULL, "", ""))) {
    SHARE_LOG(WARN, "add event failed", KP(module), KP(event), KP(name1), K(value1), KP(name2), K(value2), K(ret));
  }
  return ret;
}

template <typename T1>
int ObEventHistoryTableOperator::add_event(const char* module, const char* event, const char* name1, const T1& value1)
{
  int ret = common::OB_SUCCESS;
  if (!inited_) {
    ret = common::OB_NOT_INIT;
    SHARE_LOG(WARN, "not init", K(ret));
  } else if (NULL == module || NULL == event || NULL == name1) {
    ret = common::OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KP(module), KP(event), KP(name1), K(ret));
  } else if (OB_FAIL(add_event(module, event, name1, value1, NULL, "", NULL, "", NULL, "", NULL, "", NULL, "", ""))) {
    SHARE_LOG(WARN, "add event failed", KP(module), KP(event), KP(name1), K(value1), K(ret));
  }
  return ret;
}

}  // namespace share
}  // end namespace oceanbase

#endif
