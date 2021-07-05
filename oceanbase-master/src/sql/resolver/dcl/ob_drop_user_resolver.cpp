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
#include "sql/resolver/dcl/ob_drop_user_resolver.h"

#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_struct.h"
using namespace oceanbase::sql;
using namespace oceanbase::common;
using oceanbase::share::schema::ObUserInfo;

ObDropUserResolver::ObDropUserResolver(ObResolverParams& params) : ObDCLResolver(params)
{}

ObDropUserResolver::~ObDropUserResolver()
{}

int ObDropUserResolver::resolve(const ParseNode& parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode* top_node = const_cast<ParseNode*>(&parse_tree);
  ParseNode* user_list_node = nullptr;
  ObDropUserStmt* drop_user_stmt = NULL;
  CHECK_COMPATIBILITY_MODE(params_.session_info_);
  if (OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Session info is not inited", K(ret));
  } else if (lib::is_oracle_mode()) {
    // oracle mode
    if (OB_ISNULL(top_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "top_node is null", K(ret));
    } else if (T_DROP_USER != top_node->type_ || 2 != top_node->num_child_) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid argument", K(ret), K(top_node->type_), K(top_node->num_child_));
    } else {
      // resolve cascade_node
      ParseNode* cascade_node = top_node->children_[1];
      if (OB_ISNULL(cascade_node)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "cascade_node is null", K(ret));
      } else if (1 != cascade_node->value_) {
        // todo: drop user only support cascade
        ret = OB_NOT_SUPPORTED;
        SQL_RESV_LOG(WARN, "this grammar is not supported now", K(ret));
      } else {
        if (OB_ISNULL(user_list_node = top_node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "node is null", K(ret));
        } else if (T_DROP_USER_LIST != user_list_node->type_ || user_list_node->num_child_ <= 0) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "invalid argument", K(ret), K(user_list_node->type_), K(user_list_node->num_child_));
        } else if (user_list_node->num_child_ > 1) {
          ret = OB_ERR_PARSER_SYNTAX;
          SQL_RESV_LOG(WARN, "this grammar is not supported in oracle mode", K(ret));
        } else {
          // do nothing
        }
      }
    }
  } else {  // !is_oracle_mode()
    // mysql_mode
    if (OB_ISNULL(top_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "top_node is null", K(ret));
    } else if (T_DROP_USER != top_node->type_ || top_node->num_child_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid argument", K(ret), K(top_node->type_), K(top_node->num_child_));
    } else {
      user_list_node = top_node;
    }
  }
  if (OB_SUCC(ret)) {
    // mysql mode and oracle mode in common
    if (OB_ISNULL(drop_user_stmt = create_stmt<ObDropUserStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "Failed to create ObDropUserStmt", K(ret));
    } else {
      uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
      // resovle user_list_node
      for (int i = 0; i < user_list_node->num_child_ && OB_SUCCESS == ret; ++i) {
        if (OB_ISNULL(user_list_node->children_[i])) {
          ret = OB_ERR_PARSE_SQL;
          LOG_WARN("The child of user_hostname node should not be NULL", K(ret), K(i));
        } else if (2 != user_list_node->children_[i]->num_child_) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("sql_parser parse user error", K(ret));
        } else if (OB_ISNULL(user_list_node->children_[i]->children_[0])) {
          // 0: user, 1: hostname
          ret = OB_ERR_PARSE_SQL;
          LOG_WARN("The child of user node should not be NULL", K(ret), K(i));
        } else {
          ParseNode* user_hostname_node = user_list_node->children_[i];
          ObString user_name(user_hostname_node->children_[0]->str_len_, user_hostname_node->children_[0]->str_value_);
          ObString host_name;
          if (NULL == user_hostname_node->children_[1]) {
            host_name.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
          } else {
            host_name.assign_ptr(user_hostname_node->children_[1]->str_value_,
                static_cast<int32_t>(user_hostname_node->children_[1]->str_len_));
          }
          const ObUserInfo* user_info = NULL;
          if (OB_FAIL(schema_checker_->get_user_info(tenant_id, user_name, host_name, user_info))) {
            LOG_WARN("failed to get user info", K(ret), K(user_name));
            if (OB_USER_NOT_EXIST == ret) {
              // skip, RS general process , compat MySQL
              ret = OB_SUCCESS;
            }
          } else if (extract_pure_id(user_info->get_user_id()) <= OB_USER_ID) {
            ret = OB_ERR_NO_PRIVILEGE;
            SQL_RESV_LOG(WARN, "Can not drop internal user", K(ret));
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(drop_user_stmt->add_user(user_name, host_name))) {
              LOG_WARN("Add user error", K(user_name), K(ret));
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        drop_user_stmt->set_tenant_id(tenant_id);
      }
    }
  }

  if (OB_SUCC(ret) && ObSchemaChecker::is_ora_priv_check()) {
    OZ(schema_checker_->check_ora_ddl_priv(session_info_->get_effective_tenant_id(),
           session_info_->get_priv_user_id(),
           ObString(""),
           stmt::T_DROP_USER,
           session_info_->get_enable_role_array()),
        session_info_->get_effective_tenant_id(),
        session_info_->get_user_id());
  }

  return ret;
}
