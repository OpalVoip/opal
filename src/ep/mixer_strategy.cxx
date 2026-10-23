/*
 * mixer_strategy.cxx
 *
 * Video layout, video delivery and audio mix strategies for OpalMixer.
 *
 * Copyright (c) 2026 Vox Lucida Pty. Ltd.
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
 */

#include <ptlib.h>
#include <ep/opalmixer.h>

#if OPAL_HAS_MIXER

#include <algorithm>
#include <set>
#include <ptclib/threadpool.h>

#if OPAL_VIDEO
#include <ptlib/videoio.h>
#include <ptlib/vconvert.h>
#endif

#if OPAL_SDP
#include <sdp/sdp.h>
#endif


///////////////////////////////////////////////////////////////////////////////

bool OpalAdditiveAudioStrategy::Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame)
{
  mixer.PreMixStreams();
  frame.SetPayloadSize(0);
  mixer.MixAdditive(frame, NULL);
  return true;
}


void OpalAdditiveAudioStrategy::MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract)
{
  mixer.MixAdditive(frame, audioToSubtract);
}


bool OpalStereoAudioStrategy::Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame)
{
  mixer.MixStereo(frame);
  return true;
}


void OpalStereoAudioStrategy::MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract)
{
  (void)audioToSubtract;
  mixer.MixStereo(frame);
}


bool OpalPassThroughAudioStrategy::Mix(OpalAudioMixer &, RTP_DataFrame & frame)
{
  frame.SetPayloadSize(0);
  return true;
}


void OpalPassThroughAudioStrategy::MixListener(OpalAudioMixer &, RTP_DataFrame & frame, const short *)
{
  frame.SetPayloadSize(0);
}


#if OPAL_VIDEO

///////////////////////////////////////////////////////////////////////////////

static bool AdvanceTile(const OpalVideoMixer & mixer, unsigned left, OpalVideoMixer::MixPosition & pos)
{
  pos.x += pos.w;
  if (pos.x + pos.w <= mixer.GetFrameWidth())
    return true;

  pos.x = left;
  pos.y += pos.h;
  return pos.y + pos.h <= mixer.GetFrameHeight();
}


bool OpalGridVideoLayout::GetMixPosition(OpalVideoMixer & mixer,
                                         const OpalVideoMixer::Key_T &,
                                         bool firstStream,
                                         OpalVideoMixer::MixPosition & pos)
{
  if (!firstStream)
    return AdvanceTile(mixer, m_left, pos);

  pos.x = m_left = 0;
  pos.y = 0;
  switch (mixer.GetInputCount()) {
    case 0:
    case 1:
      pos.w = mixer.GetFrameWidth();
      pos.h = mixer.GetFrameHeight();
      break;

    case 2:
      pos.y = mixer.GetFrameHeight() / 4;
      // Fall into next case

    case 3:
    case 4:
      pos.w = mixer.GetFrameWidth() / 2;
      pos.h = mixer.GetFrameHeight() / 2;
      break;

    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      pos.w = mixer.GetFrameWidth() / 3;
      pos.h = mixer.GetFrameHeight() / 3;
      break;

    default:
      if (mixer.GetInputCount() <= 16) {
        pos.w = mixer.GetFrameWidth() / 4;
        pos.h = mixer.GetFrameHeight() / 4;
      }
      else {
        pos.w = mixer.GetFrameWidth() / 5;
        pos.h = mixer.GetFrameHeight() / 5;
      }
      break;
  }

  return true;
}


bool OpalSideBySideLetterboxLayout::GetMixPosition(OpalVideoMixer & mixer,
                                                   const OpalVideoMixer::Key_T & key,
                                                   bool firstStream,
                                                   OpalVideoMixer::MixPosition & pos)
{
  if (mixer.GetInputCount() > 2)
    return m_grid.GetMixPosition(mixer, key, firstStream, pos);

  if (!firstStream)
    return AdvanceTile(mixer, m_left, pos);

  pos.x = m_left = 0;
  pos.y = mixer.GetFrameHeight() / 4;
  pos.w = mixer.GetFrameWidth() / 2;
  pos.h = mixer.GetFrameHeight() / 2;
  return true;
}


