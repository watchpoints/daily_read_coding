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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_INFO_FLAG_H_
#define OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_INFO_FLAG_H_
#include "lib/utility/ob_print_utils.h"
namespace oceanbase {
namespace sql {
// IS_XXX and CNT_XXX must be added in one-to-one correspondence
enum ObExprInfoFlag {
  IS_CONST = 0x0,  // const expression, e.g. 1.2, 'abc'
  IS_COLUMN,       // column expression, e.g. C1, T1.C1
  IS_PARAM,        // vaiable expression, e.g. ?
  IS_AGG,          // aggregate function, e.g. max, avg
  IS_FUNC,         // system function
  IS_IN,
  IS_NOT,
  IS_OR,
  IS_SUB_QUERY,
  IS_ALIAS,
  IS_CUR_TIME,
  IS_IS_EXPR,
  IS_CALCULABLE_EXPR,
  IS_VALUES,
  IS_DEFAULT,
  IS_USER_VARIABLE,
  IS_STATE_FUNC,
  IS_SET_OP,
  IS_LAST_INSERT_ID,
  IS_DOMAIN_INDEX_FUNC,
  IS_WINDOW_FUNC,
  IS_PRIOR,
  IS_RAND_FUNC,  // rand(const) or rand() is a random function , rand(some column) is not a random function
  IS_ROWNUM,
  IS_LEVEL,
  IS_CONNECT_BY_ISLEAF,
  IS_CONNECT_BY_ISCYCLE,
  IS_PSEUDO_COLUMN,
  IS_CONNECT_BY_ROOT,
  IS_SYS_CONNECT_BY_PATH,
  IS_OUTER_JOIN_SYMBOL,
  IS_SO_UDF_EXPR,  // expression contains user defined function(dll) expression. add prefix 'SO', means different with
                   // PL UDF.
  IS_SEQ_EXPR,
  IS_EXEC_PARAM,  // for opt to distinguish normal paramaterized const and exec-params
  IS_ENUM_OR_SET,
  IS_VOLATILE_CONST,  // the const expr may be altered by overwrite, non-const in execution.
  IS_ORA_ROWSCN_EXPR,
  IS_VAR_EXPR,
  IS_CONST_EXPR,   // expression contains calculable expression
  CNT_CONST_EXPR,  // IS_CONST_EXPR and CNT_CONST_EXPR at most one is true

  CNT_CONST,   // expression contains const
  CNT_COLUMN,  // expression contains column
  CNT_PARAM,   // expression contains variable
  CNT_AGG,     // expression contains aggregate function
  CNT_FUNC,    // expression contains system function
  CNT_IN,      // expression contains In expression
  CNT_NOT,     // expression contains NOT expression
  CNT_OR,      // expression contains OR expression
  CNT_SUB_QUERY,
  CNT_ALIAS,
  CNT_CUR_TIME,
  CNT_IS_EXPR,
  CNT_CALCULABLE_EXPR,
  CNT_VALUES,
  CNT_DEFAULT,
  CNT_USER_VARIABLE,
  CNT_STATE_FUNC,
  CNT_SET_OP,
  CNT_LAST_INSERT_ID,
  CNT_DOMAIN_INDEX_FUNC,
  CNT_WINDOW_FUNC,
  CNT_PRIOR,
  CNT_RAND_FUNC,
  CNT_ROWNUM,
  CNT_LEVEL,
  CNT_CONNECT_BY_ISLEAF,
  CNT_CONNECT_BY_ISCYCLE,
  CNT_PSEUDO_COLUMN,
  CNT_CONNECT_BY_ROOT,
  CNT_SYS_CONNECT_BY_PATH,
  CNT_OUTER_JOIN_SYMBOL,
  CNT_SO_UDF,  // expression contains user defined function(dll) expression. add prefix 'SO', means different with PL
               // UDF.
  CNT_SEQ_EXPR,
  CNT_EXEC_PARAM,
  CNT_ENUM_OR_SET,
  CNT_VOLATILE_CONST,
  CNT_ORA_ROWSCN_EXPR,
  CNT_VAR_EXPR,

