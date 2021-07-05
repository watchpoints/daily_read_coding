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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_tenant_sql_service.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_sql_string.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_schema_service.h"

namespace oceanbase {
using namespace common;
namespace share {
namespace schema {

int ObTenantSqlService::insert_tenant(const ObTenantSchema& tenant_schema, const ObSchemaOperationType op,
    ObISQLClient& sql_client, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }
  return ret;
}

int ObTenantSqlService::rename_tenant(
    const ObTenantSchema& tenant_schema, ObISQLClient& sql_client, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  const ObSchemaOperationType op = OB_DDL_RENAME_TENANT;

  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }

  return ret;
}

int ObTenantSqlService::alter_tenant(const ObTenantSchema& tenant_schema, ObISQLClient& sql_client,
    const ObSchemaOperationType op, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }
  return ret;
}

int ObTenantSqlService::delay_to_drop_tenant(
    const ObTenantSchema& tenant_schema, ObISQLClient& sql_client, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  const ObSchemaOperationType op = OB_DDL_DEL_TENANT_START;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }
  return ret;
}

int ObTenantSqlService::drop_tenant_to_recyclebin(const ObTenantSchema& tenant_schema, ObISQLClient& sql_client,
    const ObSchemaOperationType op, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant schema", K(tenant_schema), K(ret));
  } else if (OB_FAIL(replace_tenant(tenant_schema, op, sql_client, ddl_stmt_str))) {
    LOG_WARN("replace_tenant failed", K(tenant_schema), K(op), K(ret));
  }
  return ret;
}

int ObTenantSqlService::delete_tenant(const uint64_t tenant_id, const int64_t new_schema_version,
    common::ObISQLClient& sql_client, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const int64_t IS_DELETED = 1;

  // delete from __all_tenant
  if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE tenant_id = %lu", OB_ALL_TENANT_TNAME, tenant_id))) {
    LOG_WARN("format sql failed", K(sql), K(ret));
  } else if (OB_FAIL(sql_client.write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
    LOG_WARN("execute sql failed", K(sql), K(ret));
  } else if (!is_single_row(affected_rows)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("affected_rows is expected to one", K(affected_rows), K(ret));
  }

  // mark __all_tenant_history
  if (OB_SUCC(ret)) {
    if (FAILEDx(sql.assign_fmt("INSERT INTO %s(tenant_id, schema_version, is_deleted) "
                               "VALUES(%lu, %ld, %ld)",
            OB_ALL_TENANT_HISTORY_TNAME,
            tenant_id,
            new_schema_version,
            IS_DELETED))) {
      LOG_WARN("format sql failed", K(sql), K(ret));
    } else if (OB_FAIL(sql_client.write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
      LOG_WARN("execute sql failed", K(sql), K(ret));
    } else if (!is_single_row(affected_rows)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("affected_rows is expected to one", K(affected_rows), K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObSchemaOperation delete_tenant_op;
    delete_tenant_op.tenant_id_ = tenant_id;
    delete_tenant_op.database_id_ = 0;
    delete_tenant_op.tablegroup_id_ = 0;
    delete_tenant_op.table_id_ = 0;
    delete_tenant_op.op_type_ = OB_DDL_DEL_TENANT_END;
    delete_tenant_op.schema_version_ = new_schema_version;
    delete_tenant_op.ddl_stmt_str_ = ddl_stmt_str ? *ddl_stmt_str : ObString();
    int64_t sql_tenant_id = OB_SYS_TENANT_ID;
    if (OB_FAIL(log_operation(delete_tenant_op, sql_client, sql_tenant_id))) {
      LOG_WARN("log delete tenant ddl operation failed", K(delete_tenant_op), K(ret));
    }
  }
  return ret;
}

int ObTenantSqlService::delete_tenant_content(ObISQLClient& client, const uint64_t tenant_id, const char* table_name)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  const uint64_t exec_tenant_id = ObSchemaUtils::get_exec_tenant_id(tenant_id);
  if (OB_INVALID_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", K(tenant_id), K(ret));
  } else if (OB_ISNULL(table_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table_name is null", K(ret));
  } else if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE tenant_id = %lu",
                 table_name,
                 ObSchemaUtils::get_extract_tenant_id(exec_tenant_id, tenant_id)))) {
    LOG_WARN("assign_fmt failed", K(ret));
  } else if (OB_FAIL(client.write(exec_tenant_id, sql.ptr(), affected_rows))) {
    LOG_WARN("execute sql failed", K(sql), K(ret));
  }
  return ret;
}

