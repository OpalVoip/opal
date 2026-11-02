/*
 * ice.cxx
 *
 * Interactive Connectivity Establishment
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

#include <ptclib/random.h>
#include <opal/connection.h>

#include <map>
#include <set>
#include <string>
#include <opal/endpoint.h>
#include <opal/manager.h>


#define PTraceModule() "ICE"
#define new PNEW


// Magic numbers from RFC5245
const unsigned HostTypePriority = 126;
const unsigned PeerReflexiveTypePriority = 110;
const unsigned ServerReflexiveTypePriority = 100;
const unsigned RelayTypePriority = 0;
const unsigned CandidateTypePriority[PNatCandidate::NumTypes] = {
  HostTypePriority,
  ServerReflexiveTypePriority,
  PeerReflexiveTypePriority,
  RelayTypePriority
};

static const char LiteFoundation[] = "xyzzy";


/////////////////////////////////////////////////////////////////////////////

OpalICEMediaTransport::OpalICEMediaTransport(OpalConnection & connection, const PString & name)
  : OpalUDPMediaTransport(connection, name)
  , m_localUsername(PBase64::Encode(PRandom::Octets(12)))
  , m_localPassword(PBase64::Encode(PRandom::Octets(18)))
  , m_localLite(false)
  , m_remoteLite(false)
  , m_localTrickle(false)
  , m_remoteTrickle(false)
  , m_useNetworkCost(false)
  , m_offerer(false)
  , m_relayOnly(false)
  , m_consentFreshness(false)
  , m_iceDisabled(false)
  , m_checklistFailed(false)
  , m_gatheringComplete(false)
  , m_stopGather(false)
  , m_gatherThread(NULL)
  , m_allocatingSocket(NULL)
{
  PTRACE_CONTEXT_ID_TO(m_server);
  PTRACE_CONTEXT_ID_TO(m_client);
  m_checkTimer.SetNotifier(PCREATE_NOTIFIER(OnCheckTimer));
  m_consentTimer.SetNotifier(PCREATE_NOTIFIER(OnConsentTimer));
}


OpalICEMediaTransport::~OpalICEMediaTransport()
{
  m_stopGather = true;
  m_checkTimer.Stop(false);
  m_consentTimer.Stop(false);

  for (vector<PUDPSocket *>::iterator it = m_turnSockets.begin(); it != m_turnSockets.end(); ++it) {
    if (*it != NULL)
      (*it)->Close();
  }
  for (vector<PUDPSocket *>::iterator it = m_extraSockets.begin(); it != m_extraSockets.end(); ++it) {
    if (*it != NULL)
      (*it)->Close();
  }
  {
    PWaitAndSignal lock(m_gatherMutex);
    if (m_allocatingSocket != NULL)
      m_allocatingSocket->Close();
  }

  InternalClose();

  delete m_gatherThread;
  m_gatherThread = NULL;

  for (ChannelArray::iterator it = m_subchannels.begin(); it != m_subchannels.end(); ++it) {
    delete it->m_thread;
    it->m_thread = NULL;
  }

  for (vector<PUDPSocket *>::iterator it = m_turnSockets.begin(); it != m_turnSockets.end(); ++it)
    delete *it;
  for (vector<PUDPSocket *>::iterator it = m_extraSockets.begin(); it != m_extraSockets.end(); ++it)
    delete *it;
  m_turnSockets.clear();
  m_extraSockets.clear();
}


bool OpalICEMediaTransport::Open(OpalMediaSession & session,
                                 PINDEX count,
                                 const PString & localInterface,
                                 const OpalTransportAddress & remoteAddress)
{
  const PStringOptions & options = session.GetStringOptions();
  m_localLite = options.GetBoolean(OPAL_OPT_ICE_LITE, true);
  m_relayOnly = options.GetBoolean(OPAL_OPT_ICE_RELAY_ONLY, false);
  m_consentFreshness = options.GetBoolean(OPAL_OPT_ICE_CONSENT, false);
  m_gatherTimeout = options.GetVar(OPAL_OPT_ICE_GATHER_TIMEOUT, PTimeInterval(0, 5));

  m_localTrickle = options.GetBoolean(OPAL_OPT_TRICKLE_ICE, true);
  m_useNetworkCost = options.GetBoolean(OPAL_OPT_NETWORK_COST_ICE);
  m_iceTimeout = options.GetVar(OPAL_OPT_ICE_TIMEOUT, m_connection.GetEndPoint().GetManager().GetICETimeout());

  // As per RFC 5425
  static const PTimeInterval MinTimeout(0,15);
  if (m_iceTimeout < MinTimeout)
    m_iceTimeout = MinTimeout;
  if (m_mediaTimeout < MinTimeout)
    m_mediaTimeout = MinTimeout;

  if (!OpalUDPMediaTransport::Open(session, count, localInterface, remoteAddress))
      return false;

  // Open the STUN server and set what credentials we have so far
  m_server.Open(GetSubChannelAsSocket(e_Data), GetSubChannelAsSocket(e_Control));
  m_server.SetCredentials(m_localUsername + ':' + m_remoteUsername, m_localPassword, PString::Empty());

  if (!m_localLite) {
    GatherHostCandidates();
    StartGathering();
    Start();
    PTRACE(3, *this << "full ICE agent started, lite=false");
  }
  return true;
}


bool OpalICEMediaTransport::UseNatTraversal() const
{
  return m_localLite;
}


bool OpalICEMediaTransport::Write(const void * data, PINDEX length, SubChannels subchannel, const PIPSocketAddressAndPort * remote, int * mtu)
{
  if (!m_localLite && m_consentFreshness && (size_t)subchannel < m_iceChannelCache.size()) {
    if (GetICEChannel(subchannel).m_agent.ConsentExpired(PTime(), PTimeInterval(0, 30))) {
      PTRACE(2, *this << "consent expired on " << subchannel << ", not sending media");
      return false;
    }
  }

  if (!m_localLite && (size_t)subchannel < m_iceChannelCache.size() && GetICEChannel(subchannel).m_relaySelected) {
    PUDPSocket * turn = subchannel < (SubChannels)m_turnSockets.size() ? m_turnSockets[subchannel] : NULL;
    if (turn == NULL || !turn->IsOpen())
      return false;
    if (remote != NULL)
      turn->SetSendAddress(*remote);
    return turn->Write(data, length);
  }

  return OpalUDPMediaTransport::Write(data, length, subchannel, remote, mtu);
}


bool OpalICEMediaTransport::IsEstablished() const
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return false);

  if (m_checklistFailed)
    return false;

  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel) {
    if (GetICEChannel(subchannel).m_state > e_Completed) // Still running
      return false;
  }

  return OpalUDPMediaTransport::IsEstablished();
}


bool OpalICEMediaTransport::InternalRxData(SubChannels subchannel, const PBYTEArray & data)
{
  ChecklistState state;
  {
    P_INSTRUMENTED_LOCK_READ_ONLY(return false);
    state = GetICEChannel(subchannel).m_state;
  }

  if (m_checklistFailed)
    return OpalMediaTransport::InternalRxData(subchannel, data);

  switch (state) {
    case e_Disabled :
      return OpalUDPMediaTransport::InternalRxData(subchannel, data);

    case e_Completed :
      return OpalMediaTransport::InternalRxData(subchannel, data);

    default :
      OpalMediaTransport::InternalRxData(subchannel, data);
      return false;
  }
}


bool OpalICEMediaTransport::InternalOpenPinHole(PUDPSocket &)
{
  // Opening pin hole not needed as ICE protocol has already done so
  return true;
}


PChannel * OpalICEMediaTransport::AddWrapperChannels(SubChannels subchannel, PChannel * channel)
{
  ICEChannel * iceChannel = new ICEChannel(*this, subchannel, channel);
  m_iceChannelCache.push_back(iceChannel);
  return iceChannel;
}


void OpalICEMediaTransport::SetCandidates(const PString & user,
                                          const PString & pass,
                                          const PNatCandidateList & remoteCandidates,
                                          unsigned options)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return);

  if (user.IsEmpty() || pass.IsEmpty()) {
    PTRACE(3, *this << "disabled");
    m_iceDisabled = true;
    m_checkTimer.Stop(false);
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel)
      GetICEChannel(subchannel).m_state = e_Disabled;
    return;
  }

  if (!m_remoteUsername.IsEmpty() && user != m_remoteUsername) {
    PTRACE(3, *this << "ICE restart, remote ufrag changed");
    m_localUsername = PBase64::Encode(PRandom::Octets(12));
    m_localPassword = PBase64::Encode(PRandom::Octets(18));
    m_checklistFailed = false;
    m_gatheringComplete = m_localLite;
    m_pending.clear();
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_iceChannelCache.size(); ++subchannel)
      GetICEChannel(subchannel).m_agent.Restart();
    if (!m_localLite)
      StartGathering();
  }

  if (user == m_remoteUsername && pass == m_remotePassword && IsEstablished()) {
    PTRACE(4, *this << "username/password unchanged and is established, ignoring candidates.");
    return;
  }

  m_remoteUsername = user;
  m_remotePassword = pass;
  m_remoteLite = options & OptLite;
  m_remoteTrickle = options & OptTrickle;

  for (PNatCandidateList::const_iterator it = remoteCandidates.begin(); it != remoteCandidates.end(); ++it) {
    if (it->m_type >= PNatCandidate::FinalType) {
      PTRACE(4, *this << "remote gathering complete");
      continue;
    }
    if (it->m_component > 0 && (size_t)it->m_component <= m_subchannels.size()) {
      ICEChannel & channel = GetICEChannel((SubChannels)(it->m_component - 1));
      if (m_localLite)
        channel.AddRemoteCandidate(*it PTRACE_PARAM(, "offer/answer"));
      else if (channel.m_agent.AddRemoteCandidate(*it))
        PTRACE(4, *this << "remote candidate added: " << *it);
    }
    else
      PTRACE(3, "Invalid component in candidate: " << *it);
  }

  bool offerMade = GetICEChannel(e_Media).m_state == e_Offering;

  PTRACE(3, "Setting " << remoteCandidates.size() << " candidates:"
            " local-lite=" << boolalpha << m_localLite << ","
            " remote-lite=" << m_remoteLite << ","
            " local-trickle=" << m_localTrickle << ","
            " remote-trickle=" << m_remoteTrickle);

  if (m_localLite) {
    m_server.SetIceRole(PSTUN::IceLite);
    m_client.SetIceRole(PSTUN::IceLite);
  }
  else if (!offerMade) {
    m_server.SetIceRole(PSTUN::IceControlled);
    m_client.SetIceRole(PSTUN::IceControlled);
  }
  else {
    m_server.SetIceRole(PSTUN::IceControlling);
    m_client.SetIceRole(PSTUN::IceControlling);
  }

  m_server.SetCredentials(m_localUsername + ':' + m_remoteUsername, m_localPassword, PString::Empty());
  m_client.SetCredentials(m_remoteUsername + ':' + m_localUsername, m_remotePassword, PString::Empty());
  AssignRoles();

  SetRemoteBehindNAT();

  if (!m_localLite) {
    bool anyRemote = false;
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_iceChannelCache.size(); ++subchannel) {
      ICEChannel & channel = GetICEChannel(subchannel);
      channel.m_agent.SetRemoteCredentials(user, pass);
      channel.m_agent.FormChecklist();
      if (!channel.m_agent.Checklist().empty()) {
        anyRemote = true;
        if (channel.m_state == e_Disabled || channel.m_state == e_Completed)
          channel.m_state = m_offerer ? e_OfferAnswered : e_Answering;
      }
    }
    if (anyRemote)
      m_checkTimer.RunContinuous(50);
  }

  // If we had an early USE-CANDIDATE before we got this answer to our offer, then we complete now.
  if (offerMade) {
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel) {
      PNatCandidate selectedCandidate = GetICEChannel(subchannel).m_selectedCandidate;
      if (selectedCandidate.m_type != PNatCandidate::EndTypes) {
        PTRACE(4, *this << "Using early USE-CANDIDATE for " << subchannel << ' ' << selectedCandidate);
        InternalSetRemoteAddress(selectedCandidate.m_baseTransportAddress, subchannel, e_RemoteAddressFromICE);
      }
    }
  }
    
  /* With ICE we start the thread straight away, as we need to respond to STUN
     requests before we get an answer back from the remote, which is when we
     would usually start the read thread. */
  Start();
}


