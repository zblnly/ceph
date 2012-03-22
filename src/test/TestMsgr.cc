/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 * Do a really stupid test of the messenger system with the
 * TestDriver interface.
 */

#include "msgr/TestDriver.h"
#include "msgr/MessengerDriver.h"

#include "messages/MOSDOp.h"

int sample_test(TestDriver *driver)
{
  entity_name_t name = entity_name_t::OSD();
  entity_addr_t empty_addr;
  entity_inst_t entity(name, empty_addr);
  MDriver msgr1 = driver->create_messenger(entity);
  MDriver msgr2 = driver->create_messenger(entity);
  entity_inst_t msgr2_ent = msgr2->get_inst();

  // register a watch for new messages on msgr2
  Mutex lock("TestDriver::run_tests::lock");
  const State *received_state =
      mdriver_tracker->retrieve_state(MessengerDriver::message_received);
  StateAlert message_alert(new StateAlertImpl(received_state, lock));
  msgr2->register_alert(message_alert);

  // send msgr2 a message
  MOSDOp *m = new MOSDOp();
  bufferlist bl;
  ::encode("test message 1", bl);
  m->writefull(bl);
  driver->connect_messengers(msgr1, msgr2_ent);
  msgr1->send_message(m, msgr2->get_inst());

  lock.Lock();
  while (!message_alert->is_state_reached()) {
    message_alert->cond.Wait(lock);
  }
  lock.Unlock();

  std::cerr << "received message " << *((MOSDOp*) message_alert->get_payload())
                      << "\nafter sending\n" << *m << std::endl;

  // check that they match
  bool match = driver->message_contents_equal(m, (Message*)message_alert->get_payload());

  driver->shutdown_messenger(msgr1);
  driver->shutdown_messenger(msgr2);
  if (!match)
    return 1;
  std::cerr << "Success!" << "sizes are " << m->get_payload().length()
                << m->get_data().length() << m->get_middle().length() << std::endl;
  return 0;
}

int main (int argc, const char **argv)
{
  TestDriver driver;
  return sample_test(&driver);
}