int ObTenantSqlService::replace_tenant(const ObTenantSchema& tenant_schema, const ObSchemaOperationType op,
    common::ObISQLClient& sql_client, const ObString* ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  common::ObArray<common::ObZone> zone_list;
  char* zone_list_buf = NULL;
  if (!tenant_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant_schema is invalid", "tenant_schema", to_cstring(tenant_schema), K(ret));
  } else if (OB_DDL_ADD_TENANT != op && OB_DDL_ADD_TENANT_START != op && OB_DDL_ADD_TENANT_END != op &&
             OB_DDL_ALTER_TENANT != op && OB_DDL_DEL_TENANT_START != op && OB_DDL_DROP_TENANT_TO_RECYCLEBIN != op &&
             OB_DDL_RENAME_TENANT != op && OB_DDL_FLASHBACK_TENANT != op) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid replace tenant op", K(op), K(ret));
  } else if (OB_FAIL(tenant_schema.get_zone_list(zone_list))) {
    LOG_WARN("fail to get zone list", K(ret));
  } else if (OB_UNLIKELY(
                 NULL == (zone_list_buf = static_cast<char*>(ob_malloc(MAX_ZONE_LIST_LENGTH, ObModIds::OB_SCHEMA))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else if (OB_FAIL(tenant_schema.zone_array2str(zone_list, zone_list_buf, MAX_ZONE_LIST_LENGTH))) {
    LOG_WARN("fial to convert to str", K(ret), K(zone_list));
  } else {
    int64_t affected_rows = 0;
    ObDMLSqlSplicer dml;
    const char* locality = tenant_schema.get_locality_str().empty() ? "" : tenant_schema.get_locality_str().ptr();
    const char* previous_locality =
        tenant_schema.get_previous_locality_str().empty() ? "" : tenant_schema.get_previous_locality_str().ptr();
    const char* primary_zone =
        tenant_schema.get_primary_zone().empty() ? OB_RANDOM_PRIMARY_ZONE : tenant_schema.get_primary_zone().ptr();
    if (OB_SUCC(ret)) {
      const int64_t INVALID_REPLICA_NUM = -1;
      if (OB_SUCC(ret) &&
          (OB_FAIL(dml.add_pk_column(OBJ_GET_K(tenant_schema, tenant_id))) ||
              OB_FAIL(dml.add_column("tenant_name", ObHexEscapeSqlStr(tenant_schema.get_tenant_name_str()))) ||
              OB_FAIL(dml.add_column("replica_num", INVALID_REPLICA_NUM)) ||
              OB_FAIL(dml.add_column(OBJ_GET_K(tenant_schema, locked))) ||
              OB_FAIL(dml.add_column("zone_list", zone_list_buf)) ||
              OB_FAIL(dml.add_column("primary_zone", ObHexEscapeSqlStr(primary_zone))) ||
              OB_FAIL(dml.add_column("info", tenant_schema.get_comment())) ||
              OB_FAIL(dml.add_column("collation_type", CS_TYPE_INVALID)) ||
              OB_FAIL(dml.add_column("read_only", tenant_schema.is_read_only())) ||
              OB_FAIL(dml.add_column("rewrite_merge_version", tenant_schema.get_rewrite_merge_version())) ||
              OB_FAIL(dml.add_column("locality", ObHexEscapeSqlStr(locality))) ||
              OB_FAIL(dml.add_column("previous_locality", ObHexEscapeSqlStr(previous_locality))) ||
              OB_FAIL(dml.add_column(OBJ_GET_K(tenant_schema, logonly_replica_num))) ||
              OB_FAIL(dml.add_column(OBJ_GET_K(tenant_schema, storage_format_version))) ||
              OB_FAIL(dml.add_column(OBJ_GET_K(tenant_schema, storage_format_work_version))) ||
              OB_FAIL(dml.add_column("default_tablegroup_id", tenant_schema.get_default_tablegroup_id())) ||
              OB_FAIL(dml.add_column("compatibility_mode", tenant_schema.get_compatibility_mode())) ||
              OB_FAIL(dml.add_column("drop_tenant_time", tenant_schema.get_drop_tenant_time())) ||
              OB_FAIL(dml.add_column("status", ob_tenant_status_str(tenant_schema.get_status()))) ||
              OB_FAIL(dml.add_column("in_recyclebin", tenant_schema.is_in_recyclebin())) ||
              OB_FAIL(dml.add_gmt_modified()))) {
        LOG_WARN("add column failed", K(ret));
      }
    }
    // insert into __all_tenant
    if (OB_SUCC(ret)) {
      ObDMLExecHelper exec(sql_client, OB_SYS_TENANT_ID);
      if (OB_FAIL(exec.exec_replace(OB_ALL_TENANT_TNAME, dml, affected_rows))) {
        LOG_WARN("execute insert failed", K(ret));
      } else if (0 != affected_rows && 1 != affected_rows && 2 != affected_rows) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows unexpected", K(affected_rows), K(ret));
      } else {
        OB_LOG(INFO, "replace tenant success", K(affected_rows), K(tenant_schema));
      }
    }

    // insert into __all_tenant_history
    if (OB_SUCC(ret)) {
      ObDMLExecHelper exec(sql_client, OB_SYS_TENANT_ID);
      const int64_t is_deleted = 0;
      if (OB_FAIL(dml.add_pk_column("schema_version", tenant_schema.get_schema_version())) ||
          OB_FAIL(dml.add_column("is_deleted", is_deleted))) {
        LOG_WARN("add column failed", K(ret));
      } else if (OB_FAIL(exec.exec_replace(OB_ALL_TENANT_HISTORY_TNAME, dml, affected_rows))) {
        LOG_WARN("execute insert failed", K(ret));
      } else if (0 != affected_rows && 1 != affected_rows && 2 != affected_rows) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows unexpected", K(affected_rows), K(ret));
      }
    }

    // log operation
    if (OB_SUCC(ret)) {
      ObSchemaOperation tenant_op;
      tenant_op.tenant_id_ = tenant_schema.get_tenant_id();
      tenant_op.op_type_ = op;
      tenant_op.schema_version_ = tenant_schema.get_schema_version();
      tenant_op.ddl_stmt_str_ = ddl_stmt_str ? *ddl_stmt_str : ObString();
      int64_t sql_tenant_id = OB_SYS_TENANT_ID;
      if (OB_FAIL(log_operation(tenant_op, sql_client, sql_tenant_id))) {
        LOG_WARN("log add tenant ddl operation failed", K(ret));
      }
    }
    if (NULL != zone_list_buf) {
      ob_free(zone_list_buf);
      zone_list_buf = NULL;
    }
  }
  return ret;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
