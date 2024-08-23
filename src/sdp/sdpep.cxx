/*
 * sdpep.cxx
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (C) 2014 Vox Lucida Pty. Ltd.
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
 * The Initial Developer of the Original Code is Post Increment
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "sdpep.h"
#endif

#include <sdp/sdpep.h>

#if OPAL_SDP

#include <rtp/rtpconn.h>
#include <rtp/rtp_stream.h>
#include <rtp/dtls_srtp_session.h>
#include <codec/rfc2833.h>
#include <opal/transcoders.h>
#include <opal/patch.h>
#include <ptclib/random.h>
#include <sdp/ice.h>


#define PTraceModule() "SDP-EP"


///////////////////////////////////////////////////////////////////////////////

const PCaselessString & OpalSDPEndPoint::ContentType() { static PConstCaselessString const s("application/sdp"); return s; }

OpalSDPEndPoint::OpalSDPEndPoint(OpalManager & manager,     ///<  Manager of all endpoints.
                       const PCaselessString & prefix,      ///<  Prefix for URL style address strings
                                  Attributes   attributes)  ///<  Bit mask of attributes endpoint has
  : OpalRTPEndPoint(manager, prefix, attributes)
  , m_holdTimeout(0, 40) // Seconds
{
}


OpalSDPEndPoint::~OpalSDPEndPoint()
{
}


PStringList OpalSDPEndPoint::GetAvailableStringOptions() const
{
  static char const * const StringOpts[] = {
    OPAL_OPT_OFFER_SDP_PTIME,
    OPAL_OPT_OFFER_RTCP_FB,
    OPAL_OPT_FORCE_RTCP_FB,
    OPAL_OPT_SUPPRESS_UDP_TLS,
    OPAL_OPT_RTCP_MUX,
    OPAL_OPT_OFFER_REDUCED_SIZE_RTCP,
    OPAL_OPT_SUPPRESS_UDP_TLS,
    #ifdef OPAL_ICE
      OPAL_OPT_OFFER_ICE,
      OPAL_OPT_ICE_DISABLE_mDNS,
    #endif
    OPAL_OPT_ALLOW_MUSIC_ON_HOLD,
    OPAL_OPT_AV_BUNDLE,
    OPAL_OPT_BUNDLE_ONLY,
    OPAL_OPT_ENABLE_RID,
    OPAL_OPT_SIMULCAST,
    OPAL_OPT_INACTIVE_AUDIO_FLOW,
    OPAL_OPT_MULTI_SSRC
  };

  return OpalRTPEndPoint::GetAvailableStringOptions() + PStringList(PARRAYSIZE(StringOpts), StringOpts, true);
}


SDPSessionDescription * OpalSDPEndPoint::CreateSDP(time_t sessionId, unsigned version, const OpalTransportAddress & address)
{
  return new SDPSessionDescription(sessionId, version, address);
}


///////////////////////////////////////////////////////////////////////////////

OpalSDPConnection::OpalSDPConnection(OpalCall & call,
                             OpalSDPEndPoint  & ep,
                                const PString & token,
                                   unsigned int options,
                                StringOptions * stringOptions)
  : OpalRTPConnection(call, ep, token, options, stringOptions)
  , m_endpoint(ep)
  , m_offerPending(false)
  , m_sdpSessionId(PTime().GetTimeInSeconds())
  , m_sdpVersion(0)
  , m_sdpVersionFromRemote(UINT_MAX)
  , m_holdToRemote(eHoldOff)
  , m_holdFromRemote(false)
{
}


OpalSDPConnection::~OpalSDPConnection()
{
}


void OpalSDPConnection::SetSimulcastOffers(const SimulcastOffer & sendOffer, const SimulcastOffer & recvOffer)
{
  m_simulcastOffers[SDPMediaDescription::e_Send] = sendOffer;
  m_simulcastOffers[SDPMediaDescription::e_Recv] = recvOffer;
  m_stringOptions.SetBoolean(OPAL_OPT_SIMULCAST, true);
  m_stringOptions.SetBoolean(OPAL_OPT_ENABLE_RID, true);
}


OpalMediaFormatList OpalSDPConnection::GetMediaFormats() const
{
  // Need to limit the media formats to what the other side provided in it's offer
  if (!m_activeFormatList.IsEmpty()) {
    PTRACE(4, "Using offered media format list: " << setfill(',') << m_activeFormatList);
    return m_activeFormatList;
  }

  if (!m_remoteFormatList.IsEmpty()) {
    PTRACE(4, "Using remote media format list: " << setfill(',') << m_remoteFormatList);
    return m_remoteFormatList;
  }

  return OpalMediaFormatList();
}


void OpalSDPConnection::OnApplyStringOptions()
{
  if (m_stringOptions.GetBoolean(OPAL_OPT_SIMULCAST))
    m_stringOptions.SetBoolean(OPAL_OPT_ENABLE_RID, true);
  OpalRTPConnection::OnApplyStringOptions();
}


bool OpalSDPConnection::HoldRemote(bool placeOnHold)
{
  PSafeLockReadWrite lock(*this);
  if (!lock.IsLocked())
    return false;

  switch (m_holdToRemote) {
    case eHoldOff :
    case eRetrieveInProgress :
      if (!placeOnHold) {
        PTRACE(4, "Hold off request ignored as not on hold for " << *this);
        return true;
      }
      break;

    case eHoldOn :
    case eHoldInProgress :
      if (placeOnHold) {
        PTRACE(4, "Hold on request ignored as already on hold fir " << *this);
        return true;
      }
      break;
  }

  HoldState origState = m_holdToRemote;

  switch (m_holdToRemote) {
    case eHoldOff :
      m_holdToRemote = eHoldInProgress;
      break;

    case eHoldOn :
      m_holdToRemote = eRetrieveInProgress;
      break;

    case eRetrieveInProgress :
    case eHoldInProgress :
      PTRACE(4, "Hold " << (placeOnHold ? "on" : "off") << " request deferred as in progress for " << *this);
      GetEndPoint().GetManager().QueueDecoupledEvent(new PSafeWorkArg1<OpalSDPConnection, bool>(
                                         this, placeOnHold, &OpalSDPConnection::RetryHoldRemote));
      return true;
  }

  if (OnHoldStateChanged(placeOnHold))
    return true;

  m_holdToRemote = origState;
  return false;
}


void OpalSDPConnection::RetryHoldRemote(bool placeOnHold)
{
  HoldState progressState = placeOnHold ? eRetrieveInProgress : eHoldInProgress;
  PSimpleTimer failsafe(m_endpoint.GetHoldTimeout());
  while (m_holdToRemote == progressState) {
    PThread::Sleep(100);

    if (IsReleased() || failsafe.HasExpired()) {
      PTRACE(3, "Hold " << (placeOnHold ? "on" : "off") << " request failed for " << *this);
      return;
    }

    PTRACE(5, "Hold " << (placeOnHold ? "on" : "off") << " request still in progress for " << *this);
  }

  HoldRemote(placeOnHold);
}


PBoolean OpalSDPConnection::IsOnHold(bool fromRemote) const
{
  return fromRemote ? m_holdFromRemote : (m_holdToRemote >= eHoldOn);
}


bool OpalSDPConnection::GetOfferSDP(SDPSessionDescription & offer, bool offerOpenMediaStreamsOnly)
{
  if (m_offerPending.exchange(true)) {
    PTRACE(2, "Outgoing offer pending, cannot send another offer.");
    return false;
  }

  if (GetPhase() == UninitialisedPhase) {
    InternalSetAsOriginating();
    SetPhase(SetUpPhase);
    OnApplyStringOptions();
  }

  return OnSendOfferSDP(offer, offerOpenMediaStreamsOnly);
}


PString OpalSDPConnection::GetOfferSDP(bool offerOpenMediaStreamsOnly)
{
  PAutoPtr<SDPSessionDescription> sdp(CreateSDP(PString::Empty()));
  if (sdp.get() == NULL) {
    PTRACE(2, "Could not create SDP");
    return false;
  }

  PTRACE_CONTEXT_ID_TO(*sdp);
  return GetOfferSDP(*sdp, offerOpenMediaStreamsOnly) ? sdp->Encode() : PString::Empty();
}


bool OpalSDPConnection::AnswerOfferSDP(const SDPSessionDescription & offer, SDPSessionDescription & answer)
{
  if (m_offerPending) {
    PTRACE(2, "Outgoing offer pending, cannot handle incoming offer.");
    return false;
  }

  if (GetPhase() == UninitialisedPhase) {
    SetPhase(SetUpPhase);
    OnApplyStringOptions();
    if (!OnIncomingConnection(0, NULL))
      return false;
  }

  return OnSendAnswerSDP(offer, answer);
}


PString OpalSDPConnection::AnswerOfferSDP(const PString & offer)
{
  if (GetPhase() == UninitialisedPhase) {
    SetPhase(SetUpPhase);
    OnApplyStringOptions();
    if (!OnIncomingConnection(0, NULL))
      return PString::Empty();
  }

  PAutoPtr<SDPSessionDescription> sdpIn(CreateSDP(offer));
  PAutoPtr<SDPSessionDescription> sdpOut(CreateSDP(PString::Empty()));
  if (sdpIn.get() == NULL || sdpOut.get() == NULL)
    return PString::Empty();

  PTRACE_CONTEXT_ID_TO(*sdpIn);
  PTRACE_CONTEXT_ID_TO(*sdpOut);

  if (!OnSendAnswerSDP(*sdpIn, *sdpOut))
    return PString::Empty();

  SetConnected();
  return sdpOut->Encode();
}


bool OpalSDPConnection::HandleAnswerSDP(const SDPSessionDescription & answer)
{
  if (!m_offerPending.exchange(false)) {
    PTRACE(1, "Did not send an offer before handling answer");
    return false;
  }

  bool dummy;
  if (!OnReceivedAnswerSDP(answer, dummy))
    return false;

  InternalOnConnected();
  return true;
}


bool OpalSDPConnection::HandleAnswerSDP(const PString & answer)
{
  PAutoPtr<SDPSessionDescription> sdp(CreateSDP(answer));
  PTRACE_CONTEXT_ID_TO(sdp.get());
  return sdp.get() != NULL && HandleAnswerSDP(*sdp);
}


SDPSessionDescription * OpalSDPConnection::CreateSDP(const PString & sdpStr)
{
  if (sdpStr.IsEmpty())
    return m_endpoint.CreateSDP(m_sdpSessionId, ++m_sdpVersion, OpalTransportAddress(GetMediaInterface(), 0, OpalTransportAddress::UdpPrefix()));

  OpalMediaFormatList formats = GetLocalMediaFormats();
  if (formats.IsEmpty())
    formats = OpalMediaFormat::GetAllRegisteredMediaFormats();

  SDPSessionDescription * sdpPtr = m_endpoint.CreateSDP(0, 0, OpalTransportAddress());
  PTRACE_CONTEXT_ID_TO(*sdpPtr);

  sdpPtr->SetStringOptions(m_stringOptions);

  if (sdpPtr->Decode(sdpStr, formats))
    return sdpPtr;

  delete sdpPtr;
  return NULL;
}


bool OpalSDPConnection::SetRemoteMediaFormats(const OpalMediaFormatList & formats)
{
  m_remoteFormatList = formats;
  m_remoteFormatList.MakeUnique();

#if OPAL_T38_CAPABILITY
  /* We default to having T.38 included as most UAs do not actually
     tell you that they support it or not. For the re-INVITE mechanism
     to work correctly, the rest ofthe system has to assume that the
     UA is capable of it, even it it isn't. */
  m_remoteFormatList += OpalT38;
