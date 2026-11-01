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
#include <climits>
#include <set>
#include <ptclib/threadpool.h>

#if OPAL_VIDEO
#include <ptlib/videoio.h>
#include <ptlib/vconvert.h>
#endif

#if OPAL_SDP
#include <sdp/sdp.h>
#include <sdp/sdpep.h>
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


void OpalStillImageVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer & mixer)
{
  node.RenegotiateParticipants(false);

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


unsigned OpalVideoMixerStrategy::GetExpectedReceiveVideoStreams(const OpalMixerNode &) const
{
  return 1;
}


void OpalOriginalVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer &)
{
  node.RenegotiateParticipants(false);
}


bool OpalPairRelayVideoStrategy::Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame)
{
  OpalVideoStreamMixer * streams = dynamic_cast<OpalVideoStreamMixer *>(&mixer);
  // RTP pass-through or a 3→2 encoded shrink both own the outbound m-lines.
  if (streams != NULL &&
      (streams->GetMixerNode().PairRelayActive() ||
       streams->GetMixerNode().EncodedRelayActive())) {
    frame.SetPayloadSize(0);
    return true;
  }

  if (!mixer.MixVideo())
    return false;
  return mixer.CopyMixedFrame(frame);
}


void OpalPairRelayVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer &)
{
  // Pair mode uses a single video m-line on each side.
  node.RenegotiateParticipants(false);

  // 3→2 already remapped encoded SFU onto the primary m-lines. Do not tear
  // that down in favour of RTP pass-through — that path fails after Arm.
  if (node.EncodedRelayActive() && node.GetConnectionCount() == 2) {
    node.RequestVideoKeyFrames();
    return;
  }

  bool waiting = false;
  if (node.TryVideoPairRelay(true, waiting)) {
    node.RequestVideoKeyFrames();
    return;
  }

  node.RequestVideoKeyFrames();
  if (waiting) {
    node.ScheduleVideoDeliveryRetry();
    return;
  }

  node.SetForceTranscode(true);
}


void OpalPairRelayVideoStrategy::Deactivate(OpalMixerNode & node, OpalVideoMixer &)
{
  bool waiting = false;
  node.TryVideoPairRelay(false, waiting);
  // A prior 3→2 shrink may still be feeding encoded relay on the pair. Clear
  // it before multi/original takes over so Arm can reopen cleanly.
  if (node.RelayEncodedVideo())
    node.StopMultiStreamRelay();
  node.RequestVideoKeyFrames();
}


bool OpalMultiStreamRelayStrategy::Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame)
{
  OpalVideoStreamMixer * streams = dynamic_cast<OpalVideoStreamMixer *>(&mixer);
  // Between pair teardown and SFU arm, do not paint a transcoded mix — that is
  // the flash on 2→3. Hold the last frame until EncodedRelayActive.
  if (streams != NULL && !streams->GetMixerNode().EncodedRelayActive()) {
    frame.SetPayloadSize(0);
    return true;
  }

  if (streams != NULL && streams->GetMixerNode().RelayEncodedVideo()) {
    frame.SetPayloadSize(0);
    return true;
  }

  if (!mixer.MixVideo())
    return false;
  return mixer.CopyMixedFrame(frame);
}


void OpalMultiStreamRelayStrategy::Activate(OpalMixerNode & node, OpalVideoMixer &)
{
  // Ask for one RTP session per video m-line only after this mode is selected.
  node.RenegotiateParticipants(true);

  bool waiting = false;
  if (node.StartMultiStreamRelay(waiting))
    return;

  if (!waiting)
    node.SetForceTranscode(true);
}


void OpalMultiStreamRelayStrategy::Deactivate(OpalMixerNode & node, OpalVideoMixer &)
{
  // Prefer keeping encoded camera paths and remapping 1:1. Reopening as YUV
  // and switching to RTP pass-through leaves the remaining window frozen.
  if (node.GetConnectionCount() == 2 && node.RelayEncodedVideo() && node.ShrinkEncodedRelayToPair()) {
    node.RenegotiateParticipants(false);
    node.RequestVideoKeyFrames();
    return;
  }

  node.StopMultiStreamRelay();
  node.RenegotiateParticipants(false);
  node.RequestVideoKeyFrames();
}


