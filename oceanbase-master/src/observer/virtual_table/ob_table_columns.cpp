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

#define USING_LOG_PREFIX SERVER
#include "observer/virtual_table/ob_table_columns.h"
#include "lib/string/ob_sql_string.h"
#include "lib/oblog/ob_log.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/parser/ob_parser.h"
#include "sql/resolver/dml/ob_select_resolver.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;
namespace oceanbase {
namespace observer {

ObTableColumns::ObTableColumns()
    : ObVirtualTableScannerIterator(),
      type_str_(),
      column_type_str_(type_str_),
      column_type_str_len_(OB_MAX_SYS_PARAM_NAME_LENGTH)
{
  MEMSET(type_str_, 0, OB_MAX_SYS_PARAM_NAME_LENGTH);
}

ObTableColumns::~ObTableColumns()
{}

void ObTableColumns::reset()
{
  MEMSET(type_str_, 0, OB_MAX_SYS_PARAM_NAME_LENGTH);
  ObVirtualTableScannerIterator::reset();
}

int ObTableColumns::inner_get_next_row(ObNewRow*& row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_) || OB_ISNULL(allocator_) || OB_ISNULL(schema_guard_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("data member is not init", K(ret), K(session_), K(allocator_), K(schema_guard_));
  } else {
    if (!start_to_read_) {
      const ObTableSchema* table_schema = NULL;
      uint64_t show_table_id = OB_INVALID_ID;
      if (OB_FAIL(calc_show_table_id(show_table_id))) {
        LOG_WARN("fail to calc show table id", K(ret), K(show_table_id));
      } else if (OB_UNLIKELY(OB_INVALID_ID == show_table_id)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, "this table is used for show clause, can't be selected");
      } else if (OB_FAIL(schema_guard_->get_table_schema(show_table_id, table_schema))) {
        LOG_WARN("fail to get table schema", K(ret));
      } else if (OB_UNLIKELY(NULL == table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("fail to get table schema", K(ret), K(show_table_id));
      } else if (table_schema->is_dropped_schema()) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table schema is dropped schema", K(ret), K(show_table_id));
      } else if (OB_SYS_TENANT_ID != table_schema->get_tenant_id() && table_schema->is_vir_table() &&
                 is_restrict_access_virtual_table(table_schema->get_table_id())) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("fail to get table schema", K(ret), K(show_table_id));
      } else {
        if (table_schema->is_view_table() && !table_schema->is_materialized_view()) {
          ObString view_definition;
          ObSelectStmt* select_stmt = NULL;
          ObSelectStmt* real_stmt = NULL;
          ObStmtFactory stmt_factory(*allocator_);
          ObRawExprFactory expr_factory(*allocator_);
          if (OB_FAIL(ObSQLUtils::generate_view_definition_for_resolve(*allocator_,
                  session_->get_local_collation_connection(),
                  table_schema->get_view_schema(),
                  view_definition))) {
            LOG_WARN("fail to generate view definition for resolve", K(ret));
          } else if (OB_FAIL(resolve_view_definition(allocator_,
                         session_,
                         schema_guard_,
                         *table_schema,
                         select_stmt,
                         expr_factory,
                         stmt_factory))) {
            LOG_WARN("failed to resolve view definition", K(view_definition), K(ret));
          } else if (OB_UNLIKELY(NULL == select_stmt)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("select_stmt is NULL", K(ret));
          } else if (OB_ISNULL(real_stmt = select_stmt->get_real_stmt())) {
            // case : view definition is set_op
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("real stmt is NULL", K(ret));
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < real_stmt->get_select_item_size(); ++i) {
              if (OB_FAIL(fill_row_cells(table_schema->get_table_id(), real_stmt, real_stmt->get_select_item(i)))) {
                LOG_WARN("fail to fill row cells", K(ret));
              } else if (OB_FAIL(scanner_.add_row(cur_row_))) {
                LOG_WARN("fail to add row", K(ret), K(cur_row_));
              } else { /*do nothing*/
              }
            }
          }
        } else {
          ObColumnIterByPrevNextID iter(*table_schema);
          const ObColumnSchemaV2* col = NULL;
          while (OB_SUCC(ret) && OB_SUCC(iter.next(col))) {
            if (OB_ISNULL(col)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("The column is null", K(ret));
            } else if (col->is_shadow_column()) {
              // do nothing
            } else if (col->is_invisible_column()) {
              // do nothing
            } else if (col->is_hidden()) {
              // do nothing
            } else if (OB_FAIL(fill_row_cells(*table_schema, *col))) {
              LOG_WARN("fail to fill row cells", K(ret), K(col));
            } else if (OB_FAIL(scanner_.add_row(cur_row_))) {
              LOG_WARN("fail to add row", K(ret), K(cur_row_));
            } else { /*do nothing*/
            }
          }
          if (ret != OB_ITER_END) {
            LOG_WARN("Failed to iterate all table columns. iter quit. ", K(ret));
          } else {
            ret = OB_SUCCESS;
          }
        }
        if (OB_SUCC(ret)) {
          scanner_it_ = scanner_.begin();
          start_to_read_ = true;
        }
      }
    }
    if (OB_LIKELY(OB_SUCC(ret) && start_to_read_)) {
      if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to get next row", K(ret));
        }
      } else {
        row = &cur_row_;
      }
    }
  }
  return ret;
}