  BE_USED,         // expression has been applied
  IS_SIMPLE_COND,  // column = const
  IS_RANGE_COND,   // column belongs to a range
  IS_JOIN_COND,    // column = column of different tables
  IS_COLUMNLIZED,  // for code generator, @see ObExprGenerator
  IS_ROOT,         // for code generator, @see ObExprGenerator
  IS_NEW_AGG_EXPR,
  IS_WITH_ALL,
  IS_WITH_ANY,
  IS_WITH_SUBQUERY,
  IS_INNER_ADDED_EXPR,
  IS_PL_ACCESS_IDX,
  IS_SELECT_REF,  // expr is refer to select item
  IS_SHARED_REF,
  IS_OP_OPERAND_IMPLICIT_CAST,  // is implicit cast added for operand casting (add in type deducing)
};

#define IS_INFO_MASK_BEGIN IS_CONST
#define IS_INFO_MASK_END IS_VAR_EXPR
#define CNT_INFO_MASK_BEGIN CNT_CONST
#define CNT_INFO_MASK_END CNT_VAR_EXPR
#define INHERIT_MASK_BEGIN CNT_CONST_EXPR
#define INHERIT_MASK_END CNT_VAR_EXPR

inline const char* get_expr_info_flag_str(const ObExprInfoFlag flag)
{
  const char* ret = "UNKNOWN";
  switch (flag) {
    case IS_CONST: {
      ret = "IS_CONST";
      break;
    }
    case IS_COLUMN: {
      ret = "IS_COLUMN";
      break;
    }
    case IS_PARAM: {
      ret = "IS_PARAM";
      break;
    }
    case IS_AGG: {
      ret = "IS_AGG";
      break;
    }
    case IS_FUNC: {
      ret = "IS_FUNC";
      break;
    }
    case IS_IN: {
      ret = "IS_IN";
      break;
    }
    case IS_NOT: {
      ret = "IS_NOT";
      break;
    }
    case IS_OR: {
      ret = "IS_OR";
      break;
    }
    case IS_SUB_QUERY: {
      ret = "IS_SUB_QUERY";
      break;
    }
    case IS_ALIAS: {
      ret = "IS_ALIAS";
      break;
    }
    case IS_CUR_TIME: {
      ret = "IS_CUR_TIME";
      break;
    }
    case IS_IS_EXPR: {
      ret = "IS_IS_EXPR";
      break;
    }
    case IS_CALCULABLE_EXPR: {
      ret = "IS_CALCULABLE_EXPR";
      break;
    }
    case IS_VALUES: {
      ret = "IS_VALUES";
      break;
    }
    case IS_DEFAULT: {
      ret = "IS_DEFAULT";
      break;
    }
    case IS_USER_VARIABLE: {
      ret = "IS_USER_VARIABLE";
      break;
    }
    case IS_STATE_FUNC: {
      ret = "IS_STATE_FUNC";
      break;
    }
    case IS_SET_OP: {
      ret = "IS_SET_OP";
      break;
    }
    case IS_LAST_INSERT_ID: {
      ret = "IS_LAST_INSERT_ID";
      break;
    }
    case IS_DOMAIN_INDEX_FUNC: {
      ret = "IS_DOMAIN_INDEX_FUNC";
      break;
    }
    case IS_WINDOW_FUNC: {
      ret = "IS_WINDOW_FUNC";
      break;
    }
    case IS_PRIOR: {
      ret = "IS_PRIOR";
      break;
    }
    case IS_RAND_FUNC: {
      ret = "IS_RAND_FUNC";
      break;
    }
    case IS_ROWNUM: {
      ret = "IS_ROWNUM";
      break;
    }
    case IS_LEVEL: {
      ret = "IS_LEVEL";
      break;
    }
    case IS_CONNECT_BY_ISLEAF: {
      ret = "IS_CONNECT_BY_ISLEAF";
      break;
    }
    case IS_CONNECT_BY_ISCYCLE: {
      ret = "IS_CONNECT_BY_ISCYCLE";
      break;
    }
    case IS_PSEUDO_COLUMN: {
      ret = "IS_PSEUDO_COLUMN";
      break;
    }
    case IS_CONNECT_BY_ROOT: {
      ret = "IS_CONNECT_BY_ROOT";
      break;
    }
    case IS_SYS_CONNECT_BY_PATH: {
      ret = "IS_SYS_CONNECT_BY_PATH";
      break;
    }
    case IS_OUTER_JOIN_SYMBOL: {
      ret = "IS_OUTER_JOIN_SYMBOL";
      break;
    }
    case IS_SO_UDF_EXPR: {
      ret = "IS_SO_UDF_EXPR";
      break;
    }
    case IS_SEQ_EXPR: {
      ret = "IS_SEQ_EXPR";
      break;
    }
    case IS_ENUM_OR_SET: {
      ret = "IS_ENUM_OR_SET";
      break;
    }
    case IS_CONST_EXPR: {
      ret = "IS_CONST_EXPR";
      break;
    }
    case CNT_CONST_EXPR: {
      ret = "CNT_CONST_EXPR";
      break;
    }
    case CNT_CONST: {
      ret = "CNT_CONST";
      break;
    }
    case CNT_COLUMN: {
      ret = "CNT_COLUMN";
      break;
    }
    case CNT_PARAM: {
      ret = "CNT_PARAM";
      break;
    }
    case CNT_AGG: {
      ret = "CNT_AGG";
      break;
    }
    case CNT_FUNC: {
      ret = "CNT_FUNC";
      break;
    }
    case CNT_IN: {
      ret = "CNT_IN";
      break;
    }
    case CNT_NOT: {
      ret = "CNT_NOT";
      break;
    }
    case CNT_OR: {
      ret = "CNT_OR";
      break;
    }
    case CNT_SUB_QUERY: {
      ret = "CNT_SUB_QUERY";
      break;
    }
    case CNT_ALIAS: {
      ret = "CNT_ALIAS";
      break;
    }
    case CNT_CUR_TIME: {
      ret = "CNT_CUR_TIME";
      break;
    }
    case CNT_IS_EXPR: {
      ret = "CNT_IS_EXPR";
      break;
    }
    case CNT_CALCULABLE_EXPR: {
      ret = "CNT_CALCULABLE_EXPR";
      break;
    }
    case CNT_VALUES: {
      ret = "CNT_VALUES";
      break;
    }
    case CNT_DEFAULT: {
      ret = "CNT_DEFAULT";
      break;
    }
    case CNT_USER_VARIABLE: {
      ret = "CNT_USER_VARIABLE";
      break;
    }
    case CNT_STATE_FUNC: {
      ret = "CNT_STATE_FUNC";
      break;
    }
    case CNT_SET_OP: {
      ret = "CNT_SET_OP";
      break;
    }
    case CNT_LAST_INSERT_ID: {
      ret = "CNT_LAST_INSERT_ID";
      break;
    }
    case CNT_DOMAIN_INDEX_FUNC: {
      ret = "CNT_DOMAIN_INDEX_FUNC";
      break;
    }
    case CNT_WINDOW_FUNC: {
      ret = "CNT_WINDOW_FUNC";
      break;
    }
    case CNT_PRIOR: {
      ret = "CNT_PRIOR";
      break;
    }
    case CNT_RAND_FUNC: {
      ret = "CNT_RAND_FUNC";
      break;
    }
    case CNT_ROWNUM: {
      ret = "CNT_ROWNUM";
      break;
    }
    case CNT_LEVEL: {
      ret = "CNT_LEVEL";
      break;
    }
    case CNT_CONNECT_BY_ISLEAF: {
      ret = "CNT_CONNECT_BY_ISLEAF";
      break;
    }
    case CNT_CONNECT_BY_ISCYCLE: {
      ret = "CNT_CONNECT_BY_ISCYCLE";
      break;
    }
    case CNT_PSEUDO_COLUMN: {
      ret = "CNT_PSEUDO_COLUMN";
      break;
    }
    case CNT_CONNECT_BY_ROOT: {
      ret = "CNT_CONNECT_BY_ROOT";
      break;
    }
    case CNT_SYS_CONNECT_BY_PATH: {
      ret = "CNT_SYS_CONNECT_BY_PATH";
      break;
    }
    case CNT_OUTER_JOIN_SYMBOL: {
      ret = "CNT_OUTER_JOIN_SYMBOL";
      break;
    }
    case CNT_SO_UDF: {
      ret = "CNT_SO_UDF";
      break;
    }
    case CNT_SEQ_EXPR: {
      ret = "CNT_SEQ_EXPR";
      break;
    }
    case CNT_EXEC_PARAM: {
      ret = "CNT_EXEC_PARAM";
      break;
    }
    case CNT_ENUM_OR_SET: {
      ret = "CNT_ENUM_OR_SET";
      break;
    }
    case BE_USED: {
      ret = "BE_USED";
      break;
    }
    case IS_SIMPLE_COND: {
      ret = "IS_SIMPLE_COND";
      break;
    }
    case IS_RANGE_COND: {
      ret = "IS_RANGE_COND";
      break;
    }
    case IS_JOIN_COND: {
      ret = "IS_JOIN_COND";
      break;
    }
    case IS_COLUMNLIZED: {
      ret = "IS_COLUMNLIZED";
      break;
    }
    case IS_ROOT: {
      ret = "IS_ROOT";
      break;
    }
    case IS_EXEC_PARAM: {
      ret = "IS_EXEC_PARAM";
      break;
    }
    case IS_NEW_AGG_EXPR: {
      ret = "IS_NEW_AGG_EXPR";
      break;
    }
    case IS_WITH_ALL: {
      ret = "IS_WITH_ALL";
      break;
    }
    case IS_WITH_ANY: {
      ret = "IS_WITH_ANY";
      break;
    }
    case IS_WITH_SUBQUERY: {
      ret = "IS_WITH_SUBQUERY";
      break;
    }
    case IS_INNER_ADDED_EXPR: {
      ret = "IS_INNER_ADDED_EXPR";
      break;
    }
    case IS_PL_ACCESS_IDX: {
      ret = "IS_PL_ACCESS_IDX";
      break;
    }
    case IS_SELECT_REF: {
      ret = "IS_SELECT_REF";
      break;
    }
    case IS_SHARED_REF: {
      ret = "IS_SHARED_REF";
      break;
    }
    case IS_OP_OPERAND_IMPLICIT_CAST: {
      ret = "IS_OP_OPERAND_IMPLICIT_CAST";
      break;
    }
    default:
      break;
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_INFO_FLAG_H_ */