///////////////////////////////////////////////////////////////////////////////

OpalParticipantCountVideoStrategy::OpalParticipantCountVideoStrategy(
    const PString & stillImage,
    std::unique_ptr<OpalVideoLayoutStrategy> layout,
    unsigned multiMax)
  : m_still(stillImage)
  , m_original(std::move(layout))
  , m_active(&m_original)
  , m_forceTranscode(false)
  , m_multiMax(multiMax < 3 ? 3 : multiMax)
  , m_transitioning(false)
  , m_reenterDelivery(false)
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
    if (count >= 3 && (unsigned)count <= m_multiMax)
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


unsigned OpalParticipantCountVideoStrategy::GetExpectedReceiveVideoStreams(const OpalMixerNode & node) const
{
  if (m_active == &m_multi) {
    PINDEX count = node.GetConnectionCount();
    if (count >= 3)
      return (unsigned)count - 1;
  }
  return 1;
}


void OpalParticipantCountVideoStrategy::Activate(OpalMixerNode & node, OpalVideoMixer & mixer)
{
  // One mode must finish leaving before the next mode starts. Nested delivery
  // updates wait for the current transition to complete, then run again.
  if (m_transitioning.exchange(true)) {
    m_reenterDelivery = true;
    return;
  }

  for (;;) {
    OpalVideoMixerStrategy * previous;
    OpalVideoMixerStrategy * next;
    {
      PWaitAndSignal lock(mixer.GetMutex());
      previous = m_active;
      next = &Select(node);
    }

    if (previous != next) {
      if (previous != NULL)
        previous->Deactivate(node, mixer);
      {
        PWaitAndSignal lock(mixer.GetMutex());
        m_active = next;
      }
    }

    if (next != NULL)
      next->Activate(node, mixer);

    OpalVideoMixerStrategy * again;
    {
      PWaitAndSignal lock(mixer.GetMutex());
      again = &Select(node);
      if (again == m_active)
        break;
    }
    // Participant count changed while the previous mode was finalising.
  }

  m_transitioning = false;
  if (m_reenterDelivery.exchange(false))
    node.UpdateVideoDelivery(true);
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


/**First video stream by session id. StreamDict is keyed by object address, so
   GetMediaStream(type) is allocation-order dependent and nondeterministic.
  */
static OpalMediaStreamPtr LowestSessionVideo(OpalConnection & connection, bool source)
{
  OpalMediaStreamPtr best;
  unsigned bestSession = UINT_MAX;
  PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
  for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
    if (!IsVideo(*it) || it->IsSource() != source)
      continue;
    unsigned session = it->GetSessionID();
    if (best == NULL || session < bestSession) {
      best = &*it;
      bestSession = session;
    }
  }
  return best;
}


static bool SameVideoCodec(const OpalMediaFormat & a, const OpalMediaFormat & b)
{
  return a.GetMediaType() == OpalMediaType::Video() &&
         b.GetMediaType() == OpalMediaType::Video() &&
         a.GetName() == b.GetName();
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
    // Every m-line stays in one BUNDLE group, so extra video sessions reuse
    // the tagged m-line's ports instead of opening a socket of their own.
    OpalConnection::StringOptions options = network->GetStringOptions();
    options.SetBoolean(OPAL_OPT_AV_BUNDLE, true);
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
      if (IsVideo(*it))
        it->ExecuteCommand(OpalVideoUpdatePicture());
    }

    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

    // Prefer the camera (lowest session source) so PLI/FIR reaches each
    // participant. Also walk the other video streams for SFU/bundle cases.
    OpalMediaStreamPtr camera = LowestSessionVideo(*network, true);
    if (camera != NULL)
      camera->ExecuteCommand(OpalVideoUpdatePicture());

    PSafeArray<OpalMediaStream> remote = network->GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = remote.begin(); it != remote.end(); ++it) {
      if (IsVideo(*it) && (camera == NULL || it->GetID() != camera->GetID()))
        it->ExecuteCommand(OpalVideoUpdatePicture());
    }
  }
}


