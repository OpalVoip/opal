/*
 * opalmixer.h
 *
 * OPAL media mixers
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
 *
 * Copyright (C) 2007 Post Increment
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
 * Contributor(s): Craig Southeren (craigs@postincrement.com)
 *                 Robert Jongbloed (robertj@voxlucida.com.au)
 */


#ifndef OPAL_OPAL_OPALMIXER_H
#define OPAL_OPAL_OPALMIXER_H

#ifndef _PTLIB_H
#include <ptlib.h>
#endif

#include <opal_config.h>

#if OPAL_HAS_MIXER

#include <queue>

#include <ep/localep.h>
#include <codec/vidcodec.h>


class RTP_DataFrame;
class OpalJitterBuffer;
class OpalMixerConnection;


//#define OPAL_MIXER_AUDIO_DEBUG 1


#define OPAL_MIXER_PREFIX "mcu"

#define OPAL_OPT_LISTEN_ONLY "Listen-Only"      ///< String option for listen only mixer connection
#define OPAL_OPT_CONF_OWNER  "Conference-Owner" ///< String option for listen only mixer connection


///////////////////////////////////////////////////////////////////////////////

/** Class base for a media mixer.

    The mixer operates by re-buffering the input media into chunks each with an
    associated timestamp. A main mixer thread then reads from each  stream at
    regular intervals, mixes the media and creates the output buffer.
 
    Note the timestamps of the input media are extremely important as they are
    used so that breaks or too fast data in the input media is dealt with correctly.
  */
class OpalBaseMixer : public PObject
{
    PCLASSINFO(OpalBaseMixer, PObject);
  public:
    OpalBaseMixer(
      bool pushThread,    ///< Indicate if the push thread should be started
      unsigned periodMS,  ///< The output buffer time in milliseconds
      unsigned periodTS   ///< The output buffer time in RTP timestamp units
    );

    virtual ~OpalBaseMixer();

    typedef PString Key_T;

    /**Add a stream to mixer using the specified key.
      */
    virtual bool AddStream(
      const Key_T & key   ///< key for mixer stream
    );

    /** Remove an input stream from mixer.
      */
    virtual void RemoveStream(
      const Key_T & key   ///< key for mixer stream
    );

    /** Remove all input streams from mixer.
      */
    virtual void RemoveAllStreams();

    /**Write an RTP data frame to mixer.
       A copy of the RTP data frame is created. This function is generally
       quite fast as the actual mixing is done in a different thread so
       minimal interference with the normal media stream processing occurs.
      */
    virtual bool QueueStream(
      const Key_T & key,          ///< key for mixer stream
      const RTP_DataFrame & input ///< Input RTP data for media
    );

    /**Read media from mixer.
       A pull model system would call this function to get the mixed media
       from the mixer. Note the stream indicated by the streamToIgnore key is
       not included in the mixing operation, allowing for example, the member
       of a conference to not hear themselves.

       Note this function is the function that does all the "heavy lifting"
       for the mixer.
      */
    virtual RTP_DataFrame * ReadMixed();
    virtual bool ReadMixed(RTP_DataFrame & mixed);

    /**Mixed data is now available.
       For a push model system, this is called with mixed data as returned by
       ReadMixed().

       The "mixed" parameter is a reference to a pointer, so if the consumer
       wishes to take responsibility for deleting the pointer to an RTP data
       frame, then they can set it to NULL.

       If false is returned then the push thread is exited.
      */
    virtual bool OnMixed(
      std::unique_ptr<RTP_DataFrame> & mixed   ///< Pointer to mixed media.
    );

    /**Start the push thread.
       Normally called internally.
      */
    virtual void StartPushThread();

    /**Start the push thread even when no input stream has been added yet.
       A still-image conference sends video before anyone provides a picture.
      */
    void EnsurePushThread();

    /**Push collected input through the mixer to output.
       This would normally be called from a thread, on a metronomic basis,
       from a thread started via StartPushThread(), however if m_pushThread
       is false, that thread is suppressed and it is expected the
       application would call this function as required.
      */
    virtual bool OnPush();

    /**Get the period for mixing in milliseconds.
      */
    unsigned GetPeriodMS() const { return m_periodMS; }

    /**Get the period for mixing in RTP timestamp units.
      */
    unsigned GetPeriodTS() const { return m_periodTS; }

    /**Mutex that guards the input streams and the published mix strategy.
       A strategy pointer is replaced only while this mutex is held, and the
       mix tick holds it across the call into the strategy.
      */
    PMutex & GetMutex() { return m_mutex; }

  protected:
    virtual void OnStrategyPublished() { }
    struct Stream : public PObject {
      virtual void QueuePacket(const RTP_DataFrame & rtp) = 0;
      std::queue<RTP_DataFrame> m_queue;
    };
    typedef std::map<Key_T, Stream *> StreamMap_T;

    virtual Stream * CreateStream() = 0;
    virtual bool MixStreams(RTP_DataFrame & frame) = 0;
    virtual size_t GetOutputSize() const = 0;

    void PushThreadMain();
    void StopPushThread(bool lock = true);

    bool            m_pushThread;       // true if to use a thread to push data out
    unsigned        m_periodMS;         // Mixing interval in milliseconds
    unsigned        m_periodTS;         // Mixing interval in timestamp units
    StreamMap_T     m_inputStreams;     // Map of key to stream for input RTP frame queues
    unsigned        m_outputTimestamp;  // RTP timestamp for output data
    PThread       * m_workerThread;     // reader thread handle
    atomic<bool>    m_threadRunning;    // used to stop reader thread
    std::unique_ptr<RTP_DataFrame> m_pushFrame; // Cached frame for pushing RTP
    PDECLARE_MUTEX(  m_mutex);           // mutex for list of streams and thread handle
   PTRACE_THROTTLE( m_threadProgressThrottle, 3, 30000);
};

///////////////////////////////////////////////////////////////////////////////

/** Class for an audio mixer.
    This takes raw PCM-16 data and sums all the input data streams to produce
    a single PCM-16 sample value.

    For 2 or less channels, they may be mixed as stereo where 16 bit PCM
    samples are placed in adjacent pairs in the output, rather than summing
    them.
  */
class OpalAudioMixerStrategy;

class OpalAudioMixer : public OpalBaseMixer
{
    PCLASSINFO(OpalAudioMixer, OpalBaseMixer);
    friend class OpalAudioMixerStrategy;
  public:
    OpalAudioMixer(
      bool stereo = false,    ///< Indicate stereo or mixed mono mode
      unsigned sampleRate = OpalMediaFormat::AudioClockRate, ///< Sample rate for audio, default 8kHz
      bool pushThread = true, ///< Indicate push thread is to be used
      unsigned period = 10,   ///< Period for push/pull of audio from mixer in milliseconds
      const PTimeInterval & activeSpakerDeadband = 0 ///< Period for active speaker detection
    );

    ~OpalAudioMixer();

    /** Remove an input stream from mixer.
      */
    virtual void RemoveStream(
      const Key_T & key   ///< key for mixer stream
    ) override;

