/*
 * Common Plugin code for OpenH323/OPAL
 *
 * This code is based on the following files from the OPAL project which
 * have been removed from the current build and distributions but are still
 * available in the CVS "attic"
 * 
 *    src/codecs/h263codec.cxx 
 *    include/codecs/h263codec.h 

 * The original files, and this version of the original code, are released under the same 
 * MPL 1.0 license. Substantial portions of the original code were contributed
 * by Salyens and March Networks and their right to be identified as copyright holders
 * of the original code portions and any parts now included in this new copy is asserted through 
 * their inclusion in the copyright notices below.
 *
 * Copyright (C) 2006 Post Increment
 * Copyright (C) 2005 Salyens
 * Copyright (C) 2001 March Networks Corporation
 * Copyright (C) 1999-2000 Equivalence Pty. Ltd.
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
 * The Original Code is Open H323 Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): Guilhem Tardy (gtardy@salyens.com)
 *                 Craig Southeren (craigs@postincrement.com)
 *                 Matthias Schneider (ma30002000@yahoo.de)
 */

#include "ffmpeg.h"
#include "critsect.h"

#include <iomanip>
#include <string>

#ifndef PLUGIN_CODEC_DLL_EXPORTS
#include "plugin_config.h"
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(60, 0, 0)
  #error Must have libavcodec version 60.x or later
#endif

static CriticalSection g_avcodec_mutex;


#if PLUGINCODEC_TRACING
static void logCallbackFFMPEG(void * avcl, int severity, const char* fmt , va_list arg)
{
  unsigned level;
  if (severity <= AV_LOG_FATAL)
    level = 0;
  else if (severity <= AV_LOG_ERROR)
    level = 1;
  else if (severity <= AV_LOG_WARNING)
    level = 2;
  else if (severity <= AV_LOG_INFO)
    level = 3;
  else if (severity <= AV_LOG_VERBOSE)
    level = 4;
  else
    level = 5;

  if (level > 1 && !PTRACE_CHECK(level))
    return;

  char buffer[512];
  int len = vsnprintf(buffer, sizeof(buffer), fmt, arg);
  if (len <= 0)
    return;

  // Drop trailing white space, in particular line feed, if present
  while (len > 0 && isspace(buffer[len-1]))
    buffer[--len] = '\0';

  // Nothing to log
  if (buffer[0] == '\0')
    return;

  if (avcl != NULL && strcmp((*(AVClass**)avcl)->class_name, "AVCodecContext") == 0 && static_cast<AVCodecContext *>(avcl)->opaque != NULL)
    static_cast<FFMPEGCodec *>(static_cast<AVCodecContext *>(avcl)->opaque)->ErrorCallback(level, buffer);
  else
    PTRACE(level, "FFMPEG", buffer);
}

std::string av_error_string(int errnum)
{
  char buffer[512];
  if (av_strerror(errnum, buffer, sizeof(buffer)) != 0)
    return "Unknown error";
  return buffer;
}
#endif


///////////////////////////////////////////////////////////////////////////////

FFMPEGCodec::FFMPEGCodec(const char * prefix, OpalPluginFrame * fullFrame)
  : m_prefix(prefix)
  , m_codec(NULL)
  , m_context(NULL)
  , m_options(NULL)
  , m_picture(NULL)
  , m_packet(NULL)
  , m_fullFrame(fullFrame)
  , m_open(false)
  , m_consecutiveFails(0)
  , m_errorCount(0)
  , m_hadMissingPacket(false)
{
  g_avcodec_mutex.Wait();

#if PLUGINCODEC_TRACING
  av_log_set_level(AV_LOG_DEBUG);
  av_log_set_callback(&logCallbackFFMPEG);
#endif

  g_avcodec_mutex.Signal();

  m_alignedYUV[0] = m_alignedYUV[1] = m_alignedYUV[2] = NULL;
}


FFMPEGCodec::~FFMPEGCodec()
{
  CloseCodec();

  if (m_context != NULL)
    av_free(m_context);
  
  if (m_picture != NULL) {
    av_dict_free(&m_options);
    av_frame_free(&m_picture);
  }

  av_free(m_alignedYUV[0]);
  av_free(m_alignedYUV[1]);
  av_free(m_alignedYUV[2]);

  delete m_fullFrame;

  av_packet_free(&m_packet);

  PTRACE(4, m_prefix, "Codec closed");
}


