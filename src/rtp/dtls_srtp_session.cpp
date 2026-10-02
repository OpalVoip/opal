/*
 * dtls_srtp_session.cxx
 *
 * SRTP protocol session handler with DTLS key exchange
 *
 * OPAL Library
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
 * The Original Code is OPAL Library.
 *
 * The Initial Developer of the Original Code is Sysolyatin Pavel
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "dtls_srtp_session.h"
#endif

#include <opal_config.h>

#if OPAL_SRTP
#include <rtp/dtls_srtp_session.h>
#ifdef P_SSL_LIB1
#pragma comment(lib, P_SSL_LIB1)
#endif
#ifdef P_SSL_LIB2
#pragma comment(lib, P_SSL_LIB2)
#endif

#include <opal/manager.h>
#include <opal/endpoint.h>
#include <opal/connection.h>


#define PTraceModule() "DTLS"


class OpalDTLSContext : public PSSLContext
{
    PCLASSINFO(OpalDTLSContext, PSSLContext);
  public:
    OpalDTLSContext(const OpalDTLSMediaTransport & transport)
      : PSSLContext(PSSLContext::DTLSv1_2)
    {
      if (!UseCertificate(transport.m_certificate))
      {
        PTRACE(1, "Could not use DTLS certificate.");
        return;
      }

      if (!UsePrivateKey(transport.m_privateKey))
      {
        PTRACE(1, "Could not use private key for DTLS.");
        return;
      }

      /* Place into the DTLS negotiation extension the crypto suites we support in order
        of their strength. Especially 80 bit salt over 32 bit salt. */
      std::map<unsigned, PString> cryptoSuitesByStrength;
      OpalMediaCryptoSuiteFactory::KeyList_T all = OpalMediaCryptoSuiteFactory::GetKeyList();
      for (OpalMediaCryptoSuiteFactory::KeyList_T::iterator it = all.begin(); it != all.end(); ++it) {
        OpalMediaCryptoSuite & cryptoSuite = *OpalMediaCryptoSuiteFactory::CreateInstance(*it);
        cryptoSuitesByStrength[cryptoSuite.GetCipherKeyBits()+cryptoSuite.GetAuthSaltBits()*1000] = cryptoSuite.GetDTLSName();
      }

      PStringStream ext;
      for (std::map<unsigned, PString>::reverse_iterator it = cryptoSuitesByStrength.rbegin(); it != cryptoSuitesByStrength.rend(); ++it) {
        if (it->second.IsEmpty())
          continue;
        if (!ext.IsEmpty())
          ext << ':';
        ext << it->second;
      }

      if (SetExtension(ext))
        PTRACE(4, "Extension set to \"" << ext << '"');
      else {
        PTRACE(1, "Could not set extension \"" << ext << "\" for SSL context.");
      }
    }
};


OpalDTLSMediaTransport::DTLSChannel::DTLSChannel(OpalDTLSMediaTransport & transport, PChannel * channel)
  : PSSLChannelDTLS(new OpalDTLSContext(transport), true)
  , m_transport(transport)
  , m_lastResponseLength(0)
{
  SetMTU(m_transport.m_MTU);
  SetVerifyMode(PSSLContext::VerifyPeerMandatory,
                PCREATE_NOTIFIER2_EXT(m_transport, OpalDTLSMediaTransport, OnVerify, PSSLChannel::VerifyInfo &));
  Open(channel);
}


