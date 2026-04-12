/*
 * main.h
 *
 * OPAL application source file for testing codecs
 *
 * Copyright (c) 2007 Equivalence Pty. Ltd.
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
 * The Original Code is Portable Windows Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#ifndef _CodecTest_MAIN_H
#define _CodecTest_MAIN_H

#include <codec/vidcodec.h>
#include <opal/patch.h>
#include <rtp/pcapfile.h>

class TranscoderThread : public PThread
{
  public:
    TranscoderThread(unsigned num, const char * name)
      : PThread(5000, NoAutoDeleteThread, NormalPriority, name)
      , m_running(false)
      , m_num(num)
      , m_timestamp(0)
      , m_markerHandling(NormalMarkers)
      , m_dropPercent(0)
    {
    }

    ~TranscoderThread()
    {
    }

    int InitialiseCodec(PArgList & args, const OpalMediaType & mediaType, OpalMediaFormat & mediaFormat, OpalMediaFormat & rawFormat);

    virtual void Main() override;

    virtual bool Read(RTP_DataFrame & frame) = 0;
    virtual bool Write(const RTP_DataFrame & frame) = 0;
    void Start()
    {
      if (m_running)
        Resume();
    }
    virtual void Stop() = 0;

    virtual void UpdateStats(const RTP_DataFrame &) { }

    virtual void CalcSNR(const RTP_DataFrame & /*src*/, const RTP_DataFrame & /*dst*/)
    {  }

    virtual void ReportSNR()
    {  }

    bool m_running;

    PSyncPointAck m_pause;
    PSyncPoint    m_resume;

    std::unique_ptr<OpalTranscoder> m_encoder, m_encoder2;
    std::unique_ptr<OpalTranscoder> m_decoder, m_decoder2;
    unsigned         m_num;
    DWORD            m_timestamp;
    enum MarkerHandling {
      SuppressMarkers,
      ForceMarkers,
      NormalMarkers
    } m_markerHandling;

    bool m_forceIFrame;

    int  m_framesToTranscode;
    int  m_frameTime;
    bool m_calcSNR;
    BYTE m_headerExtension;
    uint32_t m_dropPercent;

    OpalPCAPFile m_pcapFile;
};


class AudioThread : public TranscoderThread
{
  public:
    AudioThread(unsigned _num)
      : TranscoderThread(_num, "Audio")
      , m_readSize(0)
    {
    }

    bool Initialise(PArgList & args);

    virtual void Main() override;
    virtual bool Read(RTP_DataFrame & frame) override;
    virtual bool Write(const RTP_DataFrame & frame) override;
    virtual void Stop() override;

    std::unique_ptr<PSoundChannel> m_recorder;
    std::unique_ptr<PSoundChannel> m_player;
    PINDEX                         m_readSize;
};


#if OPAL_VIDEO
class VideoThread : public TranscoderThread
{
  public:
    VideoThread(unsigned num)
      : TranscoderThread(num, "Video")
      , m_singleStep(false)
      , m_frameCount(0)
      , m_frameStartTimestamp(0)
      , m_snrWidth(0)
      , m_snrHeight(0)
      , m_sumYSNR(0)
      , m_sumCbSNR(0)
      , m_sumCrSNR(0)
      , m_snrCount(0)
    {
    }

    bool Initialise(PArgList & args);

    virtual void Main() override;
    virtual bool Read(RTP_DataFrame & frame) override;
    virtual bool Write(const RTP_DataFrame & frame) override;
    virtual void Stop() override;

    PDECLARE_NOTIFIER(OpalMediaCommand, VideoThread, OnTranscoderCommand);
    void WriteFrameStats(const PString & str);

    std::unique_ptr<PVideoInputDevice> m_grabber;
    std::unique_ptr<PVideoOutputDevice> m_display;

    bool                 m_singleStep;
    PSemaphore           m_frameWait;
    PTimeInterval        m_lastGrabTime;

    void SaveSNRFrame(const RTP_DataFrame & src);
    void CalcSNR(const RTP_DataFrame & src);
    void CalcSNR(const RTP_DataFrame & src, const RTP_DataFrame & dst) override;
    void ReportSNR() override;

    PString   m_frameFilename;
    PTextFile m_frameStatFile;
    PInt64    m_frameCount;
    DWORD     m_frameStartTimestamp;
    std::queue<const RTP_DataFrame *> m_snrSourceFrames;

    unsigned m_snrWidth, m_snrHeight;
    double   m_sumYSNR;
    double   m_sumCbSNR;
    double   m_sumCrSNR;
    PInt64   m_snrCount;
};
#endif


class CodecTest : public PProcess
{
  PCLASSINFO(CodecTest, PProcess)

  public:
    CodecTest();
    ~CodecTest();

    virtual void Main() override;

    class TestThreadInfo : public PObject
    {
      public:
        AudioThread m_audio;

#if OPAL_VIDEO
        VideoThread m_video;

        TestThreadInfo(unsigned num) :            m_audio(num),                 m_video(num) { }
        bool Initialise(PArgList & args) { return m_audio.Initialise(args)  &&  m_video.Initialise(args); }
        void Start()                     {        m_audio.Start();              m_video.Start(); }
        void Stop()                      {        m_audio.Stop();               m_video.Stop(); }
        void Wait()                      {        m_audio.WaitForTermination(); m_video.WaitForTermination(); }

#else
        TestThreadInfo(unsigned num) :            m_audio(num) { }
        bool Initialise(PArgList & args) { return m_audio.Initialise(args); }
        void Start()                     {        m_audio.Start(); }
        void Stop()                      {        m_audio.Stop(); }
        void Wait()                      {        m_audio.WaitForTermination(); }
#endif
    };
};


#endif  // _CodecTest_MAIN_H


// End of File ///////////////////////////////////////////////////////////////
