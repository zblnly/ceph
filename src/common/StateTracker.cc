/*
 * Ceph - scalable distributed file system
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#include "StateTracker.h"
#include <errno.h>

void StateMakerImpl::_allocate_state(const char *state_name, int id,
                                  int superstate)
{
  state_names.resize(id+1);
  states.resize(id+1);
  state_names[id] = state_name;
  State *state = new State(this, id, state_names[id]);
  states[id] = state;
  state_name_map[state_name] = id;

  if (superstate >= 0) {
    State *super = states[superstate];
    state->superstate = super;
    super->substate_lock.Lock();
    super->substates.push_back(id);
    super->substate_lock.Unlock();
  }

  assert(state_names.size() == states.size());
  assert(state_names.size() == state_name_map.size());
  assert(state_names.size() == (unsigned) state_id_alloc);
}

int StateMakerImpl::create_new_state(const char *state_name, int superstate)
{
  Mutex::Locker locker(lock);
  if (state_name_map.count(state_name)) {
    return -EEXIST;
  }
  if (superstate >= 0) {
    if (superstate >= state_id_alloc ||
        states[superstate] == NULL) {
      return -ENOENT;
    }
  }
  int id = state_id_alloc++;
  _allocate_state(state_name, id, superstate);
  return id;
}

int StateMakerImpl::create_new_state_with_id(const char *state_name, int id,
                                           int superstate)
{
  Mutex::Locker locker(lock);
  if (state_name_map.count(state_name)) {
    return -EEXIST;
  }
  if (superstate >= 0) {
    if (superstate >= state_id_alloc ||
        states[superstate] == NULL) {
      return -ENOENT;
    }
  }
  if (id < state_id_alloc) {
    return -EINVAL;
  }
  state_id_alloc = id + 1;
  _allocate_state(state_name, id, superstate);
  return id;
}

const State *StateMakerImpl::retrieve_state (int id) const
{
  Mutex::Locker locker(lock);
  if (id >= state_id_alloc) {
    return NULL;
  }
  return states[id];
}

int StateMakerImpl::retrieve_state_id(const char *name) const
{
  Mutex::Locker locker(lock);
  std::map<std::string, int>::const_iterator iter = state_name_map.find(name);
  if (iter != state_name_map.end()) {
    return iter->second;
  }
  return -ENOENT;
}

StateMaker ModularStateMakerImpl::create_maker(const char *module)
{
  Mutex::Locker locker(lock);
  std::map<const char *, StateMaker>::iterator iter = modules.find(module);
  if (iter != modules.end()) {
    return iter->second;
  }
  StateMaker module_maker = StateMakerImpl::create_state_maker(module);
  modules[module] = module_maker;
  return module_maker;
}

StateMaker ModularStateMakerImpl::get_maker(const char *module)
{
  Mutex::Locker locker(lock);
  std::map<const char *, StateMaker>::iterator iter = modules.find(module);
  if (iter != modules.end()) {
    return iter->second;
  }
  return StateMaker();
}
