/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef TESTDRIVER_H_
#define TESTDRIVER_H_

#include <set>

#include "msg/Messenger.h"
#include "common/Mutex.h"
#include "common/code_environment.h"

class MessengerDriver;
typedef std::tr1::shared_ptr<MessengerDriver> MDriver;

/**
 * The TestDriver defines the interface for communicating with the
 * MessengerDriver, and implements the interface for MessengerDrivers to report
 * important events back. It's expected that actual tests will consist of
 * subclasses which override the default run_test() function.
 */
class TestDriver {
public:
  /**
   * Run this TestDriver's sequence of tests.
   *
   * @return 0 on success, -1 if the tests don't pass.
   */
  virtual int run_tests();

  TestDriver();
  virtual ~TestDriver(){};
protected:
  set<MDriver> msgr_drivers;
  int nonce;
  CephContext *cct;
  Mutex lock;
  map<entity_addr_t, MDriver> driver_addresses;

  /**
   * @defgroup Orders
   * @{
   */
  /**
   * Create a new Messenger (and MessengerDriver for it), bound to the
   * given address. Do initialization, and return the MessengerDriver.
   *
   * @param entity The entity type and address (optional) for the Messenger.
   * @return An MDriver reference to the newly-created Messenger[Driver].
   */
  virtual MDriver create_messenger(entity_inst_t& address);
  /**
   * Shut down the Messenger associated with this MessengerDriver. This
   * does not free all the resources allocated to either one,
   * but it will free up the network resources and reject
   * future orders to the MessengerDriver.
   *
   * @return 0 for success; -1 if the call was invalid; -errno otherwise.
   */
  int shutdown_messenger(MDriver msgrdriver);
  /**
   * Tell the origin MessengerDriver to connect to dest. Note that if it's
   * already connected this won't do anything.
   * LATER: report back if already connected, instead of silent success.
   *
   * @param origin The MessengerDriver which should initiate the connection.
   * @param dest The address to connect to.
   * @return -1 if this is invalid at this time (uninitialized), 0 otherwise.
   */
  virtual int connect_messengers(MDriver origin, entity_inst_t& dest);
  /**
   * @} Orders
   */
};

#endif /* TESTDRIVER_H_ */