#endif

  AdjustMediaFormats(false, NULL, m_remoteFormatList);

  if (m_remoteFormatList.IsEmpty()) {
    PTRACE(2, "All possible remote media formats were removed.");
    return false;
  }

  PTRACE(4, "Remote media formats set:\n    " << setfill(',') << m_remoteFormatList << setfill(' '));
  return true;
}


bool OpalSDPConnection::OnReceivedSDP(const SDPSessionDescription & sdp)
{
  if (!SetActiveMediaFormats(sdp.GetMediaFormats()))
    return false;

  // Merge received media formats into remote list of formats we have
  // already been told about, if any.
  OpalMediaFormatList newRemoteFormats = m_activeFormatList;
  newRemoteFormats += m_remoteFormatList;
  SetRemoteMediaFormats(newRemoteFormats);

  m_sdpVersionFromRemote = sdp.GetOwnerVersion();
  return true;
}


bool OpalSDPConnection::SetActiveMediaFormats(const OpalMediaFormatList & formats)
{
  if (formats.IsEmpty()) {
    PTRACE(3, "No known media formats in remotes SDP.");
    return false;
  }

  // get the remote media formats
  m_activeFormatList = formats;

  OpalMediaFormatList const localMediaFormats = GetLocalMediaFormats(); // Use function to make sure is set
  // Remove anything we never offered
  while (!m_activeFormatList.IsEmpty() && !localMediaFormats.HasFormat(m_activeFormatList.front()))
    m_activeFormatList.pop_front();

  if (!m_activeFormatList.IsEmpty())
    AdjustMediaFormats(false, NULL, m_activeFormatList);

  if (m_activeFormatList.IsEmpty()) {
    PTRACE(3, "All media formats in remotes SDP have been removed.");
    return false;
  }

  return true;
}


struct OpalSDPConnection::BundleMergeInfo
{
    vector<bool> m_allowPauseRecvMediaStream;
    vector<bool> m_allowPauseSendMediaStream;
    OpalMediaTransportPtr m_transport;
    RTP_SyncSourceArray m_recvSsrcs;
    RTP_SyncSourceArray m_sendSsrcs;
    vector<unsigned> m_sessionId;

    BundleMergeInfo(size_t mediaDescriptionCount)
      : m_allowPauseRecvMediaStream(mediaDescriptionCount+1, true)
      , m_allowPauseSendMediaStream(mediaDescriptionCount+1, true)
      , m_sendSsrcs(mediaDescriptionCount+1)
      , m_sessionId(mediaDescriptionCount+1)
    { }

    void RemoveSessionSSRCs(SessionMap & sessions)
    {
      if (m_recvSsrcs.empty())
        return;

      for (SessionMap::iterator session = sessions.begin(); session != sessions.end(); ++session) {
        OpalRTPSession * rtpSession = dynamic_cast<OpalRTPSession *>(&*session->second);
        if (rtpSession == NULL)
          continue;

        RTP_SyncSourceArray ssrcs = rtpSession->GetSyncSources(OpalRTPSession::e_Receiver);
        for (RTP_SyncSourceArray::const_iterator ssrc = ssrcs.begin(); ssrc != ssrcs.end(); ++ssrc) {
          if (std::find(m_recvSsrcs.begin(), m_recvSsrcs.end(), *ssrc) == m_recvSsrcs.end() &&
              rtpSession->GetRtpStreamId(*ssrc, OpalRTPSession::e_Receiver).IsEmpty()) // Don't clear if using new header extensions
            rtpSession->RemoveSyncSource(*ssrc PTRACE_PARAM(, "clearing non-confirmed SSRC in bundle"));
        }
      }
    }
};


OpalMediaSession * OpalSDPConnection::SetUpMediaSession(const unsigned   sessionId,
                                                   const OpalMediaType & mediaType,
                                             const SDPMediaDescription & mediaDescription,
                                                  OpalTransportAddress & localAddress,
                                                       BundleMergeInfo & bundleMergeInfo)
{
  if (mediaDescription.GetPort() == 0) {
    PTRACE(2, "Received disabled/missing media description for " << mediaType);

    /* Some remotes return all of the media detail (a= lines) in SDP even though
       port is zero indicating the media is not to be used. So don't return these
       bogus media formats from SDP to the "remote media format list". */
    m_remoteFormatList.Remove(PString('@')+mediaType);
    return NULL;
  }

  // Create the OpalMediaSession if required
  OpalMediaSession * session = UseMediaSession(sessionId, mediaType, mediaDescription.GetSessionType());
  if (session == NULL)
    return NULL;

  OpalTransportAddress remoteMediaAddress;
#if OPAL_ICE
  if (mediaDescription.HasICE())
    remoteMediaAddress = GetRemoteMediaAddress();
  else
#endif
  {
    remoteMediaAddress = mediaDescription.GetMediaAddress();
    PTRACE_IF(3, session->IsOpen() && session->GetRemoteAddress() != remoteMediaAddress,
            "Remote changed IP address: " << session->GetRemoteAddress() << " -> " << remoteMediaAddress);
  }

  // Once before opening
  if (!mediaDescription.ToSession(session, bundleMergeInfo.m_recvSsrcs))
    return NULL;

  bool bundled = session->IsGroupMember(OpalMediaSession::GetBundleGroupId());
  if (bundled && bundleMergeInfo.m_transport != NULL)
    session->AttachTransport(bundleMergeInfo.m_transport);

  if (!session->Open(GetMediaInterface(), remoteMediaAddress))
    return NULL;

  if (bundled && bundleMergeInfo.m_transport == NULL)
    bundleMergeInfo.m_transport = session->GetTransport();

  // And again after
  if (!mediaDescription.ToSession(session, bundleMergeInfo.m_recvSsrcs))
    return NULL;

  dynamic_cast<OpalRTPEndPoint &>(m_endpoint).CheckEndLocalRTP(*this, dynamic_cast<OpalRTPSession *>(session));
  localAddress = session->GetLocalAddress();
  return session;
}


static bool SetNxECapabilities(OpalRFC2833Proto * handler,
                      const OpalMediaFormatList & localMediaFormats,
                      const OpalMediaFormatList & remoteMediaFormats,
                          const OpalMediaFormat & baseMediaFormat,
                            SDPMediaDescription * localMedia = NULL)
{
  OpalMediaFormatList::const_iterator remFmt = remoteMediaFormats.FindFormat(baseMediaFormat);
  if (remFmt == remoteMediaFormats.end()) {
    // Not in remote list, disable transmitter
    handler->SetTxMediaFormat(OpalMediaFormat());
    return false;
  }

  OpalMediaFormatList::const_iterator localFmt = localMediaFormats.FindFormat(baseMediaFormat);
  if (localFmt == localMediaFormats.end()) {
    // Not in our local list, disable transmitter
    handler->SetTxMediaFormat(OpalMediaFormat());
    return true;
  }

  // Merge remotes format into ours.
  // Note if this is our initial offer remote is the same as local.
  OpalMediaFormat adjustedFormat = *localFmt;
  adjustedFormat.Merge(*remFmt, true);

  handler->SetTxMediaFormat(adjustedFormat);

  if (localMedia != NULL) {
    // Set the receive handler to what we are sending to remote in our SDP
    handler->SetRxMediaFormat(adjustedFormat);
    SDPMediaFormat * fmt = localMedia->CreateSDPMediaFormat();
    if (fmt != NULL) {
      fmt->FromMediaFormat(adjustedFormat);
      localMedia->AddSDPMediaFormat(fmt);
    }
  }

  return true;
}


