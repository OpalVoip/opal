/*
 * recording.cxx
 *
 * OPAL call recording
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (C) 2009 Post Increment
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

#include <opal_config.h>

#if defined(OPAL_HAS_MIXER) && defined(P_WAVFILE)

#include <opal/recording.h>

#include <ep/opalmixer.h>
#include <ptclib/mediafile.h>


#define PTraceModule() "OpalRecord"


//////////////////////////////////////////////////////////////////////////////

bool OpalRecordManager::Open(const PFilePath & fn)
{
  return OpenFile(fn);
}


bool OpalRecordManager::Open(const PFilePath & fn, bool mono)
{
  m_options.m_stereo = !mono;
  return OpenFile(fn);
}


bool OpalRecordManager::Open(const PFilePath & fn, const Options & options)
{
  m_options = options;
  return Open(fn);
}


bool OpalRecordManager::OpenFile(const PFilePath & fn)
{
  if (IsOpen())
    Close();

  m_filename = fn;
  m_audioPushTime.SetTimestamp(0);
#if OPAL_VIDEO
  m_videoPushTime.SetTimestamp(0);
#endif
  return !fn.IsEmpty();
}


bool OpalRecordManager::OpenStream(const PString & strmId, const OpalMediaFormat & format)
{
  PTRACE(4, "Opening stream " << strmId << ' ' << format);

  // We can only record audio and video
  return (format.GetMediaType() == OpalMediaType::Audio()
#if OPAL_VIDEO
            || format.GetMediaType() != OpalMediaType::Video()
#endif
         ) &&  m_streamFormats.insert(make_pair(strmId, format)).second;
}


OpalMediaFormat OpalRecordManager::GetStreamFormat(const PString & strmId) const
{
  StreamFormatMap::const_iterator it = m_streamFormats.find(strmId);
  return it != m_streamFormats.end() ? it->second : OpalMediaFormat();
}


bool OpalRecordManager::QueueStream(const PString & strmId, const RTP_DataFrame & rtp)
{
  StreamFormatMap::const_iterator it = m_streamFormats.find(strmId);
  if (it == m_streamFormats.end())
    return false;

  PTRACE(m_audioLogThrottle, "Writing " << rtp.GetPayloadSize() << " bytes"
         " of " << it->second.GetMediaType() << " to " << strmId << (m_muted.load() ? " (muted)" : "")
         << m_audioLogThrottle);

  if (m_muted.load())
    return true;

  if (it->second.GetMediaType() == OpalMediaType::Audio())
    return InternalQueueAudio(strmId, rtp);

#if OPAL_VIDEO
  if (it->second.GetMediaType() == OpalMediaType::Video())
    return InternalQueueVideo(strmId, rtp);
#endif

  return false;
}


bool OpalRecordManager::QueueAudio(const PString & strmId, const RTP_DataFrame & rtp)
{
  PTRACE(m_audioLogThrottle, "Writing " << rtp.GetPayloadSize() << " bytes"
         " of audio to " << strmId << (m_muted.load() ? " (muted)" : "")
         << m_audioLogThrottle);
  return m_muted.load() || InternalQueueAudio(strmId, rtp);
}


#if OPAL_VIDEO
bool OpalRecordManager::QueueVideo(const PString & strmId, const RTP_DataFrame & rtp)
{
  PTRACE(m_videoLogThrottle, "Writing " << rtp.GetPayloadSize() << " bytes"
         " of video to " << strmId << (m_muted.load() ? " (muted)" : "")
         << m_videoLogThrottle);
  return m_muted.load() || InternalQueueVideo(strmId, rtp);
}
#endif

bool OpalRecordManager::CloseStream(const PString & strmId)
{
  if (m_streamFormats.erase(strmId) == 0)
    return false;

  PTRACE(4, "Closed stream " << strmId);
  return true;
}


bool OpalRecordManager::OnPushMedia(const PTime & when)
{
  if (!m_audioPushTime.IsValid() || when - m_audioPushTime > m_maxJumpTime)
    m_audioPushTime = when;
  while (when > m_audioPushTime) {
    if (!OnPushAudio())
      return false;
    m_audioPushTime += GetPushAudioPeriodMS();
  }

#if OPAL_VIDEO
  if (!m_videoPushTime.IsValid() || when - m_videoPushTime > m_maxJumpTime)
    m_videoPushTime = when;
  while (when > m_videoPushTime) {
    if (!OnPushVideo())
      return false;
    m_videoPushTime += GetPushVideoPeriodMS();
  }
#endif

  return true;
}


//////////////////////////////////////////////////////////////////////////////

/** This class manages the recording of OPAL calls to files supported by PMediaFile.
  */
