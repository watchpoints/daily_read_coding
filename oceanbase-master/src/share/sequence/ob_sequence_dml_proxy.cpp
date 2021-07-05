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

#define USING_LOG_PREFIX SHARE
#include "ob_sequence_dml_proxy.h"
#include "lib/string/ob_string.h"
#include "lib/string/ob_sql_string.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "lib/mysqlclient/ob_mysql_result.h"
#include "lib/number/ob_number_v2.h"
#include "common/ob_timeout_ctx.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_struct.h"
#include "share/sequence/ob_sequence_cache.h"
#include "share/sequence/ob_sequence_option_builder.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_service_sql_impl.h"
#include "share/ob_schema_status_proxy.h"
#include "observer/ob_server_struct.h"
#include "observer/ob_sql_client_decorator.h"

using namespace oceanbase::common;
using namespace oceanbase::common::number;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

static const int64_t PREFETCH_OP_TIMEOUT_US = 500 * 1000;  // us

ObSequenceDMLProxy::ObSequenceDMLProxy() : schema_service_(nullptr), sql_proxy_(nullptr), inited_(false)
{}

ObSequenceDMLProxy::~ObSequenceDMLProxy()
{}

void ObSequenceDMLProxy::init(
    share::schema::ObMultiVersionSchemaService& schema_service, common::ObMySQLProxy& sql_proxy)
{
  schema_service_ = &schema_service;
  sql_proxy_ = &sql_proxy;
  inited_ = true;
}

int ObSequenceDMLProxy::set_pre_op_timeout(common::ObTimeoutCtx& ctx)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout_us = ctx.get_abs_timeout();

  if (abs_timeout_us < 0) {
    abs_timeout_us = ObTimeUtility::current_time() + PREFETCH_OP_TIMEOUT_US;
  }
  if (THIS_WORKER.get_timeout_ts() > 0 && THIS_WORKER.get_timeout_ts() < abs_timeout_us) {
    abs_timeout_us = THIS_WORKER.get_timeout_ts();
  }

  if (OB_FAIL(ctx.set_abs_timeout(abs_timeout_us))) {
    LOG_WARN("set timeout failed", K(ret), K(abs_timeout_us));
  } else if (ctx.is_timeouted()) {
    ret = OB_TIMEOUT;
    LOG_WARN("is timeout",
        K(ret),
        "abs_timeout",
        ctx.get_abs_timeout(),
        "this worker timeout ts",
        THIS_WORKER.get_timeout_ts());
  }
  return ret;
}

