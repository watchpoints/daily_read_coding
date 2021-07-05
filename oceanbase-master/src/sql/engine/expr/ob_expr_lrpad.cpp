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

#include "sql/engine/expr/ob_expr_lrpad.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/ob_exec_context.h"
#include <limits.h>
#include <string.h>

#include "sql/session/ob_sql_session_info.h"
#include "share/ob_worker.h"

namespace oceanbase {
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace sql {
/* ObExprBaseLRpad {{{1 */
/* common util {{{2 */
ObExprBaseLRpad::ObExprBaseLRpad(ObIAllocator& alloc, ObExprOperatorType type, const char* name, int32_t param_num)
    : ObStringExprOperator(alloc, type, name, param_num)
{}

ObExprBaseLRpad::~ObExprBaseLRpad()
{}

// See: ObExprBaseLRpad::calc_mysql
int ObExprBaseLRpad::calc_type_length_mysql(const ObExprResType result_type, const ObObj& text, const ObObj& pad_text,
    const ObObj& len, const ObBasicSessionInfo* session, int64_t& result_size)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = OB_MAX_VARCHAR_LENGTH;
  int64_t int_len = 0;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  int64_t text_len = 0;

  ObString str_text;
  ObString str_pad;
  result_size = max_result_size;
  ObExprCtx expr_ctx;
  ObArenaAllocator allocator(common::ObModIds::OB_SQL_EXPR_CALC);
  expr_ctx.calc_buf_ = &allocator;
  if (OB_NOT_NULL(session) && OB_FAIL(session->get_max_allowed_packet(max_result_size))) {
    if (OB_ENTRY_NOT_EXIST == ret) {  // for compatibility with server before 1470
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("Failed to get max allow packet size", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(ObExprUtil::get_round_int64(len, expr_ctx, int_len))) {
    LOG_WARN("get_round_int64 failed and ignored", K(ret));
    ret = OB_SUCCESS;
  } else {
    if (!ob_is_string_type(text.get_type())) {
      result_size = int_len;
    } else if (OB_FAIL(text.get_string(str_text))) {
      LOG_WARN("Failed to get str_text", K(ret), K(text));
    } else if (FALSE_IT(
                   text_len = ObCharset::strlen_char(
                       result_type.get_collation_type(), const_cast<const char*>(str_text.ptr()), str_text.length()))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if (text_len >= int_len) {
      // only substr needed
      result_size = ObCharset::charpos(result_type.get_collation_type(), str_text.ptr(), str_text.length(), int_len);
    } else {
      if (!ob_is_string_type(pad_text.get_type())) {
        result_size = int_len;
      } else if (OB_FAIL(pad_text.get_string(str_pad))) {
        LOG_WARN("Failed to get str_text", K(ret), K(pad_text));
      } else if (str_pad.length() == 0) {
        result_size = int_len;
      } else if (OB_FAIL(get_padding_info_mysql(result_type.get_collation_type(),
                     str_text,
                     int_len,
                     str_pad,
                     max_result_size,
                     repeat_count,
                     prefix_size,
                     result_size))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(int_len), K(str_pad), K(max_result_size));
      } else {
        result_size = str_text.length() + str_pad.length() * repeat_count + prefix_size;
      }
    }
  }
  if (result_size > max_result_size) {
    result_size = max_result_size;
  }
  return ret;
}

// See: ObExprBaseLRpad::calc_oracle
int ObExprBaseLRpad::calc_type_length_oracle(
    const ObExprResType& result_type, const ObObj& text, const ObObj& pad_text, const ObObj& len, int64_t& result_size)
{
  int ret = OB_SUCCESS;
  const int64_t max_result_size = result_type.is_lob() ? OB_MAX_LONGTEXT_LENGTH : OB_MAX_ORACLE_VARCHAR_LENGTH;
  int64_t width = 0;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  bool pad_space = false;
  int64_t text_width = 0;
  ObString str_text;
  ObString str_pad;
  ObString space_str = ObCharsetUtils::get_const_str(result_type.get_collation_type(), ' ');

  ObExprCtx expr_ctx;
  ObArenaAllocator allocator(common::ObModIds::OB_SQL_EXPR_CALC);
  expr_ctx.calc_buf_ = &allocator;
  result_size = max_result_size;
  /* get length */
  if (OB_FAIL(ObExprUtil::get_trunc_int64(len, expr_ctx, width))) {
    LOG_WARN("get_trunc_int64 failed and ignored", K(ret));
    ret = OB_SUCCESS;
  } else if (width < 0) {
    // do nothing
  } else {
    // both_const_str if true means: a and b are const in rpad(a, count, b)
    bool both_const_str = false;
    OX(both_const_str = ob_is_string_type(text.get_type()) && ob_is_string_type(pad_text.get_type()));
    if (OB_FAIL(ret)) {
    } else if (false == both_const_str) {
      // when not const, 4 times length in Oracle
      result_size = LS_CHAR == result_type.get_length_semantics() ? width : width * 4;
    } else {
      if (OB_FAIL(text.get_string(str_text))) {
        LOG_WARN("Failed to get str_text", K(ret), K(text));
      } else if (OB_FAIL(ObCharset::display_len(result_type.get_collation_type(), str_text, text_width))) {
        LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
      } else if (text_width == width) {
        result_size = width;
      } else if (text_width > width) {
        // substr
        int64_t total_width = 0;
        pad_space = true;
        if (OB_FAIL(ObCharset::max_display_width_charpos(result_type.get_collation_type(),
                str_text.ptr(),
                str_text.length(),
                width,
                prefix_size,
                &total_width))) {
        } else {
          pad_space = (total_width != width);
          result_size = prefix_size + (pad_space ? space_str.length() : 0);
        }
      } else if (OB_FAIL(pad_text.get_string(str_pad))) {
        LOG_WARN("Failed to get str_text", K(ret), K(pad_text));
      } else if (OB_FAIL(get_padding_info_oracle(result_type.get_collation_type(),
                     str_text,
                     width,
                     str_pad,
                     max_result_size,
                     repeat_count,
                     prefix_size,
                     pad_space))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(width), K(str_pad), K(max_result_size));
      } else {
        ObCollationType cs_type = result_type.get_collation_type();
        if (LS_CHAR != result_type.get_length_semantics()) {
          result_size =
              str_text.length() + str_pad.length() * repeat_count + prefix_size + (pad_space ? space_str.length() : 0);
        } else {
          result_size = ObCharset::strlen_char(cs_type, str_text.ptr(), str_text.length()) +
                        ObCharset::strlen_char(cs_type, str_pad.ptr(), str_pad.length()) * repeat_count + prefix_size +
                        (pad_space ? space_str.length() : 0);
        }
      }
    }
  }
  if (result_size > max_result_size) {
    result_size = max_result_size;
  }
  return ret;
}

int ObExprBaseLRpad::calc_type(
    ObExprResType& type, ObExprResType& text, ObExprResType& len, ObExprResType* pad_text, ObExprTypeCtx& type_ctx)
{
  int ret = OB_SUCCESS;
  ObObjType text_type = ObNullType;
  ObObjType len_type = ObNullType;
  const bool is_oracle_mode = share::is_oracle_mode();
  int64_t max_len = OB_MAX_VARCHAR_LENGTH;
  int64_t text_len = text.get_length();
  if (is_oracle_mode) {
    len_type = ObNumberType;

    if (text.is_nstring()) {
      text_type = ObNVarchar2Type;
      max_len = OB_MAX_ORACLE_VARCHAR_LENGTH;
    } else if (!text.is_lob()) {
      text_type = ObVarcharType;
      max_len = OB_MAX_ORACLE_VARCHAR_LENGTH;
    } else if (text.is_blob()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("Blob type in LRpad not supported", K(ret), K(text.get_type()));
    } else {
      text_type = ObLongTextType;
      max_len = OB_MAX_LONGTEXT_LENGTH;
    }
  } else if (share::is_mysql_mode()) {
    len_type = ObIntType;
    text_type = ObVarcharType;
    max_len = OB_MAX_VARCHAR_LENGTH;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error compat mode", K(ret));
  }

  const ObSQLSessionInfo* session = type_ctx.get_session();
  CK(OB_NOT_NULL(session));

  if (OB_SUCC(ret)) {
    ObObj length_obj = len.get_param();
    ObObj text_obj = text.get_param();
    ObObj pad_obj;
    ObString default_pad_str = ObString(" ");  // default ' '
    type.set_length(static_cast<ObLength>(max_len));
    len.set_calc_type(len_type);
    if (NULL != pad_text) {
      if (is_mysql_mode()) {
        pad_obj = pad_text->get_param();
        type.set_type(text_type);
        text.set_calc_type(text_type);
        pad_text->set_calc_type(text_type);
        ObSEArray<ObExprResType, 2> types;
        OZ(types.push_back(text));
        OZ(types.push_back(*pad_text));
        OZ(aggregate_charsets_for_string_result(type, &types.at(0), 2, type_ctx.get_coll_type()));
        OX(text.set_calc_collation_type(type.get_collation_type()));
        OX(pad_text->set_calc_collation_type(type.get_collation_type()));
      } else {
        OX(pad_obj = pad_text->get_param());
        ObSEArray<ObExprResType*, 1, ObNullAllocator> types;
        OZ(types.push_back(&text));
        OZ(aggregate_string_type_and_charset_oracle(*session, types, type, true));
        OX(text.set_calc_meta(type));
        OX(pad_text->set_calc_meta(type));
      }
    } else {
      type.set_type(text_type);
      text.set_calc_type(text_type);
      pad_obj.set_string(ObVarcharType, default_pad_str);
      pad_obj.set_collation_type(ObCharset::get_system_collation());
      if (text.is_string_type()) {
        type.set_collation_type(text.get_collation_type());
        type.set_collation_level(text.get_collation_level());
      } else {
        type.set_collation_type(get_default_collation_type(type.get_type(), *type_ctx.get_session()));
        type.set_collation_level(CS_LEVEL_IMPLICIT);
      }
      text.set_calc_collation_type(type.get_collation_type());
    }

    const int64_t buf_len = pad_obj.is_character_type() ? pad_obj.get_string_len() * 4 : 0;
    char buf[buf_len];
    if (OB_SUCC(ret) && share::is_oracle_mode() && !pad_obj.is_null_oracle() && pad_obj.is_character_type()) {
      ObDataBuffer data_buf(buf, buf_len);
      if (OB_FAIL(convert_result_collation(type, pad_obj, &data_buf))) {
        LOG_WARN("fail to convert result_collation", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_NOT_NULL(type_ctx.get_session()) && is_oracle_mode) {
        if (type.is_nvarchar2()) {
          type.set_length_semantics(LS_CHAR);
        } else {
          const ObLengthSemantics default_length_semantics =
              (OB_NOT_NULL(type_ctx.get_session()) ? type_ctx.get_session()->get_actual_nls_length_semantics()
                                                   : LS_BYTE);
          type.set_length_semantics(default_length_semantics);
        }
      }
      if (!length_obj.is_null()) {
        if (is_oracle_mode && OB_FAIL(calc_type_length_oracle(type, text_obj, pad_obj, length_obj, text_len))) {
          LOG_WARN("failed to calc result type length oracle mode", K(ret));
        } else if (!is_oracle_mode && OB_FAIL(calc_type_length_mysql(
                                          type, text_obj, pad_obj, length_obj, type_ctx.get_session(), text_len))) {
          LOG_WARN("failed to calc result type length mysql mode", K(ret));
        }
      } else {
        text_len = max_len;
      }
      text_len = (text_len > max_len) ? max_len : text_len;
      type.set_length(static_cast<ObLength>(text_len));

      LOG_DEBUG("ObExprBaseLRpad::calc_type()",
          K(ret),
          K(text),
          K(text_obj),
          K(pad_obj),
          K(len),
          K(length_obj),
          KP(pad_text),
          K(type),
          K(text_len),
          K(max_len),
          K(type.get_length_semantics()));
    }
  }
  return ret;
}

int ObExprBaseLRpad::padding(LRpadType type, const ObCollationType coll_type, const char* text,
    const int64_t& text_size, const char* pad, const int64_t& pad_size, const int64_t& prefix_size,
    const int64_t& repeat_count,
    const bool& pad_space,  // for oracle
    ObIAllocator* allocator, char*& result, int64_t& size)
{
  int ret = OB_SUCCESS;
  ObString space_str = ObCharsetUtils::get_const_str(coll_type, ' ');
  // start pos
  char* text_start_pos = NULL;
  char* pad_start_pos = NULL;
  char* sp_start_pos = NULL;

  size = text_size + pad_size * repeat_count + prefix_size + (pad_space ? space_str.length() : 0);
  if (OB_UNLIKELY(size <= 0) || OB_UNLIKELY(repeat_count < 0) || OB_UNLIKELY(pad_size <= 0) ||
      OB_UNLIKELY(prefix_size >= pad_size) || (OB_ISNULL(text) && text_size != 0) || OB_ISNULL(pad) ||
      OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Wrong param",
        K(ret),
        K(size),
        K(repeat_count),
        K(pad_size),
        K(prefix_size),
        K(text),
        K(text_size),
        K(pad),
        K(allocator));
  } else if (OB_ISNULL(result = static_cast<char*>(allocator->alloc(size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc", K(ret));
  } else if (type == LPAD_TYPE) {
    // lpad: [sp] + padtext * t + padprefix + text
    if (pad_space) {
      sp_start_pos = result;
      pad_start_pos = result + space_str.length();
    } else {
      pad_start_pos = result;
    }
    text_start_pos = pad_start_pos + (pad_size * repeat_count + prefix_size);
  } else if (type == RPAD_TYPE) {
    // rpad: text + padtext * t + padprefix + [sp]
    text_start_pos = result;
    pad_start_pos = text_start_pos + text_size;
    if (pad_space) {
      sp_start_pos = pad_start_pos + (pad_size * repeat_count + prefix_size);
    }
  }

  if (OB_SUCC(ret)) {
    // place pad string
    for (int64_t i = 0; i < repeat_count; i++) {
      MEMCPY(pad_start_pos + i * pad_size, pad, pad_size);
    }

    // place pad string prefix
    MEMCPY(pad_start_pos + repeat_count * pad_size, pad, prefix_size);

    // place text string
    MEMCPY(text_start_pos, text, text_size);
    if (pad_space) {
      MEMCPY(sp_start_pos, space_str.ptr(), space_str.length());
    }
  }
  return ret;
}

int ObExprBaseLRpad::calc(const LRpadType type, const ObObj& text, const ObObj& len, const ObObj& pad_text,
    ObExprCtx& expr_ctx, ObObj& result) const
{
  int ret = OB_SUCCESS;
  if (share::is_mysql_mode()) {
    if (OB_FAIL(
            calc_mysql(type, result_type_, text, len, pad_text, expr_ctx.my_session_, expr_ctx.calc_buf_, result))) {
      LOG_WARN("Failed to calc mysql",
          K(type),
          K(text),
          K(len),
          K(pad_text),
          K(expr_ctx.my_session_),
          K(expr_ctx.calc_buf_));
    }
  } else if (share::is_oracle_mode()) {
    if (OB_FAIL(calc_oracle(type, result_type_, text, len, pad_text, expr_ctx, result))) {
      LOG_WARN("Failed to calc oracle", K(type), K(text), K(len), K(pad_text), K(expr_ctx.calc_buf_));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error compat mode", K(ret));
  }
  return ret;
}
/* common util END }}} */

/* mysql util {{{2 */
int ObExprBaseLRpad::calc_mysql(const LRpadType type, const ObExprResType result_type, const ObObj& text,
    const ObObj& len, const ObObj& pad_text, const ObSQLSessionInfo* session, ObIAllocator* allocator, ObObj& result)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = -1;
  int64_t int_len = 0;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;

  int64_t text_len = 0;

  ObString str_text;
  ObString str_pad;

  char* result_ptr = NULL;
  int64_t result_size = 0;

  if (OB_ISNULL(session) || OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null session or allocator in Lpad function", K(ret), K(session), K(allocator));
  } else if (OB_FAIL(session->get_max_allowed_packet(max_result_size))) {
    if (OB_ENTRY_NOT_EXIST == ret) {  // for compatibility with server before 1470
      ret = OB_SUCCESS;
      max_result_size = OB_MAX_VARCHAR_LENGTH;
    } else {
      LOG_WARN("Failed to get max allow packet size", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (text.is_null() || len.is_null() || pad_text.is_null()) {
    result.set_null();
  } else {
    TYPE_CHECK(len, ObIntType);

    if (OB_FAIL(len.get_int(int_len))) {
      LOG_WARN("Failed to get len", K(ret), K(len));
    } else if (int_len < 0) {
      result.set_null();
    } else if (int_len == 0) {
      result.set_varchar(ObString::make_empty_string());
      result.set_collation(result_type);
    } else if (OB_FAIL(text.get_string(str_text))) {
      LOG_WARN("Failed to get str_text", K(ret), K(text));
    } else if (OB_FAIL(pad_text.get_string(str_pad))) {
      LOG_WARN("Failed to get str_text", K(ret), K(pad_text));
    } else if (FALSE_IT(
                   text_len = ObCharset::strlen_char(
                       result_type.get_collation_type(), const_cast<const char*>(str_text.ptr()), str_text.length()))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if (text_len >= int_len) {
      // only substr needed
      result_size = ObCharset::charpos(result_type.get_collation_type(), str_text.ptr(), str_text.length(), int_len);
      result_ptr = static_cast<char*>(allocator->alloc(result_size));
      if (OB_ISNULL(result_ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Failed to alloc", K(ret));
      } else {
        MEMCPY(result_ptr, str_text.ptr(), result_size);
        result.set_varchar(result_ptr, static_cast<ObString::obstr_size_t>(result_size));
        result.set_collation(result_type);
      }
    } else if (str_pad.length() == 0) {
      result.set_null();
    } else if (OB_FAIL(get_padding_info_mysql(result_type.get_collation_type(),
                   str_text,
                   int_len,
                   str_pad,
                   max_result_size,
                   repeat_count,
                   prefix_size,
                   result_size))) {
      LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(int_len), K(str_pad), K(max_result_size));
    } else if (result_size > max_result_size) {
      result.set_null();
      if (type == RPAD_TYPE) {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "rpad", static_cast<int>(max_result_size));
      } else {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "lpad", static_cast<int>(max_result_size));
      }
    } else if (OB_FAIL(padding(type,
                   result_type.get_collation_type(),
                   str_text.ptr(),
                   str_text.length(),
                   str_pad.ptr(),
                   str_pad.length(),
                   prefix_size,
                   repeat_count,
                   false,
                   allocator,
                   result_ptr,
                   result_size))) {
      LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count));
    } else {
      result.set_string(result_type.get_type(), result_ptr, static_cast<ObString::obstr_size_t>(result_size));
      result.set_collation(result_type);
    }
  }
  return ret;
}

int ObExprBaseLRpad::get_padding_info_mysql(const ObCollationType& cs, const ObString& str_text, const int64_t& len,
    const ObString& str_padtext, const int64_t max_result_size, int64_t& repeat_count, int64_t& prefix_size,
    int64_t& size)
{
  // lpad: [sp] + padtext * t + padprefix + text
  // rpad: text + padtext * t + padprefix + [sp]
  int ret = OB_SUCCESS;
  int64_t text_size = str_text.length();
  int64_t pad_size = str_padtext.length();

  // GOAL: get repeat_count, prefix_size and pad space.
  int64_t text_len = ObCharset::strlen_char(cs, const_cast<const char*>(str_text.ptr()), str_text.length());
  int64_t pad_len = ObCharset::strlen_char(cs, const_cast<const char*>(str_padtext.ptr()), str_padtext.length());

  if (OB_UNLIKELY(len <= text_len) || OB_UNLIKELY(len <= 0) || OB_UNLIKELY(pad_len <= 0) ||
      OB_UNLIKELY(pad_size <= 0)) {
    // this should been resolve outside
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong len", K(ret), K(len), K(text_len));
  } else {
    repeat_count = std::min((len - text_len) / pad_len, (max_result_size - text_size) / pad_size);
    int64_t remain_len = len - (text_len + pad_len * repeat_count);
    prefix_size = ObCharset::charpos(cs, const_cast<const char*>(str_padtext.ptr()), str_padtext.length(), remain_len);

    size = text_size + pad_size * repeat_count + prefix_size;
  }
  return ret;
}

// for engine 3.0
int ObExprBaseLRpad::calc_mysql_pad_expr(const ObExpr& expr, ObEvalCtx& ctx, LRpadType pad_type, ObDatum& res)
{
  int ret = OB_SUCCESS;
  ObDatum* text = NULL;
  ObDatum* len = NULL;
  ObDatum* pad_text = NULL;
  if (OB_UNLIKELY(3 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("arg cnt must be 3", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, text, len, pad_text))) {
    LOG_WARN("eval param value failed", K(ret));
  } else {
    const ObSQLSessionInfo* session = ctx.exec_ctx_.get_my_session();
    ObExprStrResAlloc res_alloc(expr, ctx);  // make sure alloc() is called only once
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL", K(ret));
    } else if (OB_FAIL(calc_mysql(pad_type, expr, *text, *len, *pad_text, *session, res_alloc, res))) {
      LOG_WARN("calc_mysql failed", K(ret));
    }
  }
  return ret;
}

int ObExprBaseLRpad::calc_mysql(const LRpadType pad_type, const ObExpr& expr, const ObDatum& text, const ObDatum& len,
    const ObDatum& pad_text, const ObSQLSessionInfo& session, ObIAllocator& res_alloc, ObDatum& res)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = -1;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  int64_t text_len = 0;

  char* result_ptr = NULL;
  int64_t result_size = 0;
  const ObCollationType cs_type = expr.datum_meta_.cs_type_;

  if (OB_FAIL(session.get_max_allowed_packet(max_result_size))) {
    if (OB_ENTRY_NOT_EXIST == ret) {  // for compatibility with server before 1470
      ret = OB_SUCCESS;
      max_result_size = OB_MAX_VARCHAR_LENGTH;
    } else {
      LOG_WARN("Failed to get max allow packet size", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (text.is_null() || len.is_null() || pad_text.is_null()) {
    res.set_null();
  } else {
    int64_t int_len = len.get_int();
    const ObString& str_text = text.get_string();
    const ObString& str_pad = pad_text.get_string();
    if (int_len < 0) {
      res.set_null();
    } else if (int_len == 0) {
      res.set_string(ObString::make_empty_string());
    } else if (FALSE_IT(text_len = ObCharset::strlen_char(
                            cs_type, const_cast<const char*>(str_text.ptr()), str_text.length()))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if (text_len >= int_len) {
      // only substr needed
      result_size = ObCharset::charpos(cs_type, str_text.ptr(), str_text.length(), int_len);
      res.set_string(ObString(result_size, str_text.ptr()));
    } else if (str_pad.length() == 0) {
      res.set_null();
    } else if (OB_FAIL(get_padding_info_mysql(
                   cs_type, str_text, int_len, str_pad, max_result_size, repeat_count, prefix_size, result_size))) {
      LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(int_len), K(str_pad), K(max_result_size));
    } else if (result_size > max_result_size) {
      res.set_null();
      if (pad_type == RPAD_TYPE) {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "rpad", static_cast<int>(max_result_size));
      } else {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "lpad", static_cast<int>(max_result_size));
      }
    } else if (OB_FAIL(padding(pad_type,
                   cs_type,
                   str_text.ptr(),
                   str_text.length(),
                   str_pad.ptr(),
                   str_pad.length(),
                   prefix_size,
                   repeat_count,
                   false,
                   &res_alloc,
                   result_ptr,
                   result_size))) {
      LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count));
    } else {
      if (NULL == result_ptr || 0 == result_size) {
        res.set_null();
      } else {
        res.set_string(result_ptr, result_size);
      }
    }
  }
  return ret;
}
/* mysql util END }}} */

/* oracle util {{{2 */
int ObExprBaseLRpad::calc_oracle(const LRpadType type, const ObExprResType result_type, const ObObj& text,
    const ObObj& len, const ObObj& pad_text, common::ObExprCtx& expr_ctx, ObObj& result)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = result_type.is_lob() ? OB_MAX_LONGTEXT_LENGTH : OB_MAX_ORACLE_VARCHAR_LENGTH;
  int64_t width = 0;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  bool pad_space = false;

  int64_t text_width = 0;

  ObString str_text;
  ObString str_pad;

  char* result_ptr = NULL;
  int64_t result_size = 0;

  ObIAllocator* allocator = expr_ctx.calc_buf_;
  int64_t tmp_pad_len = 0;

  if (text.is_null_oracle() || len.is_null_oracle() || pad_text.is_null_oracle()) {
    result.set_null();
  } else if (OB_UNLIKELY(!text.is_string_type())                                    // clob or varchar
             || OB_UNLIKELY(!len.is_number())                                       // number
             || OB_UNLIKELY(!pad_text.is_string_type()) || OB_ISNULL(allocator)) {  // clob or varchar
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Wrong param", K(ret), K(text.get_type()), K(len.get_type()), K(pad_text.get_type()), K(allocator));
  } else if (text.is_blob()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Blob type in LRpad not supported", K(ret), K(text.get_type()));
  } else if (OB_FAIL(ObExprUtil::get_trunc_int64(len, expr_ctx, tmp_pad_len))) {
    LOG_WARN("fail to get pad_len", K(ret), K(len));
  } else if (text.is_clob() && pad_text.is_clob() && (0 == pad_text.val_len_) && (text.val_len_ <= tmp_pad_len)) {
    result.set_string(result_type.get_type(), text.get_string());
    result.set_collation(result_type);
  } else {
    /* get length */
    number::ObNumber len_num;
    int64_t decimal_parts = -1;
    if (OB_FAIL(len.get_number(len_num))) {
      LOG_WARN("failed to get length", K(ret));
    } else if (len_num.is_negative()) {
      width = -1;
    } else if (!len_num.is_int_parts_valid_int64(width, decimal_parts)) {
      width = UINT32_MAX;
    }

    if (OB_FAIL(ret)) {
    } else if (width <= 0) {
      result.set_null();
    } else if (OB_FAIL(text.get_string(str_text))) {
      LOG_WARN("Failed to get str_text", K(ret), K(text));
    } else if (OB_FAIL(pad_text.get_string(str_pad))) {
      LOG_WARN("Failed to get str_text", K(ret), K(pad_text));
    } else if (OB_FAIL(ObCharset::display_len(result_type.get_collation_type(), str_text, text_width))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else {
      if (text_width == width) {
        result = text;
        result_ptr = const_cast<char*>(text.get_string_ptr());
        result_size = text.get_string_len();
      } else if (text_width > width) {
        // substr
        int64_t total_width = 0;
        if (OB_FAIL(ObCharset::max_display_width_charpos(result_type.get_collation_type(),
                str_text.ptr(),
                str_text.length(),
                width,
                prefix_size,
                &total_width))) {
          LOG_WARN("Failed to get max display width", K(ret));
        } else if (OB_FAIL(padding(type,
                       result_type.get_collation_type(),
                       "",
                       0,
                       str_text.ptr(),
                       str_text.length(),
                       prefix_size,
                       0,
                       (total_width != width),
                       allocator,
                       result_ptr,
                       result_size))) {
          LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count), K(pad_space));
        }
      } else if (OB_FAIL(get_padding_info_oracle(result_type.get_collation_type(),
                     str_text,
                     width,
                     str_pad,
                     max_result_size,
                     repeat_count,
                     prefix_size,
                     pad_space))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(width), K(str_pad), K(max_result_size));
      } else if (OB_FAIL(padding(type,
                     result_type.get_collation_type(),
                     str_text.ptr(),
                     str_text.length(),
                     str_pad.ptr(),
                     str_pad.length(),
                     prefix_size,
                     repeat_count,
                     pad_space,
                     allocator,
                     result_ptr,
                     result_size))) {
        LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count), K(pad_space));
      }

      if (OB_SUCC(ret)) {
        result.set_string(result_type.get_type(), result_ptr, static_cast<ObString::obstr_size_t>(result_size));
        result.set_collation(result_type);
      }
    }
  }
  return ret;
}

