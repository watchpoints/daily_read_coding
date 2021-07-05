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
#include "sql/resolver/dcl/ob_grant_resolver.h"

#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/resolver/dcl/ob_set_password_resolver.h"
#include "share/schema/ob_obj_priv_type.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

ObGrantResolver::ObGrantResolver(ObResolverParams& params) : ObDCLResolver(params)
{}

ObGrantResolver::~ObGrantResolver()
{}

int ObGrantResolver::resolve_grantee_clause(
    const ParseNode* grantee_clause, ObIArray<ObString>& user_name_array, ObIArray<ObString>& host_name_array)
{
  int ret = OB_SUCCESS;
  ParseNode* grant_user = NULL;
  if (OB_ISNULL(grantee_clause) || OB_ISNULL(grantee_clause->children_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("resolve grantee error", K(ret));
  } else {
    // Put every grant_user into grant_user_arry
    LOG_INFO("grantee_clause type", K(grantee_clause->type_));
    if (grantee_clause->type_ != T_USERS && grantee_clause->type_ != T_GRANT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("invalid type", K(ret), K(grantee_clause->type_));
    } else if (grantee_clause->type_ == T_USERS) {
      LOG_INFO("grantee_clause:", K(grantee_clause->str_value_), K(grantee_clause->type_));
      const ParseNode* grant_user_list = grantee_clause->children_[0];
      LOG_INFO("grant_user_list", K(grant_user_list->str_value_), K(grant_user_list->type_));
      LOG_INFO("grant_user_list children", K(grant_user_list->num_child_));
      for (int i = 0; OB_SUCC(ret) && i < grant_user_list->num_child_; ++i) {
        grant_user = grant_user_list->children_[i];
        LOG_INFO("grant_user", K(i), K(grant_user->str_value_), K(grant_user->type_));
        if (OB_ISNULL(grant_user)) {
          ret = OB_ERR_PARSE_SQL;
          LOG_WARN("grant_user is NULL", K(ret));
        } else {
          ObString user_name;
          ObString host_name(OB_DEFAULT_HOST_NAME);
          if (OB_FAIL(resolve_grant_user(grant_user, user_name, host_name))) {
            LOG_WARN("failed to resolve grant_user", K(ret), K(grant_user));
          } else {
            OZ(user_name_array.push_back(user_name));
            OZ(host_name_array.push_back(host_name));
          }
        }
      }
    } else if (grantee_clause->type_ == T_GRANT) {
      grant_user = grantee_clause->children_[0];
      if (OB_ISNULL(grant_user)) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("grant_user is NULL", K(ret));
      } else {
        ObString user_name;
        ObString host_name(OB_DEFAULT_HOST_NAME);
        if (OB_FAIL(resolve_grant_user(grant_user, user_name, host_name))) {
          LOG_WARN("failed to resolve grant_user", K(ret), K(grant_user));
        } else {
          OZ(user_name_array.push_back(user_name));
          OZ(host_name_array.push_back(host_name));
        }
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_grant_user(const ParseNode* grant_user, ObString& user_name, ObString& host_name)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(grant_user)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("resolve grant_user error", K(ret));
  } else {
    if (grant_user->type_ == T_CREATE_USER_SPEC) {
      if (grant_user->num_child_ != 4) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Parse node error in grentee ", K(ret));
      } else {
        user_name.assign_ptr(const_cast<char*>(grant_user->children_[0]->str_value_),
            static_cast<int32_t>(grant_user->children_[0]->str_len_));
        if (NULL != grant_user->children_[3]) {
          // host name is not default
          host_name.assign_ptr(const_cast<char*>(grant_user->children_[3]->str_value_),
              static_cast<int32_t>(grant_user->children_[3]->str_len_));
        }
      }
    } else {
      user_name.assign_ptr(const_cast<char*>(grant_user->str_value_), static_cast<int32_t>(grant_user->str_len_));
    }
  }

  return ret;
}

/* grant_system_privileges:
role_list TO grantee_clause opt_with_admin_option
[0]: role_list
[1]: grantee_cluase
[2]: opt_with_admin_option

enum GrantParseOffset
{
  PARSE_GRANT_ROLE_LIST,
  PARSE_GRANT_ROLE_GRANTEE,
  PARSE_GRANT_ROLE_OPT_WITH,
  PARSE_GRANT_ROLE_MAX_IDX
};
resolve grantee and role_list from grant_role, put grantee and role_list into role of grant_stmt.
role[0]:user_name of grantee
role[1]: host_name of grantee
role[2..n]: role_list to grant
*/
// This function is only used in Oracle mode.
int ObGrantResolver::resolve_grant_role_to_ur(const ParseNode* grant_role, ObGrantStmt* grant_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(grant_role) || OB_ISNULL(grant_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("grant_role and grant_stmt should not be NULL", K(grant_role), K(grant_stmt), K(ret));
  } else {
    // put user_name + host_name into roles_[0], roles_[1]
    ParseNode* grantee_clause = grant_role->children_[PARSE_GRANT_ROLE_GRANTEE];
    ObSArray<ObString> user_name_array;
    ObSArray<ObString> host_name_array;
    ObSEArray<uint64_t, 4> role_id_array;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(resolve_grantee_clause(grantee_clause, user_name_array, host_name_array))) {
      LOG_WARN("resolve grentee fail", K(ret));
    } else {
      if (user_name_array.count() != host_name_array.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user_name count is not equal to host_name count",
            K(ret),
            K(user_name_array.count()),
            K(host_name_array.count()));
      } else {
        for (int i = 0; OB_SUCC(ret) && i < user_name_array.count(); ++i) {
          ObString& user_name = user_name_array.at(i);
          ObString& host_name = host_name_array.at(i);
          if (OB_FAIL(grant_stmt->add_grantee(user_name))) {
            LOG_WARN("failed to add grantee", K(ret), K(user_name));
          } else if (OB_FAIL(grant_stmt->add_user(user_name, host_name))) {
            LOG_WARN("failed to add user and host name", K(ret), K(user_name), K(host_name));
          }
        }
      }
    }

    ParseNode* role_list = grant_role->children_[PARSE_GRANT_ROLE_LIST];
    ParseNode* role = NULL;
    if (OB_ISNULL(role_list)) {
      ret = OB_ERR_PARSE_SQL;
      LOG_WARN("role_list should not be NULL", K(grant_role), K(ret));
    } else {
      uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
      const ObUserInfo* role_info = NULL;
      ObSchemaChecker* schema_ck = params_.schema_checker_;
      uint64_t option = NO_OPTION;

      CK(schema_ck != NULL);
      for (int i = 0; OB_SUCC(ret) && i < role_list->num_child_; ++i) {
        role = role_list->children_[i];
        if (NULL == role) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("role node is null", K(ret));
        } else {
          ObString role_name;
          role_name.assign_ptr(const_cast<char*>(role->str_value_), static_cast<int32_t>(role->str_len_));
          if (OB_FAIL(grant_stmt->add_role(role_name))) {
            LOG_WARN("failed to add role", K(ret));
          } else {
            // check roles exists
            OZ(schema_ck->get_user_info(tenant_id,
                   role_name,
                   // role has fixed host_name '%'
                   ObString::make_string(OB_DEFAULT_HOST_NAME),
                   role_info),
                tenant_id,
                role_name);
            if (OB_USER_NOT_EXIST == ret) {
              ret = OB_ROLE_NOT_EXIST;
              LOG_USER_ERROR(OB_ROLE_NOT_EXIST, role_name.length(), role_name.ptr());
            }
            if (OB_SUCC(ret) && role_info != NULL) {
              OZ(role_id_array.push_back(role_info->get_user_id()));
            }
          }
        }
        OZ(resolve_admin_option(grant_role->children_[PARSE_GRANT_ROLE_OPT_WITH], option));
        OX(grant_stmt->set_option(option));
      }
      /* check grant role stmt's priv */
      OZ(schema_ck->check_ora_grant_role_priv(tenant_id,
          params_.session_info_->get_user_id(),
          role_id_array,
          params_.session_info_->get_enable_role_array()));
    }
  }

  return ret;
}

int ObGrantResolver::obj_priv_exists(share::ObRawObjPrivArray& obj_privs, share::ObRawObjPriv priv, bool& exists)
{
  int ret = OB_SUCCESS;

  exists = FALSE;
  ARRAY_FOREACH_X(obj_privs, idx, cnt, !exists)
  {
    if (obj_privs.at(idx) == priv) {
      exists = TRUE;
    }
  }
  return ret;
}