bool OpalSDPConnection::PauseOrCloseMediaStream(OpalMediaStreamPtr & stream, bool changed, bool paused)
{
  if (stream == NULL)
    return false;

  if (!stream->IsOpen()) {
    PTRACE(4, "Answer SDP, stream closed " << *stream);
    stream.SetNULL();
    return false;
  }

  if (!changed) {
    OpalMediaFormatList::const_iterator fmt = m_activeFormatList.FindFormat(stream->GetMediaFormat());
    if (fmt != m_activeFormatList.end() && stream->UpdateMediaFormat(*fmt, true)) {
      if (paused &&
          m_stringOptions.GetBoolean(OPAL_OPT_INACTIVE_AUDIO_FLOW) &&
          stream->IsSource() &&
          stream->GetMediaFormat().GetMediaType() == OpalMediaType::Audio())
      {
        PTRACE(4, "Answer SDP change pause ignored on stream " << *stream);
        return true;
      }
      if (stream->InternalSetPaused(paused, false, false)) {
        PTRACE(4, "Answer SDP change " << (paused ? "paused" : "resumed") << " stream " << *stream);
      }
      return !paused;
    }
    PTRACE(4, "Answer SDP (format change) needs to close stream " << *stream);
  }
  else {
    PTRACE(4, "Answer SDP (type change) needs to close stream " << *stream);
  }

  OpalMediaPatchPtr patch = stream->GetPatch();
  if (patch != NULL)
    patch->GetSource().Close();
  stream.SetNULL();
  return false;
}


bool OpalSDPConnection::OnSendOfferSDP(SDPSessionDescription & sdpOut, bool offerOpenMediaStreamsOnly)
{
  bool sdpOK = false;

  if (offerOpenMediaStreamsOnly && !m_mediaStreams.IsEmpty()) {
    PTRACE(3, "Offering only current media streams");
    m_activeFormatList = m_remoteFormatList; // Must have this by now
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it) {
      if (OnSendOfferSDPSession(it->first, sdpOut, true, it->second->IsGroupMember(OpalMediaSession::GetBundleGroupId())))
        sdpOK = true;
      else
        sdpOut.AddMediaDescription(it->second->CreateSDPMediaDescription());
    }
  }
  else {
    // If not got remote media format yet, we need to fake them,
    // so parts of the offering work correctly
    if (m_remoteFormatList.IsEmpty())
      SetRemoteMediaFormats(GetLocalMediaFormats());
    m_activeFormatList = m_remoteFormatList;

    PTRACE(3, "Offering all configured media:\n    " << setfill(',') << m_activeFormatList << setfill(' '));

    // Create media sessions based on available media types and make sure audio and video are first two sessions
    vector<bool> sessions = CreateAllMediaSessions();

#if OPAL_VIDEO
    SetUpLipSyncMediaStreams();
#endif

#if OPAL_BFCP
    // If a presentation video session was opened, then we need a BFCP session
    for (vector<bool>::size_type sessionId = 1; sessionId < sessions.size(); ++sessionId) {
      if (sessions[sessionId]) {
        OpalMediaSession * session = GetMediaSession(sessionId);
        if (session->GetMediaType() == OpalPresentationVideoMediaDefinition::Name()) {
          unsigned nextSessionId = m_sessions.GetSize() + 1;
          OpalMediaSession::Init init(*this, nextSessionId, BFCPMediaDefinition::Name(), false);
          m_sessions.SetAt(nextSessionId, new BFCPSession(init, this));
          if (sessions.size() < nextSessionId)
            sessions.resize(nextSessionId + 1);
          sessions[nextSessionId] = true;
          break;
        }
      }
    }
#endif // OPAL_BFCP

    OpalMediaTransportPtr bundledTransport;
    for (vector<bool>::size_type sessionId = 1; sessionId < sessions.size(); ++sessionId) {
      if (sessions[sessionId]) {
        OpalMediaSession * session = GetMediaSession(sessionId);

        bool bundled = m_stringOptions.GetBoolean(OPAL_OPT_AV_BUNDLE) || session->IsGroupMember(OpalMediaSession::GetBundleGroupId());
        if (bundled && bundledTransport != NULL)
          session->AttachTransport(bundledTransport);

        if (OnSendOfferSDPSession(sessionId, sdpOut, false, bundled)) {
          sdpOK = true;

          if (bundled && bundledTransport == NULL)
            bundledTransport = session->GetTransport();
        }
        else
          ReleaseMediaSession(sessionId);
      }
    }
  }

  m_activeFormatList = OpalMediaFormatList(); // Don't do RemoveAll() in case of references

  return sdpOK && !sdpOut.GetMediaDescriptions().IsEmpty();
}


bool OpalSDPConnection::OnSendOfferSDPSession(unsigned sessionId, SDPSessionDescription & sdp, bool offerOpenMediaStreamOnly, bool bundled)
{
  OpalMediaSession * mediaSession = GetMediaSession(sessionId);
  if (mediaSession == NULL) {
    PTRACE(1, "Could not create RTP session " << sessionId);
    return false;
  }

  OpalMediaType mediaType = mediaSession->GetMediaType();
  if (
#if OPAL_BFCP
      mediaType != BFCPMediaDefinition::Name() &&
#endif
      !m_localMediaFormats.HasType(mediaType))
  {
    PTRACE(2, "No formats for RTP session " << sessionId << " of type " << mediaType << " in " << setfill(',') << m_localMediaFormats);
    return false;
  }

  OpalRTPSession * rtpSession = dynamic_cast<OpalRTPSession *>(mediaSession);
  if (rtpSession != NULL && m_stringOptions.GetBoolean(OPAL_OPT_RTCP_MUX))
    rtpSession->SetSinglePortRx();

  // Set bundle before the mediaSession->Open(), should have at least the first one
  if (bundled) {
    unsigned rtpStreamIndex = sdp.GetMediaDescriptions().GetSize()+1;
    mediaSession->AddGroup(OpalMediaSession::GetBundleGroupId(), rtpStreamIndex, PString(rtpStreamIndex));
  }

  if (!mediaSession->Open(GetMediaInterface(), GetRemoteMediaAddress())) {
    PTRACE(1, "Could not open RTP session " << sessionId << " for media type " << mediaType);
    return false;
  }

  if (sdp.GetDefaultConnectAddress().IsEmpty())
    sdp.SetDefaultConnectAddress(mediaSession->GetLocalAddress());

  if (rtpSession != NULL && bundled && !m_stringOptions.GetBoolean(OPAL_OPT_MULTI_SSRC)) {
    RTP_SyncSourceArray ssrcs = rtpSession->GetSyncSources(OpalRTPSession::e_Sender);
    size_t count = 0;
    for (RTP_SyncSourceArray::iterator ssrc = ssrcs.begin(); ssrc != ssrcs.end(); ++ssrc) {
      if (!rtpSession->GetMediaStreamId(*ssrc, OpalRTPSession::e_Sender).IsEmpty() &&
           rtpSession->GetRtxSyncSource(*ssrc, OpalRTPSession::e_Sender, false) == 0)
        ++count;
    }
    PTRACE(4, "Bundled session has msid for " << count << " of " << ssrcs.size() << " SSRCs");
    if (count > 0) {
      bool gotOne = false;
      for (RTP_SyncSourceArray::iterator ssrc = ssrcs.begin(); ssrc != ssrcs.end(); ++ssrc) {
        if (!rtpSession->GetMediaStreamId(*ssrc, OpalRTPSession::e_Sender).IsEmpty() &&
             rtpSession->GetRtxSyncSource(*ssrc, OpalRTPSession::e_Sender, false) == 0) {
          SDPMediaDescription * localMedia = OnSendOfferSDPStream(mediaSession,
                                                                  offerOpenMediaStreamOnly,
                                                                  bundled,
                                                                  sdp.GetMediaDescriptions().GetSize()+1,
                                                                  *ssrc);
          if (localMedia != NULL) {
            /* Remember the offered direction for each RTP stream, as the test in OnSendOfferSDPStream
               woud apply to all streams in the session, which is not accurate. */
            size_t index = sdp.GetMediaDescriptions().size()+1;
            if (offerOpenMediaStreamOnly && index < m_bundledDirections.size())
              localMedia->SetDirection((SDPMediaDescription::Direction)(localMedia->GetDirection() & m_bundledDirections[index]));
            else {
              m_bundledDirections.resize(index+1);
              m_bundledDirections[index] = localMedia->GetDirection();
            }

            sdp.AddMediaDescription(localMedia);
            gotOne = true;
          }
        }
      }
      return gotOne;
    }
  }

  SDPMediaDescription * localMedia = OnSendOfferSDPStream(mediaSession,
                                                          offerOpenMediaStreamOnly,
                                                          bundled,
                                                          sdp.GetMediaDescriptions().GetSize()+1,
                                                          0);
  if (localMedia == NULL)
    return false;

  sdp.AddMediaDescription(localMedia);
  return true;
}


