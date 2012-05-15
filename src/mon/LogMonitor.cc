// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#include "LogMonitor.h"
#include "Monitor.h"
#include "MonitorStore.h"

#include "messages/MMonCommand.h"
#include "messages/MLog.h"
#include "messages/MLogAck.h"

#include "common/Timer.h"

#include "osd/osd_types.h"

#include "common/config.h"
#include <sstream>

#define dout_subsys ceph_subsys_mon
#undef dout_prefix
#define dout_prefix _prefix(_dout, mon, paxos->get_version())
static ostream& _prefix(std::ostream *_dout, Monitor *mon, version_t v) {
  return *_dout << "mon." << mon->name << "@" << mon->rank
		<< "(" << mon->get_state_name()
		<< ").log v" << v << " ";
}

ostream& operator<<(ostream& out, LogMonitor& pm)
{
  std::stringstream ss;
  /*
  for (hash_map<int,int>::iterator p = pm.pg_map.num_pg_by_state.begin();
       p != pm.pg_map.num_pg_by_state.end();
       ++p) {
    if (p != pm.pg_map.num_pg_by_state.begin())
      ss << ", ";
    ss << p->second << " " << pg_state_string(p->first);
  }
  string states = ss.str();
  return out << "v" << pm.pg_map.version << ": "
	     << pm.pg_map.pg_stat.size() << " pgs: "
	     << states << "; "
	     << kb_t(pm.pg_map.total_pg_kb()) << " data, " 
	     << kb_t(pm.pg_map.total_used_kb()) << " used, "
	     << kb_t(pm.pg_map.total_avail_kb()) << " / "
	     << kb_t(pm.pg_map.total_kb()) << " free";
  */
  return out << "log";
}

/*
 Tick function to update the map based on performance every N seconds
*/

void LogMonitor::tick() 
{
  if (!paxos->is_active()) return;

  update_from_paxos();
  dout(10) << *this << dendl;

  if (!mon->is_leader()) return; 

}

void LogMonitor::create_initial()
{
  dout(10) << "create_initial -- creating initial map" << dendl;
  LogEntry e;
  memset(&e.who, 0, sizeof(e.who));
  e.stamp = ceph_clock_now(g_ceph_context);
  e.type = CLOG_INFO;
  std::stringstream ss;
  ss << "mkfs " << mon->monmap->get_fsid();
  e.msg = ss.str();
  e.seq = 0;
  pending_log.insert(pair<utime_t,LogEntry>(e.stamp, e));
}

void LogMonitor::update_from_paxos()
{
  version_t paxosv = paxos->get_version();
  if (paxosv == summary.version)
    return;
  assert(paxosv >= summary.version);

  bufferlist blog;
  bufferlist blogdebug;
  bufferlist bloginfo;
  bufferlist blogwarn;
  bufferlist blogerr;
  bufferlist blogsec;

  if (summary.version != paxos->get_stashed_version()) {
    bufferlist latest;
    version_t v = paxos->get_stashed(latest);
    dout(7) << "update_from_paxos loading summary e" << v << dendl;
    bufferlist::iterator p = latest.begin();
    ::decode(summary, p);
  } 

  // walk through incrementals
  while (paxosv > summary.version) {
    bufferlist bl;
    bool success = paxos->read(summary.version+1, bl);
    assert(success);

    bufferlist::iterator p = bl.begin();
    __u8 v;
    ::decode(v, p);
    while (!p.end()) {
      LogEntry le;
      le.decode(p);
      dout(7) << "update_from_paxos applying incremental log " << summary.version+1 <<  " " << le << dendl;

      stringstream ss;
      ss << le;
      string s;
      getline(ss, s);
      s += "\n";

      blog.append(s);
      if (le.type >= CLOG_DEBUG)
	blogdebug.append(s);
      if (le.type >= CLOG_INFO)
	bloginfo.append(s);
      if (le.type == CLOG_SEC)
        blogsec.append(s);
      if (le.type >= CLOG_WARN)
	blogwarn.append(s);
      if (le.type >= CLOG_ERROR)
	blogerr.append(s);

      summary.add(le);
    }

    summary.version++;
  }

  bufferlist bl;
  ::encode(summary, bl);
  paxos->stash_latest(paxosv, bl);

  if (blog.length())
    mon->store->append_bl_ss(blog, "log", NULL);
  if (blogdebug.length())
    mon->store->append_bl_ss(blogdebug, "log.debug", NULL);
  if (bloginfo.length())
    mon->store->append_bl_ss(bloginfo, "log.info", NULL);
  if (blogsec.length())
    mon->store->append_bl_ss(bloginfo, "log.security", NULL);
  if (blogwarn.length())
    mon->store->append_bl_ss(blogwarn, "log.warn", NULL);
  if (blogerr.length())
    mon->store->append_bl_ss(blogerr, "log.err", NULL);


  // trim
  unsigned max = g_conf->mon_max_log_epochs;
  if (mon->is_leader() && paxosv > max)
    paxos->trim_to(paxosv - max);
}