int ObExprBaseLRpad::get_padding_info_oracle(const ObCollationType cs, const ObString& str_text, const int64_t& width,
    const ObString& str_padtext, const int64_t max_result_size, int64_t& repeat_count, int64_t& prefix_size,
    bool& pad_space)
{
  // lpad: [sp] + padtext * t + padprefix + text
  // rpad: text + padtext * t + padprefix + [sp]
  int ret = OB_SUCCESS;
  int64_t text_size = str_text.length();
  int64_t pad_size = str_padtext.length();

  int64_t text_width = 0;
  int64_t pad_width = 0;
  pad_space = false;

  // GOAL: get repeat_count, prefix_size and pad space.
  if (OB_FAIL(ObCharset::display_len(cs, str_text, text_width))) {
    LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
  } else if (OB_FAIL(ObCharset::display_len(cs, str_padtext, pad_width))) {
    LOG_WARN("Failed to get displayed length", K(ret), K(str_padtext));
  } else if (OB_UNLIKELY(width <= text_width) || OB_UNLIKELY(width <= 0) || OB_UNLIKELY(pad_size <= 0) ||
             OB_UNLIKELY(pad_width <= 0)) {
    // this should been resolve outside
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong width", K(ret), K(width), K(text_width), K(pad_size), K(pad_width));
  } else {
    repeat_count = std::min((width - text_width) / pad_width, (max_result_size - text_size) / pad_size);
    int64_t remain_width = width - (text_width + repeat_count * pad_width);
    int64_t remain_size = max_result_size - (text_size + repeat_count * pad_size);
    int64_t total_width = 0;
    LOG_DEBUG("calc pad",
        K(remain_width),
        K(width),
        K(text_width),
        K(pad_width),
        K(max_result_size),
        K(text_size),
        K(pad_size),
        K(ret),
        K(remain_size));

    if (remain_width > 0 && remain_size > 0) {
      if (OB_FAIL(ObCharset::max_display_width_charpos(
              cs, str_padtext.ptr(), std::min(remain_size, pad_size), remain_width, prefix_size, &total_width))) {
        LOG_WARN("Failed to get max display width", K(ret), K(str_text), K(remain_width));
      } else if (remain_width != total_width && remain_size != prefix_size) {
        pad_space = true;
      }
    }
  }
  return ret;
}