class OpalMediaFileRecordManager : public OpalRecordManager
{
  public:
    OpalMediaFileRecordManager();
    ~OpalMediaFileRecordManager();

    virtual bool OpenFile(const PFilePath & fn) override;
    virtual bool IsOpen() const override;
    virtual bool Close() override;
    virtual bool OpenStream(const PString & strmId, const OpalMediaFormat & format) override;
    virtual bool CloseStream(const PString & strmId) override;

    struct FactoryInitialiser : OpalRecordManager::Factory::WorkerBase
    {
      FactoryInitialiser();
      virtual OpalRecordManager * Create(OpalRecordManager::Factory::Param_T) const override;
    };

  protected:
    mutable PDECLARE_INSTRUMENTED_MUTEX(m_mutex, MediaFileRecorder, 500, 100);
    PFilePath m_tempFile;
    PFilePath m_finalFile;
    PMediaFile::Ptr m_file;
    std::vector<uint8_t> m_buffer;

    // Audio
    virtual bool InternalQueueAudio(const PString & strmId, const RTP_DataFrame & rtp) override;
    virtual bool OnPushAudio() override;
    virtual unsigned GetPushAudioPeriodMS() const override;
    // Callback from OpalAudioMixer
    virtual bool OnMixedAudio(const RTP_DataFrame & frame);

    struct AudioMixer : public OpalAudioMixer
    {
      AudioMixer(
        OpalMediaFileRecordManager & manager,
        bool stereo,
        unsigned sampleRate,
        bool pushThread
      ) : OpalAudioMixer(stereo, sampleRate, pushThread, 60) // 60ms is good for 99% of codecs
        , m_manager(manager)
      { }
      ~AudioMixer() { StopPushThread(); }
      virtual bool OnMixed(std::unique_ptr<RTP_DataFrame> & output) override { return m_manager.OnMixedAudio(*output); }
      OpalMediaFileRecordManager & m_manager;
    };
    PAtomicSharedPtr<AudioMixer> m_audioMixer;
    unsigned m_audioTrack;

#if OPAL_VIDEO
    virtual bool InternalQueueVideo(const PString & strmId, const RTP_DataFrame & rtp) override;
    virtual bool OnPushVideo() override;
    virtual unsigned GetPushVideoPeriodMS() const override;

    // Callback from OpalVideoMixer
    virtual bool OnMixedVideo(const RTP_DataFrame & frame);

    struct VideoMixer : public OpalVideoMixer
    {
      VideoMixer(
        OpalMediaFileRecordManager & manager,
        OpalVideoMixer::Styles style,
        unsigned width,
        unsigned height,
        unsigned rate,
        bool pushThread
      ) : OpalVideoMixer(style, width, height, rate, pushThread)
        , m_manager(manager)
      { }
      ~VideoMixer() { StopPushThread(); }
      virtual bool OnMixed(std::unique_ptr<RTP_DataFrame> & output) override { return m_manager.OnMixedVideo(*output); }
      OpalMediaFileRecordManager & m_manager;
    };
    PAtomicSharedPtr<VideoMixer> m_videoMixer;
    unsigned m_videoTrack;
#endif
};


OpalMediaFileRecordManager::OpalMediaFileRecordManager()
  : m_audioTrack(numeric_limits<unsigned>::max())
#if OPAL_VIDEO
  , m_videoTrack(numeric_limits<unsigned>::max())
#endif
{
}


OpalMediaFileRecordManager::~OpalMediaFileRecordManager()
{
  Close();
}


