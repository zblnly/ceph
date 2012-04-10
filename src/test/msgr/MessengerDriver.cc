/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */
#include "MessengerDriver.h"
#include "msg/SimpleMessenger.h"

int MessengerDriver::init()
{
  Mutex::Locker l(lock);
  int ret = 0;
  messenger->add_dispatcher_head(this);
  ret = messenger->start();
  if (ret) { // umm, that shouldn't have happened...
    state = FAILED;
  } else {
    state = RUNNING;
  }
  return ret;
}

int MessengerDriver::stop()
{
  int ret = 0;
  if (state == RUNNING) { // turn off the Messenger
    ret = messenger->shutdown();
    if (ret) {// uh-oh, failure!? :(
      state = FAILED;
      assert(0);
      return ret;
    }
    messenger->wait();
    state = STOPPED;
  } else if (state == STOPPED) {
    ret = -1; // invalid, already stopped
  } else {
    state = STOPPED;
  }
  return ret;
}

int MessengerDriver::send_message(Message *message, const entity_inst_t& dest)
{
  if (state != RUNNING) {
    message->put();
    return -1;
  }
  return messenger->send_message(message, dest);
}

int MessengerDriver::establish_connection(const entity_inst_t& dest)
{
  if (state != RUNNING) {
    return -1;
  }
  messenger->get_connection(dest)->put(); // we don't want the ref, so put it
  return 0;
}

int MessengerDriver::break_connection(const entity_inst_t& dest)
{
  if (state != RUNNING) {
    return -1;
  }

  Connection *con = messenger->get_connection(dest);
  if (!con) {
    return -ENOTCONN;
  }
  messenger->mark_down(con);
  con->put();
  return 0;
}

int MessengerDriver::break_socket(const entity_inst_t& other, int count)
{
  if (state != RUNNING) {
    return -1;
  }

  Connection *con = messenger->get_connection(other);

  RefCountedObject * system = con->get_priv();
  if (system == NULL) {
    return -ENOENT;
  }
  system->put();
  con->put();

  return break_socket_in(long(system), count, NULL);
}

int MessengerDriver::break_socket_in(long system_id,
                                     int count,
                                     const State *break_state)
{
  if (state != RUNNING) {
    return -1;
  }
  lock.Lock();
  sockets_to_break[system_id].insert(pair<const State*, int>(break_state,count));
  lock.Unlock();
  return 0;
}

void MessengerDriver::register_alert(StateAlert alert)
{
  // right now we can't handle Messenger states
  assert(statetracker->is_my_state(alert->get_watched_state()));

  lock.Lock();
  my_alerts[alert->get_watched_state()->state_id].push_back(alert);
  lock.Unlock();
}

void MessengerDriver::register_msgr_alert(StateAlert alert, const char *system,
                                          long sysid)
{
  lock.Lock();
  messenger_alerts[system][sysid][alert->get_watched_state()->state_id].push_back(alert);
  lock.Unlock();
}

bool MessengerDriver::ms_dispatch(Message *m)
{
  lock.Lock();
  list<StateAlert>::iterator i = my_alerts[message_received].begin();
  set<StateAlert> alerts;
  while (i != my_alerts[message_received].end()) {
    alerts.insert(*i);
    my_alerts[message_received].erase(i++);
  }
  lock.Unlock();
  for (set<StateAlert>::iterator alert = alerts.begin();
      alert != alerts.end();
      ++alert) {
    (*alert)->set_state_reached(m);
  }
  return true;
}

bool MessengerDriver::ms_handle_reset(Connection *c)
{
  lock.Lock();
  list<StateAlert>::iterator i = my_alerts[lossy_connection_broke].begin();
  set<StateAlert> alerts;
  while (i != my_alerts[lossy_connection_broke].end()) {
    alerts.insert(*i);
    my_alerts[lossy_connection_broke].erase(i++);
  }
  lock.Unlock();
  for (set<StateAlert>::iterator alert = alerts.begin();
      alert != alerts.end();
      ++alert) {
    (*alert)->set_state_reached();
  }
  return true;
}

