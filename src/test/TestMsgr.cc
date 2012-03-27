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

#include <vector>

#include "common/ceph_argparse.h"
#include "global/global_init.h"

#include "msgr/TestDriver.h"
#include "msgr/MessengerDriver.h"
#include "msgr/msgr_test_functions.h"

int main (int argc, const char **argv)
{
  vector<const char *> args;
  argv_to_vec(argc, argv, args);
  env_to_vec(args);

  global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);

  TestDriver driver(g_ceph_context);
  return sample_test(&driver);
}
