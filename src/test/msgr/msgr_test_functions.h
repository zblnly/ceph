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

inline void wait_until_complete(StateAlert alert)
{
  alert->lock.Lock();
  while (!alert->is_state_reached())
    alert->cond.Wait(alert->lock);
  alert->lock.Unlock();
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

  wait_until_complete(message_alert);

  std::cerr << "received message " << *((MOSDOp*) message_alert->get_payload())
                      << "\nafter sending\n" << *m << std::endl;

  // check that they match
  bool match = driver->message_contents_equal(m, (Message*)message_alert->get_payload());
  return (match? 0 : 1);
}

/**
 * Break the connection, and send a message. We expect this to fail
 * and to receive a notification on remote_reset_connection.
 * MDrivers should already be connected.
 *
 * It depends on the current implementation of mark_down, which is used
 * by break_connection.
 */
int test_broken_connection(TestDriver *driver, MDriver origin, MDriver dest)
{
  int ret = dest->break_connection(origin->get_inst());
  if (ret)
    return ret;
  // register a watch for remote reset on origin
  const State *remote_reset_state =
      driver->lookup_state(MESSENGER_DRIVER, MessengerDriver::remote_reset_connection);
  StateAlert reset_alert = driver->generate_alert(remote_reset_state);
  origin->register_alert(reset_alert);
  //register a watch for received messages on dest
  const State *received_state =
      driver->lookup_state(MESSENGER_DRIVER, MessengerDriver::message_received);
  StateAlert message_alert = driver->generate_alert(received_state);
  dest->register_alert(message_alert);


  MOSDOp *unsent_m = generate_message("I won't be sent");
  MOSDOp *sent_m = generate_message("I will be sent");

  origin->send_message(unsent_m, dest->get_inst());
  wait_until_complete(reset_alert);

  std::cerr << "got reset alert" << std::endl;

  origin->send_message(sent_m, dest->get_inst());
  wait_until_complete(message_alert);

  std::cerr << "received message " << *((MOSDOp*) message_alert->get_payload())
                      << "\nafter sending\n" << *sent_m << std::endl;

  // check that they match
  bool match = driver->message_contents_equal(sent_m, (Message*)message_alert->get_payload());
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

  std::cerr << "Sending test message..." << std::endl;
  int ret = send_test_message(driver, msgr1, msgr2);

  if (ret)
    goto shutdown;
  std::cerr << "Success!" << std::endl;

  std::cerr << "Sending message after breaking connection..." << std::endl;
  ret = test_broken_connection(driver, msgr1, msgr2);
  if (ret)
    goto shutdown;
  std::cerr << "Success!" << std::endl;

shutdown:
  driver->clean_up();
  return ret;
}

#endif /* MSGR_TEST_FUNCTIONS_H_ */