void OpalMixerNode::RequestRelayKeyFrame(PString outputStreamId)
{
  PString sinkId;
  {
    PWaitAndSignal lock(m_relayMutex);
    std::map<PString, PString>::iterator map = m_relaySourceOf.find(outputStreamId);
    if (map == m_relaySourceOf.end())
      return;
    sinkId = map->second;
  }

  for (auto & connection : m_connections) {
    PSafeArray<OpalMediaStream> streams = connection.GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
      if (it->GetID() != sinkId)
        continue;

      // Mixer sink receives from the camera. UpdatePicture walks the patch to
      // the network source and sends PLI/FIR to that participant.
      it->ExecuteCommand(OpalVideoUpdatePicture());

      PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
      if (network != NULL) {
        OpalMediaStreamPtr camera = LowestSessionVideo(*network, true);
        if (camera != NULL)
          camera->ExecuteCommand(OpalVideoUpdatePicture());
      }
      return;
    }
  }
}


static bool PairVideoDirection(OpalConnection & from,
                               OpalConnection & to,
                               bool enable,
                               bool & waiting,
                               bool & formatMismatch)
{
  // Match by media type. Session ids are local to each connection and need
  // not be the same number on both sides of the conference. Always take the
  // lowest session so pair mode does not depend on StreamDict pointer order.
  OpalMediaStreamPtr source = LowestSessionVideo(from, true);
  OpalMediaStreamPtr sink = LowestSessionVideo(to, false);
  if (source == NULL || sink == NULL ||
      source->GetPatch().GetObject() == NULL || sink->GetPatch().GetObject() == NULL) {
    waiting = true;
    return false;
  }

  if (!SameVideoCodec(source->GetMediaFormat(), sink->GetMediaFormat())) {
    formatMismatch = true;
    return false;
  }

  // SetMediaPassThrough compares full format objects. After SFU the options on
  // the camera and the outbound m-line often differ even for the same codec.
  if (enable && source->GetMediaFormat() != sink->GetMediaFormat())
    sink->UpdateMediaFormat(source->GetMediaFormat());

  return source->SetMediaPassThrough(*sink, enable);
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
    for (size_t j = i + 1; j < networks.size(); ++j) {
      bool waiting = false;
      bool mismatch = false;
      PairVideoDirection(*networks[i], *networks[j], false, waiting, mismatch);
      PairVideoDirection(*networks[j], *networks[i], false, waiting, mismatch);
    }
  }
}


bool OpalMixerNode::TryVideoPairRelay(bool enable, bool & waiting)
{
  waiting = false;

  if (!enable) {
    m_pairRelayActive = false;
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
    waiting = true;
    return false;
  }

  PSafePtr<OpalConnection> network1 = first->GetOtherPartyConnection();
  PSafePtr<OpalConnection> network2 = second->GetOtherPartyConnection();
  if (network1 == NULL || network2 == NULL) {
    waiting = true;
    return false;
  }

  bool formatMismatch = false;
  bool forwardWaiting = false;
  bool reverseWaiting = false;
  bool forward = PairVideoDirection(*network1, *network2, true, forwardWaiting, formatMismatch);
  bool reverse = PairVideoDirection(*network2, *network1, true, reverseWaiting, formatMismatch);

  if (formatMismatch) {
    bool unusedWaiting = false;
    bool unusedMismatch = false;
    PairVideoDirection(*network1, *network2, false, unusedWaiting, unusedMismatch);
    PairVideoDirection(*network2, *network1, false, unusedWaiting, unusedMismatch);
    PTRACE(3, "Pair video formats differ, falling back to a transcoded mix on " << *this);
    return false;
  }

  // Both directions must succeed. SetMediaPassThrough used to return true when
  // only one side was ready, which left one participant without video.
  if (!forward || !reverse) {
    bool unusedWaiting = false;
    bool unusedMismatch = false;
    PairVideoDirection(*network1, *network2, false, unusedWaiting, unusedMismatch);
    PairVideoDirection(*network2, *network1, false, unusedWaiting, unusedMismatch);
    waiting = forwardWaiting || reverseWaiting || !forward || !reverse;
    return false;
  }

  m_pairRelayActive = true;
  // Both directions just switched to RTP pass-through; force an intra from
  // each camera so receivers are not left on a mid-GOP stream from SFU mode.
  RequestVideoKeyFrames();
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
  if (connectionCount < 3)
    return false;

  unsigned needed = (unsigned)connectionCount - 1;
  unsigned ready = 0;
  for (const auto & connection : node.GetConnections()) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL || LowestSessionVideo(*network, true) == NULL)
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


