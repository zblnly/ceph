// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2010 Sage Weil <sage@newdream.net>
 * Copyright (C) 2010 Dreamhost
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "global/global_init.h"
#include "common/ceph_argparse.h"
#include "common/config.h"
#include "tools/common.h"

#include <iostream>
#include <sstream>
#include <vector>

// tool/gui.cc
int run_gui(CephToolCtx *ctx, int argc, char **argv);

using std::vector;

static std::ostringstream gss;

static void usage()
{
  cerr << "usage: gceph [options]\n\n";
  cerr << "Runs the ceph graphical monitor\n";
  generic_client_usage(); // Will exit()
}

static void parse_gceph_args(vector<const char*> &args)
{
  std::vector<const char*>::iterator i;
  std::string val;
  for (i = args.begin(); i != args.end(); ) {
    if (ceph_argparse_flag(args, i, "-h", "--help", (char*)NULL)) {
      usage();
    } else {
      ++i;
    }
  }
}

static int cephtool_run_gui(CephToolCtx *ctx, int argc,
			    const char **argv)
{
  ctx->log = &gss;
  ctx->slog = &gss;

  // TODO: make sure that we capture the log this generates in the GUI
  ctx->lock.Lock();
  ctx->mc.set_want_keys(CEPH_ENTITY_TYPE_ANY);
  ctx->mc.sub_want("osdmap", 0, 0);
  ctx->mc.sub_want("mdsmap", 0, 0);
  ctx->mc.sub_want("monmap", 0, 0);
  ctx->lock.Unlock();

  return run_gui(ctx, argc, (char **)argv);
}

static CephToolCtx *ctx = NULL;

void ceph_tool_common_shutdown_wrapper()
{
  ceph_tool_messenger_shutdown();
  ceph_tool_common_shutdown(ctx);
}

int main(int argc, const char **argv)
{
  int ret = 0;

  vector<const char*> args;
  argv_to_vec(argc, argv, args);
  env_to_vec(args);

  global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);
  parse_gceph_args(args);

  ctx = ceph_tool_common_init(CEPH_TOOL_MODE_GUI, false);
  if (!ctx) {
    derr << "cephtool_common_init failed." << dendl;
    return 1;
  }

  atexit(ceph_tool_common_shutdown_wrapper);

  vec_to_argv(args, argc, argv);
  if (cephtool_run_gui(ctx, argc, argv))
    ret = 1;

  if (ceph_tool_messenger_shutdown())
    ret = 1;

  return ret;
}
