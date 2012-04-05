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

  // check that they match
  bool match = driver->message_contents_equal(m, (Message*)message_alert->get_payload());
  return (match? 0 : 1);
}

/**
 * Send a test message from origin to dest and check it. They should
 * be already connected.
 */
int send_test_message_with_break(TestDriver *driver, MDriver origin, MDriver dest)
{
  // register a watch for new messages on dest
  const State *received_state =
      driver->lookup_state(MESSENGER_DRIVER, MessengerDriver::message_received);
  StateAlert message_alert = driver->generate_alert(received_state);
  dest->register_alert(message_alert);

  // break the connection
  dest->break_socket(origin->get_inst(), 1);
  // send dest a message
  MOSDOp *m = generate_message("send_test_message message 1");
  origin->send_message(m, dest->get_inst());

  wait_until_complete(message_alert);

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

  origin->send_message(sent_m, dest->get_inst());
  wait_until_complete(message_alert);

  // check that they match
  bool match = driver->message_contents_equal(sent_m, (Message*)message_alert->get_payload());
  return (match? 0 : 1);
}

int test_break_in_accept(TestDriver *driver)
{
  entity_name_t name = entity_name_t::OSD();
  entity_addr_t empty_addr;
  entity_inst_t entity(name, empty_addr);
  MDriver msgr1 = driver->create_messenger(entity);
  MDriver msgr2 = driver->create_messenger(entity);

  // create an alert for when msgr2 first creates a Pipe for incoming Connections
  const State *new_incoming_state =
      driver->lookup_state(MESSENGER_DRIVER, MessengerDriver::new_incoming_connection);
  StateAlert new_incoming_alert = driver->generate_alert(new_incoming_state);
  // make the alerter block in the notification until we signal it to continue
  new_incoming_alert->require_signal_to_resume();
  msgr2->register_alert(new_incoming_alert);

  // we want to break the socket once Pipe::accept is done.
  // there's a state report for "accept::open" that's at the right place
  // so we retrieve a reference to it
  const State *accept_open_state =
      driver->lookup_state("Pipe::reader", "accept::open");
  // and we also want to make sure it actually breaks, which is
  // a marked state too
  const State *fail_unlocked_state =
      driver->lookup_state("Pipe::reader", "accept::fail_unlocked");
  StateAlert fail_unlocked_alert = driver->generate_alert(fail_unlocked_state);

  // start a connection from msgr1 to msgr2
  msgr1->establish_connection(msgr2->get_inst());
  // wait for msgr2 to notice that it has an incoming connection
  wait_until_complete(new_incoming_alert);
  // get out the system id for the new Pipe
  long new_pipe_id = (long) new_incoming_alert->get_payload();
  // instruct msgr2 to break on socket syscalls once it reaches the
  // "accept::open" state
  msgr2->break_socket_in(new_pipe_id, 1, accept_open_state);
  msgr2->register_msgr_alert(fail_unlocked_alert, "Pipe::reader", new_pipe_id);
  new_incoming_alert->cond.SignalAll();
  // and wait until we hit the fail_unlocked alert, since we should
  wait_until_complete(fail_unlocked_alert);

  // make sure the connection still works
  return send_test_message(driver, msgr1, msgr2);
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

  std::cerr << "Sending message after breaking socket..." << std::endl;
  ret = send_test_message_with_break(driver, msgr1, msgr2);
  if (ret)
    goto shutdown;
  std::cerr << "Success!" << std::endl;

  std::cerr << "Sending message after breaking socket in accept..." << std::endl;
  ret = test_break_in_accept(driver);
  if (ret)
    goto shutdown;
  std::cerr << "Success!" << std::endl;

shutdown:
  driver->clean_up();
  return ret;
}

#endif /* MSGR_TEST_FUNCTIONS_H_ */