bool OpalSideBySideScaledLayout::GetMixPosition(OpalVideoMixer & mixer,
                                                const OpalVideoMixer::Key_T & key,
                                                bool firstStream,
                                                OpalVideoMixer::MixPosition & pos)
{
  if (mixer.GetInputCount() > 2)
    return m_grid.GetMixPosition(mixer, key, firstStream, pos);

  if (!firstStream)
    return AdvanceTile(mixer, m_left, pos);

  pos.x = m_left = 0;
  pos.y = 0;
  pos.w = mixer.GetFrameWidth() / 2;
  pos.h = mixer.GetFrameHeight();
  return true;
}


bool OpalStackedPillarboxLayout::GetMixPosition(OpalVideoMixer & mixer,
                                                const OpalVideoMixer::Key_T & key,
                                                bool firstStream,
                                                OpalVideoMixer::MixPosition & pos)
{
  if (mixer.GetInputCount() > 2)
    return m_grid.GetMixPosition(mixer, key, firstStream, pos);

  if (!firstStream)
    return AdvanceTile(mixer, m_left, pos);

  pos.x = m_left = mixer.GetFrameWidth() / 4;
  pos.y = 0;
  pos.w = mixer.GetFrameWidth() / 2;
  pos.h = mixer.GetFrameHeight() / 2;
  return true;
}


bool OpalStackedScaledLayout::GetMixPosition(OpalVideoMixer & mixer,
                                             const OpalVideoMixer::Key_T & key,
                                             bool firstStream,
                                             OpalVideoMixer::MixPosition & pos)
{
  if (mixer.GetInputCount() > 2)
    return m_grid.GetMixPosition(mixer, key, firstStream, pos);

  if (!firstStream)
    return AdvanceTile(mixer, m_left, pos);

  pos.x = m_left = 0;
  pos.y = 0;
  pos.w = mixer.GetFrameWidth();
  pos.h = mixer.GetFrameHeight() / 2;
  return true;
}


bool OpalActiveSpeakerLayout::GetMixPosition(OpalVideoMixer & mixer,
                                             const OpalVideoMixer::Key_T & key,
                                             bool firstStream,
                                             OpalVideoMixer::MixPosition & pos)
{
  pos.x = pos.y = pos.w = pos.h = 0;

  OpalVideoMixer::Key_T activeSpeaker = mixer.GetActiveSpeaker();
  if ((firstStream && activeSpeaker.empty()) || key == activeSpeaker) {
    pos.w = mixer.GetFrameWidth();
    pos.h = mixer.GetFrameHeight();
  }

  return true;
}


std::unique_ptr<OpalVideoLayoutStrategy> OpalVideoMixer::CreateLayout(Styles style)
{
  switch (style) {
    case eSideBySideLetterbox:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalSideBySideLetterboxLayout);
    case eSideBySideScaled:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalSideBySideScaledLayout);
    case eStackedPillarbox:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalStackedPillarboxLayout);
    case eStackedScaled:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalStackedScaledLayout);
    case eActiveSpeaker:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalActiveSpeakerLayout);
    case eGrid:
    case eUser:
    default:
      return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalGridVideoLayout);
  }
}


///////////////////////////////////////////////////////////////////////////////

OpalTranscodeVideoStrategy::OpalTranscodeVideoStrategy(std::unique_ptr<OpalVideoLayoutStrategy> layout)
  : m_layout(std::move(layout))
{
  if (m_layout.get() == NULL)
    m_layout.reset(new OpalGridVideoLayout);
}


bool OpalTranscodeVideoStrategy::Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame)
{
  if (!mixer.MixVideo())
    return false;
  return mixer.CopyMixedFrame(frame);
}


