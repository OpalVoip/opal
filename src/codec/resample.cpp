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

  #ifdef P_SWRESAMPLE_LIB
    #pragma comment(lib, P_SWRESAMPLE_LIB)
  #endif
#endif // P_FFMPEG


OpalResample::OpalResample(const OpalMediaFormat & inputMediaFormat,
                           const OpalMediaFormat & outputMediaFormat)
  : OpalTranscoder(inputMediaFormat, outputMediaFormat)
  , m_outSampleRate(outputMediaFormat.GetClockRate())
  , m_inSampleRate(inputMediaFormat.GetClockRate())
{
#if P_FFMPEG
  m_bufferLength = 0;

  unsigned outChannels = outputMediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption());
  unsigned inChannels = inputMediaFormat.GetOptionInteger(OpalAudioFormat::ChannelsOption());

  m_outSampleSize = sizeof(int16_t) * outChannels;
  m_inSampleSize = sizeof(int16_t) * inChannels;

  int error = 1;

#if LIBSWRESAMPLE_VERSION_MAJOR > 4
  AVChannelLayout out_layout, in_layout;
  switch (outChannels) {
    case 1: out_layout = AV_CHANNEL_LAYOUT_MONO; break;
    case 2: out_layout = AV_CHANNEL_LAYOUT_STEREO; break;
    case 4: out_layout = AV_CHANNEL_LAYOUT_QUAD; break;
    case 5: out_layout = AV_CHANNEL_LAYOUT_5POINT0; break;
    case 6: out_layout = AV_CHANNEL_LAYOUT_5POINT1; break;
    default: out_layout = {};
  }
  switch (inChannels) {
    case 1: in_layout = AV_CHANNEL_LAYOUT_MONO; break;
    case 2: in_layout = AV_CHANNEL_LAYOUT_STEREO; break;
    case 4: in_layout = AV_CHANNEL_LAYOUT_QUAD; break;
    case 5: in_layout = AV_CHANNEL_LAYOUT_5POINT0; break;
    case 6: in_layout = AV_CHANNEL_LAYOUT_5POINT1; break;
    default: in_layout = {};
  }

  m_swrContext = NULL;
  error = swr_alloc_set_opts2(&m_swrContext,
                              &out_layout, AV_SAMPLE_FMT_S16, m_outSampleRate,
                              &in_layout, AV_SAMPLE_FMT_S16, m_inSampleRate,
                              0, NULL);
  if (error == 0)
#else
  int64_t outChLayout, inChLayout;
  switch (outChannels) {
    case 1: outChLayout = AV_CH_LAYOUT_MONO; break;
    case 2: outChLayout = AV_CH_LAYOUT_STEREO; break;
    case 4: outChLayout = AV_CH_LAYOUT_QUAD; break;
    case 5: outChLayout = AV_CH_LAYOUT_5POINT0; break;
    case 6: outChLayout = AV_CH_LAYOUT_5POINT1; break;
    default: outChLayout = 0;
  }
  switch (inChannels) {
    case 1: inChLayout = AV_CH_LAYOUT_MONO; break;
    case 2: inChLayout = AV_CH_LAYOUT_STEREO; break;
    case 4: inChLayout = AV_CH_LAYOUT_QUAD; break;
    case 5: inChLayout = AV_CH_LAYOUT_5POINT0; break;
    case 6: inChLayout = AV_CH_LAYOUT_5POINT1; break;
    default: inChLayout = 0;
  }
  m_swrContext = swr_alloc_set_opts(NULL,
                                    outChLayout, AV_SAMPLE_FMT_S16, m_outSampleRate,
                                    inChLayout, AV_SAMPLE_FMT_S16, m_inSampleRate,
                                    0, NULL);
  if (m_swrContext != NULL)
#endif
    if ((error = swr_init(m_swrContext)) == 0)
      return;

  PTRACE(1, "Cannot allocate/create resampler (error=" << error << ") from "
          << inputMediaFormat << " to " << outputMediaFormat);
  swr_free(&m_swrContext);
#else
  m_outSampleSize = sizeof(int16_t);
  m_inSampleSize = sizeof(int16_t);
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
      CopyHeader(output.front(), input);
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
