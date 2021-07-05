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

#ifndef _OB_EXPR_TRUNCATE_H_
#define _OB_EXPR_TRUNCATE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {
class ObExprTruncate : public ObFuncExprOperator {
public:
  explicit ObExprTruncate(common::ObIAllocator& alloc);
  virtual ~ObExprTruncate(){};

  virtual int calc_result_type2(
      ObExprResType& type, ObExprResType& type1, ObExprResType& type2, common::ObExprTypeCtx& type_ctx) const override;

  virtual int calc_result2(common::ObObj& result, const common::ObObj& obj1, const common::ObObj& obj2,
      common::ObExprCtx& expr_ctx) const override;
  virtual int cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const;
  static int set_trunc_val(
      common::ObObj& result, common::number::ObNumber& nmb, common::ObExprCtx& expr_ctx, common::ObObjType res_type);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprTruncate);
};
}  // namespace sql
}  // namespace oceanbase

#endif  // _OB_EXPR_TRUNCATE_H_