// for engine 3.0
int ObExprBaseLRpad::calc_oracle_pad_expr(const ObExpr& expr, ObEvalCtx& ctx, LRpadType pad_type, ObDatum& res)
{
  int ret = OB_SUCCESS;
  ObDatum* text = NULL;
  ObDatum* len = NULL;
  ObDatum* pad_text = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, text, len, pad_text))) {
    LOG_WARN("eval param failed", K(ret));
  } else {
    ObExprStrResAlloc res_alloc(expr, ctx);
    if (3 == expr.arg_cnt_) {
      if (OB_ISNULL(text) || OB_ISNULL(len) || OB_ISNULL(pad_text)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected datum", K(ret), KP(text), KP(len), KP(pad_text));
      } else if (OB_FAIL(calc_oracle(pad_type, expr, *text, *len, *pad_text, res_alloc, res))) {
        LOG_WARN("calc pad failed", K(ret));
      }
    } else if (2 == expr.arg_cnt_) {
      ObCollationType in_coll = ObCharset::get_system_collation();
      ObCollationType out_coll = expr.datum_meta_.cs_type_;
      ObIAllocator& calc_alloc = ctx.get_reset_tmp_alloc();
      ObString pad_str_utf8(1, " ");
      ObString pad_str;
      ObDatum tmp_pad_text;
      tmp_pad_text.set_string(pad_str);
      if (OB_ISNULL(text) || OB_ISNULL(len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected datum", K(ret), KP(text), KP(len));
      } else if (OB_FAIL(ObExprUtil::convert_string_collation(pad_str_utf8, in_coll, pad_str, out_coll, calc_alloc))) {
        LOG_WARN("convert collation failed", K(ret), K(in_coll), K(pad_str), K(out_coll));
      } else if (OB_UNLIKELY(pad_str.empty())) {
        LOG_WARN("unexpected pad_str after convert collation", K(ret), K(pad_str));
      } else {
        tmp_pad_text.set_string(pad_str);
        if (OB_FAIL(calc_oracle(pad_type, expr, *text, *len, tmp_pad_text, res_alloc, res))) {
          LOG_WARN("calc pad failed", K(ret));
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid arg cnt", K(ret), K(expr.arg_cnt_));
    }
  }

  return ret;
}

int ObExprBaseLRpad::calc_oracle(LRpadType pad_type, const ObExpr& expr, const ObDatum& text, const ObDatum& len,
    const ObDatum& pad_text, ObIAllocator& res_alloc, ObDatum& res)
{
  int ret = OB_SUCCESS;
  if (text.is_null() || len.is_null() || pad_text.is_null()) {
    res.set_null();
  } else {
    int64_t width = 0;
    int64_t repeat_count = 0;
    int64_t prefix_size = 0;
    bool pad_space = false;
    int64_t text_width = 0;
    const ObString& str_text = text.get_string();
    const ObString& str_pad = pad_text.get_string();
    int64_t max_result_size =
        ob_is_text_tc(expr.datum_meta_.type_) ? OB_MAX_LONGTEXT_LENGTH : OB_MAX_ORACLE_VARCHAR_LENGTH;
    const ObCollationType cs_type = expr.datum_meta_.cs_type_;

    number::ObNumber len_num(len.get_number());
    int64_t decimal_parts = -1;
    if (len_num.is_negative()) {
      width = -1;
    } else if (!len_num.is_int_parts_valid_int64(width, decimal_parts)) {
      width = UINT32_MAX;
    }
    if (width <= 0) {
      res.set_null();
    } else if (OB_FAIL(ObCharset::display_len(cs_type, str_text, text_width))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if ((3 == expr.arg_cnt_) && expr.args_[0]->datum_meta_.is_clob() && (0 == str_pad.length()) &&
               (text_width <= width)) {
      res.set_datum(text);
    } else if (text_width == width) {
      res.set_datum(text);
    } else {
      char* result_ptr = NULL;
      int64_t result_size = 0;
      if (text_width > width) {
        // substr
        int64_t total_width = 0;
        if (OB_FAIL(ObCharset::max_display_width_charpos(
                cs_type, str_text.ptr(), str_text.length(), width, prefix_size, &total_width))) {
          LOG_WARN("Failed to get max display width", K(ret));
        } else if (OB_FAIL(padding(pad_type,
                       cs_type,
                       "",
                       0,
                       str_text.ptr(),
                       str_text.length(),
                       prefix_size,
                       0,
                       (total_width != width),
                       &res_alloc,
                       result_ptr,
                       result_size))) {
          LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count), K(pad_space));
        }
      } else if (OB_FAIL(get_padding_info_oracle(
                     cs_type, str_text, width, str_pad, max_result_size, repeat_count, prefix_size, pad_space))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(width), K(str_pad), K(max_result_size));
      } else if (OB_FAIL(padding(pad_type,
                     cs_type,
                     str_text.ptr(),
                     str_text.length(),
                     str_pad.ptr(),
                     str_pad.length(),
                     prefix_size,
                     repeat_count,
                     pad_space,
                     &res_alloc,
                     result_ptr,
                     result_size))) {
        LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count), K(pad_space));
      }

      if (OB_SUCC(ret)) {
        if (NULL == result_ptr || 0 == result_size) {
          res.set_null();
        } else {
          res.set_string(result_ptr, result_size);
        }
      }
    }
  }
  return ret;
}
/* oracle util END }}} */
/* ObExprBaseLRpad END }}} */