bool OpalMixerNode::RetryMultiStreamRelay()
{
  PThread::Sleep(200);
  m_multiStreamRetryPending = false;
  if (!m_shuttingDown)
    UpdateVideoDelivery(true);
  return true;
}


void OpalMixerNode::ScheduleVideoDeliveryRetry()
{
  if (m_shuttingDown)
    return;
  if (!m_multiStreamRetryPending.exchange(true)) {
    m_manager.GetManager().QueueDecoupledEvent(
      new PSafeWorkNoArg<OpalMixerNode, bool>(this, &OpalMixerNode::RetryMultiStreamRelay));
  }
}


bool OpalMixerNode::StartMultiStreamRelay(bool & waiting)
{
  waiting = false;

  if (m_relayInProgress.load())
    return true;

  if (m_relayEncodedVideo.load() && m_relayPartyCount == (unsigned)m_connections.GetSize())
    return true;

  if (!ParticipantsHaveRelaySinks(*this)) {
    waiting = m_connections.GetSize() >= 3;
    if (waiting && !m_multiStreamRetryPending.exchange(true)) {
      m_manager.GetManager().QueueDecoupledEvent(
        new PSafeWorkNoArg<OpalMixerNode, bool>(this, &OpalMixerNode::RetryMultiStreamRelay));
    }
    return false;
  }

  struct Party {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
    OpalMediaFormat format;
    PString sinkId;
  };
  std::vector<Party> parties;

  if (m_connections.GetSize() < 3) {
    waiting = true;
    return false;
  }

  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;

    OpalMediaStreamPtr video = LowestSessionVideo(*network, true);
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

  for (size_t i = 1; i < parties.size(); ++i) {
    if (!SameVideoCodec(parties[i].format, parties[0].format)) {
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
      if (!m_multiStreamRetryPending.exchange(true)) {
        m_manager.GetManager().QueueDecoupledEvent(
          new PSafeWorkNoArg<OpalMixerNode, bool>(this, &OpalMixerNode::RetryMultiStreamRelay));
      }
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
  else if (waiting && !m_multiStreamRetryPending.exchange(true)) {
    m_manager.GetManager().QueueDecoupledEvent(
      new PSafeWorkNoArg<OpalMixerNode, bool>(this, &OpalMixerNode::RetryMultiStreamRelay));
  }
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

    OpalMediaStreamPtr video = LowestSessionVideo(*network, true);
    if (video == NULL)
      continue;

    Party party;
    party.mixer = &connection;
    party.network = network;
    party.format = video->GetMediaFormat();
    party.sessionID = video->GetSessionID();
    parties.push_back(party);
  }

  if (parties.size() < 3) {
    waiting = true;
    return false;
  }

  for (size_t i = 1; i < parties.size(); ++i) {
    if (!SameVideoCodec(parties[i].format, parties[0].format)) {
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

  // Stable mapping: same join order → same m-line assignment for every arm.
  std::sort(parties.begin(), parties.end(),
            [](const Party & a, const Party & b) { return a.mixer->GetToken() < b.mixer->GetToken(); });

  // Leave the streams the browsers are already receiving. Only the path that
  // carries each camera into the mixer has to be reopened without a transcoder.
  // Encoded relay and the mix cannot run together: reopening drops YUV input.
  m_relayEncodedVideo = true;
  m_relayPartyCount = 0;
  m_relayAwaitingKeyFrame = false;
  {
    PWaitAndSignal lock(m_relayMutex);
    m_relaySourceOf.clear();
    m_relayStreamsFed.clear();
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
      // Bind by the camera's session id. GetMediaStream(Video) is pointer-ordered.
      OpalMediaStreamPtr sink = sender->mixer->GetMediaStream(sender->sessionID, false);
      if (sink == NULL)
        sink = LowestSessionVideo(*sender->mixer, false);
      if (sink != NULL) {
        sender->sinkId = sink->GetID();
        continue;
      }
    }

    PTRACE(2, "Could not reopen encoded video from " << *sender->network);
    m_relayEncodedVideo = false;
    m_relayPartyCount = 0;
    m_relayAwaitingKeyFrame = false;
    {
      PWaitAndSignal lock(m_relayMutex);
      m_relayStreamsFed.clear();
    }
    for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
      CloseVideoDirection(*it->network, true);
      CloseVideoDirection(*it->mixer, false);
      it->mixer->GetCall().OpenSourceMediaStreams(*it->network,
                                                 OpalMediaType::Video(),
                                                 it->sessionID,
                                                 it->format);
      // OpenSourceMediaStreams builds the patch but does not start it.
      it->network->StartMediaStreams();
      it->mixer->StartMediaStreams();
    }
    waiting = true;
    return false;
  }

  for (std::vector<Party>::iterator receiver = parties.begin(); receiver != parties.end(); ++receiver) {
    std::vector<std::pair<unsigned, PString> > outputs;
    PSafeArray<OpalMediaStream> streams = receiver->mixer->GetMediaStreams();
    for (PSafeArray<OpalMediaStream>::iterator it = streams.begin(); it != streams.end(); ++it) {
      if (it->IsSource() && IsVideo(*it))
        outputs.push_back(std::make_pair(it->GetSessionID(), it->GetID()));
    }
    std::sort(outputs.begin(), outputs.end());

    size_t index = 0;
    for (std::vector<Party>::iterator sender = parties.begin(); sender != parties.end(); ++sender) {
      if (&*sender->mixer == &*receiver->mixer)
        continue;
      if (index >= outputs.size() || sender->sinkId.IsEmpty()) {
        PTRACE(2, "Relay video stream towards " << *receiver->network << " was not created");
        m_relayEncodedVideo = false;
        m_relayPartyCount = 0;
        m_relayAwaitingKeyFrame = false;
        {
          PWaitAndSignal lock(m_relayMutex);
          m_relayStreamsFed.clear();
        }
        waiting = true;
        return false;
      }

      PWaitAndSignal lock(m_relayMutex);
      m_relaySourceOf[outputs[index].second] = sender->sinkId;
      ++index;
    }
  }

  // Patches created by OpenSourceMediaStreams are idle until StartMediaStreams.
  // Without this, cameras never feed ForwardEncodedVideo and every SFU m-line
  // stays black after the mix stops.
  for (std::vector<Party>::iterator it = parties.begin(); it != parties.end(); ++it) {
    it->network->StartMediaStreams();
    it->mixer->StartMediaStreams();
  }

  m_relayPartyCount = (unsigned)parties.size();
  m_relayKeyFrameTimer.SetInterval(500);
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
  m_relayAwaitingKeyFrame = false;
  {
    PWaitAndSignal lock(m_relayMutex);
    m_relaySourceOf.clear();
    m_relayStreamsFed.clear();
  }

  // Arm left camera→mixer paths encoded-only. Pair pass-through and a YUV mix
  // both need those patches rebuilt with RelayEncodedVideo clear so sinks open
  // as YUV again. Leave the browser-facing mixer sources alone.
  struct Leg {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
    unsigned sessionID;
    OpalMediaFormat format;
  };
  std::vector<Leg> legs;
  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      continue;
    OpalMediaStreamPtr camera = LowestSessionVideo(*network, true);
    if (camera == NULL)
      continue;
    Leg leg;
    leg.mixer = &connection;
    leg.network = network;
    leg.sessionID = camera->GetSessionID();
    leg.format = camera->GetMediaFormat();
    legs.push_back(leg);
  }

  for (std::vector<Leg>::iterator it = legs.begin(); it != legs.end(); ++it) {
    CloseVideoDirection(*it->network, true);
    CloseVideoDirection(*it->mixer, false);
  }

  for (std::vector<Leg>::iterator it = legs.begin(); it != legs.end(); ++it) {
    it->mixer->GetCall().OpenSourceMediaStreams(*it->network,
                                               OpalMediaType::Video(),
                                               it->sessionID,
                                               it->format);
    it->network->StartMediaStreams();
    it->mixer->StartMediaStreams();
  }

  RequestVideoKeyFrames();
}