bool OpalMediaFileRecordManager::OpenFile(const PFilePath & fn)
{
  PWaitAndSignal mutex(m_mutex);

  if (!OpalRecordManager::OpenFile(fn))
    return false;

  m_file.reset(PMediaFile::Create(fn));
  if (!m_file) {
    PTRACE(2, "Cannot use media file type of " << fn);
    return false;
  }

  PTRACE_CONTEXT_ID_TO(*m_file);

  m_finalFile = m_tempFile = fn;
  m_tempFile.SetType(".recording" + m_finalFile.GetType());
  if (!m_file->OpenForWriting(m_tempFile)) {
    PTRACE(2, "Cannot open media file \"" << m_tempFile << "\" for writing: " << m_file->GetErrorText());
    m_file = NULL;
    return false;
  }

  m_audioMixer = std::make_shared<AudioMixer>(*this,
                                m_options.m_stereo,
                                m_options.m_audioSampleRate,
                                m_options.m_pushThreads);
  PTRACE_CONTEXT_ID_TO(*m_audioMixer.load());

  if (m_options.m_audioFormat.IsEmpty()) {
    PMediaFile::TrackInfo info;
    if (m_file->GetDefaultTrackInfo(PMediaFile::Audio(), info)) {
      m_options.m_audioFormat = info.m_format;
      m_options.m_audioOptions.Merge(info.m_options, PStringOptions::e_MergeIgnore);
    }
  }

#if OPAL_VIDEO
  if (m_options.m_videoFormat.IsEmpty()) {
    PMediaFile::TrackInfo info;
    if (m_file->GetDefaultTrackInfo(PMediaFile::Video(), info)) {
      m_options.m_videoFormat = info.m_format;
      m_options.m_videoOptions.Merge(info.m_options, PStringOptions::e_MergeIgnore);
    }
  }

  OpalVideoMixer::Styles style;
  switch (m_options.m_videoMixing) {
    default :
      PAssertAlways(PInvalidParameter);
    case eSideBySideScaled :
      m_options.m_videoWidth *= 2;
      style = OpalVideoMixer::eSideBySideScaled;
      break;

    case eSideBySideLetterbox :
      style = OpalVideoMixer::eSideBySideLetterbox;
      break;

    case eStackedScaled :
      m_options.m_videoHeight *= 2;
      style = OpalVideoMixer::eStackedScaled;
      break;

    case eStackedPillarbox :
      style = OpalVideoMixer::eStackedPillarbox;
      break;
  }

  m_videoMixer = std::make_shared<VideoMixer>(*this,
                                style,
                                m_options.m_videoWidth,
                                m_options.m_videoHeight,
                                m_options.m_videoRate,
                                m_options.m_pushThreads);
  PTRACE_CONTEXT_ID_TO(*m_videoMixer.load());
#endif // OPAL_VIDEO

  PMediaFile::TracksInfo tracks;
  m_file->GetTracks(tracks);
  for (size_t i = 0; i < tracks.size(); ++i) {
    if (tracks[i].m_type == OpalMediaType::Audio())
      m_audioTrack = i;
#if OPAL_VIDEO
    else if (tracks[i].m_type == OpalMediaType::Video())
      m_videoTrack = i;
#endif //OPAL_VIDEO
  }

#if PTRACING
  const unsigned LogLevel = 3;
  if (PTrace::CanTrace(LogLevel)) {
    ostream & log = PTRACE_BEGIN(LogLevel, this);
    if (m_options.m_audioFormat.empty())
      log << "No-audio";
    else
      log << (m_options.m_stereo ? "Stereo" : "Mono") << '-' << m_options.m_audioFormat;
    if (m_audioTrack < tracks.size())
      log << '[' << m_audioTrack << ']';
  #if OPAL_VIDEO
    log << '/';
    if (m_options.m_videoFormat.empty())
      log << "No-video";
    else
      log << m_options.m_videoFormat;
    if (m_videoTrack < tracks.size())
      log << '[' << m_videoTrack << ']';
#endif //OPAL_VIDEO
    log << " opened for file \"" << fn << '"' << PTrace::End;
  }
#endif // PTRACING

  return true;
}


bool OpalMediaFileRecordManager::IsOpen() const
{
  return m_file != NULL;
}


bool OpalMediaFileRecordManager::Close()
{
  // Deleted when out of scope
  m_audioMixer.store(nullptr);

#if OPAL_VIDEO
  m_videoMixer.store(nullptr);
#endif

  m_mutex.Wait();

  PMediaFile::Ptr file = m_file;
  m_file = NULL;

  if (file) {
    if (!m_buffer.empty()) {
      PINDEX written;
      file->WriteAudio(m_audioTrack, m_buffer.data(), m_buffer.size(), written);
      m_buffer.clear();
    }

    if (!file->Close()) {
      PTRACE(2, "Could not close \"" << m_tempFile << '"');
    }
    else if (!PFile::Move(m_tempFile, m_finalFile, true)) {
      PTRACE(2, "Could not rename \"" << m_tempFile << "\" to \"" << m_finalFile << '"');
    }
    else {
      PTRACE(3, "Recording completed to \"" << m_finalFile << '"');
    }
  }

  m_mutex.Signal();

  return true;
}


