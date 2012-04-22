// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2012 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.	See file COPYING.
 *
 */

#include "WatchCtx.h"
#include "ImageCtx.h"

#include "common/Cond.h"

#include "common/dout.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd: "

namespace librbd {

void WatchCtx::invalidate()
{
  Mutex::Locker l(lock);
  valid = false;
}

void WatchCtx::notify(uint8_t opcode, uint64_t ver, bufferlist& bl)
{
  Mutex::Locker l(lock);
  ldout(ictx->cct, 1) << " got notification opcode=" << (int)opcode << " ver=" << ver << " cookie=" << cookie << dendl;
  if (valid) {
    Mutex::Locker lictx(ictx->refresh_lock);
    ictx->needs_refresh = true;
    ictx->perfcounter->inc(l_librbd_notify);
  }
}

}
