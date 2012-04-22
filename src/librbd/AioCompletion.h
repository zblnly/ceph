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

#ifndef CEPH_LIBRBD_AIOCOMPLETION_H
#define CEPH_LIBRBD_AIOCOMPLETION_H

namespace librbd {

  struct AioBlockCompletion;

  struct AioCompletion {
    Mutex lock;
    Cond cond;
    bool done;
    ssize_t rval;
    callback_t complete_cb;
    void *complete_arg;
    rbd_completion_t rbd_comp;
    int pending_count;
    int ref;
    bool released;

    AioCompletion()
      : lock("AioCompletion::lock", true),
	done(false), rval(0), complete_cb(NULL), complete_arg(NULL),
	rbd_comp(NULL), pending_count(1), ref(1), released(false) {
    }
    ~AioCompletion() {
    }

    int wait_for_complete() {
      lock.Lock();
      while (!done)
	cond.Wait(lock);
      lock.Unlock();
      return 0;
    }

    void add_block_completion(AioBlockCompletion *aio_completion) {
      lock.Lock();
      pending_count++;
      lock.Unlock();
      get();
    }

    void finish_adding_completions() {
      lock.Lock();
      assert(pending_count);
      int count = --pending_count;
      if (!count) {
	complete();
      }
      lock.Unlock();
    }

    void complete() {
      assert(lock.is_locked());
      if (complete_cb) {
	complete_cb(rbd_comp, complete_arg);
      }
      done = true;
      cond.Signal();
    }

    void set_complete_cb(void *cb_arg, callback_t cb) {
      complete_cb = cb;
      complete_arg = cb_arg;
    }

    void complete_block(AioBlockCompletion *block_completion, ssize_t r);

    ssize_t get_return_value() {
      lock.Lock();
      ssize_t r = rval;
      lock.Unlock();
      return r;
    }

    void get() {
      lock.Lock();
      assert(ref > 0);
      ref++;
      lock.Unlock();
    }
    void release() {
      lock.Lock();
      assert(!released);
      released = true;
      put_unlock();
    }
    void put() {
      lock.Lock();
      put_unlock();
    }
    void put_unlock() {
      assert(ref > 0);
      int n = --ref;
      lock.Unlock();
      if (!n)
	delete this;
    }
  };

  struct AioBlockCompletion : Context {
    CephContext *cct;
    struct AioCompletion *completion;
    uint64_t ofs;
    size_t len;
    char *buf;
    map<uint64_t,uint64_t> m;
    bufferlist data_bl;
    librados::ObjectWriteOperation write_op;

    AioBlockCompletion(CephContext *cct_, AioCompletion *aio_completion,
		       uint64_t _ofs, size_t _len, char *_buf)
      : cct(cct_), completion(aio_completion),
	ofs(_ofs), len(_len), buf(_buf) {}
    virtual ~AioBlockCompletion() {}
    virtual void finish(int r);
  };


}

#endif
