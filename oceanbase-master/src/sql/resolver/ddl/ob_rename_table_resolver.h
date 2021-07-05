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

#ifndef OCEANBASE_SQL_OB_RENAME_TABLE_RESOLVER_
#define OCEANBASE_SQL_OB_RENAME_TABLE_RESOLVER_

#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase {
namespace sql {

class ObRenameTableResolver : public ObDDLResolver {
  static const int64_t OLD_NAME_NODE = 0;
  static const int64_t NEW_NAME_NODE = 1;
  static const int64_t NAME_NODE_COUNT = 2;

public:
  explicit ObRenameTableResolver(ObResolverParams& params);
  virtual ~ObRenameTableResolver();
  virtual int resolve(const ParseNode& parse_tree);
  ObRenameTableStmt* get_rename_table_stmt()
  {
    return static_cast<ObRenameTableStmt*>(stmt_);
  };

private:
  int resolve_rename_action(const ParseNode& rename_action_node);
  DISALLOW_COPY_AND_ASSIGN(ObRenameTableResolver);
};

}  // end namespace sql
}  // end namespace oceanbase

#endif  // OCEANBASE_SQL_OB_RENAME_TABLE_RESOLVER_
