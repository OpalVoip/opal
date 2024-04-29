/*
 * ivr.cxx
 *
 * Interactive Voice Response support.
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (c) 2000 Equivalence Pty. Ltd.
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
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "ivr.h"
#endif

#include <opal_config.h>

#include <ep/ivr.h>
#include <opal/call.h>
#include <opal/patch.h>


#define new PNEW
#define PTraceModule() "IVR"


#if OPAL_IVR


/////////////////////////////////////////////////////////////////////////////

OpalIVREndPoint::OpalIVREndPoint(OpalManager & mgr, const char * prefix)
  : OpalLocalEndPoint(mgr, prefix, false)
  , m_ttsCache(new PVXMLCache())
{
  SetDefaultVXML("<?xml version=\"1.0\"?>\n"
                "<vxml version=\"1.0\">\n"
                "  <form id=\"root\">\n"
                "    <audio src=\"file:welcome.wav\">\n"
                "      This is the OPAL, V X M L test program, please speak after the tone.\n"
                "    </audio>\n"
                "    <record name=\"msg\" beep=\"true\" dtmfterm=\"true\" dest=\"file:recording.wav\" maxtime=\"10s\"/>\n"
                "  </form>\n"
                 "</vxml>\n");

  PTRACE(4, "Created endpoint.");
}


OpalIVREndPoint::~OpalIVREndPoint()
{
  PTRACE(4, "Deleted endpoint.");
}


PStringList OpalIVREndPoint::GetAvailableStringOptions() const
{
  static char const * const StringOpts[] = {
    OPAL_OPT_IVR_NATIVE_CODEC,
    OPAL_OPT_IVR_TEXT_TO_SPEECH,
    OPAL_OPT_IVR_SPEECH_RECOGNITION,
    OPAL_OPT_IVR_RECORDING_DIR,
    OPAL_OPT_IVR_TTS_CACHE_DIR,
    OPAL_OPT_IVR_PROPERTY
  };

  return OpalEndPoint::GetAvailableStringOptions() + PStringList(PARRAYSIZE(StringOpts), StringOpts, true);
}


PSafePtr<OpalConnection> OpalIVREndPoint::MakeConnection(OpalCall & call,
                                                    const PString & remoteParty,
                                                             void * userData,
                                                       unsigned int options,
                                    OpalConnection::StringOptions * stringOptions)
{
  return AddConnection(CreateConnection(call, userData, remoteParty, options, stringOptions));
}


OpalMediaFormatList OpalIVREndPoint::GetMediaFormats() const
{
  OpalMediaFormatList mediaFormats;

  mediaFormats += OpalPCM16;
  mediaFormats += OpalRFC2833;
#if P_VXML_VIDEO
  mediaFormats += OPAL_YUV420P;
#endif

  return mediaFormats;
}


OpalIVRConnection * OpalIVREndPoint::CreateConnection(OpalCall & call,
                                                      void * userData,
                                                      const PString & vxml,
                                                      unsigned int options,
                                                      OpalConnection::StringOptions * stringOptions)
{
  return new OpalIVRConnection(call, *this, userData, vxml, options, stringOptions);
}


static OpalConnection::StringOptions ExtractOptionsFromVXML(const PString & vxml)
{
  OpalConnection::StringOptions options;

  PString vxmlSource = vxml;
  PURL url(vxml, NULL);
  if (!url.IsEmpty())
    url.LoadResource(vxmlSource);
  else {
    PTextFile vxmlFile;
    if (vxmlFile.Open(vxml, PFile::ReadOnly))
      vxmlSource = vxmlFile.ReadString(P_MAX_INDEX);
  }

  PRegularExpression embeddedOption("<!--OPAL-[^=]*=");
  PINDEX pos = 0, len;
  while (vxmlSource.FindRegEx(embeddedOption, pos, len, pos)) {
    PINDEX end = vxmlSource.Find("-->", pos + len);
    PString name = vxmlSource.Mid(pos + 9, len - 10).RightTrim();
    PString value = vxmlSource(pos + len, end - 1);
    if (options.Contains(name))
      options.SetAt(name, options.GetString(name) + '\n' + value);
    else
      options.SetAt(name, value);

    pos = end + 3;
  }

  return options;
}


void OpalIVREndPoint::SetDefaultVXML(const PString & vxml)
{
  m_defaultsMutex.Wait();
  m_defaultVXML = vxml;
  m_defaultStringOptions.Merge(ExtractOptionsFromVXML(vxml), PStringOptions::e_MergeOverwrite);
  m_defaultsMutex.Signal();
}


void OpalIVREndPoint::OnEndDialog(OpalIVRConnection & connection)
{
  PTRACE(3, "OnEndDialog for connection " << connection);
  connection.Release();
}



/////////////////////////////////////////////////////////////////////////////

OpalIVRConnection::OpalIVRConnection(OpalCall & call,
                                     OpalIVREndPoint & ep,
                                     void * userData,
                                     const PString & remoteParty,
                                     unsigned int options,
                                     OpalConnection::StringOptions * stringOptions)
  : OpalLocalConnection(call, ep, userData, options, stringOptions, 'I')
  , m_ivrEndPoint(ep)
  , P_DISABLE_MSVC_WARNINGS(4355, m_vxmlSession(*this))
  , m_vxmlStarted(false)
{
  PTRACE_CONTEXT_ID_TO(m_vxmlSession);
  SetVXML(remoteParty);

#if OPAL_VIDEO
  m_autoStartInfo[OpalMediaType::Video()] = OpalMediaType::DontOffer;
#endif

  m_vxmlSession.SetCache(ep.GetTextToSpeechCache());

  PTRACE(4, "Constructed");
}


OpalIVRConnection::~OpalIVRConnection()
{
  PTRACE(4, "Destroyed.");
}


void OpalIVRConnection::OnApplyStringOptions()
{
  OpalLocalConnection::OnApplyStringOptions();

  m_vxmlSession.SetTextToSpeech(m_stringOptions.GetString(OPAL_OPT_IVR_TEXT_TO_SPEECH, m_ivrEndPoint.GetDefaultTextToSpeech()), m_stringOptions);
  m_vxmlSession.SetSpeechRecognition(m_stringOptions.GetString(OPAL_OPT_IVR_SPEECH_RECOGNITION, m_ivrEndPoint.GetDefaultSpeechRecognition()), m_stringOptions);
  m_vxmlSession.SetRecordDirectory(m_stringOptions.GetString(OPAL_OPT_IVR_RECORDING_DIR, m_ivrEndPoint.GetRecordDirectory()));
  m_vxmlSession.SetProxies(m_stringOptions);
#if OPAL_PTLIB_SSL
  if (m_endpoint.HasSSLCertificates())
    m_vxmlSession.SetSSLCredentials(m_endpoint);
  else
    m_vxmlSession.SetSSLCredentials(m_endpoint.GetManager());
#endif

  if (m_stringOptions.Contains(OPAL_OPT_IVR_TTS_CACHE_DIR)) {
    PDirectory dir = m_stringOptions.GetString(OPAL_OPT_IVR_TTS_CACHE_DIR);
    if (dir != m_ivrEndPoint.GetCacheDir()) {
      if (m_ttsCache == NULL)
        m_ttsCache = new PVXMLCache();
      m_ttsCache->SetDirectory(dir);
      m_vxmlSession.SetCache(m_ttsCache);
    }
  }

  PStringArray properties = m_stringOptions.GetString(OPAL_OPT_IVR_PROPERTY).Lines();
  for (PINDEX i = 0; i < properties.GetSize(); ++i) {
    PString name, value;
    properties[i].Split(':', name, value, PString::SplitDefaultToBefore|PString::SplitTrimBefore);
    m_vxmlSession.SetProperty(name, value, true);
  }
}


void OpalIVRConnection::OnReleased()
{
  m_vxmlSession.Close();
  OpalLocalConnection::OnReleased();
}


void OpalIVRConnection::OnStartMediaPatch(OpalMediaPatch & patch)
{
  OpalLocalConnection::OnStartMediaPatch(patch);

  for (StreamDict::iterator it = m_mediaStreams.begin(); it != m_mediaStreams.end(); ++it) {
    OpalMediaStreamPtr mediaStream = it->second;
    if (mediaStream != NULL && !mediaStream->IsEstablished()) {
      PTRACE(4, "Delayed starting VXML, not yet established " << *mediaStream);
      return;
    }
  }

  if (!m_vxmlStarted.exchange(true))
    GetEndPoint().GetManager().QueueDecoupledEvent(
          new PSafeWorkNoArg<OpalIVRConnection, bool>(this, &OpalIVRConnection::StartVXML));
}


bool OpalIVRConnection::OnTransferNotify(const PStringToString & info,
                                         const OpalConnection * transferringConnection)
{
  if (info["party"] == "B") {
    PCaselessString result = info["result"];
    if (result == "success")
      m_vxmlSession.SetTransferStatus(PVXMLSession::TransferSuccessful);
    else if (result == "error" || result == "unknown")
      m_vxmlSession.SetTransferStatus(PVXMLSession::TransferUnknown);
    else if (result == "failed") {
      CallEndReason reason(info["reason"].AsInteger());
      switch (reason) {
        case EndedByRemoteBusy:
        case EndedByRefusal:
          m_vxmlSession.SetTransferStatus(PVXMLSession::TransferBusy);
          break;
        case EndedByRemoteCongestion:
        case EndedByLocalCongestion:
          m_vxmlSession.SetTransferStatus(PVXMLSession::TransferCongestion);
          break;
        case EndedByNoAnswer :
          m_vxmlSession.SetTransferStatus(PVXMLSession::TransferNoAnswer);
          break;
        default :
          if (transferringConnection && reason.code != 0)
            m_vxmlSession.SetTransferStatus(PVXMLSession::TransferFailed,
                                            PSTRSTRM(transferringConnection->GetPrefixName() << '.' << reason.code));
          else
            m_vxmlSession.SetTransferStatus(PVXMLSession::TransferUnknown);
      }
    }
  }

  return OpalConnection::OnTransferNotify(info, transferringConnection);
}


bool OpalIVRConnection::TransferConnection(const PString & remoteParty)
{
  if (IsReleased())
    return false;

  PSafeLockReadWrite mutex(*this);
  if (!mutex.IsLocked())
    return false;

  SetVXML(remoteParty);
  return StartVXML();
}


void OpalIVRConnection::SetVXML(const PString & vxml)
{
  // First strip off the prefix if present
  PINDEX prefixLength = 0;
  if (vxml.Find(GetPrefixName()+":") == 0)
    prefixLength = GetPrefixName().GetLength()+1;

  m_vxmlScript = vxml.Mid(prefixLength);
  if (m_vxmlScript.IsEmpty() || m_vxmlScript == "*")
    m_vxmlScript = m_ivrEndPoint.GetDefaultVXML();
  m_localPartyURL = GetPrefixName() + ':' + PURL::TranslateString(m_vxmlScript.Left(m_vxmlScript.FindOneOf("\r\n")), PURL::LoginTranslation);

  m_stringOptions.Merge(ExtractOptionsFromVXML(m_vxmlScript), PStringOptions::e_MergeOverwrite);
}


static PStringToString * MakeRedirectInfo(const PString & uri)
{
  PStringToString * redirect =  new PStringToString;
  redirect->SetAt("uri", uri);
  redirect->SetAt("pi", "Unknown");
  redirect->SetAt("si", "Unknown");
  redirect->SetAt("reason", "Unknown");
  return redirect;
}


PBoolean OpalIVRConnection::StartVXML()
{
  if (m_vxmlScript.IsEmpty())
    return false;

  PSafeLockReadWrite mutex(*this);
  if (!mutex.IsLocked())
    return false;

  PArray<PStringToString> redirects;
  PSafePtr<OpalConnection> network = GetOtherPartyConnection();
  if (network != NULL) {
    PURL redirectingParty(network->GetRedirectingParty(), "tel");
    if (!redirectingParty.IsEmpty())
      redirects.Append(MakeRedirectInfo(redirectingParty));
  }

  redirects.Append(MakeRedirectInfo(GetCalledPartyURL()));

  PStringToString aai;
  aai.SetAt("callToken", GetCall().GetToken());

  PURL remoteURL = GetRemotePartyURL();
  m_vxmlSession.SetConnectionVars(
    GetLocalPartyURL(),
    remoteURL,
    remoteURL.GetScheme(),
    "Unknown",
    redirects,
    aai,
    IsOriginating()
  );

  // These are for backward compatibility
  m_vxmlSession.SetVar("session.connection.local.dnis", GetCalledPartyNumber());
  m_vxmlSession.SetVar("session.connection.remote.ani", GetRemotePartyNumber());
  m_vxmlSession.SetVar("session.connection.remote.ip", remoteURL.GetHostName());
  m_vxmlSession.SetVar("session.connection.remote.port", remoteURL.GetPort());
  m_vxmlSession.SetVar("session.connection.calltoken", GetCall().GetToken());

  return m_vxmlSession.Load(m_vxmlScript) || StartScript();
}


bool OpalIVRConnection::StartScript()
{
  PINDEX repeat = 1;
  PINDEX delay = 0;
  PString voice;

  PTRACE(4, "Started using simplified script: " << m_vxmlScript);

  PINDEX i;
  PStringArray tokens = m_vxmlScript.Tokenise(';', false);
  for (i = 0; i < tokens.GetSize(); ++i) {
    PString str(tokens[i]);

    if (str.Find("file:") == 0) {
      PURL url = str;
      if (url.IsEmpty()) {
        PTRACE(2, "Invalid URL \"" << str << '"');
        continue;
      }

      PFilePath fn = url.AsFilePath();
      if (fn.IsEmpty()) {
        PTRACE(2, "Unsupported host in URL " << url);
        continue;
      }

      if (!voice.IsEmpty())
        fn = fn.GetDirectory() + voice + PDIR_SEPARATOR + fn.GetFileName();

      PTRACE(3, "Playing file " << fn << ' ' << repeat << " times, " << delay << "ms");
      m_vxmlSession.PlayFile(fn, repeat, delay);
      continue;
    }

    PString key, val;
    str.Split('=', key, val, PString::SplitDefaultToBefore|PString::SplitTrimBefore);

    if (key *= "repeat") {
      if (!val.IsEmpty())
        repeat = val.AsInteger();
    }
    else if (key *= "delay") {
      if (!val.IsEmpty())
        delay = val.AsInteger();
    }
    else if (key *= "voice") {
      if (!val.IsEmpty()) {
        voice = val;
        PTextToSpeech * tts = m_vxmlSession.GetTextToSpeech();
        if (tts != NULL)
          tts->SetVoice(voice);
      }
    }

    else if (key *= "tone") {
      PTRACE(3, "Playing tone " << val);
      m_vxmlSession.PlayTone(val, repeat, delay);
   }

    else if (key *= "speak") {
      if (!val.IsEmpty() && (val[0] == '$'))
        val = m_vxmlSession.GetVar(val.Mid(1));
      PTRACE(3, "Speaking text '" << val << "'");
      m_vxmlSession.PlayText(val, PTextToSpeech::Default, repeat, delay);
    }

    else if (key *= "silence") {
      unsigned msecs;
      if (val.IsEmpty() && (val[0] == '$'))
        msecs = m_vxmlSession.GetVar(val.Mid(1)).AsUnsigned();
      else
        msecs = val.AsUnsigned();
      PTRACE(3, "Speaking silence for " << msecs << " msecs");
      m_vxmlSession.PlaySilence(msecs);
    }
    else {
      PTRACE(2, "Invalid command \"" << key << "\" in \"" << m_vxmlScript << '"');
      return false;
    }
  }

  m_vxmlSession.PlayStop();
  m_vxmlSession.Trigger();

  return true;
}


void OpalIVRConnection::OnEndDialog()
{
  m_ivrEndPoint.OnEndDialog(*this);
}


OpalMediaFormatList OpalIVRConnection::GetMediaFormats() const
{
  OpalMediaFormatList mediaFormats = m_endpoint.GetMediaFormats();

  PStringArray native = m_stringOptions.GetString(OPAL_OPT_IVR_NATIVE_CODEC).Lines();
  for (PINDEX i = 0; i < native.GetSize(); ++i)
    mediaFormats += native[i];

  return mediaFormats;
}


OpalMediaStream * OpalIVRConnection::CreateMediaStream(const OpalMediaFormat & mediaFormat,
                                                       unsigned sessionID,
                                                       PBoolean isSource)
{
  if (mediaFormat.GetMediaType() == OpalMediaType::Audio())
    return new OpalIVRMediaStream(*this, mediaFormat, sessionID, isSource, m_vxmlSession);

#if OPAL_VIDEO && P_VXML_VIDEO
  if (mediaFormat.GetMediaType() == OpalMediaType::Video()) {
    if (isSource)
      return new OpalVideoMediaStream(*this, mediaFormat, sessionID, &m_vxmlSession.GetVideoSender(), NULL, false, false);
    else
      return new OpalVideoMediaStream(*this, mediaFormat, sessionID, NULL, &m_vxmlSession.GetVideoReceiver(), false, false);
  }
#endif // P_VXML_VIDEO

  return OpalLocalConnection::CreateMediaStream(mediaFormat, sessionID, isSource);
}


PBoolean OpalIVRConnection::SendUserInputString(const PString & value)
{
  PTRACE(3, "SendUserInputString(" << value << ')');

  for (PINDEX i = 0; i < value.GetLength(); i++)
    m_vxmlSession.OnUserInput(value[i]);

  return true;
}


/////////////////////////////////////////////////////////////////////////////

OpalIVRMediaStream::OpalIVRMediaStream(OpalIVRConnection & conn,
                                       const OpalMediaFormat & mediaFormat,
                                       unsigned sessionID,
                                       PBoolean isSourceStream,
                                       PVXMLSession & vxml)
  : OpalRawMediaStream(conn, mediaFormat, sessionID, isSourceStream, &vxml, FALSE)
  , m_vxmlSession(vxml)
{
  PTRACE(3, "OpalIVRMediaStream sessionID = " << sessionID << ", isSourceStream = " << isSourceStream);
}


PBoolean OpalIVRMediaStream::Open()
{
  if (m_isOpen)
    return true;

  P_INSTRUMENTED_LOCK_READ_WRITE(return false);

  if (!m_vxmlSession.IsOpen()) {
    PTRACE(3, "Opening VXML sesion via " << *this);
    if (m_vxmlSession.Open(m_mediaFormat.IsTransportable() ? m_mediaFormat.GetName() : PString(VXML_PCM16),
                           m_mediaFormat.GetClockRate(),
                           m_mediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption(), 1)))
      return true;

    PTRACE(1, "Cannot open VXML engine, incompatible media format: " << m_mediaFormat);
    return false;
  }

  PTRACE(3, "Re-opening VXML sesion via " << *this);

  PVXMLChannel * vxmlChannel = m_vxmlSession.GetAndLockVXMLChannel();
  if (vxmlChannel == NULL) {
    PTRACE(1, "VXML engine not really open");
    return false;
  }

  bool ok = false;
  if (m_mediaFormat.GetName().NumCompare(vxmlChannel->GetAudioFormat()) != EqualTo)
    PTRACE(1, "Cannot use VXML engine, asymmetrical media formats:"
           " strm=" << m_mediaFormat << ", VXML=" << vxmlChannel->GetAudioFormat());
  else if (!vxmlChannel->SetSampleRate(m_mediaFormat.GetClockRate()))
    PTRACE(1, "Cannot use VXML engine, asymmetrical sample rate:"
           " strm=" << m_mediaFormat.GetClockRate() << ", VXML=" << vxmlChannel->GetSampleRate());
  else if (!vxmlChannel->SetChannels(m_mediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption(), 1)))
    PTRACE(1, "Cannot use VXML engine, asymmetrical channels:"
           " strm=" << m_mediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption(), 1) << ", VXML=" << vxmlChannel->GetChannels());
  else
    ok = true;

  m_vxmlSession.UnLockVXMLChannel();

  return ok && OpalMediaStream::Open();
}


void OpalIVRMediaStream::InternalClose()
{
  if (m_connection.IsReleased())
    OpalRawMediaStream::InternalClose();
}


PBoolean OpalIVRMediaStream::IsSynchronous() const
{
  return IsSource();
}


#endif // OPAL_IVR
