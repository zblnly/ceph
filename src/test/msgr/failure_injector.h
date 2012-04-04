/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef FAILURE_INJECTOR_H_
#define FAILURE_INJECTOR_H_

/**
 * A simple interface for deciding whether to fail an operation or not.
 * Given a syscall to potentially fail, call pre_fail() before doing the
 * syscall and call post_fail() after calling the syscall. If either
 * returns non-zero, return the given value.
 */
class FailureInjector {
public:
  /**
   * Call this before calling the syscall.
   * @param system The system calling into pre_fail
   * @param sysid The ID of the instance of the system calling in.
   * @return 0, or the error code you should return instead of proceeding.
   */
  virtual int pre_fail(const char *system, long sysid) = 0;
  /**
   * Call this after the syscall succeeds.
   * @param system The system calling into pre_fail
   * @param sysid The ID of the instance of the system calling in.
   * @return 0, or the error code you should return instead of succeeding.
   */
  virtual int post_fail(const char *system, long sysid) = 0;
protected:
  ~FailureInjector() {}
};
#endif /* FAILURE_INJECTOR_H_ */