void MessengerDriver::ms_handle_remote_reset(Connection *c)
{
  lock.Lock();
  list<StateAlert>::iterator i = my_alerts[remote_reset_connection].begin();
  set<StateAlert> alerts;
  while (i != my_alerts[remote_reset_connection].end()) {
    alerts.insert(*i);
    my_alerts[remote_reset_connection].erase(i++);
  }
  lock.Unlock();
  for (set<StateAlert>::iterator alert = alerts.begin();
      alert != alerts.end();
      ++alert) {
    (*alert)->set_state_reached();
  }
}

void MessengerDriver::new_incoming(long id)
{
  lock.Lock();
  list<StateAlert>::iterator i = my_alerts[new_incoming_connection].begin();
  set<StateAlert> alerts;
  while (i != my_alerts[new_incoming_connection].end()) {
    alerts.insert(*i);
    my_alerts[new_incoming_connection].erase(i++);
  }
  lock.Unlock();
  for (set<StateAlert>::iterator alert = alerts.begin();
      alert != alerts.end();
      ++alert) {
    (*alert)->set_state_reached((void*)id);
  }
}

StateMaker MessengerDriver::get_subsystem_maker(const char *system)
{
  return modular_maker->create_maker(system);
}

// TODO: make this function support super states.
int MessengerDriver::create_messenger_state(StateMaker maker, const char *state)
{
  int super = -1;
  int state_id = maker->create_new_state(state, super);
  return state_id;
}

int MessengerDriver::report_state_changed(const char *system,
                                          long id, const char *state)
{
  StateMaker maker = get_subsystem_maker(system);
  int state_id = maker->retrieve_state_id(state);
  if (state_id < 0) {
    if (state_id == -ENOENT) {
      state_id = create_messenger_state(maker, state);
    } else {
      assert(0); // retrieve_state_id doesn't return anything else!
    }
  }
  report_state_changed(system, id, state_id);
  if (!strcmp(system, "Pipe::reader") && !strcmp(state, "create")) {
    new_incoming(id);
  }
  return state_id;
}

void MessengerDriver::report_state_changed(const char *system, long id, int state)
{
  // update the messenger state listing
  StateMaker maker = get_subsystem_maker(system);
  const State *state_obj = maker->retrieve_state(state);
  assert(state_obj != NULL);

  lock.Lock();
  messenger_states[system][id] = state_obj;

  set<StateAlert> alerts;
  // check for StateAlerts to activate
  map<string, map<long, map<int, list<StateAlert> > > >::iterator system_iter =
      messenger_alerts.find(system);
  if (system_iter != messenger_alerts.end()) {
    map<long, map<int, list<StateAlert> > >::iterator iter = system_iter->second.find(id);
    if (iter != system_iter->second.end()) {
      map<int, list<StateAlert> >::iterator state_iter =
          iter->second.find(state);
      if (state_iter != iter->second.end()) {
        list<StateAlert>::iterator alert_iter = state_iter->second.begin();
        while (alert_iter != state_iter->second.end()) {
          alerts.insert(*alert_iter);
          state_iter->second.erase(alert_iter++);
        }
      }
    }
  }
  lock.Unlock();
  for (set<StateAlert>::iterator alert = alerts.begin();
      alert != alerts.end();
      ++alert) {
    (*alert)->set_state_reached();
  }
}

int MessengerDriver::do_fail_checks(const char *system, long sysid)
{
  bool fail = false;
  Mutex::Locker l(lock);
  map<long, map<const State *, int> >::iterator iter =
      sockets_to_break.find(sysid);
  if (iter != sockets_to_break.end()) {
    // see if the current state is a break state or not
    const State *cur_state = messenger_states[system][sysid];
    map<const State *, int>::iterator state_iter =
        iter->second.find(cur_state);
    if (state_iter == iter->second.end()) {
      // see if we have a generic break on the socket
      state_iter = iter->second.find(NULL);
    }
    if (state_iter != iter->second.end()) {
      state_iter->second--;
      fail = true;
    }
    if (state_iter->second <= 0) {
      // erase that section
      iter->second.erase(state_iter);
      if (!iter->second.size()) {
        sockets_to_break.erase(iter);
      }
    }
    if (fail) {
      return -1;
    }
  }
  return 0;
}