void OpalICEMediaTransport::AddCandidate(const PNatCandidate & candidate)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return);

  if (candidate.m_component > 0 && (size_t)candidate.m_component <= m_subchannels.size()) {
    ICEChannel & channel = GetICEChannel((SubChannels)(candidate.m_component - 1));
    if (m_localLite)
      channel.AddRemoteCandidate(candidate PTRACE_PARAM(, "trickle"));
    else if (channel.m_agent.AddRemoteCandidate(candidate)) {
      channel.m_agent.FormChecklist();
      if (channel.m_state == e_Disabled)
        channel.m_state = e_Answering;
      m_checkTimer.RunContinuous(50);
      PTRACE(4, *this << "trickled remote candidate: " << candidate);
    }
  }
  else
    PTRACE(2, *this << "Attempt to add candidate with invalid component: " << candidate);
}


void OpalICEMediaTransport::ICEChannel::AddRemoteCandidate(const PNatCandidate & candidate PTRACE_PARAM(, const char * where))
{
  if (!PAssert(m_subchannel == (candidate.m_component -1), PInvalidParameter))
    return; // Not for us!

  if (m_state == e_Completed) {
    PTRACE(4, m_owner << m_subchannel << ", from " << where << ", negotiation completed, ignoring candidate: " << candidate);
    return;
  }

  bool add = true;

  for (CandidatePairs::iterator it = m_checklist.begin(); it != m_checklist.end(); ++it) {
    if (it->m_remote == candidate) {
      PTRACE(4, m_owner << m_subchannel << ", from " << where << ", remote candidate already added: " << candidate);
      return;
    }

    if (it->m_remote.m_foundation.empty() && // Came from early STUN
        it->m_remote.m_protocol == candidate.m_protocol &&
        it->m_remote.m_baseTransportAddress == candidate.m_baseTransportAddress)
    {
      bool earlySelection = m_selectedCandidate.m_component            == candidate.m_component &&
                            m_selectedCandidate.m_protocol             == candidate.m_protocol  &&
                            m_selectedCandidate.m_baseTransportAddress == candidate.m_baseTransportAddress;

      PTRACE(4, m_owner << m_subchannel << ","
             " from " << where << ","
             " " << (earlySelection ? "selected" : "unused") <<
             " early candidate merged:\n"
             "  New candidate=" << setw(-1) << candidate << "\n"
             "  Old candidate=" << setw(-1) << PNatCandidate(it->m_remote));

      /* Update the fields. For some, the SDP value is "better" and for
          others, like networkCost, the value in the STUN request is
          "better". */
      it->m_remote.m_type = candidate.m_type;
      it->m_remote.m_priority = candidate.m_priority;
      it->m_remote.m_foundation = candidate.m_foundation;
      if (it->m_remote.m_networkId == 0)
        it->m_remote.m_networkId = candidate.m_networkId;
      if (it->m_remote.m_networkCost == 0)
        it->m_remote.m_networkCost = candidate.m_networkCost;
      it->m_remote.m_localTransportAddress = candidate.m_localTransportAddress;

      // Update the early USE-CANDIDATE as well
      if (earlySelection)
        m_selectedCandidate = it->m_remote;

      add = false;
    }
  }

  if (m_state == e_Disabled)
    m_state = e_Answering;
  else if (m_state == e_Offering)
    m_state = e_OfferAnswered;

  if (add) {
    PTRACE(4, m_owner << m_subchannel << ", from " << where << ", remote candidate added: " << candidate);
    for (vector<PNatCandidate>::iterator it = m_localCandidates.begin(); it != m_localCandidates.end(); ++it)
      m_checklist.push_back(CandidatePair(*it, candidate));
  }
}


