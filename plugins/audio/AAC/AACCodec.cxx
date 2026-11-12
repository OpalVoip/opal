/*
 * AAC-LC plugin codec for OPAL
 *
 * Uses libavcodec's native "aac" encoder and decoder. Packets are raw AAC
 * access units, not ADTS. The decoder is primed with the fixed
 * AudioSpecificConfig for AAC-LC at 48 kHz stereo.
 *
 * Copyright (C) 2026 Vox Lucida Pty Ltd, All Rights Reserved
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
 * The Initial Developer of the Original Code is Vox Lucida Pty Ltd
 *
 * Contributor(s): ______________________________________.
 */

#include <codec/opalplugin.hpp>
#include <codec/known.h>

#ifndef PLUGIN_CODEC_DLL_EXPORTS
#include "plugin_config.h"
#endif

#include "../../../src/codec/aacmf_inc.cxx"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/defs.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <vector>


#define MY_CODEC AAC
#define MY_CODEC_LOG STRINGIZE(MY_CODEC)
class MY_CODEC { };

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF


static const char AACEncodingName[] = "AAC";
static const char MyDescription[] = "AAC-LC Audio Codec (libavcodec)";

PLUGINCODEC_LICENSE(
  "Vox Lucida Pty.Ltd.",
  "1.0",
  "",
  "",
  "Copyright (C) 2026 by Vox Lucida Pty.Ltd., All Rights Reserved",
  "MPL 1.0",
  PluginCodec_License_MPL,

  MyDescription,
  "FFmpeg",
  "",
  "",
  "https://ffmpeg.org",
  "Native AAC encoder and decoder, LGPL",
  "libavcodec native AAC, LGPL 2.1 or later",
  PluginCodec_License_LGPL
);


///////////////////////////////////////////////////////////////////////////////

static std::string AvError(int errnum)
{
  char buffer[AV_ERROR_MAX_STRING_SIZE];
  if (av_strerror(errnum, buffer, sizeof(buffer)) != 0)
    return "unknown libavcodec error";
  return buffer;
}


class AACPluginMediaFormat : public PluginCodec_AudioFormat<MY_CODEC>
{
  public:
    AACPluginMediaFormat()
      : PluginCodec_AudioFormat<MY_CODEC>(OPAL_AAC48S, AACEncodingName, MyDescription,
                                           AAC_OPAL_SAMPLES,
                                           AAC_MAX_FRAME_BYTES,
                                           AAC_SAMPLE_RATE)
    {
      m_rawFormat = OPAL_PCM16S_48KHZ;
      m_recommendedFramesPerPacket = 1;
      m_maxFramesPerPacket = 1;
      m_maxBandwidth = AAC_BIT_RATE;
      m_flags |= PluginCodec_SetChannels(AAC_CHANNELS);
    }

    virtual bool IsValidForProtocol(const char * protocol) const
    {
      // Raw AAC access units are not an RTP payload. SIP and H.323 must
      // not offer this format. Any other user, including a later RTMP
      // endpoint, can select it by name.
      if (protocol == NULL || protocol[0] == '\0')
        return true;
      return strcasecmp(protocol, PLUGINCODEC_OPTION_PROTOCOL_SIP) != 0 &&
             strcasecmp(protocol, PLUGINCODEC_OPTION_PROTOCOL_H323) != 0 &&
             strcasecmp(protocol, "h.323") != 0;
    }

    virtual bool ToNormalised(OptionMap & /*original*/, OptionMap & /*changed*/) const
    {
      return true;
    }

    virtual bool ToCustomised(OptionMap & /*original*/, OptionMap & /*changed*/) const
    {
      return true;
    }
};

static AACPluginMediaFormat const MyMediaFormat;


///////////////////////////////////////////////////////////////////////////////

static int16_t ClampSample(float value)
{
  if (value > 1.0f)
    value = 1.0f;
  else if (value < -1.0f)
    value = -1.0f;
  return (int16_t)(value * 32767.0f);
}


class AACPluginCodec : public PluginCodec<MY_CODEC>
{
  protected:
    AVCodecContext * m_context;
    AVFrame *        m_frame;
    AVPacket *       m_packet;
    int64_t          m_pts;

  public:
    AACPluginCodec(const PluginCodec_Definition * defn)
      : PluginCodec<MY_CODEC>(defn)
      , m_context(NULL)
      , m_frame(NULL)
      , m_packet(NULL)
      , m_pts(0)
    {
    }

