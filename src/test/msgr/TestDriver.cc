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

int TestDriver::run_tests()
{
  entity_name_t name = entity_name_t::OSD();
  entity_addr_t empty_addr;
  entity_inst_t entity(name, empty_addr);
  MDriver msgr1 = create_messenger(entity);
  MDriver msgr2 = create_messenger(entity);
  return 0;
}
// constructors
TestDriver::TestDriver() : nonce(0), lock("TestDriver::lock")
{
  cct = new CephContext(CODE_ENVIRONMENT_UTILITY);
}

// protected functions
MDriver TestDriver::create_messenger(entity_inst_t& address)
{
  Mutex::Locker locker(lock);
  SimpleMessenger *msgr = new SimpleMessenger(cct, address.name, nonce++);
  msgr->set_default_policy(Messenger::Policy::lossless_peer(0, 0));
  msgr->bind(address.addr);
  MDriver driver(new MessengerDriver(this, msgr));
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

int TestDriver::connect_messengers(MDriver origin, entity_inst_t& dest)
{
  Mutex::Locker locker(lock);
  map<entity_addr_t, MDriver>::iterator iter = driver_addresses.find(dest.addr);
  assert(iter != driver_addresses.end());

  int ret = origin->establish_connection(dest);
  if (ret) {
    return ret;
  }

  return 0;
}