bool OpalDTLSMediaTransport::DTLSChannel::Read(void * buf, PINDEX size)
{
  if (CheckNotOpen())
    return false;

  if (!m_transport.IsEstablished()) {
    PTRACE(4, "Awaiting transport establishment.");

    // Keep reading until we have both the remote address and remote fingerprint
    // (i.e. ICE complete and remote SDP processed)
    while (!m_transport.OpalDTLSMediaTransportParent::IsEstablished()
            || !m_transport.GetRemoteFingerprint().IsValid()) {
      // Let lower protocol layers handle their packets
      if (!PSSLChannelDTLS::Read(buf, size))
        return false;
      // Cache the most recent packet data passed up to us (probably ClientHello)
      m_lastReceivedPackets.push_back(PBYTEArray(static_cast<BYTE*>(buf), GetLastReadCount(), true));
      if (m_lastReceivedPackets.size() > 2)
        m_lastReceivedPackets.pop_front();
    }

    // In here, DTLSChannel::BioRead is called instead of DTLSChannel::Read
    if (!m_transport.PerformHandshake(*this))
      return false;

    // Send dummy packet to trigger OnEstablished(), in case we don't get any real data
    m_transport.OpalMediaTransport::InternalRxData(e_Media, PBYTEArray(1));
  }

  while (PSSLChannelDTLS::Read(buf, size)) {
    PINDEX len = GetLastReadCount();
    if (len == 0)
      return false; // Shouldn't happen, but just in case ...

#pragma pack(1)
    struct AlertFrame {
      uint8_t  m_type;
      int8_t   m_versionMajor;
      int8_t   m_versionMinor;
      PUInt16b m_epoch;
      PUInt16b m_snHigh;
      PUInt32b m_snLow;
      PUInt16b m_length;
      uint8_t  m_cipherType;
      uint8_t  m_alertLevel;
      uint8_t  m_alertDescription;
    } * frame = reinterpret_cast<AlertFrame *>(buf);
#pragma pack()

    // From https://tools.ietf.org/html/rfc7983
    if (frame->m_type <= 20 || frame->m_type >= 63)
      return true; // Not a DTLS packet

    if (len < sizeof(*frame)) {
      PTRACE(2, "Truncated frame received: " << PHexDump(buf, len));
      continue;
    }

    if (frame->m_type == 21 /* DTLS Alert */ &&
        frame->m_cipherType == 0 && /* stream cipher */
        frame->m_length >= 2 &&
        frame->m_alertLevel == 1 && /* Warning level */
        frame->m_alertDescription == 0) /* close_notify */
    {
      PTRACE(2, "Received close_notify from remote.");
      Shutdown(ShutdownReadAndWrite); // Does SSL_shutdown, sending a close_notify back
      CloseBaseReadChannel();  // Then close (most likely) socket
      return false;
    }

    PTRACE(4, "Packet received after handshake completed:"
           " type=" << (unsigned)frame->m_type << ","
           " ver=" << ~frame->m_versionMajor << '.' << ~frame->m_versionMinor << ","
           " epoch=" << frame->m_epoch << ","
           " sn=" << (frame->m_snHigh*0x100000000ULL+frame->m_snLow) << ","
           " cipher=" << (unsigned)frame->m_cipherType << ","
           " len=" << frame->m_length << ","
           " level=" << (unsigned)frame->m_alertLevel << ","
           " desc=" << (unsigned)frame->m_alertDescription << '\n'
           << setprecision(2) << PHexDump(buf, len, false));

    /* In case remote didn't get our last response, send it again if we get
       a DTLS packet. This is a bit primitive, but we don't really want to
       decode the whole packet to see if it really a retransmit of the last
       DTLS exchange. If we are here, we thought the handshake was complete,
       so the only logical reason for getting another DTLS packet is if remote
       didn't get the last packet we sent. Plausible on UDP. */
    if (m_lastResponseLength > 0) {
      PTRACE(2, "Resending last response: " << m_lastResponseLength << " bytes");
      if (!Write(m_lastResponseData, m_lastResponseLength))
        return false;
    }
  }

  return false;
}


int OpalDTLSMediaTransport::DTLSChannel::BioRead(char * buf, int len)
{
  while (!m_lastReceivedPackets.empty()) {
    // First return packets received before we were ready to handshake
    PBYTEArray packet = m_lastReceivedPackets.front();
    m_lastReceivedPackets.pop_front();

    size_t packetLen = packet.size();
    if (static_cast<size_t>(len) >= packetLen) {
      memcpy(buf, packet.GetPointer(), packetLen);
      PTRACE(5, "Read " << packetLen << " bytes from pre-handshake cache");
      return packetLen;
    }

    PTRACE(3, "Cannot use " << packetLen << " bytes from pre-handshake cache, expected max is " << len << " bytes");
  }

  int result = PSSLChannelDTLS::BioRead(buf, len);
  if (result < 0)
    PTRACE_IF(2, IsOpen(), "Read error: " << GetErrorText(PChannel::LastReadError));
  else {
#if PTRACING
    static unsigned const Level = 5;
    if (PTrace::CanTrace(Level)) {
      PUDPSocket * udp = dynamic_cast<PUDPSocket *>(GetBaseReadChannel());
      if (udp != NULL)
        PTRACE_BEGIN(Level) << "Read " << result << " bytes from " << udp->GetLastReceiveAddress() << PTrace::End;
    }
#endif // PTRACING
  }
  return result;
}