/* judge priv exist in sys privs.
   add priv is all privileges */
int ObGrantResolver::priv_exists(share::ObRawPrivArray& sys_privs, int64_t priv, bool& exists)
{
  int ret = OB_SUCCESS;

  exists = FALSE;
  if (priv != PRIV_ID_MAX) {
    ARRAY_FOREACH_X(sys_privs, idx, cnt, !exists)
    {
      if (sys_privs.at(idx) == priv) {
        exists = TRUE;
      }
    }
  } else {
    /* Oracle Database provides the ALL PRIVILEGES shortcut for
      granting all the system privileges listed in Table 18-1,
      except the SELECT ANY DICTIONARY privilege.
      So, here just judge if only one select any dictiony in privs */
    if (sys_privs.count() != 1) {
      exists = FALSE;
    } else if (sys_privs.at(0) != PRIV_ID_SELECT_ANY_DICTIONARY) {
      exists = FALSE;
    } else {
      exists = TRUE;
    }
  }
  return ret;
}

/* put priv into array.
   add all privileges processing.
   all privileges is aias for all privs except select any dictionary */
int ObGrantResolver::push_pack_sys_priv(share::ObRawPrivArray& sys_privs, int64_t priv)
{
  int ret = OB_SUCCESS;
  CK(priv > PRIV_ID_NONE && priv <= PRIV_ID_MAX);
  if (priv != PRIV_ID_MAX) {
    OZ(sys_privs.push_back(priv));
  } else {
    for (int i = PRIV_ID_NONE + 1; OB_SUCC(ret) && i < PRIV_ID_MAX; i++) {
      if (i != PRIV_ID_SELECT_ANY_DICTIONARY) {
        OZ(sys_privs.push_back(i));
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_sys_privs(const ParseNode* privs_node, share::ObRawPrivArray& sys_privs)
{
  int ret = OB_SUCCESS;
  int64_t priv;
  bool exists = FALSE;
  CK(OB_NOT_NULL(privs_node));
  if (OB_SUCC(ret)) {
    for (int i = 0; i < privs_node->num_child_ && OB_SUCC(ret); ++i) {
      ParseNode* obj_with_col_priv_node = NULL;
      ParseNode* priv_node = NULL;
      if (OB_ISNULL(obj_with_col_priv_node = privs_node->children_[i])) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("obj_with_col_priv_node is NULL", K(ret));
      } else if (T_ORA_PRIV_TYPE != obj_with_col_priv_node->type_) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("invalid obj_with_col_priv_node type", K(ret), K(obj_with_col_priv_node->type_));
      } else if (OB_ISNULL(priv_node = obj_with_col_priv_node->children_[0])) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("priv_node should not be NULL", K(ret));
      } else {
        int64_t priv;
        if (priv_node->type_ == T_PRIV_TYPE && priv_node->value_ == OB_PRIV_ALL) {
          priv = PRIV_ID_MAX;
        } else if (T_ORACLE_SYS_PRIV_TYPE == priv_node->type_) {
          priv = priv_node->value_;
        } else {
          ret = OB_ERR_PARSE_SQL;
          LOG_WARN("invalid priv_node", K(ret), K(priv_node->type_), K(priv_node->value_));
        }
        OZ(priv_exists(sys_privs, priv, exists));
        if (OB_SUCC(ret) && exists) {
          ret = OB_PRIV_DUP;
          LOG_WARN("duplicate privilege list", K(priv), K(ret));
        }
        OZ(push_pack_sys_priv(sys_privs, priv));
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_admin_option(const ParseNode* admin_option, uint64_t& option)
{
  int ret = OB_SUCCESS;
  if (admin_option == NULL) {
    option = NO_OPTION;
  } else {
    if (admin_option->type_ != T_WITH_ADMIN_OPTION) {
      ret = OB_ERR_PARSE_SQL;
    } else {
      option = ADMIN_OPTION;
    }
  }
  return ret;
}

/*grant_system_privileges:
system_privilege_list TO grantee_clause opt_with_admin_option

enum GrantParseSysOffset
{
  PARSE_GRANT_SYS_PRIV_ORACLE_LIST,
  PARSE_GRANT_SYS_PRIV_ORACLE_GRANTEE,
  PARSE_GRANT_SYS_PRIV_ORACLE_OPT_WITH,
  PARSE_GRANT_SYS_PRIV_ORACLE_MAX_IDX
};

resolve grantee and role_list from grant_role, put grantee and role_list into role of grant_stmt.
role[0]:user_name of grantee
role[1]: host_name of grantee
role[2..n]: role_list to grant
*/
// This function is only used in Oracle mode
int ObGrantResolver::resolve_grant_sys_priv_to_ur(const ParseNode* grant_sys_privs, ObGrantStmt* grant_stmt)
{
  int ret = OB_SUCCESS;
  ParseNode* grantee_clause = NULL;
  CK(OB_NOT_NULL(params_.schema_checker_) || OB_NOT_NULL(params_.session_info_));
  if (OB_ISNULL(grant_sys_privs) || OB_ISNULL(grant_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(grant_sys_privs), K(grant_stmt), K(ret));
  } else {
    grantee_clause = grant_sys_privs->children_[PARSE_GRANT_ROLE_GRANTEE];
    ObSArray<ObString> user_name_array;
    ObSArray<ObString> host_name_array;
    if (OB_FAIL(resolve_grantee_clause(grantee_clause, user_name_array, host_name_array))) {
      LOG_WARN("resolve grantee_clause failed", K(ret));
    } else {
      if (user_name_array.count() != host_name_array.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user_name count is not equal to host_name count",
            K(ret),
            K(user_name_array.count()),
            K(host_name_array.count()));
      } else {
        for (int i = 0; OB_SUCC(ret) && i < user_name_array.count(); ++i) {
          ObString& user_name = user_name_array.at(i);
          ObString& host_name = host_name_array.at(i);
          if (OB_FAIL(grant_stmt->add_grantee(user_name))) {
            LOG_WARN("failed to add grantee", K(ret), K(user_name));
          } else if (OB_FAIL(grant_stmt->add_user(user_name, host_name))) {
            LOG_WARN("failed to add user and host name", K(ret), K(user_name), K(host_name));
          }
        }
      }
    }

    // resolve privileges
    if (OB_SUCC(ret)) {
      share::ObRawPrivArray sys_priv_array;
      uint64_t option = NO_OPTION;
      OZ(resolve_sys_privs(grant_sys_privs->children_[PARSE_GRANT_SYS_PRIV_ORACLE_LIST], sys_priv_array));
      OZ(grant_stmt->set_priv_array(sys_priv_array));
      OX(grant_stmt->set_grant_level(OB_PRIV_SYS_ORACLE_LEVEL));
      OZ(resolve_admin_option(grant_sys_privs->children_[PARSE_GRANT_SYS_PRIV_ORACLE_OPT_WITH], option));
      OX(grant_stmt->set_option(option));
    }
    if (OB_SUCC(ret) && ObSchemaChecker::is_ora_priv_check()) {
      OZ(params_.schema_checker_->check_ora_grant_sys_priv(params_.session_info_->get_effective_tenant_id(),
          params_.session_info_->get_user_id(),
          grant_stmt->get_priv_array(),
          params_.session_info_->get_enable_role_array()));
    }
  }
  return ret;
}

int ObGrantResolver::resolve_grant_system_privileges(const ParseNode* grant_system_privileges, ObGrantStmt* grant_stmt)
{
  int ret = OB_SUCCESS;
  // resolve grant role to user
  if (NULL == grant_system_privileges || NULL == grant_system_privileges->children_[0] ||
      NULL == grant_system_privileges->children_[1]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(ret));
  } else {
    if (grant_system_privileges->type_ == T_GRANT_ROLE) {
      grant_stmt->set_stmt_type(stmt::T_GRANT_ROLE);
      if (OB_FAIL(resolve_grant_role_to_ur(grant_system_privileges, grant_stmt))) {
        LOG_WARN("resolve_grant_role fail", K(ret));
      }
    } else if (grant_system_privileges->type_ == T_GRANT_SYS_PRIV_ORACLE) {
      if (OB_FAIL(resolve_grant_sys_priv_to_ur(grant_system_privileges, grant_stmt))) {
        LOG_WARN("resolve_grant_sys_priv fail", K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected grant system privilege type", K(grant_system_privileges->type_));
    }
  }
  return ret;
}

int ObGrantResolver::resolve(const ParseNode& parse_tree)
{
  int ret = OB_SUCCESS;
  CHECK_COMPATIBILITY_MODE(session_info_);
  if (lib::is_oracle_mode()) {
    ret = resolve_ora(parse_tree);
  } else {
    ret = resolve_mysql(parse_tree);
  }
  return ret;
}

/* 1. check raw_obj_priv if is insert, update,  references.
   2. check dup col_name, resolve col_name --> col_id */
int ObGrantResolver::resolve_col_names(
    ObGrantStmt* grant_stmt, share::ObRawObjPriv raw_obj_priv, ParseNode* opt_column_list)
{
  int ret = OB_SUCCESS;

  CK(grant_stmt != NULL);
  if (opt_column_list != NULL) {

    ObSEArray<uint64_t, 4> col_ids;
    ObObjectType object_type = grant_stmt->get_object_type();
    uint64_t obj_id = grant_stmt->get_object_id();

    if (raw_obj_priv != OBJ_PRIV_ID_INSERT && raw_obj_priv != OBJ_PRIV_ID_UPDATE &&
        raw_obj_priv != OBJ_PRIV_ID_REFERENCES) {
      ret = OB_ERR_MISSING_ON_KEYWORD;
    }
    if (OB_SUCC(ret) && opt_column_list != NULL) {
      if (share::schema::ObObjectType::TABLE != object_type) {
        ret = OB_WRONG_TABLE_NAME;
        LOG_USER_ERROR(OB_WRONG_TABLE_NAME, grant_stmt->get_table_name().length(), grant_stmt->get_table_name().ptr());
      } else {
        ObString column_name;
        for (int32_t i = 0; OB_SUCCESS == ret && i < opt_column_list->num_child_; ++i) {
          const ParseNode* child_node = NULL;
          if (OB_ISNULL(child_node = opt_column_list->children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("child node is null");
          } else {
            const share::schema::ObColumnSchemaV2* column_schema;
            column_name.assign_ptr(
                const_cast<char*>(child_node->str_value_), static_cast<int32_t>(child_node->str_len_));
            if (OB_FAIL(params_.schema_checker_->get_column_schema(obj_id, column_name, column_schema))) {
              /* change errinfo */
              if (ret == OB_ERR_BAD_FIELD_ERROR) {
                ret = OB_WRONG_COLUMN_NAME;
                LOG_USER_ERROR(OB_WRONG_COLUMN_NAME, column_name.length(), column_name.ptr());
              }
            } else {
              // filter out duplicated column
              bool found = false;
              for (int64_t j = 0; j < col_ids.count() && OB_SUCC(ret); j++) {
                if (col_ids.at(j) == column_schema->get_column_id()) {
                  ret = OB_ERR_FIELD_SPECIFIED_TWICE;
                  LOG_USER_ERROR(OB_ERR_FIELD_SPECIFIED_TWICE, to_cstring(column_name));
                }
              }
              OZ(col_ids.push_back(column_schema->get_column_id()));
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (raw_obj_priv == OBJ_PRIV_ID_INSERT) {
          OZ(grant_stmt->set_ins_col_ids(col_ids));
        } else if (raw_obj_priv == OBJ_PRIV_ID_UPDATE) {
          OZ(grant_stmt->set_upd_col_ids(col_ids));
        } else if (raw_obj_priv == OBJ_PRIV_ID_REFERENCES) {
          OZ(grant_stmt->set_ref_col_ids(col_ids));
        }
      }
    }
  }
  return ret;
}

/* check grant stmt valid
1. username dup
2. grant role can not with grant option
3. grant role can not reference, index priv */
int ObGrantResolver::check_user_dup_and_role_grant_option(ObSchemaGetterGuard* guard,
    ObIArray<ObString>& user_name_array, const ObGrantStmt* grant_stmt, const ObString& user_name,
    const ObString& host_name)
{
  int ret = OB_SUCCESS;
  CK(grant_stmt != NULL);

  if (ObSchemaChecker::is_ora_priv_check()) {
    /* 1. check user dup */
    if (has_exist_in_array(user_name_array, user_name)) {
      ret = OB_ERR_DUPLICATE_USERNAME_IN_LIST;
    } else {
      OZ(user_name_array.push_back(user_name));
    }
    bool has_ref_priv = false;
    bool has_index_priv = false;
    if (has_exist_in_array(
            grant_stmt->get_obj_priv_array(), static_cast<share::ObRawObjPriv>(OBJ_PRIV_ID_REFERENCES))) {
      has_ref_priv = true;
    } else if (has_exist_in_array(
                   grant_stmt->get_obj_priv_array(), static_cast<share::ObRawObjPriv>(OBJ_PRIV_ID_INDEX))) {
      has_index_priv = true;
    }

    /* grant obj priv to role has restrict:
       1. can't be with grant option, raise error highest priority
       2. can't be grant reference, secondary priority
       3. can't be grant index */
    if (grant_stmt->get_option() == GRANT_OPTION || has_ref_priv || has_index_priv) {
      const ObUserInfo* user_info = NULL;
      CK(params_.session_info_ != NULL);
      uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
      CK(guard != NULL);
      OZ(guard->get_user_info(tenant_id, user_name, host_name, user_info));
      if (OB_SUCC(ret)) {
        if (NULL == user_info) {
          ret = OB_USER_NOT_EXIST;
        } else if (user_info->is_role()) {
          if (grant_stmt->get_option() == GRANT_OPTION) {
            ret = OB_ERR_CANNOT_GRANT_TO_A_ROLE_WITH_GRANT_OPTION;
          } else if (has_ref_priv) {
            ObString str = "REFERENCES";
            ret = OB_ERR_CANNOT_GRANT_STRING_TO_A_ROLE;
            LOG_USER_ERROR(OB_ERR_CANNOT_GRANT_STRING_TO_A_ROLE, str.length(), str.ptr());
          } else {
            CK(has_index_priv);
            ObString str = "INDEX";
            ret = OB_ERR_CANNOT_GRANT_STRING_TO_A_ROLE;
            LOG_USER_ERROR(OB_ERR_CANNOT_GRANT_STRING_TO_A_ROLE, str.length(), str.ptr());
          }
        }
      }
    }
  }

  return ret;
}

// Currently, this function is only used to resolve obj privileges in oracle mode.
int ObGrantResolver::resolve_grant_obj_privileges(const ParseNode* node, ObGrantStmt* grant_stmt)
{
  int ret = OB_SUCCESS;
  CK(params_.session_info_ != NULL);
  uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
  ParseNode* privs_node = node->children_[0];
  ParseNode* priv_level_node = node->children_[1];
  ParseNode* users_node = node->children_[2];
  bool is_directory = false;
  bool is_owner = false;
  bool explicit_db = false;
  bool priv_has_execute = false;
  bool duplicated_privs = false;

  // ParseNode *opt_column_list = node->children_[3];
  CK(node != NULL && grant_stmt != NULL);

  if (privs_node != NULL && priv_level_node != NULL && users_node != NULL) {
    ObPrivLevel grant_level = OB_PRIV_INVALID_LEVEL;
    // resolve on obj clause
    if (OB_SUCC(ret)) {
      ObString db = ObString::make_string("");
      ObString table = ObString::make_string("");

      // Check if the privileges contain execute or privileges are duplicated
      if (OB_FAIL(check_duplicated_privs_with_info_ora(privs_node, duplicated_privs, priv_has_execute))) {
        LOG_WARN("Failed to check whether privilege is execute", K(ret));
      } else {
        LOG_DEBUG("check duplicated privs", K(duplicated_privs), K(ret));
        LOG_DEBUG("check priv_is_execute", K(priv_has_execute), K(ret));
      }

      if (OB_INVALID_ARGUMENT == ret || OB_ERR_PARSE_SQL == ret) {
        // Reset ret to OB_SUCCESS since obj shall be resolved anyway,
        // even if check_duplicated_privs_with_info_ora failed.
        ret = OB_SUCCESS;
      }

      if (OB_SUCC(ret)) {
        // If privileges are duplicated, skip the obj check.
        if (duplicated_privs) {
          ret = OB_PRIV_DUP;
          LOG_WARN("Duplicated privileges", K(duplicated_privs), K(ret));
        } else {
          // Check obj
          if (OB_FAIL(resolve_obj_ora(priv_level_node,
                  params_.session_info_->get_database_name(),
                  db,
                  table,
                  grant_level,
                  is_directory,
                  explicit_db))) {
            LOG_WARN("Resolve priv_level node error", K(ret));
          } else if (OB_FAIL(check_and_convert_name(db, table))) {
            LOG_WARN("Check and convert name error", K(db), K(table), K(ret));
          } else {
            grant_stmt->set_grant_level(grant_level);
            if (OB_FAIL(grant_stmt->set_database_name(db))) {
              LOG_WARN("Failed to set database_name to grant_stmt", K(ret));
            } else if (OB_FAIL(grant_stmt->set_table_name(table))) {
              LOG_WARN("Failed to set table_name to grant_stmt", K(ret));
            } else if (grant_level == OB_PRIV_TABLE_LEVEL) {
              share::schema::ObObjectType object_type = share::schema::ObObjectType::INVALID;
              uint64_t object_id = OB_INVALID_ID;
              void* view_query = NULL;
              if (db.empty() || table.empty()) {
                object_type = share::schema::ObObjectType::MAX_TYPE;
              } else {
                /* when synonym, obj_db_name return the final basic object's dbname */
                ObString obj_db_name;
                OZ(params_.schema_checker_->get_object_type_with_view_info(allocator_,
                    &params_,
                    tenant_id,
                    db,
                    table,
                    object_type,
                    object_id,
                    view_query,
                    is_directory,
                    obj_db_name,
                    explicit_db,
                    ObString("")));
                OX(grant_stmt->set_ref_query(static_cast<ObSelectStmt*>(view_query)));
                OX(grant_stmt->set_object_type(object_type));
                OX(grant_stmt->set_object_id(object_id));
                OX(is_owner = share::ObOraPrivCheck::user_is_owner(params_.session_info_->get_user_name(), db));
                OZ(grant_stmt->set_database_name(obj_db_name));
              }
            }
          }
        }
      }
    }

    if (OB_TABLE_NOT_EXIST == ret && true == priv_has_execute) {
      ret = OB_ERR_PROGRAM_UNIT_NOT_EXIST;
      LOG_INFO("Cannot find object when priv contains execute, change ret.");
    }

    // resolve privileges
    OZ(resolve_obj_priv_list_ora(grant_stmt, privs_node, grant_level, is_owner));

    // check grant object privs for oracle
    if (OB_SUCC(ret) && lib::is_oracle_mode() && grant_stmt->get_object_id() != OB_INVALID_ID) {
      if (ObSchemaChecker::is_ora_priv_check()) {
        uint64_t grantor_id_out = OB_INVALID_ID;
        OZ(params_.schema_checker_->check_ora_grant_obj_priv(tenant_id,
            params_.session_info_->get_user_id(),
            grant_stmt->get_database_name(),
            grant_stmt->get_object_id(),
            static_cast<uint64_t>(grant_stmt->get_object_type()),
            grant_stmt->get_obj_priv_array(),
            grant_stmt->get_ins_col_ids(),
            grant_stmt->get_upd_col_ids(),
            grant_stmt->get_ref_col_ids(),
            grantor_id_out,
            params_.session_info_->get_enable_role_array()));
        OX(grant_stmt->set_grantor_id(grantor_id_out));
      } else {
        OX(grant_stmt->set_grantor_id(params_.session_info_->get_user_id()));
      }
    }

    // resolve users
    if (OB_SUCC(ret)) {
      // oracle tenant, when grant, if user not exists, can't create user
      bool need_create_user = false;
      CHECK_COMPATIBILITY_MODE(session_info_);
      if (!lib::is_oracle_mode()) {
        need_create_user = (0 == (params_.session_info_->get_sql_mode() & SMO_NO_AUTO_CREATE_USER));
      }
      grant_stmt->set_need_create_user(need_create_user);
      if (users_node->num_child_ > 0) {
        ObSEArray<ObString, 4> user_name_array;
        for (int i = 0; OB_SUCC(ret) && i < users_node->num_child_; ++i) {
          ParseNode* user_node = users_node->children_[i];
          ObString user_name;
          ObString host_name;
          ObString pwd;
          ObString need_enc = ObString::make_string("NO");
          if (OB_ISNULL(user_node)) {
            ret = OB_ERR_PARSE_SQL;
            LOG_WARN("Parse SQL error, user node should not be NULL", K(user_node), K(ret));
          } else if (0 == user_node->num_child_) {
            user_name.assign_ptr(const_cast<char*>(user_node->str_value_), static_cast<int32_t>(user_node->str_len_));
            host_name.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
            pwd = ObString("");
          } else if (4 != user_node->num_child_) {
            ret = OB_ERR_PARSE_SQL;
            LOG_WARN("User specification's child node num error", K(ret));
          } else if (OB_ISNULL(user_node->children_[0])) {
            ret = OB_ERR_PARSE_SQL;
            LOG_WARN("The child 0 should not be NULL", K(ret));
          } else {
            user_name.assign_ptr(const_cast<char*>(user_node->children_[0]->str_value_),
                static_cast<int32_t>(user_node->children_[0]->str_len_));
            if (NULL == user_node->children_[3]) {
              host_name.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
            } else {
              host_name.assign_ptr(
                  user_node->children_[3]->str_value_, static_cast<int32_t>(user_node->children_[3]->str_len_));
            }
            if (user_node->children_[1] != NULL) {
              if (0 != user_name.compare(session_info_->get_user_name())) {
                grant_stmt->set_need_create_user_priv(true);
              }
              pwd.assign_ptr(const_cast<char*>(user_node->children_[1]->str_value_),
                  static_cast<int32_t>(user_node->children_[1]->str_len_));
              if (OB_ISNULL(user_node->children_[2])) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("The child 2 of user_node should not be NULL", K(ret));
              } else if (0 == user_node->children_[2]->value_) {
                if (!ObSetPasswordResolver::is_valid_mysql41_passwd(pwd)) {
                  ret = OB_ERR_PASSWORD_FORMAT;
                  LOG_WARN("Wrong password hash format");
                }
              } else {
                need_enc = ObString::make_string("YES");
              }
            } else {
              pwd = ObString("");
            }
          }
          if (OB_SUCC(ret)) {
            if (user_name.length() > OB_MAX_USER_NAME_LENGTH) {
              ret = OB_WRONG_USER_NAME_LENGTH;
              LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, user_name.length(), user_name.ptr());
            } else if (OB_FAIL(grant_stmt->add_grantee(user_name))) {
              LOG_WARN("Add grantee error", K(user_name), K(ret));
            } else if (OB_FAIL(grant_stmt->add_user(user_name, host_name, pwd, need_enc))) {
              LOG_WARN("Add user and pwd error", K(user_name), K(pwd), K(ret));
            } else {
              // do nothing
            }
          }
          OZ(check_user_dup_and_role_grant_option(
              params_.schema_checker_->get_schema_guard(), user_name_array, grant_stmt, user_name, host_name));
        }
      }
    }  // end of resolve users
  }

  return ret;
}

int ObGrantResolver::resolve_ora(const ParseNode& parse_tree)
{
  int ret = OB_SUCCESS;

  ParseNode* node = const_cast<ParseNode*>(&parse_tree);
  ObGrantStmt* grant_stmt = NULL;
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema_checker or session_info not inited",
        "schema_checker",
        params_.schema_checker_,
        "session_info",
        params_.session_info_,
        K(ret));
  } else if (node == NULL || (T_GRANT != node->type_ && T_SYSTEM_GRANT != node->type_) ||
             ((1 != node->num_child_) && (3 != node->num_child_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(ret));
  } else if (OB_ISNULL(grant_stmt = create_stmt<ObGrantStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("Failed to create ObCreateUserStmt", K(ret));
  } else {
    grant_stmt->set_stmt_type(T_GRANT == node->type_ ? stmt::T_GRANT : stmt::T_SYSTEM_GRANT);
    stmt_ = grant_stmt;
    uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
    grant_stmt->set_tenant_id(tenant_id);
    if (T_SYSTEM_GRANT == node->type_) {
      if (OB_FAIL(resolve_grant_system_privileges(node->children_[0], grant_stmt))) {
        LOG_WARN("resolve grant system privileges failed", K(ret));
      }
    } else {
      OZ(resolve_grant_obj_privileges(node, grant_stmt));
    }
  }
  return ret;
}

int ObGrantResolver::resolve_mysql(const ParseNode& parse_tree)
{
  int ret = OB_SUCCESS;

  ParseNode* node = const_cast<ParseNode*>(&parse_tree);
  ObGrantStmt* grant_stmt = NULL;
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema_checker or session_info not inited",
        "schema_checker",
        params_.schema_checker_,
        "session_info",
        params_.session_info_,
        K(ret));
  } else if (node == NULL || (T_GRANT != node->type_ && T_SYSTEM_GRANT != node->type_) ||
             ((1 != node->num_child_) && (3 != node->num_child_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Grant ParseNode error", K(ret));
  } else if (OB_ISNULL(grant_stmt = create_stmt<ObGrantStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("Failed to create ObCreateUserStmt", K(ret));
  } else {
    grant_stmt->set_stmt_type(T_GRANT == node->type_ ? stmt::T_GRANT : stmt::T_SYSTEM_GRANT);
    stmt_ = grant_stmt;
    uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
    grant_stmt->set_tenant_id(tenant_id);
    if (T_SYSTEM_GRANT == node->type_) {
      if (OB_FAIL(resolve_grant_system_privileges(node->children_[0], grant_stmt))) {
        LOG_WARN("resolve grant system privileges failed", K(ret));
      }
    } else {
      ParseNode* privs_node = node->children_[0];
      ParseNode* priv_level_node = node->children_[1];
      ParseNode* users_node = node->children_[2];
      if (privs_node != NULL && priv_level_node != NULL && users_node != NULL) {
        ObPrivLevel grant_level = OB_PRIV_INVALID_LEVEL;
        // resolve priv_level
        if (OB_SUCC(ret)) {
          ObString db = ObString::make_string("");
          ObString table = ObString::make_string("");
          if (OB_FAIL(resolve_priv_level(params_.schema_checker_->get_schema_guard(),
                  session_info_,
                  priv_level_node,
                  params_.session_info_->get_database_name(),
                  db,
                  table,
                  grant_level))) {
            LOG_WARN("Resolve priv_level node error", K(ret));
          } else if (OB_FAIL(check_and_convert_name(db, table))) {
            LOG_WARN("Check and convert name error", K(db), K(table), K(ret));
          } else {
            grant_stmt->set_grant_level(grant_level);
            if (OB_FAIL(grant_stmt->set_database_name(db))) {
              LOG_WARN("Failed to set database_name to grant_stmt", K(ret));
            } else if (OB_FAIL(grant_stmt->set_table_name(table))) {
              LOG_WARN("Failed to set table_name to grant_stmt", K(ret));
            } else {
              share::schema::ObObjectType object_type = share::schema::ObObjectType::INVALID;
              uint64_t object_id = OB_INVALID_ID;
              ObString object_db_name;
              if (db.empty() || table.empty()) {
                object_type = share::schema::ObObjectType::MAX_TYPE;
              } else {
                (void)params_.schema_checker_->get_object_type(
                    tenant_id, db, table, object_type, object_id, object_db_name, false, false, ObString(""));
              }
              grant_stmt->set_object_type(object_type);
              grant_stmt->set_object_id(object_id);
            }
          }
        }

        // resolve privileges
        if (OB_SUCC(ret)) {
          ObPrivSet priv_set = 0;
          if (OB_FAIL(resolve_priv_set(privs_node, grant_level, priv_set))) {
            LOG_WARN("Resolve priv set error", K(ret));
          } else {
            grant_stmt->set_priv_set(priv_set);
          }
        }

        // check whether table exist.If table no exist, priv set should contain create priv.
        if (OB_SUCC(ret)) {
          if (OB_PRIV_TABLE_LEVEL == grant_level) {  // need check if table exist
            bool exist = false;
            const bool is_index = false;
            const ObString& db = grant_stmt->get_database_name();
            const ObString& table = grant_stmt->get_table_name();
            if (OB_FAIL(params_.schema_checker_->check_table_exists(tenant_id, db, table, is_index, exist))) {
              LOG_WARN("Check table exist error", K(ret));
            } else if (!exist) {
              if (!(OB_PRIV_CREATE & grant_stmt->get_priv_set()) && !params_.is_restore_ &&
                  !params_.is_ddl_from_primary_) {
                ret = OB_TABLE_NOT_EXIST;
                LOG_WARN("table not exist", K(ret), K(table), K(db));
                LOG_USER_ERROR(OB_TABLE_NOT_EXIST, to_cstring(db), to_cstring(table));
              }
            } else {
              // do nothing
            }
          }
        }

        // resolve users
        if (OB_SUCC(ret)) {
          // oracle tenant, when grant , if user not exists, can't create user
          bool need_create_user = false;
          CHECK_COMPATIBILITY_MODE(session_info_);
          if (!lib::is_oracle_mode()) {
            need_create_user = (0 == (params_.session_info_->get_sql_mode() & SMO_NO_AUTO_CREATE_USER));
          }
          grant_stmt->set_need_create_user(need_create_user);
          if (users_node->num_child_ > 0) {
            for (int i = 0; OB_SUCC(ret) && i < users_node->num_child_; ++i) {
              ParseNode* user_node = users_node->children_[i];
              ObString user_name;
              ObString host_name;
              ObString pwd;
              ObString need_enc = ObString::make_string("NO");
              if (OB_ISNULL(user_node)) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("Parse SQL error, user node should not be NULL", K(user_node), K(ret));
              } else if (4 != user_node->num_child_) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("User specification's child node num error", K(ret));
              } else if (OB_ISNULL(user_node->children_[0])) {
                ret = OB_ERR_PARSE_SQL;
                LOG_WARN("The child 0 should not be NULL", K(ret));
              } else {
                user_name.assign_ptr(const_cast<char*>(user_node->children_[0]->str_value_),
                    static_cast<int32_t>(user_node->children_[0]->str_len_));
                if (NULL == user_node->children_[3]) {
                  host_name.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
                } else {
                  host_name.assign_ptr(
                      user_node->children_[3]->str_value_, static_cast<int32_t>(user_node->children_[3]->str_len_));
                }
                if (user_node->children_[1] != NULL) {
                  if (0 != user_name.compare(session_info_->get_user_name())) {
                    grant_stmt->set_need_create_user_priv(true);
                  }
                  pwd.assign_ptr(const_cast<char*>(user_node->children_[1]->str_value_),
                      static_cast<int32_t>(user_node->children_[1]->str_len_));
                  if (OB_ISNULL(user_node->children_[2])) {
                    ret = OB_ERR_PARSE_SQL;
                    LOG_WARN("The child 2 of user_node should not be NULL", K(ret));
                  } else if (0 == user_node->children_[2]->value_) {
                    if (!ObSetPasswordResolver::is_valid_mysql41_passwd(pwd)) {
                      ret = OB_ERR_PASSWORD_FORMAT;
                      LOG_WARN("Wrong password hash format");
                    }
                  } else {
                    need_enc = ObString::make_string("YES");
                  }
                } else {
                  pwd = ObString("");
                }
              }
              if (OB_SUCC(ret)) {
                if (user_name.length() > OB_MAX_USER_NAME_LENGTH) {
                  ret = OB_WRONG_USER_NAME_LENGTH;
                  LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, user_name.length(), user_name.ptr());
                } else if (OB_FAIL(grant_stmt->add_grantee(user_name))) {
                  LOG_WARN("Add grantee error", K(user_name), K(ret));
                } else if (OB_FAIL(grant_stmt->add_user(user_name, host_name, pwd, need_enc))) {
                  LOG_WARN("Add user and pwd error", K(user_name), K(pwd), K(ret));
                } else {
                  // do nothing
                }
              }
            }
          }
        }  // end of resolve users
      }
    }
  }
  return ret;
}

// 0 == priv_level_node->num_child_ -> grant priv on * to user
// 1 == priv_level_node->num_child_ -> grant priv on table to user
// 2 == priv_level_node->num_child_ -> grant priv on db.table to user
int ObGrantResolver::resolve_priv_level(ObSchemaGetterGuard* guard, const ObSQLSessionInfo* session,
    const ParseNode* node, const ObString& session_db, ObString& db, ObString& table, ObPrivLevel& grant_level)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(node), K(ret));
  } else {
    CK(guard != NULL);
    db = ObString::make_string("");
    table = ObString::make_string("");
    // 0 == priv_level_node->num_child_ -> grant priv on * to user
    // 0 == priv_level_node->num_child_ -> grant priv on table to user
    // 2 == priv_level_node->num_child_ -> grant priv on db.table to user
    if (0 == node->num_child_) {
      if (T_STAR == node->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
      } else if (T_IDENT == node->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        table.assign_ptr(node->str_value_, static_cast<const int32_t>(node->str_len_));
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
      if (OB_SUCC(ret)) {
        if (0 == session_db.length()) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("No database selected", K(ret));
        } else {
          db = session_db;
        }
      }
    } else if (T_PRIV_LEVEL == node->type_ && 2 == node->num_child_) {
      if (OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("Parse priv level error", K(ret), "child 0", node->children_[0], "child 1", node->children_[1]);
      } else if (T_STAR == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_USER_LEVEL;
      } else if (T_IDENT == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_, static_cast<const int32_t>(node->children_[0]->str_len_));
        OZ(ObSQLUtils::cvt_db_name_to_org(*guard, session, db));
      } else if (T_IDENT == node->children_[0]->type_ && T_IDENT == node->children_[1]->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_, static_cast<const int32_t>(node->children_[0]->str_len_));
        table.assign_ptr(node->children_[1]->str_value_, static_cast<const int32_t>(node->children_[1]->str_len_));
        OZ(ObSQLUtils::cvt_db_name_to_org(*guard, session, db));
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
    } else {
      ret = OB_ERR_PARSE_SQL;
      LOG_WARN("sql_parser parse grant_stmt error", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_PRIV_TABLE_LEVEL == grant_level && table.empty()) {
        ret = OB_WRONG_TABLE_NAME;
        LOG_USER_ERROR(OB_WRONG_TABLE_NAME, table.length(), table.ptr());
      } else if (!(OB_PRIV_USER_LEVEL == grant_level) && db.empty()) {
        // different with MySQL. MySQL may be error.
        ret = OB_WRONG_DB_NAME;
        LOG_USER_ERROR(OB_WRONG_DB_NAME, db.length(), db.ptr());
      } else {
        // do nothing
      }
    }
  }
  return ret;
}

// 0 == priv_level_node->num_child_ -> grant priv on * to user
// 1 == priv_level_node->num_child_ -> grant priv on table to user
// 2 == priv_level_node->num_child_ -> grant priv on db.table to user
/* resolve obj of oracle grant objauth */
int ObGrantResolver::resolve_obj_ora(const ParseNode* node, const ObString& session_db, ObString& db, ObString& table,
    ObPrivLevel& grant_level, bool& is_directory, bool& explicit_db)
{
  int ret = OB_SUCCESS;
  is_directory = false;
  explicit_db = false;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(node), K(ret));
  } else {
    db = ObString::make_string("");
    table = ObString::make_string("");
    // 0 == priv_level_node->num_child_ -> grant priv on * to user
    // 1 == priv_level_node->num_child_ -> grant priv on table to user
    // 2 == priv_level_node->num_child_ -> grant priv on db.table to user
    if (0 == node->num_child_) {
      if (T_STAR == node->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
      } else if (T_IDENT == node->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        table.assign_ptr(node->str_value_, static_cast<const int32_t>(node->str_len_));
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
      if (OB_SUCC(ret)) {
        if (0 == session_db.length()) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("No database selected", K(ret));
        } else {
          db = session_db;
        }
      }
    } else if (T_PRIV_LEVEL == node->type_ && 2 == node->num_child_) {
      if (OB_ISNULL(node->children_[0]) || OB_ISNULL(node->children_[1])) {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("Parse priv level error", K(ret), "child 0", node->children_[0], "child 1", node->children_[1]);
      } else if (T_STAR == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_USER_LEVEL;
      } else if (T_IDENT == node->children_[0]->type_ && T_STAR == node->children_[1]->type_) {
        grant_level = OB_PRIV_DB_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_, static_cast<const int32_t>(node->children_[0]->str_len_));
        explicit_db = true;
      } else if (T_IDENT == node->children_[0]->type_ && T_IDENT == node->children_[1]->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        db.assign_ptr(node->children_[0]->str_value_, static_cast<const int32_t>(node->children_[0]->str_len_));
        table.assign_ptr(node->children_[1]->str_value_, static_cast<const int32_t>(node->children_[1]->str_len_));
        explicit_db = true;
      } else if (T_PRIV_TYPE == node->children_[0]->type_ && T_IDENT == node->children_[1]->type_) {
        grant_level = OB_PRIV_TABLE_LEVEL;
        db = ObString::make_string("SYS");
        table.assign_ptr(node->children_[1]->str_value_, static_cast<const int32_t>(node->children_[1]->str_len_));
        is_directory = true;
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser error", K(ret));
      }
    } else {
      ret = OB_ERR_PARSE_SQL;
      LOG_WARN("sql_parser parse grant_stmt error", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_PRIV_TABLE_LEVEL == grant_level && table.empty()) {
        ret = OB_WRONG_TABLE_NAME;
        LOG_USER_ERROR(OB_WRONG_TABLE_NAME, table.length(), table.ptr());
      } else if (!(OB_PRIV_USER_LEVEL == grant_level) && db.empty()) {
        // different with MySQL. MySQL may be error.
        ret = OB_WRONG_DB_NAME;
        LOG_USER_ERROR(OB_WRONG_DB_NAME, db.length(), db.ptr());
      } else {
        // do nothing
      }
    }
  }
  return ret;
}

