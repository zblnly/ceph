/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 * Do a really stupid test of the messenger system with the
 * TestDriver interface.
 */

#include "msgr/TestDriver.h"
#include "msgr/MessengerDriver.h"

int main (int argc, const char **argv)
{
  TestDriver driver;
  return driver.run_tests();
}