int OpalDTLSMediaTransport::DTLSChannel::BioWrite(const char * buf, int len)
{
  int result = PSSLChannelDTLS::BioWrite(buf, len);
  if (result < 0)
    PTRACE_IF(2, IsOpen(), "Write error: " << GetErrorText(PChannel::LastWriteError));
  else {
    memcpy(m_lastResponseData.GetPointer(result), buf, result);
    m_lastResponseLength = result;

#if PTRACING
    static unsigned const Level = 5;
    if (PTrace::CanTrace(Level)) {
      PUDPSocket * udp = dynamic_cast<PUDPSocket *>(GetBaseReadChannel());
      if (udp != NULL)
        PTRACE_BEGIN(Level) << "Written " << result << " bytes to " << udp->GetSendAddress() << PTrace::End;
    }
#endif // PTRACING
  }
  return result;
}


OpalDTLSMediaTransport::OpalDTLSMediaTransport(const PString & name, OpalMediaSession::SetUpMode mode, const PSSLCertificateFingerprint& fp)
  : OpalDTLSMediaTransportParent(name)
  , m_setupMode(mode)
  , m_handshakeTimeout(0, 2)
  , m_MTU(1400)
  , m_privateKey(1024)
  , m_remoteFingerprint(fp)
{
}


bool OpalDTLSMediaTransport::Open(OpalMediaSession & session,
                                  PINDEX count,
                                  const PString & localInterface,
                                  const OpalTransportAddress & remoteAddress)
{
  PStringStream subject;
  subject << "/O=" + PProcess::Current().GetManufacturer() << "/CN=" << GetLocalAddress().Mid(4);
  if (!m_certificate.CreateRoot(subject, m_privateKey)) {
    PTRACE(1, "Could not create certificate for DTLS.");
    return false;
  }

  m_handshakeTimeout = session.GetStringOptions().GetVar(OPAL_OPT_DTLS_TIMEOUT,
                       session.GetConnection().GetEndPoint().GetManager().GetDTLSTimeout());
  m_MTU = session.GetConnection().GetMaxRtpPayloadSize();

  return OpalDTLSMediaTransportParent::Open(session, count, localInterface, remoteAddress);
}


PChannel * OpalDTLSMediaTransport::AddWrapperChannels(SubChannels subchannel, PChannel * channel)
{
  // Should already be locked, or in ctor
  return new DTLSChannel(*this, OpalDTLSMediaTransportParent::AddWrapperChannels(subchannel, channel));
}


bool OpalDTLSMediaTransport::IsEstablished() const
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return false);
  for (PINDEX i = 0; i < 2; ++i) {
    if (m_keyInfo[i].get() == NULL)
      return false;
  }
  return OpalDTLSMediaTransportParent::IsEstablished();
}


bool OpalDTLSMediaTransport::GetKeyInfo(OpalMediaCryptoKeyInfo * keyInfo[2])
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return false);
  for (PINDEX i = 0; i < 2; ++i) {
    if ((keyInfo[i] = m_keyInfo[i].get()) == NULL)
      return false;
  }
  return true;
}


void OpalDTLSMediaTransport::SetSetUpMode(OpalMediaSession::SetUpMode mode)
{
  m_setupMode = mode;
}


PSSLCertificateFingerprint OpalDTLSMediaTransport::GetLocalFingerprint(PSSLCertificateFingerprint::HashType hashType) const
{
  return PSSLCertificateFingerprint(hashType, m_certificate);
}


