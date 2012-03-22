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
#include <pthread.h>

#include "msg/Messenger.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include "common/code_environment.h"
#include "common/StateTracker.h"

class MessengerDriver;
class TestDriver;
typedef std::tr1::shared_ptr<MessengerDriver> MDriver;

/**
 * The TestDriver defines the interface for communicating with the
 * MessengerDriver, and implements the interface for MessengerDrivers to report
 * important events back.
 */
class TestDriver {
public:
  TestDriver();
  ~TestDriver(){};
private:
  set<MDriver> msgr_drivers;
  int nonce;
  CephContext *cct;
  Mutex lock;
  map<entity_addr_t, MDriver> driver_addresses;
  StateTracker mdriver_tracker;

  /**
   * @defgroup Orders
   * @{
   */
public:
  /**
   * Create a new Messenger (and MessengerDriver for it), bound to the
   * given address. Do initialization, and return the MessengerDriver.
   *
   * @param entity The entity type and address (optional) for the Messenger.
   * @return An MDriver reference to the newly-created Messenger[Driver].
   */
  MDriver create_messenger(entity_inst_t& address);
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
   * @param dest The MessengerDriver whose Messenger should be connected to.
   * @return -1 if this is invalid at this time (uninitialized), 0 otherwise.
   */
  int connect_messengers(MDriver origin, MDriver dest);
  /**
   * @} Orders
   */

  /**
   * Check whether two messages have equal contents.
   *
   * @return True if all the encoded bits match, false otherwise.
   */
  bool message_contents_equal(Message *m1, Message *m2) {
    return m1->get_payload().contents_equal(m2->get_payload()) &&
        m1->get_data().contents_equal(m2->get_data()) &&
        m1->get_middle().contents_equal(m2->get_middle());
  }
};

/**
 * The StateAlertImpl (which you should always reference as a shared_ptr, which
 * is typedefed to StateAlert) specifies a state to alert on, provides a Cond
 * to wait and alert on, and exposes a void *payload.
 *
 * The object reporting the state (probably a MessengerDriver) should call
 * set_state_reached exactly once, and if it has a payload, pass it there.
 * The object watching state should only look at payload once is_state_reached()
 * returns true.
 * Use a loop running on is_state_reached() and waiting on the Cond to wait until
 * the StateAlert has been filled in.
 *
 * LATER: extend it briefly so that we can force the MessengerDriver to wait
 * for permission from the TestDriver to continue running?
 */
class StateAlertImpl {
  const State *state;
  bool state_reached;
  void *payload;
  Mutex& lock;
public:
  Cond cond;

  StateAlertImpl(const State *s, Mutex &_lock) : state(s),
      state_reached(0), lock(_lock)
  {}
  ~StateAlertImpl() {}

  const State *get_watched_state() { return state; }
  void set_state_reached(void *payload=NULL) {
    lock.Lock();
    state_reached = true;
    this->payload = payload;
    cond.SignalAll();
    lock.Unlock();
  }
  // hold the lock when calling this function
  bool is_state_reached() {
    return state_reached;
  }
  void *get_payload() { return payload; }
};
typedef std::tr1::shared_ptr<StateAlertImpl> StateAlert;

#endif /* TESTDRIVER_H_ */