void LogMonitor::create_pending()
{
  pending_log.clear();
  pending_summary = summary;
  dout(10) << "create_pending v " << (paxos->get_version() + 1) << dendl;
}

void LogMonitor::encode_pending(bufferlist &bl)
{
  dout(10) << "encode_pending v " << (paxos->get_version() + 1) << dendl;
  __u8 v = 1;
  ::encode(v, bl);
  for (multimap<utime_t,LogEntry>::iterator p = pending_log.begin();
       p != pending_log.end();
       p++)
    p->second.encode(bl);
}

bool LogMonitor::preprocess_query(PaxosServiceMessage *m)
{
  dout(10) << "preprocess_query " << *m << " from " << m->get_orig_source_inst() << dendl;
  switch (m->get_type()) {
  case MSG_MON_COMMAND:
    return preprocess_command((MMonCommand*)m);

  case MSG_LOG:
    return preprocess_log((MLog*)m);

  default:
    assert(0);
    m->put();
    return true;
  }
}

bool LogMonitor::prepare_update(PaxosServiceMessage *m)
{
  dout(10) << "prepare_update " << *m << " from " << m->get_orig_source_inst() << dendl;
  switch (m->get_type()) {
  case MSG_MON_COMMAND:
    return prepare_command((MMonCommand*)m);
  case MSG_LOG:
    return prepare_log((MLog*)m);
  default:
    assert(0);
    m->put();
    return false;
  }
}

bool LogMonitor::preprocess_log(MLog *m)
{
  dout(10) << "preprocess_log " << *m << " from " << m->get_orig_source() << dendl;
  int num_new = 0;

  MonSession *session = m->get_session();
  if (!session)
    goto done;
  if (!session->caps.check_privileges(PAXOS_LOG, MON_CAP_X)) {
    dout(0) << "preprocess_log got MLog from entity with insufficient privileges "
	    << session->caps << dendl;
    goto done;
  }
  
  for (deque<LogEntry>::iterator p = m->entries.begin();
       p != m->entries.end();
       p++) {
    if (!pending_summary.contains(p->key()))
      num_new++;
  }
  if (!num_new) {
    dout(10) << "  nothing new" << dendl;
    goto done;
  }

  return false;

 done:
  m->put();
  return true;
}

bool LogMonitor::prepare_log(MLog *m) 
{
  dout(10) << "prepare_log " << *m << " from " << m->get_orig_source() << dendl;

  if (m->fsid != mon->monmap->fsid) {
    dout(0) << "handle_log on fsid " << m->fsid << " != " << mon->monmap->fsid << dendl;
    m->put();
    return false;
  }

  for (deque<LogEntry>::iterator p = m->entries.begin();
       p != m->entries.end();
       p++) {
    dout(10) << " logging " << *p << dendl;
    if (!pending_summary.contains(p->key())) {
      pending_summary.add(*p);
      pending_log.insert(pair<utime_t,LogEntry>(p->stamp, *p));
    }
  }

  paxos->wait_for_commit(new C_Log(this, m));
  return true;
}

