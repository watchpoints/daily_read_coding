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

#ifndef OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_WORD_SEGMENT_H_
#define OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_WORD_SEGMENT_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {
class ObExprWordSegment : public ObFuncExprOperator {
public:
  explicit ObExprWordSegment(common::ObIAllocator& alloc);
  virtual ~ObExprWordSegment()
  {}
  virtual int calc_result_typeN(
      ObExprResType& type, ObExprResType* types, int64_t param_num, common::ObExprTypeCtx& type_ctx) const;
  virtual int calc_resultN(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx) const;
  virtual common::ObCastMode get_cast_mode() const
  {
    return CM_NULL_ON_WARN;
  }
  inline void set_tokenizer(const common::ObString& tokenizer)
  {
    tokenizer_ = tokenizer;
  }

private:
  common::ObString tokenizer_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprWordSegment);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_ENGINE_EXPR_OB_EXPR_WORD_SEGMENT_H_ */
