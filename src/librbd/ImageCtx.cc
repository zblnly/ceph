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

#include "ImageCtx.h"
#include "SnapInfo.h"

#include "common/dout.h"
#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd: "

namespace librbd {

ImageCtx::ImageCtx(std::string imgname, const char *snap, IoCtx& p)
  : cct((CephContext*)p.cct()),
    perfcounter(NULL),
    snapid(CEPH_NOSNAP),
    snap_exists(true),
    name(imgname),
    needs_refresh(true),
    refresh_lock("librbd::ImageCtx::refresh_lock"),
    lock("librbd::ImageCtx::lock"),
    cache_lock("librbd::ImageCtx::cache_lock"),
    object_cacher(NULL), writeback_handler(NULL), object_set(NULL)
{
  md_ctx.dup(p);
  data_ctx.dup(p);

  string pname = string("librbd-") + data_ctx.get_pool_name() + string("/") + name;
  if (snap) {
    snapname = snap;
    pname += "@";
    pname += snapname;
  }
  perf_start(pname);

  if (cct->_conf->rbd_cache) {
    Mutex::Locker l(cache_lock);
    ldout(cct, 20) << "enabling writback caching..." << dendl;
    writeback_handler = new LibrbdWriteback(data_ctx, cache_lock);
    object_cacher = new ObjectCacher(cct, pname, *writeback_handler, cache_lock,
				     NULL, NULL);
    object_set = new ObjectCacher::ObjectSet(NULL, data_ctx.get_id(), 0);
    object_cacher->start();
  }
}

ImageCtx::~ImageCtx()
{
  perf_stop();
  if (object_cacher) {
    delete object_cacher;
    object_cacher = NULL;
  }
  if (writeback_handler) {
    delete writeback_handler;
    writeback_handler = NULL;
  }
  if (object_set) {
    delete object_set;
    object_set = NULL;
  }
}

void ImageCtx::perf_start(string name)
{
  PerfCountersBuilder plb(cct, name, l_librbd_first, l_librbd_last);

  plb.add_u64_counter(l_librbd_rd, "rd");
  plb.add_u64_counter(l_librbd_rd_bytes, "rd_bytes");
  plb.add_u64_counter(l_librbd_wr, "wr");
  plb.add_u64_counter(l_librbd_wr_bytes, "wr_bytes");
  plb.add_u64_counter(l_librbd_discard, "discard");
  plb.add_u64_counter(l_librbd_discard_bytes, "discard_bytes");
  plb.add_u64_counter(l_librbd_flush, "flush");
  plb.add_u64_counter(l_librbd_aio_rd, "aio_rd");
  plb.add_u64_counter(l_librbd_aio_rd_bytes, "aio_rd_bytes");
  plb.add_u64_counter(l_librbd_aio_wr, "aio_wr");
  plb.add_u64_counter(l_librbd_aio_wr_bytes, "aio_wr_bytes");
  plb.add_u64_counter(l_librbd_aio_discard, "aio_discard");
  plb.add_u64_counter(l_librbd_aio_discard_bytes, "aio_discard_bytes");
  plb.add_u64_counter(l_librbd_snap_create, "snap_create");
  plb.add_u64_counter(l_librbd_snap_remove, "snap_remove");
  plb.add_u64_counter(l_librbd_snap_rollback, "snap_rollback");
  plb.add_u64_counter(l_librbd_notify, "notify");
  plb.add_u64_counter(l_librbd_resize, "resize");

  perfcounter = plb.create_perf_counters();
  cct->get_perfcounters_collection()->add(perfcounter);
}

void ImageCtx::perf_stop()
{
  assert(perfcounter);
  cct->get_perfcounters_collection()->remove(perfcounter);
  delete perfcounter;
}

int ImageCtx::snap_set(std::string snap_name)
{
  std::map<std::string, struct SnapInfo>::iterator it = snaps_by_name.find(snap_name);
  if (it != snaps_by_name.end()) {
    snapname = snap_name;
    snapid = it->second.id;
    return 0;
  }
  return -ENOENT;
}

void ImageCtx::snap_unset()
{
  snapid = CEPH_NOSNAP;
  snapname = "";
}

snap_t ImageCtx::get_snapid(std::string snap_name) const
{
  std::map<std::string, struct SnapInfo>::const_iterator it = snaps_by_name.find(snap_name);
  if (it != snaps_by_name.end())
    return it->second.id;
  return CEPH_NOSNAP;
}

int ImageCtx::get_snap_size(std::string snap_name, uint64_t *size) const
{
  std::map<std::string, struct SnapInfo>::const_iterator it = snaps_by_name.find(snap_name);
  if (it != snaps_by_name.end()) {
    *size = it->second.size;
    return 0;
  }
  return -ENOENT;
}

void ImageCtx::add_snap(std::string snap_name, snap_t id, uint64_t size)
{
  snapc.snaps.push_back(id);
  snaps.push_back(id);
  struct SnapInfo info(id, size);
  snaps_by_name.insert(std::pair<std::string, struct SnapInfo>(snap_name, info));
}

uint64_t ImageCtx::get_image_size() const
{
  if (snapname.length() == 0) {
    return header.image_size;
  } else {
    map<std::string,SnapInfo>::const_iterator p = snaps_by_name.find(snapname);
    if (p == snaps_by_name.end())
      return 0;
    return p->second.size;
  }
}

void ImageCtx::aio_read_from_cache(object_t o, bufferlist *bl, size_t len,
				   uint64_t off, Context *onfinish)
{
  lock.Lock();
  ObjectCacher::OSDRead *rd = object_cacher->prepare_read(snapid, bl, 0);
  lock.Unlock();
  ObjectExtent extent(o, off, len);
  extent.oloc.pool = data_ctx.get_id();
  extent.buffer_extents[0] = len;
  rd->extents.push_back(extent);
  cache_lock.Lock();
  int r = object_cacher->readx(rd, object_set, onfinish);
  cache_lock.Unlock();
  if (r > 0)
    onfinish->complete(r);
}

void ImageCtx::write_to_cache(object_t o, bufferlist& bl, size_t len, uint64_t off)
{
  lock.Lock();
  ObjectCacher::OSDWrite *wr = object_cacher->prepare_write(snapc, bl,
							    utime_t(), 0);
  lock.Unlock();
  ObjectExtent extent(o, off, len);
  extent.oloc.pool = data_ctx.get_id();
  extent.buffer_extents[0] = len;
  wr->extents.push_back(extent);
  {
    Mutex::Locker l(cache_lock);
    object_cacher->wait_for_write(len, cache_lock);
    object_cacher->writex(wr, object_set);
  }
}

int ImageCtx::read_from_cache(object_t o, bufferlist *bl, size_t len, uint64_t off)
{
  int r;
  Mutex mylock("librbd::ImageCtx::read_from_cache");
  Cond cond;
  bool done;
  Context *onfinish = new C_SafeCond(&mylock, &cond, &done, &r);
  aio_read_from_cache(o, bl, len, off, onfinish);
  mylock.Lock();
  while (!done)
    cond.Wait(mylock);
  mylock.Unlock();
  return r;
}

void ImageCtx::flush_cache()
{
  int r;
  Mutex mylock("librbd::ImageCtx::flush_cache");
  Cond cond;
  bool done;
  Context *onfinish = new C_SafeCond(&mylock, &cond, &done, &r);
  cache_lock.Lock();
  bool already_flushed = object_cacher->commit_set(object_set, onfinish);
  cache_lock.Unlock();
  if (!already_flushed) {
    mylock.Lock();
    while (!done) {
      ldout(cct, 20) << "waiting for cache to be flushed" << dendl;
      cond.Wait(mylock);
    }
    mylock.Unlock();
    ldout(cct, 20) << "finished flushing cache" << dendl;
  }
}

void ImageCtx::shutdown_cache()
{
  lock.Lock();
  invalidate_cache();
  lock.Unlock();
  object_cacher->stop();
}

void ImageCtx::invalidate_cache()
{
  assert(lock.is_locked());
  if (!object_cacher)
    return;
  cache_lock.Lock();
  object_cacher->release_set(object_set);
  cache_lock.Unlock();
  flush_cache();
  cache_lock.Lock();
  bool unclean = object_cacher->release_set(object_set);
  cache_lock.Unlock();
  assert(!unclean);
}


}