SDPMediaDescription * OpalSDPConnection::OnSendOfferSDPStream(OpalMediaSession * mediaSession,
                                                              bool offerOpenMediaStreamOnly,
                                                              bool bundled,
                                                              unsigned rtpStreamIndex,
                                                              RTP_SyncSourceId ssrc)
{
  OpalMediaType mediaType = mediaSession->GetMediaType();

  PAutoPtr<SDPMediaDescription> localMedia(mediaSession->CreateSDPMediaDescription());
  if (localMedia.get() == NULL) {
    PTRACE(2, "Can't create SDP media description for media type " << mediaType);
    return NULL;
  }

  PTRACE_CONTEXT_ID_TO(*localMedia);
  localMedia->SetStringOptions(m_stringOptions);
  localMedia->SetIndex(rtpStreamIndex);

  if (offerOpenMediaStreamOnly) {
    unsigned sessionId = mediaSession->GetSessionID();
    OpalMediaStreamPtr recvStream = GetMediaStream(sessionId, true);
    OpalMediaStreamPtr sendStream = GetMediaStream(sessionId, false);
    if (recvStream != NULL) {
      OpalMediaFormat rxFormat = recvStream->GetMediaFormat();
      OpalMediaFormatList::const_iterator it = m_localMediaFormats.FindFormat(rxFormat);
      if (it != m_localMediaFormats.end())
        localMedia->AddMediaFormat(*it);
      else {
        PTRACE(2, "Could not find media format " << rxFormat << " from stream " << *recvStream << " in local media formats.");
        localMedia->AddMediaFormat(rxFormat);
      }
    }
    else if (sendStream != NULL)
      localMedia->AddMediaFormat(sendStream->GetMediaFormat());
    else
      localMedia->AddMediaFormats(m_localMediaFormats, mediaType);

    if (sendStream != NULL) {
      // Add in the "rtx" capabilities corressponding to the selected codec
      RTP_DataFrame::PayloadTypes pt = sendStream->GetMediaFormat().GetPayloadType();
      OpalMediaFormatList::const_iterator it;
      while ((it = m_localMediaFormats.FindFormat(OpalRtx::GetName(mediaType), it)) != m_localMediaFormats.end()) {
        if (it->GetOptionPayloadType(OpalRtx::AssociatedPayloadTypeOption()) == pt) {
          localMedia->AddMediaFormat(*it);
          break;
        }
      }
    }

    bool sending = !m_holdFromRemote         && sendStream != NULL && sendStream->IsOpen();
    bool recving =  m_holdToRemote < eHoldOn && recvStream != NULL && recvStream->IsOpen();

    if (sending && recving)
      localMedia->SetDirection(SDPMediaDescription::SendRecv);
    else if (recving)
      localMedia->SetDirection(SDPMediaDescription::RecvOnly);
    else if (sending)
      localMedia->SetDirection(SDPMediaDescription::SendOnly);
    else
      localMedia->SetDirection(SDPMediaDescription::SendOnly);
  }
  else {
    switch (GetAutoStart(mediaType, ssrc).AsBits()) {
      case OpalMediaType::Transmit :
        localMedia->SetDirection(SDPCommonAttributes::SendOnly);
        break;
      case OpalMediaType::Receive :
        localMedia->SetDirection(SDPCommonAttributes::RecvOnly);
        break;
      case OpalMediaType::TransmitReceive :
        localMedia->SetDirection(SDPCommonAttributes::SendRecv);
        break;
      case OpalMediaType::OfferInactive :
        localMedia->SetDirection(SDPCommonAttributes::Inactive);
        break;
      default : // Don't offer
        return NULL;
    }
    localMedia->AddMediaFormats(m_localMediaFormats, mediaType);
  }

  if (!m_simulcastOffers[SDPMediaDescription::e_Send].empty() || !m_simulcastOffers[SDPMediaDescription::e_Recv].empty()) {
    SDPMediaDescription::Restrictions restrictions;
    SDPMediaDescription::Simulcast simulcast;
    for (SDPMediaDescription::Directions dir = SDPMediaDescription::BeginDirections; dir < SDPMediaDescription::EndDirections; ++dir) {
      for (SimulcastOffer::iterator it = m_simulcastOffers[dir].begin(); it != m_simulcastOffers[dir].end(); ++it) {
        PString rid = it->GetString(OPAL_OPT_SIMULCAST_RID);
        if (rid.empty())
          continue;
        SDPMediaDescription::Restriction & restriction = restrictions[rid];
        restriction.m_id = rid;
        restriction.m_direction = dir;
        restriction.m_options = *it;
        restriction.m_options.MakeUnique();
        PStringArray formats = restriction.m_options.GetString(OPAL_OPT_SIMULCAST_FORMATS).Tokenise(",");
        for (PINDEX i = 0; i < formats.GetSize(); ++i)
          restriction.m_mediaFormats += formats[i];
        SDPMediaDescription::SimulcastStream stream(restriction.m_id, restriction.m_options.GetBoolean(OPAL_OPT_SIMULCAST_PAUSED));
        restriction.m_options.Remove(OPAL_OPT_SIMULCAST_RID);
        restriction.m_options.Remove(OPAL_OPT_SIMULCAST_PAUSED);
        restriction.m_options.Remove(OPAL_OPT_SIMULCAST_FORMATS);
        simulcast[dir].push_back(SDPMediaDescription::SimulcastAlternative(1, rid));
      }
    }
    localMedia->SetRestrictions(restrictions);
    localMedia->SetSimulcast(simulcast);
  }

  if (mediaType == OpalMediaType::Audio()) {
    // Set format if we have an RTP payload type for RFC2833 and/or NSE
    // Must be after other codecs, as Mediatrix gateways barf if RFC2833 is first
    SetNxECapabilities(m_rfc2833Handler, m_localMediaFormats, m_activeFormatList, OpalRFC2833, localMedia.get());
#if OPAL_T38_CAPABILITY
    SetNxECapabilities(m_ciscoNSEHandler, m_localMediaFormats, m_activeFormatList, OpalCiscoNSE, localMedia.get());
#endif
  }

#if OPAL_SRTP
  if (GetMediaCryptoKeyExchangeModes()&OpalMediaCryptoSuite::e_SecureSignalling) {
    OpalMediaCryptoKeyList keys;
    OpalMediaCryptoKeyInfo * txKey = mediaSession->IsCryptoSecured(false);
    if (txKey != NULL)
      keys.Append(txKey->CloneAs<OpalMediaCryptoKeyInfo>());
    else
      keys = mediaSession->GetOfferedCryptoKeys();
    localMedia->SetCryptoKeys(keys);
  }
#endif

#if OPAL_RTP_FEC
  if (GetAutoStart(OpalFEC::MediaType()) != OpalMediaType::DontOffer) {
    OpalMediaFormat redundantMediaFormat;
    for (OpalMediaFormatList::iterator it = m_localMediaFormats.begin(); it != m_localMediaFormats.end(); ++it) {
      if (it->GetMediaType() == OpalFEC::MediaType() && it->GetOptionString(OpalFEC::MediaTypeOption()) == mediaType) {
        if (it->GetName().NumCompare(OPAL_REDUNDANT_PREFIX) == EqualTo)
          redundantMediaFormat = *it;
        else
          localMedia->AddMediaFormat(*it);
      }
    }

    if (redundantMediaFormat.IsValid()) {
      // Calculate the fmtp for red
      PStringStream fmtp;
      OpalMediaFormatList formats = localMedia->GetMediaFormats();
      for (OpalMediaFormatList::iterator it = formats.begin(); it != formats.end(); ++it) {
        if (it->IsTransportable() && *it != redundantMediaFormat) {
          if (!fmtp.IsEmpty())
            fmtp << '/';
          fmtp << (unsigned)it->GetPayloadType();
        }
      }
      redundantMediaFormat.SetOptionString("FMTP", fmtp);
      localMedia->AddMediaFormat(redundantMediaFormat);
    }
  }
#endif // OPAL_RTP_FEC

#if OPAL_VIDEO
  // Set video content to media description
  OpalVideoFormat::ContentRole role = OpalVideoFormat::eNoRole;
  if (mediaSession->GetMediaType() == OpalPresentationVideoMediaDefinition::Name())
    role = OpalVideoFormat::ePresentation;
  else if (mediaSession->GetMediaType() == OpalMediaType::Video()) {
    OpalMediaStreamPtr sendStream = GetMediaStream(mediaSession->GetSessionID(), false);
    if (sendStream != NULL)
      role = sendStream->GetMediaFormat().GetOptionEnum(OpalVideoFormat::ContentRoleOption(), OpalVideoFormat::eMainRole);
    else
      role = OpalVideoFormat::eMainRole;
  }

  if (role != OpalVideoFormat::eNoRole) {
    localMedia->SetContentRole(role);
    unsigned label = mediaSession->GetSessionID(); // Can be anything, really, but we try and make it easier.
    localMedia->SetLabel(label);
#if OPAL_BFCP
    BFCPSession * bfcpSession = dynamic_cast<BFCPSession *>(FindSessionByMediaType(BFCPMediaDefinition::Name()));
    if (bfcpSession != NULL)
      bfcpSession->SetFloorStreamMapping(0, label); // Zero floor indicates use next available
#endif
  }
#endif // OPAL_VIDEO

  if (bundled) {
    PString mid(rtpStreamIndex);
    mediaSession->AddGroup(OpalMediaSession::GetBundleGroupId(), rtpStreamIndex, mid);
    if (ssrc != 0)
      dynamic_cast<OpalRTPSession *>(mediaSession)->SetBundleMediaId(mid, ssrc, OpalRTPSession::e_Sender);
  }

  localMedia->FromSession(mediaSession, NULL, ssrc);
  return localMedia.release();
}