void FFMPEGCodec::LogAllCodecs(bool encoder, const AVMediaType type)
{
  void * context = NULL;
  for (;;) {
    const AVCodec* codec = av_codec_iterate(&context);
    if (!codec)
      break;

    if (encoder == (av_codec_is_encoder(codec) != 0) && type == codec->type) {
      PTRACE(1, m_prefix, codec->id << " \"" << codec->name << "\" \"" << codec->long_name << '"');
      if (codec->profiles) {
        for (const AVProfile* profile = codec->profiles; profile->profile != 0 && profile->name; ++profile)
          PTRACE(2, m_prefix, "  Profiles: " << profile->profile << "\" \"" << profile->name << '"');
      }
    }
  }
}


bool FFMPEGCodec::InitContext()
{
  if (m_fullFrame == NULL) {
    PTRACE(1, m_prefix, "No packetizer");
    return false;
  }

  m_context = avcodec_alloc_context3(m_codec);
  if (m_context == NULL) {
    PTRACE(1, m_prefix, "Failed to allocate context for codec");
    return false;
  }

  m_context->opaque = this;
  m_context->pix_fmt = m_context->sw_pix_fmt = AV_PIX_FMT_YUV420P;
  m_context->workaround_bugs = FF_BUG_AUTODETECT;

  // debugging flags
#if PLUGINCODEC_TRACING
  if (PTRACE_CHECK(4))
    m_context->debug |= FF_DEBUG_ER;
  if (PTRACE_CHECK(5))
    m_context->debug |= FF_DEBUG_PICT_INFO | FF_DEBUG_RC;
  if (PTRACE_CHECK(6))
    m_context->debug |= FF_DEBUG_BUGS | FF_DEBUG_BUFFERS;
#endif

#ifdef CODEC_FLAG_EMU_EDGE
  m_context->flags |= CODEC_FLAG_EMU_EDGE;   // don't draw edges
#endif
#ifdef AV_CODEC_FLAG_TRUNCATED
  m_context->flags |= AV_CODEC_FLAG_TRUNCATED;  // Possible missing packets
#endif

  m_context->mb_decision = FF_MB_DECISION_SIMPLE;    // high quality off

  m_context->qblur = 0.3f; // Reduce the difference in quantization between frames.

  if (m_picture == NULL) {
    m_picture = av_frame_alloc();
    if (m_picture == NULL) {
      PTRACE(1, m_prefix, "Failed to allocate frame for codec");
      return false;
    }

    m_picture->format = m_context->pix_fmt;
    m_picture->quality = 1;
  }

  if (m_packet == NULL)
    m_packet = av_packet_alloc();

  return true;
}


bool FFMPEGCodec::InitEncoder(const char * name)
{
  if (InternalInitEncoder(avcodec_find_encoder_by_name(name)))
    return true;

  PTRACE(4, m_prefix, "Could not find encoder: " << name);
  return false;
}

bool FFMPEGCodec::InitEncoder(AVCodecID codecId)
{
  if (InternalInitEncoder(avcodec_find_encoder(codecId)))
    return true;

  PTRACE(2, m_prefix, "FFMPEG does not support encoder: " << codecId);
  return false;
}

bool FFMPEGCodec::InternalInitEncoder(const AVCodec * codec)
{
  if (codec == NULL)
    return false;

  m_codec = codec;

  if (!InitContext() || !SetEncoderOptions(352, 288, 33, 256000, 1200, 31, 0))
    return false;

  PTRACE(4, m_prefix, "Encoder initialised: \"" << codec->long_name << '"');
  return true;
}


bool FFMPEGCodec::InitDecoder(const char * name)
{
  if (InternalInitDecoder(avcodec_find_decoder_by_name(name)))
    return true;

  PTRACE(5, m_prefix, "Could not find decoder: \"" << name << '"');
  return false;
}


