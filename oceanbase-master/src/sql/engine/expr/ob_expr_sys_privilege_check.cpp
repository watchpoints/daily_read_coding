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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_sys_privilege_check.h"

#include "share/object/ob_obj_cast.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
namespace oceanbase {
namespace sql {

ObExprSysPrivilegeCheck::ObExprSysPrivilegeCheck(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_SYS_PRIVILEGE_CHECK, N_SYS_PRIVILEGE_CHECK, 4, NOT_ROW_DIMENSION)
{}

ObExprSysPrivilegeCheck::~ObExprSysPrivilegeCheck()
{}

int ObExprSysPrivilegeCheck::calc_result_typeN(
    ObExprResType& type, ObExprResType* types, int64_t param_num, ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  CK(NULL != types);
  CK(4 == param_num);
  CK(NOT_ROW_DIMENSION == row_dimension_);
  if (OB_SUCC(ret)) {
    type.set_int();
    type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
    type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);

    types[0].set_type(ObVarcharType);
    types[0].set_calc_collation_type(ObCharset::get_system_collation());
    types[1].set_calc_type(ObIntType);
    types[2].set_type(ObVarcharType);
    types[2].set_calc_collation_type(ObCharset::get_system_collation());
    types[3].set_type(ObVarcharType);
    types[3].set_calc_collation_type(ObCharset::get_system_collation());
  }
  return ret;
}

int ObExprSysPrivilegeCheck::calc_resultN(
    common::ObObj& result, const common::ObObj* obj_array, int64_t param_num, common::ObExprCtx& expr_ctx) const
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(expr_ctx.exec_ctx_) || OB_ISNULL(expr_ctx.exec_ctx_->get_my_session()) || OB_UNLIKELY(4 != param_num)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("exec_ctx or my_session should not be NULL",
        K(ret),
        "exec_ctx",
        expr_ctx.exec_ctx_,
        "session",
        expr_ctx.my_session_);
  } else {
    int64_t tenant_id = OB_INVALID_ID;
    ObString level_str;
    ObString db_name;
    ObString table_name;
    EXPR_DEFINE_CAST_CTX(expr_ctx, CM_NONE);
    EXPR_GET_VARCHAR_V2(obj_array[0], level_str);
    EXPR_GET_INT64_V2(obj_array[1], tenant_id);
    EXPR_GET_VARCHAR_V2(obj_array[2], db_name);
    EXPR_GET_VARCHAR_V2(obj_array[3], table_name);
    bool allow_show = true;
    if (OB_FAIL(check_show_priv(allow_show, *expr_ctx.exec_ctx_, level_str, tenant_id, db_name, table_name))) {
      LOG_WARN("check show privilege failed", K(ret));
    } else {
      if (allow_show) {
        result.set_int(0);
      } else {
        result.set_int(-1);
      }
    }
  }
  return ret;
}

int ObExprSysPrivilegeCheck::check_show_priv(bool& allow_show, ObExecContext& exec_ctx,
    const common::ObString& level_str, const uint64_t tenant_id, const common::ObString& db_name,
    const common::ObString& table_name)
{
  int ret = OB_SUCCESS;
  share::schema::ObSessionPrivInfo session_priv;
  const share::schema::ObSchemaGetterGuard* schema_guard = exec_ctx.get_virtual_table_ctx().schema_guard_;
  if (OB_UNLIKELY(NULL == schema_guard)) {
    ret = OB_SCHEMA_ERROR;
  }
  exec_ctx.get_my_session()->get_session_priv_info(session_priv);
  allow_show = true;
  if (OB_SUCC(ret)) {
    // tenant_id in table is static casted to int64_t,
    // and use statis_cast<uint64_t> for retrieving(same with schema_service)
    if (session_priv.tenant_id_ != static_cast<uint64_t>(tenant_id) && OB_INVALID_TENANT_ID != tenant_id) {
      // not current tenant's row
    } else if (0 == level_str.case_compare("db_acc")) {
      if (OB_FAIL(const_cast<share::schema::ObSchemaGetterGuard*>(schema_guard)
                      ->check_db_show(session_priv, db_name, allow_show))) {
        LOG_WARN("Check db show failed", K(ret));
      }
    } else if (0 == level_str.case_compare("table_acc")) {
      // if (OB_FAIL(priv_mgr.check_table_show(session_priv,
      if (OB_FAIL(const_cast<share::schema::ObSchemaGetterGuard*>(schema_guard)
                      ->check_table_show(session_priv, db_name, table_name, allow_show))) {
        LOG_WARN("Check table show failed", K(ret));
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Check priv level error", K(ret));
    }
  }
  return ret;
}

int ObExprSysPrivilegeCheck::cg_expr(ObExprCGCtx&, const ObRawExpr&, ObExpr& expr) const
{
  int ret = OB_SUCCESS;
  CK(4 == expr.arg_cnt_);
  expr.eval_func_ = eval_sys_privilege_check;
  return ret;
}

int ObExprSysPrivilegeCheck::eval_sys_privilege_check(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum* level = NULL;
  ObDatum* tenant = NULL;
  ObDatum* db = NULL;
  ObDatum* table = NULL;
  bool allow_show = true;
  if (OB_FAIL(expr.eval_param_value(ctx, level, tenant, db, table))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (tenant->is_null()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant is null", K(ret));
  } else if (OB_FAIL(check_show_priv(allow_show,
                 ctx.exec_ctx_,
                 level->is_null() ? ObString() : level->get_string(),
                 tenant->get_int(),
                 db->is_null() ? ObString() : db->get_string(),
                 table->is_null() ? ObString() : table->get_string()))) {
    LOG_WARN("check show privilege failed", K(ret));
  } else {
    expr_datum.set_int(allow_show ? 0 : -1);
  }

  return ret;
}

}  // namespace sql
}  // namespace oceanbase