bool OpalMixerNode::ShrinkEncodedRelayToPair()
{
  if (!m_relayEncodedVideo.load() || m_connections.GetSize() != 2)
    return false;

  struct Party {
    PSafePtr<OpalMixerConnection> mixer;
    PSafePtr<OpalConnection> network;
    PString sinkId;
    PString primaryOutId;
  };
  std::vector<Party> parties;

  for (auto & connection : m_connections) {
    PSafePtr<OpalConnection> network = connection.GetOtherPartyConnection();
    if (network == NULL)
      return false;

    OpalMediaStreamPtr sink = LowestSessionVideo(connection, false);
    OpalMediaStreamPtr primary = LowestSessionVideo(connection, true);
    if (sink == NULL || primary == NULL)
      return false;

    Party party;
    party.mixer = &connection;
    party.network = network;
    party.sinkId = sink->GetID();
    party.primaryOutId = primary->GetID();
    parties.push_back(party);
  }

  if (parties.size() != 2)
    return false;

  {
    PWaitAndSignal lock(m_relayMutex);
    m_relaySourceOf.clear();
    m_relayStreamsFed.clear();
    // A sees B's camera on the primary m-line, and B sees A's.
    m_relaySourceOf[parties[0].primaryOutId] = parties[1].sinkId;
    m_relaySourceOf[parties[1].primaryOutId] = parties[0].sinkId;
  }

  m_relayPartyCount = 2;
  m_relayAwaitingKeyFrame = false;
  m_relayKeyFrameTimer.SetInterval(500);
  PTRACE(3, "Shrunk encoded relay to a pair on " << *this);
  return true;
}