    /** Remove all input streams from mixer.
      */
    virtual void RemoveAllStreams() override;

    /**Return flag for mixing stereo audio data.
      */
    bool IsStereo() const { return m_stereo; }

    /**Get sample rate for audio.
      */
    unsigned GetSampleRate() const { return m_sampleRate; }

    /**Set sample rate for audio data.
       Note that all streams must have the same sample rate.

       Returns false if attempts to set sample rate to something different to
       existing streams.
      */
    bool SetSampleRate(
      unsigned rate   ///< New rate
    );

    /**Sets the size of the jitter buffer to be used by the specified stream
       in this mixer. A mixer defaults to not having any jitter buffer enabled.

       If either jitter delay parameter is zero, it destroys the jitter buffer
       attached to this mixer.
      */
    bool SetJitterBufferSize(
      const Key_T & key,                    ///< key for mixer stream
      const OpalJitterBuffer::Init & init   ///< Initialisation information
    );

  protected:
    struct AudioStream : public Stream
    {
      AudioStream(OpalAudioMixer & mixer);

      virtual void QueuePacket(const RTP_DataFrame & rtp);
      const short * GetAudioDataPtr();

      OpalAudioMixer   & m_mixer;
      PAutoPtr<OpalJitterBuffer> m_jitter;
      unsigned           m_nextTimestamp;
      PShortArray        m_cacheSamples;
      size_t             m_samplesUsed;
      OpalSilenceDetector::CalculateDB m_dbCalculator;
    };

    virtual Stream * CreateStream() override;
    virtual bool MixStreams(RTP_DataFrame & frame) override;
    virtual size_t GetOutputSize() const override;

    /**Published audio strategy. The mixer mutex is held by the caller.
      */
    virtual OpalAudioMixerStrategy & GetAudioStrategy();

  public:
    bool PreMixStreams(); // Return value is active speaker changed
    void MixStereo(RTP_DataFrame & frame);
    void MixAdditive(RTP_DataFrame & frame, const short * audioToSubtract);

  protected:
    bool     m_stereo;
    unsigned m_sampleRate;

    AudioStream    * m_left;
    AudioStream    * m_right;
    std::vector<int> m_mixedAudio;

    Key_T         m_activeSpeakerKey;
    PTimeInterval m_activeSpeakerDeadband;
    PTimeInterval m_activeSpeakerTime;

    OpalAudioMixerStrategy * m_audioStrategy;
    std::unique_ptr<OpalAudioMixerStrategy> m_ownedAudioStrategy;
};


/**How an audio mixer combines its input streams.
   The active instance is published under OpalBaseMixer::m_mutex.
  */
class OpalAudioMixerStrategy
{
  public:
    virtual ~OpalAudioMixerStrategy() { }

    virtual bool Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame) = 0;
    virtual void MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract) = 0;
    virtual bool IsStereo() const = 0;
    virtual bool IsPassThrough() const { return false; }
};


class OpalAdditiveAudioStrategy : public OpalAudioMixerStrategy
{
  public:
    virtual bool Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame) override;
    virtual void MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract) override;
    virtual bool IsStereo() const override { return false; }
};


class OpalStereoAudioStrategy : public OpalAudioMixerStrategy
{
  public:
    virtual bool Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame) override;
    virtual void MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract) override;
    virtual bool IsStereo() const override { return true; }
};


/**Forwards encoded audio instead of summing it.
   Conference nodes keep the additive strategy so each listener still hears
   one stream with their own audio removed.
  */
class OpalPassThroughAudioStrategy : public OpalAudioMixerStrategy
{
  public:
    virtual bool Mix(OpalAudioMixer & mixer, RTP_DataFrame & frame) override;
    virtual void MixListener(OpalAudioMixer & mixer, RTP_DataFrame & frame, const short * audioToSubtract) override;
    virtual bool IsStereo() const override { return false; }
    virtual bool IsPassThrough() const override { return true; }
};


///////////////////////////////////////////////////////////////////////////////

#if OPAL_VIDEO

/**Video mixer.
   This takes raw YUV420P frames with a PluginCodec_Video_FrameHeader in the
   RTP data frames, scales them and places them in particular positions of the
   output data frame. A number of different patterns for positioning the sub
   images are available in the Styles enum.
  */
class OpalVideoMixerStrategy;

class OpalVideoMixer : public OpalBaseMixer
{
    PCLASSINFO(OpalVideoMixer, OpalBaseMixer);
    friend class OpalVideoMixerStrategy;
  public:
    enum Styles {
      eSideBySideLetterbox, /**< Two images side by side with black bars top and bottom.
                                 It is expected that the input frames and output are all
                                 the same aspect ratio, e.g. 4:3. Works well if inputs
                                 are QCIF and output is CIF for example. */
      eSideBySideScaled,    /**< Two images side by side, scaled to fit halves of output
                                 frame. It is expected that the output frame be double
                                 the width of the input data to maintain aspect ratio.
                                 e.g. for CIF inputs, output would be 704x288. */
      eStackedPillarbox,    /**< Two images, one on top of the other with black bars down
                                 the sides. It is expected that the input frames and output
                                 are all the same aspect ratio, e.g. 4:3. Works well if
                                 inputs are QCIF and output is CIF for example. */
      eStackedScaled,       /**< Two images, one on top of the other, scaled to fit halves
                                 of output frame. It is expected that the output frame be
                                 double the height of the input data to maintain aspect
                                 ratio. e.g. for CIF inputs, output would be 352x576. */
      eGrid,                /**< Standard 2x2, 3x3, 4x4 grid pattern. Size of grid is
                                 dependent on the number of video streams. */
      eActiveSpeaker,       /**< Active speaker is full size, all others are suppressed. */
      eUser                 /**< User defined */
    };

    OpalVideoMixer(
      Styles style,           ///< Style for mixing video
      unsigned width,         ///< Width of output frame
      unsigned height,        ///< Height of output frame
      unsigned rate = 15,     ///< Frames per second for output
      bool pushThread = true  ///< A push thread is to be created
    );

    ~OpalVideoMixer();

    /**Get output video frame width.
      */
    unsigned GetFrameWidth() const { return m_width; }

    /**Get output video frame height.
      */
    unsigned GetFrameHeight() const { return m_height; }

    /**Get output video frame rate (frames per second)
      */
    unsigned GetFrameRate() const { return 1000/m_periodMS; }

    /**Set output video frame rate.
       May be dynamically changed at any time.
      */
    virtual bool SetFrameRate(
      unsigned rate   // New frames per second.
    );

    /**Set the output video frame width and height.
       May be dynamically changed at any time.
      */
    bool SetFrameSize(
      unsigned width,   ///< New width
      unsigned height   ///< new height
    );

    /** Get the current active speaker for eActiveSpeaker mode.
      */
    virtual Key_T GetActiveSpeaker() const;

    Styles GetStyle() const { return m_style; }

    /**Number of queued input streams. Caller holds the mixer mutex.
      */
    size_t GetInputCount() const { return m_inputStreams.size(); }