    ~AACPluginCodec()
    {
      av_packet_free(&m_packet);
      av_frame_free(&m_frame);
      avcodec_free_context(&m_context);
    }

    virtual size_t GetOutputDataSize()
    {
      return AAC_FRAME_SAMPLES * AAC_CHANNELS * sizeof(int16_t);
    }

  protected:
    bool OpenCodec(const AVCodec * codec, bool encoder)
    {
      if (codec == NULL) {
        PTRACE(1, MY_CODEC_LOG, "libavcodec has no native \"aac\" codec");
        return false;
      }

      m_context = avcodec_alloc_context3(codec);
      m_packet = av_packet_alloc();
      m_frame = av_frame_alloc();
      if (m_context == NULL || m_packet == NULL || m_frame == NULL) {
        PTRACE(1, MY_CODEC_LOG, "Failed to allocate libavcodec objects");
        return false;
      }

      m_context->sample_rate = AAC_SAMPLE_RATE;
      m_context->profile = AV_PROFILE_AAC_LOW;
      (void)av_channel_layout_default(&m_context->ch_layout, AAC_CHANNELS);
      m_context->time_base.num = 1;
      m_context->time_base.den = AAC_SAMPLE_RATE;

      if (encoder) {
        m_context->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_context->bit_rate = AAC_BIT_RATE;
        m_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
      }
      else {
        m_context->request_sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_context->extradata_size = 2;
        m_context->extradata = (uint8_t *)av_mallocz(m_context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (m_context->extradata == NULL)
          return false;
        m_context->extradata[0] = AAC_ASC_BYTE0;
        m_context->extradata[1] = AAC_ASC_BYTE1;
      }

      int err = avcodec_open2(m_context, codec, NULL);
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Failed to open " << codec->name << ": " << AvError(err));
        return false;
      }

      if (encoder) {
        if (m_context->frame_size != AAC_FRAME_SAMPLES) {
          PTRACE(1, MY_CODEC_LOG, "Unexpected AAC frame size " << m_context->frame_size);
          return false;
        }

        m_frame->format = m_context->sample_fmt;
        m_frame->nb_samples = m_context->frame_size;
        m_frame->sample_rate = AAC_SAMPLE_RATE;
        if (av_channel_layout_copy(&m_frame->ch_layout, &m_context->ch_layout) < 0)
          return false;
        err = av_frame_get_buffer(m_frame, 0);
        if (err < 0) {
          PTRACE(1, MY_CODEC_LOG, "Failed to allocate encoder frame: " << AvError(err));
          return false;
        }
      }

      PTRACE(4, MY_CODEC_LOG, (encoder ? "Encoder" : "Decoder") << " opened: " << codec->long_name);
      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

class AACPluginEncoder : public AACPluginCodec
{
  protected:
    std::vector<int16_t> m_pending;

  public:
    AACPluginEncoder(const PluginCodec_Definition * defn)
      : AACPluginCodec(defn)
    {
      PTRACE(4, MY_CODEC_LOG, "Encoder created");
    }

    virtual bool Construct()
    {
      return OpenCodec(avcodec_find_encoder_by_name("aac"), true);
    }

    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & /*flags*/)
    {
      const unsigned frameSamples = AAC_FRAME_SAMPLES * AAC_CHANNELS;

      if (fromLen == 0) {
        if (m_pending.empty())
          m_pending.assign(frameSamples, 0);
        else if (m_pending.size() < frameSamples)
          m_pending.resize(frameSamples, 0);
      }
      else {
        if ((fromLen % (AAC_CHANNELS * sizeof(int16_t))) != 0) {
          PTRACE(1, MY_CODEC_LOG, "Encoder input is not a whole number of stereo samples");
          return false;
        }

        const int16_t * input = (const int16_t *)fromPtr;
        unsigned inputSamples = fromLen / sizeof(int16_t);
        unsigned missing = frameSamples > m_pending.size() ? frameSamples - (unsigned)m_pending.size() : 0;
        unsigned take = inputSamples < missing ? inputSamples : missing;
        m_pending.insert(m_pending.end(), input, input + take);
        fromLen = take * sizeof(int16_t);
      }

      if (m_pending.size() < frameSamples) {
        toLen = 0;
        return true;
      }

      return EncodeFrame(toPtr, toLen);
    }

  protected:
    bool EncodeFrame(void * toPtr, unsigned & toLen)
    {
      const unsigned frameSamples = AAC_FRAME_SAMPLES * AAC_CHANNELS;
      int err = av_frame_make_writable(m_frame);
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Encoder frame is not writable: " << AvError(err));
        return false;
      }

      float * left = (float *)m_frame->data[0];
      float * right = (float *)m_frame->data[1];
      for (unsigned i = 0; i < AAC_FRAME_SAMPLES; ++i) {
        left[i] = m_pending[i * 2] / 32768.0f;
        right[i] = m_pending[i * 2 + 1] / 32768.0f;
      }
      m_frame->nb_samples = AAC_FRAME_SAMPLES;
      m_frame->pts = m_pts;
      m_pts += AAC_FRAME_SAMPLES;

      err = avcodec_send_frame(m_context, m_frame);
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Encoder send failed: " << AvError(err));
        return false;
      }