int ObGrantResolver::resolve_priv_set(const ParseNode* privs_node, ObPrivLevel grant_level, ObPrivSet& priv_set)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(privs_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, priv_node_list should not be NULL", K(privs_node), K(ret));
  } else if (OB_PRIV_INVALID_LEVEL == grant_level) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, grant_level should not be invalid", K(grant_level), K(ret));
  } else {
    for (int i = 0; i < privs_node->num_child_ && OB_SUCCESS == ret; ++i) {
      if (OB_NOT_NULL(privs_node->children_[i]) && T_PRIV_TYPE == privs_node->children_[i]->type_) {
        const ObPrivType priv_type = privs_node->children_[i]->value_;
        if (OB_PRIV_USER_LEVEL == grant_level) {
          priv_set |= priv_type;
        } else if (OB_PRIV_DB_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_DB_ACC;
          } else if (priv_type & (~(OB_PRIV_DB_ACC | OB_PRIV_GRANT))) {
            ret = OB_ERR_PRIV_USAGE;
            LOG_WARN("Grant/Revoke privilege than can not be used", "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else if (OB_PRIV_TABLE_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_TABLE_ACC;
          } else if (priv_type & (~(OB_PRIV_TABLE_ACC | OB_PRIV_GRANT))) {
            ret = OB_ILLEGAL_GRANT_FOR_TABLE;
            LOG_WARN("Grant/Revoke privilege than can not be used", "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else {
          // do nothing
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser parse privileges error", K(ret));
      }
    }
  }
  return ret;
}

int ObGrantResolver::map_mysql_priv_type_to_ora_type(
    const ObPrivType mysql_priv_type, share::ObRawObjPriv& ora_obj_priv, bool& can_map)
{
  int ret = OB_SUCCESS;
  can_map = true;
  switch (mysql_priv_type) {
    case OB_PRIV_ALTER:
      ora_obj_priv = OBJ_PRIV_ID_ALTER;
      break;
    case OB_PRIV_DELETE:
      ora_obj_priv = OBJ_PRIV_ID_DELETE;
      break;
    case OB_PRIV_GRANT:
      ora_obj_priv = OBJ_PRIV_ID_GRANT;
      break;
    case OB_PRIV_INDEX:
      ora_obj_priv = OBJ_PRIV_ID_INDEX;
      break;
    case OB_PRIV_INSERT:
      ora_obj_priv = OBJ_PRIV_ID_INSERT;
      break;
    case OB_PRIV_LOCK:
      ora_obj_priv = OBJ_PRIV_ID_LOCK;
      break;
    case OB_PRIV_SELECT:
      ora_obj_priv = OBJ_PRIV_ID_SELECT;
      break;
    case OB_PRIV_UPDATE:
      ora_obj_priv = OBJ_PRIV_ID_UPDATE;
      break;
    case OB_PRIV_REFERENCES:
      ora_obj_priv = OBJ_PRIV_ID_REFERENCES;
      break;
    case OB_PRIV_EXECUTE:
      ora_obj_priv = OBJ_PRIV_ID_EXECUTE;
      break;
    case OB_PRIV_DEBUG:
      ora_obj_priv = OBJ_PRIV_ID_DEBUG;
      break;
    case OB_PRIV_FLASHBACK:
      ora_obj_priv = OBJ_PRIV_ID_FLASHBACK;
      break;
    case OB_PRIV_READ:
      ora_obj_priv = OBJ_PRIV_ID_READ;
      break;
    case OB_PRIV_WRITE:
      ora_obj_priv = OBJ_PRIV_ID_WRITE;
      break;
    case OB_PRIV_COMMENT:
    case OB_PRIV_AUDIT:
    case OB_PRIV_RENAME:
    case OB_PRIV_CREATE:
    case OB_PRIV_CREATE_USER:
    case OB_PRIV_DROP:
    case OB_PRIV_CREATE_VIEW:
    case OB_PRIV_SHOW_VIEW:
    case OB_PRIV_SHOW_DB:
    case OB_PRIV_SUPER:
    case OB_PRIV_PROCESS:
    case 0:
    case OB_PRIV_CREATE_SYNONYM:
      can_map = false;
      break;
    default:
      can_map = false;
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("map_mysql_priv_type_to_ora_type fail, input mysql priv type error", K(ret), K(mysql_priv_type));
      break;
  }
  return ret;
}

int ObGrantResolver::check_obj_priv_valid(ObGrantStmt* grant_stmt, share::ObRawObjPriv ora_obj_priv)
{
  int ret = OB_SUCCESS;
  CK(grant_stmt != NULL);
  switch (grant_stmt->get_object_type()) {
    case (ObObjectType::TABLE):
    case (ObObjectType::INDEX):
      if (grant_stmt->get_ref_query() == NULL) {
        if (ora_obj_priv != OBJ_PRIV_ID_ALTER && ora_obj_priv != OBJ_PRIV_ID_DELETE &&
            ora_obj_priv != OBJ_PRIV_ID_INDEX && ora_obj_priv != OBJ_PRIV_ID_INSERT &&
            ora_obj_priv != OBJ_PRIV_ID_REFERENCES && ora_obj_priv != OBJ_PRIV_ID_SELECT &&
            ora_obj_priv != OBJ_PRIV_ID_UPDATE && ora_obj_priv != OBJ_PRIV_ID_FLASHBACK)
          ret = OB_ERR_EXECUTE_PRIVILEGE_NOT_ALLOWED_FOR_TABLES;
      } else {
        /* VIEW */
        if (ora_obj_priv != OBJ_PRIV_ID_DELETE && ora_obj_priv != OBJ_PRIV_ID_INSERT &&
            ora_obj_priv != OBJ_PRIV_ID_SELECT && ora_obj_priv != OBJ_PRIV_ID_UPDATE &&
            ora_obj_priv != OBJ_PRIV_ID_REFERENCES)
          ret = OB_ERR_ALTER_INDEX_AND_EXECUTE_NOT_ALLOWED_FOR_VIEWS;
      }
      break;

    case (ObObjectType::SEQUENCE):
      if (ora_obj_priv != OBJ_PRIV_ID_ALTER && ora_obj_priv != OBJ_PRIV_ID_SELECT) {
        ret = OB_ERR_ONLY_SELECT_AND_ALTER_PRIVILEGES_ARE_VALID_FOR_SEQUENCES;
      }
      break;

    case (ObObjectType::PACKAGE):
    case (ObObjectType::PROCEDURE):
    case (ObObjectType::FUNCTION):
      if (ora_obj_priv != OBJ_PRIV_ID_EXECUTE && ora_obj_priv != OBJ_PRIV_ID_DEBUG) {
        ret = OB_ERR_ONLY_EXECUTE_AND_DEBUG_PRIVILEGES_ARE_VALID_FOR_PROCEDURES;
      }
      break;

    case (ObObjectType::TYPE):
      if (ora_obj_priv != OBJ_PRIV_ID_EXECUTE && ora_obj_priv != OBJ_PRIV_ID_DEBUG) {
        ret = OB_ERR_ONLY_EXECUTE_DEBUG_AND_UNDER_PRIVILEGES_ARE_VALID_FOR_TYPES;
      }
      break;

    case (ObObjectType::DIRECTORY):
      if (ora_obj_priv != OBJ_PRIV_ID_READ && ora_obj_priv != OBJ_PRIV_ID_WRITE &&
          ora_obj_priv != OBJ_PRIV_ID_EXECUTE) {
        ret = OB_ERR_INVALID_PRIVILEGE_ON_DIRECTORIES;
      }
      break;
    /*to do: */
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("check_obj_priv_valid error", K(grant_stmt->get_object_type()));
  }
  return ret;
}

bool ObGrantResolver::is_ora_obj_priv_type(ObPrivType priv_type)
{
  if (priv_type == OB_PRIV_AUDIT || priv_type == OB_PRIV_COMMENT || priv_type == OB_PRIV_LOCK ||
      priv_type == OB_PRIV_RENAME || priv_type == OB_PRIV_REFERENCES || priv_type == OB_PRIV_EXECUTE ||
      priv_type == OB_PRIV_FLASHBACK || priv_type == OB_PRIV_READ || priv_type == OB_PRIV_WRITE) {
    return true;
  } else {
    return false;
  }
}

/* grant all on obj:
1. judege if owner
2. if not owner, extractr all privs with grant option */
int ObGrantResolver::build_table_priv_arary_for_all(
    ObGrantStmt* grant_stmt, share::ObRawObjPrivArray& table_priv_array, bool is_owner)
{
  int ret = OB_SUCCESS;
  CK(grant_stmt != NULL);
  if (OB_ISNULL(params_.schema_checker_) || OB_ISNULL(params_.session_info_)) {
    ret = OB_NOT_INIT;
  } else {
    ObSchemaGetterGuard* guard = params_.schema_checker_->get_schema_guard();
    const uint64_t tenant_id = params_.session_info_->get_effective_tenant_id();
    CK(guard != NULL);
    if (OB_SUCC(ret)) {
      share::ObPackedObjPriv obj_privs;
      share::schema::ObObjPrivSortKey obj_key(tenant_id,
          grant_stmt->get_object_id(),
          static_cast<uint64_t>(grant_stmt->get_object_type()),
          COL_ID_FOR_TAB_PRIV, /* grant all on col not supported */
          0,                   /* grantor */
          params_.session_info_->get_user_id());

      if (!is_owner) {
        bool exists = false;
        OZ(guard->get_obj_privs_in_ur_and_obj(tenant_id, obj_key, obj_privs));
        if (OB_SUCC(ret)) {
          for (int i = OBJ_PRIV_ID_NONE + 1; OB_SUCC(ret) && i < OBJ_PRIV_ID_MAX; i++) {
            OZ(share::ObOraPrivCheck::raw_obj_priv_exists(i, GRANT_OPTION, obj_privs, exists));
            if (OB_SUCC(ret) && exists) {
              OZ(table_priv_array.push_back(i));
            }
          }
        }
      } else {
        switch (grant_stmt->get_object_type()) {
          case share::schema::ObObjectType::TABLE:
          case share::schema::ObObjectType::INDEX: {
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_ALTER));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_DELETE));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_INDEX));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_INSERT));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_SELECT));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_FLASHBACK));
            break;
          }
          case share::schema::ObObjectType::SEQUENCE: {
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_ALTER));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_SELECT));
            break;
          }
          case share::schema::ObObjectType::VIEW: {
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_DELETE));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_INSERT));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_SELECT));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
            break;
          }
          case share::schema::ObObjectType::DIRECTORY: {
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_READ));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_WRITE));
            OZ(table_priv_array.push_back(OBJ_PRIV_ID_EXECUTE));
            break;
          }
          default:
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("build_table_priv_arary_for_all error", K(grant_stmt->get_object_type()));
            break;
        }
      }
    }
  }

  return ret;
}

