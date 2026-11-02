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
{
  PTRACE_CONTEXT_ID_TO(m_server);
  PTRACE_CONTEXT_ID_TO(m_client);
}


bool OpalICEMediaTransport::Open(OpalMediaSession & session,
                                 PINDEX count,
                                 const PString & localInterface,
                                 const OpalTransportAddress & remoteAddress)
{
  const PStringOptions & options = session.GetStringOptions();
  m_localLite = options.GetBoolean(OPAL_OPT_ICE_LITE, true);
  if (!m_localLite) {
    PTRACE(2, "Only ICE-Lite supported at this time");
    return false;
  }

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
  return true;
}


bool OpalICEMediaTransport::IsEstablished() const
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return false);

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
    for (SubChannels subchannel = e_Media; (size_t)subchannel < m_subchannels.size(); ++subchannel)
      GetICEChannel(subchannel).m_state = e_Disabled;
    return;
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
    if (it->m_component > 0 && (size_t)it->m_component <= m_subchannels.size())
      GetICEChannel((SubChannels)(it->m_component - 1)).AddRemoteCandidate(*it PTRACE_PARAM(, "offer/answer"));
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

  SetRemoteBehindNAT();

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

  if (candidate.m_component > 0 && (size_t)candidate.m_component <= m_subchannels.size())
    GetICEChannel((SubChannels)(candidate.m_component - 1)).AddRemoteCandidate(candidate PTRACE_PARAM(, "trickle"));
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
                                          bool /*offering*/)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return false);

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

  for (ChannelArray::iterator it = m_subchannels.begin(); it != m_subchannels.end(); ++it)
    GetICEChannel(it->m_subchannel).GetLocalCandidates(candidates);

  if (candidates.empty()) {
    PTRACE(3, *this << "no candidates to offer");
    return false;
  }

  if (m_localTrickle) {
    options += OptTrickle;
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

  candidate.m_priority = (CandidateTypePriority[candidate.m_type] << 24) | (256 - candidate.m_component);

  if (candidate.m_baseTransportAddress.GetAddress().GetVersion() != 6)
    candidate.m_priority |= 0xffff00;
  else {
    /* Incomplete need to get precedence from following table, for now use 50
          Prefix        Precedence Label
          ::1/128               50     0
          ::/0                  40     1
          2002::/16             30     2
          ::/96                 20     3
          ::ffff:0:0/96         10     4
    */
    candidate.m_priority |= 50 << 8;
  }

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
{
  Open(channel);
}


PTimeInterval OpalICEMediaTransport::GetTimeout(SubChannels subchannel) const
{
  return GetICEChannel(subchannel).m_state <= e_Completed ? m_mediaTimeout : m_iceTimeout;
}


PBoolean OpalICEMediaTransport::ICEChannel::Read(void * data, PINDEX size)
{
  for (;;) {
    if (!PIndirectChannel::Read(data, size))
      return false;
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