// Get the next batch of cached data
// begin;
// obj = select * from OB_ALL_SEQUENCE_OBJECT_TNAME for update;
// cache_inclusive_start = obj[next_value];
// cache_exclusive_end   = obj[next_value] + obj[increment_by] * obj[cache_size];
// update OB_ALL_SEQUENCE_OBJECT_TNAME set next_value = cache_inclusive_end;
// commit;
int ObSequenceDMLProxy::next_batch(const uint64_t tenant_id, const uint64_t sequence_id,
    const share::ObSequenceOption& option, SequenceCacheNode& cache_range)
{
  int ret = OB_SUCCESS;
  const char* tname = OB_ALL_SEQUENCE_VALUE_TNAME;
  common::ObMySQLTransaction trans;
  common::ObArenaAllocator allocator(ObModIds::OB_SCHEMA_SEQUENCE);
  ObSqlString sql;
  bool with_snap_shot = true;
  ObNumber next_value;  // default to zero
  ObNumber cache_inclusive_start;
  ObNumber cache_exclusive_end;
  ObNumber cache_size;
  const ObNumber& max_value = option.get_max_value();
  const ObNumber& min_value = option.get_min_value();
  const ObNumber& increment_by = option.get_increment_by();
  bool order_flag = option.get_order_flag();
  bool cycle_flag = option.get_cycle_flag();

  if (true == order_flag) {
    // In order to achieve the purpose of reading the table every time, as long as order_flag = true
    // force to read all_sequence_value one line at a time
    ret = cache_size.from(static_cast<int64_t>(1), allocator);  // allocator_use_1
  } else {
    cache_size.shadow_copy(option.get_cache_size());
  }

  if (OB_FAIL(ret)) {
    // pass
  } else if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("proxy not init", K(ret));
  } else if (OB_FAIL(trans.start(sql_proxy_, with_snap_shot))) {
    LOG_WARN("fail start trans", K(ret));
  }

  bool need_init_sequence_value_table = false;
  // note: res is placed in the block to destroy it as soon as possible, otherwise update later
  // will report an error: connection still be referred by previous sql result
  ObISQLClient* sql_client = &trans;
  ObSQLClientRetryWeak sql_client_retry_weak(sql_client, tenant_id, OB_ALL_SEQUENCE_VALUE_TID);
  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, res)
    {
      common::sqlclient::ObMySQLResult* result = NULL;
      ObNumber tmp;
      if (OB_FAIL(sql.assign_fmt("SELECT NEXT_VALUE FROM %s "
                                 "WHERE SEQUENCE_ID = %lu FOR UPDATE",
              tname,
              extract_pure_id(sequence_id)))) {
        STORAGE_LOG(WARN, "fail format sql", K(ret));
      } else if (OB_FAIL(sql_client_retry_weak.read(res, tenant_id, sql.ptr()))) {
        LOG_WARN("fail to execute sql", K(sql), K(ret));
      } else if (NULL == (result = res.get_result())) {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("can't find sequence", K(tname), K(tenant_id), K(sequence_id));
      } else if (OB_SUCCESS != (ret = result->next())) {
        if (OB_ITER_END == ret) {
          need_init_sequence_value_table = true;
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail get next row", K(ret), K(tname), K(tenant_id), K(sequence_id));
        }
      } else {
        EXTRACT_NUMBER_FIELD_MYSQL(*result, NEXT_VALUE, tmp);
        if (OB_FAIL(ret)) {
          LOG_WARN("fail get NEXT_VALUE", K(ret));
        } else if (OB_FAIL(next_value.from(tmp, allocator))) {
          // Must deep copy, because Res memory will be released after next()
          LOG_WARN("fail deep copy next_val", K(tmp), K(ret));
        } else if (OB_ITER_END != (ret = result->next())) {
          // It is expected that only one row of data should meet the condition, if it is greater
          // than one row, it is an exception
          LOG_WARN("expected OB_ITER_END", K(ret));
          ret = (OB_SUCCESS == ret ? OB_ERR_UNEXPECTED : ret);
        } else {
          ret = OB_SUCCESS;
        }
      }
    }
  }

  if (OB_SUCC(ret) && need_init_sequence_value_table) {
    // When reading data from the all_sequence_object table for the first time, you need to
    // insert a row of initial data in the all_sequence_object table
    if (OB_FAIL(init_sequence_value_table(
            trans, sql_client_retry_weak, allocator, tenant_id, sequence_id, option, next_value))) {
      LOG_WARN("fail init sequence value table", K(sequence_id), K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    // The following piece of logic has the following purpose:
    // 1. calculate the new next_value for updating into the internal table
    // 2. for the cycle scenario, modify next_value to the correct value
    // 3. for the no cycle scenario, it is necessary to judge whether the value range is exceeded

    // cache_inclusive_start = next_value;
    cache_inclusive_start.shadow_copy(next_value);
    // cache_exclusive_end = next_value + increment_by * cache_size;;
    ObNumberCalc inc(increment_by, allocator);
    if (OB_FAIL(inc.mul(cache_size).add(next_value).get_result(cache_exclusive_end))) {
      LOG_WARN("fail get cache_exclusive_end", K(ret));
    } else if (OB_UNLIKELY(increment_by > static_cast<int64_t>(0) && cache_exclusive_end > max_value)) {

      // cache_exclusive_end = max_value + 1;
      // if (cycle_flag) {
      //   next_value = min_value;
      // } else if (next_value > max_value) {
      //   ret = OB_ERR_SEQ_VALUE_EXCEED_LIMIT;
      //   LOG_USER_ERROR(ret, "MAXVALUE");
      // } else {
      //   next_value = cache_exclusive_end;
      // }

      // Note: if cache_exclusive_end exceeds the range, it needs to be corrected to the
      // minimum upper bound. used by the caller to cache an available range, if not corrected
      // the cache range will exceed max_value
      ObNumberCalc mv(max_value, allocator);
      if (OB_FAIL(mv.add(static_cast<int64_t>(1)).get_result(cache_exclusive_end))) {
        LOG_WARN("fail calc cache_exclusive_end", K(ret));
      } else if (cycle_flag) {
        next_value.shadow_copy(min_value);
      } else if (next_value > max_value) {  // no cycle
        ret = OB_ERR_SEQ_VALUE_EXCEED_LIMIT;
        LOG_USER_ERROR(OB_ERR_SEQ_VALUE_EXCEED_LIMIT, "MAXVALUE");
      } else {
        next_value.shadow_copy(cache_exclusive_end);
      }

    } else if (OB_UNLIKELY(increment_by < static_cast<int64_t>(0) && cache_exclusive_end < min_value)) {

      // cache_exclusive_end = min_value - 1;
      // if (cycle_flag) {
      //   next_value = max_value;
      // } else if (next_value < min_value) {
      //   ret = OB_ERR_SEQ_VALUE_EXCEED_LIMIT;
      //   LOG_USER_ERROR(ret, "MINVALUE");
      // } else {
      //   next_value = cache_exclusive_end;
      // }

      // Note: if cache_exclusive_end exceeds the range, it needs to be corrected to the
      // minimum upper bound. used by the caller to cache an available range, if not corrected
      // the cache range will exceed max_value
      ObNumberCalc mv(min_value, allocator);
      if (OB_FAIL(mv.sub(static_cast<int64_t>(1)).get_result(cache_exclusive_end))) {
        LOG_WARN("fail calc cache_exclusive_end", K(ret));
      } else if (cycle_flag) {
        next_value.shadow_copy(max_value);
      } else if (next_value < min_value) {  // no cycle
        ret = OB_ERR_SEQ_VALUE_EXCEED_LIMIT;
        LOG_USER_ERROR(OB_ERR_SEQ_VALUE_EXCEED_LIMIT, "MINVALUE");
      } else {
        next_value.shadow_copy(cache_exclusive_end);
      }

    } else {
      next_value.shadow_copy(cache_exclusive_end);
    }
  }

  // update
  if (OB_SUCC(ret)) {
    int64_t affected_rows = 0;
    if (OB_FAIL(sql.assign_fmt("UPDATE %s SET next_value = %s "
                               "WHERE SEQUENCE_ID = %lu",
            tname,
            next_value.format(),
            extract_pure_id(sequence_id)))) {
      LOG_WARN("format update sql fail", K(ret));
    } else if (GCTX.is_standby_cluster() && OB_SYS_TENANT_ID != tenant_id) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("can't write sys table now", K(ret), K(tenant_id));
    } else if (OB_FAIL(trans.write(tenant_id, sql.ptr(), affected_rows))) {
      LOG_WARN("fail to execute sql", K(sql), K(ret));
    } else {
      if (!is_single_row(affected_rows)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected value", K(affected_rows), K(sql), K(ret));
      }
    }
  }

  if (trans.is_started()) {
    int temp_ret = OB_SUCCESS;
    bool is_commit = (OB_SUCCESS == ret);
    if (OB_SUCCESS != (temp_ret = trans.end(is_commit))) {
      LOG_WARN("trans end failed", "is_commit", is_commit, K(temp_ret));
      ret = (OB_SUCC(ret)) ? temp_ret : ret;
    }
  }

  if (OB_SUCC(ret)) {
    OZ(cache_range.set_start(cache_inclusive_start));
    OZ(cache_range.set_end(cache_exclusive_end));
    LOG_INFO("get next sequence batch success",
        K(tenant_id),
        K(sequence_id),
        "cache_inclusive_start",
        cache_inclusive_start.format(),
        "cache_exclusive_end",
        cache_exclusive_end.format(),
        "increment_by",
        increment_by.format(),
        "cache_size",
        cache_size.format(),
        K(ret));
  }

  return ret;
}

