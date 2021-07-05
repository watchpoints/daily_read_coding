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

#ifndef OB_RECURSIVE_INNER_DATA_OP_
#define OB_RECURSIVE_INNER_DATA_OP_

#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/allocator/ob_malloc.h"
#include "sql/engine/sort/ob_base_sort.h"
#include "sql/engine/sort/ob_specific_columns_sort.h"
#include "sql/engine/aggregate/ob_exec_hash_struct.h"
#include "ob_search_method_op.h"
#include "ob_fake_cte_table_op.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase {
namespace sql {
class ObRecursiveInnerDataOp {
  using ObTreeNode = ObSearchMethodOp::ObTreeNode;
  friend class ObRecursiveUnionAllOp;
  friend class ObRecursiveUnionAllSpec;

public:
  struct RowComparer;
  enum RecursiveUnionState { R_UNION_BEGIN, R_UNION_READ_LEFT, R_UNION_READ_RIGHT, R_UNION_END, R_UNION_STATE_COUNT };
  enum SearchStrategyType { DEPTH_FRIST, BREADTH_FRIST };

public:
  explicit ObRecursiveInnerDataOp(ObEvalCtx& eval_ctx, ObExecContext& exec_ctx, const ExprFixedArray& left_output,
      const common::ObIArray<ObSortFieldCollation>& sort_collations,
      const common::ObIArray<uint64_t>& cycle_by_col_lists, const common::ObIArray<ObExpr*>& output_union_exprs)
      : state_(RecursiveUnionState::R_UNION_READ_LEFT),
        stored_row_buf_(ObModIds::OB_SQL_CTE_ROW),
        pump_operator_(nullptr),
        left_op_(nullptr),
        right_op_(nullptr),
        search_type_(SearchStrategyType::BREADTH_FRIST),
        sort_collations_(sort_collations),
        result_output_(stored_row_buf_),
        search_expr_(nullptr),
        cycle_expr_(nullptr),
        cycle_value_(),
        non_cycle_value_(),
        cte_columns_(nullptr),
        ordering_column_(1),
        dfs_pump_(stored_row_buf_, left_output, sort_collations, cycle_by_col_lists),
        bfs_pump_(stored_row_buf_, left_output, sort_collations, cycle_by_col_lists),
        eval_ctx_(eval_ctx),
        ctx_(exec_ctx),
        output_union_exprs_(output_union_exprs)
  {}
  ~ObRecursiveInnerDataOp() = default;

  inline void set_left_child(ObOperator* op)
  {
    left_op_ = op;
  };
  inline void set_right_child(ObOperator* op)
  {
    right_op_ = op;
  };
  inline void set_fake_cte_table(ObFakeCTETableOp* cte_table)
  {
    pump_operator_ = cte_table;
  };
  inline void set_search_strategy(ObRecursiveInnerDataOp::SearchStrategyType strategy)
  {
    search_type_ = strategy;
  }
  int add_sort_collation(ObSortFieldCollation sort_collation);
  int add_cycle_column(uint64_t index);
  int add_cmp_func(ObCmpFunc cmp_func);
  int get_next_row();
  int rescan();
  int set_fake_cte_table_empty();
  int init(const ObExpr* search_expr, const ObExpr* cycle_expr);
  void set_cte_column_exprs(common::ObIArray<ObExpr*>* exprs)
  {
    cte_columns_ = exprs;
  }

private:
  void destroy();
  int add_pseudo_column(bool cycle = false);
  int try_get_left_rows();
  int try_get_right_rows();
  int try_format_output_row();
  /**
   * The left child of a recursive union is called plan a, and the right child is called plan b
   * plan a will produce initial data, and the recursive union itself controls the progress of recursion.
   * The right child is a plan executed recursively
   */
  int get_all_data_from_left_child();
  int get_all_data_from_right_child();
  // In depth-first recursion, perform row UNION ALL operations
  int depth_first_union(const bool sort = true);
  // In breadth-first recursion, the UNION ALL operation of the row is performed
  int breadth_first_union(bool left_branch, bool& continue_search);
  int start_new_level(bool left_branch);
  // output a row to the fake cte table operator,
  // which will be used as the input for the plan b later
  int fake_cte_table_add_row(ObTreeNode& node);
  // set value for cte table column expr
  int assign_to_cur_row(ObChunkDatumStore::StoredRow* stored_row);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRecursiveInnerDataOp);

private:
  RecursiveUnionState state_;
  common::ObArenaAllocator stored_row_buf_;
  ObFakeCTETableOp* pump_operator_;
  ObOperator* left_op_;
  ObOperator* right_op_;
  // Mark depth first or breadth first
  SearchStrategyType search_type_;
  // Sort by which columns
  const common::ObIArray<ObSortFieldCollation>& sort_collations_;
  // The data to be output to the next operator, R in pseudo code
  common::ObList<ObTreeNode, common::ObIAllocator> result_output_;
  // pseudo column
  const ObExpr* search_expr_;
  const ObExpr* cycle_expr_;
  // cycle value
  ObDatum cycle_value_;
  // non-cycle value
  ObDatum non_cycle_value_;
  common::ObIArray<ObExpr*>* cte_columns_;
  /**
   * represent search breadth/depth first by xxx set ordering_column.
   * Oracle explain it as:
   * The ordering_column is automatically added to the column list for the query name.
   * The query that selects from query_name can include an ORDER BY on ordering_column to return
   * the rows in the order that was specified by the SEARCH clause.
   */
  int64_t ordering_column_;
  // depth first
  ObDepthFisrtSearchOp dfs_pump_;
  // breadth first
  ObBreadthFisrtSearchOp bfs_pump_;
  ObEvalCtx& eval_ctx_;
  ObExecContext& ctx_;
  const common::ObIArray<ObExpr*>& output_union_exprs_;
};
}  // end namespace sql
}  // end namespace oceanbase

#endif
