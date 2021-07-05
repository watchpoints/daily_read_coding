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

#ifndef _OB_RAW_EXPR_DEDUCE_TYPE_H
#define _OB_RAW_EXPR_DEDUCE_TYPE_H 1
#include "sql/resolver/expr/ob_raw_expr.h"
#include "lib/container/ob_iarray.h"
#include "common/ob_accuracy.h"
#include "share/ob_i_sql_expression.h"
namespace oceanbase {
namespace sql {
class ObRawExprDeduceType : public ObRawExprVisitor {
public:
  ObRawExprDeduceType(const ObSQLSessionInfo* my_session)
      : ObRawExprVisitor(), my_session_(my_session), alloc_(), expr_factory_(NULL)
  {}
  virtual ~ObRawExprDeduceType()
  {
    alloc_.reset();
  }
  void set_expr_factory(ObRawExprFactory* expr_factory)
  {
    expr_factory_ = expr_factory;
  }
  int deduce(ObRawExpr& expr);
  /// interface of ObRawExprVisitor
  virtual int visit(ObConstRawExpr& expr);
  virtual int visit(ObVarRawExpr& expr);
  virtual int visit(ObQueryRefRawExpr& expr);
  virtual int visit(ObColumnRefRawExpr& expr);
  virtual int visit(ObOpRawExpr& expr);
  virtual int visit(ObCaseOpRawExpr& expr);
  virtual int visit(ObAggFunRawExpr& expr);
  virtual int visit(ObSysFunRawExpr& expr);
  virtual int visit(ObSetOpRawExpr& expr);
  virtual int visit(ObAliasRefRawExpr& expr);
  virtual int visit(ObFunMatchAgainst& expr);
  virtual int visit(ObWinFunRawExpr& expr);
  virtual int visit(ObPseudoColumnRawExpr& expr);

  int add_implicit_cast(ObOpRawExpr& parent, const ObCastMode& cast_mode);
  int add_implicit_cast(ObCaseOpRawExpr& parent, const ObCastMode& cast_mode);
  int add_implicit_cast(ObAggFunRawExpr& parent, const ObCastMode& cast_mode);

  int check_type_for_case_expr(ObCaseOpRawExpr& case_expr, common::ObIAllocator& alloc);
  static bool skip_cast_expr(const ObRawExpr& parent, const int64_t child_idx);

private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprDeduceType);
  // function members
  int assign_var_expr_result_type(ObRawExpr* expr, const ObRawExpr* origin_param, bool& assigned);
  int assign_var_exprs_result_type(ObNonTerminalRawExpr& expr, ObIExprResTypes& types);
  int calc_result_type(
      ObNonTerminalRawExpr& expr, ObIExprResTypes& types, common::ObCastMode& cast_mode, int32_t row_dimension);
  int calc_result_type_with_const_arg(ObNonTerminalRawExpr& expr, ObIExprResTypes& types,
      common::ObExprTypeCtx& type_ctx, ObExprOperator* op, ObExprResType& result_type, int32_t row_dimension);
  int check_expr_param(ObOpRawExpr& expr);
  int check_row_param(ObOpRawExpr& expr);
  int check_param_expr_op_row(ObRawExpr* param_expr, int64_t column_count);
  int visit_left_param(ObRawExpr& expr);
  int visit_right_param(ObOpRawExpr& expr);
  int64_t get_expr_output_column(const ObRawExpr& expr);
  int get_row_expr_param_type(const ObRawExpr& expr, ObIExprResTypes& types);
  int deduce_type_visit_for_special_func(int64_t param_index, const ObRawExpr& expr, ObIExprResTypes& types);
  // init udf expr
  int init_normal_udf_expr(ObNonTerminalRawExpr& expr, ObExprOperator* op);
  // get agg udf result type
  int set_agg_udf_result_type(ObAggFunRawExpr& expr);

  // helper functions for add_implicit_cast
  int add_implicit_cast_for_op_row(ObOpRawExpr& parent, ObRawExpr* child_ptr,
      const common::ObIArray<ObExprResType>& input_types, const ObCastMode& cast_mode);
  // try add cast expr on subquery stmt's oubput && update column types.
  int add_implicit_cast_for_subquery(
      ObQueryRefRawExpr& expr, const common::ObIArray<ObExprResType>& input_types, const ObCastMode& cast_mode);
  // try add cast expr above %child_idx child,
  // %input_type.get_calc_meta() is the destination type!
  template <typename RawExprType>
  int try_add_cast_expr(
      RawExprType& parent, int64_t child_idx, const ObExprResType& input_type, const ObCastMode& cast_mode);

  // try add cast expr above %expr , set %new_expr to &expr if no cast added.
  // %input_type.get_calc_meta() is the destination type!
  int try_add_cast_expr_above_for_deduce_type(
      ObRawExpr& expr, ObRawExpr*& new_expr, const ObExprResType& input_type, const ObCastMode& cm);
  int check_group_aggr_param(ObAggFunRawExpr& expr);
  int check_median_percentile_param(ObAggFunRawExpr& expr);
  int add_median_percentile_implicit_cast(ObAggFunRawExpr& expr, const ObCastMode& cast_mode, const bool keep_type);
  int add_group_aggr_implicit_cast(ObAggFunRawExpr& expr, const ObCastMode& cast_mode);
  int adjust_cast_as_signed_unsigned(ObSysFunRawExpr& expr);

private:
  const sql::ObSQLSessionInfo* my_session_;
  common::ObArenaAllocator alloc_;
  ObRawExprFactory* expr_factory_;
  // data members
};

}  // end namespace sql
}  // end namespace oceanbase

#endif /* _OB_RAW_EXPR_DEDUCE_TYPE_H */