static bool LoadStillImage(const PString & path, unsigned width, unsigned height, PBYTEArray & yuv)
{
  if (path.IsEmpty())
    return false;

#if P_VIDFILE
  PVideoDevice::OpenArgs args;
  args.driverName = P_VIDEO_FILE_DRIVER;
  args.deviceName = path;
  args.width = width;
  args.height = height;
  args.colourFormat = PVideoFrameInfo::YUV420P();
  args.convertFormat = true;
  args.convertSize = true;
  args.rate = 1;

  PAutoPtr<PVideoInputDevice> device(PVideoInputDevice::CreateOpenedDevice(args, true));
  if (device.get() == NULL) {
    PTRACE(2, "Could not open still image \"" << path << '"');
    return false;
  }

  PINDEX yuvBytes = PVideoFrameInfo::CalculateFrameBytes(width, height);
  PINDEX bytes = std::max(device->GetMaxFrameBytes(), yuvBytes);
  PBYTEArray raw(bytes);
  bool keyFrame = true;
  if (!device->GetFrameDataNoDelay(raw.GetPointer(), &bytes, keyFrame)) {
    PTRACE(2, "Could not read still image \"" << path << '"');
    return false;
  }

  if (device->GetColourFormat() == PVideoFrameInfo::YUV420P()) {
    yuv = raw;
    yuv.SetSize(yuvBytes);
    return true;
  }

  PAutoPtr<PColourConverter> converter(PColourConverter::Create(device->GetColourFormat(),
                                                               PVideoFrameInfo::YUV420P(),
                                                               width,
                                                               height));
  if (converter.get() == NULL) {
    PTRACE(2, "No conversion from " << device->GetColourFormat() << " for still image \"" << path << '"');
    return false;
  }

  yuv.SetSize(yuvBytes);
  PINDEX written = yuvBytes;
  if (!converter->Convert(raw.GetPointer(), yuv.GetPointer(), &written)) {
    PTRACE(2, "Could not convert still image \"" << path << '"');
    return false;
  }
  return true;
#else
  (void)width;
  (void)height;
  (void)yuv;
  PTRACE(2, "Still image \"" << path << "\" needs the video file device");
  return false;
#endif
}


OpalStillImageVideoStrategy::OpalStillImageVideoStrategy(const PString & image)
  : m_image(image)
{
}


bool OpalStillImageVideoStrategy::Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame)
{
  mixer.PaintStillFrame(m_frame);
  return mixer.CopyMixedFrame(frame);
}


void OpalStillImageVideoStrategy::Activate(OpalMixerNode &, OpalVideoMixer & mixer)
{
  if (m_frame.IsEmpty()) {
    PBYTEArray loaded;
    LoadStillImage(m_image, mixer.GetFrameWidth(), mixer.GetFrameHeight(), loaded);
    PWaitAndSignal lock(mixer.GetMutex());
    m_frame = loaded;
  }

  mixer.EnsurePushThread();
}


void OpalStillImageVideoStrategy::Deactivate(OpalMixerNode &, OpalVideoMixer & mixer)
{
  PWaitAndSignal lock(mixer.GetMutex());
  m_frame.SetSize(0);
}


bool OpalPairRelayVideoStrategy::Mix(OpalVideoMixer &, RTP_DataFrame & frame)
{
  frame.SetPayloadSize(0);
  return true;
}


void OpalPairRelayVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer &)
{
  bool waiting = false;
  if (node.TryVideoPairRelay(true, waiting))
    return;

  if (!waiting)
    node.SetForceTranscode(true);
}


void OpalPairRelayVideoStrategy::Deactivate(OpalMixerNode & node, OpalVideoMixer &)
{
  bool waiting = false;
  node.TryVideoPairRelay(false, waiting);
  node.RequestVideoKeyFrames();
}


bool OpalMultiStreamRelayStrategy::Mix(OpalVideoMixer &, RTP_DataFrame & frame)
{
  frame.SetPayloadSize(0);
  return true;
}


void OpalMultiStreamRelayStrategy::Activate(OpalMixerNode & node, OpalVideoMixer &)
{
  bool waiting = false;
  if (node.StartMultiStreamRelay(waiting))
    return;

  if (!waiting)
    node.SetForceTranscode(true);
}


void OpalMultiStreamRelayStrategy::Deactivate(OpalMixerNode & node, OpalVideoMixer &)
{
  node.StopMultiStreamRelay();
}


///////////////////////////////////////////////////////////////////////////////

OpalParticipantCountVideoStrategy::OpalParticipantCountVideoStrategy(
    const PString & stillImage,
    std::unique_ptr<OpalVideoLayoutStrategy> layout)
  : m_still(stillImage)
  , m_original(std::move(layout))
  , m_active(&m_original)
  , m_forceTranscode(false)
{
}


OpalVideoMixerStrategy & OpalParticipantCountVideoStrategy::Select(const OpalMixerNode & node)
{
  if (!m_forceTranscode) {
    PINDEX count = node.GetConnectionCount();
    if (count <= 1)
      return m_still;
    if (count == 2)
      return m_pair;
    if (count <= 4 && node.MultiStreamReady())
      return m_multi;
  }
  return m_original;
}


bool OpalParticipantCountVideoStrategy::Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame)
{
  return m_active->Mix(mixer, frame);
}


