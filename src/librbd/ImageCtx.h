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

#ifndef CEPH_LIBRBD_IMAGECTX_H
#define CEPH_LIBRBD_IMAGECTX_H

#include <map>

#include "common/perf_counters.h"
#include "include/rbd/librbd.hpp"

#include "librbd/LibrbdWriteback.h"

#include "osdc/ObjectCacher.h"

namespace librbd {

  enum {
    l_librbd_first = 26000,

    l_librbd_rd,               // read ops
    l_librbd_rd_bytes,         // bytes read
    l_librbd_wr,
    l_librbd_wr_bytes,
    l_librbd_discard,
    l_librbd_discard_bytes,
    l_librbd_flush,

    l_librbd_aio_rd,               // read ops
    l_librbd_aio_rd_bytes,         // bytes read
    l_librbd_aio_wr,
    l_librbd_aio_wr_bytes,
    l_librbd_aio_discard,
    l_librbd_aio_discard_bytes,

    l_librbd_snap_create,
    l_librbd_snap_remove,
    l_librbd_snap_rollback,

    l_librbd_notify,
    l_librbd_resize,

    l_librbd_last,
  };

  struct WatchCtx;

  using ceph::bufferlist;
  using librados::snap_t;
  using librados::IoCtx;
  using librados::Rados;

  struct ImageCtx {
    CephContext *cct;
    PerfCounters *perfcounter;
    struct rbd_obj_header_ondisk header;
    ::SnapContext snapc;
    vector<snap_t> snaps;
    std::map<std::string, struct SnapInfo> snaps_by_name;
    uint64_t snapid;
    bool snap_exists; // false if our snapid was deleted
    std::string name;
    std::string snapname;
    IoCtx data_ctx, md_ctx;
    WatchCtx *wctx;
    bool needs_refresh;
    Mutex refresh_lock;
    Mutex lock; // protects access to snapshot and header information
    Mutex cache_lock; // used as client_lock for the ObjectCacher

    ObjectCacher *object_cacher;
    LibrbdWriteback *writeback_handler;
    ObjectCacher::ObjectSet *object_set;

    ImageCtx(std::string imgname, const char *snap, IoCtx& p);
    ~ImageCtx();

    void perf_start(string name);
    void perf_stop();

    int snap_set(std::string snap_name);
    void snap_unset();
    snap_t get_snapid(std::string snap_name) const;
    int get_snap_size(std::string snap_name, uint64_t *size) const;
    void add_snap(std::string snap_name, snap_t id, uint64_t size);

    const string md_oid() const
    {
      return name + RBD_SUFFIX;
    }

    uint64_t get_image_size() const;

    void aio_read_from_cache(object_t o, bufferlist *bl, size_t len,
			     uint64_t off, Context *onfinish);
    void write_to_cache(object_t o, bufferlist& bl, size_t len, uint64_t off);
    int read_from_cache(object_t o, bufferlist *bl, size_t len, uint64_t off);
    void flush_cache();
    void shutdown_cache();
    void invalidate_cache();
  };

}

#endif
