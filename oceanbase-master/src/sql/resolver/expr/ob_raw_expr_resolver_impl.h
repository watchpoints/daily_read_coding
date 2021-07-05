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

#ifndef OCEANBASE_SQL_OB_RAW_EXPR_RESOLVER_IMPL_H
#define OCEANBASE_SQL_OB_RAW_EXPR_RESOLVER_IMPL_H
#include "sql/resolver/ob_column_ref.h"
#include "ob_raw_expr_resolver.h"

namespace oceanbase {
namespace sql {
class ObRawExprResolverImpl : public ObRawExprResolver {
  static const int32_t OUT_OF_STR_LEN = -2;
  struct AggNestedCheckerGuard {
    AggNestedCheckerGuard(ObExprResolveContext& ctx) : ctx_(ctx), ret_(common::OB_SUCCESS)
    {}
    ~AggNestedCheckerGuard()
    {
      ObExprResolveContext::ObAggResolveLinkNode* node = ctx_.agg_resolve_link_.remove_last();
      if (node != NULL) {
        ctx_.is_win_agg_ = node->is_win_agg_;
      }
    }
    int check_agg_nested(bool& is_in_nested_aggr);
    ObExprResolveContext& ctx_;
    int ret_;
  };

public:
  explicit ObRawExprResolverImpl(ObExprResolveContext& ctx);
  virtual ~ObRawExprResolverImpl()
  {}

  int resolve(const ParseNode* node, ObRawExpr*& expr, common::ObIArray<ObQualifiedName>& columns,
      common::ObIArray<ObVarInfo>& sys_vars, common::ObIArray<ObSubQueryInfo>& sub_query_info,
      common::ObIArray<ObAggFunRawExpr*>& aggr_exprs, common::ObIArray<ObWinFunRawExpr*>& win_exprs,
      common::ObIArray<ObOpRawExpr*>& op_exprs, common::ObIArray<ObUserVarIdentRawExpr*>& user_var_exprs);

  bool is_contains_assignment()
  {
    return is_contains_assignment_;
  }
  void set_contains_assignment(bool v)
  {
    is_contains_assignment_ = v;
  }

  static int check_sys_func(ObQualifiedName& q_name, bool& is_sys_func);

  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprResolverImpl);
  // function members
  int try_negate_const(ObRawExpr*& expr, const int64_t neg_cnt, int64_t& remain_reg_cnt);
  int do_recursive_resolve(const ParseNode* node, ObRawExpr*& expr);
  int recursive_resolve(const ParseNode* node, ObRawExpr*& expr);
  int process_datatype_or_questionmark(const ParseNode& node, ObRawExpr*& expr);
  int process_system_variable_node(const ParseNode* node, ObRawExpr*& expr);
  int process_char_charset_node(const ParseNode* node, ObRawExpr*& expr);