void LogMonitor::_updated_log(MLog *m)
{
  dout(7) << "_updated_log for " << m->get_orig_source_inst() << dendl;
  mon->send_reply(m, new MLogAck(m->fsid, m->entries.rbegin()->seq));
  check_subs(m);

  m->put();
}



bool LogMonitor::preprocess_command(MMonCommand *m)
{
  int r = -1;
  bufferlist rdata;
  stringstream ss;

  if (r != -1) {
    string rs;
    getline(ss, rs);
    mon->reply_command(m, r, rs, rdata, paxos->get_version());
    return true;
  } else
    return false;
}


bool LogMonitor::prepare_command(MMonCommand *m)
{
  stringstream ss;
  string rs;
  int err = -EINVAL;

  // nothing here yet
  ss << "unrecognized command";

  getline(ss, rs);
  mon->reply_command(m, err, rs, paxos->get_version());
  return false;
}

void LogMonitor::check_subs(MLog *m)
{
/*
  map<string, int> types;
  types["log-debug"]  = CLOG_DEBUG;
  types["log-info"]   = CLOG_INFO;
  types["log-sec"]    = CLOG_SEC;
  types["log-warn"]   = CLOG_WARN;
  types["log-error"]  = CLOG_ERROR;
*/

  map<int, string> types;
  types[CLOG_DEBUG] = "log-debug";
  types[CLOG_INFO]  = "log-info";
  types[CLOG_SEC]   = "log-sec";
  types[CLOG_WARN]  = "log-warn";
  types[CLOG_ERROR] = "log-error";

  std::deque<LogEntry>::iterator it = m->entries.begin();

  for (; it != m->entries.end(); it++) {
    LogEntry e = *it;

    if (!mon->session_map.subs.count(types[e.type]))
      continue;

    xlist<Subscription*> *subs = mon->session_map.subs[types[e.type]];
    xlist<Subscription*>::iterator subs_it = subs->begin();
    for (; !subs_it.end(); ++subs_it) {
      Subscription *s = *subs_it;
      MLog *msg = new MLog(m->fsid);
      msg->entries.push_back(e);
      mon->messenger->send_message(msg, s->session->inst);

      if (s->onetime)
	mon->session_map.remove_sub(s);
    }
  }

#if 0
  map<string, xlist<Subscription*> >::iterator s;
  s = mon->session_map.subs.begin();

  map<int, xlist<Subscription*>> subs;

  for (; s != mon->session_map.subs.end(); s++) {
    if (s->first.substr(0,3) != "log")
      continue;

    /* get these subscriptions' log level */
    map<string, int>::iterator f;
    f = types.find(it->first);
    if (f == types.end())
      continue;
    int level = f->second;

    if (!subs.exist(level)) {
      subs[level] = new xlist<Subscription*>;
    }
    for (xlist<Subscription*>::iterator i = s->second.begin();
	i != s->second.end(); i++) {
      subs[level]->push_back(*i);
    }
  }

  map<int, MLog*> logs;
  for (int l = 0; l < 5; l ++) {
    MLog *log = new MLog;
    log->fsid = m->fsid;
  }

  std::deque<LogEntry>::iterator entry_it = m->entries.begin();
  for (; entry_it != m->entries.end(); entry_it++) {
    LogEntry *e = *entry_it;
    if (!subs.exist(e->type))
      continue;

    for (int l = 0; l <= e->type; l++)
      logs[l]->entries.push_back(e);
  }

  map<int,xlist<Subscription*> >::iterator subs_it = subs.begin();
  for (; subs_it != subs.end(); subs_it++) {
    level = subs_it->first;
    xlist<Subscription*>::iterator *i = subs_it->second.begin();

    mon->messenger->send_message(logs[level], *i->session->inst);
    if (*i->onetime)
      mon->session_map.remove_sub(*i);
  }
#endif
}