/* ObExprLpad {{{1 */
ObExprLpad::ObExprLpad(ObIAllocator& alloc) : ObExprBaseLRpad(alloc, T_FUN_SYS_LPAD, N_LPAD, 3)
{}

ObExprLpad::~ObExprLpad()
{}

int ObExprLpad::calc_result_type3(ObExprResType& type, ObExprResType& text, ObExprResType& len, ObExprResType& pad_text,
    ObExprTypeCtx& type_ctx) const
{
  return ObExprBaseLRpad::calc_type(type, text, len, &pad_text, type_ctx);
}

int ObExprLpad::calc_result3(
    ObObj& result, const ObObj& text, const ObObj& len, const ObObj& pad_text, ObExprCtx& expr_ctx) const
{
  return ObExprBaseLRpad::calc(LPAD_TYPE, text, len, pad_text, expr_ctx, result);
}

int ObExprLpad::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_mysql_lpad_expr;
  return ret;
}

int ObExprLpad::calc_mysql_lpad_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res)
{
  return calc_mysql_pad_expr(expr, ctx, LPAD_TYPE, res);
}
/* ObExprLpad END }}} */

/* ObExprRpad {{{1 */
ObExprRpad::ObExprRpad(ObIAllocator& alloc) : ObExprBaseLRpad(alloc, T_FUN_SYS_RPAD, N_RPAD, 3)
{}