bool FFMPEGCodec::InitDecoder(AVCodecID codecId)
{
  if (InternalInitDecoder(avcodec_find_decoder(codecId)))
    return true;

  PTRACE(5, m_prefix, "FFMPEG does not support decoder: " << codecId);
  return false;
}


bool FFMPEGCodec::InternalInitDecoder(const AVCodec * codec)
{
  if (codec == NULL)
    return false;

  m_codec = codec;

  if (!InitContext())
    return false;

  m_context->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;

  PTRACE(5, m_prefix, "Decoder initialised: \"" << codec->long_name << '"');
  return true;
}


bool FFMPEGCodec::OpenCodec()
{
  if (m_codec == NULL || m_context == NULL || m_picture == NULL) {
    PTRACE(1, m_prefix, "Codec not initialized");
    return false;
  }

  PTRACE(5, m_prefix, "Opening codec \"" << m_codec->long_name << '"');
  g_avcodec_mutex.Wait();

  int result = avcodec_open2(m_context, m_codec, &m_options);

  g_avcodec_mutex.Signal();

  if (result < 0) {
    PTRACE(1, m_prefix, "Failed to open codec \"" << m_codec->long_name << "\" - " << av_error_string(result));
    return false;
  }

  PTRACE(4, m_prefix, "Codec opened \"" << m_codec->long_name << '"');
  m_open = true;
  return true;
}


void FFMPEGCodec::CloseCodec()
{
  if (m_open) {
    PTRACE(4, m_prefix, "Closing codec \"" << m_codec->long_name << '"');
    g_avcodec_mutex.Wait();
    avcodec_free_context(&m_context);
    g_avcodec_mutex.Signal();
    m_open = false;
  }
}