  int process_left_value_node(const ParseNode* node, ObRawExpr*& expr);
  int process_outer_join_symbol_node(const ParseNode* node, ObRawExpr*& expr);
  int process_column_ref_node(const ParseNode* node, ObRawExpr*& expr);
  template <class T>
  int process_node_with_children(const ParseNode* node, int64_t children_num, T*& raw_expr);
  int process_any_or_all_node(const ParseNode* node, ObRawExpr*& expr);
  int process_not_node(const ParseNode* node, ObRawExpr*& expr);
  int process_user_var_node(const ParseNode* node, ObRawExpr*& expr);
  int process_pos_or_neg_node(const ParseNode* node, ObRawExpr*& expr);
  int process_operator_node(const ParseNode* node, ObRawExpr*& expr);
  int process_is_or_is_not_node(const ParseNode* node, ObRawExpr*& expr);
  int process_regexp_or_not_regexp_node(const ParseNode* node, ObRawExpr*& expr);
  int process_like_node(const ParseNode* node, ObRawExpr*& expr);
  int process_between_node_mysql(const ParseNode* node, ObRawExpr*& expr);
  int process_between_node_ora(const ParseNode* node, ObRawExpr*& expr);
  int process_in_or_not_in_node(const ParseNode* node, ObRawExpr*& expr);
  int process_case_node(const ParseNode* node, ObRawExpr*& expr);
  int process_sub_query_node(const ParseNode* node, ObRawExpr*& expr);
  int process_agg_node(const ParseNode* node, ObRawExpr*& expr);
  int process_group_aggr_node(const ParseNode* node, ObRawExpr*& expr);
  int process_keep_aggr_node(const ParseNode* node, ObRawExpr*& expr);
  int process_sort_list_node(const ParseNode* node, ObAggFunRawExpr* parent_agg_expr);
  int process_timestamp_node(const ParseNode* node, common::ObString& err_info, ObRawExpr*& expr);
  int process_oracle_timestamp_node(
      const ParseNode* node, ObRawExpr*& expr, int16_t min_precision, int16_t max_precision, int16_t default_precision);
  int process_collation_node(const ParseNode* node, ObRawExpr*& expr);
  int process_if_node(const ParseNode* node, ObRawExpr*& expr);
  int process_fun_interval_node(const ParseNode* node, ObRawExpr*& expr);
  int process_isnull_node(const ParseNode* node, ObRawExpr*& expr);
  int process_lnnvl_node(const ParseNode* node, ObRawExpr*& expr);
  int process_fun_sys_node(const ParseNode* node, ObRawExpr*& expr);
  int process_dll_udf_node(const ParseNode* node, ObRawExpr*& expr);
  int process_agg_udf_node(const ParseNode* node, const share::schema::ObUDF& udf_info, ObAggFunRawExpr*& expr);
  int process_normal_udf_node(const ParseNode* node, const common::ObString& udf_name,
      const share::schema::ObUDF& udf_info, ObSysFunRawExpr*& expr);
  int resolve_udf_param_expr(const ParseNode* node, common::ObIArray<ObRawExpr*>& param_exprs);
  int process_fun_match_against(const ParseNode* node, ObRawExpr*& expr);
  int process_window_function_node(const ParseNode* node, ObRawExpr*& expr);
  int process_sort_list_node(const ParseNode* node, common::ObIArray<OrderItem>& order_items);
  int process_frame_node(const ParseNode* node, ObFrame& frame);
  int process_window_agg_node(
      const ObItemType func_type, ObWinFunRawExpr* win_func, ObRawExpr* agg_node_expr, ObRawExpr*& expr);
  int process_window_complex_agg_node(const ObItemType func_type, ObWinFunRawExpr* win_func, ObRawExpr*& agg_node_expr);
  int process_bound_node(const ParseNode* node, const bool is_upper, Bound& bound);
  int process_interval_node(
      const ParseNode* node, bool& is_nmb_literal, ObRawExpr*& interval_expr, ObRawExpr*& date_unit_expr);
  bool is_win_expr_valid_scope(ObStmtScope scope) const;
  int check_and_canonicalize_window_expr(ObRawExpr* expr);
  int process_ident_node(const ParseNode& node, ObRawExpr*& expr);
  int process_multiset_node(const ParseNode* node, ObRawExpr*& expr);
  int process_obj_access_node(const ParseNode& node, ObRawExpr*& expr);
  int resolve_obj_access_idents(const ParseNode& node, ObQualifiedName& q_name);
  int check_name_type(ObQualifiedName& q_name, ObStmtScope scope, AccessNameType& type);
  int check_dll_udf(ObQualifiedName& q_name, bool& is_dll_udf);
  int is_explict_func_expr(const ParseNode& node, bool& is_func);
  int process_pseudo_column_node(const ParseNode& node, ObRawExpr*& expr);
  inline bool is_pseudo_column_valid_scope(ObStmtScope scope) const;
  int process_connect_by_root_node(const ParseNode& node, ObRawExpr*& expr);
  inline bool is_connec_by_root_expr_valid_scope(ObStmtScope scope) const;
  int process_sys_connect_by_path_node(const ParseNode* node, ObRawExpr*& expr);
  int check_sys_connect_by_path_params(const ObRawExpr* param1, const ObRawExpr* param2);
  inline bool is_sys_connect_by_path_expr_valid_scope(ObStmtScope scope) const;
  int process_prior_node(const ParseNode& node, ObRawExpr*& expr);
  inline bool is_prior_expr_valid_scope(ObStmtScope scope) const;
  static bool should_not_contain_window_clause(const ObItemType func_type);
  static bool should_contain_order_by_clause(const ObItemType func_type);
  int process_sqlerrm_node(const ParseNode* node, ObRawExpr*& expr);

