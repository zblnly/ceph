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

#ifndef CEPH_LIBRBD_SNAPINFO_H
#define CEPH_LIBRBD_SNAPINFO_H

#include <inttypes.h>

#include "include/rbd/librbd.hpp"

namespace librbd {

  using librados::snap_t;

  struct SnapInfo {
    snap_t id;
    uint64_t size;
    SnapInfo(snap_t _id, uint64_t _size) : id(_id), size(_size) {};
  };

}

#endif
