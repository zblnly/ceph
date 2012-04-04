/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef MESSENGERDRIVER_H_
#define MESSENGERDRIVER_H_

#include "msg/Messenger.h"
#include "TestDriver.h"
#include "failure_injector.h"
#include "common/StateTracker.h"

#include <boost/scoped_ptr.hpp>
#include <list>
#include <map>
#include <vector>
#include <set>

#define MESSENGER_DRIVER "MessengerDriver"

/**
 * The MessengerDriver is a fairly simple object which takes responsibility
 * for a single Messenger, does its generic setup, translates requested
 * actions from the TestDriver into specific Messenger commands, and relays
 * important event changes from the Messenger to the TestDriver.
 */
class MessengerDriver : public Dispatcher,
                        public StateTracker,
                        public FailureInjector {
  /**
   * The TestDriver which I report to.
   */
  TestDriver *driver;

  /**
   * The actual Messenger this MessengerDriver operates on.
   */
  boost::scoped_ptr<Messenger> messenger;

  /// StateMaker for my class
  const StateMaker statetracker;
  ModularStateMaker modular_maker;
public:
  CephContext *cct;
  /**
   * Construct a MessengerDriver.
   *
   * @param testdriver: The TestDriver object this MessengerDriver
   * will send reports back to.
   * @param msgr: The Messenger this MessengerDriver will handle. msgr
   * must be a valid Messenger, and any implementation-specific options must
   * be set, but the MessengerDriver will perform all of generic Messenger
   * startup and takes over the reference to it.
   * @param msgr_maker The ModularStateMaker for the Messenger we drive.
   */
  MessengerDriver(TestDriver *testdriver, Messenger *msgr,
                  StateMaker tracker, ModularStateMaker msgr_maker) :
    Dispatcher(msgr->cct),
    driver(testdriver), messenger(msgr),
    statetracker(tracker), modular_maker(msgr_maker),
    state(BUILT), lock("MessengerDriver::lock") {
    my_alerts.resize(num_states);
    messenger->tracker = this;
    messenger->failure_injector = this;
  }

  virtual ~MessengerDriver() {
    assert(state == STOPPED || state == FAILED);
    for (list<Message*>::iterator i = received_messages.begin();
        i != received_messages.end();
        ++i)
      (*i)->put();
  }

  /**
   * Initialize the MessengerDriver and its components. Call this function
   * before doing anything else after construction.
   * Once this function completes, the MessengerDriver and its Messenger
   * are ready to go and have started running.
   *
   * @return 0 on success, -errno on failure.
   */
  int init();

  /**
   * Stop the Messenger and shut everything down. This should be called once
   * before destruction, and nothing else should be called afterwards.
   *
   * @return -1 if this is currently invalid, -errno on a failure, 0 otherwise.
   */
  int stop();

  /**
   * @defgroup State flags
   * @{
   */
  enum STATE_POINTS {
    message_received = 0,
    lossy_connection_broke,
    remote_reset_connection,
    num_states
  };

  static void build_states(StateMaker tracker) {
    assert(!tracker->get_system_name().compare(MESSENGER_DRIVER));
    static const char *state_names[] =
    {
     "message received",
     "lossy connection broke",
     "remote reset connection"
    };
    for (int i = 0; i < num_states; ++i) {
      tracker->create_new_state_with_id(state_names[i], i, -1);
    }
  }

  /**
   * @defgroup Accessors
   * @{
   */
  /**
   * Get the address of the Messenger we control.
   *
   * @return A constant reference to the address.
   */
  const entity_addr_t& get_addr() { return messenger->get_myaddr(); }
  const entity_inst_t& get_inst() { return messenger->get_myinst(); }
  /**
   * @} Accessors
   */


  /**
   * @defgroup Orders
   * @{
   */
  /**
   * Send a message to the given entity. Completion of this function
   * does not guarantee delivery, but it does guarantee you get a notification
   * of either message send or connection break. (There may be other guarantees
   * depending on the Policy that applies between this entity and the recipient.)
   *
   * @param message The message to send. We take control
   * of the reference we are passed.
   * @param dest The entity to send the message to.
   *
   * @return -1 if this command is invalid (Messenger is shut down or
   * unitialized), 0 otherwise.
   */
  int send_message(Message *message, const entity_inst_t& dest);

  /**
   * Establish a connection to the given endpoint. This is not synchronous -- it
   * initiates the connection but more work may be required for it to finish. If
   * we already have an active connection, this function is currently a no-op.
   * LATER: make it tell you if the connection already exists.
   *
   * @param dest The entity to connect to.
   *
   * @return -1 if this command is invalid (Messenger is shut down or
   * unitialized), 0 otherwise.
   */
  int establish_connection(const entity_inst_t& dest);

  /**
   * Break any connection to the given entity. This is currently equivalent
   * to calling mark_down().
   * If we do not have an active connection with the given endpoint, this
   * function is a no-op.
   * LATER: make it tell you if there's no connection.
   *
   * @param other The entity to break your connection with.
   *
   * @return -ENOTCONN if there is not a connection to other, -1 if this
   * command is invalid (Messenger is shut down or uninitialized), 0 otherwise.
   */
  virtual int break_connection(const entity_inst_t& other);

  virtual int break_socket(const entity_inst_t& other);

  /**
   * Register a new alert that this MessengerDriver should report when
   * reaching the given state.
   *
   * @param alert The StateAlert to register.
   */
  virtual void register_alert(StateAlert alert);
  /**
   * @} Orders
   */

  /**
   * @defgroup Dispatcher
   * @{
   */
  virtual bool ms_dispatch(Message *m);
  virtual bool ms_handle_reset(Connection *c);
  virtual void ms_handle_remote_reset(Connection *c);
  /**
   * @} Dispatcher
   */

  /**
   * @defgroup StateTracker
   * @{
   */
  virtual StateMaker get_subsystem_maker(const char *system);
  virtual int report_state_changed(const char *system, int id, const char *state);
  virtual void report_state_changed(const char *system, int id, int state);
private:
  int create_messenger_state(StateMaker maker, const char *state);
  /**
   * @} StateTracker
   */

  /**
   * @defgroup FailureInjector
   * @{
   */
public:
  virtual int pre_fail(const char *system, long sysid);
  virtual int post_fail(const char *system, long sysid);
  /**
   * @} FailureInjector
   */

protected:
  enum STATE { BUILT, RUNNING, STOPPED, FAILED };
  STATE state;
  Mutex lock;
  list<Message *> received_messages;
  vector<list<StateAlert> > my_alerts;
  // TODO: messenger alerts should take into account the system_id they belong to.
  /// subsystem -> [state_id, list]
  map<string, map<int, list<StateAlert> > > messenger_alerts;
  /// subsystem -> [system_id, state]
  map<string, map<long, const State*> > messenger_states;
  /** system_id of Pipes that we want to break. We can't currently
   * distinguish between reader and writer breaking.
   */
  set<long> sockets_to_break;
};

#endif /* MESSENGERDRIVER_H_ */
