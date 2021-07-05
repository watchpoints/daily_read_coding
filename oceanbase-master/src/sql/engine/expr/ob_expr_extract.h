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

#ifndef OCEANBASE_SQL_OB_EXPR_EXTRACT_H_
#define OCEANBASE_SQL_OB_EXPR_EXTRACT_H_

#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {
class ObPhysicalPlanCtx;
class ObExprExtract : public ObFuncExprOperator {
public:
  explicit ObExprExtract(common::ObIAllocator& alloc);
  virtual ~ObExprExtract();
  virtual int calc_result_type2(ObExprResType& type, ObExprResType& date_unit, ObExprResType& date,
      common::ObExprTypeCtx& type_ctx) const override;
  template <typename T>
  static int calc(T& result, const int64_t date_unit, const T& date, common::ObObjType date_type,
      const common::ObCastMode cast_mode, const common::ObTimeZoneInfo* tz_info, const int64_t cur_ts_value);
  template <typename T>
  static int calc_oracle(T& result, const int64_t date_unit, const T& date, common::ObObjType type,
      const ObSQLSessionInfo* session, common::ObIAllocator* calc_buf);
  virtual int calc_result2(common::ObObj& result, const common::ObObj& date_unit, const common::ObObj& date,
      common::ObExprCtx& expr_ctx) const override;
  virtual int cg_expr(ObExprCGCtx& op_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const override;
  static int calc_extract_oracle(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& expr_datum);
  static int calc_extract_mysql(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& expr_datum);

private:
  int set_result_type_oracle(
      common::ObExprTypeCtx& type_ctx, const ObExprResType& date_unit, ObExprResType& res_type) const;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprExtract);
};

}  // namespace sql
}  // namespace oceanbase
#endif  // OCEANBASE_SQL_OB_EXPR_EXTRACT_H_