    /**Layout object for a style value. eUser uses the same grid as an unspecified style.
      */
    static std::unique_ptr<class OpalVideoLayoutStrategy> CreateLayout(Styles style);

    /**Published video strategy. Caller holds the mixer mutex.
       The default is OpalOriginalVideoStrategy. A derived mixer overrides
       this to publish a different strategy.
      */
    virtual OpalVideoMixerStrategy & GetVideoStrategy();

    /**True when GetVideoStrategy published a different instance. Caller holds the mixer mutex.
      */
    bool TakeDeliveryChange();

    /**Lock, publish the strategy, and report the previous and next instances.
      */
    bool ExchangeStrategy(OpalVideoMixerStrategy * & previous, OpalVideoMixerStrategy * & next);

    /**Copy the YUV frame store into an RTP payload. Caller holds the mixer mutex.
      */
    bool CopyMixedFrame(RTP_DataFrame & frame);

    /**Paint a YUV420P image, or the background colour when the buffer is empty.
       Caller holds the mixer mutex.
      */
    void PaintStillFrame(const PBYTEArray & yuv);

    struct MixPosition
    {
      MixPosition() : x(0), y(0), w(0), h(0) { }
      MixPosition(unsigned _x, unsigned _y, unsigned _w, unsigned _h) : x(_x), y(_y), w(_w), h(_h) { }
      unsigned x, y, w, h;
    };

protected:
    struct VideoStream : public Stream
    {
      VideoStream(OpalVideoMixer & mixer);
      virtual void QueuePacket(const RTP_DataFrame & rtp);
      void InsertVideoFrame(const MixPosition & pos);

      OpalVideoMixer & m_mixer;
    };

    friend struct VideoStream;

    virtual Stream * CreateStream() override;
    virtual bool MixStreams(RTP_DataFrame & frame) override;
    virtual size_t GetOutputSize() const override;

  public:
    virtual bool MixVideo();

  protected:
    virtual bool GetMixPosition(
      const PString & id,
      bool firstStream,
      MixPosition & pos
    );

  protected:
    Styles     m_style;
    unsigned   m_width, m_height;
    BYTE       m_bgFillRed,m_bgFillGreen,m_bgFillBlue;

    PBYTEArray m_frameStore;
    size_t     m_lastStreamCount;

    unsigned   m_mixLeftStartPosition;

    OpalVideoMixerStrategy * m_videoStrategy;
    std::unique_ptr<OpalVideoMixerStrategy> m_ownedVideoStrategy;
    bool m_deliveryChanged;

  P_REMOVE_VIRTUAL(bool,StartMix(unsigned&,unsigned&,unsigned&,unsigned&,unsigned&),false);
  P_REMOVE_VIRTUAL(bool,NextMix(unsigned&,unsigned&,unsigned&,unsigned&,unsigned&),false);
};


/**Rectangle placement for one input of a transcoded mix.
  */
class OpalVideoLayoutStrategy
{
  public:
    virtual ~OpalVideoLayoutStrategy() { }

    virtual bool GetMixPosition(
      OpalVideoMixer & mixer,
      const OpalVideoMixer::Key_T & key,
      bool firstStream,
      OpalVideoMixer::MixPosition & pos
    ) = 0;
};


class OpalGridVideoLayout : public OpalVideoLayoutStrategy
{
  public:
    OpalGridVideoLayout() : m_left(0) { }
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;

  private:
    unsigned m_left;
};


class OpalSideBySideLetterboxLayout : public OpalVideoLayoutStrategy
{
  public:
    OpalSideBySideLetterboxLayout() : m_left(0) { }
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;

  private:
    unsigned m_left;
    OpalGridVideoLayout m_grid;
};


class OpalSideBySideScaledLayout : public OpalVideoLayoutStrategy
{
  public:
    OpalSideBySideScaledLayout() : m_left(0) { }
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;

  private:
    unsigned m_left;
    OpalGridVideoLayout m_grid;
};


class OpalStackedPillarboxLayout : public OpalVideoLayoutStrategy
{
  public:
    OpalStackedPillarboxLayout() : m_left(0) { }
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;

  private:
    unsigned m_left;
    OpalGridVideoLayout m_grid;
};


class OpalStackedScaledLayout : public OpalVideoLayoutStrategy
{
  public:
    OpalStackedScaledLayout() : m_left(0) { }
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;

  private:
    unsigned m_left;
    OpalGridVideoLayout m_grid;
};


class OpalActiveSpeakerLayout : public OpalVideoLayoutStrategy
{
  public:
    virtual bool GetMixPosition(OpalVideoMixer & mixer, const OpalVideoMixer::Key_T & key, bool firstStream, OpalVideoMixer::MixPosition & pos) override;
};


class OpalMixerNode;

/**What a video mixer sends, and how a transcoded frame is arranged.
   The mixer publishes one instance under OpalBaseMixer::m_mutex. Slow work
   (pass-through, image files, extra sessions) is done from Activate and
   Deactivate after that mutex is released.
  */
class OpalVideoMixerStrategy
{
  public:
    virtual ~OpalVideoMixerStrategy() { }

    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) = 0;
    virtual bool WantsTranscode() const = 0;
    virtual OpalVideoLayoutStrategy & Layout() = 0;
    virtual void Activate(OpalMixerNode & node, OpalVideoMixer & mixer) { (void)node; (void)mixer; }
    virtual void Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer) { (void)node; (void)mixer; }
    virtual void SetForceTranscode(bool on) { (void)on; }
};


class OpalTranscodeVideoStrategy : public OpalVideoMixerStrategy
{
  public:
    explicit OpalTranscodeVideoStrategy(std::unique_ptr<OpalVideoLayoutStrategy> layout);
    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) override;
    virtual bool WantsTranscode() const override { return true; }
    virtual OpalVideoLayoutStrategy & Layout() override { return *m_layout; }

  private:
    std::unique_ptr<OpalVideoLayoutStrategy> m_layout;
};


/**Historic mixer behaviour: decode, paint one YUV frame, encode one stream.
  */
class OpalOriginalVideoStrategy : public OpalTranscodeVideoStrategy
{
  public:
    explicit OpalOriginalVideoStrategy(std::unique_ptr<OpalVideoLayoutStrategy> layout)
      : OpalTranscodeVideoStrategy(std::move(layout)) { }
};


class OpalStillImageVideoStrategy : public OpalVideoMixerStrategy
{
  public:
    explicit OpalStillImageVideoStrategy(const PString & image);
    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) override;
    virtual bool WantsTranscode() const override { return true; }
    virtual OpalVideoLayoutStrategy & Layout() override { return m_layout; }
    virtual void Activate(OpalMixerNode & node, OpalVideoMixer & mixer) override;
    virtual void Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer) override;

  private:
    PString m_image;
    PBYTEArray m_frame;
    OpalGridVideoLayout m_layout;
};


class OpalPairRelayVideoStrategy : public OpalVideoMixerStrategy
{
  public:
    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) override;
    virtual bool WantsTranscode() const override { return false; }
    virtual OpalVideoLayoutStrategy & Layout() override { return m_layout; }
    virtual void Activate(OpalMixerNode & node, OpalVideoMixer & mixer) override;
    virtual void Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer) override;

  private:
    OpalGridVideoLayout m_layout;
};