bool OpalICEMediaTransport::GetCandidates(PString & user,
                                          PString & pass,
                                          PNatCandidateList & candidates,
                                          unsigned & options,
                                          bool offering)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return false);

  if (offering)
    m_offerer = true;
  if (!m_localLite)
    AssignRoles();

  if (m_subchannels.empty()) {
    PTRACE(3, *this << "cannot make offer when transport not open");
    return false;
  }

  user = m_localUsername;
  pass = m_localPassword;
  options = m_localLite ? OptLite : OptNone;

  if (IsEstablished()) {
    PTRACE(4, *this << "is established, no candidates being provided");
    return true;
  }

  for (ChannelArray::iterator it = m_subchannels.begin(); it != m_subchannels.end(); ++it) {
    ICEChannel & channel = GetICEChannel(it->m_subchannel);
    if (m_localLite)
      channel.GetLocalCandidates(candidates);
    else
      channel.m_agent.CopyLocalCandidates(candidates);
  }

  if (candidates.empty() && (m_localLite || !m_localTrickle)) {
    PTRACE(3, *this << "no candidates to offer");
    return false;
  }

  if (m_localTrickle) {
    options += OptTrickle;
    if (m_localLite || m_gatheringComplete)
      candidates.push_back(PNatCandidate(PNatCandidate::FinalType, PNatMethod::eComponent_Unknown, LiteFoundation));
  }
    
  /* With ICE we start the thread straight away, as we need to respond to STUN
     requests before we get an answer back from the remote, which is when we
     would usually start the read thread. */
  Start();

  PTRACE(3, "Getting " << candidates.size() << " candidates:"
            " local-lite=" << boolalpha << m_localLite << ","
            " remote-lite=" << m_remoteLite << ","
            " local-trickle=" << m_localTrickle << ","
            " remote-trickle=" << m_remoteTrickle);
  return true;
}


void OpalICEMediaTransport::ICEChannel::GetLocalCandidates(PNatCandidateList & candidates)
{
  if (m_localCandidates.empty()) {
    OpalTransportAddress localAddress = m_owner.m_subchannels[m_subchannel].m_localAddress;

    // Only do ICE-Lite right now so just offer "host" type using local address.
    if (!AddLocalCandidate(localAddress)) {
      PTRACE(2, m_owner << m_subchannel << ", no local binding when using ICE-lite!");
      return;
    }

    // If OpenNAT is present, we add the external IP address as a candidate too.
    PIPAddress ip;
    WORD port;
    if (localAddress.GetIpAndPort(ip, port)) {
      PNatMethod * natMethod = m_owner.m_connection.GetEndPoint().GetManager().GetNatMethods().GetMethod(ip, this);
      if (natMethod != NULL &&
          natMethod->GetNatType() == PNatMethod::OpenNat &&
          natMethod->GetExternalAddress(ip) &&
          m_localCandidates.back().m_baseTransportAddress.GetAddress() != ip)
        AddLocalCandidate(OpalTransportAddress(ip, port));
    }
  }

  for (vector<PNatCandidate>::iterator it = m_localCandidates.begin(); it != m_localCandidates.end(); ++it)
    candidates.Append(new PNatCandidate(*it));
}


bool OpalICEMediaTransport::ICEChannel::AddLocalCandidate(const OpalTransportAddress & address)
{
  static const PNatMethod::Component ComponentId[2] = { PNatMethod::eComponent_RTP, PNatMethod::eComponent_RTCP };
  PNatCandidate candidate(PNatCandidate::HostType, ComponentId[m_subchannel], LiteFoundation);
  if (!address.GetIpAndPort(candidate.m_baseTransportAddress))
    return false;

  unsigned localPreference = candidate.m_baseTransportAddress.GetAddress().GetVersion() == 6 ? 50 : 0xffff;
  candidate.m_priority = OpalICEAgent::CandidatePriority(candidate.m_type, localPreference, candidate.m_component);

  m_localCandidates.push_back(candidate);
  PTRACE(4, m_owner << m_subchannel << ", added local candidate: " << candidate);

  if (m_state == e_Disabled)
    m_state = e_Offering;

  return true;
}


#if OPAL_STATISTICS
void OpalICEMediaTransport::GetStatistics(OpalMediaStatistics & statistics) const
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return);

  OpalMediaTransport::GetStatistics(statistics);

  statistics.m_candidates.clear();
  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel) {
    ICEChannel & iceChannel = GetICEChannel(subchannel);
    for (CandidatePairs::const_iterator it = iceChannel.m_checklist.begin(); it != iceChannel.m_checklist.end(); ++it)
      statistics.m_candidates.push_back(it->m_remote);
  }
}
#endif // OPAL_STATISTICS


OpalICEMediaTransport::ICEChannel::ICEChannel(OpalICEMediaTransport & owner, SubChannels subchannel, PChannel * channel)
  : m_owner(owner)
  , m_subchannel(subchannel)
  , m_state(e_Disabled)
  , m_agent((unsigned)subchannel + 1)
  , m_haveLastFrom(false)
  , m_relaySelected(false)
{
  m_agent.SetLocalLite(owner.m_localLite);
  m_agent.SetRelayOnly(owner.m_relayOnly);
  m_agent.SetConsentRequired(owner.m_consentFreshness);
  Open(channel);
}


PTimeInterval OpalICEMediaTransport::GetTimeout(SubChannels subchannel) const
{
  return GetICEChannel(subchannel).m_state <= e_Completed ? m_mediaTimeout : m_iceTimeout;
}