bool OpalDTLSMediaTransport::SetRemoteFingerprint(const PSSLCertificateFingerprint& fp)
{
  P_INSTRUMENTED_LOCK_READ_WRITE(return false);

  if (m_remoteFingerprint == fp)
    return false;

  bool firstTime = !m_remoteFingerprint.IsValid();

  m_remoteFingerprint = fp;

  if (firstTime)
    return false;

  if (IsEstablished()) {
    PTRACE(2, "Remote fingerprint changed, renegotiating DTLS in read thread");
    P_INSTRUMENTED_LOCK_READ_WRITE(return false);
    for (PINDEX i = 0; i < 2; ++i)
      m_keyInfo[i].reset();
  }
  else {
    PTRACE(2, "Remote fingerprint changed, renegotiating DTLS immediately");
    for (vector<ChannelInfo>::iterator it = m_subchannels.begin(); it != m_subchannels.end(); ++it) {
      DTLSChannel * channel = dynamic_cast<DTLSChannel *>(it->m_channel);
      if (PAssert(channel != NULL, "Not a DTLS channel"))
        PerformHandshake(*channel);
    }
  }

  return true;
}


PSSLCertificateFingerprint OpalDTLSMediaTransport::GetRemoteFingerprint() const
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return PSSLCertificateFingerprint());
  return m_remoteFingerprint;
}


bool OpalDTLSMediaTransport::PerformHandshake(DTLSChannel & channel)
{
  switch (m_setupMode.load()) {
    case OpalMediaSession::SetUpModePassive:
      if (channel.Accept())
        break;

      PTRACE(2, *this << "could not accept DTLS channel");
      return false;

    case OpalMediaSession::SetUpModeActive:
      if (channel.Connect())
        break;

      PTRACE(2, *this << "could not connect DTLS channel");
      return false;

    default :
      PTRACE(5, *this << "not ready to perform handshake in DTLS channel");
      return false;
  }

  PTRACE(4, "Performing handshake: timeout=" << m_handshakeTimeout);

  PTimeInterval oldReadTimeout = channel.GetReadTimeout();
  channel.SetReadTimeout(m_handshakeTimeout);
  bool ok = channel.ExecuteHandshake();
  channel.SetReadTimeout(oldReadTimeout);

  if (!ok) {
    PTRACE_IF(2, channel.GetErrorCode() != PChannel::NotOpen,
              *this << "error in DTLS handshake:"
              " code=" << channel.GetErrorCode() << ","
              " number=" << channel.GetErrorNumber() << ","
              " text=\"" << channel.GetErrorText() << '"');
    return false;
  }

  PCaselessString profileName = channel.GetSelectedProfile();
  const OpalMediaCryptoSuite* cryptoSuite = NULL;
  OpalMediaCryptoSuiteFactory::KeyList_T all = OpalMediaCryptoSuiteFactory::GetKeyList();
  for (OpalMediaCryptoSuiteFactory::KeyList_T::iterator it = all.begin(); ; ++it) {
    if (it == all.end()) {
      PTRACE(2, *this << "error in SRTP profile (" << profileName << ") after DTLS handshake.");
      return false;
    }

    cryptoSuite = OpalMediaCryptoSuiteFactory::CreateInstance(*it);
    if (profileName == cryptoSuite->GetDTLSName())
      break;
  }

  PINDEX keyLength = cryptoSuite->GetCipherKeyBytes();
  PINDEX saltLength = std::max(cryptoSuite->GetAuthSaltBytes(), (PINDEX)14); // Weird, but 32 bit salt still needs 14 bytes,

  PBYTEArray keyMaterial = channel.GetKeyMaterial((saltLength + keyLength)*2, "EXTRACTOR-dtls_srtp");
  if (keyMaterial.IsEmpty()) {
    PTRACE(2, *this << "no SRTP keys after DTLS handshake.");
    return false;
  }

  {
    P_INSTRUMENTED_LOCK_READ_WRITE(return false);

    OpalSRTPKeyInfo * keyInfo = dynamic_cast<OpalSRTPKeyInfo*>(cryptoSuite->CreateKeyInfo());
    PAssertNULL(keyInfo);

    keyInfo->SetCipherKey(PBYTEArray(keyMaterial, keyLength));
    keyInfo->SetAuthSalt(PBYTEArray(keyMaterial + keyLength*2, saltLength));
    m_keyInfo[channel.IsServer() ? OpalRTPSession::e_Receiver : OpalRTPSession::e_Sender].reset(keyInfo);

    keyInfo = dynamic_cast<OpalSRTPKeyInfo*>(cryptoSuite->CreateKeyInfo());
    keyInfo->SetCipherKey(PBYTEArray(keyMaterial + keyLength, keyLength));
    keyInfo->SetAuthSalt(PBYTEArray(keyMaterial + keyLength*2 + saltLength, saltLength));
    m_keyInfo[channel.IsServer() ? OpalRTPSession::e_Sender : OpalRTPSession::e_Receiver].reset(keyInfo);
  }

  PTRACE(3, *this << "completed DTLS handshake.");
  return true;
}


