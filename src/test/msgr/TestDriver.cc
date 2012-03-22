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
  mdriver_tracker = StateTrackerImpl::create_state_tracker(MESSENGER_DRIVER);
  MessengerDriver::build_states(mdriver_tracker);
}

// protected functions
MDriver TestDriver::create_messenger(entity_inst_t& address)
{
  Mutex::Locker locker(lock);
  SimpleMessenger *msgr = new SimpleMessenger(cct, address.name, nonce++);
  msgr->set_default_policy(Messenger::Policy::lossless_peer(0, 0));
  msgr->bind(address.addr);
  MDriver driver(new MessengerDriver(this, msgr, mdriver_tracker));
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