bool OpalParticipantCountVideoStrategy::WantsTranscode() const
{
  return m_active->WantsTranscode();
}


OpalVideoLayoutStrategy & OpalParticipantCountVideoStrategy::Layout()
{
  return m_active->Layout();
}


void OpalParticipantCountVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer & mixer)
{
  OpalVideoMixerStrategy * previous;
  OpalVideoMixerStrategy * next;
  {
    PWaitAndSignal lock(mixer.GetMutex());
    previous = m_active;
    next = &Select(node);
    m_active = next;
  }

  if (previous != next && previous != NULL)
    previous->Deactivate(node, mixer);
  next->Activate(node, mixer);
}


void OpalParticipantCountVideoStrategy::Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer)
{
  OpalVideoMixerStrategy * active;
  {
    PWaitAndSignal lock(mixer.GetMutex());
    active = m_active;
  }
  if (active != NULL)
    active->Deactivate(node, mixer);
}


void OpalParticipantCountVideoStrategy::SetForceTranscode(bool on)
{
  // Caller holds the mixer mutex.
  m_forceTranscode = on;
}


///////////////////////////////////////////////////////////////////////////////

static bool IsVideo(const OpalMediaStream & stream)
{
  return stream.GetMediaFormat().GetMediaType() == OpalMediaType::Video();
}


void OpalMixerNode::CloseVideoStreams(OpalConnection & connection)
{
  std::vector<OpalMediaStreamPtr> video;
  PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (IsVideo(*it))
      video.push_back(&*it);
  }

  for (std::vector<OpalMediaStreamPtr>::iterator it = video.begin(); it != video.end(); ++it)
    connection.CloseMediaStream(*it);
}


void OpalMixerNode::ReopenMixedVideo()
{
  struct Leg {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
  };
  std::vector<Leg> legs;

  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network != NULL) {
      Leg leg;
      leg.mixer = &connection;
      leg.network = network;
      legs.push_back(leg);
    }
  }

  for (std::vector<Leg>::iterator it = legs.begin(); it != legs.end(); ++it) {
    CloseVideoStreams(*it->mixer);
    CloseVideoStreams(*it->network);
  }

  for (std::vector<Leg>::iterator it = legs.begin(); it != legs.end(); ++it) {
    it->mixer->GetCall().OpenSourceMediaStreams(*it->network, OpalMediaType::Video());
    it->mixer->GetCall().OpenSourceMediaStreams(*it->mixer, OpalMediaType::Video());
  }
}


void OpalMixerNode::RenegotiateParticipants(bool separateBundle)
{
  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

#if OPAL_SDP
    OpalConnection::StringOptions options = network->GetStringOptions();
    options.SetBoolean(OPAL_OPT_SEPARATE_BUNDLE_STREAMS, separateBundle);
    network->SetStringOptions(options, true, false);
#else
    (void)separateBundle;
#endif

  }
}


void OpalMixerNode::RequestVideoKeyFrames()
{
  for (auto & connection : m_connections) {
    PSafeArray<OpalMediaStream> local = connection.GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = local.begin(); it != local.end(); ++it) {
      if (it->IsSource() && IsVideo(*it))
        it->ExecuteCommand(OpalVideoUpdatePicture());
    }

    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

    PSafeArray<OpalMediaStream> remote = network->GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = remote.begin(); it != remote.end(); ++it) {
      if (it->IsSource() && IsVideo(*it))
        it->ExecuteCommand(OpalVideoUpdatePicture());
    }
  }
}


static bool PairDirectionReady(OpalConnection & from,
                               OpalConnection & to,
                               unsigned sessionID,
                               bool & formatMismatch)
{
  OpalMediaStreamPtr source = from.GetMediaStream(sessionID, true);
  OpalMediaStreamPtr sink = to.GetMediaStream(sessionID, false);
  if (source == NULL || sink == NULL || source->GetPatch().GetObject() == NULL || sink->GetPatch().GetObject() == NULL)
    return false;

  if (source->GetMediaFormat() != sink->GetMediaFormat()) {
    formatMismatch = true;
    return false;
  }

  return true;
}


static void ClearVideoPassThrough(OpalMixerNode & node)
{
  std::vector<PSafePtr<OpalConnection>> networks;
  for (auto & connection : node.GetConnections()) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network != NULL)
      networks.push_back(network);
  }

  for (size_t i = 0; i < networks.size(); ++i) {
    for (size_t j = i + 1; j < networks.size(); ++j)
      OpalManager::SetMediaPassThrough(*networks[i], *networks[j], false, 0);
  }
}


