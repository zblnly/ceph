/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef MSGR_TEST_FUNCTIONS_H_
#define MSGR_TEST_FUNCTIONS_H_

#include "messages/MOSDOp.h"

inline MOSDOp *generate_message(const char *message)
{
  MOSDOp *m = new MOSDOp();
  bufferlist bl;
  ::encode(message, bl);
  m->writefull(bl);
  return m;
}

/**
 * Send a test message from origin to dest and check it. They should
 * be already connected.
 */
int send_test_message(TestDriver *driver, MDriver origin, MDriver dest)
{
  // register a watch for new messages on dest
  const State *received_state =
      driver->lookup_state(MESSENGER_DRIVER, MessengerDriver::message_received);
  StateAlert message_alert = driver->generate_alert(received_state);
  dest->register_alert(message_alert);

  // send msgr2 a message
  MOSDOp *m = generate_message("send_test_message message 1");
  origin->send_message(m, dest->get_inst());

  message_alert->lock.Lock();
  while (!message_alert->is_state_reached()) {
    message_alert->cond.Wait(message_alert->lock);
  }
  message_alert->lock.Unlock();

  std::cerr << "received message " << *((MOSDOp*) message_alert->get_payload())
                      << "\nafter sending\n" << *m << std::endl;

  // check that they match
  bool match = driver->message_contents_equal(m, (Message*)message_alert->get_payload());
  return (match? 0 : 1);
}

int sample_test(TestDriver *driver)
{
  entity_name_t name = entity_name_t::OSD();
  entity_addr_t empty_addr;
  entity_inst_t entity(name, empty_addr);
  MDriver msgr1 = driver->create_messenger(entity);
  MDriver msgr2 = driver->create_messenger(entity);

  driver->connect_messengers(msgr1, msgr2);

  int ret = send_test_message(driver, msgr1, msgr2);

  driver->shutdown_messenger(msgr1);
  driver->shutdown_messenger(msgr2);
  if (ret)
    return ret;
  std::cerr << "Success sending message!" << std::endl;
  return 0;
}

#endif /* MSGR_TEST_FUNCTIONS_H_ */