PBoolean OpalICEMediaTransport::ICEChannel::Read(void * data, PINDEX size)
{
  for (;;) {
    PUDPSocket * host = dynamic_cast<PUDPSocket *>(GetBaseReadChannel());
    PUDPSocket * turn = m_subchannel < (SubChannels)m_owner.m_turnSockets.size() ? m_owner.m_turnSockets[m_subchannel] : NULL;
    bool turnOpen = turn != NULL && turn->IsOpen();
    bool extra = !m_owner.m_extraSockets.empty();

    if (host != NULL && (turnOpen || extra) && !m_owner.m_localLite) {
      PSocket::SelectList ready;
      ready += *host;
      if (turnOpen)
        ready += *turn;
      for (vector<PUDPSocket *>::iterator it = m_owner.m_extraSockets.begin(); it != m_owner.m_extraSockets.end(); ++it) {
        if (*it != NULL && (*it)->IsOpen())
          ready += **it;
      }
      if (PSocket::Select(ready, GetReadTimeout()) != PChannel::NoError || ready.IsEmpty()) {
        SetErrorValues(PChannel::Timeout, 0, PChannel::LastReadError);
        return false;
      }
      PUDPSocket * which = dynamic_cast<PUDPSocket *>(&ready[0]);
      if (which == NULL || !which->Read(data, size)) {
        if (which != NULL)
          SetErrorValues(which->GetErrorCode(PChannel::LastReadError),
                         which->GetErrorNumber(PChannel::LastReadError),
                         PChannel::LastReadError);
        else
          SetErrorValues(PChannel::NotOpen, 0, PChannel::LastReadError);
        return false;
      }
      SetLastReadCount(which->GetLastReadCount());
      which->GetLastReceiveAddress(m_lastFrom);
      m_haveLastFrom = true;
    }
    else {
      if (!PIndirectChannel::Read(data, size))
        return false;
      m_haveLastFrom = false;
    }

    if (HandleICE(data, GetLastReadCount()))
      return true;
  }
}


/*
 * Process STUN binding requests received on the channel.
 * 
 * @return true if the data should be passed up for processing by another layer,
 *         false if the data was processed here or should be discarded.
 */
bool OpalICEMediaTransport::ICEChannel::HandleICE(const void * data, PINDEX length)
{
  P_INSTRUMENTED_LOCK_READ_WRITE2(lock, m_owner);
  if (!lock.IsLocked())
    return false;

  if (m_state == e_Disabled)
    return true;

  if (!m_owner.m_localLite)
    return HandleICEFull(data, length);

  if (m_owner.m_subchannels[m_subchannel].m_remoteGoneError == PChannel::Unavailable)
    m_owner.m_subchannels[m_subchannel].m_remoteGoneError = PChannel::ProtocolFailure;

  PUDPSocket * socket = m_owner.GetSubChannelAsSocket(m_subchannel);
  PIPAddressAndPort ap;
  socket->GetLastReceiveAddress(ap);

  // Demultiplex based on https://tools.ietf.org/html/rfc7983
  const BYTE *byteData = static_cast<const BYTE*>(data);
  if (byteData[0] > 3) {
    /* As per https://tools.ietf.org/html/rfc5245#section-11.2, receiving media
       is not dependent on having a selected candidate pair; that only applies to
       sending media. */
    return true;
  }

  PSTUNMessage message(byteData, length, ap);
  if (!message.IsValid()) {
    PTRACE(2, m_owner << m_subchannel << ", discarding invalid message from=" << ap << " len=" << length
            << ": " << PHexDump(data, std::min(length, static_cast<PINDEX>(16))));
    return false;
  }

  CandidatePairs::iterator candidate;
  for (candidate = m_checklist.begin(); candidate != m_checklist.end(); ++candidate) {
    if (candidate->m_remote.m_baseTransportAddress == ap)
      break;
  }
  if (candidate == m_checklist.end()) {
    if (!message.IsRequest()) {
      PTRACE(4, m_owner << m_subchannel << ", ignoring unexpected STUN message: " << message);
      return false;
    }

    /* If we had not got the candidate via SDP, then there was some sort of short cut to
       a trickle ICE and this candidate took too long to determine by the signalling system.
       We will assume that was a TURN server, as that would be typically slowest to set up,
       and give it lowest possible priority. Note, that if we have a PRIORITY attribute in
       the STUN, and it was constructed as per RFC recommendation, we can actually determine
       the type from it. */
    PNatCandidate newCandidate(PNatCandidate::RelayType, (PNatMethod::Component)(m_subchannel+1));

    PSTUNIcePriority * priAttr = PSTUNIcePriority::Find(message);
    if (priAttr != NULL) {
      newCandidate.m_priority = priAttr->m_priority;
      switch (newCandidate.m_priority >> 24) {
        case HostTypePriority :
          newCandidate.m_type = PNatCandidate::HostType;
          break;

        case PeerReflexiveTypePriority :
          newCandidate.m_type = PNatCandidate::PeerReflexiveType;
          break;

        case ServerReflexiveTypePriority :
          newCandidate.m_type = PNatCandidate::ServerReflexiveType;
          break;

        case RelayTypePriority :
          break;

        default :
          PTRACE(4, "Could not derive the candidate type from priority (" << newCandidate.m_priority << ") in STUN message.");
      }
    }

    newCandidate.m_baseTransportAddress = ap;
    AddRemoteCandidate(newCandidate PTRACE_PARAM(, "early STUN"));
    candidate = m_checklist.begin();
    std::advance(candidate, m_checklist.size()-1);
    PTRACE(3, m_owner << m_subchannel << ", received STUN request for unknown candidate, adding: " << newCandidate);
  }

  if (message.IsRequest()) {
#if OPAL_STATISTICS
    candidate->m_remote.m_rxRequests.Count();
#endif

    if (!m_owner.m_server.OnReceiveMessage(message, PSTUNServer::SocketInfo(socket)))
      return false; // Probably a authentication error

    PSTUNIceNetworkCost * costAttr = PSTUNIceNetworkCost::Find(message);
    if (costAttr != NULL) {
      unsigned networkId = costAttr->m_networkId;
      unsigned networkCost = costAttr->m_networkCost;
      if (candidate->m_remote.m_networkId != networkId || candidate->m_remote.m_networkCost != networkCost) {
        PTRACE(4, m_owner << m_subchannel << ","
               " updating candidate network cost:"
               " old-id=" << candidate->m_remote.m_networkId << ","
               " new-id=" << networkId << ","
               " old-cost " << candidate->m_remote.m_networkCost << ","
               " new-cost=" << networkCost);
        candidate->m_remote.m_networkId = networkId;
        candidate->m_remote.m_networkCost = networkCost;
      }
    }

    if (message.FindAttribute(PSTUNAttribute::USE_CANDIDATE) == NULL) {
      PTRACE_IF(4, m_state != e_Completed, m_owner << m_subchannel << ", "
                << (m_state == e_Offering ? "early" : "answer")
                << " STUN request in ICE, awaiting USE-CANDIDATE");
      candidate->m_state = e_CandidateInProgress;
      return false;
    }

#if OPAL_STATISTICS
    ++candidate->m_remote.m_nominations;
    candidate->m_remote.m_lastNomination.SetCurrentTime();
#endif

    /* Already got this candidate, so don't do any more processing, but still return
       false as we don't want next layer in stack trying to use this STUN packet. */
    if (m_state == e_Completed && m_selectedCandidate.m_baseTransportAddress == ap)
      return false;

    candidate->m_state = e_CandidateSucceeded;
    PTRACE(3, m_owner << m_subchannel << ", found USE-CANDIDATE from " << ap);

    /* With ICE-lite (which only supports regular nomination), only one candidate pair
       should ever be selected. However, Firefox only supports aggressive nomination,
       see https://bugzilla.mozilla.org/show_bug.cgi?id=1034964 which means we have
       to implement at least that small part of full ICE. So, we check for if we
       already have a selected candidate, and ignore any others unless one with a
       higher priority arrives. */
    if (m_selectedCandidate.m_type != PNatCandidate::EndTypes) {
      bool useNewCandidate;
      if (!m_owner.m_useNetworkCost)
        useNewCandidate = candidate->m_remote.m_priority > m_selectedCandidate.m_priority;
      else if (candidate->m_remote.m_networkCost < m_selectedCandidate.m_networkCost)
        useNewCandidate = true;
      else if (candidate->m_remote.m_networkCost > m_selectedCandidate.m_networkCost)
        useNewCandidate = false;
      else
        useNewCandidate = candidate->m_remote.m_priority > m_selectedCandidate.m_priority;
      if (!useNewCandidate)
        return false;
      PTRACE(3, m_owner << m_subchannel << ", found better candidate: " << candidate->m_remote);
    }
    else {
      PTRACE(3, m_owner << m_subchannel << ", found initial candidate: " << candidate->m_remote);
    }
  }
  else if (message.IsSuccessResponse()) {
    if (m_state != e_Offering) {
      PTRACE(3, m_owner << m_subchannel << ", unexpected STUN response in ICE: " << message);
      return false;
    }

    if (!m_owner.m_client.ValidateMessageIntegrity(message))
      return false;
  }
  else {
    PTRACE(5, m_owner << m_subchannel << ", unexpected STUN message in ICE: " << message);
    return false;
  }

#if OPAL_STATISTICS
  for (CandidatePairs::iterator it = m_checklist.begin(); it != m_checklist.end(); ++it)
    it->m_remote.m_selected = it->m_state == e_CandidateSucceeded;
#endif
  m_selectedCandidate = candidate->m_remote;

  // Do not complete, if we just got an early USE-CANDIDATE
  if (m_state != e_Offering) {
    m_owner.InternalSetRemoteAddress(ap, m_subchannel, e_RemoteAddressFromICE);
    m_state = e_Completed;
  }

  // Don't pass this STUN packet up the protocol stack
  return false;
}


