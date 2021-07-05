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

#ifndef _OCEANBASE_SQL_OB_EXPR_ORACLE_TO_CHAR_H_
#define _OCEANBASE_SQL_OB_EXPR_ORACLE_TO_CHAR_H_
#include "lib/ob_name_def.h"
#include "lib/allocator/ob_allocator.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase {
namespace sql {

class ObExprToCharCommon : public ObStringExprOperator {
public:
  using ObStringExprOperator::ObStringExprOperator;

  static int number_to_char(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx);
  static int datetime_to_char(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx);
  static int calc_result_length_for_string_param(ObExprResType& type, const ObExprResType& param);
  virtual int cg_expr(ObExprCGCtx& op_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const override;

  static int eval_oracle_to_char(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& expr_datum);

protected:
  static int interval_to_char(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx);

  static int process_number_value(
      common::ObExprCtx& expr_ctx, const common::ObObj& obj, const int scale, common::ObString& number_str);

  static int process_number_format(common::ObString& fmt_raw, int& scale, bool& has_fm);

  static int format_number(const char* number_str, const int64_t number_len, const char* format_str,
      const int64_t format_len, char* result_buf, int64_t& result_size, bool has_fm);
  static int64_t trim_number(const common::ObString& number);

  static int is_valid_to_char_number(const common::ObObj* objs_array, const int64_t param_num);
  static int process_number_sci_value(
      common::ObExprCtx& expr_ctx, const common::ObObj& obj, const int scale, common::ObString& number_sci);

  // functions for static typing engine, it's hard to reuse the code of old engine,
  // we copy the old functions adapt it to new engine.
  static int is_valid_to_char_number(const ObExpr& expr, const common::ObString& fmt);

  static int datetime_to_char(const ObExpr& expr, ObEvalCtx& ctx, common::ObIAllocator& alloc,
      const common::ObDatum& input, const common::ObString& fmt, const common::ObString& nlsparam,
      common::ObString& res);

  static int interval_to_char(const ObExpr& expr, ObEvalCtx& ctx, common::ObIAllocator& alloc,
      const common::ObDatum& input, const common::ObString& fmt, const common::ObString& nlsparam,
      common::ObString& res);

  static int number_to_char(const ObExpr& expr, common::ObIAllocator& alloc, const common::ObDatum& input,
      common::ObString& fmt, common::ObString& res);

  static int process_number_sci_value(const ObExpr& expr, common::ObIAllocator& alloc, const common::ObDatum& input,
      const int scale, common::ObString& res);

  static int process_number_value(const ObExpr& expr, common::ObIAllocator& alloc, const common::ObDatum& input,
      const int scale, common::ObString& res);
};

class ObExprOracleToChar : public ObExprToCharCommon {
public:
  explicit ObExprOracleToChar(common::ObIAllocator& alloc);
  virtual ~ObExprOracleToChar();
  virtual int calc_result_typeN(
      ObExprResType& type, ObExprResType* type_array, int64_t params_count, common::ObExprTypeCtx& type_ctx) const;
  virtual int calc_resultN(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx) const;
};

class ObExprOracleToNChar : public ObExprToCharCommon {
public:
  explicit ObExprOracleToNChar(common::ObIAllocator& alloc);
  virtual ~ObExprOracleToNChar();
  virtual int calc_result_typeN(
      ObExprResType& type, ObExprResType* type_array, int64_t params_count, common::ObExprTypeCtx& type_ctx) const;
  virtual int calc_resultN(
      common::ObObj& result, const common::ObObj* objs_array, int64_t param_num, common::ObExprCtx& expr_ctx) const;
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OB_EXPR_ORACLE_TO_CHAR_H_