bool FFMPEGCodec::SetEncoderOptions(unsigned frameWidth,
                                    unsigned frameHeight,
                                    unsigned frameTime,
                                    unsigned maxBitRate,
                                    unsigned maxRTPSize,
                                    unsigned tsto,
                                    unsigned keyFramePeriod)
{
  if (m_context == NULL)
    return false;

  bool wasOpen = m_open;
  if (wasOpen) {
    if ((unsigned)m_context->width == frameWidth && (unsigned)m_context->height == frameHeight) {
      PTRACE(4, m_prefix, "Resolution is already set to " << frameWidth << 'x' << frameHeight);
      return true;
    }

    PTRACE(3, m_prefix, "Resolution has changed - reopening codec");
    CloseCodec();

    if (!InitContext()) {
      PTRACE(1, m_prefix, "Failed to initialise context during resolution change");
      return false;
    }
  }

  if (!m_fullFrame->SetSize(frameWidth*frameHeight*2)) {
    PTRACE(1, m_prefix, "Frame handler SetResolution failed");
    return false;
  }

  // YUV420P input
  if ((unsigned)m_picture->width != frameWidth || (unsigned)m_picture->height != frameHeight) {
    m_picture->width = frameWidth;
    m_picture->height = frameHeight;
    m_picture->linesize[0] = frameWidth;
    m_picture->linesize[1] = m_picture->linesize[2] = frameWidth/2;

    av_free(m_alignedYUV[0]);
    av_free(m_alignedYUV[1]);
    av_free(m_alignedYUV[2]);

    /* Allocate correctly aligned memory using av_malloc(), also pad out length
        so don't overrrun the end of the allocated buffer.
    */
    size_t sz = ((frameWidth*frameHeight+15)/16)*16;
    m_picture->data[0] = (uint8_t *)(m_alignedYUV[0] = av_malloc(sz));
    sz = ((frameWidth*frameHeight/4+15)/16)*16;
    m_picture->data[1] = (uint8_t *)(m_alignedYUV[1] = av_malloc(sz));
    m_picture->data[2] = (uint8_t *)(m_alignedYUV[2] = av_malloc(sz));
  }

#ifdef CODEC_FLAG_EMU_EDGE
  if (frameWidth > 352)
    m_context->flags &= ~CODEC_FLAG_EMU_EDGE; // Totally bizarre! FFMPEG crashes if on for CIF4
#endif

  m_context->width = frameWidth;
  m_context->height = frameHeight;
  m_context->coded_width = frameWidth;
  m_context->coded_height = frameHeight;

  if (frameTime > 0) {
    m_context->framerate.den = PLUGINCODEC_VIDEO_CLOCK;
    m_context->framerate.num = frameTime;
    m_context->time_base.den = 2997;
    m_context->time_base.num = frameTime*m_context->time_base.den/PLUGINCODEC_VIDEO_CLOCK;
  }

  m_context->bit_rate = m_context->rc_min_rate = m_context->rc_max_rate = maxBitRate;

  // Ratecontrol buffer size, in bits. Usually 0.5-2 second worth. We have 2.
  // 224 kbyte is what VLC uses, and it seems to fix the quantization pulse (at Level 5)
  m_context->rc_buffer_size = maxBitRate*2;

  // In MEncoder this defaults to 1/4 buffer size, but in ffmpeg.c it
  // defaults to 3/4. I think the buffer is supposed to stabilize at
  // about half full. Note that setting this after avcodec_open() has
  // no effect.
  m_context->rc_initial_buffer_occupancy = m_context->rc_buffer_size * 1/2;

  // FFMPEG requires bit rate tolerance to be at least one frame size
  m_context->bit_rate_tolerance = maxBitRate/10;
  int oneFrameBits = (int)((int64_t)m_context->bit_rate*m_context->time_base.num/m_context->time_base.den) + 1;
  if (m_context->bit_rate_tolerance < oneFrameBits) {
    PTRACE(4, m_prefix, "Limited bit_rate_tolerance "
            "(" << m_context->bit_rate_tolerance << ") to size of one frame (" << oneFrameBits << ')');
    m_context->bit_rate_tolerance = oneFrameBits;
  }

  m_context->i_quant_factor = (float)-0.6;  // qscale factor between p and i frames
  m_context->i_quant_offset = (float)0.0;   // qscale offset between p and i frames
  m_context->max_qdiff = 10;  // was 3      // max q difference between frames
  m_context->qcompress = 0.5;               // qscale factor between easy & hard scenes (0.0-1.0)

  m_fullFrame->SetMaxPayloadSize(maxRTPSize);

  m_context->qmax = tsto;
  if (m_context->qmax <= m_context->qmin)
    m_context->qmax = m_context->qmin+1;

  m_context->sample_aspect_ratio.num = 1;
  m_context->sample_aspect_ratio.den = 1;
  m_context->gop_size = keyFramePeriod;
  m_context->max_b_frames = 0;
  m_context->refs = 3;

  // Dump info
  PTRACE(5, m_prefix, "Size is " << m_context->width << "x" << m_context->height);
  PTRACE(5, m_prefix, "GOP is " << m_context->gop_size);
  PTRACE(5, m_prefix, "time_base set to " << m_context->time_base.num << '/' << m_context->time_base.den
          << " (" << std::setprecision(2) << ((double)m_context->time_base.den/m_context->time_base.num) << "fps)");
  PTRACE(5, m_prefix, "bit_rate set to " << m_context->bit_rate);
  PTRACE(5, m_prefix, "rc_max_rate is " <<  m_context->rc_max_rate);
  PTRACE(5, m_prefix, "rc_min_rate set to " << m_context->rc_min_rate);
  PTRACE(5, m_prefix, "bit_rate_tolerance set to " <<m_context->bit_rate_tolerance);
  PTRACE(5, m_prefix, "qmin set to " << m_context->qmin);
  PTRACE(5, m_prefix, "qmax set to " << m_context->qmax);
  PTRACE(5, m_prefix, "payload size set to " << m_fullFrame->GetMaxPayloadSize());

  if (wasOpen && !OpenCodec()) {
    PTRACE(1, m_prefix, "Reopening codec failed");
    return false;
  }

  PTRACE(3, m_prefix, "Encoder parameters set: "
         << frameWidth << 'x' << frameHeight << '@' << (PLUGINCODEC_VIDEO_CLOCK/frameTime)
         << ' ' << maxBitRate << "bps rtp=" << maxRTPSize << " tsto=" << tsto << " gop=" << keyFramePeriod);
  return true;
}