int ObGrantResolver::trans_ora_sys_priv_to_obj(ParseNode* priv_type_node)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(priv_type_node) && T_ORACLE_SYS_PRIV_TYPE == priv_type_node->type_) {
    if (PRIV_ID_CREATE_USER == priv_type_node->value_) {
      priv_type_node->type_ = T_PRIV_TYPE;
      priv_type_node->value_ = OB_PRIV_CREATE_USER;
    } else if (PRIV_ID_CREATE_VIEW == priv_type_node->value_) {
      priv_type_node->type_ = T_PRIV_TYPE;
      priv_type_node->value_ = OB_PRIV_CREATE_VIEW;
    } else if (PRIV_ID_CREATE_SYN == priv_type_node->value_) {
      priv_type_node->type_ = T_PRIV_TYPE;
      priv_type_node->value_ = OB_PRIV_CREATE_SYNONYM;
    }
  }
  return ret;
}

int ObGrantResolver::resolve_obj_priv_list_ora(
    ObGrantStmt* grant_stmt, const ParseNode* privs_node, ObPrivLevel grant_level, bool is_owner)
{
  int ret = OB_SUCCESS;
  ObPrivSet priv_set = 0;
  share::ObRawObjPrivArray table_priv_array; /* only table level privs */
  ParseNode* priv_type_node = NULL;
  ParseNode* opt_colnames_node = NULL;
  share::ObRawObjPriv ora_obj_priv;
  if (OB_ISNULL(privs_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, priv_node_list should not be NULL", K(privs_node), K(ret));
  } else if (OB_PRIV_INVALID_LEVEL == grant_level) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, grant_level should not be invalid", K(grant_level), K(ret));
  } else {
    for (int i = 0; i < privs_node->num_child_ && OB_SUCCESS == ret; ++i) {
      if (OB_NOT_NULL(privs_node->children_[i]) && T_ORA_PRIV_TYPE == privs_node->children_[i]->type_) {
        CK(privs_node->children_[i]->num_child_ <= 2);
        priv_type_node = privs_node->children_[i]->children_[0];
        OZ(trans_ora_sys_priv_to_obj(priv_type_node));  // For compatibility with 2_2_1 on backup
        opt_colnames_node = privs_node->children_[i]->children_[1];
        if (OB_NOT_NULL(priv_type_node) && T_PRIV_TYPE == priv_type_node->type_) {
          const ObPrivType priv_type = priv_type_node->value_;
          if (OB_PRIV_USER_LEVEL == grant_level) {
            if (is_ora_obj_priv_type(priv_type) == false) {
              priv_set |= priv_type;
            }
          } else if (OB_PRIV_DB_LEVEL == grant_level) {
            if (OB_PRIV_ALL == priv_type) {
              priv_set |= OB_PRIV_DB_ACC;
            } else if (priv_type & (~(OB_PRIV_DB_ACC | OB_PRIV_GRANT))) {
              ret = OB_ERR_PRIV_USAGE;
              LOG_WARN("Grant/Revoke privilege than can not be used", "priv_type", ObPrintPrivSet(priv_type), K(ret));
            } else {
              if (is_ora_obj_priv_type(priv_type) == false) {
                priv_set |= priv_type;
              }
            }
          } else if (OB_PRIV_TABLE_LEVEL == grant_level) {
            if (OB_PRIV_ALL == priv_type) {
              priv_set |= OB_PRIV_TABLE_ACC;
              OZ(build_table_priv_arary_for_all(grant_stmt, table_priv_array, is_owner));
            } else if (priv_type & (~(OB_PRIV_TABLE_ACC | OB_PRIV_GRANT)) && is_ora_obj_priv_type(priv_type) == false) {
              ret = OB_ILLEGAL_GRANT_FOR_TABLE;
              LOG_WARN("Grant/Revoke privilege than can not be used", "priv_type", ObPrintPrivSet(priv_type), K(ret));
            } else {
              bool can_map;
              if (is_ora_obj_priv_type(priv_type) == false) {
                priv_set |= priv_type;
              }
              OZ(map_mysql_priv_type_to_ora_type(priv_type, ora_obj_priv, can_map));
              if (OB_SUCC(ret) && can_map) {
                if (ora_obj_priv != OBJ_PRIV_ID_GRANT) {
                  OZ(check_obj_priv_valid(grant_stmt, ora_obj_priv), K(ora_obj_priv), K(ret));
                  /* when no column, add into table priv array */
                  if (opt_colnames_node == NULL) {
                    OZ(table_priv_array.push_back(ora_obj_priv));
                  }
                } else {
                  OX(grant_stmt->set_option(GRANT_OPTION));
                }
              }
            }
          } else {
            // do nothing
          }
        } else if (OB_NOT_NULL(priv_type_node) && T_ORACLE_SYS_PRIV_TYPE == priv_type_node->type_) {
          if (OB_PRIV_USER_LEVEL == grant_level) {
            if (PRIV_ID_ALTER_SYSTEM == priv_type_node->value_) {
              priv_set |= OB_PRIV_ALTER_SYSTEM;
            }
          }
        }
        if (OB_PRIV_TABLE_LEVEL == grant_level) {
          OZ(resolve_col_names(grant_stmt, ora_obj_priv, opt_colnames_node));
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser parse privileges error", K(ret));
      }
    }
  }
  OX(grant_stmt->set_priv_set(priv_set));
  OZ(grant_stmt->set_obj_priv_array(table_priv_array));
  return ret;
}

/**
 * ObGrantResolver
 * Used to check if privs_node contain duplicated privileges
 * and if privs_node contain EXECUTE privilege
 * @param  {const ParseNode*} privs_node : node being checked
 * @param  {bool &} duplicated_privs     : flag on whether duplicated privilege was found
 * @param  {bool &} priv_has_execute     : flag on whether privs_node contain EXECUTE privilege
 * @return {int}                         : ret
 */
int ObGrantResolver::check_duplicated_privs_with_info_ora(
    const ParseNode* privs_node, bool& duplicated_privs, bool& priv_has_execute)
{
  int ret = OB_SUCCESS;
  ParseNode* priv_type_node = NULL;
  duplicated_privs = false;
  priv_has_execute = false;
  share::ObRawObjPriv ora_obj_priv;
  share::ObRawObjPrivArray obj_priv_array;  // Help to check duplicated privileges
  obj_priv_array.reset();

  if (OB_ISNULL(privs_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, privs_node should not be NULL", K(privs_node), K(ret));
  } else {
    // Scan every children to see if EXECUTE can be found inside privs_node
    for (int i = 0; i < privs_node->num_child_ && OB_SUCC(ret) && !priv_has_execute; ++i) {
      if (OB_NOT_NULL(privs_node->children_[i]) && T_ORA_PRIV_TYPE == privs_node->children_[i]->type_) {
        CK(privs_node->children_[i]->num_child_ <= 2);
        priv_type_node = privs_node->children_[i]->children_[0];
        // Only care about T_PRIV_TYPE
        if (OB_NOT_NULL(priv_type_node) && T_PRIV_TYPE == priv_type_node->type_) {
          const ObPrivType priv_type = priv_type_node->value_;
          // Check whether privs_node contain EXECUTE
          if (OB_PRIV_EXECUTE == priv_type) {
            LOG_DEBUG("Found OB_PRIV_EXECUTE", K(priv_type));
            priv_has_execute = true;
          }
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser parse privileges error", K(ret));
      }
    }

    // Scan every children to see if there are duplicated privileges
    for (int i = 0; i < privs_node->num_child_ && OB_SUCC(ret) && !duplicated_privs; ++i) {
      if (OB_NOT_NULL(privs_node->children_[i]) && T_ORA_PRIV_TYPE == privs_node->children_[i]->type_) {
        CK(privs_node->children_[i]->num_child_ <= 2);
        priv_type_node = privs_node->children_[i]->children_[0];
        // Only care about T_PRIV_TYPE
        if (OB_NOT_NULL(priv_type_node) && T_PRIV_TYPE == priv_type_node->type_) {
          const ObPrivType priv_type = priv_type_node->value_;
          if (OB_PRIV_ALL == priv_type) {
            // Do not check duplicated for OB_PRIV_ALL
          } else {
            // Check whether there are duplicated privs
            bool can_map = true;
            OZ(map_mysql_priv_type_to_ora_type(priv_type, ora_obj_priv, can_map));
            if (OB_SUCC(ret) && can_map) {
              if (ora_obj_priv != OBJ_PRIV_ID_GRANT) {
                bool exists = false;
                OZ(obj_priv_exists(obj_priv_array, ora_obj_priv, exists));
                if (OB_SUCC(ret) && exists) {
                  duplicated_privs = true;
                  LOG_WARN("duplicated privilege list", K(ora_obj_priv), K(ret));
                }
                OZ(obj_priv_array.push_back(ora_obj_priv));
              }
            }
          }
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        LOG_WARN("sql_parser parse privileges error", K(ret));
      }
    }
  }

  return ret;
}