void OpalMixerNode::ForwardEncodedVideo(const OpalMixerMediaStream & from, const RTP_DataFrame & packet)
{
  struct Target {
    OpalMediaStreamPtr stream;
    PString id;
    bool awaitingKeyFrame;
  };
  std::vector<Target> targets;
  bool needKeyFrameRequest = false;

  OpalVideoFormat format(from.GetMediaFormat());
  bool keyFrame = format.IsValid() &&
                  format.GetFrameType(packet.GetPayloadPtr(),
                                      packet.GetPayloadSize(),
                                      m_relayFrameDetector) == OpalVideoFormat::e_IntraFrame;

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

        // Match codec family only. Payload type / option diffs must not drop
        // the only path that feeds SFU outputs after the mix stops.
        if (it->GetMediaFormat().GetName() != from.GetMediaFormat().GetName())
          continue;

        Target target;
        target.stream = &*it;
        target.id = it->GetID();
        target.awaitingKeyFrame = m_relayStreamsFed.find(target.id) == m_relayStreamsFed.end();
        if (target.awaitingKeyFrame && !keyFrame) {
          needKeyFrameRequest = true;
          continue;
        }
        targets.push_back(target);
      }
    }

    // Rate-limit PLI storms while any output is still waiting for an intra.
    if (needKeyFrameRequest && m_relayKeyFrameTimer.HasExpired())
      m_relayKeyFrameTimer.SetInterval(500);
    else
      needKeyFrameRequest = false;
  }

  for (std::vector<Target>::iterator it = targets.begin(); it != targets.end(); ++it) {
    RTP_DataFrame copy;
    copy.Copy(packet);
    if (it->stream->PushPacket(copy) && keyFrame) {
      PWaitAndSignal lock(m_relayMutex);
      m_relayStreamsFed.insert(it->id);
    }
  }

  if (needKeyFrameRequest)
    RequestVideoKeyFrames();
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
