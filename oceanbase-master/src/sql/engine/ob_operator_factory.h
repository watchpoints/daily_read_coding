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

#ifndef OCEANBASE_ENGINE_OB_OPERATOR_FACTORY_H_
#define OCEANBASE_ENGINE_OB_OPERATOR_FACTORY_H_

#include "lib/allocator/ob_allocator.h"
#include "sql/engine/ob_phy_operator_type.h"

namespace oceanbase {
namespace sql {

class ObOpSpec;
class ObOperator;
class ObOpInput;
class ObExecContext;
class ObStaticEngineCG;
class ObLogicalOperator;

struct ObOperatorFactory {
public:
  // allocate operator specification
  static int alloc_op_spec(
      common::ObIAllocator& alloc, const ObPhyOperatorType type, const int64_t child_cnt, ObOpSpec*& spec);

  // allocate operator
  static int alloc_operator(common::ObIAllocator& alloc, ObExecContext& exec_ctx, const ObOpSpec& spec,
      ObOpInput* input, const int64_t child_cnt, ObOperator*& op);

  // allocate operator input
  static int alloc_op_input(
      common::ObIAllocator& alloc, ObExecContext& exec_ctx, const ObOpSpec& spec, ObOpInput*& input);

  // generate operator specification
  static int generate_spec(ObStaticEngineCG& cg, ObLogicalOperator& log_op, ObOpSpec& spec, const bool in_root_job);

  static inline bool is_registered(const ObPhyOperatorType type)
  {
    return type >= 0 && type < PHY_END && NULL != G_ALL_ALLOC_FUNS_[type].op_func_;
  }

  static inline bool has_op_input(const ObPhyOperatorType type)
  {
    return type >= 0 && type < PHY_END && NULL != G_ALL_ALLOC_FUNS_[type].input_func_;
  }

  struct AllocFun {
    __typeof__(&ObOperatorFactory::alloc_op_spec) spec_func_;
    __typeof__(&ObOperatorFactory::alloc_operator) op_func_;
    __typeof__(&ObOperatorFactory::alloc_op_input) input_func_;
    __typeof__(&ObOperatorFactory::generate_spec) gen_spec_func_;
  };

private:
  static AllocFun* G_ALL_ALLOC_FUNS_;
};

}  // end namespace sql
}  // end namespace oceanbase

#endif  // OCEANBASE_ENGINE_OB_OPERATOR_FACTORY_H_