bool OpalMediaFileRecordManager::OpenStream(const PString & strmId, const OpalMediaFormat & format)
{
  PWaitAndSignal mutex(m_mutex);

  if (!OpalRecordManager::OpenStream(strmId, format))
    return false;

  OpalMediaType mediaType = format.GetMediaType();

  unsigned trackId;
  PString outputFormat;
  OpalBaseMixer * mixer;

  if (mediaType == OpalMediaType::Audio() && !m_options.m_audioFormat.empty()) {
    trackId = m_audioTrack;
    outputFormat = m_options.m_audioFormat;
    mixer = m_audioMixer.load().get();
  }
#if OPAL_VIDEO
  else if (mediaType == OpalMediaType::Video() && !m_options.m_videoFormat.empty()) {
    trackId = m_videoTrack;
    outputFormat = m_options.m_videoFormat;
    mixer = m_videoMixer.load().get();
  }
#endif
  else {
    PTRACE(2, "Unsupported media type " << mediaType);
    return false;
  }

  if (PAssertNULL(mixer) == NULL)
    return false;

  if (outputFormat.IsEmpty()) {
    PTRACE(2, "No output format for media type " << mediaType);
    return false;
  }

  PMediaFile::TracksInfo tracks;
  m_file->GetTracks(tracks);

  if (trackId >= tracks.size()) {
    trackId = tracks.size();
    PMediaFile::TrackInfo info;
    if (m_file->GetDefaultTrackInfo(mediaType, info))
      tracks.push_back(info);
    else
      tracks.push_back(PMediaFile::TrackInfo(mediaType, outputFormat));
  }

  PMediaFile::TrackInfo & track = tracks[trackId];

#if OPAL_VIDEO
  if (mediaType == OpalMediaType::Video()) {
    track.m_width = m_options.m_videoWidth;
    track.m_height = m_options.m_videoHeight;
    track.m_rate = m_options.m_videoRate;
    track.m_options.Merge(m_options.m_videoOptions, PStringOptions::e_MergeOverwrite);

    if (!m_file->SetTracks(tracks)) {
      PTRACE(2, "Could not set " << tracks.size() << " tracks with video at " << trackId);
      return false;
    }

    m_videoTrack = trackId;

    PVideoFrameInfo frameInfo(m_options.m_videoWidth, m_options.m_videoHeight);
    if (!m_file->ConfigureVideo(m_videoTrack, frameInfo)) {
      PTRACE(2, "Cannot use " << frameInfo << " as video output");
      return false;
    }
  }
  else
#endif
  {
    track.m_channels = m_options.m_stereo ? 2 : 1;
    track.m_size = track.m_channels * sizeof(short);
    track.m_rate = m_options.m_audioSampleRate;
    track.m_options.Merge(m_options.m_audioOptions, PStringOptions::e_MergeOverwrite);

    if (!m_file->SetTracks(tracks)) {
      PTRACE(2, "Could not set " << tracks.size() << " tracks with audio at " << trackId);
      return false;
    }

    m_audioTrack = trackId;

    if (!m_file->ConfigureAudio(trackId, track.m_channels, format.GetClockRate())) {
      PTRACE(2, "Cannot use " << track.m_channels << " channels, " << format.GetClockRate() << "Hz as audio output");
      return false;
    }

    if (!m_audioMixer.load()->SetSampleRate(format.GetClockRate())) {
      PTRACE(2, "Could not set audio sample rate to " << format.GetClockRate());
      return false;
    }
  }

  PTRACE(3, "Set media file track for " << mediaType << ":"
            " stream-format=" << format << ","
            " file-format=\"" << outputFormat << "\","
            " id=" << strmId << ","
            " track=" << trackId);

  mixer->StartPushThread();
  return mixer->AddStream(strmId);
}


bool OpalMediaFileRecordManager::CloseStream(const PString & streamId)
{
  auto audioMixer = m_audioMixer.load();
#if OPAL_VIDEO
  auto videoMixer = m_videoMixer.load();
#endif

  if (audioMixer != NULL)
    audioMixer->RemoveStream(streamId);
#if OPAL_VIDEO
  if (videoMixer != NULL)
    videoMixer->RemoveStream(streamId);
#endif

  return OpalRecordManager::CloseStream(streamId);
}