int ObTableColumns::calc_show_table_id(uint64_t& show_table_id)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && OB_INVALID_ID == show_table_id && i < key_ranges_.count(); ++i) {
    ObRowkey start_key = key_ranges_.at(i).start_key_;
    ObRowkey end_key = key_ranges_.at(i).end_key_;
    const ObObj* start_key_obj_ptr = start_key.get_obj_ptr();
    const ObObj* end_key_obj_ptr = end_key.get_obj_ptr();
    if (start_key.get_obj_cnt() > 0 && start_key.get_obj_cnt() == end_key.get_obj_cnt()) {
      if (OB_UNLIKELY(NULL == start_key_obj_ptr || NULL == end_key_obj_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("key obj ptr is NULL", K(ret), K(start_key_obj_ptr), K(end_key_obj_ptr));
      } else if (start_key_obj_ptr[0] == end_key_obj_ptr[0] && ObIntType == start_key_obj_ptr[0].get_type()) {
        show_table_id = start_key_obj_ptr[0].get_int();
      } else { /*do nothing*/
      }
    }
  }
  return ret;
}

int ObTableColumns::get_type_str(const ObObjMeta& obj_meta, const ObAccuracy& accuracy,
    const common::ObIArray<ObString>& type_info, const int16_t default_length_semantics, ObString& type_val)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;

  if (OB_FAIL(ob_sql_type_str(
          obj_meta, accuracy, type_info, default_length_semantics, column_type_str_, column_type_str_len_, pos))) {
    if (OB_MAX_SYS_PARAM_NAME_LENGTH == column_type_str_len_ && OB_SIZE_OVERFLOW == ret) {
      if (OB_UNLIKELY(
              NULL == (column_type_str_ = static_cast<char*>(allocator_->alloc(OB_MAX_EXTENDED_TYPE_INFO_LENGTH))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SERVER_LOG(ERROR, "fail to alloc memory", K(ret));
      } else {
        pos = 0;
        column_type_str_len_ = OB_MAX_EXTENDED_TYPE_INFO_LENGTH;
        ret = ob_sql_type_str(
            obj_meta, accuracy, type_info, default_length_semantics, column_type_str_, column_type_str_len_, pos);
      }
    }
  }
  if (OB_SUCC(ret)) {
    type_val = ObString(column_type_str_len_, static_cast<int32_t>(strlen(column_type_str_)), column_type_str_);
  }
  return ret;
}

int ObTableColumns::fill_row_cells(const ObTableSchema& table_schema, const ObColumnSchemaV2& column_schema)
{
  int ret = OB_SUCCESS;
  uint64_t cell_idx = 0;
  const ObRowkeyInfo& rowkey_info = table_schema.get_rowkey_info();
  char* buf = NULL;
  int64_t buf_len = number::ObNumber::MAX_PRINTABLE_SIZE;
  int64_t pos = 0;
  if (OB_ISNULL(cur_row_.cells_) || OB_ISNULL(allocator_) || OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data member is not init", K(ret), K(cur_row_.cells_), K(allocator_), K(session_));
  } else if (OB_UNLIKELY(cur_row_.count_ < output_column_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur row cell count is less than output coumn", K(ret), K(cur_row_.count_), K(output_column_ids_.count()));
  } else if (OB_UNLIKELY(NULL == (buf = static_cast<char*>(allocator_->alloc(buf_len))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to allocate memory", K(ret));
  } else { /*do nothing*/
  }

  for (int64_t j = 0; OB_SUCC(ret) && j < output_column_ids_.count(); ++j) {
    uint64_t col_id = output_column_ids_.at(j);
    switch (col_id) {
      case TABLE_ID: {
        cur_row_.cells_[cell_idx].set_int(static_cast<int64_t>(table_schema.get_table_id()));
        break;
      }
      case FIELD: {
        cur_row_.cells_[cell_idx].set_varchar(column_schema.get_column_name_str());
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case TYPE: {
        ObAccuracy acc = column_schema.get_accuracy();
        if (share::is_oracle_mode() && column_schema.get_meta_type().is_number() &&
            acc.get_precision() == PRECISION_UNKNOWN_YET && acc.get_scale() >= OB_MIN_NUMBER_SCALE) {
          // compatible with oracle, just show differently
          acc.set_precision(38);
        }
        ObString type_val;
        const ObLengthSemantics default_length_semantics = session_->get_local_nls_length_semantics();
        if (OB_FAIL(get_type_str(column_schema.get_meta_type(),
                acc,
                column_schema.get_extended_type_info(),
                default_length_semantics,
                type_val))) {
          LOG_WARN("fail to get data type str", K(ret), K(column_schema.get_data_type()));
          break;
        } else {
          if (share::is_oracle_mode()) {
            ObCharset::caseup(ObCollationType::CS_TYPE_UTF8MB4_BIN, type_val);
          }
          cur_row_.cells_[cell_idx].set_varchar(type_val);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
        }
        break;
      }
      case COLLATION: {
        if (column_schema.is_string_type() && CS_TYPE_INVALID != column_schema.get_collation_type() &&
            CS_TYPE_BINARY != column_schema.get_collation_type()) {
          cur_row_.cells_[cell_idx].set_varchar(ObCharset::collation_name(column_schema.get_collation_type()));
        } else {
          cur_row_.cells_[cell_idx].set_varchar(ObString("NULL"));
        }
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case NULLABLE: {
        const char* ptr = column_schema.is_nullable() ? "YES" : "NO";
        ObString nullable_val = ObString::make_string(ptr);
        cur_row_.cells_[cell_idx].set_varchar(nullable_val);
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case KEY: {
        ObRowkeyColumn rowkey_column;
        int64_t index = -1;
        rowkey_info.get_index(column_schema.get_column_id(), index, rowkey_column);
        // cells[cell_idx].set_int(index + 1); /* rowkey id is rowkey index plus 1 */
        KeyType key_type = KEY_TYPE_MAX;
        if (OB_FAIL(get_key_type(table_schema, column_schema, key_type))) {
          LOG_WARN("get key type fail", K(ret));
        } else {
          switch (key_type) {
            case KEY_TYPE_PRIMARY:
              cur_row_.cells_[cell_idx].set_varchar(ObString("PRI"));
              cur_row_.cells_[cell_idx].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            case KEY_TYPE_UNIQUE:
              cur_row_.cells_[cell_idx].set_varchar(ObString("UNI"));
              cur_row_.cells_[cell_idx].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            case KEY_TYPE_MULTIPLE:
              cur_row_.cells_[cell_idx].set_varchar(ObString("MUL"));
              cur_row_.cells_[cell_idx].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            case KEY_TYPE_EMPTY:
              cur_row_.cells_[cell_idx].set_varchar(ObString(""));
              cur_row_.cells_[cell_idx].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
              break;
            default:
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected key type", K(ret), K(key_type));
              break;
          }
        }
        break;
      }
      case DEFAULT: {
        ObObj def_obj = column_schema.get_cur_default_value();
        if (IS_DEFAULT_NOW_OBJ(def_obj)) {
          const int16_t scale = column_schema.get_data_scale();
          if (OB_FAIL(databuff_printf(buf, buf_len, pos, N_UPPERCASE_CUR_TIMESTAMP))) {
            LOG_WARN("fail to print default_datetime_func_name", K(ret));
          } else if (scale != 0 && OB_FAIL(databuff_printf(buf, buf_len, pos, "(%d)", scale))) {
            LOG_WARN("fail to print scale", K(ret), K(scale));
          } else {
            cur_row_.cells_[cell_idx].set_varchar(ObString(static_cast<int32_t>(pos), buf));
          }
        } else if (def_obj.is_null()) {
          // NOTICE: default value is NULL need print string "NULL"
          cur_row_.cells_[cell_idx].set_varchar("NULL");
        } else if (def_obj.is_bit()) {
          if (OB_FAIL(def_obj.print_varchar_literal(buf, buf_len, pos, TZ_INFO(session_)))) {
            LOG_WARN("fail to print varchar literal", K(ret), K(def_obj), K(buf_len), K(pos), K(buf));
          } else {
            cur_row_.cells_[cell_idx].set_varchar(ObString(static_cast<int32_t>(pos), buf));
          }
        } else if (ob_is_enum_or_set_type(def_obj.get_type())) {
          if (OB_FAIL(def_obj.print_plain_str_literal(column_schema.get_extended_type_info(), buf, buf_len, pos))) {
            LOG_WARN("fail to print plain str literal", K(column_schema), K(buf), K(buf_len), K(pos), K(ret));
          } else {
            cur_row_.cells_[cell_idx].set_varchar(ObString(static_cast<int32_t>(pos), buf));
          }
        } else if (column_schema.is_default_expr_v2_column()) {
          cur_row_.cells_[cell_idx].set_varchar(column_schema.get_cur_default_value().get_string());
        } else if (def_obj.is_varchar()) {
          cur_row_.cells_[cell_idx].set_varchar(column_schema.get_cur_default_value().get_string());
        } else {
          // TODO: obobj.print not compatible with mysql, here use cast.
          const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session_);
          ObCastCtx cast_ctx(allocator_, &dtc_params, CM_NONE, ObCharset::get_system_collation());
          ObObj buf_obj;
          const ObObj* res_obj_ptr = NULL;
          if (OB_FAIL(ObObjCaster::to_type(ObVarcharType, cast_ctx, def_obj, buf_obj, res_obj_ptr))) {
            LOG_WARN("failed to cast object to ObVarcharType ", K(ret), K(def_obj));
          } else if (OB_UNLIKELY(NULL == res_obj_ptr)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("cast result is NULL", K(ret), K(res_obj_ptr));
          } else {
            cur_row_.cells_[cell_idx] = *res_obj_ptr;
          }
        }
        if (OB_SUCC(ret)) {
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
        }
        break;
      }
      case EXTRA: {
        ObString extra_val;
        if (column_schema.is_autoincrement()) {
          extra_val = ObString::make_string("auto_increment");
        } else if (column_schema.is_on_update_current_timestamp()) {
          extra_val = ObString::make_string(N_UPDATE_CURRENT_TIMESTAMP);
        } else if (column_schema.is_virtual_generated_column()) {
          extra_val = ObString::make_string("VIRTUAL GENERATED");
        } else if (column_schema.is_stored_generated_column()) {
          extra_val = ObString::make_string("STORED GENERATED");
        } else { /*do nothing*/
        }
        cur_row_.cells_[cell_idx].set_varchar(extra_val);
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case PRIVILEGES: {
        cur_row_.cells_[cell_idx].set_varchar(ObString(""));  // TODO
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case COMMENT: {
        cur_row_.cells_[cell_idx].set_varchar(column_schema.get_comment_str());
        cur_row_.cells_[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column id", K(ret), K(cell_idx), K(j), K(output_column_ids_), K(col_id));
        break;
      }
    }
    if (OB_SUCC(ret)) {
      cell_idx++;
    }
  }
  return ret;
}

int ObTableColumns::fill_row_cells(
    const uint64_t table_id, const ObSelectStmt* select_stmt, const SelectItem& select_item)
{
  int ret = OB_SUCCESS;
  uint64_t cell_idx = 0;
  ColumnAttributes column_attributes;
  if (OB_ISNULL(cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data member is not init", K(ret), K(cur_row_.cells_));
  } else if (OB_UNLIKELY(cur_row_.count_ < output_column_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN(
        "cur row cell count is less than output column", K(ret), K(cur_row_.count_), K(output_column_ids_.count()));
  } else if (OB_FAIL(deduce_column_attributes(select_stmt, select_item, column_attributes))) {
    LOG_WARN("failed to deduce column attributes", K(select_item), K(ret));
  } else {
    for (int64_t j = 0; OB_SUCC(ret) && j < output_column_ids_.count(); ++j) {
      uint64_t col_id = output_column_ids_.at(j);
      switch (col_id) {
        case TABLE_ID: {
          cur_row_.cells_[cell_idx].set_int(static_cast<int64_t>(table_id));
          break;
        }
        case FIELD: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.field_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case TYPE: {
          if (share::is_oracle_mode()) {
            ObCharset::caseup(ObCollationType::CS_TYPE_UTF8MB4_BIN, column_attributes.type_);
          }
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.type_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case COLLATION: {
          cur_row_.cells_[cell_idx].set_varchar(ObString(""));
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case NULLABLE: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.null_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case KEY: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.key_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case DEFAULT: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.default_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case EXTRA: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.extra_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case PRIVILEGES: {
          cur_row_.cells_[cell_idx].set_varchar(column_attributes.privileges_);
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case COMMENT: {
          cur_row_.cells_[cell_idx].set_varchar(ObString(""));
          cur_row_.cells_[cell_idx].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid column id", K(ret), K(cell_idx), K(j), K(output_column_ids_), K(col_id));
          break;
        }
      }  // end switch
      if (OB_SUCC(ret)) {
        cell_idx++;
      }
    }  // end for
  }

  return ret;
}

int ObTableColumns::deduce_column_attributes(
    const ObSelectStmt* select_stmt, const SelectItem& select_item, ColumnAttributes& column_attributes)
{
  int ret = OB_SUCCESS;
  // nullable = YES:  if some binaryref expr is nullable
  // nullable = NO, other cases
  // that's to say, only if all binaryref expr are NOT nullable, result is NOT nullable
  bool nullable = false;

  // default = NULL: if some binaryref expr has no default
  // default = 0: only if all binaryref expr has default value, and type is INT
  // default = NULL: other cases
  // TODO: default = 0 case
  bool has_default = true;
  const ObRawExpr* expr = select_item.expr_;
  if (OB_ISNULL(select_stmt) || OB_ISNULL(expr) || OB_ISNULL(session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is NULL", K(ret), K(expr), K(select_stmt), KP(session_));
  } else {
    if (ObRawExpr::EXPR_COLUMN_REF == expr->get_expr_class()) {
      if (OB_FAIL(set_null_and_default_according_binary_expr(select_stmt, expr, nullable, has_default))) {
        LOG_WARN("fail to get null and default for binary expr", K(ret));
      }
    } else {
      // ObOpRawExpr, ObCaseOpRawExpr, ObAggFunRawExpr
      for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
        const ObRawExpr* t_expr = expr->get_param_expr(i);
        if (OB_UNLIKELY(NULL == t_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("expr is NULL", K(ret), K(i));
        } else {
          switch (t_expr->get_expr_class()) {
            case ObRawExpr::EXPR_COLUMN_REF:
              if (OB_FAIL(set_null_and_default_according_binary_expr(select_stmt, t_expr, nullable, has_default))) {
                LOG_WARN("fail to get null and default for binary expr", K(ret));
              }
              break;
            default:
              break;
          }  // end switch
        }
      }  // end for
    }
  }

  if (OB_SUCC(ret)) {
    // TODO: should get types_info from expr, wait for yyy
    const ObExprResType& result_type = select_item.expr_->get_result_type();
    ObLength char_len = result_type.get_length();
    const ObLengthSemantics default_length_semantics = session_->get_local_nls_length_semantics();
    int16_t precision_or_length_semantics = result_type.get_precision();
    if (lib::is_oracle_mode() &&
        ((result_type.is_varchar_or_char() && precision_or_length_semantics == default_length_semantics) ||
            ob_is_nstring_type(result_type.get_type()))) {
      precision_or_length_semantics = LS_DEFAULT;
    }
    if (OB_SUCC(ret)) {
      int64_t pos = 0;
      if (OB_FAIL(ob_sql_type_str(column_type_str_,
              column_type_str_len_,
              pos,
              result_type.get_type(),
              char_len,
              precision_or_length_semantics,
              result_type.get_scale(),
              result_type.get_collation_type()))) {
        LOG_WARN("fail to get data type str", K(ret));
      } else {
        LOG_DEBUG("succ to ob_sql_type_str",
            K(ret),
            K(result_type),
            K(select_stmt),
            KPC(select_item.expr_),
            K(precision_or_length_semantics));
      }
    }
  }

  if (OB_SUCC(ret)) {
    // set attributes
    column_attributes.field_ = select_item.alias_name_;
    column_attributes.type_ =
        ObString(column_type_str_len_, static_cast<int32_t>(strlen(column_type_str_)), column_type_str_);
    column_attributes.key_ = ObString::make_string("");
    column_attributes.null_ = ObString::make_string(nullable ? "YES" : "NO");
    column_attributes.default_ = ObString::make_string(!has_default ? "NULL" : "");
    column_attributes.extra_ = ObString::make_string("");
    column_attributes.privileges_ = ObString::make_string("");
    // TODO:
    // ObObj default;
    // view_column.set_cur_default_value(default);
  }

  return ret;
}

int ObTableColumns::set_null_and_default_according_binary_expr(
    const ObSelectStmt* select_stmt, const ObRawExpr* expr, bool& nullable, bool& has_default)
{
  int ret = OB_SUCCESS;
  const ObColumnRefRawExpr* bexpr = NULL;
  const ObTableSchema* table_schema = NULL;
  const ObColumnSchemaV2* column_schema = NULL;
  const TableItem* tbl_item = NULL;
  if (OB_ISNULL(expr) || OB_ISNULL(select_stmt) || OB_ISNULL(schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameter is not init", K(ret), K(expr), K(select_stmt), K(schema_guard_));
  } else if (OB_UNLIKELY(ObRawExpr::EXPR_COLUMN_REF != expr->get_expr_class())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr type is unexpected", K(ret), K(expr->get_expr_class()));
  } else {
    bexpr = static_cast<const ObColumnRefRawExpr*>(expr);
    // ObBinaryRefRawExpr`s first_id not real table_id, TableItem::ref_id it is.
    if (OB_UNLIKELY(NULL == (tbl_item = select_stmt->get_table_item_by_id(bexpr->get_table_id())))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table item is NULL", K(ret), K(tbl_item));
    } else if (OB_FAIL(schema_guard_->get_table_schema(tbl_item->ref_id_, table_schema)) || NULL == table_schema) {
      // reset return code to success: view_2.test
      ret = OB_SUCCESS;
      LOG_WARN("fail to get table schema", K(ret), "table_id", tbl_item->ref_id_);
    } else {
      if (OB_UNLIKELY(NULL == (column_schema = table_schema->get_column_schema(bexpr->get_column_id())))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN(" column schema is NULL", K(ret), K(column_schema));
      } else if (!nullable && column_schema->is_nullable()) {
        nullable = true;
      } else { /*do nothing*/
      }
      if (has_default && column_schema->get_cur_default_value().is_null()) {
        has_default = false;
      }
    }
  }

  return ret;
}

int ObTableColumns::resolve_view_definition(ObIAllocator* allocator, ObSQLSessionInfo* session,
    ObSchemaGetterGuard* schema_guard, const ObTableSchema& table_schema, ObSelectStmt*& select_stmt,
    ObRawExprFactory& expr_factory, ObStmtFactory& stmt_factory)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema* db_schema = NULL;
  if (OB_UNLIKELY(!table_schema.is_view_table() || NULL == allocator || NULL == session || NULL == schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parameter or data member",
        K(ret),
        K(table_schema.is_view_table()),
        K(allocator),
        K(session),
        K(schema_guard));
  } else if (OB_FAIL(schema_guard->get_database_schema(table_schema.get_database_id(), db_schema))) {
    LOG_WARN("failed to get database_schema",
        K(ret),
        K(session->get_effective_tenant_id()),
        K(table_schema.get_database_id()));
  } else if (OB_UNLIKELY(NULL == db_schema)) {
    ret = OB_ERR_BAD_DATABASE;
    LOG_WARN("db_schema is null", K(ret), K(session->get_effective_tenant_id()), K(table_schema.get_database_id()));
  } else {
    // construct sql
    const ObString& db_name = db_schema->get_database_name_str();
    const ObString& table_name = table_schema.get_table_name_str();
    ObSqlString select_sql;
    bool is_oracle_mode = share::is_oracle_mode();
    if (OB_FAIL(
            select_sql.append_fmt(is_oracle_mode ? "select * from \"%.*s\".\"%.*s\"" : "select * from `%.*s`.`%.*s`",
                db_name.length(),
                db_name.ptr(),
                table_name.length(),
                table_name.ptr()))) {
      LOG_WARN("fail to append select sql", K(ret));
    } else {
      ParseResult parse_result;
      ObParser parser(*allocator, session->get_sql_mode(), session->get_local_collation_connection());
      if (OB_FAIL(parser.parse(select_sql.string(), parse_result))) {
        LOG_WARN("parse view definition failed", K(select_sql), K(ret));
      } else {
        ObSchemaChecker schema_checker;
        if (OB_FAIL(schema_checker.init(const_cast<ObSchemaGetterGuard&>(*schema_guard)))) {
          LOG_WARN("fail to init schema checker", K(ret));
        } else {
          ObResolverParams resolver_ctx;
          resolver_ctx.allocator_ = allocator;
          resolver_ctx.schema_checker_ = &schema_checker;
          resolver_ctx.session_info_ = const_cast<ObSQLSessionInfo*>(session);
          resolver_ctx.expr_factory_ = &expr_factory;
          resolver_ctx.stmt_factory_ = &stmt_factory;
          if (OB_ISNULL(resolver_ctx.query_ctx_ = stmt_factory.get_query_ctx())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("create query context failed", K(ret));
          } else {
            // set # of question marks
            resolver_ctx.query_ctx_->question_marks_count_ =
                static_cast<int64_t>(parse_result.question_mark_ctx_.count_);
          }
          if (OB_SUCC(ret)) {
            ObSelectResolver select_resolver(resolver_ctx);
            ParseNode* select_stmt_node = parse_result.result_tree_->children_[0];
            if (OB_UNLIKELY(NULL == select_stmt_node || select_stmt_node->type_ != T_SELECT)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid select_stmt_node", K(ret), K(select_stmt_node), K(select_stmt_node->type_));
            } else if (OB_FAIL(select_resolver.resolve(*select_stmt_node))) {
              LOG_WARN("resolve view definition failed", K(ret));
              ret = OB_ERR_VIEW_INVALID;
              LOG_USER_ERROR(
                  OB_ERR_VIEW_INVALID, db_name.length(), db_name.ptr(), table_name.length(), table_name.ptr());
            } else if (OB_UNLIKELY(NULL == select_resolver.get_basic_stmt())) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid stmt", K(ret));
            } else {
              select_stmt = static_cast<ObSelectStmt*>(select_resolver.get_basic_stmt());
              TableItem* view_item = NULL;
              if (OB_UNLIKELY(select_stmt->get_table_size() != 1)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("table count should equals 1", K(ret));
              } else if (OB_ISNULL(view_item = select_stmt->get_table_item(0))) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("view item is null");
              } else if (OB_UNLIKELY(NULL == (select_stmt = static_cast<ObSelectStmt*>(view_item->ref_query_)))) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("select_stmt should not NULL", K(ret));
              } else { /*do-nothing*/
              }
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObTableColumns::is_primary_key(
    const ObTableSchema& table_schema, const ObColumnSchemaV2& column_schema, bool& is_pri) const
{
  int ret = OB_SUCCESS;
  is_pri = false;
  const ObRowkeyInfo& rowkey_info = table_schema.get_rowkey_info();
  // If Key is PRI, the column is a PRIMARY KEY or is one of the columns in a multiple-column PRIMARY KEY.
  if (!column_schema.is_heap_alter_rowkey_column() &&
      OB_FAIL(rowkey_info.is_rowkey_column(column_schema.get_column_id(), is_pri))) {
    LOG_WARN("check if rowkey column failed.", K(ret), "column_id", column_schema.get_column_id(), K(rowkey_info));
  } else { /*do nothing*/
  }
  return ret;
}

int ObTableColumns::is_unique_key(
    const ObTableSchema& table_schema, const ObColumnSchemaV2& column_schema, bool& is_unique) const
{
  int ret = OB_SUCCESS;
  bool tmp_unique = false;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;

  if (OB_UNLIKELY(NULL == schema_guard_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("data member or parameter is NULL", K(ret), K(schema_guard_));
  } else if (OB_FAIL(table_schema.get_simple_index_infos_without_delay_deleted_tid(simple_index_infos))) {
    LOG_WARN("get simple_index_infos without delay_deleted_tid failed", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema* index_schema = NULL;
      if (OB_FAIL(schema_guard_->get_table_schema(simple_index_infos.at(i).table_id_, index_schema))) {
        LOG_WARN("fail to get table schema", K(ret));
      } else if (OB_UNLIKELY(NULL == index_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index schema from schema guard is NULL", K(ret), K(index_schema));
      } else if (index_schema->is_unique_index() && 1 == index_schema->get_index_column_num()) {
        const ObIndexInfo& index_info = index_schema->get_index_info();
        uint64_t column_id = OB_INVALID_ID;
        if (OB_FAIL(index_info.get_column_id(0, column_id))) {
          LOG_WARN("get index column id fail", K(ret));
        } else if (column_schema.get_column_id() == column_id) {
          tmp_unique = true;
        } else { /*do nothing*/
        }
      } else { /*do nothing*/
      }
    }  // for
  }
  if (OB_SUCC(ret)) {
    is_unique = tmp_unique;
  }
  return ret;
}

int ObTableColumns::is_multiple_key(
    const ObTableSchema& table_schema, const ObColumnSchemaV2& column_schema, bool& is_mul) const
{
  int ret = OB_SUCCESS;
  bool tmp_mul = false;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  if (OB_UNLIKELY(NULL == schema_guard_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("data member or parameter is NULL", K(ret), K(schema_guard_));
  } else if (OB_FAIL(table_schema.get_simple_index_infos_without_delay_deleted_tid(simple_index_infos))) {
    LOG_WARN("get simple_index_infos without delay_deleted_tid failed", K(ret));
  } else {
    // Two Case need print column to NULL:
    //  1.non-unique index`s first column.
    //  2.unique index has mutil columns, print first column to NULL.
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema* index_schema = NULL;
      if (OB_FAIL(schema_guard_->get_table_schema(simple_index_infos.at(i).table_id_, index_schema))) {
        SERVER_LOG(WARN, "fail to get table schema", K(ret));
      } else if (OB_UNLIKELY(NULL == index_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index schema from schema guard is NULL", K(ret), K(index_schema));
      } else if ((index_schema->is_unique_index() && 1 < index_schema->get_index_column_num()) ||
                 index_schema->is_normal_index()) {
        const ObIndexInfo& index_info = index_schema->get_index_info();
        uint64_t column_id = OB_INVALID_ID;
        if (OB_FAIL(index_info.get_column_id(0, column_id))) {
          LOG_WARN("get index column id fail", K(ret));
        } else if (column_schema.get_column_id() == column_id) {
          tmp_mul = true;
        } else { /*do nothing*/
        }
      } else { /*do nothing*/
      }
    }  // for
  }
  if (OB_SUCC(ret)) {
    is_mul = tmp_mul;
  }
  return ret;
}

int ObTableColumns::get_key_type(
    const ObTableSchema& table_schema, const ObColumnSchemaV2& column_schema, KeyType& key_type) const
{
  int ret = OB_SUCCESS;
  KeyType tmp_key_type = KEY_TYPE_MAX;
  bool is_pri = false;
  bool is_uni = false;
  bool is_mul = false;
  if (OB_FAIL(is_primary_key(table_schema, column_schema, is_pri))) {
    LOG_WARN("judge primary key fail", K(ret));
  } else if (is_pri) {
    tmp_key_type = KEY_TYPE_PRIMARY;
  } else if (OB_FAIL(is_unique_key(table_schema, column_schema, is_uni))) {
    LOG_WARN("judge primary key fail", K(ret));
  } else if (is_uni) {
    tmp_key_type = KEY_TYPE_UNIQUE;
  } else if (OB_FAIL(is_multiple_key(table_schema, column_schema, is_mul))) {
    LOG_WARN("judge multiple key fail", K(ret));
  } else if (is_mul) {
    tmp_key_type = KEY_TYPE_MULTIPLE;
  } else {
    tmp_key_type = KEY_TYPE_EMPTY;
  }
  if (OB_SUCC(ret)) {
    key_type = tmp_key_type;
  }
  return ret;
}

}  // namespace observer
}  // namespace oceanbase
