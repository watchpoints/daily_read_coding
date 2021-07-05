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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dml/ob_view_table_resolver.h"
#include "share/schema/ob_table_schema.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "sql/parser/ob_parser.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_utils.h"
namespace oceanbase {
using namespace common;
using namespace share::schema;
namespace sql {
int ObViewTableResolver::do_resolve_set_query(
    const ParseNode& parse_tree, ObSelectStmt*& child_stmt, const bool is_left_child) /*default false*/
{
  int ret = OB_SUCCESS;
  child_stmt = NULL;
  ObViewTableResolver child_resolver(params_, view_db_name_, view_name_);

  child_resolver.set_current_level(current_level_);
  child_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  child_resolver.set_current_view_item(current_view_item);
  child_resolver.set_parent_view_resolver(parent_view_resolver_);
  child_resolver.set_calc_found_rows(is_left_child && has_calc_found_rows_);

  if (OB_FAIL(add_cte_table_to_children(child_resolver))) {
    LOG_WARN("failed to add cte table to children", K(ret));
  } else if (OB_FAIL(child_resolver.resolve_child_stmt(parse_tree))) {
    LOG_WARN("failed to resolve child stmt", K(ret));
  } else if (OB_ISNULL(child_stmt = child_resolver.get_child_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null child stmt", K(ret));
  }
  return ret;
}

int ObViewTableResolver::expand_view(TableItem& view_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_view_circular_reference(view_item))) {
    LOG_WARN("check view circular reference failed", K(ret));
  } else {
    // expand view as subquery which use view name as alias
    const ObTableSchema* view_schema = NULL;

    if (OB_FAIL(schema_checker_->get_table_schema(view_item.ref_id_, view_schema))) {
      LOG_WARN("get table schema failed", K(view_item));
    } else {
      ObViewTableResolver view_resolver(params_, view_db_name_, view_name_);
      view_resolver.set_current_level(current_level_);
      view_resolver.set_current_view_item(view_item);
      view_resolver.set_parent_view_resolver(this);
      if (OB_FAIL(do_expand_view(view_item, view_resolver))) {
        LOG_WARN("do expand view failed", K(ret));
      }
    }
  }
  return ret;
}

int ObViewTableResolver::check_view_circular_reference(const TableItem& view_item)
{
  int ret = OB_SUCCESS;
  ObViewTableResolver* cur_resolver = this;
  if (OB_UNLIKELY(!view_db_name_.empty() && !view_name_.empty() &&
                  0 == view_db_name_.compare(view_item.database_name_) &&
                  0 == view_name_.compare(view_item.table_name_))) {
    if (is_oracle_mode()) {
      ret = OB_ERR_VIEW_RECURSIVE;
    } else {
      ret = OB_ERR_VIEW_RECURSIVE;
      LOG_USER_ERROR(
          OB_ERR_VIEW_RECURSIVE, view_db_name_.length(), view_db_name_.ptr(), view_name_.length(), view_name_.ptr());
    }
  } else {
    do {
      if (OB_UNLIKELY(view_item.ref_id_ == cur_resolver->current_view_item.ref_id_)) {
        ret = OB_ERR_VIEW_RECURSIVE;
        const ObString& db_name = cur_resolver->current_view_item.database_name_;
        const ObString& tbl_name = cur_resolver->current_view_item.table_name_;
        LOG_USER_ERROR(OB_ERR_VIEW_RECURSIVE, db_name.length(), db_name.ptr(), tbl_name.length(), tbl_name.ptr());
      } else {
        cur_resolver = cur_resolver->parent_view_resolver_;
      }
    } while (OB_SUCC(ret) && cur_resolver != NULL);
  }
  return ret;
}

int ObViewTableResolver::resolve_generate_table(
    const ParseNode& table_node, const ObString& alias_name, TableItem*& table_item)
{
  int ret = OB_SUCCESS;
  ObViewTableResolver view_table_resolver(params_, view_db_name_, view_name_);
  view_table_resolver.set_current_level(current_level_);
  view_table_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  view_table_resolver.set_parent_view_resolver(parent_view_resolver_);
  view_table_resolver.set_current_view_item(current_view_item);
  if (OB_FAIL(view_table_resolver.set_cte_ctx(cte_ctx_, true, true))) {
    LOG_WARN("set cte ctx to child resolver failed", K(ret));
  } else if (OB_FAIL(add_cte_table_to_children(view_table_resolver))) {
    LOG_WARN("add CTE table to children failed", K(ret));
  } else if (OB_FAIL(do_resolve_generate_table(table_node, alias_name, view_table_resolver, table_item))) {
    LOG_WARN("do resolve generate table failed", K(ret));
  }
  return ret;
}

int ObViewTableResolver::check_need_use_sys_tenant(bool& use_sys_tenant) const
{
  int ret = OB_SUCCESS;

  const ObTableSchema* table_schema = NULL;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (OB_SYS_TENANT_ID == session_info_->get_effective_tenant_id()) {
    use_sys_tenant = false;
  } else {
    use_sys_tenant = true;
  }

  if (OB_SUCC(ret) && use_sys_tenant) {
    if (OB_FAIL(schema_checker_->get_table_schema(current_view_item.ref_id_, table_schema))) {
      LOG_WARN("fail to get table_schema", K(ret));
    } else if (NULL == table_schema) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("table_schema should not be NULL", K(ret));
    } else if (!table_schema->is_sys_view()) {
      use_sys_tenant = false;
    }
  }