static std::string IceTransactionKey(const PBYTEArray & id)
{
  return std::string((const char *)id.GetPointer(), (size_t)id.GetSize());
}


static bool ResolveStunServer(const PString & server, PIPSocketAddressAndPort & address)
{
  if (server.IsEmpty())
    return false;

  PString host = server;
  WORD port = 3478;
  PINDEX colon = server.Find(':');
  if (colon != P_MAX_INDEX && server.Left(1) != "[") {
    host = server.Left(colon);
    port = (WORD)server.Mid(colon + 1).AsUnsigned();
    if (port == 0)
      port = 3478;
  }

  PIPSocket::Address ip;
  if (!PIPSocket::GetHostAddress(host, ip))
    return false;
  address.SetAddress(ip, port);
  return address.IsValid();
}


void OpalICEMediaTransport::NoteTransaction(const PBYTEArray & id, SubChannels subchannel, int pairIndex, bool gather, bool consent)
{
  PendingTxn txn;
  txn.subchannel = subchannel;
  txn.pairIndex = pairIndex;
  txn.gather = gather;
  txn.consent = consent;
  m_pending[IceTransactionKey(id)] = txn;
}


bool OpalICEMediaTransport::TakeTransaction(const PBYTEArray & id, SubChannels & subchannel, int & pairIndex, bool & gather, bool & consent)
{
  std::map<std::string, PendingTxn>::iterator it = m_pending.find(IceTransactionKey(id));
  if (it == m_pending.end())
    return false;
  subchannel = it->second.subchannel;
  pairIndex = it->second.pairIndex;
  gather = it->second.gather;
  consent = it->second.consent;
  if (!gather)
    m_pending.erase(it);
  return true;
}


void OpalICEMediaTransport::GatherHostCandidates()
{
  PIPSocket::InterfaceTable interfaces;
  bool haveInterfaces = PIPSocket::GetInterfaceTable(interfaces);

  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel) {
    ICEChannel & channel = GetICEChannel(subchannel);
    channel.m_agent.SetLocalLite(false);
    channel.m_agent.SetRelayOnly(m_relayOnly);
    channel.m_agent.SetConsentRequired(m_consentFreshness);

    PUDPSocket * socket = GetSubChannelAsSocket(subchannel);
    if (socket == NULL)
      continue;

    PIPSocketAddressAndPort bound;
    if (!socket->GetLocalAddress(bound))
      continue;

    unsigned preference = 65535;
    std::set<std::string> seen;
    bool wantIPv6 = bound.GetAddress().GetVersion() == 6 || bound.GetAddress().IsAny();

    if (haveInterfaces && (bound.GetAddress().IsAny() || bound.GetAddress().IsLoopback())) {
      for (PINDEX i = 0; i < interfaces.GetSize(); ++i) {
        PIPSocket::Address addr = interfaces[i].GetAddress();
        if (!addr.IsValid() || addr.IsAny() || addr.IsLoopback() || addr.IsMulticast() || addr.IsLinkLocal())
          continue;
        if (!wantIPv6 && addr.GetVersion() != 4)
          continue;
        if (bound.GetAddress().GetVersion() == 6 && addr.GetVersion() != 6)
          continue;
        std::string key = addr.AsString();
        if (!seen.insert(key).second)
          continue;

        PNatCandidate candidate(PNatCandidate::HostType,
                                (PNatMethod::Component)(subchannel + 1),
                                OpalICEAgent::Foundation(PNatCandidate::HostType, addr.AsString(), "udp", PString::Empty()),
                                OpalICEAgent::CandidatePriority(PNatCandidate::HostType, preference, subchannel + 1),
                                "udp");
        candidate.m_baseTransportAddress.SetAddress(addr, bound.GetPort());
        channel.m_agent.AddLocalCandidate(candidate);
        if (preference > 256)
          preference -= 256;
        PTRACE(4, *this << "host candidate " << candidate.m_baseTransportAddress);
      }
    }

    if (!bound.GetAddress().IsAny()) {
      PNatCandidate candidate(PNatCandidate::HostType,
                              (PNatMethod::Component)(subchannel + 1),
                              OpalICEAgent::Foundation(PNatCandidate::HostType, bound.AsString(), "udp", PString::Empty()),
                              OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 65535, subchannel + 1),
                              "udp");
      candidate.m_baseTransportAddress = bound;
      channel.m_agent.AddLocalCandidate(candidate);
    }

    if (channel.m_state == e_Disabled)
      channel.m_state = e_Offering;

#if P_HAS_IPV6
    if (wantIPv6 && bound.GetAddress().GetVersion() == 4) {
      PIPSocket::Address v6any("::");
      PUDPSocket * v6 = new PUDPSocket((WORD)0, AF_INET6);
      if (v6any.IsValid() && v6->Listen(v6any, 1, bound.GetPort())) {
        m_extraSockets.push_back(v6);
        PIPSocketAddressAndPort v6bound;
        v6->GetLocalAddress(v6bound);
        PTRACE(4, *this << "additional IPv6 socket " << v6bound);
        if (haveInterfaces) {
          for (PINDEX i = 0; i < interfaces.GetSize(); ++i) {
            PIPSocket::Address addr = interfaces[i].GetAddress();
            if (!addr.IsValid() || addr.GetVersion() != 6 || addr.IsLoopback() || addr.IsLinkLocal() || addr.IsMulticast())
              continue;
            PNatCandidate candidate(PNatCandidate::HostType,
                                    (PNatMethod::Component)(subchannel + 1),
                                    OpalICEAgent::Foundation(PNatCandidate::HostType, addr.AsString(), "udp", PString::Empty()),
                                    OpalICEAgent::CandidatePriority(PNatCandidate::HostType, preference, subchannel + 1),
                                    "udp");
            candidate.m_baseTransportAddress.SetAddress(addr, v6bound.GetPort());
            channel.m_agent.AddLocalCandidate(candidate);
          }
        }
      }
      else {
        delete v6;
        PTRACE(4, *this << "IPv6 host socket was not created");
      }
    }