class OpalMultiStreamRelayStrategy : public OpalVideoMixerStrategy
{
  public:
    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) override;
    virtual bool WantsTranscode() const override { return false; }
    virtual OpalVideoLayoutStrategy & Layout() override { return m_layout; }
    virtual void Activate(OpalMixerNode & node, OpalVideoMixer & mixer) override;
    virtual void Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer) override;

  private:
    OpalGridVideoLayout m_layout;
};


/**Example selector: still image, pair relay, multi-stream relay, then the original mix.
   Not used unless a derived mixer returns it from GetVideoStrategy.
  */
class OpalParticipantCountVideoStrategy : public OpalVideoMixerStrategy
{
  public:
    OpalParticipantCountVideoStrategy(const PString & stillImage,
                                      std::unique_ptr<OpalVideoLayoutStrategy> layout);
    virtual bool Mix(OpalVideoMixer & mixer, RTP_DataFrame & frame) override;
    virtual bool WantsTranscode() const override;
    virtual OpalVideoLayoutStrategy & Layout() override;
    virtual void Activate(OpalMixerNode & node, OpalVideoMixer & mixer) override;
    virtual void Deactivate(OpalMixerNode & node, OpalVideoMixer & mixer) override;
    virtual void SetForceTranscode(bool on) override;

  private:
    OpalVideoMixerStrategy & Select(const OpalMixerNode & node);

    OpalStillImageVideoStrategy m_still;
    OpalPairRelayVideoStrategy m_pair;
    OpalMultiStreamRelayStrategy m_multi;
    OpalOriginalVideoStrategy m_original;
    OpalVideoMixerStrategy * m_active;
    bool m_forceTranscode;
};

#endif // OPAL_VIDEO


///////////////////////////////////////////////////////////////////////////////


/**Base class for OpalMixerNode options.
   The user may derive from this class, making sure they implement the Clone()
   function, to add extra options for use by their OpalMixerNode derived class.
  */
struct OpalMixerNodeInfo
{
  OpalMixerNodeInfo(const char * name = NULL)
    : m_name(name)
    , m_closeOnEmpty(false)
    , m_listenOnly(false)
    , m_sampleRate(OpalMediaFormat::AudioClockRate)
#if OPAL_VIDEO
    , m_audioOnly(false)
    , m_style(OpalVideoMixer::eGrid)
    , m_width(PVideoFrameInfo::CIFWidth)
    , m_height(PVideoFrameInfo::CIFHeight)
    , m_rate(15)
#endif
    , m_mediaPassThru(false)
    , m_activeSpeakerDeadband(0, 2) // Default to 2 seconds
  { }

  virtual ~OpalMixerNodeInfo() { }

  // Make sure derived classes implement this function
  virtual OpalMixerNodeInfo * Clone() const = 0;

  PString  m_name;                ///< Name for mixer node.
  bool     m_closeOnEmpty;        ///< Mixer node is removed when last participant exits
  bool     m_listenOnly;          ///< Mixer only transmits data to "listeners"
  unsigned m_sampleRate;          ///< Audio sample rate, usually 8000
#if OPAL_VIDEO
  bool     m_audioOnly;           ///< No video is to be allowed.
  OpalVideoMixer::Styles m_style; ///< Method for mixing video
  unsigned m_width;               ///< Width of mixed video
  unsigned m_height;              ///< Height of mixed video
  unsigned m_rate;                ///< Frame rate of mixed video
  PString  m_stillImage;          ///< JPEG, BMP or PNG for OpalStillImageVideoStrategy
#endif
  bool     m_mediaPassThru;       /**< Enable media pass through to optimise mixer node
                                       with precisely two attached connections. */

  PTimeInterval m_activeSpeakerDeadband; ///< Dead band time for active speaker switching

  PString m_displayText;          ///< Human readable text for conference name
  PString m_subject;              ///< Subject for conference
  PString m_notes;                ///< Free text about conference
  PString m_keywords;             ///< Space separated list of keywords for conference
};

struct OpalMixerNodeInfoDefault : public OpalMixerNodeInfo
{
  OpalMixerNodeInfoDefault(const char * name = NULL)
    : OpalMixerNodeInfo(name)
  { }
  OpalMixerNodeInfoDefault(const OpalMixerNodeInfo& other)
    : OpalMixerNodeInfo(other)
  { }
  virtual OpalMixerNodeInfo * Clone() const { return new OpalMixerNodeInfoDefault(*this); }
};

///////////////////////////////////////////////////////////////////////////////

class OpalMixerNode;
class OpalAudioStreamMixer;
class OpalVideoStreamMixer;


/** Mixer node manager.
    This class is a collection of OpalMixerNodes.
    It provides access to nodes by GUID or name.
  */
class OpalMixerNodeManager
{
  public:
  /**@name Construction */
  //@{
    /**Create a new mixer node manager.
     */
    OpalMixerNodeManager(OpalManager & manager);

    /**Destroy all mixer nodes.
       Calls ShutDown.
     */
    virtual ~OpalMixerNodeManager();

    /**Shuts down, removes and destroys all mixer nodes.
      */
    virtual void ShutDown();

    /** Execute garbage collection of nodes.
        Returns true if all garbage has been collected.
        Default behaviour deletes the objects that have been
        removed from the m_nodesByUID list.
      */
    virtual PBoolean GarbageCollection();
  //@}

  /**@name Operations */
  //@{
    /**Create a new node.
       This should create the new instance of the OpalMixerNode as required
       by the derived class, if any.
       The info variable should be created on the heap and it is subsequently
       owned by the node. NULL can be passed if defaults are to be used.
      */
    virtual OpalMixerNode * CreateNode(
      const OpalMixerNodeInfo & info ///< Initial info for node
    );

    /**Add a new node.
       The info variable should be created on the heap and it is subsequently
       owned by the node. NULL can be passed if defaults are to be used.
       Calls CreateNode.
      */
    virtual PSafePtr<OpalMixerNode> AddNode(
      const OpalMixerNodeInfo & info ///< Initial info for node
    );

    /**Add an existing node.
      */
    PSafePtr<OpalMixerNode> AddExistingNode(OpalMixerNode * node);

    /**Get the nodes.
      */
    PSafeArray<OpalMixerNode> GetNodes() const;

    /**Find a new node.
       This will search for the mixer node using GUID and then name.
      */
    virtual PSafePtr<OpalMixerNode> FindNode(
      const PString & name,             ///< GUID or alias name for node
      PSafetyMode mode = PSafeReference ///< Lock mode for returned pointer
    );

    /**Remove a node.
       Shut down all active connections with node, remove its name 
       associations and delete it.
      */
    virtual void RemoveNode(
      OpalMixerNode & node
    );

    /**Add node name to association list.
      */
    bool AddNodeName(
      PString name,        ///< alias name for node
      OpalMixerNode * node ///< node associated with name
    );

    /**Remove node's name from association list.
      */
    void RemoveNodeName(
      PString name        ///< alias name for node
    );