ObExprRpad::ObExprRpad(ObIAllocator& alloc, ObExprOperatorType type, const char* name)
    : ObExprBaseLRpad(alloc, type, name, 3)
{}

ObExprRpad::~ObExprRpad()
{}

int ObExprRpad::calc_result_type3(ObExprResType& type, ObExprResType& text, ObExprResType& len, ObExprResType& pad_text,
    ObExprTypeCtx& type_ctx) const
{
  return ObExprBaseLRpad::calc_type(type, text, len, &pad_text, type_ctx);
}

int ObExprRpad::calc_result3(
    ObObj& result, const ObObj& text, const ObObj& len, const ObObj& pad_text, ObExprCtx& expr_ctx) const
{
  return ObExprBaseLRpad::calc(RPAD_TYPE, text, len, pad_text, expr_ctx, result);
}

int ObExprRpad::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_mysql_rpad_expr;
  return ret;
}

int ObExprRpad::calc_mysql_rpad_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res)
{
  return calc_mysql_pad_expr(expr, ctx, RPAD_TYPE, res);
}
/* ObExprRpad END }}} */

/* ObExprLpadOracle {{{1 */
ObExprOracleLpad::ObExprOracleLpad(ObIAllocator& alloc) : ObExprBaseLRpad(alloc, T_FUN_SYS_LPAD, N_LPAD, TWO_OR_THREE)
{}