#endif
  }
}


void OpalICEMediaTransport::StartGathering()
{
  if (m_localLite)
    return;
  if (m_gatherThread != NULL) {
    if (!m_gatheringComplete)
      return;
    delete m_gatherThread;
    m_gatherThread = NULL;
  }
  m_stopGather = false;
  m_gatheringComplete = false;
  m_gatherThread = new PThreadObj<OpalICEMediaTransport>(*this, &OpalICEMediaTransport::GatherThreadMain, false, "ICE-Gather");
}


void OpalICEMediaTransport::GatherThreadMain()
{
  PTRACE(4, *this << "gathering server-reflexive and relay candidates");

  OpalManager & manager = m_connection.GetEndPoint().GetManager();
  vector<PIPSocketAddressAndPort> stunServers;
  PNatMethods & methods = manager.GetNatMethods();
  for (PINDEX i = 0; i < methods.GetSize(); ++i) {
    PNatMethod & method = methods[i];
    if (!method.IsActive())
      continue;
    if (method.GetMethodName() != PSTUNClient::MethodName())
      continue;
    PIPSocketAddressAndPort server;
    if (ResolveStunServer(method.GetServer(), server))
      stunServers.push_back(server);
  }

  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size() && !m_stopGather; ++subchannel) {
    PUDPSocket * socket = GetSubChannelAsSocket(subchannel);
    if (socket == NULL)
      continue;
    for (size_t s = 0; s < stunServers.size(); ++s) {
      PSTUNMessage request(PSTUNMessage::BindingRequest);
      request.AddFingerprint();
      {
        P_INSTRUMENTED_LOCK_READ_WRITE(continue);
        NoteTransaction(request.GetTransactionID(), subchannel, -1, true, false);
      }
      if (!request.Write(*socket, stunServers[s])) {
        PTRACE(3, *this << "STUN binding send failed for " << stunServers[s]);
        continue;
      }
      PTRACE(4, *this << "sent STUN binding to " << stunServers[s] << " on " << subchannel);
    }
  }

#if P_TURN
  PNatMethod * turnMethod = methods.GetMethodByName(PTURNClient::MethodName());
  PTURNClient * turnClient = dynamic_cast<PTURNClient *>(turnMethod);
  if (turnClient != NULL && turnClient->IsActive() && !turnClient->GetServer().IsEmpty()) {
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size() && !m_stopGather; ++subchannel) {
      if (subchannel < (SubChannels)m_turnSockets.size() && m_turnSockets[subchannel] != NULL)
        continue;

      PIPSocketAddressAndPort bound;
      PUDPSocket * host = GetSubChannelAsSocket(subchannel);
      PIPSocket::Address bindAddress = PIPSocket::GetDefaultIpAny();
      if (host != NULL && host->GetLocalAddress(bound) && !bound.GetAddress().IsAny())
        bindAddress = bound.GetAddress();

      PTURNUDPSocket * turnSocket = new PTURNUDPSocket((PNatMethod::Component)(subchannel + 1));
      if (!turnSocket->Listen(bindAddress, 1, 0)) {
        PTRACE(2, *this << "TURN socket bind failed");
        delete turnSocket;
        continue;
      }

      {
        PWaitAndSignal lock(m_gatherMutex);
        m_allocatingSocket = turnSocket;
      }
      int code = m_stopGather ? -1 : turnSocket->OpenTURN(*turnClient);
      {
        PWaitAndSignal lock(m_gatherMutex);
        m_allocatingSocket = NULL;
      }
      if (code != 0 || m_stopGather) {
        PTRACE(2, *this << "TURN allocation failed, code=" << code);
        delete turnSocket;
        continue;
      }

      PNatCandidate info;
      turnSocket->GetCandidateInfo(info);
      PNatCandidate relay(PNatCandidate::RelayType,
                          (PNatMethod::Component)(subchannel + 1),
                          OpalICEAgent::Foundation(PNatCandidate::RelayType, info.m_localTransportAddress.AsString(), "udp", turnClient->GetServer()),
                          OpalICEAgent::CandidatePriority(PNatCandidate::RelayType, 65535, subchannel + 1),
                          "udp");
      relay.m_baseTransportAddress = info.m_localTransportAddress;
      relay.m_localTransportAddress = info.m_baseTransportAddress;
      if (!relay.m_baseTransportAddress.IsValid()) {
        PTRACE(2, *this << "TURN allocation did not produce a relay address");
        delete turnSocket;
        continue;
      }

      bool stored = true;
      {
        P_INSTRUMENTED_LOCK_READ_WRITE(stored = false);
        if (stored) {
          while (m_turnSockets.size() <= (size_t)subchannel)
            m_turnSockets.push_back(NULL);
          m_turnSockets[subchannel] = turnSocket;
          GetICEChannel(subchannel).m_agent.AddLocalCandidate(relay);
          if (!m_remoteUsername.IsEmpty())
            GetICEChannel(subchannel).m_agent.FormChecklist();
          PTRACE(3, *this << "relay candidate " << relay.m_baseTransportAddress);
        }
      }
      if (!stored) {
        delete turnSocket;
        continue;
      }
    }
  }
#endif

  PTimeInterval waited(0);
  while (!m_stopGather && waited < m_gatherTimeout) {
    PThread::Sleep(100);
    waited += 100;
  }

  if (!m_stopGather) {
    P_INSTRUMENTED_LOCK_READ_WRITE(return);
    m_gatheringComplete = true;
    PTRACE(3, *this << "candidate gathering complete");
  }
}


PUDPSocket * OpalICEMediaTransport::SocketForCandidate(SubChannels subchannel, const PNatCandidate & local) const
{
  if (local.m_type == PNatCandidate::RelayType) {
    if (subchannel < (SubChannels)m_turnSockets.size())
      return m_turnSockets[subchannel];
    return NULL;
  }

  unsigned version = 4;
  if (local.m_type == PNatCandidate::HostType)
    version = local.m_baseTransportAddress.GetAddress().GetVersion();
  else if (local.m_localTransportAddress.IsValid())
    version = local.m_localTransportAddress.GetAddress().GetVersion();
  else
    version = local.m_baseTransportAddress.GetAddress().GetVersion();

  PUDPSocket * host = GetSubChannelAsSocket(subchannel);
  PIPSocketAddressAndPort bound;
  if (host != NULL && host->GetLocalAddress(bound) && bound.GetAddress().GetVersion() == version)
    return host;

  for (vector<PUDPSocket *>::const_iterator it = m_extraSockets.begin(); it != m_extraSockets.end(); ++it) {
    PIPSocketAddressAndPort extra;
    if (*it != NULL && (*it)->GetLocalAddress(extra) && extra.GetAddress().GetVersion() == version)
      return *it;
  }
  return host;
}