    /**Remove list of node names from association list.
       Commonly used when node destroyed.
      */
    void RemoveNodeNames(
      const PStringSet & names   ///< list of alias names for nodes
    );

    /**Internal function to get internal URI string.
      */
    virtual PString CreateInternalURI(
      const PGloballyUniqueID & guid
    );

    /**Call back when mixed node state information changes.

       Default behaviour does nothing.
      */
    virtual void OnNodeStatusChanged(
      const OpalMixerNode & node,            ///< Node that has changed state
      OpalConferenceState::ChangeType change ///< Change that occurred
    );

    /// Create the instance of the audio mixer
    virtual OpalAudioStreamMixer * CreateAudioMixer(OpalMixerNode & node);

#if OPAL_VIDEO
    /// Create the instance of the video mixer
    virtual OpalVideoStreamMixer * CreateVideoMixer(OpalMixerNode & node);
#endif

    /// Get manager
    OpalManager & GetManager() const { return m_manager; }
  //@}

  protected:
    OpalManager & m_manager;
    typedef PSafeDictionary<PGloballyUniqueID, OpalMixerNode> NodesByGUID;
    NodesByGUID m_nodesByGUID;
    typedef PSafeDictionary<PString, OpalMixerNode> NodesByName;
    NodesByName m_nodesByName;
};


///////////////////////////////////////////////////////////////////////////////

/** Mixer EndPoint.
    This class represents an endpoint that mixes media. It can be used as the
    basis for a Multipoint Conferencing Unit.
 */
class OpalMixerEndPoint : public OpalLocalEndPoint, public OpalMixerNodeManager
{
    PCLASSINFO(OpalMixerEndPoint, OpalLocalEndPoint);
  public:
  /**@name Construction */
  //@{
    /**Create a new endpoint.
     */
    OpalMixerEndPoint(
      OpalManager & manager,  ///<  Manager of all endpoints.
      const char * prefix = OPAL_MIXER_PREFIX  ///<  Prefix for URL style address strings
    );

    /**Destroy endpoint.
     */
    ~OpalMixerEndPoint();

    /**Shut down the endpoint, this is called by the OpalManager just before
       destroying the object and can be handy to make sure some things are
       stopped before the vtable gets clobbered.
      */
    virtual void ShutDown() override;
  //@}

  /**@name Overrides from OpalEndPoint */
  //@{
    /**Get the data formats this endpoint is capable of operating.
       This provides a list of media data format names that may be used by an
       OpalMediaStream may be created by a connection from this endpoint.

       Note that a specific connection may not actually support all of the
       media formats returned here, but should return no more.

       The default behaviour returns the most basic media formats, PCM audio
       and YUV420P video.
      */
    virtual OpalMediaFormatList GetMediaFormats() const override;

    /** Get available string option names.
      */
    virtual PStringList GetAvailableStringOptions() const override;

    /**Set up a connection to a remote party.
       This is called from the OpalManager::MakeConnection() function once
       it has determined that this is the endpoint for the protocol.

       The general form for this party parameter is:

            [proto:][alias@][transport$]address[:port]

       where the various fields will have meanings specific to the endpoint
       type. For example, with H.323 it could be "h323:Fred@site.com" which
       indicates a user Fred at gatekeeper size.com. Whereas for the PSTN
       endpoint it could be "pstn:5551234" which is to call 5551234 on the
       first available PSTN line.

       The proto field is optional when passed to a specific endpoint. If it
       is present, however, it must agree with the endpoints protocol name or
       false is returned.

       This function usually returns almost immediately with the connection
       continuing to occur in a new background thread.

       If false is returned then the connection could not be established. For
       example if a PSTN endpoint is used and the associated line is engaged
       then it may return immediately. Returning a non-NULL value does not
       mean that the connection will succeed, only that an attempt is being
       made.
     */
    virtual PSafePtr<OpalConnection> MakeConnection(
      OpalCall & call,           ///<  Owner of connection
      const PString & party,     ///<  Remote party to call
      void * userData = NULL,    ///<  Arbitrary data to pass to connection
      unsigned options = 0,      ///< Option bit mask to pass to connection
      OpalConnection::StringOptions * stringOptions = NULL ///< Options to pass to connection
    ) override;

    /**Get conference state information for all nodes.
       This obtains the state of one or more conferences managed by this
       endpoint. If this endpoint does not do conferencing, then false is
       returned.

       The \p name parameter may be one of the aliases for the conference, or
       the internal URI for the conference. An empty string indicates all 
       active conferences are to be returned.

       Note that if the \p name does not match an active conference, true is
       still returned, but the states list will be empty.

       The default behaviour returns false indicating this is not a
       conferencing endpoint.
      */
    virtual bool GetConferenceStates(
      OpalConferenceStates & states,          ///< List of conference states
      const PString & name = PString::Empty() ///< Name for specific node, empty string is all
    ) const override;

    /** Execute garbage collection for endpoint.
        Returns true if all garbage has been collected.
        Default behaviour deletes the objects in the connectionsActive list.
      */
    virtual PBoolean GarbageCollection() override;
  //@}

  /**@name Operations */
  //@{
    /**Find a connection that uses the specified token.
       This searches the endpoint for the connection that contains the token
       as provided by functions such as MakeConnection(). If not then it
       attempts to use the token as a OpalCall token and find a connection
       of the same class.
      */
    PSafePtr<OpalMixerConnection> GetMixerConnectionWithLock(
      const PString & token,     ///<  Token to identify connection
      PSafetyMode mode = PSafeReadWrite ///< Lock mode
    ) { return GetConnectionWithLockAs<OpalMixerConnection>(token, mode); }

    /**Create a connection for the PCSS endpoint.
       The default implementation is to create a OpalMixerConnection.
      */
    virtual OpalMixerConnection * CreateConnection(
      PSafePtr<OpalMixerNode> node, ///<  Node the connection is in
      OpalCall & call,              ///<  Owner of connection
      void * userData,              ///<  Arbitrary data to pass to connection
      unsigned options,             ///< Option bit mask to pass to connection
      OpalConnection::StringOptions * stringOptions ///< Options to pass to connection
    );

    /**Get Node Info for the given alias name.
       The pointer returned is not transferred to the caller, so should not be
       a clone or new instance.

       Default behaviour returns NULL, and m_adHocNodeInfo is used.
      */
    virtual OpalMixerNodeInfo * FindNodeInfo(
      const PString & name
    );
  //@}

  /**@name Member variable access */
  //@{
    /**Set default ad hoc node information.
       The pointer is passed to the CreateNode() function, so may be a
       reference to derived class, which a derived class of OpalMixerNode
       could use.

       Note if NULL, then ad hoc nodes are not created and incoming
       connections are refused. A user must explicitly call AddNode() to create
       a name that can be connected to.

       The version that takes a reference will utilise the Clone() function
       to create a copy of the mixer info.
      */
    void SetAdHocNodeInfo(
      const OpalMixerNodeInfo & info
    );

