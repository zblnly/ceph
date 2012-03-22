/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#include "test/msgr/TestDriver.h"
#include "test/msgr/MessengerDriver.h"
#include "test/msgr/msgr_test_functions.h"

#include "gtest/gtest.h"

TEST(msgr, sample)
{
  TestDriver driver;
  int sample_test_result = sample_test(&driver);
  ASSERT_EQ(sample_test_result, 0);
}
