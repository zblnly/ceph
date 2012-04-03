/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#include "TestDriver.h"
#include "MessengerDriver.h"
#include "msg/SimpleMessenger.h"

// constructors
TestDriver::TestDriver() : nonce(0), lock("TestDriver::lock")
{
  cct = new CephContext(CODE_ENVIRONMENT_UTILITY);
  mdriver_tracker = StateMakerImpl::create_state_maker(MESSENGER_DRIVER);
  MessengerDriver::build_states(mdriver_tracker);
  msgr_maker = ModularStateMakerImpl::create_modular_state_maker("SimpleMessengerMaker");
}
TestDriver::TestDriver(CephContext *context) :
    nonce(0),
    cct(context),
    lock("TestDriver::lock")
{
  mdriver_tracker = StateMakerImpl::create_state_maker(MESSENGER_DRIVER);
  MessengerDriver::build_states(mdriver_tracker);
  msgr_maker = ModularStateMakerImpl::create_modular_state_maker("SimpleMessengerMaker");
}


// protected functions
MDriver TestDriver::create_messenger(entity_inst_t& address)
{
  Mutex::Locker locker(lock);
  SimpleMessenger *msgr = new SimpleMessenger(cct, address.name, nonce++);
  msgr->set_default_policy(Messenger::Policy::lossless_peer(0, 0));
  msgr->bind(address.addr);
  MDriver driver(new MessengerDriver(this, msgr, mdriver_tracker, msgr_maker));
  driver->init();

  msgr_drivers.insert(driver);
  driver_addresses[driver->get_addr()] = driver;

  return driver;
}

int TestDriver::shutdown_messenger(MDriver msgrdriver)
{
  Mutex::Locker locker(lock);
  set<MDriver>::iterator iter = msgr_drivers.find(msgrdriver);
  assert(iter != msgr_drivers.end());
  int ret = msgrdriver->stop();
  msgr_drivers.erase(iter);
  driver_addresses.erase(msgrdriver->get_addr());
  return ret;
}

int TestDriver::connect_messengers(MDriver origin, MDriver dest)
{
  Mutex::Locker locker(lock);

  int ret = origin->establish_connection(dest->get_inst());
  if (ret) {
    return ret;
  }

  return 0;
}

StateAlert TestDriver::generate_alert(const State *state,
                                      Mutex& lock, Cond& cond)
{
  StateAlert alert(new StateAlertImpl(state, lock, cond));
  return alert;
}

StateAlert TestDriver::generate_alert(const State *state)
{
  Mutex *lock = new Mutex(state->state_name.c_str());
  Cond *cond = new Cond;
  StateAlert alert = generate_alert(state, *lock, *cond);
  alert->cond_ptr = cond;
  alert->lock_ptr = lock;
  return alert;
}

void TestDriver::clean_up()
{
  Mutex::Locker l(lock);
  map<entity_addr_t, MDriver>::iterator iter = driver_addresses.begin();
  while (iter != driver_addresses.end()){
    msgr_drivers.erase(iter->second);
    iter->second->stop();
    driver_addresses.erase(iter++);
  }
}

const State *TestDriver::lookup_state(const char *system_name, const char *state_name)
{
  if (!mdriver_tracker->get_system_name().compare(system_name)) {
    // it's the MDriver system
    int id = mdriver_tracker->retrieve_state_id(state_name);
    return mdriver_tracker->retrieve_state(id);
  }
  // it's not a StateMaker we have right now
  return NULL;
}

const State *TestDriver::lookup_state(const char *system_name, int state_id)
{
  if (!mdriver_tracker->get_system_name().compare(system_name)) {
    // it's the MDriver system
    return mdriver_tracker->retrieve_state(state_id);
  }
  // it's not a StateMaker we have right now
  return NULL;
}