bool OpalMixerNode::TryVideoPairRelay(bool enable, bool & waiting)
{
  waiting = false;

  if (!enable) {
    ClearVideoPassThrough(*this);
    return true;
  }

  if (m_connections.GetSize() != 2) {
    waiting = true;
    return false;
  }

  PSafePtr<OpalMixerConnection> first = m_connections.GetAt(0, PSafeReference);
  PSafePtr<OpalMixerConnection> second = m_connections.GetAt(1, PSafeReference);
  if (first == NULL || second == NULL) {
    waiting = enable;
    return !enable;
  }

  PSafePtr<OpalConnection> network1 = first->GetOtherPartyConnection();
  PSafePtr<OpalConnection> network2 = second->GetOtherPartyConnection();
  if (network1 == NULL || network2 == NULL) {
    waiting = enable;
    return !enable;
  }

  bool sawVideo = false;
  bool pending = false;
  bool mismatched = false;
  std::vector<unsigned> sessions;

  PSafeArray<OpalMediaStream> streams = network1->GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (!it->IsSource() || !IsVideo(*it))
      continue;

    sawVideo = true;
    unsigned sessionID = it->GetSessionID();
    sessions.push_back(sessionID);

    bool formatMismatch = false;
    bool forward = PairDirectionReady(*network1, *network2, sessionID, formatMismatch);
    bool reverse = PairDirectionReady(*network2, *network1, sessionID, formatMismatch);
    if (formatMismatch)
      mismatched = true;
    else if (!forward || !reverse)
      pending = true;
  }

  if (!sawVideo || pending) {
    waiting = true;
    return false;
  }

  if (mismatched) {
    PTRACE(3, "Pair video formats differ, falling back to a transcoded mix on " << *this);
    return false;
  }

  for (std::vector<unsigned>::iterator it = sessions.begin(); it != sessions.end(); ++it) {
    if (OpalManager::SetMediaPassThrough(*network1, *network2, true, *it))
      continue;

    PTRACE(2, "Pair video relay could not bypass session " << *it << " on " << *this);
    for (std::vector<unsigned>::iterator undo = sessions.begin(); undo != sessions.end(); ++undo)
      OpalManager::SetMediaPassThrough(*network1, *network2, false, *undo);
    return false;
  }

  PTRACE(3, "Relaying video between two participants on " << *this);
  return true;
}


static void CloseVideoDirection(OpalConnection & connection, bool source)
{
  std::vector<OpalMediaStreamPtr> video;
  PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (IsVideo(*it) && it->IsSource() == source)
      video.push_back(&*it);
  }
  for (std::vector<OpalMediaStreamPtr>::iterator it = video.begin(); it != video.end(); ++it)
    connection.CloseMediaStream(*it);
}


static unsigned MixerVideoSourceCount(OpalConnection & mixer)
{
  unsigned count = 0;
  PSafeArray<OpalMediaStream> streams = mixer.GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (it->IsSource() && IsVideo(*it))
      ++count;
  }
  return count;
}


static unsigned VideoSinkCount(OpalConnection & connection)
{
  unsigned count = 0;
  PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (!it->IsSource() && IsVideo(*it))
      ++count;
  }
  return count;
}


static bool ParticipantsHaveRelaySinks(const OpalMixerNode & node)
{
  PINDEX connectionCount = node.GetConnectionCount();
  if (connectionCount < 3 || connectionCount > 4)
    return false;

  unsigned needed = (unsigned)connectionCount - 1;
  unsigned ready = 0;
  for (const auto & connection : node.GetConnections()) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL || network->GetMediaStream(OpalMediaType::Video(), true) == NULL)
      continue;
    if (VideoSinkCount(*network) < needed)
      return false;
    ++ready;
  }
  return ready == (unsigned)connectionCount;
}


bool OpalMixerNode::MultiStreamReady() const
{
  return m_relayInProgress.load() || ParticipantsHaveRelaySinks(*this);
}


