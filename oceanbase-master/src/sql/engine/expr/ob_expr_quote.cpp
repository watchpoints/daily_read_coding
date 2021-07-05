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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_quote.h"
#include "lib/oblog/ob_log.h"
#include "share/object/ob_obj_cast.h"
#include "sql/parser/ob_item_type.h"
//#include "sql/engine/expr/ob_expr_promotion_util.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase {
using namespace common;
namespace sql {

ObExprQuote::ObExprQuote(ObIAllocator& alloc) : ObStringExprOperator(alloc, T_OP_QUOTE, N_QUOTE, 1)
{}

ObExprQuote::~ObExprQuote()
{}

int ObExprQuote::calc_result_type1(ObExprResType& type, ObExprResType& type1, ObExprTypeCtx& type_ctx) const
{
  int ret = OB_SUCCESS;
  if (is_oracle_mode()) {
    const ObSQLSessionInfo* session = type_ctx.get_session();
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL", K(ret));
    } else {
      ObSEArray<ObExprResType*, 1, ObNullAllocator> params;
      OZ(params.push_back(&type1));
      OZ(aggregate_string_type_and_charset_oracle(*session, params, type));
      OZ(deduce_string_param_calc_type_and_charset(*session, type, params));
    }
  } else {
    type.set_varchar();
    type.set_length(type1.get_length());
    if OB_FAIL (aggregate_charsets_for_string_result(type, &type1, 1, type_ctx.get_coll_type())) {
      LOG_WARN("aggregate charset for res failed", K(ret));
    } else {
      type1.set_calc_type(ObVarcharType);
      type1.set_calc_collation_type(type.get_collation_type());
    }
  }
  return ret;
}

int ObExprQuote::calc_result1(ObObj& result, const ObObj& obj, ObExprCtx& expr_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr_ctx.calc_buf_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("calc buf is NULL", K(ret));
  } else if (obj.is_null()) {
    // in mysql, quote(null) does not return nulltype but 'null' (without enclosing single quotation marks)
    char* buf = reinterpret_cast<char*>(expr_ctx.calc_buf_->alloc(LEN_OF_NULL));
    if (NULL == buf) {  // alloc memory failed
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed.", K(ret));
    } else {
      MEMCPY(buf, "NULL", LEN_OF_NULL);
      ObString obstr;
      obstr.assign_ptr(buf, LEN_OF_NULL);
      result.set_varchar(obstr);
      result.set_collation(result_type_);
    }
  } else {
    ObString str = obj.get_string();
    ObString res_str;
    if (OB_FAIL(calc(res_str, str, obj.get_collation_type(), expr_ctx.calc_buf_))) {
      LOG_WARN("calc quote expr failed", K(ret), K(str));
    } else {
      result.set_varchar(res_str);
      result.set_collation(result_type_);
    }
  }
  return ret;
}

int ObExprQuote::string_write_buf(const ObString& str, char* buf, const int64_t buf_len, int64_t& pos)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(!str.empty())) {
    if (OB_UNLIKELY(pos + str.length() > buf_len)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      MEMCPY(buf + pos, str.ptr(), str.length());
      pos += str.length();
    }
  }
  return ret;
}

int ObExprQuote::calc(ObString& res_str, ObString str, ObCollationType coll_type,
    ObIAllocator* allocator)  // make sure alloc() is called once
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator)) {
    ret = OB_NOT_INIT;
    LOG_WARN("calc buf is NULL", K(ret));
  } else {
    ObString escape_char = ObCharsetUtils::get_const_str(coll_type, '\\');
    int64_t buf_len = str.length() * 2 + 2 * escape_char.length();
    int64_t pos = 0;
    char* buf = reinterpret_cast<char*>(allocator->alloc(buf_len));
    if (NULL == buf) {  // alloc memory failed
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed.", K(ret));
    } else {
      auto handle_char_func = [&](ObString code_point, int wchar) -> int {
        int ret = OB_SUCCESS;
        switch (wchar) {
          case '\0': {
            OZ(string_write_buf(escape_char, buf, buf_len, pos));
            OZ(string_write_buf(ObCharsetUtils::get_const_str(coll_type, '0'), buf, buf_len, pos));
            break;
          }
          case '\'': {
            OZ(string_write_buf(escape_char, buf, buf_len, pos));
            OZ(string_write_buf(ObCharsetUtils::get_const_str(coll_type, '\''), buf, buf_len, pos));
            break;
          }
          case '\\': {
            OZ(string_write_buf(escape_char, buf, buf_len, pos));
            OZ(string_write_buf(escape_char, buf, buf_len, pos));
            break;
          }
          case '\032': {
            OZ(string_write_buf(escape_char, buf, buf_len, pos));
            OZ(string_write_buf(ObCharsetUtils::get_const_str(coll_type, 'Z'), buf, buf_len, pos));
            break;
          }
          default: {
            OZ(string_write_buf(code_point, buf, buf_len, pos));
            break;
          }
        }
        LOG_DEBUG("debug result", K(wchar), KPHEX(buf, pos));
        return ret;
      };
      OZ(string_write_buf(ObCharsetUtils::get_const_str(coll_type, '\''), buf, buf_len, pos));
      OZ(ObCharsetUtils::foreach_char(str, coll_type, handle_char_func));
      OZ(string_write_buf(ObCharsetUtils::get_const_str(coll_type, '\''), buf, buf_len, pos));
      res_str.assign_ptr(buf, pos);
    }
  }
  return ret;
}

int ObExprQuote::calc_quote_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum* arg = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, arg))) {
    LOG_WARN("eval param failed", K(ret));
  } else if (arg->is_null()) {
    // in mysql, quote(null) does not return nulltype but 'null'
    // (without enclosing single quotation marks)
    ObExprStrResAlloc res_alloc(expr, ctx);
    char* buf = reinterpret_cast<char*>(res_alloc.alloc(LEN_OF_NULL));
    if (NULL == buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed.", K(ret));
    } else {
      MEMCPY(buf, "NULL", LEN_OF_NULL);
      res_datum.set_string(buf, LEN_OF_NULL);
    }
  } else {
    const ObString& str = arg->get_string();
    ObString res_str;
    ObExprStrResAlloc res_alloc(expr, ctx);
    if (OB_FAIL(calc(res_str, str, expr.datum_meta_.cs_type_, &res_alloc))) {
      LOG_WARN("calc quote expr failed", K(ret), K(str));
    } else {
      res_datum.set_string(res_str);
    }
  }
  return ret;
}

int ObExprQuote::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_quote_expr;
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