void OpalICEMediaTransport::SendOneCheck()
{
  static const PTimeInterval RTO(500);
  static const unsigned MaxAttempts = 7;

  bool anyFailed = false;
  bool anyRunning = false;
  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_iceChannelCache.size(); ++subchannel) {
    ICEChannel & channel = GetICEChannel(subchannel);
    OpalICEAgent & agent = channel.m_agent;
    if (agent.GetState() == OpalICEAgent::Failed) {
      anyFailed = true;
      continue;
    }
    if (agent.GetState() == OpalICEAgent::Completed)
      continue;
    anyRunning = true;

    int index = agent.NextCheck(PTime(), RTO, MaxAttempts);
    if (agent.GetState() == OpalICEAgent::Failed) {
      anyFailed = true;
      PTRACE(2, *this << "ICE failed on " << subchannel << ": " << agent.GetFailureReason());
      continue;
    }
    if (index < 0)
      continue;

    const OpalICEAgent::Pair & pair = agent.Checklist()[index];
    bool nominate = agent.IsControlling() && pair.state == OpalICEAgent::Succeeded;
    PUDPSocket * socket = SocketForCandidate(subchannel, pair.local);
    if (socket == NULL) {
      PTRACE(2, *this << "no socket for candidate " << pair.local);
      continue;
    }

    if (pair.local.m_type == PNatCandidate::RelayType)
      socket->SetSendAddress(pair.remote.m_baseTransportAddress);

    const BYTE * existingId = NULL;
    if (pair.state == OpalICEAgent::InProgress && pair.transactionId.GetSize() >= 12)
      existingId = pair.transactionId.GetPointer();

    PSTUNMessage request(PSTUNMessage::BindingRequest, existingId);
    PSTUNIcePriority priority;
    priority.m_priority = pair.local.m_priority;
    request.AddAttribute(priority);
    if (nominate)
      request.AddAttribute(PSTUNAttribute(PSTUNAttribute::USE_CANDIDATE, 0));
    m_client.AppendMessageIntegrity(request);
    request.AddFingerprint();

    bool sent = pair.local.m_type == PNatCandidate::RelayType
                  ? request.Write(*socket)
                  : request.Write(*socket, pair.remote.m_baseTransportAddress);
    if (!sent) {
      PTRACE(3, *this << "connectivity check send failed to " << pair.remote.m_baseTransportAddress);
      continue;
    }

    agent.NoteCheckSent(index, request.GetTransactionID(), PTime(), nominate);
    NoteTransaction(request.GetTransactionID(), subchannel, index, false, false);
    PTRACE(4, *this << "check " << (nominate ? "nominate " : "")
           << pair.local.m_baseTransportAddress << " -> " << pair.remote.m_baseTransportAddress
           << " priority=" << pair.priority);
  }

  if (anyFailed && !anyRunning) {
    m_checklistFailed = true;
    m_checkTimer.Stop(false);
    PTRACE(2, *this << "ICE checklist failed; media will not fall back to the SDP address");
  }
}


void OpalICEMediaTransport::ApplyNominated(SubChannels subchannel, const PNatCandidate & local, const PNatCandidate & remote)
{
  ICEChannel & channel = GetICEChannel(subchannel);
  channel.m_state = e_Completed;
  channel.m_selectedCandidate = remote;
  channel.m_relaySelected = local.m_type == PNatCandidate::RelayType;
  channel.m_agent.NoteConsent(PTime());

  if (channel.m_relaySelected) {
    PUDPSocket * turn = SocketForCandidate(subchannel, local);
    if (turn != NULL)
      turn->SetSendAddress(remote.m_baseTransportAddress);
  }
  else
    InternalSetRemoteAddress(remote.m_baseTransportAddress, subchannel, e_RemoteAddressFromICE);

  bool allDone = true;
  for (SubChannels other = e_Media; (size_t)other < m_iceChannelCache.size(); ++other) {
    if (GetICEChannel(other).m_agent.GetState() != OpalICEAgent::Completed)
      allDone = false;
  }
  if (allDone) {
    m_checkTimer.Stop(false);
    PTimeInterval keepAlive = m_connection.GetEndPoint().GetManager().GetNatKeepAliveTime();
    if (keepAlive == 0)
      keepAlive = m_consentFreshness ? PTimeInterval(0, 5) : PTimeInterval(0, 15);
    if (m_consentFreshness && keepAlive > PTimeInterval(0, 5))
      keepAlive = PTimeInterval(0, 5);
    m_consentTimer.RunContinuous(keepAlive);
  }
  TraceSelectedPath(subchannel);
}


void OpalICEMediaTransport::TraceSelectedPath(SubChannels subchannel) const
{
  PStringStream strm;
  GetICEChannel(subchannel).m_agent.PrintSelectedPath(strm);
  PTRACE(3, *this << subchannel << " selected path\n" << strm);
}


void OpalICEMediaTransport::AssignRoles()
{
  bool controlling = m_localLite ? false : (m_offerer || m_remoteLite);
  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_iceChannelCache.size(); ++subchannel) {
    OpalICEAgent & agent = GetICEChannel(subchannel).m_agent;
    agent.SetLocalLite(m_localLite);
    agent.SetRemoteLite(m_remoteLite);
    agent.SetOfferer(m_offerer || m_remoteLite);
    agent.SetRelayOnly(m_relayOnly);
    agent.SetConsentRequired(m_consentFreshness);
    agent.AssignRole();
  }

  PSTUN::IceRole role = PSTUN::IceLite;
  if (!m_localLite)
    role = controlling ? PSTUN::IceControlling : PSTUN::IceControlled;

  if (m_server.GetIceRole() == role && m_server.GetIceTieBreak() != 0) {
    m_client.AdoptIceRole(role, m_server.GetIceTieBreak());
    return;
  }

  m_server.SetIceRole(role);
  m_client.AdoptIceRole(role, m_server.GetIceTieBreak());
  PTRACE(3, *this << "ICE role " << (role == PSTUN::IceControlling ? "controlling" : role == PSTUN::IceControlled ? "controlled" : "lite"));
}


void OpalICEMediaTransport::OnCheckTimer(PTimer &, P_INT_PTR)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return);
  if (m_stopGather || m_localLite || m_iceDisabled || m_checklistFailed)
    return;
  SendOneCheck();
}


void OpalICEMediaTransport::SendConsentOrKeepalive()
{
  for (SubChannels subchannel = e_Media; (size_t)subchannel < m_iceChannelCache.size(); ++subchannel) {
    ICEChannel & channel = GetICEChannel(subchannel);
    PNatCandidate local, remote;
    if (!channel.m_agent.GetNominated(local, remote))
      continue;

    PUDPSocket * socket = SocketForCandidate(subchannel, local);
    if (socket == NULL)
      continue;

    if (m_consentFreshness) {
      PSTUNMessage request(PSTUNMessage::BindingRequest);
      m_client.AppendMessageIntegrity(request);
      request.AddFingerprint();
      bool sent = local.m_type == PNatCandidate::RelayType ? request.Write(*socket) : request.Write(*socket, remote.m_baseTransportAddress);
      if (sent)
        NoteTransaction(request.GetTransactionID(), subchannel, -1, false, true);
      PTRACE(5, *this << "consent check on " << subchannel);
    }
    else {
      PSTUNMessage indication(PSTUNMessage::BindingIndication);
      indication.AddFingerprint();
      if (local.m_type == PNatCandidate::RelayType)
        indication.Write(*socket);
      else
        indication.Write(*socket, remote.m_baseTransportAddress);
      PTRACE(5, *this << "binding indication keepalive on " << subchannel);
    }
  }
}


void OpalICEMediaTransport::OnConsentTimer(PTimer &, P_INT_PTR)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return);
  if (m_stopGather || m_iceDisabled)
    return;
  SendConsentOrKeepalive();
}


