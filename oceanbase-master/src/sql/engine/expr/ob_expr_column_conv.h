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

#ifndef _OB_SQL_EXPR_COLUMN_CONV_H_
#define _OB_SQL_EXPR_COLUMN_CONV_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_postfix_expression.h"

namespace oceanbase {
namespace sql {

class ObBaseExprColumnConv {
public:
  ObBaseExprColumnConv(common::ObIAllocator& alloc) : alloc_(alloc), str_values_(alloc_)
  {}
  virtual ~ObBaseExprColumnConv()
  {}

  const common::ObIArray<common::ObString>& get_str_values() const
  {
    return str_values_;
  }

  // this func is used in resolver
  int shallow_copy_str_values(const common::ObIArray<common::ObString>& str_values);
  // this func is used in code generator
  int deep_copy_str_values(const common::ObIArray<common::ObString>& str_values);

  ObBaseExprColumnConv(const ObBaseExprColumnConv& other) = delete;
  ObBaseExprColumnConv& operator=(const ObBaseExprColumnConv& other) = delete;

protected:
  common::ObIAllocator& alloc_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> str_values_;
};

// fast column convert is a optimized form of OExprColumnConvert
class ObFastColumnConvExpr : public ObBaseExprColumnConv, public ObFastExprOperator {
public:
  explicit ObFastColumnConvExpr(common::ObIAllocator& alloc);
  virtual ~ObFastColumnConvExpr()
  {}
  int assign(const ObFastExprOperator& other);
  virtual int calc(common::ObExprCtx& expr_ctx, const common::ObNewRow& row, common::ObObj& result) const;
  OB_INLINE void set_column_type(const ObExprResType& column_type)
  {
    column_type_ = column_type;
  }
  OB_INLINE const ObExprResType& get_column_type() const
  {
    return column_type_;
  }
  OB_INLINE void set_column(int64_t column_index)
  {
    value_item_.set_column(column_index);
  }
  int set_const_value(const common::ObObj& value);
  OB_INLINE void set_value_accuracy(const common::ObAccuracy& accuracy)
  {
    value_item_.set_accuracy(accuracy);
  }
  OB_INLINE int set_column_info(const ObString& column_info)
  {
    return ob_write_string(alloc_, column_info, column_info_);
  }
  OB_INLINE bool has_column_info() const
  {
    return !column_info_.empty();
  }
  OB_INLINE const ObString& get_column_info() const
  {
    return column_info_;
  }
  VIRTUAL_TO_STRING_KV(K_(column_type), K_(value_item), K_(column_info));

private:
  ObExprResType column_type_;
  ObPostExprItem value_item_;
  ObString column_info_;
};

class ObExprColumnConv : public ObBaseExprColumnConv, public ObFuncExprOperator {
  OB_UNIS_VERSION_V(1);

public:
  // objs[0]  type
  // objs[1] collation_type
  // objs[2] accuray_expr
  // objs[3] nullable_expr
  // objs[4] value
  // objs[5] column_info
  enum PARAMS_EXPLAIN { TYPE, COLLATION_TYPE, ACCURITY, NULLABLE, VALUE_EXPR, COLUMN_INFO, PARAMS_MAX };

  static const int64_t PARAMS_COUNT_WITHOUT_COLUMN_INFO = 5;
  static const int64_t PARAMS_COUNT_WITH_COLUMN_INFO = 6;

public:
  explicit ObExprColumnConv(common::ObIAllocator& alloc);
  virtual ~ObExprColumnConv();
  virtual int calc_result_typeN(
      ObExprResType& type, ObExprResType* types, int64_t param_num, common::ObExprTypeCtx& type_ctx) const;
  virtual int calc_resultN(
      common::ObObj& result, const common::ObObj* objs, int64_t param_num, common::ObExprCtx& expr_ctx) const;
  virtual int cg_expr(ObExprCGCtx& ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const;
  static int convert_with_null_check(common::ObObj& result, const common::ObObj& obj, const ObExprResType& res_type,
      bool is_strict, common::ObCastCtx& cast_ctx, const common::ObIArray<ObString>* type_infos = NULL);
  static int convert_skip_null_check(common::ObObj& result, const common::ObObj& obj, const ObExprResType& res_type,
      bool is_strict, common::ObCastCtx& cast_ctx, const common::ObIArray<ObString>* type_infos = NULL,
      const ObString* column_info = NULL);
  static int column_convert(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& datum);

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprColumnConv) const;
};

}  // namespace sql
}  // namespace oceanbase
#endif /* _OB_SQL_EXPR_COLUMN_CONV_H_ */
