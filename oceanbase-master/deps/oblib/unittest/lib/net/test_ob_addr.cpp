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

#include <gtest/gtest.h>
#include "lib/net/ob_addr.h"

using namespace oceanbase::common;

TEST(OB_ADDR, TEST1)
{
  ObAddr addr;
  EXPECT_EQ(addr.get_ipv4(), 0U);
  EXPECT_FALSE(addr.is_valid());

  addr.set_ip_addr("0.0.0.1", 1024);
  EXPECT_EQ(addr.get_ipv4(), 1U);
  EXPECT_TRUE(addr.is_valid());

  addr.set_ip_addr("1.0.0.0", 1024);
  EXPECT_EQ(addr.get_ipv4(), 1U << 24);
  EXPECT_TRUE(addr.is_valid());

  EXPECT_EQ(addr.parse_from_cstring("1.0.0.0:1234"), OB_SUCCESS);
  EXPECT_TRUE(addr.is_valid());
  EXPECT_EQ(addr.get_ipv4(), 1U << 24);
  EXPECT_EQ(addr.get_port(), 1234);

  EXPECT_EQ(addr.parse_from_cstring("1.0.0.1234:1234"), OB_SUCCESS);
  EXPECT_FALSE(addr.is_valid());

  ObAddr addr2;
  EXPECT_LT(addr2, addr);

  addr2.set_port(1234);
  EXPECT_EQ(addr2, addr);

  addr.set_ip_addr("0.0.0.1", 1);
  EXPECT_EQ(addr.get_ipv4(), 1U);
  addr2.set_ip_addr("1.0.0.0", 1);
  EXPECT_EQ(addr2.get_ipv4(), 1U << 24);
  EXPECT_LT(addr, addr2);

  addr.set_ip_addr("0.0.0.1", 2);
  addr2.set_ip_addr("1.0.0.0", 1);
  EXPECT_LT(addr, addr2);

  addr.set_ip_addr("1.0.0.1", 1);
  addr2.set_ip_addr("1.0.0.1", 2);
  EXPECT_LT(addr, addr2);
  int ret = addr.ip_port_to_string(NULL, 10);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  char buf[64];
  ret = addr.ip_port_to_string(buf, 0);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = addr.ip_port_to_string(buf, 5);
  ASSERT_EQ(OB_SIZE_OVERFLOW, ret);
}

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