  return ret;
}

int ObViewTableResolver::check_in_sysview(bool& in_sysview) const
{
  int ret = OB_SUCCESS;
  in_sysview = is_sys_view(current_view_item.ref_id_);
  return ret;
}

// construct select item from select_expr
int ObViewTableResolver::set_select_item(SelectItem& select_item, bool is_auto_gen)
{
  int ret = OB_SUCCESS;
  ObCollationType cs_type = CS_TYPE_INVALID;
  ObSelectStmt* select_stmt = get_select_stmt();

  if (OB_ISNULL(select_stmt) || OB_ISNULL(session_info_) || OB_ISNULL(select_item.expr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("select stmt is null", K_(session_info), K(select_stmt), K_(select_item.expr));
  } else if (is_create_view_ && !select_item.is_real_alias_) {
    if (OB_FAIL(ObSelectResolver::set_select_item(select_item, is_auto_gen))) {
      LOG_WARN("set select item failed", K(ret));
    }
  } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    LOG_WARN("fail to get collation_connection", K(ret));
  } else {
    // create new name
    if (!is_create_view_ && !select_item.is_real_alias_ && is_auto_gen &&
        select_item.alias_name_.length() > static_cast<size_t>(OB_MAX_VIEW_COLUMN_NAME_LENGTH_MYSQL)) {
      ObString tmp_col_name;
      ObString col_name;
      char temp_str_buf[number::ObNumber::MAX_PRINTABLE_SIZE];
      if (snprintf(temp_str_buf, sizeof(temp_str_buf), "Name_exp_%ld", auto_name_id_++) < 0) {
        ret = OB_SIZE_OVERFLOW;
        SQL_RESV_LOG(WARN, "failed to generate buffer for temp_str_buf", K(ret));
      }
      if (OB_SUCC(ret)) {
        tmp_col_name = ObString::make_string(temp_str_buf);
        if (OB_FAIL(ob_write_string(*allocator_, tmp_col_name, col_name))) {
          SQL_RESV_LOG(WARN, "Can not malloc space for constraint name", K(ret));
        } else {
          select_item.alias_name_.assign_ptr(col_name.ptr(), col_name.length());
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObSQLUtils::check_column_name(cs_type, select_item.alias_name_))) {
      LOG_WARN("fail to make field name", K(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(select_stmt->add_select_item(select_item))) {
      LOG_WARN("add select item to select stmt failed", K(ret));
    }
  }
  return ret;
}

int ObViewTableResolver::resolve_subquery_info(const ObIArray<ObSubQueryInfo>& subquery_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (current_level_ + 1 >= OB_MAX_SUBQUERY_LAYER_NUM && subquery_info.count() > 0) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "too many levels of subqueries");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < subquery_info.count(); i++) {
    const ObSubQueryInfo& info = subquery_info.at(i);
    ObViewTableResolver subquery_resolver(params_, view_db_name_, view_name_);
    subquery_resolver.set_current_level(current_level_ + 1);
    subquery_resolver.set_parent_namespace_resolver(this);
    subquery_resolver.set_parent_view_resolver(parent_view_resolver_);
    subquery_resolver.set_current_view_item(current_view_item);
    if (OB_FAIL(add_cte_table_to_children(subquery_resolver))) {
      LOG_WARN("add CTE table to children failed", K(ret));
    } else if (is_only_full_group_by_on(session_info_->get_sql_mode())) {
      subquery_resolver.set_parent_aggr_level(
          info.parents_expr_info_.has_member(IS_AGG) ? current_level_ : parent_aggr_level_);
    }
    if (OB_FAIL(do_resolve_subquery_info(info, subquery_resolver))) {
      LOG_WARN("do resolve subquery info failed", K(ret));
    }
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
