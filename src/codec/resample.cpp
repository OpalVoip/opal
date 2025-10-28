/*
 * resample.cxx
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
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "resample.h"
#endif

#include <opal_config.h>

#include <codec/resample.h>

#if P_FFMPEG
  P_PUSH_MSVC_WARNINGS(4244)
  P_PUSH_GCC_WARNING("-Wconversion")
  extern "C" {
    #include <libswresample/swresample.h>
  };
  P_POP_MSVC_WARNINGS()
  P_POP_GCC_WARNING()
#endif // P_FFMPEG


OpalResample::OpalResample(const OpalMediaFormat & inputMediaFormat,
                           const OpalMediaFormat & outputMediaFormat)
  : OpalTranscoder(inputMediaFormat, outputMediaFormat)
  , m_outSampleRate(outputMediaFormat.GetClockRate())
  , m_inSampleRate(inputMediaFormat.GetClockRate())
  , m_outSampleSize(sizeof(int16_t)* outputMediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption()))
  , m_inSampleSize(sizeof(int16_t)* inputMediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption()))
  {
#if P_FFMPEG
  m_bufferLength = 0;

  AVChannelLayout out_layout, in_layout;
  av_channel_layout_default(&out_layout, m_outSampleSize/sizeof(int16_t));
  av_channel_layout_default(&in_layout, m_inSampleSize/sizeof(int16_t));

  m_swrContext = NULL;
  int error = swr_alloc_set_opts2(&m_swrContext,
                                  &out_layout, AV_SAMPLE_FMT_S16, m_outSampleRate,
                                  &in_layout, AV_SAMPLE_FMT_S16, m_inSampleRate, 0, NULL);
  if (error == 0 && (error = swr_init(m_swrContext)) == 0)
    return;

  PTRACE(1, "Cannot allocate/create resampler (error=" << error << ") from "
          << inputMediaFormat << " to " << outputMediaFormat);
  swr_free(&m_swrContext);
#endif
}


OpalResample::~OpalResample()
{
#if P_FFMPEG
  swr_free(&m_swrContext);
#endif
}


PINDEX OpalResample::GetOptimalDataFrameSize(PBoolean input) const
{
  // Default to 20ms, the most commonly used size
  PWaitAndSignal mutex(updateMutex);
  return (input ? m_inSampleRate : m_outSampleRate) * 20 / 1000 * (input ? m_inSampleSize : m_outSampleSize);
}


PBoolean OpalResample::ConvertFrames(const RTP_DataFrame & input, RTP_DataFrameList & output)
{
  PINDEX inputBytes = input.GetPayloadSize();
  int inSamples = inputBytes / m_inSampleSize;
  PINDEX outputBytes = inputBytes * m_outSampleRate / m_inSampleRate;

#if P_FFMPEG
  if (m_swrContext) {
    int outSamples = swr_get_out_samples(m_swrContext, inSamples);
    if (outSamples < 0) {
      PTRACE(2, "Resample error: " << outSamples);
      return false;
    }

    size_t requiredSize = m_bufferLength + outSamples*m_outSampleSize;
    if (m_buffer.size() < requiredSize)
      m_buffer.resize(requiredSize);

    uint8_t * outPtr = m_buffer.data() + m_bufferLength;
    const uint8_t * inPtr = input.GetPayloadPtr();
    auto result = swr_convert(m_swrContext, &outPtr, outSamples, &inPtr, inSamples);
    if (result < 0) {
      PTRACE(2, "Resample error: " << result);
      return false;
    }
    m_bufferLength += result*m_outSampleSize;

    if (m_bufferLength < outputBytes)
      output.clear();
    else {
      if (output.empty())
        output.Append(new RTP_DataFrame(outputBytes));
      output.front().SetPayload(m_buffer.data(), outputBytes);
      memcpy(m_buffer.data(), m_buffer.data() + outputBytes, m_bufferLength - outputBytes);
      m_bufferLength -= outputBytes;
    }

    return true;
  }
#endif

  if (output.empty())
    output.Append(new RTP_DataFrame(outputBytes));

  RTP_DataFrame & frame = output.front();
  frame.SetPayloadSize(outputBytes);
  const int16_t * inPtr = (const int16_t *)input.GetPayloadPtr();
  int16_t * outPtr = (int16_t *)frame.GetPayloadPtr();

  // Naive method, quality is not that great
  if (m_inSampleRate < m_outSampleRate) {
    unsigned ratio = m_outSampleRate / m_inSampleRate;
    while (inSamples-- > 0) {
      for (unsigned i = 0; i < ratio; ++i)
        *outPtr++ = *inPtr;
      inPtr++;
    }
  }
  else {
    unsigned ratio = m_inSampleRate / m_outSampleRate;
    while (inSamples > 0) {
      *outPtr++ = *inPtr;
      inSamples -= ratio;
      inPtr += ratio;
    }
  }

  return true;
}

PBoolean OpalResample::Convert(const RTP_DataFrame &, RTP_DataFrame &)
{
  // Never executed due to overriding ConvertFrames
  return false;
}