bool FFMPEGCodec::SetOptionsInt(const char * name, const char * value, bool & optionsSame)
{
  int64_t newValue = atoi(value);
  AVDictionaryEntry * entry = av_dict_get(m_options, name, NULL, 0);
  if (entry != NULL && atoi(entry->value) == newValue)
    return true;

  optionsSame = false;
  av_dict_set_int(&m_options, name, newValue, 0);
  return true;
}


bool FFMPEGCodec::EncodeVideoPacket(const PluginCodec_RTP & in, PluginCodec_RTP & out, unsigned & flags)
{
  if (m_codec == NULL) {
    PTRACE(1, m_prefix, "Encoder did not open");
    return false;
  }

  bool forceIFrame = (flags & PluginCodec_CoderForceIFrame) != 0;
  flags = 0;

  out.SetTimestamp(in.GetTimestamp());

  if (m_fullFrame->GetPacket(out, flags))
    return true;

  PluginCodec_Video_FrameHeader * header = in.GetVideoHeader();
  if (header->x != 0 || header->y != 0) {
    PTRACE(2, m_prefix, "Video grab of partial frame unsupported, closing down video transmission thread.");
    return false;
  }

  size_t planeSize = m_context->width*m_context->height;

  // Need to copy to local buffer to guarantee byte alignment
  uint8_t * yuv = OPAL_VIDEO_FRAME_DATA_PTR(header);
  memcpy(m_picture->data[0], yuv, planeSize);
  yuv += planeSize;
  planeSize /= 4;
  memcpy(m_picture->data[1], yuv, planeSize);
  memcpy(m_picture->data[2], yuv + planeSize, planeSize);

  /*
  m_picture->pts = (int64_t)srcRTP.GetTimestamp()*m_context->time_base.den/m_context->time_base.num/PLUGINCODEC_VIDEO_CLOCK;

  It would be preferable to use the above line which would get the correct bit rate if
  the grabber is actually slower that the desired frame rate. But due to rounding
  errors, this could end up with two consecutive frames with the same pts and FFMPEG
  then spits the dummy and returns -1 error, which kills the stream. Sigh.

  So, we just assume the frame rate is actually correct and use it for bit rate control.
  */
  m_picture->pts = AV_NOPTS_VALUE;

  m_picture->pict_type = forceIFrame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(62, 0, 0)
  m_picture->key_frame = 0;
#endif

  if (!EncodeVideoFrame())
    return false;

  if (m_fullFrame->IsIntraFrame()
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(62, 0, 0)
      || m_picture->key_frame
#endif
  )
    flags |= PluginCodec_ReturnCoderIFrame;

  m_fullFrame->GetPacket(out, flags);
  return true;
}


bool FFMPEGCodec::EncodeVideoFrame()
{
  if (avcodec_send_frame(m_context, m_picture) != 0)
    return false;

  m_fullFrame->Reset();

  for (;;) {
    int result = avcodec_receive_packet(m_context, m_packet);
    PTRACE(5, m_prefix, "Encoder avcodec_receive_packet: result=" << result << ", size=" << m_packet->size);
    if (result == AVERROR(EAGAIN))
      break;

    if (result != 0) {
      PTRACE(2, m_prefix, "Encoding error: " << result);
      return false;
    }

    m_fullFrame->Append(m_packet->data, m_packet->size);

    size_t mbSize;
    const uint8_t * mbInfo = av_packet_get_side_data(m_packet, AV_PKT_DATA_H263_MB_INFO, &mbSize);
    if (mbInfo) {
      PTRACE(5, m_prefix, "MB Info: " << mbSize << '/' << mbSize/12);
      unsigned lastBitOffset = 0;
      while (mbSize > 0) {
        unsigned bitOffset = *(uint32_t*)mbInfo;
        unsigned mbAddress = *(uint16_t*)(mbInfo+6);
        m_fullFrame->RTPCallBack(NULL, (bitOffset - lastBitOffset)/8, mbAddress);
        lastBitOffset = bitOffset;
        mbInfo += 12;
        mbSize -= 12;
      }
    }
  }

  PTRACE(5, m_prefix, "Encoded frame: " << m_fullFrame->GetLength() << " bytes");
  return true;
}