    /**Get default ad hoc mode information.
       The pointer returned from this function is passed to the CreateNode()
       function, so may be a reference to derived class, which a derived class
       of OpalMixerNode could use.

       Note if NULL, then ad hoc nodes are not created and incoming
       connections are refused. A user must explicitly call AddNode() to create
       a name that can be connected to.

       Default behaviour returns member variable m_adHocNodeInfo.
      */
    const OpalMixerNodeInfo & GetAdHocNodeInfo() { return *m_adHocNodeInfo; }

    /**Set factory node information.
       If an incoming connections destination address is the info->m_name,
       then a new node is created and the incoming call is forwarded to it.
       This is an alternative to enabling ad-hoc nodes and follows the
       RFC4353 model.

       The pointer is passed to the CreateNode() function, so may be a
       reference to derived class, which a derived class of OpalMixerNode
       could use.

       Note if NULL, then factory nodes are not created and incoming
       connections are refused. A user must explicitly call AddNode() to create
       a name that can be connected to.

       The version that takes a reference will utilise the Clone() function
       to create a copy of the mixer info.
      */
    void SetFactoryNodeInfo(
      const OpalMixerNodeInfo & info
    );

    /**Generate a new name when a factory generated node is created.
       The default is to append a monotonic increasing integer to the factory
       name.
      */
    virtual PString GetNewFactoryName();

    /// Get manager
    OpalManager & GetManager() const { return OpalLocalEndPoint::GetManager(); }

    /**Call back when mixed node state information changes.

       Default behaviour does nothing.
      */
    virtual void OnNodeStatusChanged(
      const OpalMixerNode & node,            ///< Node that has changed state
      OpalConferenceState::ChangeType change ///< Change that occurred
    ) override;
  //@}

  protected:
    virtual PString CreateInternalURI(const PGloballyUniqueID & guid) override;

    PAutoPtr<OpalMixerNodeInfo> m_adHocNodeInfo;
    PAutoPtr<OpalMixerNodeInfo> m_factoryNodeInfo;
    PDECLARE_MUTEX(m_infoMutex); // For above two fields
    atomic<uint32_t> m_factoryIndex;
};


///////////////////////////////////////////////////////////////////////////////

/** Mixer connection.
 */
class OpalMixerConnection : public OpalLocalConnection
{
    PCLASSINFO(OpalMixerConnection, OpalLocalConnection);
  public:
  /**@name Construction */
  //@{
    /**Create a new connection.
     */
    OpalMixerConnection(
      PSafePtr<OpalMixerNode> node, ///<  Node the connection is in
      OpalCall & call,              ///<  Owner call for connection
      OpalMixerEndPoint & endpoint, ///<  Owner endpoint for connection
      void * userData,              ///<  Arbitrary data to pass to connection
      unsigned options = 0,         ///< Option bit map to be passed to connection
      OpalConnection::StringOptions * stringOptions = NULL ///< Options to be passed to connection
    );

    /**Destroy connection.
     */
    ~OpalMixerConnection();
  //@}

  /**@name Overrides from OpalConnection */
  //@{
    /**Clean up the termination of the connection.
       This function can do any internal cleaning up and waiting on background
       threads that may be using the connection object.

       Note that there is not a one to one relationship with the
       OnEstablishedConnection() function. This function may be called without
       that function being called. For example if SetUpConnection() was used
       but the call never completed.

       Classes that override this function should make sure they call the
       ancestor version for correct operation.

       An application will not typically call this function as it is used by
       the OpalManager during a release of the connection.

       The default behaviour calls the OpalEndPoint function of the same name.
      */
    virtual void OnReleased() override;

    /**Get the data formats this connection is capable of operating.
       This provides a list of media data format names that a
       OpalMediaStream may be created in within this connection.

       The default behaviour calls GetMediaFormats() on the endpoint.
      */
    virtual OpalMediaFormatList GetMediaFormats() const override;

    /**Open a new media stream.
       This will create a media stream of an appropriate subclass as required
       by the underlying connection protocol. For instance H.323 would create
       an OpalRTPStream.

       The sessionID parameter may not be needed by a particular media stream
       and may be ignored. In the case of an OpalRTPStream it us used.

       Note that media streams may be created internally to the underlying
       protocol. This function is not the only way a stream can come into
       existence.

       The default behaviour is pure.
     */
    virtual OpalMediaStream * CreateMediaStream(
      const OpalMediaFormat & mediaFormat, ///<  Media format for stream
      unsigned sessionID,                  ///<  Session number for stream
      PBoolean isSource                    ///<  Is a source stream
    ) override;

    /**Call back when media stream patch thread starts.
      */
    virtual void OnStartMediaPatch(
      OpalMediaPatch & patch    ///< Patch being started
    ) override;

    /// Call back for connection to act on changed string options
    virtual void OnApplyStringOptions() override;

    /**Send a user input indication to the remote endpoint.
       This is for sending arbitrary strings as user indications.

       The default behaviour is to call SendUserInputTone() for each character
       in the string.
      */
    virtual PBoolean SendUserInputString(
      const PString & value                   ///<  String value of indication
    ) override;

    /**Send a user input indication to the remote endpoint.
       This sends DTMF emulation user input. If something more sophisticated
       than the simple tones that can be sent using the SendUserInput()
       function.

       A duration of zero indicates that no duration is to be indicated.
       A non-zero logical channel indicates that the tone is to be synchronised
       with the logical channel at the rtpTimestamp value specified.

       The tone parameter must be one of "0123456789#*ABCD!" where '!'
       indicates a hook flash. If tone is a ' ' character then a
       signalUpdate PDU is sent that updates the last tone indication
       sent. See the H.245 specification for more details on this.

       The default behaviour sends the tone using RFC2833.
      */
    virtual PBoolean SendUserInputTone(
      char tone,        ///<  DTMF tone code
      unsigned duration = 0  ///<  Duration of tone in milliseconds
    ) override;

    /**Get Conference state information.
       This obtains the state information about a conference this connection
       is directly a part of. If the connection type does not embody a
       conference then false is returned.

       The \p state parameter, if non-NULL, is filled with the state of the
       conference. When NULL, this just indicates that the connection is
       part of a conference with the return value.

       Default behaviour is to return false which indicates this connection is
       not part of a conference.
      */
    virtual bool GetConferenceState(
      OpalConferenceState * state  ///< Optional conference state information
    ) const override;
  //@}

  /**@name Operations */
  //@{
    /**Set this connection to listen only mode.
      */
    void SetListenOnly(
      bool listenOnly   ///< New listen only state.
    );

    /**Get flag for this connection is in listen only mode.
      */
    bool GetListenOnly() const { return m_listenOnly; }

    /**Get the node that this connection is being mixed in.
      */
    PSafePtr<OpalMixerNode> GetNode() const { return m_node; }

    /** Get the roles that this connection has in the conference.
      */
    virtual PStringSet GetRoles() const;
  //@}

  protected:
    OpalMixerEndPoint     & m_endpoint;
    PSafePtr<OpalMixerNode> m_node;
    bool                    m_listenOnly;
};


/**Mixer media stream.
   This class represents a media stream that will send/get media from a mixer.
 */
