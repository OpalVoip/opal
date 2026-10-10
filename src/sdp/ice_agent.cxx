/*
 * ice_agent.cxx
 *
 * RFC 8445 ICE checklist. Sockets stay with OpalICEMediaTransport.
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (C) 2010 Vox Lucida Pty. Ltd.
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Vox Lucida Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "ice.h"
#endif

#include <sdp/ice.h>

#if OPAL_ICE

#include <algorithm>
#include <set>


#define PTraceModule() "ICE"
#define new PNEW


static bool SameAddress(const PIPSocketAddressAndPort & lhs, const PIPSocketAddressAndPort & rhs)
{
  return lhs.IsValid() && lhs == rhs;
}


static bool SameTransaction(const PBYTEArray & lhs, const PBYTEArray & rhs)
{
  return lhs.GetSize() > 0 &&
         lhs.GetSize() == rhs.GetSize() &&
         memcmp(lhs.GetPointer(), rhs.GetPointer(), lhs.GetSize()) == 0;
}


static std::string PairFoundation(const OpalICEAgent::Pair & pair)
{
  return std::string((const char *)pair.local.m_foundation) + "/" +
         std::string((const char *)pair.remote.m_foundation);
}


OpalICEAgent::OpalICEAgent(unsigned component)
  : m_component(component == 0 ? 1 : component)
  , m_state(New)
  , m_role(UnknownRole)
  , m_localLite(false)
  , m_remoteLite(false)
  , m_offerer(false)
  , m_relayOnly(false)
  , m_consentRequired(false)
  , m_haveConsent(false)
{
}


unsigned OpalICEAgent::TypePreference(PNatCandidate::Types type)
{
  switch (type) {
    case PNatCandidate::HostType :
      return 126;
    case PNatCandidate::PeerReflexiveType :
      return 110;
    case PNatCandidate::ServerReflexiveType :
      return 100;
    case PNatCandidate::RelayType :
      return 0;
    default :
      return 0;
  }
}


unsigned OpalICEAgent::CandidatePriority(PNatCandidate::Types type, unsigned localPreference, unsigned component)
{
  if (component == 0 || component > 256)
    component = 1;
  if (localPreference > 65535)
    localPreference = 65535;
  return (TypePreference(type) << 24) | ((localPreference & 0xffff) << 8) | (256 - component);
}


uint64_t OpalICEAgent::PairPriority(unsigned controllingPriority, unsigned controlledPriority)
{
  uint64_t controlling = controllingPriority;
  uint64_t controlled = controlledPriority;
  uint64_t lesser = controlling < controlled ? controlling : controlled;
  uint64_t greater = controlling < controlled ? controlled : controlling;
  return (lesser << 32) + (2 * greater) + (controlling > controlled ? 1 : 0);
}


PString OpalICEAgent::Foundation(PNatCandidate::Types type,
                                 const PString & base,
                                 const PString & protocol,
                                 const PString & server)
{
  PString material = psprintf("%u|%s|%s|%s", (unsigned)type, (const char *)base, (const char *)protocol, (const char *)server);
  unsigned hash = 2166136261u;
  for (PINDEX i = 0; i < material.GetLength(); ++i)
    hash = (hash ^ (unsigned char)material[i]) * 16777619u;
  return psprintf("%08x", hash);
}


PString OpalICEAgent::Username(const PString & remoteUfrag, const PString & localUfrag)
{
  return remoteUfrag + ':' + localUfrag;
}


bool OpalICEAgent::MustSwitchRole(bool localControlling, uint64_t localTieBreak, uint64_t remoteTieBreak)
{
  if (localControlling)
    return localTieBreak < remoteTieBreak;
  return localTieBreak >= remoteTieBreak;
}


const char * OpalICEAgent::StateName(State state)
{
  switch (state) {
    case New :
      return "New";
    case Gathering :
      return "Gathering";
    case Checking :
      return "Checking";
    case Connected :
      return "Connected";
    case Completed :
      return "Completed";
    case Failed :
      return "Failed";
    case Closed :
      return "Closed";
  }
  return "Unknown";
}


void OpalICEAgent::SetRole(Role role)
{
  if (m_role == role)
    return;
  m_role = role;
  for (vector<Pair>::iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (m_role == Controlling)
      it->priority = PairPriority(it->local.m_priority, it->remote.m_priority);
    else if (m_role == Controlled)
      it->priority = PairPriority(it->remote.m_priority, it->local.m_priority);
  }
  SortPairs();
}


void OpalICEAgent::SetLocalLite(bool lite)
{
  m_localLite = lite;
}


void OpalICEAgent::SetRemoteLite(bool lite)
{
  m_remoteLite = lite;
}


void OpalICEAgent::SetOfferer(bool offerer)
{
  m_offerer = offerer;
}


void OpalICEAgent::SetRelayOnly(bool relayOnly)
{
  m_relayOnly = relayOnly;
}


void OpalICEAgent::SetConsentRequired(bool required)
{
  m_consentRequired = required;
}


void OpalICEAgent::AssignRole()
{
  if (m_localLite)
    m_role = Lite;
  else if (m_remoteLite || m_offerer)
    m_role = Controlling;
  else
    m_role = Controlled;
}


void OpalICEAgent::AddLocalCandidate(const PNatCandidate & candidate)
{
  if (m_state == Closed || candidate.m_type >= PNatCandidate::FinalType)
    return;

  for (vector<PNatCandidate>::const_iterator it = m_local.begin(); it != m_local.end(); ++it) {
    if (it->m_type == candidate.m_type && SameAddress(it->m_baseTransportAddress, candidate.m_baseTransportAddress))
      return;
  }

  m_local.push_back(candidate);
  if (m_state == New)
    m_state = Gathering;
}


bool OpalICEAgent::AddRemoteCandidate(const PNatCandidate & candidate)
{
  if (m_state == Closed || candidate.m_type >= PNatCandidate::FinalType)
    return false;

  for (vector<PNatCandidate>::const_iterator it = m_remote.begin(); it != m_remote.end(); ++it) {
    if (it->m_protocol == candidate.m_protocol &&
        SameAddress(it->m_baseTransportAddress, candidate.m_baseTransportAddress))
      return false;
  }

  m_remote.push_back(candidate);
  return true;
}


void OpalICEAgent::SetRemoteCredentials(const PString & ufrag, const PString & password)
{
  m_remoteUsername = ufrag;
  m_remotePassword = password;
}


bool OpalICEAgent::RemoteCredentialsChanged(const PString & ufrag) const
{
  return !m_remoteUsername.IsEmpty() && ufrag != m_remoteUsername;
}


void OpalICEAgent::SortPairs()
{
  std::sort(m_pairs.begin(), m_pairs.end(), [](const Pair & lhs, const Pair & rhs) {
    return lhs.priority > rhs.priority;
  });
}


void OpalICEAgent::FormChecklist()
{
  if (m_role == Lite || m_state == Closed || m_state == Failed)
    return;

  vector<Pair> previous = m_pairs;
  m_pairs.clear();

  for (vector<PNatCandidate>::const_iterator local = m_local.begin(); local != m_local.end(); ++local) {
    if (local->m_type >= PNatCandidate::FinalType)
      continue;
    if (m_relayOnly && local->m_type != PNatCandidate::RelayType)
      continue;

    for (vector<PNatCandidate>::const_iterator remote = m_remote.begin(); remote != m_remote.end(); ++remote) {
      if (remote->m_type >= PNatCandidate::FinalType)
        continue;
      if (!remote->m_protocol.IsEmpty() && remote->m_protocol != local->m_protocol)
        continue;

      Pair pair;
      pair.local = *local;
      pair.remote = *remote;
      if (m_role == Controlling)
        pair.priority = PairPriority(local->m_priority, remote->m_priority);
      else
        pair.priority = PairPriority(remote->m_priority, local->m_priority);

      for (vector<Pair>::const_iterator old = previous.begin(); old != previous.end(); ++old) {
        if (old->local.m_type == local->m_type &&
            SameAddress(old->local.m_baseTransportAddress, local->m_baseTransportAddress) &&
            SameAddress(old->remote.m_baseTransportAddress, remote->m_baseTransportAddress)) {
          pair.state = old->state;
          pair.nominated = old->nominated;
          pair.triggered = old->triggered;
          pair.nominateOnSend = old->nominateOnSend;
          pair.attempts = old->attempts;
          pair.lastSent = old->lastSent;
          pair.transactionId = old->transactionId;
          pair.rttMs = old->rttMs;
          break;
        }
      }
      m_pairs.push_back(pair);
    }
  }

  SortPairs();

  std::set<std::string> seenFoundation;
  for (vector<Pair>::iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (it->state == Succeeded || it->state == InProgress || it->nominated)
      continue;
    if (seenFoundation.insert(PairFoundation(*it)).second)
      it->state = Waiting;
    else
      it->state = Frozen;
  }

  if (m_state != Completed && !m_remote.empty())
    m_state = Checking;
}


void OpalICEAgent::Restart()
{
  m_remote.clear();
  m_pairs.clear();
  m_remoteUsername.MakeEmpty();
  m_remotePassword.MakeEmpty();
  m_selectedLocal = PNatCandidate();
  m_selectedRemote = PNatCandidate();
  m_failureReason.MakeEmpty();
  m_haveConsent = false;
  m_state = m_local.empty() ? New : Gathering;
}


void OpalICEAgent::Close()
{
  m_pairs.clear();
  m_state = Closed;
}


int OpalICEAgent::FindTransaction(const PBYTEArray & transactionId) const
{
  for (size_t i = 0; i < m_pairs.size(); ++i) {
    if (SameTransaction(m_pairs[i].transactionId, transactionId))
      return (int)i;
  }
  return -1;
}


int OpalICEAgent::FindRemoteAddress(const PIPSocketAddressAndPort & address) const
{
  for (size_t i = 0; i < m_pairs.size(); ++i) {
    if (SameAddress(m_pairs[i].remote.m_baseTransportAddress, address))
      return (int)i;
  }
  return -1;
}


bool OpalICEAgent::KnownLocalAddress(const PIPSocketAddressAndPort & address) const
{
  for (vector<PNatCandidate>::const_iterator it = m_local.begin(); it != m_local.end(); ++it) {
    if (SameAddress(it->m_baseTransportAddress, address) || SameAddress(it->m_localTransportAddress, address))
      return true;
  }
  return false;
}


void OpalICEAgent::FailIfExhausted()
{
  if (m_pairs.empty() || m_state == Completed || m_state == Closed || m_state == Failed)
    return;

  for (vector<Pair>::const_iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (it->state == Frozen || it->state == Waiting || it->state == InProgress || it->state == Succeeded)
      return;
  }

  m_state = Failed;
  m_failureReason = "all candidate pairs failed";
}


void OpalICEAgent::SelectNominated(int index)
{
  if (index < 0 || (size_t)index >= m_pairs.size())
    return;

  Pair & pair = m_pairs[index];
  pair.state = Succeeded;
  pair.nominated = true;
  m_selectedLocal = pair.local;
  m_selectedRemote = pair.remote;
  m_state = Completed;
  m_failureReason.MakeEmpty();
}


void OpalICEAgent::Unfreeze(const PString & localFoundation, const PString & remoteFoundation)
{
  for (vector<Pair>::iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (it->state == Frozen && it->local.m_foundation == localFoundation && it->remote.m_foundation == remoteFoundation)
      it->state = Waiting;
  }
}


int OpalICEAgent::NextCheck(const PTime & now, const PTimeInterval & rto, unsigned maxAttempts)
{
  if (m_role != Controlling && m_role != Controlled)
    return -1;
  if (m_state == Failed || m_state == Closed || m_state == Completed || m_state == New)
    return -1;

  for (vector<Pair>::iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (it->state == InProgress && it->attempts >= maxAttempts)
      it->state = FailedPair;
  }

  int nominate = -1;
  int triggered = -1;
  int retransmit = -1;
  int waiting = -1;
  bool relayBusy = false;

  for (size_t i = 0; i < m_pairs.size(); ++i) {
    const Pair & pair = m_pairs[i];
    if (pair.local.m_type == PNatCandidate::RelayType && pair.state == InProgress)
      relayBusy = true;
  }

  for (size_t i = 0; i < m_pairs.size(); ++i) {
    const Pair & pair = m_pairs[i];
    if (pair.nominated)
      continue;
    if (pair.local.m_type == PNatCandidate::RelayType && relayBusy && pair.state != InProgress)
      continue;
    if (nominate < 0 && pair.state == Succeeded && m_role == Controlling)
      nominate = (int)i;
    if (triggered < 0 && pair.state == Waiting && pair.triggered)
      triggered = (int)i;
    if (retransmit < 0 && pair.state == InProgress && pair.attempts < maxAttempts && pair.lastSent + rto <= now)
      retransmit = (int)i;
    if (waiting < 0 && pair.state == Waiting)
      waiting = (int)i;
  }

  FailIfExhausted();
  if (m_state == Failed)
    return -1;
  if (nominate >= 0)
    return nominate;
  if (triggered >= 0)
    return triggered;
  if (retransmit >= 0)
    return retransmit;
  return waiting;
}


void OpalICEAgent::NoteCheckSent(int index, const PBYTEArray & transactionId, const PTime & now, bool nominate)
{
  if (index < 0 || (size_t)index >= m_pairs.size())
    return;

  Pair & pair = m_pairs[index];
  if (pair.state != InProgress)
    pair.attempts = 0;
  pair.transactionId = transactionId;
  pair.lastSent = now;
  pair.nominateOnSend = nominate;
  pair.triggered = false;
  pair.state = InProgress;
  ++pair.attempts;
  if (m_state == Gathering || m_state == Connected)
    m_state = Checking;
}


OpalICEAgent::CheckResult OpalICEAgent::OnCheckResult(const PBYTEArray & transactionId,
                                                      bool success,
                                                      unsigned errorCode,
                                                      const PIPSocketAddressAndPort & mapped,
                                                      const PTime & now)
{
  CheckResult result;
  int index = FindTransaction(transactionId);
  if (index < 0)
    return result;

  result.matched = true;
  Pair & pair = m_pairs[index];
  result.local = pair.local;
  result.remote = pair.remote;

  if (success) {
    result.rttMs = (int)(now - pair.lastSent).GetMilliSeconds();
    pair.rttMs = result.rttMs;
    NoteConsent(now);

    if (mapped.IsValid() && !KnownLocalAddress(mapped)) {
      PNatCandidate prflx(PNatCandidate::PeerReflexiveType,
                          (PNatMethod::Component)m_component,
                          Foundation(PNatCandidate::PeerReflexiveType, mapped.AsString(), pair.local.m_protocol, "prflx"),
                          CandidatePriority(PNatCandidate::PeerReflexiveType, 32768, m_component),
                          pair.local.m_protocol);
      prflx.m_baseTransportAddress = mapped;
      if (pair.local.m_type == PNatCandidate::HostType)
        prflx.m_localTransportAddress = pair.local.m_baseTransportAddress;
      else if (pair.local.m_localTransportAddress.IsValid())
        prflx.m_localTransportAddress = pair.local.m_localTransportAddress;
      AddLocalCandidate(prflx);
      result.addedPeerReflexive = true;
    }

    if (pair.nominateOnSend) {
      SelectNominated(index);
      result.nominated = true;
    }
    else {
      pair.state = Succeeded;
      if (m_state != Completed)
        m_state = Connected;
      Unfreeze(pair.local.m_foundation, pair.remote.m_foundation);
    }

    if (result.addedPeerReflexive)
      FormChecklist();
    return result;
  }

  if (errorCode == 487 && m_role != Lite) {
    result.roleConflict = true;
    m_role = (m_role == Controlling) ? Controlled : Controlling;
    for (vector<Pair>::iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
      if (it->state == InProgress) {
        it->state = Waiting;
        it->attempts = 0;
        it->nominateOnSend = false;
      }
      if (m_role == Controlling)
        it->priority = PairPriority(it->local.m_priority, it->remote.m_priority);
      else
        it->priority = PairPriority(it->remote.m_priority, it->local.m_priority);
    }
    SortPairs();
    if (m_state != Completed)
      m_state = Checking;
    return result;
  }

  pair.state = FailedPair;
  Unfreeze(pair.local.m_foundation, pair.remote.m_foundation);
  FailIfExhausted();
  result.checklistFailed = m_state == Failed;
  return result;
}


OpalICEAgent::InboundResult OpalICEAgent::OnInboundRequest(const PIPSocketAddressAndPort & from,
                                                          unsigned remotePriority,
                                                          bool useCandidate)
{
  InboundResult result;
  if (m_state == Closed || !from.IsValid())
    return result;

  int index = FindRemoteAddress(from);
  if (index < 0) {
    PNatCandidate::Types type = PNatCandidate::PeerReflexiveType;
    switch (remotePriority >> 24) {
      case 126 :
        type = PNatCandidate::HostType;
        break;
      case 110 :
        type = PNatCandidate::PeerReflexiveType;
        break;
      case 100 :
        type = PNatCandidate::ServerReflexiveType;
        break;
      case 0 :
        type = PNatCandidate::RelayType;
        break;
      default :
        break;
    }
    PNatCandidate remote(type,
                         (PNatMethod::Component)m_component,
                         Foundation(type, from.AsString(), "udp", "inbound"),
                         remotePriority == 0 ? CandidatePriority(type, 65535, m_component) : remotePriority,
                         "udp");
    remote.m_baseTransportAddress = from;
    if (AddRemoteCandidate(remote))
      FormChecklist();
    index = FindRemoteAddress(from);
  }

  if (index < 0)
    return result;

  Pair & pair = m_pairs[index];
  if (pair.state == Frozen || pair.state == FailedPair)
    pair.state = Waiting;
  pair.triggered = true;
  result.handled = true;
  result.local = pair.local;
  result.remote = pair.remote;

  if (useCandidate && (m_state != Completed || pair.remote.m_priority >= m_selectedRemote.m_priority)) {
    SelectNominated(index);
    result.nominated = true;
    result.local = m_selectedLocal;
    result.remote = m_selectedRemote;
  }
  return result;
}


bool OpalICEAgent::GetNominated(PNatCandidate & local, PNatCandidate & remote) const
{
  if (m_state != Completed)
    return false;
  local = m_selectedLocal;
  remote = m_selectedRemote;
  return remote.m_baseTransportAddress.IsValid();
}


bool OpalICEAgent::ConsentExpired(const PTime & now, const PTimeInterval & expiry) const
{
  if (!m_consentRequired || !m_haveConsent || m_state != Completed)
    return false;
  return m_lastConsent + expiry < now;
}


void OpalICEAgent::NoteConsent(const PTime & now)
{
  m_lastConsent = now;
  m_haveConsent = true;
}


void OpalICEAgent::CopyLocalCandidates(PNatCandidateList & candidates) const
{
  for (vector<PNatCandidate>::const_iterator it = m_local.begin(); it != m_local.end(); ++it) {
    if (m_relayOnly && it->m_type != PNatCandidate::RelayType)
      continue;
    candidates.Append(new PNatCandidate(*it));
  }
}


void OpalICEAgent::PrintSelectedPath(ostream & strm) const
{
  strm << "ICE State: " << StateName(m_state);
  if (!m_failureReason.IsEmpty())
    strm << " (" << m_failureReason << ')';
  if (m_state != Completed)
    return;

  static const char * const TypeNames[] = { "host", "srflx", "prflx", "relay", "end-of-candidates" };
  const char * localType = m_selectedLocal.m_type < PNatCandidate::NumTypes ? TypeNames[m_selectedLocal.m_type] : "unknown";
  const char * remoteType = m_selectedRemote.m_type < PNatCandidate::NumTypes ? TypeNames[m_selectedRemote.m_type] : "unknown";
  strm << "\n"
          "Local Candidate: " << localType << "\n"
          "Remote Candidate: " << remoteType << "\n"
          "Protocol: " << m_selectedLocal.m_protocol << "\n"
          "Local Address: " << m_selectedLocal.m_baseTransportAddress << "\n"
          "Remote Address: " << m_selectedRemote.m_baseTransportAddress;
  if (m_pairs.empty())
    return;
  for (vector<Pair>::const_iterator it = m_pairs.begin(); it != m_pairs.end(); ++it) {
    if (it->nominated && it->rttMs >= 0) {
      strm << "\nRTT: " << it->rttMs << " ms";
      break;
    }
  }
}


#endif // OPAL_ICE