bool OpalSDPConnection::OnSendAnswerSDP(const SDPSessionDescription & sdpOffer, SDPSessionDescription & sdpOut, bool transfer)
{
  if (!OnReceivedSDP(sdpOffer))
    return false;

  /* Each m= line is an RTP stream, if not bundled there is a 1 to 1 correspondence
     with RTP sessions, if bundled then there is an RTP session only for each media
     type, audio/video etc. */
  size_t rtpStreamCount = sdpOffer.GetMediaDescriptions().GetSize();
  vector<SDPMediaDescription *> sdpMediaDescriptions(rtpStreamCount+1);
  size_t rtpStreamIndex;

  BundleMergeInfo bundleMergeInfo(rtpStreamCount);
  m_bundledDirections.resize(rtpStreamCount+1);

  PTRACE(4, "OnSendAnswerSDP for " << rtpStreamCount << " offered streams");

  /* When using BUNDLE, sessionId is not 1 to 1 with media description (RTP stream)
      any more, so need to try and match it up by media type. Anything not
      bundled is allocated next sessionId. Note, if nothing is bundled then
      this effectively reverts to the 1 to 1 relationship between RTP stream
      index and session ID. */
  unsigned allocateSessionId = 1;
  for (rtpStreamIndex = 1; rtpStreamIndex <= rtpStreamCount; ++rtpStreamIndex) {
    SDPMediaDescription * incomingMedia = sdpOffer.GetMediaDescriptionByIndex(rtpStreamIndex);
    if (PAssertNULL(incomingMedia) == NULL)
      return false;

    size_t bundleIndex = 0;
    if (incomingMedia->IsGroupMember(OpalMediaSession::GetBundleGroupId())) {
      for (bundleIndex = rtpStreamIndex-1; bundleIndex > 0; --bundleIndex) {
        SDPMediaDescription * previousMedia = sdpOffer.GetMediaDescriptionByIndex(bundleIndex);
        if (previousMedia->GetMediaType() == incomingMedia->GetMediaType() &&
            previousMedia->IsGroupMember(OpalMediaSession::GetBundleGroupId())) {
          bundleMergeInfo.m_sessionId[rtpStreamIndex] = bundleMergeInfo.m_sessionId[bundleIndex];
          break;
        }
      }
    }

    if (bundleIndex == 0)
      bundleMergeInfo.m_sessionId[rtpStreamIndex] = allocateSessionId++;
    m_bundledDirections[rtpStreamIndex] = incomingMedia->GetDirection();
  }

#if OPAL_SRTP
  PStringArray cryptoSuites = GetMediaCryptoSuites();
  bool hasClearText = cryptoSuites.GetValuesIndex(OpalMediaCryptoSuite::ClearText()) != P_MAX_INDEX;
  if (cryptoSuites.GetSize() > (hasClearText ? 1 : 0)) {
    for (rtpStreamIndex = 1; rtpStreamIndex <= rtpStreamCount; ++rtpStreamIndex) {
      SDPMediaDescription * incomingMedia = sdpOffer.GetMediaDescriptionByIndex(rtpStreamIndex);
      if (incomingMedia->IsSecure())
        sdpMediaDescriptions[rtpStreamIndex] = OnSendAnswerSDPStream(incomingMedia, rtpStreamIndex, transfer, bundleMergeInfo);
    }
  }
  if (hasClearText) {
    for (rtpStreamIndex = 1; rtpStreamIndex <= rtpStreamCount; ++rtpStreamIndex) {
      SDPMediaDescription * incomingMedia = sdpOffer.GetMediaDescriptionByIndex(rtpStreamIndex);
      if (!incomingMedia->IsSecure())
        sdpMediaDescriptions[rtpStreamIndex] = OnSendAnswerSDPStream(incomingMedia, rtpStreamIndex, transfer, bundleMergeInfo);
    }
  }
#else
  for (rtpStreamIndex = 1; rtpStreamIndex <= rtpStreamCount; ++rtpStreamIndex)
    sdpMediaDescriptions[rtpStreamIndex] = OnSendAnswerSDPStream(sdpOffer.GetMediaDescriptionByIndex(rtpStreamIndex),
                                                                 rtpStreamIndex, transfer, bundleMergeInfo);
#endif // OPAL_SRTP

  bundleMergeInfo.RemoveSessionSSRCs(m_sessions);

#if OPAL_VIDEO
  SetUpLipSyncMediaStreams();
#endif // OPAL_VIDEO

  // Fill in refusal for media sessions we didn't like
  std::set<unsigned> openedSessions;
  for (rtpStreamIndex = 1; rtpStreamIndex <= rtpStreamCount; ++rtpStreamIndex) {
    SDPMediaDescription * incomingMedia = sdpOffer.GetMediaDescriptionByIndex(rtpStreamIndex);
    if (!PAssert(incomingMedia != NULL, PLogicError))
      return false;

    SDPMediaDescription * mediaDescription = sdpMediaDescriptions[rtpStreamIndex];
    unsigned sessionId = bundleMergeInfo.m_sessionId[rtpStreamIndex];
    OpalMediaSession * mediaSession = GetMediaSession(sessionId);
    if (mediaDescription != NULL && mediaSession != NULL) {
      openedSessions.insert(sessionId);
      mediaDescription->FromSession(mediaSession, incomingMedia, bundleMergeInfo.m_sendSsrcs[rtpStreamIndex]);
    }
    else {
      if (mediaSession == NULL) {
        OpalMediaSession::Init init(*this, sessionId, incomingMedia->GetMediaType(), m_remoteBehindNAT);
        PStringArray tokens(4);
        tokens[0] = incomingMedia->GetSDPMediaType();
        tokens[1] = '0';
        tokens[2] = incomingMedia->GetSDPTransportType();
        tokens[3] = incomingMedia->GetSDPPortList();
        mediaSession = new OpalDummySession(init, tokens);
        m_sessions.SetAt(sessionId, mediaSession);
      }

      if (mediaDescription == NULL) {
        mediaDescription = mediaSession->CreateSDPMediaDescription();
        mediaDescription->SetIndex(rtpStreamIndex);
      }
      mediaDescription->FromSession(mediaSession, incomingMedia, 0);
    }

    sdpOut.AddMediaDescription(mediaDescription);
  }

  if (openedSessions.empty()) {
    PTRACE(3, "Could not match any SDP media descriptions on " << *this);
    return false;
  }

  m_activeFormatList = OpalMediaFormatList(); // Don't do RemoveAll() in case of references

  /* Shut down any media that is in a session not mentioned in answer.
      While the SIP/SDP specification says this shouldn't happen, it does
      anyway so we need to deal. */
  for (StreamDict::iterator it = m_mediaStreams.begin(); it != m_mediaStreams.end(); ++it) {
    OpalMediaStreamPtr stream = it->second;
    if (stream != NULL && openedSessions.find(stream->GetSessionID()) == openedSessions.end())
      stream->Close();
  }

  bool holdFromRemote = sdpOffer.IsHold(AllowMusicOnHold());
  if (m_holdFromRemote != holdFromRemote) {
    PTRACE(3, "Remote " << (holdFromRemote ? "" : "retrieve from ") << "hold detected");
    m_holdFromRemote = holdFromRemote;
    OnHold(true, holdFromRemote);
  }

  StartMediaStreams();

  return true;
}


bool OpalSDPConnection::AllowMusicOnHold() const
{
  return m_stringOptions.GetBoolean(OPAL_OPT_ALLOW_MUSIC_ON_HOLD, true);
}


SDPMediaDescription * OpalSDPConnection::OnSendAnswerSDPStream(SDPMediaDescription * incomingMedia,
                                                               unsigned rtpStreamIndex,
                                                               bool transfer,
                                                               BundleMergeInfo & bundleMergeInfo)
{
  OpalMediaType mediaType = incomingMedia->GetMediaType();

  // See if any media formats of this session id, so don't create unused RTP session
  if (!m_activeFormatList.HasType(mediaType)) {
    PTRACE(3, "No media formats of type " << mediaType << ", not adding SDP");
    return NULL;
  }

  if (!PAssert(mediaType.GetDefinition() != NULL, PString("Unusable media type \"") + mediaType + '"'))
    return NULL;

  unsigned sessionId = bundleMergeInfo.m_sessionId[rtpStreamIndex];

#if OPAL_SRTP
  OpalMediaCryptoKeyList keys = incomingMedia->GetCryptoKeys();
  if (!keys.IsEmpty() && !(GetMediaCryptoKeyExchangeModes()&OpalMediaCryptoSuite::e_SecureSignalling)) {
    PTRACE(2, "No secure signaling, cannot use SDES crypto for " << mediaType << " RTP stream " << rtpStreamIndex);
    keys.RemoveAll();
    incomingMedia->SetCryptoKeys(keys);
  }

  // If not a match already, or if we already have another, secure version, of the media session
  if (GetMediaSession(sessionId) == NULL) {
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it) {
      if (it->second->GetSessionID() != sessionId &&
          it->second->GetMediaType() == mediaType &&
          (
            it->second->GetSessionType() == OpalSRTPSession::RTP_SAVP() ||
            it->second->GetSessionType() == OpalDTLSSRTPSession::RTP_DTLS_SAVPF()
          ) &&
          it->second->IsOpen())
      {
        PTRACE(3, "Not creating " << mediaType << " media session, already secure.");
        return NULL;
      }
    }
  }
