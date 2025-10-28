/*
 * resample.h
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2001 Equivalence Pty. Ltd.
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
 */

#ifndef OPAL_CODEC_RESAMPLE_H
#define OPAL_CODEC_RESAMPLE_H

#ifdef P_USE_PRAGMA
#pragma interface
#endif

#include <opal_config.h>

#include <opal/transcoders.h>


///////////////////////////////////////////////////////////////////////////////

class OpalResample : public OpalTranscoder
{
public:
  OpalResample(
    const OpalMediaFormat & inputMediaFormat,  ///<  Input media format
    const OpalMediaFormat & outputMediaFormat  ///<  Output media format
  );
  ~OpalResample();

  virtual PINDEX GetOptimalDataFrameSize(
    PBoolean input      ///<  Flag for input or output data size
  ) const override;

  virtual PBoolean ConvertFrames(
    const RTP_DataFrame & input,  ///<  Input data
    RTP_DataFrameList & output    ///<  Output data
  ) override;
  virtual PBoolean Convert(
    const RTP_DataFrame & input,  ///<  Input data
    RTP_DataFrame & output        ///<  Output data
  ) override;

protected:
  unsigned m_outSampleRate;
  unsigned m_inSampleRate;
  size_t m_outSampleSize;
  size_t m_inSampleSize;
#if P_FFMPEG
  struct SwrContext * m_swrContext;
  std::vector<uint8_t> m_buffer;
  size_t m_bufferLength;
#endif
};


///////////////////////////////////////////////////////////////////////////////

#define OPAL_REGISTER_RESAMPLER(from_khz, from_stereo, to_khz, to_stereo) \
  struct OpalResample_##from_khz##from_stereo##_##to_khz##to_stereo : OpalResample {\
    OpalResample_##from_khz##from_stereo##_##to_khz##to_stereo() : \
      OpalResample(OPAL_PCM16##from_stereo##_##from_khz##KHZ, OPAL_PCM16##to_stereo##_##to_khz##KHZ) { } \
  }; \
  OPAL_REGISTER_TRANSCODER(OpalResample_##from_khz##from_stereo##_##to_khz##to_stereo, \
                           OPAL_PCM16##from_stereo##_##from_khz##KHZ, OPAL_PCM16##to_stereo##_##to_khz##KHZ)

#if P_FFMPEG

#define OPAL_REGISTER_RESAMPLERS() \
  OPAL_REGISTER_RESAMPLER( 8, , 16, ) \
  OPAL_REGISTER_RESAMPLER( 8, , 32, ) \
  OPAL_REGISTER_RESAMPLER( 8, , 48, ) \
  OPAL_REGISTER_RESAMPLER(16, ,  8, ) \
  OPAL_REGISTER_RESAMPLER(16, , 32, ) \
  OPAL_REGISTER_RESAMPLER(16, , 48, ) \
  OPAL_REGISTER_RESAMPLER(32, ,  8, ) \
  OPAL_REGISTER_RESAMPLER(32, , 16, ) \
  OPAL_REGISTER_RESAMPLER(32, , 48, ) \
  OPAL_REGISTER_RESAMPLER(48, ,  8, ) \
  OPAL_REGISTER_RESAMPLER(48, , 16, ) \
  OPAL_REGISTER_RESAMPLER(48, , 32, ) \
  OPAL_REGISTER_RESAMPLER( 8,S,  8, ) \
  OPAL_REGISTER_RESAMPLER( 8,S, 16, ) \
  OPAL_REGISTER_RESAMPLER( 8,S, 32, ) \
  OPAL_REGISTER_RESAMPLER( 8,S, 48, ) \
  OPAL_REGISTER_RESAMPLER(16,S,  8, ) \
  OPAL_REGISTER_RESAMPLER(16,S, 16, ) \
  OPAL_REGISTER_RESAMPLER(16,S, 32, ) \
  OPAL_REGISTER_RESAMPLER(16,S, 48, ) \
  OPAL_REGISTER_RESAMPLER(32,S,  8, ) \
  OPAL_REGISTER_RESAMPLER(32,S, 16, ) \
  OPAL_REGISTER_RESAMPLER(32,S, 32, ) \
  OPAL_REGISTER_RESAMPLER(32,S, 48, ) \
  OPAL_REGISTER_RESAMPLER(48,S,  8, ) \
  OPAL_REGISTER_RESAMPLER(48,S, 16, ) \
  OPAL_REGISTER_RESAMPLER(48,S, 32, ) \
  OPAL_REGISTER_RESAMPLER(48,S, 48, ) \
  OPAL_REGISTER_RESAMPLER( 8, ,  8,S) \
  OPAL_REGISTER_RESAMPLER( 8, , 16,S) \
  OPAL_REGISTER_RESAMPLER( 8, , 32,S) \
  OPAL_REGISTER_RESAMPLER( 8, , 48,S) \
  OPAL_REGISTER_RESAMPLER(16, ,  8,S) \
  OPAL_REGISTER_RESAMPLER(16, , 16,S) \
  OPAL_REGISTER_RESAMPLER(16, , 32,S) \
  OPAL_REGISTER_RESAMPLER(16, , 48,S) \
  OPAL_REGISTER_RESAMPLER(32, ,  8,S) \
  OPAL_REGISTER_RESAMPLER(32, , 16,S) \
  OPAL_REGISTER_RESAMPLER(32, , 32,S) \
  OPAL_REGISTER_RESAMPLER(32, , 48,S) \
  OPAL_REGISTER_RESAMPLER(48, ,  8,S) \
  OPAL_REGISTER_RESAMPLER(48, , 16,S) \
  OPAL_REGISTER_RESAMPLER(48, , 32,S) \
  OPAL_REGISTER_RESAMPLER(48, , 48,S) \

#else

#define OPAL_REGISTER_RESAMPLERS() \
  OPAL_REGISTER_RESAMPLER( 8, , 16, ) \
  OPAL_REGISTER_RESAMPLER( 8, , 48, ) \
  OPAL_REGISTER_RESAMPLER(16, ,  8, ) \
  OPAL_REGISTER_RESAMPLER(16, , 48, ) \
  OPAL_REGISTER_RESAMPLER(48, ,  8, ) \
  OPAL_REGISTER_RESAMPLER(48, , 16, ) \

#endif

#endif // OPAL_CODEC_RESAMPLE_H

/////////////////////////////////////////////////////////////////////////////
