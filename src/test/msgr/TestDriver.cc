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
#include "messages/MOSDOp.h"

int TestDriver::run_tests()
{
  entity_name_t name = entity_name_t::OSD();
  entity_addr_t empty_addr;
  entity_inst_t entity(name, empty_addr);
  MDriver msgr1 = create_messenger(entity);
  MDriver msgr2 = create_messenger(entity);
  entity_inst_t msgr2_ent = msgr2->get_inst();

  // register a watch for new messages on msgr2
  Mutex lock("TestDriver::run_tests::lock");
  const StateTrackerImpl::State *received_state =
      mdriver_tracker->retrieve_state(MessengerDriver::message_received);
  StateAlert message_alert(new StateAlertImpl(received_state, lock));
  msgr2->register_alert(message_alert);

  // send msgr2 a message
  MOSDOp *m = new MOSDOp();
  bufferlist bl;
  ::encode("test message 1", bl);
  m->writefull(bl);
  connect_messengers(msgr1, msgr2_ent);
  msgr1->send_message(m, msgr2->get_inst());

  lock.Lock();
  while (!message_alert->is_state_reached()) {
    message_alert->cond.Wait(lock);
  }
  lock.Unlock();

  std::cerr << "received message " << *((MOSDOp*) message_alert->get_payload())
            << "\nafter sending\n" << *m << std::endl;

  // check that they match
  bool match = message_contents_equal(m, (Message*)message_alert->get_payload());

  shutdown_messenger(msgr1);
  shutdown_messenger(msgr2);
  if (!match)
    return 1;
  std::cerr << "Success!" << "sizes are " << m->get_payload().length()
      << m->get_data().length() << m->get_middle().length() << std::endl;
  return 0;
}
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