class OpalMixerMediaStream : public OpalMediaStream
{
    PCLASSINFO(OpalMixerMediaStream, OpalMediaStream);
  public:
  /**@name Construction */
  //@{
    /**Construct a new media stream for mixer.
      */
    OpalMixerMediaStream(
      OpalConnection & conn,               ///<  Connection for media stream
      const OpalMediaFormat & mediaFormat, ///<  Media format for stream
      unsigned sessionID,                  ///<  Session number for stream
      bool isSource,                       ///<  Is a source stream
      PSafePtr<OpalMixerNode> node,        ///<  Mixer node to send data
      bool listenOnly                      ///<  Effectively initial pause state
    );

    /**Destroy stream.
      */
    ~OpalMixerMediaStream();
  //@}

  /**@name Overrides of OpalMediaStream class */
  //@{
    /**Open the media stream using the media format.
      */
    virtual PBoolean Open() override;

    /**Write an RTP frame of data to the sink media stream.
       The default behaviour simply calls WriteData() on the data portion of the
       RTP_DataFrame and and sets the internal timestamp and marker from the
       member variables of the media stream class.
      */
    virtual PBoolean WritePacket(
      RTP_DataFrame & packet
    ) override;

    /**Pushes a frame to the patch
      */
    virtual bool PushPacket(
      RTP_DataFrame & packet
    ) override;

    /**Indicate if the media stream is synchronous.
       Returns true for LID streams.
      */
    virtual PBoolean IsSynchronous() const override;

    /**Indicate if the media stream requires a OpalMediaPatch thread (active patch).
       This is called on the source/sink stream and is passed the sink/source
       stream that the patch will initially be using. The function could
       conditionally require the patch thread to execute a thread reading and
       writing data, or prevent  it from doing so as it can do so in hardware
       in some way.

       The default behaviour returns true if a sink stream. If source stream
       then threading is from the mixer class.
      */
    virtual PBoolean RequiresPatchThread() const override;
  //@}

  /**@name Member variable access */
  //@{
    /**Get the mixer node for this stream.
     */
    PSafePtr<OpalMixerNode> GetNode() { return m_node; }
  //@}

#if OPAL_VIDEO
    bool CheckMixedVideoSize(unsigned width, unsigned height);
    void KeyFrameNeeded() { m_needsKeyFrame.exchange(true); }
    bool NeedsKeyFrame() { return m_needsKeyFrame.exchange(false); }
#endif

  protected:
    virtual void InternalClose() override;
    virtual bool InternalSetJitterBuffer(const OpalJitterBuffer::Init & init) override;
    virtual bool InternalExecuteCommand(const OpalMediaCommand & command) override;

    PSafePtr<OpalMixerNode> m_node;
    bool m_listenOnly;
#if OPAL_VIDEO
    unsigned m_mixedVideoWidth;
    unsigned m_mixedVideoHeight;
    std::atomic<bool> m_needsKeyFrame;
#endif
    RTP_DataFrame m_txPacket;
};


class OpalMediaStreamMixer
{
  public:
    OpalMediaStreamMixer(OpalMixerNode & node);
    void Append(const PSafePtr<OpalMixerMediaStream> & stream);
    void Remove(const PSafePtr<OpalMixerMediaStream> & stream) { m_outputStreams.RemoveAt(stream->GetID()); }
    void CloseOne(const PSafePtr<OpalMixerMediaStream> & stream);

  protected:
    OpalMixerNode & m_node;

    typedef PSafeDictionary<PString, OpalMixerMediaStream> StreamDict;
    StreamDict m_outputStreams;
};

/** Audio mixer.
    This class represents the store for the mixed audio.
*/
class OpalAudioStreamMixer : public OpalAudioMixer, public OpalMediaStreamMixer
{
    PCLASSINFO(OpalAudioStreamMixer, OpalAudioMixer);
  public:
    OpalAudioStreamMixer(OpalMixerNode & node);
    ~OpalAudioStreamMixer() { StopPushThread(); }

    virtual bool OnPush() override;

  protected:
    struct CachedAudio
    {
      CachedAudio();
      CachedAudio(const CachedAudio & other);
      void operator=(const CachedAudio &) { PAssertAlways(PLogicError); }
      CachedAudio(CachedAudio && other);
      void operator=(CachedAudio &&) { PAssertAlways(PLogicError); }
      ~CachedAudio();

      enum {
        Collecting, Collected, Completed
      } m_state;
      RTP_DataFrame m_raw;
      RTP_DataFrame m_encoded;
      OpalTranscoder * m_transcoder;
    };
    std::map<PString, CachedAudio> m_cache;

    void PushOne(
      PSafePtr<OpalMixerMediaStream> & stream,
      CachedAudio & cache,
      const short * audioToSubtract
    );

#ifdef OPAL_MIXER_AUDIO_DEBUG
    class PAudioMixerDebug * m_audioDebug;
#endif
};


#if OPAL_VIDEO
/** Video mixer.
    This class represents the frame store for the mixed video.

    A user would typically create a derived class to override the MixVideo()
    or the StartMix()/NextMix() virtual functions for new screen
    patterns/algorithms.
*/
class OpalVideoStreamMixer : public OpalVideoMixer, public OpalMediaStreamMixer
{
    PCLASSINFO(OpalVideoStreamMixer, OpalVideoMixer);
  public:
    OpalVideoStreamMixer(OpalMixerNode & node);
    ~OpalVideoStreamMixer() { StopPushThread(); }

    virtual bool SetFrameRate(unsigned rate) override;
    virtual bool OnMixed(std::unique_ptr<RTP_DataFrame> & output) override;
    virtual Key_T GetActiveSpeaker() const override;
    virtual void SetForceTranscode(bool on);
    OpalMixerNode & GetMixerNode() const { return m_node; }

  protected:
    virtual void OnStrategyPublished() override;

    // A missing sink is the interval before the RTP patch is attached. Closing
    // the stream there drops the negotiated codec. Ask for another key frame
    // and try again on the next tick.
    bool PushMixedPacket(const PSafePtr<OpalMixerMediaStream> & stream, RTP_DataFrame & packet);

  protected:
    typedef PDictionary<PString, OpalTranscoder> TranscoderMap;
    TranscoderMap m_transcoders;
};
#endif // OPAL_VIDEO


/** Mixer node.
    This class represents a group of connections that are being mixed.
  */
class OpalMixerNode : public PSafeObject
{
    PCLASSINFO(OpalMixerNode, PSafeObject);
  public:
  /**@name Construction */
  //@{
    /**Create a new node.
     */
    OpalMixerNode(
      OpalMixerNodeManager & manager, ///< Manager for this node
      const OpalMixerNodeInfo & info  ///< Configuration information
    );

    /**Destroy node.
     */
    ~OpalMixerNode();

    /**Shut down node.
       This clears all attached connections, removes all names and generally
       shuts the node down.
      */
    void ShutDown();
  //@}

  /**@name Overrides from PObject */
  //@{
    /**Standard stream print function.
       The PObject class has a << operator defined that invokes this function
       polymorphically.
      */
    void PrintOn(
      ostream & strm    ///<  Stream to output text representation
    ) const override;
  //@}