      m_pending.erase(m_pending.begin(), m_pending.begin() + frameSamples);

      err = avcodec_receive_packet(m_context, m_packet);
      if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
        toLen = 0;
        return true;
      }
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Encoder receive failed: " << AvError(err));
        return false;
      }

      if ((unsigned)m_packet->size > toLen) {
        PTRACE(1, MY_CODEC_LOG, "Encoded frame of " << m_packet->size << " bytes does not fit in " << toLen);
        av_packet_unref(m_packet);
        return false;
      }

      memcpy(toPtr, m_packet->data, m_packet->size);
      toLen = (unsigned)m_packet->size;
      av_packet_unref(m_packet);
      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

class AACPluginDecoder : public AACPluginCodec
{
  public:
    AACPluginDecoder(const PluginCodec_Definition * defn)
      : AACPluginCodec(defn)
    {
      PTRACE(4, MY_CODEC_LOG, "Decoder created");
    }

    virtual bool Construct()
    {
      return OpenCodec(avcodec_find_decoder_by_name("aac"), false);
    }

    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & /*flags*/)
    {
      if (fromLen == 0) {
        toLen = 0;
        return true;
      }

      int err = av_new_packet(m_packet, fromLen);
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Decoder packet allocation failed: " << AvError(err));
        return false;
      }
      memcpy(m_packet->data, fromPtr, fromLen);
      m_packet->pts = m_pts;
      m_pts += AAC_FRAME_SAMPLES;

      err = avcodec_send_packet(m_context, m_packet);
      av_packet_unref(m_packet);
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Decoder send failed: " << AvError(err));
        return false;
      }

      err = avcodec_receive_frame(m_context, m_frame);
      if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
        toLen = 0;
        return true;
      }
      if (err < 0) {
        PTRACE(1, MY_CODEC_LOG, "Decoder receive failed: " << AvError(err));
        return false;
      }

      bool ok = WritePcm(toPtr, toLen);
      av_frame_unref(m_frame);
      return ok;
    }

  protected:
    bool WritePcm(void * toPtr, unsigned & toLen)
    {
      if (m_frame->format != AV_SAMPLE_FMT_FLTP || m_frame->ch_layout.nb_channels < 2) {
        PTRACE(1, MY_CODEC_LOG, "Decoder produced unexpected sample format " << m_frame->format);
        return false;
      }

      unsigned samples = (unsigned)m_frame->nb_samples;
      unsigned bytes = samples * AAC_CHANNELS * sizeof(int16_t);
      if (bytes > toLen) {
        PTRACE(1, MY_CODEC_LOG, "Decoded frame of " << bytes << " bytes does not fit in " << toLen);
        return false;
      }

      const float * left = (const float *)m_frame->data[0];
      const float * right = (const float *)m_frame->data[1];
      int16_t * output = (int16_t *)toPtr;
      for (unsigned i = 0; i < samples; ++i) {
        output[i * 2] = ClampSample(left[i]);
        output[i * 2 + 1] = ClampSample(right[i]);
      }

      toLen = bytes;
      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Definition MyCodecDefinition[] =
{
  PLUGINCODEC_AUDIO_CODEC_CXX(MyMediaFormat, AACPluginEncoder, AACPluginDecoder)
};

PLUGIN_CODEC_IMPLEMENT_CXX(MY_CODEC, MyCodecDefinition);


/////////////////////////////////////////////////////////////////////////////