bool OpalMixerNode::StartMultiStreamRelay(bool & waiting)
{
  waiting = false;

  if (m_relayInProgress.load())
    return true;

  if (!ParticipantsHaveRelaySinks(*this)) {
    waiting = m_connections.GetSize() <= 4;
    return false;
  }

  struct Party {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
    OpalMediaFormat format;
    PString sinkId;
  };
  std::vector<Party> parties;

  PINDEX connectionCount = m_connections.GetSize();
  if (connectionCount < 3 || connectionCount > 4) {
    waiting = connectionCount < 3;
    return false;
  }

  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

    OpalMediaStreamPtr video = network->GetMediaStream(OpalMediaType::Video(), true);
    if (video == NULL)
      continue;

    Party party;
    party.mixer = &connection;
    party.network = network;
    party.format = video->GetMediaFormat();
    parties.push_back(party);
  }

  if (parties.size() < 3) {
    waiting = true;
    return false;
  }

  if (parties.size() > 4)
    return false;

  for (size_t i = 1; i < parties.size(); ++i) {
    if (parties[i].format != parties[0].format) {
      PTRACE(3, "Video formats differ, falling back to a transcoded mix on " << *this);
      return false;
    }
  }

  if (m_relayEncodedVideo.load() && m_relayPartyCount == parties.size())
    return true;

  unsigned needed = (unsigned)parties.size() - 1;
  for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
    if (MixerVideoSourceCount(*it->mixer) < needed) {
      // The extra m-lines are still sharing the first video session, so only
      // the mixed picture has a stream. The next offer separates them.
      waiting = true;
      return false;
    }
  }

  if (m_relayInProgress.load() || m_relayApplyPending.exchange(true))
    return true;

  m_manager.GetManager().QueueDecoupledEvent(
    new PSafeWorkNoArg<OpalMixerNode, bool>(this, &OpalMixerNode::ApplyMultiStreamRelay));
  return true;
}


bool OpalMixerNode::ApplyMultiStreamRelay()
{
  m_relayApplyPending = false;
  if (m_shuttingDown)
    return true;

  if (m_relayInProgress.exchange(true))
    return true;

  struct InProgress
  {
    atomic<bool> & flag;
    ~InProgress() { flag = false; }
  } inProgress{m_relayInProgress};

  bool waiting = false;
  if (!ArmEncodedRelay(waiting) && !waiting)
    SetForceTranscode(true);
  return true;
}


bool OpalMixerNode::ArmEncodedRelay(bool & waiting)
{
  waiting = false;

  struct Party {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
    OpalMediaFormat format;
    unsigned sessionID;
    PString sinkId;
  };
  std::vector<Party> parties;

  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

    OpalMediaStreamPtr video = network->GetMediaStream(OpalMediaType::Video(), true);
    if (video == NULL)
      continue;

    Party party;
    party.mixer = &connection;
    party.network = network;
    party.format = video->GetMediaFormat();
    party.sessionID = video->GetSessionID();
    parties.push_back(party);
  }

  if (parties.size() < 3 || parties.size() > 4) {
    waiting = parties.size() < 3;
    return false;
  }

  for (size_t i = 1; i < parties.size(); ++i) {
    if (parties[i].format != parties[0].format) {
      PTRACE(3, "Video formats differ, falling back to a transcoded mix on " << *this);
      return false;
    }
  }

  unsigned needed = (unsigned)parties.size() - 1;
  for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
    if (MixerVideoSourceCount(*it->mixer) < needed) {
      waiting = true;
      return false;
    }
  }

  // Leave the streams the browsers are already receiving. Only the path that
  // carries each camera into the mixer has to be reopened without a transcoder.
  m_relayEncodedVideo = true;
  m_relayPartyCount = 0;
  {
    PWaitAndSignal lock(m_relayMutex);
    m_relaySourceOf.clear();
  }

  for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
    CloseVideoDirection(*it->network, true);
    CloseVideoDirection(*it->mixer, false);
  }

  for (std::vector<Party>::iterator sender = parties.begin(); sender != parties.end(); ++sender) {
    if (sender->mixer->GetCall().OpenSourceMediaStreams(*sender->network,
                                                       OpalMediaType::Video(),
                                                       sender->sessionID,
                                                       sender->format)) {
      OpalMediaStreamPtr sink = sender->mixer->GetMediaStream(OpalMediaType::Video(), false);
      if (sink != NULL) {
        sender->sinkId = sink->GetID();
        continue;
      }
    }

    PTRACE(2, "Could not reopen encoded video from " << *sender->network);
    m_relayEncodedVideo = false;
    m_relayPartyCount = 0;
    for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
      CloseVideoDirection(*it->network, true);
      CloseVideoDirection(*it->mixer, false);
      it->mixer->GetCall().OpenSourceMediaStreams(*it->network,
                                                 OpalMediaType::Video(),
                                                 it->sessionID,
                                                 it->format);
    }
    waiting = true;
    return false;
  }

  for (std::vector<Party>::iterator receiver = parties.begin(); receiver != parties.end(); ++receiver) {
    std::vector<PString> outputs;
    PSafeArray<OpalMediaStream> streams = receiver->mixer->GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
      if (it->IsSource() && IsVideo(*it))
        outputs.push_back(it->GetID());
    }

    size_t index = 0;
    for (std::vector<Party>::iterator sender = parties.begin(); sender != parties.end(); ++sender) {
      if (&*sender->mixer == &*receiver->mixer)
        continue;
      if (index >= outputs.size() || sender->sinkId.IsEmpty()) {
        PTRACE(2, "Relay video stream towards " << *receiver->network << " was not created");
        m_relayEncodedVideo = false;
        m_relayPartyCount = 0;
        waiting = true;
        return false;
      }

      PWaitAndSignal lock(m_relayMutex);
      m_relaySourceOf[outputs[index]] = sender->sinkId;
      ++index;
    }
  }

  m_relayPartyCount = (unsigned)parties.size();
  RequestVideoKeyFrames();
  PTRACE(3, "Relaying encoded video between " << m_relayPartyCount << " participants on " << *this);
  return true;
}