bool OpalICEMediaTransport::ICEChannel::HandleICEFull(const void * data, PINDEX length)
{
  if (length < 1)
    return false;

  if (m_owner.m_subchannels[m_subchannel].m_remoteGoneError == PChannel::Unavailable)
    m_owner.m_subchannels[m_subchannel].m_remoteGoneError = PChannel::ProtocolFailure;

  const BYTE * byteData = static_cast<const BYTE *>(data);
  if (byteData[0] > 3) {
    if (m_agent.ConsentExpired(PTime(), PTimeInterval(0, 30)))
      return false;
    return true;
  }

  PIPSocketAddressAndPort from;
  if (m_haveLastFrom)
    from = m_lastFrom;
  else if (PUDPSocket * socket = m_owner.GetSubChannelAsSocket(m_subchannel))
    socket->GetLastReceiveAddress(from);

  PSTUNMessage message(byteData, length, from);
  if (!message.IsValid()) {
    PTRACE(2, m_owner << m_subchannel << ", discarding invalid STUN from " << from);
    return false;
  }
  if (!message.CheckFingerprint(false) && message.FindAttribute(PSTUNAttribute::FINGERPRINT) != NULL) {
    PTRACE(2, m_owner << m_subchannel << ", bad STUN fingerprint from " << from);
    return false;
  }

  if (message.IsIndication())
    return false;

  if (message.IsRequest()) {
    if (!m_owner.m_server.OnReceiveMessage(message, PSTUNServer::SocketInfo(m_owner.GetSubChannelAsSocket(m_subchannel))))
      return false;

    PSTUN::IceRole serverRole = m_owner.m_server.GetIceRole();
    if (serverRole == PSTUN::IceControlling || serverRole == PSTUN::IceControlled) {
      OpalICEAgent::Role agentRole = serverRole == PSTUN::IceControlling ? OpalICEAgent::Controlling : OpalICEAgent::Controlled;
      if (m_agent.GetRole() != agentRole) {
        m_agent.SetRole(agentRole);
        m_owner.m_client.AdoptIceRole(serverRole, m_owner.m_server.GetIceTieBreak());
        PTRACE(3, m_owner << "role conflict resolved, now " << (agentRole == OpalICEAgent::Controlling ? "controlling" : "controlled"));
      }
    }

    unsigned priority = 0;
    if (PSTUNIcePriority * pri = PSTUNIcePriority::Find(message))
      priority = pri->m_priority;
    bool useCandidate = message.FindAttribute(PSTUNAttribute::USE_CANDIDATE) != NULL;
    OpalICEAgent::InboundResult inbound = m_agent.OnInboundRequest(from, priority, useCandidate);
    if (inbound.nominated)
      m_owner.ApplyNominated(m_subchannel, inbound.local, inbound.remote);
    return false;
  }

  SubChannels txnChannel;
  int pairIndex = -1;
  bool gather = false;
  bool consent = false;
  if (!m_owner.TakeTransaction(message.GetTransactionID(), txnChannel, pairIndex, gather, consent) || txnChannel != m_subchannel) {
    PTRACE(4, m_owner << m_subchannel << ", ignoring unmatched STUN response");
    return false;
  }

  if (gather) {
    if (!message.IsSuccessResponse())
      return false;
    PIPSocketAddressAndPort mapped;
    if (!m_owner.m_client.GetFromBindingResponse(message, mapped) || !mapped.IsValid())
      return false;
    if (m_agent.LocalCandidates().empty())
      return false;
    const PNatCandidate & host = m_agent.LocalCandidates().front();
    PNatCandidate srflx(PNatCandidate::ServerReflexiveType,
                        (PNatMethod::Component)(m_subchannel + 1),
                        OpalICEAgent::Foundation(PNatCandidate::ServerReflexiveType, host.m_baseTransportAddress.AsString(), "udp", from.AsString()),
                        OpalICEAgent::CandidatePriority(PNatCandidate::ServerReflexiveType, 65535, m_subchannel + 1),
                        "udp");
    srflx.m_baseTransportAddress = mapped;
    srflx.m_localTransportAddress = host.m_baseTransportAddress;
    m_agent.AddLocalCandidate(srflx);
    if (!m_owner.m_remoteUsername.IsEmpty())
      m_agent.FormChecklist();
    PTRACE(3, m_owner << "server-reflexive candidate " << mapped);
    return false;
  }

  if (message.IsSuccessResponse() && !m_owner.m_client.IntegrityMatches(message)) {
    PTRACE(2, m_owner << m_subchannel << ", STUN response failed integrity");
    return false;
  }

  if (consent) {
    if (message.IsSuccessResponse())
      m_agent.NoteConsent(PTime());
    return false;
  }

  unsigned errorCode = 0;
  if (message.IsErrorResponse()) {
    if (PSTUNErrorCode * error = message.FindAttributeAs<PSTUNErrorCode>(PSTUNAttribute::ERROR_CODE))
      errorCode = (unsigned)error->GetErrorCode();
    if (errorCode == 487)
      PTRACE(3, m_owner << "role conflict (487) on " << m_subchannel);
  }

  PIPSocketAddressAndPort mapped;
  if (message.IsSuccessResponse())
    m_owner.m_client.GetFromBindingResponse(message, mapped);

  OpalICEAgent::CheckResult result = m_agent.OnCheckResult(message.GetTransactionID(),
                                                           message.IsSuccessResponse(),
                                                           errorCode,
                                                           mapped,
                                                           PTime());

  if (result.roleConflict) {
    PSTUN::IceRole role = m_agent.IsControlling() ? PSTUN::IceControlling : PSTUN::IceControlled;
    m_owner.m_server.AdoptIceRole(role, m_owner.m_server.GetIceTieBreak());
    m_owner.m_client.AdoptIceRole(role, m_owner.m_server.GetIceTieBreak());
    PTRACE(3, m_owner << "switched ICE role after 487 to " << (m_agent.IsControlling() ? "controlling" : "controlled"));
  }

  if (consent)
    m_agent.NoteConsent(PTime());

  if (result.nominated)
    m_owner.ApplyNominated(m_subchannel, result.local, result.remote);

  if (result.checklistFailed) {
    m_owner.m_checklistFailed = true;
    m_owner.m_checkTimer.Stop(false);
    PTRACE(2, m_owner << "ICE failed: " << m_agent.GetFailureReason());
  }
  else if (result.addedPeerReflexive)
    PTRACE(3, m_owner << "peer-reflexive candidate discovered");

  return false;
}


#if EXPERIMENTAL_ICE
OpalRTPSession::SendReceiveStatus OpalRTPSession::OnSendICE(Channel channel)
{
  if (m_candidateType == e_LocalCandidates && m_socket[channel] != NULL) {
    for (CandidateSet::iterator it = m_candidates[channel].begin(); it != m_candidates[channel].end(); ++it) {
      if (it->m_baseTransportAddress.IsValid()) {
        PTRACE(4, *this << "sending BINDING-REQUEST to " << *it);
        PSTUNMessage request(PSTUNMessage::BindingRequest);
        request.AddAttribute(PSTUNAttribute::ICE_CONTROLLED); // We are ICE-lite and always controlled
        m_client.AppendMessageIntegrity(request);
        if (!request.Write(*m_socket[channel], it->m_baseTransportAddress))
          return e_AbortTransport;
      }
    }
  }
  return m_remotePort[channel] != 0 ? e_ProcessPacket : e_IgnorePacket;
}
#endif


#endif // OPAL_ICE

/////////////////////////////////////////////////////////////////////////////