bool OpalMediaFileRecordManager::OnPushAudio()
{
  auto audioMixer = m_audioMixer.load();
  return audioMixer && audioMixer->OnPush();
}


unsigned OpalMediaFileRecordManager::GetPushAudioPeriodMS() const
{
  auto audioMixer = m_audioMixer.load();
  return audioMixer ? audioMixer->GetPeriodMS() : 0;
}


bool OpalMediaFileRecordManager::InternalQueueAudio(const PString & strmId, const RTP_DataFrame & rtp)
{
  auto audioMixer = m_audioMixer.load();
  return audioMixer && audioMixer->QueueStream(strmId, rtp);
}


bool OpalMediaFileRecordManager::OnMixedAudio(const RTP_DataFrame & frame)
{
  PWaitAndSignal mutex(m_mutex);

  if (!IsOpen())
    return false;

  if (m_audioTrack >= m_file->GetTrackCount())
    return true; // No audio to mix is not an error

  PINDEX written;
  if (m_options.m_audioBufferSize == 0) {
    if (!m_file->WriteAudio(m_audioTrack, frame.GetPayloadPtr(), frame.GetPayloadSize(), written))
      return false;
    PTRACE(5, "Written " << written << " audio samples");
  }
  else {
    if (m_buffer.capacity() < m_options.m_audioBufferSize)
      m_buffer.reserve(m_options.m_audioBufferSize);

    const uint8_t * ptr = frame.GetPayloadPtr();
    m_buffer.insert(m_buffer.end(), ptr, ptr + frame.GetPayloadSize());
    if (m_buffer.size() < m_options.m_audioBufferSize)
      return true;

    if (!m_file->WriteAudio(m_audioTrack, m_buffer.data(), m_buffer.size(), written))
      return false;

    PTRACE(4, "Written " << written << " audio samples");
    m_buffer.clear();
  }

  return true;
}


#if OPAL_VIDEO

bool OpalMediaFileRecordManager::InternalQueueVideo(const PString & strmId, const RTP_DataFrame & rtp)
{
  auto videoMixer = m_videoMixer.load();
  return videoMixer && videoMixer->QueueStream(strmId, rtp);
}


bool OpalMediaFileRecordManager::OnPushVideo()
{
  auto videoMixer = m_videoMixer.load();
  return videoMixer && videoMixer->OnPush();
}


unsigned OpalMediaFileRecordManager::GetPushVideoPeriodMS() const
{
  auto videoMixer = m_videoMixer.load();
  return videoMixer ? videoMixer->GetPeriodMS() : 0;
}


bool OpalMediaFileRecordManager::OnMixedVideo(const RTP_DataFrame & frame)
{
  PWaitAndSignal mutex(m_mutex);

  if (!IsOpen())
    return false;

  if (m_videoTrack >= m_file->GetTrackCount())
    return true; // No video to mix is not an error

  PluginCodec_Video_FrameHeader * header = (PluginCodec_Video_FrameHeader *)frame.GetPayloadPtr();
  if (header->x != 0 || header->y != 0 || header->width != m_options.m_videoWidth || header->height != m_options.m_videoHeight) {
    PTRACE(2, "Unexpected change of video frame size!");
    return false;
  }

  if (!m_file->WriteVideo(m_videoTrack, OPAL_VIDEO_FRAME_DATA_PTR(header)))
    return false;

  PTRACE(6, "Written video frame");
  return true;
}

#endif // OPAL_VIDEO


OpalMediaFileRecordManager::FactoryInitialiser::FactoryInitialiser()
{
  PStringSet fileTypes = PMediaFile::GetAllFileTypes();

  PCaselessString firstExt;
  for (PStringSet::iterator it = fileTypes.begin(); it != fileTypes.end(); ++it) {
    if (!firstExt.IsEmpty())
      OpalRecordManager::Factory::RegisterAs(*it, firstExt);
    else {
      OpalRecordManager::Factory::Register(*it, this);
      firstExt = *it;
    }
  }
}


OpalRecordManager * OpalMediaFileRecordManager::FactoryInitialiser::Create(OpalRecordManager::Factory::Param_T) const
{
  return new OpalMediaFileRecordManager();
}


static OpalMediaFileRecordManager::FactoryInitialiser OpalMediaFileRecordManager_FactoryInitialiser_instance;


#endif // OPAL_HAS_MIXER


/////////////////////////////////////////////////////////////////////////////