bool FFMPEGCodec::DecodeVideoPacket(const PluginCodec_RTP & in, unsigned & flags)
{
  if (m_codec == NULL) {
    PTRACE(1, m_prefix, "Decoder did not open");
    return false;
  }

  // Because we cannot trust the decoder not to crash on missing packets, we throw away the whole frame
  if (!m_hadMissingPacket && (flags & PluginCodec_CoderPacketLoss) != 0) {
    PTRACE(3, m_prefix, "Decoder throwing away entire video frame due to packet loss");
    m_hadMissingPacket = true;
    m_fullFrame->Reset();
  }

  flags = 0;

  if (m_hadMissingPacket) {
    // Ignore packets till end of frame
    if (in.GetMarker())
      m_hadMissingPacket = false;
    return true;
  }

  if (in.GetPayloadSize() > 0 && !m_fullFrame->AddPacket(in, flags))
    return false;

  if (!in.GetMarker())
    return true;

  if (in.GetPayloadSize() == 0 && m_fullFrame->GetLength() == 0) {
    if (m_picture->data[0] != NULL)
      flags |= PluginCodec_ReturnCoderLastFrame;
    return true; // This happens if needed to make buffer bigger, already have frame to return
  }

  bool result = DecodeVideoFrame(m_fullFrame->GetBuffer(), m_fullFrame->GetLength(), flags);
  m_fullFrame->Reset();
  return result;
}


bool FFMPEGCodec::DecodeVideoFrame(const uint8_t * frame, size_t length, unsigned & flags)
{
  PTRACE(5, m_prefix, "Decoding " << length << " bytes");
  unsigned bytesDecoded = 0;
  int result = avcodec_receive_frame(m_context, m_picture);
  if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
    m_packet->data = const_cast<uint8_t *>(frame);
    m_packet->size = (int)length;
    result = avcodec_send_packet(m_context, m_packet);
    if (result == 0) {
      bytesDecoded = m_packet->size;
      result = avcodec_receive_frame(m_context, m_picture);
      if (result == AVERROR(EAGAIN)) {
        PTRACE(4, m_prefix, "Decoder output delayed.");
        return true;
      }
    }
  }

  if (result < 0) {
    PTRACE(3, m_prefix, "Decoder error " << result);
    flags |= PluginCodec_ReturnCoderRequestIFrame;
    return true;
  }

  flags |= PluginCodec_ReturnCoderLastFrame;
  bool isIntra = m_fullFrame->IsIntraFrame();
  if (isIntra)
    flags |= PluginCodec_ReturnCoderIFrame;

  PTRACE((size_t)bytesDecoded == length ? 5 : 4, m_prefix,
          "Decoded " << bytesDecoded << " of " << length << " bytes, " <<
          (isIntra ? 'I' : 'P') << "-Frame at " << m_context->width << "x" << m_context->height);
  return true;
}


#if PLUGINCODEC_TRACING
void FFMPEGCodec::ErrorCallback(unsigned ffmpegLevel, const char * msg)
{
  unsigned ptlibLevel;

  // Check for bogus errors, everything works so what do these mean? Bump up level so don't get the noise
  if (strstr(msg, "Frame num gap") != NULL ||
      strstr(msg, "Too many slices") != NULL ||
      (isxdigit(msg[1]) && msg[2] == '\0'))
    ptlibLevel = 6;
  // This is not really so severe an error, everything decodes fine! Happens with flash, a lot.
  else if (strcmp(msg, "non-existing SPS 32 referenced in buffering period") == 0 ||
           strcmp(msg, "vbv buffer overflow") == 0)
    ptlibLevel = 5;
  else if (ffmpegLevel > 2)
    ptlibLevel = ffmpegLevel;
  else if (ffmpegLevel > 1)
    ptlibLevel = 3;
  else {
    ptlibLevel = 2;
    ++m_errorCount;
  }

  PTRACE(ptlibLevel, m_prefix, "FFMPEG(" << ffmpegLevel << "): " << msg);
}
#else
void FFMPEGCodec::ErrorCallback(unsigned, const char *)
{
}
#endif