int ObSequenceDMLProxy::prefetch_next_batch(const uint64_t tenant_id, const uint64_t sequence_id,
    const share::ObSequenceOption& option, SequenceCacheNode& cache_range)
{
  int ret = OB_SUCCESS;
  // set timeout for prefetch
  ObTimeoutCtx ctx;
  if (OB_FAIL(set_pre_op_timeout(ctx))) {
    LOG_WARN("failed to set timeout", K(ret));
  } else if (OB_FAIL(next_batch(tenant_id, sequence_id, option, cache_range))) {
    LOG_WARN("fail prefetch sequence batch", K(tenant_id), K(sequence_id), K(option), K(ret));
  }
  return ret;
}

int ObSequenceDMLProxy::init_sequence_value_table(common::ObMySQLTransaction& trans,
    ObSQLClientRetryWeak& sql_client_retry_weak, ObIAllocator& allocator, uint64_t tenant_id, uint64_t sequence_id,
    const ObSequenceOption& option, ObNumber& next_value)
{
  int ret = OB_SUCCESS;
  /*
     insert into t values(...);
     if (succ || dup) {
     select for update
     } else {
     raise err
     }
     */
  int64_t affected_rows = 0;
  ObSqlString sql;
  const char* tname = OB_ALL_SEQUENCE_VALUE_TNAME;
  if (OB_FAIL(sql.assign_fmt("INSERT INTO %s (", tname))) {
    STORAGE_LOG(WARN, "append table name failed, ", K(ret));
  } else {
    ObSqlString values;
    SQL_COL_APPEND_VALUE(sql, values, extract_pure_id(sequence_id), "sequence_id", "%lu");
    // next_value's initial value is same as start_with
    SQL_COL_APPEND_VALUE(sql, values, option.get_start_with().format(), "next_value", "%s");
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      if (OB_FAIL(sql.append_fmt(") VALUES (%.*s)", static_cast<int32_t>(values.length()), values.ptr()))) {
        LOG_WARN("append sql failed, ", K(ret));
      } else if (OB_FAIL(trans.write(tenant_id, sql.ptr(), affected_rows))) {
        if (OB_ERR_PRIMARY_KEY_DUPLICATE == ret) {
          ret = OB_SUCCESS;
          trans.reset_last_error();
          LOG_INFO(
              "Concurrent call sequence nextval() over a newly created sequence", K(tname), K(sequence_id), K(option));
        } else {
          LOG_WARN("fail to execute sql", K(sql), K(ret));
        }
      } else {
        if (!is_single_row(affected_rows)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected value", K(affected_rows), K(sql), K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    ObNumber tmp;
    SMART_VAR(ObMySQLProxy::MySQLResult, res)
    {
      common::sqlclient::ObMySQLResult* result = NULL;
      if (OB_FAIL(sql.assign_fmt("SELECT NEXT_VALUE FROM %s "
                                 "WHERE SEQUENCE_ID = %lu FOR UPDATE",
              tname,
              extract_pure_id(sequence_id)))) {
        STORAGE_LOG(WARN, "fail format sql", K(ret));
      } else if (OB_FAIL(sql_client_retry_weak.read(res, tenant_id, sql.ptr()))) {
        LOG_WARN("fail to execute sql", K(sql), K(ret));
      } else if (NULL == (result = res.get_result())) {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("can't find sequence", K(tname), K(tenant_id), K(sequence_id));
      } else if (OB_SUCCESS != (ret = result->next())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail get next row", K(ret), K(tname), K(tenant_id), K(sequence_id));
      } else {
        EXTRACT_NUMBER_FIELD_MYSQL(*result, NEXT_VALUE, tmp);
        if (OB_FAIL(ret)) {
          LOG_WARN("fail get NEXT_VALUE", K(ret));
        } else if (OB_FAIL(next_value.from(tmp, allocator))) {
          // Must deep copy, because Res memory will be released after next()
          LOG_WARN("fail deep copy next_val", K(tmp), K(ret));
        } else if (OB_ITER_END != (ret = result->next())) {
          // It is expected that only one row of data should meet the condition, if it is greater
          // than one row, it is an exception
          LOG_WARN("expected OB_ITER_END", K(ret));
          ret = (OB_SUCCESS == ret ? OB_ERR_UNEXPECTED : ret);
        } else {
          ret = OB_SUCCESS;
        }
      }
    }
  }

  return ret;
}