#endif // OPAL_SRTP

  // Create new media session
  OpalTransportAddress localAddress;
  OpalMediaSession * mediaSession = SetUpMediaSession(sessionId, mediaType, *incomingMedia, localAddress, bundleMergeInfo);
  if (mediaSession == NULL)
    return NULL;

  bool replaceSession = false;

  // For fax for example, we have to switch the media session according to mediaType
  if (mediaSession->GetMediaType() != mediaType) {
    PTRACE(3, "Replacing " << mediaSession->GetMediaType() << " session " << sessionId << " with " << mediaType);
#if OPAL_T38_CAPABILITY
    if (mediaType == OpalMediaType::Fax()) {
      if (!OnSwitchingFaxMediaStreams(true)) {
        PTRACE(2, "Switch to T.38 refused for " << *this);
        return NULL;
      }
    }
    else if (mediaSession->GetMediaType() == OpalMediaType::Fax()) {
      if (!OnSwitchingFaxMediaStreams(false)) {
        PTRACE(2, "Switch from T.38 refused for " << *this);
        return NULL;
      }
    }
#endif // OPAL_T38_CAPABILITY

    mediaSession = CreateMediaSession(sessionId, mediaType, incomingMedia->GetSessionType());
    if (mediaSession == NULL) {
      PTRACE(2, "Could not create session for " << mediaType);
      return NULL;
    }

    // Set flag to force media stream close
    replaceSession = true;
  }

  // construct a new media session list 
  PAutoPtr<SDPMediaDescription> localMedia(mediaSession->CreateSDPMediaDescription());
  if (localMedia.get() == NULL) {
    if (replaceSession)
      delete mediaSession; // Still born so can delete, not used anywhere
    PTRACE(1, "Could not create SDP media description for media type " << mediaType);
    return NULL;
  }
  PTRACE_CONTEXT_ID_TO(*localMedia);
  localMedia->SetIndex(rtpStreamIndex);

  /* Make sure SDP transport type in reply is same as in offer. This is primarily
     a workaround for broken implementations, esecially with respect to feedback
     (AVPF) and DTLS (UDP/TLS/SAFP) */
  localMedia->SetSDPTransportType(incomingMedia->GetSDPTransportType());

  // Get SDP string options through
  localMedia->SetStringOptions(m_stringOptions);

#if OPAL_SRTP
  if (!keys.IsEmpty()) {// SDES
    OpalMediaCryptoKeyInfo * rxKey = mediaSession->IsCryptoSecured(true);
    OpalMediaCryptoKeyInfo * txKey = mediaSession->IsCryptoSecured(false);
    if (txKey != NULL && rxKey != NULL && keys.GetValuesIndex(*rxKey) != P_MAX_INDEX) {
      keys.RemoveAll();
      keys.Append(txKey->CloneAs<OpalMediaCryptoKeyInfo>());
    }
    else {
      // Set rx key from the other side SDP, which it's tx key
      if (!mediaSession->ApplyCryptoKey(keys, true)) {
        PTRACE(2, "Incompatible crypto suite(s) for " << mediaType << " session " << sessionId << ", RTP stream " << rtpStreamIndex);
        return NULL;
      }

      // Use symmetric keys, generate a cloneof the remotes tx key for out yx key
      txKey = keys.front().CloneAs<OpalMediaCryptoKeyInfo>();
      if (PAssertNULL(txKey) == NULL)
        return NULL;

      // But with a different value
      txKey->Randomise();

      keys.RemoveAll();
      keys.Append(txKey);
      if (!mediaSession->ApplyCryptoKey(keys, false)) {
        PTRACE(2, "Unexpected error with crypto suite(s) for " << mediaType << " session " << sessionId << ", RTP stream " << rtpStreamIndex);
        return NULL;
      }
    }
    localMedia->SetCryptoKeys(keys);
  }
#endif // OPAL_SRTP

  SDPMediaDescription::Direction otherSidesDir = incomingMedia->GetDirection();
  if (GetPhase() < ConnectedPhase) {
    // If processing initial offer and video, obey the auto-start flags
    OpalMediaType::AutoStartMode autoStart = GetAutoStart(mediaType);
    if ((autoStart&OpalMediaType::Transmit) == 0)
      otherSidesDir = (otherSidesDir&SDPMediaDescription::SendOnly) != 0 ? SDPMediaDescription::SendOnly : SDPMediaDescription::Inactive;
    if ((autoStart&OpalMediaType::Receive) == 0)
      otherSidesDir = (otherSidesDir&SDPMediaDescription::RecvOnly) != 0 ? SDPMediaDescription::RecvOnly : SDPMediaDescription::Inactive;
    PTRACE(4, "Answering initial offer for media type " << mediaType << ","
              " index=" << rtpStreamIndex << ","
              " directions=" << otherSidesDir << ","
              " autoStart=" << autoStart);
  }
  else {
    PTRACE(4, "Answering offer for media type " << mediaType << ","
              " index=" << rtpStreamIndex << ","
              " directions=" << otherSidesDir);
  }

  SDPMediaDescription::Direction newDirection = SDPMediaDescription::Inactive;

  // Check if we had a stream and the remote has either changed the codec or
  // changed the direction of the stream
  OpalMediaStreamPtr sendStream = GetMediaStream(sessionId, false);
  bool sendDisabled = bundleMergeInfo.m_allowPauseSendMediaStream[sessionId] && (otherSidesDir&SDPMediaDescription::RecvOnly) == 0;
  bundleMergeInfo.m_allowPauseSendMediaStream[sessionId] = sendDisabled;
  if (PauseOrCloseMediaStream(sendStream, replaceSession, sendDisabled) && (otherSidesDir&SDPMediaDescription::RecvOnly) != 0)
    newDirection = SDPMediaDescription::SendOnly;

  OpalMediaStreamPtr recvStream = GetMediaStream(sessionId, true);
  bool recvDisabled = bundleMergeInfo.m_allowPauseRecvMediaStream[sessionId] && (otherSidesDir&SDPMediaDescription::SendOnly) == 0;
  bundleMergeInfo.m_allowPauseRecvMediaStream[sessionId] = recvDisabled;
  if (PauseOrCloseMediaStream(recvStream, replaceSession, m_holdToRemote >= eHoldOn || recvDisabled) && (otherSidesDir&SDPMediaDescription::SendOnly) != 0)
    newDirection = newDirection != SDPMediaDescription::Inactive ? SDPMediaDescription::SendRecv : SDPMediaDescription::RecvOnly;

  // See if we need to do a session switcharoo, but must be after stream closing
  if (replaceSession)
    ReplaceMediaSession(sessionId, mediaSession);

  /* After (possibly) closing streams, we now open them again if necessary,
     OpenSourceMediaStreams will just return true if they are already open.
     We open tx (other party source) side first so we follow the remote
     endpoints preferences. */
  if (!incomingMedia->GetMediaAddress().IsEmpty()) {
    PSafePtr<OpalConnection> otherParty = GetOtherPartyConnection();
    if (otherParty != NULL && sendStream == NULL) {
      if ((sendStream = GetMediaStream(sessionId, false)) == NULL) {
        PTRACE(5, "Opening tx " << mediaType << " stream from offer SDP");
        if (m_ownerCall.OpenSourceMediaStreams(*otherParty,
                                             mediaType,
                                             sessionId,
                                             OpalMediaFormat(),
#if OPAL_VIDEO
                                             incomingMedia->GetContentRole(),
#endif
                                             transfer,
                                             (otherSidesDir&SDPMediaDescription::RecvOnly) == 0))
          sendStream = GetMediaStream(sessionId, false);
      }

      if ((otherSidesDir&SDPMediaDescription::RecvOnly) != 0) {
        if (sendStream == NULL) {
          PTRACE(4, "Did not open required tx " << mediaType << " stream.");
          return NULL;
        }
        newDirection = newDirection != SDPMediaDescription::Inactive ? SDPMediaDescription::SendRecv
                                                                     : SDPMediaDescription::SendOnly;
      }
    }

    if (sendStream != NULL) {
      if ((newDirection&SDPMediaDescription::SendOnly) != 0) {
        PTRACE(4, "Updating send stream " << *sendStream);

        // In case is new offer and remote has tweaked the streams paramters, we need to merge them
        sendStream->UpdateMediaFormat(*m_activeFormatList.FindFormat(sendStream->GetMediaFormat()), true);
      }

      // Deal with more than one stream per session
      OpalRTPSession * rtpSession = dynamic_cast<OpalRTPSession *>(mediaSession);
      if (rtpSession != NULL) {
        PString mid;
        RTP_SyncSourceId ssrc;
        if ((newDirection&SDPMediaDescription::SendOnly) == 0)
          ssrc = rtpSession->GetControlSyncSource();
        else if ((mid = rtpSession->GetGroupMediaId(OpalMediaSession::GetBundleGroupId(), rtpStreamIndex)).empty()) {
          if (sessionId != rtpStreamIndex)
            ssrc = rtpSession->AddSyncSource(0, OpalRTPSession::e_Sender);
          else
            ssrc = rtpSession->GetSyncSourceOut();
        }
        else if ((ssrc = rtpSession->FindBundleMediaId(mid, OpalRTPSession::e_Sender)) != 0)
          PTRACE(4, "Found existing SSRC " << RTP_TRACE_SRC(ssrc) << " on index " << rtpStreamIndex << " using BUNDLE mid \"" << mid << '"');
        else {
          ssrc = rtpSession->GetSyncSourceOut();
          if (!rtpSession->GetBundleMediaId(ssrc, OpalRTPSession::e_Sender).empty())
            ssrc = rtpSession->AddSyncSource(0, OpalRTPSession::e_Sender);
          rtpSession->SetBundleMediaId(mid, ssrc, OpalRTPSession::e_Sender);
        }
        PTRACE(4, "Including BUNDLE " << rtpStreamIndex << ", mid=\"" << mid << "\", send SSRC " << RTP_TRACE_SRC(ssrc));
        bundleMergeInfo.m_sendSsrcs[rtpStreamIndex] = ssrc;
      }
    }

    if (recvStream == NULL) {
      if ((recvStream = GetMediaStream(sessionId, true)) == NULL) {
        PTRACE(5, "Opening rx " << mediaType << " stream from offer SDP");
        if (m_ownerCall.OpenSourceMediaStreams(*this,
                                             mediaType,
                                             sessionId,
                                             OpalMediaFormat(),
#if OPAL_VIDEO
                                             incomingMedia->GetContentRole(),
#endif
                                             transfer,
                                             (otherSidesDir&SDPMediaDescription::SendOnly) == 0))
          recvStream = GetMediaStream(sessionId, true);
      }

      if ((otherSidesDir&SDPMediaDescription::SendOnly) != 0) {
        if (recvStream == NULL) {
          PTRACE(4, "Did not open required rx " << mediaType << " stream.");
          return NULL;
        }
        newDirection = newDirection != SDPMediaDescription::Inactive ? SDPMediaDescription::SendRecv
                                                                     : SDPMediaDescription::RecvOnly;
      }
    }

    if ((newDirection&SDPMediaDescription::RecvOnly) != 0 && recvStream != NULL) {
      PTRACE(4, "Updating recv stream " << *recvStream);

      OpalMediaFormat adjustedMediaFormat = *m_activeFormatList.FindFormat(recvStream->GetMediaFormat());

      // If we are sendrecv we will receive the same payload type as we transmit.
      if (newDirection == SDPMediaDescription::SendRecv)
        adjustedMediaFormat.SetPayloadType(sendStream->GetMediaFormat().GetPayloadType());

      recvStream->UpdateMediaFormat(adjustedMediaFormat, true);
    }
  }

  // Now we build the reply, setting "direction" as appropriate for what we opened.
  localMedia->SetDirection(newDirection);
  if (sendStream != NULL)
    localMedia->AddMediaFormat(sendStream->GetMediaFormat());
  else if (recvStream != NULL)
    localMedia->AddMediaFormat(recvStream->GetMediaFormat());
  else {
    // Add all possible formats
    bool empty = true;
    for (OpalMediaFormatList::iterator remoteFormat = m_remoteFormatList.begin(); remoteFormat != m_remoteFormatList.end(); ++remoteFormat) {
      if (remoteFormat->GetMediaType() == mediaType) {
        for (OpalMediaFormatList::iterator localFormat = m_localMediaFormats.begin(); localFormat != m_localMediaFormats.end(); ++localFormat) {
          if (localFormat->GetMediaType() == mediaType) {
            OpalMediaFormat intermediateFormat;
            if (OpalTranscoder::FindIntermediateFormat(*localFormat, *remoteFormat, intermediateFormat)) {
              localMedia->AddMediaFormat(*remoteFormat);
              empty = false;
              break;
            }
          }
        }
      }
    }

    // RFC3264 says we MUST have an entry, but it should have port zero
    if (empty) {
      localMedia->AddMediaFormat(m_activeFormatList.front());
      localMedia->FromSession(NULL, NULL, 0);
    }
    else {
      // We can do the media type but choose not to at this time
      localMedia->SetDirection(SDPMediaDescription::Inactive);
    }
  }

  // Handle restrictions draft-ietf-mmusic-rid
  if (m_stringOptions.GetBoolean(OPAL_OPT_ENABLE_RID)) {
    SDPMediaDescription::Restrictions restrictions = incomingMedia->GetRestrictions();
    if (!restrictions.empty()) {
      for (SDPMediaDescription::Restrictions::iterator it = restrictions.begin(); it != restrictions.end(); ) {
        if (OnReceivedOfferRestriction(*incomingMedia, *localMedia, it->second))
          ++it;
        else
          restrictions.erase(it++);
      }
      PTRACE(4, restrictions.size() << " restrictions (rid) answered.");
      localMedia->SetRestrictions(restrictions);
    }
  }

  // Handle draft-ietf-mmusic-sdp-simulcast
  if (m_stringOptions.GetBoolean(OPAL_OPT_SIMULCAST))
    OnReceivedOfferSimulcast(*incomingMedia, *localMedia);

  FinaliseRtx(sendStream, localMedia.get());
  FinaliseRtx(recvStream, localMedia.get());

  if (mediaType == OpalMediaType::Audio()) {
    // Set format if we have an RTP payload type for RFC2833 and/or NSE
    SetNxECapabilities(m_rfc2833Handler, m_localMediaFormats, m_activeFormatList, OpalRFC2833, localMedia.get());
#if OPAL_T38_CAPABILITY
    SetNxECapabilities(m_ciscoNSEHandler, m_localMediaFormats, m_activeFormatList, OpalCiscoNSE, localMedia.get());
#endif
  }