void OpalMixerNode::StopMultiStreamRelay()
{
  if (!m_relayEncodedVideo.load() && m_relayPartyCount == 0)
    return;

  m_relayEncodedVideo = false;
  m_relayPartyCount = 0;
  {
    PWaitAndSignal lock(m_relayMutex);
    m_relaySourceOf.clear();
  }

  RequestVideoKeyFrames();
}


void OpalMixerNode::ForwardEncodedVideo(const OpalMixerMediaStream & from, const RTP_DataFrame & packet)
{
  PWaitAndSignal lock(m_relayMutex);
  const OpalConnection & sender = from.GetConnection();

  for (auto & connection : m_connections) {
    if (&connection == &sender)
      continue;

    PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
      if (!it->IsSource() || !IsVideo(*it))
        continue;

      std::map<PString, PString>::iterator map = m_relaySourceOf.find(it->GetID());
      if (map == m_relaySourceOf.end() || map->second != from.GetID())
        continue;

      if (it->GetMediaFormat() != from.GetMediaFormat())
        continue;

      RTP_DataFrame copy;
      copy.Copy(packet);
      it->PushPacket(copy);
    }
  }
}


void OpalMixerNode::SetForceTranscode(bool on)
{
  for (VideoMixerMap::iterator it = m_videoMixers.begin(); it != m_videoMixers.end(); ++it)
    it->second->SetForceTranscode(on);

  if (on)
    m_deliveryDirty = true;
}


void OpalMixerNode::UpdateVideoDelivery(bool force)
{
  if (m_shuttingDown)
    return;

  if (force)
    m_deliveryDirty = true;

  bool expected = false;
  if (!m_applyingDelivery.compare_exchange_strong(expected, true)) {
    m_deliveryDirty = true;
    return;
  }

  do {
    bool dirty = m_deliveryDirty.exchange(false);

    struct Work {
      OpalVideoMixer * mixer;
      OpalVideoMixerStrategy * previous;
      OpalVideoMixerStrategy * next;
      bool changed;
    };
    std::vector<Work> work;

    for (VideoMixerMap::iterator it = m_videoMixers.begin(); it != m_videoMixers.end(); ++it) {
      Work item;
      item.mixer = it->second.get();
      item.previous = NULL;
      item.next = NULL;
      item.changed = item.mixer->ExchangeStrategy(item.previous, item.next);
      if (item.changed || dirty)
        work.push_back(item);
    }

    if (work.empty())
      break;

    for (std::vector<Work>::iterator it = work.begin(); it != work.end(); ++it) {
      if (it->previous != it->next && it->previous != NULL)
        it->previous->Deactivate(*this, *it->mixer);
      if (it->next != NULL)
        it->next->Activate(*this, *it->mixer);
    }
  } while (m_deliveryDirty.load());

  m_applyingDelivery = false;
}

#endif // OPAL_VIDEO

#endif // OPAL_HAS_MIXER