  /**@name Operations */
  //@{
    /**Attach a connection.
      */
    virtual void AttachConnection(
      OpalMixerConnection * connection  ///< Connection to attach
    );

    /**Detach a connection.
      */
    virtual void DetachConnection(
      OpalMixerConnection * connection  ///< Connection to detach
    );

    /**Attach a stream for output.
      */
    virtual bool AttachStream(
      OpalMixerMediaStream * stream     ///< Stream to attach
    );

    /**Detach a stream for output.
      */
    virtual void DetachStream(
      OpalMixerMediaStream * stream     ///< Stream to detach
    );

    /**Use media bypass if applicable.
      */
    virtual void UseMediaPassThrough(
      unsigned sessionID,                 ///< Session ID to bypass, 0 indicates all
      OpalConnection * connection = NULL  ///< Just deleted connection
    );

    /**Sets the size of the jitter buffer to be used by the specified stream
       in this mixer. A mixer defaults to not having any jitter buffer enabled.

       If either jitter delay parameter is zero, it destroys the jitter buffer
       attached to this mixer.
      */
    bool SetJitterBufferSize(
      const OpalBaseMixer::Key_T & key,     ///< key for mixer stream
      const OpalJitterBuffer::Init & init   ///< Initialisation information
    );

    /**Write data to mixer.
      */
    bool WritePacket(
      const OpalMixerMediaStream & stream,  ///< key for mixer stream
      const RTP_DataFrame & input           ///< Input RTP data for media
    );

    /**Send a user input indication to all connections.
      */
    virtual void BroadcastUserInput(
      const OpalConnection * connection,      ///<  Connection NOT to send to
      const PString & value                   ///<  String value of indication
    );

    /**Get Conference state information.
       This obtains the state information about a conference this connection
       is directly a part of. If the connection type does not embody a
       conference then false is returned.

       The \p state parameter, if non-NULL, is filled with the state of the
       conference. When NULL, this just indicates that the connection is
       part of a conference with the return value.

       Default behaviour is to return false which indicates this connection is
       not part of a conference.
      */
    virtual void GetConferenceState(
      OpalConferenceState & state  ///< Conference state information
    ) const;

#if OPAL_VIDEO
    /**Publish the video strategy for the current participant count and apply it
       after the mixer mutex is released.
      */
    void UpdateVideoDelivery(bool force = false);

    /**Link or clear two-party video pass-through. Audio stays mixed.
       waiting is set when the video streams are not open yet.
      */
    bool TryVideoPairRelay(bool enable, bool & waiting);

    /**Encoded relay for three or four participants. False means the formats
       do not match or a session could not be opened, and the caller falls
       back to a transcoded mix. True with waiting means try again when media starts.
      */
    bool StartMultiStreamRelay(bool & waiting);
    void StopMultiStreamRelay();

    /**True when every participant already has a video stream for each of the others.
      */
    bool MultiStreamReady() const;
    void SetForceTranscode(bool on);
    void RequestVideoKeyFrames();
    bool RelayEncodedVideo() const { return m_relayEncodedVideo.load(); }
    bool EncodedRelayActive() const { return m_relayEncodedVideo.load() && m_relayPartyCount > 0; }
    bool ApplyMultiStreamRelay();
#endif
  //@}

  /**@name Member variable access */
  //@{
    /**Get globally unique identifier for node.
      */
    const PGloballyUniqueID & GetGUID() const { return m_guid; }

    /**Get list of names for this node.
      */
    const PStringSet & GetNames() const { return m_names; }

    /**Add a name for this node.
      */
    bool AddName(
      const PString & name
    );

    /**Remove a name for this node.
      */
    void RemoveName(
      const PString & name
    );

    /**Get count of connections.
       Note that as this value can change at any moment, it is really not
       that useful and should definitely not be used for enumeration of the
       connections.
      */
    PINDEX GetConnectionCount() const { return m_connections.GetSize(); }

    /**Get connections in the mixer node
      */
    PSafeArray<OpalMixerConnection> GetConnections() const { return m_connections; }

    /**Get connections in the mixer node
      */
    PSafePtr<OpalMixerConnection> GetConnectionFromStreamId(const PString & id) const;

    /**Get the raw audio accumulation buffer.
     */
    const OpalMixerNodeInfo & GetNodeInfo() const { return *m_info; }

    /**Get the creation time of the node.
     */
    const PTime & GetCreationTime() const { return m_creationTime; }

    /**Set the owner connection.
       If a connection with GetToken(), GetLocalPartyURL() or
       GetRemotePartyURL() equal to \p connectionIdentifier disconnects from
       the node, then the node is shut down and all other participants
       disconnected.
      */
    void SetOwnerConnection(
      const PString & connectionIdentifier  // Connection for owner of the node
    );

    /** Set the active speaker stream identifier.
      */
    virtual void SetActiveSpeaker(
      const PString & streamId    /// OpalMediaStream identifier for active speaker
    );

    /** Get the active speaker stream identifier.
      */
    PString GetActiveSpeakerStreamId() const;

    /** Get the active speaker connection URL.
      */
    PString GetActiveSpeakerURL() const;
  //@}

  protected:
    OpalMixerNodeManager  & m_manager;
    const PGloballyUniqueID m_guid;
    PStringSet              m_names;
    const PTime             m_creationTime;
    atomic<bool>            m_shuttingDown;

    const PAutoPtr<OpalMixerNodeInfo> m_info;

    PSafeArray<OpalMixerConnection> m_connections;
    PString                         m_ownerConnection;

    std::shared_ptr<OpalAudioStreamMixer> m_audioMixer;
#if OPAL_VIDEO
    typedef std::map<OpalVideoFormat::ContentRole, std::shared_ptr<OpalVideoStreamMixer>> VideoMixerMap;
    VideoMixerMap m_videoMixers;
#endif // OPAL_VIDEO

    typedef std::map<PString, std::shared_ptr<OpalBaseMixer>> MixerByIdMap;
    MixerByIdMap m_mixerById;

    PString m_activeSpeakerStreamId;
    PString m_activeSpeakerURL;

#if OPAL_VIDEO
    atomic<bool> m_applyingDelivery;
    atomic<bool> m_deliveryDirty;
    atomic<bool> m_relayEncodedVideo;
    atomic<bool> m_relayInProgress;
    atomic<bool> m_relayApplyPending;
    atomic<bool> m_relayAwaitingKeyFrame;
    bool ArmEncodedRelay(bool & waiting);
    unsigned m_relayPartyCount;
    PDECLARE_MUTEX(m_relayMutex);
    std::map<PString, PString> m_relaySourceOf;

    void ForwardEncodedVideo(const OpalMixerMediaStream & from, const RTP_DataFrame & packet);
    void RenegotiateParticipants(bool separateBundle);
    void CloseVideoStreams(OpalConnection & connection);
    void ReopenMixedVideo();
#endif
};


#endif // OPAL_HAS_MIXER

#endif // OPAL_OPAL_OPAL_MIXER


///////////////////////////////////////////////////////////////////////////////