#if OPAL_T38_CAPABILITY
  m_ownerCall.ResetSwitchingT38();
#endif

#if OPAL_RTP_FEC
  OpalMediaFormatList fec = NegotiateFECMediaFormats(*mediaSession);
  for (OpalMediaFormatList::iterator it = fec.begin(); it != fec.end(); ++it)
    localMedia->AddMediaFormat(*it);
#endif // OPAL_RTP_FEC

  PTRACE(4, "Answered offer for media type " << mediaType << ","
            " index=" << rtpStreamIndex << ","
            " address=" << localMedia->GetMediaAddress());

#if OPAL_VIDEO
  // Set video label to media description
  if (mediaSession->GetMediaType() == OpalMediaType::Video() &&
            GetMediaStream(mediaSession->GetSessionID(), true) != NULL &&
            GetMediaStream(mediaSession->GetSessionID(), false) != NULL)
      localMedia->SetLabel(mediaSession->GetSessionID());
#endif // OPAL_VIDEO

  return localMedia.release();
}


bool OpalSDPConnection::OnReceivedOfferRestriction(const SDPMediaDescription & /*offer*/,
                                                   SDPMediaDescription & answer,
                                                   SDPMediaDescription::Restriction & restriction)
{
  PTRACE(4, "Answering offer for rid: \"" << restriction.m_id << '"');

  // Default is to remove "pt" option and have it re3calculated based on restriction.m_mediaFormats
  restriction.m_options.Remove(SDPMediaDescription::RestrictionPayloadTypeKey());
  return restriction.AnswerOffer(answer.GetMediaFormats());
}


void OpalSDPConnection::OnReceivedOfferSimulcast(const SDPMediaDescription & offer, SDPMediaDescription & answer)
{
  SDPMediaDescription::Simulcast simulcast = offer.GetSimulcast();
  if (simulcast.IsValid()) {
    // Accept it as is
    PTRACE(4, "Answering offer for simulcast.");
    std::swap(simulcast[SDPMediaDescription::e_Send], simulcast[SDPMediaDescription::e_Recv]);
    answer.SetSimulcast(simulcast);
  }
}


void OpalSDPConnection::OnReceivedAnswerSimulcast(const SDPMediaDescription & answer, OpalRTPSession & session)
{
  SDPMediaDescription::SimulcastStreams simulcast = answer.GetSimulcast()[SDPMediaDescription::e_Recv];
  if (simulcast.empty() || simulcast.front().empty())
    return;

  PTRACE(4, "Received simulcast answer.");
  for (SDPMediaDescription::SimulcastStreams::const_iterator it = simulcast.begin(); it != simulcast.end(); ++it) {
    if (!it->empty())
      session.SetRtpStreamId(it->front().m_rid, session.AddSyncSource(0, OpalRTPSession::e_Sender), OpalRTPSession::e_Sender);
  }
}


bool OpalSDPConnection::OnReceivedAnswerSDP(const SDPSessionDescription & sdp, bool & multipleFormats)
{
  if (!OnReceivedSDP(sdp))
    return false;

  unsigned mediaDescriptionCount = sdp.GetMediaDescriptions().GetSize();

  std::set<unsigned> openedSessions;
  BundleMergeInfo bundleMergeInfo(mediaDescriptionCount);
  for (unsigned index = 1; index <= mediaDescriptionCount; ++index) {
    SDPMediaDescription * mediaDescription = sdp.GetMediaDescriptionByIndex(index);
    if (PAssertNULL(mediaDescription) == NULL)
      return false;

    if (mediaDescription->IsGroupMember(OpalMediaSession::GetBundleGroupId())) {
      /* When using BUNDLE, sessionId is not 1 to 1 with media description (RTP stream)
         any more, so need to try and match it up by SDP "mid" attribute. */
      PString mid = mediaDescription->GetGroupMediaId(OpalMediaSession::GetBundleGroupId());
      bool missing = true;
      for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        unsigned sessionIndex = it->second->FindGroupMediaId(OpalMediaSession::GetBundleGroupId(), mid);
        if (sessionIndex != UINT_MAX) {
          PTRACE_IF(3, sessionIndex != index, "BUNDLE mid \"" << mid << "\" index mismatch: session=" << sessionIndex << ", SDP=" << index);
          if (OnReceivedAnswerSDPStream(mediaDescription, it->first, multipleFormats, bundleMergeInfo))
            openedSessions.insert(it->first);
          missing = false;
        }
      }
      if (missing) {
        PTRACE(3, "Could not match mid=\"" << mediaDescription->GetGroupMediaId(OpalMediaSession::GetBundleGroupId()) << "\""
               " to any session in " << OpalMediaSession::GetBundleGroupId());
        return false;
      }
    }
    else {
      if (OnReceivedAnswerSDPStream(mediaDescription, index, multipleFormats, bundleMergeInfo))
        openedSessions.insert(index);
    }
  }

  bundleMergeInfo.RemoveSessionSSRCs(m_sessions);

  m_activeFormatList = OpalMediaFormatList(); // Don't do RemoveAll() in case of references

  /* Shut down any media that is in a session not mentioned in answer to our offer.
     While the SIP/SDP specification says this shouldn't happen, it does
     anyway so we need to deal. */
  for (StreamDict::iterator it = m_mediaStreams.begin(); it != m_mediaStreams.end(); ++it) {
    OpalMediaStreamPtr stream = it->second;
    if (stream != NULL && openedSessions.find(stream->GetSessionID()) == openedSessions.end())
      stream->Close();
  }

  if (openedSessions.empty())
    return false;

  StartMediaStreams();
  return true;
}