ObExprOracleLpad::~ObExprOracleLpad()
{}

int ObExprOracleLpad::calc_result_typeN(
    ObExprResType& type, ObExprResType* types_array, int64_t param_num, ObExprTypeCtx& type_ctx) const
{
  int ret = OB_SUCCESS;
  if (param_num == 3) {
    if (OB_ISNULL(types_array) || OB_ISNULL(types_array + 1) || OB_ISNULL(types_array + 2)) {
      LOG_WARN("NULL param", K(ret), K(types_array[0]), K(types_array[1]), K(types_array[2]));
    } else if (OB_FAIL(ObExprBaseLRpad::calc_type(type, types_array[0], types_array[1], types_array + 2, type_ctx))) {
      LOG_WARN("Failed to calc_type", K(ret));
    }
  } else if (param_num == 2) {
    if (OB_ISNULL(types_array) || OB_ISNULL(types_array + 1)) {
      LOG_WARN("NULL param", K(ret), K(types_array[0]), K(types_array[1]));
    } else if (OB_FAIL(ObExprBaseLRpad::calc_type(type, types_array[0], types_array[1], NULL, type_ctx))) {
      LOG_WARN("Failed to calc_type", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Wrong param num", K(ret), K(param_num));
  }
  return ret;
}

int ObExprOracleLpad::calc_resultN(ObObj& result, const ObObj* objs_array, int64_t param_num, ObExprCtx& expr_ctx) const
{
  int ret = OB_SUCCESS;
  if (param_num == 3) {
    if (OB_ISNULL(objs_array) || OB_ISNULL(objs_array + 1) || OB_ISNULL(objs_array + 2)) {
      LOG_WARN("NULL param", K(ret), K(objs_array[0]), K(objs_array[1]), K(objs_array[2]));
    } else if (OB_FAIL(
                   ObExprBaseLRpad::calc(LPAD_TYPE, objs_array[0], objs_array[1], objs_array[2], expr_ctx, result))) {
      LOG_WARN("Failed to calc", K(ret));
    }
  } else if (param_num == 2) {
    ObObj pad_text;
    pad_text.set_string(ObVarcharType, " ", 1L);
    pad_text.set_collation_type(ObCharset::get_system_collation());
    if (OB_ISNULL(objs_array) || OB_ISNULL(objs_array + 1)) {
      LOG_WARN("NULL param", K(ret), K(objs_array[0]), K(objs_array[1]));
    } else if (OB_FAIL(convert_result_collation(result_type_, pad_text, expr_ctx.calc_buf_))) {
      LOG_WARN("convert result collation failed", K(ret));
    } else if (OB_FAIL(ObExprBaseLRpad::calc(LPAD_TYPE, objs_array[0], objs_array[1], pad_text, expr_ctx, result))) {
      LOG_WARN("Failed to calc", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Wrong param num", K(ret), K(param_num));
  }
  return ret;
}

int ObExprOracleLpad::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_oracle_lpad_expr;
  return ret;
}

int ObExprOracleLpad::calc_oracle_lpad_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res)
{
  return calc_oracle_pad_expr(expr, ctx, LPAD_TYPE, res);
}
/* ObExprLpadOracle END }}} */

/* ObExprRpadOracle {{{1 */
ObExprOracleRpad::ObExprOracleRpad(ObIAllocator& alloc) : ObExprBaseLRpad(alloc, T_FUN_SYS_RPAD, N_RPAD, TWO_OR_THREE)
{}

ObExprOracleRpad::~ObExprOracleRpad()
{}

int ObExprOracleRpad::calc_result_typeN(
    ObExprResType& type, ObExprResType* types_array, int64_t param_num, ObExprTypeCtx& type_ctx) const
{
  int ret = OB_SUCCESS;
  if (param_num == 3) {
    if (OB_ISNULL(types_array) || OB_ISNULL(types_array + 1) || OB_ISNULL(types_array + 2)) {
      LOG_WARN("NULL param", K(ret), K(types_array[0]), K(types_array[1]), K(types_array[2]));
    } else if (OB_FAIL(ObExprBaseLRpad::calc_type(type, types_array[0], types_array[1], &(types_array[2]), type_ctx))) {
      LOG_WARN("Failed to calc_type", K(ret));
    }
  } else if (param_num == 2) {
    if (OB_ISNULL(types_array) || OB_ISNULL(types_array + 1)) {
      LOG_WARN("NULL param", K(ret), K(types_array[0]), K(types_array[1]));
    } else if (OB_FAIL(ObExprBaseLRpad::calc_type(type, types_array[0], types_array[1], NULL, type_ctx))) {
      LOG_WARN("Failed to calc_type", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Wrong param num", K(ret), K(param_num));
  }
  return ret;
}

int ObExprOracleRpad::calc_resultN(ObObj& result, const ObObj* objs_array, int64_t param_num, ObExprCtx& expr_ctx) const
{
  int ret = OB_SUCCESS;
  if (param_num == 3) {
    if (OB_ISNULL(objs_array) || OB_ISNULL(objs_array + 1) || OB_ISNULL(objs_array + 2)) {
      LOG_WARN("NULL param", K(ret), K(objs_array[0]), K(objs_array[1]), K(objs_array[2]));
    } else if (OB_FAIL(
                   ObExprBaseLRpad::calc(RPAD_TYPE, objs_array[0], objs_array[1], objs_array[2], expr_ctx, result))) {
      LOG_WARN("Failed to calc", K(ret));
    }
  } else if (param_num == 2) {
    ObObj pad_text;
    pad_text.set_string(ObVarcharType, " ", 1L);
    pad_text.set_collation_type(ObCharset::get_system_collation());
    if (OB_ISNULL(objs_array) || OB_ISNULL(objs_array + 1)) {
      LOG_WARN("NULL param", K(ret), K(objs_array[0]), K(objs_array[1]));
    } else if (OB_FAIL(convert_result_collation(result_type_, pad_text, expr_ctx.calc_buf_))) {
      LOG_WARN("convert result collation failed", K(ret));
    } else if (OB_FAIL(ObExprBaseLRpad::calc(RPAD_TYPE, objs_array[0], objs_array[1], pad_text, expr_ctx, result))) {
      LOG_WARN("Failed to calc", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("Wrong param num", K(ret), K(param_num));
  }
  return ret;
}

int ObExprOracleRpad::cg_expr(ObExprCGCtx& expr_cg_ctx, const ObRawExpr& raw_expr, ObExpr& rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_oracle_rpad_expr;
  return ret;
}

int ObExprOracleRpad::calc_oracle_rpad_expr(const ObExpr& expr, ObEvalCtx& ctx, ObDatum& res)
{
  return calc_oracle_pad_expr(expr, ctx, RPAD_TYPE, res);
}
/* ObExprRpadOracle END }}} */

}  // namespace sql
}  // namespace oceanbase