void OpalDTLSMediaTransport::OnVerify(PSSLChannel &, PSSLChannel::VerifyInfo & info)
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return);
  info.m_ok = m_remoteFingerprint.MatchForCertificate(info.m_peerCertificate);
  PTRACE_IF(2, !info.m_ok, "Invalid remote certificate.");
}


///////////////////////////////////////////////////////////////////////////////

const PCaselessString & OpalDTLSSRTPSession::RTP_DTLS_SAVP () { static const PConstCaselessString s("UDP/TLS/RTP/SAVP" ); return s; }
const PCaselessString & OpalDTLSSRTPSession::RTP_DTLS_SAVPF() { static const PConstCaselessString s("UDP/TLS/RTP/SAVPF" ); return s; }

PFACTORY_CREATE(OpalMediaSessionFactory, OpalDTLSSRTPSession, OpalDTLSSRTPSession::RTP_DTLS_SAVP());
bool OpalRegisteredSAVPF = OpalMediaSessionFactory::RegisterAs(OpalDTLSSRTPSession::RTP_DTLS_SAVPF(), OpalDTLSSRTPSession::RTP_DTLS_SAVP());


OpalDTLSSRTPSession::OpalDTLSSRTPSession(const Init & init)
  : OpalSRTPSession(init)
{
  // Change default from "Not Set", DTLS must have this one or the other
  m_setupMode = SetUpModeActive;
}


OpalDTLSSRTPSession::~OpalDTLSSRTPSession()
{
  Close();
}


bool OpalDTLSSRTPSession::SetSetUpMode(OpalMediaSession::SetUpMode mode)
{
  bool changed = OpalSRTPSession::SetSetUpMode(mode);

  PTRACE_IF(3, changed, *this << "set DTLS to " << (mode == SetUpModePassive ? "passive" : " active") << " mode");

  OpalMediaTransportPtr transport = m_transport;
  if (transport != NULL) {
    OpalDTLSMediaTransport * dtls = dynamic_cast<OpalDTLSMediaTransport *>(&*transport);
    if (dtls != NULL)
      dtls->SetSetUpMode(mode);
  }

  return changed;
}


PSSLCertificateFingerprint OpalDTLSSRTPSession::GetLocalFingerprint(PSSLCertificateFingerprint::HashType hashType) const
{
  OpalMediaTransportPtr transport = m_transport;
  if (transport == NULL) {
    PTRACE(3, "Tried to get certificate fingerprint before transport opened");
    return PSSLCertificateFingerprint();
  }

  return dynamic_cast<const OpalDTLSMediaTransport &>(*transport).GetLocalFingerprint(hashType);
}


void OpalDTLSSRTPSession::SetRemoteFingerprint(const PSSLCertificateFingerprint& fp)
{
  if (!fp.IsValid()) {
    PTRACE(2, "Invalid fingerprint supplied.");
    return;
  }

  P_INSTRUMENTED_LOCK_READ_WRITE(return);

  OpalMediaTransportPtr transport = m_transport;
  if (transport == NULL)
    m_earlyRemoteFingerprint = fp;
  else if (dynamic_cast<OpalDTLSMediaTransport &>(*transport).SetRemoteFingerprint(fp))
    ApplyKeysToSRTP(*transport);
}


OpalMediaTransport * OpalDTLSSRTPSession::CreateMediaTransport(const PString & name)
{
  P_INSTRUMENTED_LOCK_READ_ONLY(return NULL);
  return new OpalDTLSMediaTransport(name, m_setupMode, m_earlyRemoteFingerprint);
}


#endif // OPAL_SRTP