bool OpalSDPConnection::OnReceivedAnswerSDPStream(const SDPMediaDescription * mediaDescription,
                                                  unsigned sessionId,
                                                  bool & multipleFormats,
                                                  BundleMergeInfo & bundleMergeInfo)
{
  if (!PAssert(mediaDescription != NULL, "SDP Media description list changed"))
    return false;

  OpalMediaType mediaType = mediaDescription->GetMediaType();
  
  PTRACE(4, "Processing received SDP media description for " << mediaType);

  /* Get the media the remote has answered to our offer. Remove the media
     formats we do not support, in case the remote is insane and replied
     with something we did not actually offer. */
  if (!m_activeFormatList.HasType(mediaType)) {
    PTRACE(2, "Could not find supported media formats in SDP media description for session " << sessionId);
    return false;
  }

  // Set up the media session, e.g. RTP
  OpalTransportAddress localAddress;
  OpalMediaSession * mediaSession = SetUpMediaSession(sessionId, mediaType, *mediaDescription, localAddress, bundleMergeInfo);
  if (mediaSession == NULL)
    return false;

#if OPAL_SRTP
  OpalMediaCryptoKeyList keys = mediaDescription->GetCryptoKeys();
  if (!keys.IsEmpty()) {
    // Set our rx keys to remotes tx keys indicated in SDP
    if (!mediaSession->ApplyCryptoKey(keys, true)) {
      PTRACE(2, "Incompatible crypto suite(s) for " << mediaType << " session " << sessionId);
      return false;
    }

    if (!mediaSession->IsCryptoSecured(false)) {
      // Now match up the tag number on our offered keys
      OpalMediaCryptoKeyList & offeredKeys = mediaSession->GetOfferedCryptoKeys();
      OpalMediaCryptoKeyList::iterator it;
      for (it = offeredKeys.begin(); it != offeredKeys.end(); ++it) {
        if (it->GetTag() == keys.front().GetTag())
          break;
      }
      if (it == offeredKeys.end()) {
        PTRACE(2, "Remote selected crypto suite(s) we did not offer for " << mediaType << " session " << sessionId);
        return false;
      }

      keys.RemoveAll();
      keys.Append(&*it);

      offeredKeys.DisallowDeleteObjects(); // Can't have in two lists and both dispose of pointer
      offeredKeys.erase(it);
      offeredKeys.AllowDeleteObjects();
      offeredKeys.RemoveAll();

      if (!mediaSession->ApplyCryptoKey(keys, false)) {
        PTRACE(2, "Incompatible crypto suite(s) for " << mediaType << " session " << sessionId);
        return false;
      }
    }
  }
#endif // OPAL_SRTP

  SDPMediaDescription::Direction otherSidesDir = mediaDescription->GetDirection();

  // Check if we had a stream and the remote has either changed the codec or
  // changed the direction of the stream
  OpalMediaStreamPtr sendStream = GetMediaStream(sessionId, false);
  bool sendDisabled = bundleMergeInfo.m_allowPauseSendMediaStream[sessionId] && (otherSidesDir&SDPMediaDescription::RecvOnly) == 0;
  bundleMergeInfo.m_allowPauseSendMediaStream[sessionId] = sendDisabled;
  PauseOrCloseMediaStream(sendStream, false, sendDisabled);

  OpalMediaStreamPtr recvStream = GetMediaStream(sessionId, true);
  bool recvDisabled = bundleMergeInfo.m_allowPauseRecvMediaStream[sessionId] && (otherSidesDir&SDPMediaDescription::SendOnly) == 0;
  bundleMergeInfo.m_allowPauseRecvMediaStream[sessionId] = recvDisabled;
  PauseOrCloseMediaStream(recvStream, false, recvDisabled);

  /* After (possibly) closing streams, we now open them again if necessary,
     OpenSourceMediaStreams will just return true if they are already open.
     We open tx (other party source) side first so we follow the remote
     endpoints preferences. */
  if (sendStream == NULL) {
    PSafePtr<OpalConnection> otherParty = GetOtherPartyConnection();
    if (otherParty == NULL)
      return false;

    PTRACE(5, "Opening tx " << mediaType << " stream from answer SDP");
    if (m_ownerCall.OpenSourceMediaStreams(*otherParty,
                                          mediaType,
                                          sessionId,
                                          OpalMediaFormat(),
#if OPAL_VIDEO
                                          mediaDescription->GetContentRole(),
#endif
                                          false,
                                          sendDisabled))
      sendStream = GetMediaStream(sessionId, false);
    if (!sendDisabled && sendStream == NULL && !otherParty->IsOnHold(true))
      OnMediaStreamOpenFailed(false);
  }

  if (recvStream == NULL) {
    PTRACE(5, "Opening rx " << mediaType << " stream from answer SDP");
    if (m_ownerCall.OpenSourceMediaStreams(*this,
                                         mediaType,
                                         sessionId,
                                         OpalMediaFormat(),
#if OPAL_VIDEO
                                         mediaDescription->GetContentRole(),
#endif
                                         false,
                                         recvDisabled))
      recvStream = GetMediaStream(sessionId, true);
    if (!recvDisabled && recvStream == NULL)
      OnMediaStreamOpenFailed(true);
  }

  if (m_stringOptions.GetBoolean(OPAL_OPT_SIMULCAST)) {
    OpalRTPSession * rtpSession = dynamic_cast<OpalRTPSession *>(mediaSession);
    if (rtpSession != NULL)
      OnReceivedAnswerSimulcast(*mediaDescription, *rtpSession);
  }

  FinaliseRtx(sendStream, NULL);
  FinaliseRtx(recvStream, NULL);

  PINDEX maxFormats = 1;
  if (mediaType == OpalMediaType::Audio()) {
    if (SetNxECapabilities(m_rfc2833Handler, m_localMediaFormats, m_activeFormatList, OpalRFC2833))
      ++maxFormats;
#if OPAL_T38_CAPABILITY
    if (SetNxECapabilities(m_ciscoNSEHandler, m_localMediaFormats, m_activeFormatList, OpalCiscoNSE))
      ++maxFormats;
#endif
  }

  if (mediaDescription->GetSDPMediaFormats().GetSize() > maxFormats)
    multipleFormats = true;

#if OPAL_RTP_FEC
  NegotiateFECMediaFormats(*mediaSession);
#endif

  PTRACE_IF(3, otherSidesDir == SDPMediaDescription::Inactive, "No streams opened as " << mediaType << " inactive");
  return true;
}


void OpalSDPConnection::FinaliseRtx(const OpalMediaStreamPtr & stream, SDPMediaDescription * sdp)
{
  if (stream == NULL)
    return;

  OpalRTPSession * rtpSession = dynamic_cast<OpalRTPSession *>(GetMediaSession(stream->GetSessionID()));
  if (rtpSession == NULL)
    return;

  // Make sure rtx has correct PT
  RTP_DataFrame::PayloadTypes primaryPT = stream->GetMediaFormat().GetPayloadType();
  RTP_DataFrame::PayloadTypes rtxPT = RTP_DataFrame::IllegalPayloadType;
  PString rtxName = OpalRtx::GetName(rtpSession->GetMediaType());
  OpalMediaFormatList remoteFormats = GetMediaFormats();
  for (OpalMediaFormatList::iterator it = remoteFormats.begin(); it != remoteFormats.end(); ++it) {
    if (it->GetName() == rtxName && it->GetOptionPayloadType(OpalRtx::AssociatedPayloadTypeOption()) == primaryPT) {
      rtxPT = it->GetPayloadType();
      if (sdp != NULL)
        sdp->AddMediaFormat(*it);
      break;
    }
  }

  if (rtxPT == RTP_DataFrame::IllegalPayloadType) {
    PTRACE(4, "No RTX present for stream " << *stream);
    return;
  }

  // Adjust the session SSRCs
  PTRACE(4, "Finalising RTX as " << rtxPT << " for primary " << primaryPT << " on stream " << *stream);
  rtpSession->FinaliseSyncSourceRtx(primaryPT, rtxPT, stream->IsSource() ? OpalRTPSession::e_Receiver : OpalRTPSession::e_Sender);
}


bool OpalSDPConnection::OnHoldStateChanged(bool)
{
  return true;
}


void OpalSDPConnection::OnMediaStreamOpenFailed(bool)
{
}

#if OPAL_BFCP

bool OpalSDPConnection::OnBfcpConnected()
{
  return true;
}

bool OpalSDPConnection::OnBfcpDisconnected()
{
  return true;
}

bool OpalSDPConnection::OnFloorRequestStatusAccepted(const BFCPEvent & /*evt*/)
{
  return true;
}

bool OpalSDPConnection::OnFloorRequestStatusGranted(const BFCPEvent & /*evt*/)
{
  return true;
}

bool OpalSDPConnection::OnFloorRequestStatusAborted(const BFCPEvent & /*evt*/)
{
  return true;
}

bool OpalSDPConnection::OnFloorStatusAccepted(const BFCPEvent & /*evt*/)
{
  return true;
}

bool OpalSDPConnection::OnFloorStatusGranted(const BFCPEvent & /*evt*/)
{
  return true;
}

bool OpalSDPConnection::OnFloorStatusAborted(const BFCPEvent & /*evt*/)
{
  return true;
}

#endif // OPAL_BFCP

#endif // OPAL_SDP


///////////////////////////////////////////////////////////////////////////////