  static bool check_frame_and_order_by_valid(const ObItemType func_type, const bool has_order_by, const bool has_frame);
  int convert_keep_aggr_to_common_aggr(ObAggFunRawExpr*& agg_expr);

  static int not_row_check(const ObRawExpr* expr);
  static int param_not_row_check(const ObRawExpr* expr);

private:
  int transform_ratio_afun_to_arg_div_sum(const ParseNode* ratio_to_report, ParseNode*& div);
  int convert_any_or_all_expr(ObRawExpr*& expr, bool& happened);
  int get_opposite_string(
      const common::ObString& orig_string, common::ObString& new_string, common::ObIAllocator& allocator);
  int reset_keep_aggr_sort_direction(ObIArray<OrderItem>& aggr_sort_item);
  int reset_aggr_sort_nulls_first(ObIArray<OrderItem>& aggr_sort_item);

  // data members
  ObExprResolveContext& ctx_;
  bool is_contains_assignment_;
};
template <class T>
int ObRawExprResolverImpl::process_node_with_children(const ParseNode* node, int64_t children_num, T*& raw_expr)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(node) || OB_ISNULL(node->children_) || children_num <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument", K(node), K(children_num));
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(node->type_, raw_expr))) {
    SQL_RESV_LOG(WARN, "fail to create raw expr", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < children_num; i++) {
      ObRawExpr* sub_expr = NULL;
      if (OB_ISNULL(node->children_[i])) {
        ret = common::OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(ERROR, "invalid node children", K(ret), K(i), K(node));
      } else if (OB_FAIL(recursive_resolve(node->children_[i], sub_expr))) {
        SQL_RESV_LOG(WARN, "resolve left child failed", K(ret));
      } else if (OB_FAIL(raw_expr->add_param_expr(sub_expr))) {
        SQL_RESV_LOG(WARN, "fail to set param expr", K(ret), K(sub_expr));
      }
    }
  }
  return ret;
}

inline bool ObRawExprResolverImpl::is_prior_expr_valid_scope(ObStmtScope scope) const
{
  return scope == T_FIELD_LIST_SCOPE || scope == T_WHERE_SCOPE || scope == T_GROUP_SCOPE || scope == T_HAVING_SCOPE ||
         scope == T_ORDER_SCOPE || scope == T_CONNECT_BY_SCOPE;
}

inline bool ObRawExprResolverImpl::is_sys_connect_by_path_expr_valid_scope(ObStmtScope scope) const
{
  return scope == T_FIELD_LIST_SCOPE || scope == T_GROUP_SCOPE || scope == T_HAVING_SCOPE || scope == T_ORDER_SCOPE ||
         scope == T_CONNECT_BY_SCOPE;
}

inline bool ObRawExprResolverImpl::is_connec_by_root_expr_valid_scope(ObStmtScope scope) const
{
  return scope == T_FIELD_LIST_SCOPE || scope == T_WHERE_SCOPE || scope == T_GROUP_SCOPE || scope == T_HAVING_SCOPE ||
         scope == T_ORDER_SCOPE || scope == T_CONNECT_BY_SCOPE;
}

inline bool ObRawExprResolverImpl::is_win_expr_valid_scope(ObStmtScope scope) const
{
  return scope == T_FIELD_LIST_SCOPE || scope == T_NAMED_WINDOWS_SCOPE || scope == T_ORDER_SCOPE;
}

inline bool ObRawExprResolverImpl::is_pseudo_column_valid_scope(ObStmtScope scope) const
{
  return scope == T_FIELD_LIST_SCOPE || scope == T_WHERE_SCOPE || scope == T_GROUP_SCOPE || scope == T_HAVING_SCOPE ||
         scope == T_ORDER_SCOPE || scope == T_CONNECT_BY_SCOPE || scope == T_WITH_CLAUSE_SEARCH_SCOPE ||
         scope == T_WITH_CLAUSE_CYCLE_SCOPE;
}

}  // end namespace sql
}  // end namespace oceanbase

#endif /* OCEANBASE_SQL_OB_RAW_EXPR_RESOLVER_IMPL_H*/
