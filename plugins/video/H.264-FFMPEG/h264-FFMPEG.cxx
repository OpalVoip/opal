/*
 * H.264 Plugin codec for OPAL using FFMPEG
 *
 * Copyright (C) 2025 Vox Lucida Pty Ltd, All Rights Reserved
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
 */

#include "../common/platform.h"

#define MY_CODEC ffH264  // Name of codec (use C variable characters)
class MY_CODEC { };

#include "../../../src/codec/h264mf_inc.cxx"
#include "../common/ffmpeg.h"
#include "../common/h264frame.h"

extern "C" {
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <vector>


///////////////////////////////////////////////////////////////////////////////

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF

// Human readable description of codec
static const char MyDescription[] = "ITU-T H.264 - Video Codec (FFMPEG)";

PLUGINCODEC_LICENSE(
  "Robert Jongbloed, Vox Lucida Pty.Ltd.",                      // source code author
  "1.0",                                                        // source code version
  "robertj@voxlucida.com.au",                                   // source code email
  "http://www.voxlucida.com.au",                                // source code URL
  "Copyright (C) 2025 by Vox Lucida Pt.Ltd., All Rights Reserved", // source code copyright
  "MPL 1.0",                                                    // source code license
  PluginCodec_License_MPL,                                      // source code license

  MyDescription,                                                // codec description
  "https://www.bellard.org/",           // codec author
  "", 							        // codec version
  "ffmpeg-devel-request@mplayerhq.hu",       // codec email
  "https://ffmpeg.org/", 				// codec URL
  "https://ffmpeg.org/legal.html",        // codec copyright information
  "GNU Lesser General Public License, Version 2.1",     // codec license
  PluginCodec_License_LGPL                                      // codec license code
);


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Option const * const MyOptionTable_High[] = {
  &HiProfile,
  &Level,
  &ConstraintFlags,
  &HiH241Profiles,
  &H241Level,
  &SDPProfileAndLevel,
  &MaxMBPS_H241,
  &MaxMBPS_SDP,
  &MaxSMBPS_H241,
  &MaxSMBPS_SDP,
  &MaxFS_H241,
  &MaxFS_SDP,
  &MaxBR_H241,
  &MaxBR_SDP,
  &H241Forced,
  &SDPForced,
  &MaxNaluSize,
  &TemporalSpatialTradeOff,
  &SendAccessUnitDelimiters,
  &PacketizationModeSDP_1,
  &MediaPacketizationsH323_1,  // Note: must be last entry
  NULL
};

static struct PluginCodec_Option const * const MyOptionTable_0[] = {
  &Profile,
  &Level,
  &ConstraintFlags,
  &H241Profiles,
  &H241Level,
  &SDPProfileAndLevel,
  &MaxMBPS_H241,
  &MaxMBPS_SDP,
  &MaxSMBPS_H241,
  &MaxSMBPS_SDP,
  &MaxFS_H241,
  &MaxFS_SDP,
  &MaxBR_H241,
  &MaxBR_SDP,
  &H241Forced,
  &SDPForced,
  &MaxNaluSize,
  &TemporalSpatialTradeOff,
  &SendAccessUnitDelimiters,
  &PacketizationModeSDP_0,
  &MediaPacketizationsH323_0,  // Note: must be last entry
  NULL
};

static struct PluginCodec_Option const * const MyOptionTable_1[] = {
  &Profile,
  &Level,
  &ConstraintFlags,
  &H241Profiles,
  &H241Level,
  &SDPProfileAndLevel,
  &MaxMBPS_H241,
  &MaxMBPS_SDP,
  &MaxSMBPS_H241,
  &MaxSMBPS_SDP,
  &MaxFS_H241,
  &MaxFS_SDP,
  &MaxBR_H241,
  &MaxBR_SDP,
  &H241Forced,
  &SDPForced,
  &MaxNaluSize,
  &TemporalSpatialTradeOff,
  &SendAccessUnitDelimiters,
  &PacketizationModeSDP_1,
  &MediaPacketizationsH323_1,  // Note: must be last entry
  NULL
};


///////////////////////////////////////////////////////////////////////////////

class H264_PluginMediaFormat : public PluginCodec_VideoFormat<MY_CODEC>
{
public:
  typedef PluginCodec_VideoFormat<MY_CODEC> BaseClass;

  H264_PluginMediaFormat(const char * formatName, OptionsTable options)
    : BaseClass(formatName, H264EncodingName, MyDescription, LevelInfo[sizeof(LevelInfo)/sizeof(LevelInfo[0])-1].m_MaxBitRate, options)
  {
    m_h323CapabilityType = PluginCodec_H323Codec_generic;
    m_h323CapabilityData = &MyH323GenericData;
  }


  virtual bool IsValidForProtocol(const char * protocol) const
  {
    return strcasecmp(protocol, PLUGINCODEC_OPTION_PROTOCOL_SIP) == 0 || m_options != MyOptionTable_0;
  }


  virtual bool ToNormalised(OptionMap & original, OptionMap & changed) const
  {
    return MyToNormalised(original, changed);
  }


  virtual bool ToCustomised(OptionMap & original, OptionMap & changed) const
  {
    return MyToCustomised(original, changed);
  }
};

/* SIP requires two completely independent media formats for packetisation
   modes zero and one. */
static H264_PluginMediaFormat const MyMediaFormatInfo_Mode0(OPAL_H264_MODE0, MyOptionTable_0);
static H264_PluginMediaFormat const MyMediaFormatInfo_Mode1(OPAL_H264_MODE1, MyOptionTable_1);
static H264_PluginMediaFormat const MyMediaFormatInfo_ffMode0("ff" OPAL_H264_MODE0, MyOptionTable_0);
static H264_PluginMediaFormat const MyMediaFormatInfo_ffMode1("ff" OPAL_H264_MODE1, MyOptionTable_1);


///////////////////////////////////////////////////////////////////////////////

class H264_Encoder : public H264_EncoderBase, public FFMPEGCodec
{
public:
  H264_Encoder(const PluginCodec_Definition * defn)
    : H264_EncoderBase(defn)
    , FFMPEGCodec(MY_CODEC_LOG, new H264Frame)
    , m_scaler(NULL)
  {
    PTRACE(4, MY_CODEC_LOG, "Created encoder");
  }


  ~H264_Encoder()
  {
    sws_freeContext(m_scaler);
  }


  virtual bool Construct()
  {
    // List some hardware accelerated encoders here
    static const char * const encoders[] = { "h264_nvmpi", "h264_nvenc", "h264_qsv", "libx264" };
    for (size_t i = 0; i < sizeof(encoders)/sizeof(encoders[0]); ++i) {
      if (InitEncoder(encoders[i]) && OpenCodec())
        return true;
    }
    if (InitEncoder(AV_CODEC_ID_H264) && OpenCodec())
      return true;

    LogAllCodecs(true);
    return false;
  }


  static unsigned Macroblocks(unsigned width, unsigned height)
  {
    return ((width + 15) / 16) * ((height + 15) / 16);
  }


  // The opened H.323 channel is often H.241 level 2, extended by custom max FS.
  // libx264 will not open a camera frame at level 2, which ends the transmit patch
  // before a single RTP packet is written. A video loopback then has nothing to return.
  unsigned AllowedMacroblocks() const
  {
    unsigned limit = 0;
    for (size_t i = 0; i < sizeof(LevelInfo)/sizeof(LevelInfo[0]); ++i) {
      if (LevelInfo[i].m_H264 == m_level) {
        limit = LevelInfo[i].m_MaxFrameSize;
        break;
      }
    }
    if (m_h241MaxFS > 0 && m_h241MaxFS < MAX_FS_H241)
      limit = std::max(limit, m_h241MaxFS * SCALE_FS_H241);
    if (m_sdpMaxFS > 0 && m_sdpMaxFS < MAX_FS_SDP)
      limit = std::max(limit, m_sdpMaxFS);
    return limit;
  }


  void FitResolution(unsigned & width, unsigned & height) const
  {
    if (width < 16)
      width = 16;
    if (height < 16)
      height = 16;
    width &= ~1u;
    height &= ~1u;

    unsigned limit = AllowedMacroblocks();
    if (limit == 0 || Macroblocks(width, height) <= limit)
      return;

    double scale = std::sqrt((double)limit / (double)Macroblocks(width, height));
    unsigned fittedWidth = std::max(16u, (unsigned)(width * scale)) & ~1u;
    unsigned fittedHeight = std::max(16u, (unsigned)(height * scale)) & ~1u;
    while (Macroblocks(fittedWidth, fittedHeight) > limit && (fittedWidth > 16 || fittedHeight > 16)) {
      if (fittedWidth >= fittedHeight && fittedWidth > 16)
        fittedWidth -= 2;
      else if (fittedHeight > 16)
        fittedHeight -= 2;
      else
        break;
    }
    PTRACE(3, MY_CODEC_LOG, "Scaling " << width << 'x' << height
           << " to " << fittedWidth << 'x' << fittedHeight
           << " for " << limit << " macroblocks");
    width = fittedWidth;
    height = fittedHeight;
  }


  int LevelForResolution(unsigned width, unsigned height) const
  {
    unsigned blocks = Macroblocks(width, height);
    unsigned fps = 30;
    if (m_frameTime > 0) {
      fps = PLUGINCODEC_VIDEO_CLOCK / m_frameTime;
      if (fps == 0)
        fps = 1;
    }
    unsigned blocksPerSecond = blocks * fps;

    const LevelInfoStruct * fit = &LevelInfo[sizeof(LevelInfo)/sizeof(LevelInfo[0]) - 1];
    for (size_t i = 0; i < sizeof(LevelInfo)/sizeof(LevelInfo[0]); ++i) {
      if (LevelInfo[i].m_MaxFrameSize >= blocks && LevelInfo[i].m_MaxMBPS >= blocksPerSecond) {
        fit = &LevelInfo[i];
        break;
      }
    }

    for (size_t i = 0; i < sizeof(LevelInfo)/sizeof(LevelInfo[0]); ++i) {
      if (LevelInfo[i].m_H264 == m_level &&
          LevelInfo[i].m_MaxFrameSize >= blocks &&
          LevelInfo[i].m_MaxMBPS >= blocksPerSecond)
        return (int)m_level;
    }

    PTRACE(3, MY_CODEC_LOG, "Raising H.264 level from " << m_level << " to " << fit->m_H264
           << " for " << width << 'x' << height);
    return (int)fit->m_H264;
  }


  unsigned BitRateForLevel(int level, unsigned bitRate) const
  {
    for (size_t i = 0; i < sizeof(LevelInfo)/sizeof(LevelInfo[0]); ++i) {
      if ((int)LevelInfo[i].m_H264 == level && bitRate > LevelInfo[i].m_MaxBitRate)
        return LevelInfo[i].m_MaxBitRate;
    }
    return bitRate;
  }


  void ApplyX264Options(unsigned width, unsigned height)
  {
    // avcodec_open2 consumes this dictionary. Reopening for a new frame size
    // must install the options again, and must not also set CRF: libx264
    // refuses the combination and the transmit patch ends with nothing sent.
    av_dict_free(&m_options);
    av_dict_set(&m_options, "preset", "ultrafast", 0);
    av_dict_set(&m_options, "tune", "zerolatency", 0);
    if (m_context == NULL)
      return;

    m_context->profile = m_profile;
    if (m_constraints & 1)
      m_context->profile |= AV_PROFILE_H264_CONSTRAINED;
    if (m_constraints & 4)
      m_context->profile |= AV_PROFILE_H264_INTRA;
    m_context->level = LevelForResolution(width, height);
  }


  bool OpenEncoder(unsigned width, unsigned height)
  {
    ApplyX264Options(width, height);
    int level = m_context != NULL ? m_context->level : (int)m_level;
    unsigned bitRate = BitRateForLevel(level, m_maxBitRate);
    if (SetEncoderOptions(width, height, m_frameTime, bitRate, m_maxRTPSize, m_tsto*51/100, m_keyFramePeriod))
      return true;

    PTRACE(2, MY_CODEC_LOG, "H.264 encoder did not open at " << width << 'x' << height
           << " level " << level << ", retrying without a level cap");
    if (m_context != NULL)
      m_context->level = 0;
    av_dict_free(&m_options);
    av_dict_set(&m_options, "preset", "ultrafast", 0);
    av_dict_set(&m_options, "tune", "zerolatency", 0);
    return SetEncoderOptions(width, height, m_frameTime, bitRate, m_maxRTPSize, m_tsto*51/100, m_keyFramePeriod);
  }


  virtual bool OnChangedOptions()
  {
    if (!H264_EncoderBase::OnChangedOptions())
      return false;

    dynamic_cast<H264Frame *>(m_fullFrame)->SetPacketisationMode(m_isH323 ? m_packetisationModeH323 : m_packetisationModeSDP);
    return OpenEncoder(m_width, m_height);
  }


  /// Get options that are "active" and may be different from the last SetOptions() call.
  virtual bool GetActiveOptions(PluginCodec_OptionMap & options)
  {
    options.SetUnsigned(m_frameTime, PLUGINCODEC_OPTION_FRAME_TIME);
    return true;
  }


  virtual int GetStatistics(char * bufferPtr, unsigned bufferSize)
  {
    size_t len = H264_EncoderBase::GetStatistics(bufferPtr, bufferSize);
    len += snprintf(bufferPtr+len, bufferSize-len, "Width=%u\nHeight=%u\n", m_width, m_height);
    return (int)len;
  }


  // OPAL frames are packed YUV420P. libswscale picks the SIMD resampler
  // (NEON on this host) and reuses the context while the sizes stay the same.
  bool ScaleYUV420(const uint8_t * src, unsigned srcWidth, unsigned srcHeight,
                   uint8_t * dst, unsigned dstWidth, unsigned dstHeight)
  {
    m_scaler = sws_getCachedContext(m_scaler,
                                    (int)srcWidth, (int)srcHeight, AV_PIX_FMT_YUV420P,
                                    (int)dstWidth, (int)dstHeight, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (m_scaler == NULL) {
      PTRACE(1, MY_CODEC_LOG, "Could not create YUV scaler for "
             << srcWidth << 'x' << srcHeight << " to " << dstWidth << 'x' << dstHeight);
      return false;
    }

    const size_t srcYSize = (size_t)srcWidth * srcHeight;
    const size_t srcCSize = (size_t)(srcWidth / 2) * (srcHeight / 2);
    const size_t dstYSize = (size_t)dstWidth * dstHeight;
    const size_t dstCSize = (size_t)(dstWidth / 2) * (dstHeight / 2);
    const uint8_t * srcPlanes[4] = { src, src + srcYSize, src + srcYSize + srcCSize, NULL };
    uint8_t * dstPlanes[4] = { dst, dst + dstYSize, dst + dstYSize + dstCSize, NULL };
    const int srcStride[4] = { (int)srcWidth, (int)(srcWidth / 2), (int)(srcWidth / 2), 0 };
    const int dstStride[4] = { (int)dstWidth, (int)(dstWidth / 2), (int)(dstWidth / 2), 0 };
    return sws_scale(m_scaler, srcPlanes, srcStride, 0, (int)srcHeight, dstPlanes, dstStride) > 0;
  }


  virtual bool Transcode(const void * fromPtr,
                         unsigned & fromLen,
                         void * toPtr,
                         unsigned & toLen,
                         unsigned & flags)
  {
    PluginCodec_RTP inRTP(fromPtr, fromLen);
    PluginCodec_Video_FrameHeader * header = inRTP.GetVideoHeader();
    unsigned encWidth = header->width;
    unsigned encHeight = header->height;
    FitResolution(encWidth, encHeight);

    std::vector<uint8_t> scaled;
    const void * encodeFrom = fromPtr;
    unsigned encodeLen = fromLen;
    if (encWidth != header->width || encHeight != header->height) {
      size_t yuvSize = (size_t)encWidth * encHeight * 3 / 2;
      size_t prefix = inRTP.GetHeaderSize() + sizeof(PluginCodec_Video_FrameHeader);
      scaled.resize(prefix + yuvSize);
      memcpy(&scaled[0], fromPtr, prefix);
      PluginCodec_Video_FrameHeader * scaledHeader =
        (PluginCodec_Video_FrameHeader *)(&scaled[0] + inRTP.GetHeaderSize());
      scaledHeader->x = 0;
      scaledHeader->y = 0;
      scaledHeader->width = encWidth;
      scaledHeader->height = encHeight;
      if (!ScaleYUV420(OPAL_VIDEO_FRAME_DATA_PTR(header), header->width, header->height,
                       &scaled[prefix], encWidth, encHeight))
        return false;
      encodeFrom = &scaled[0];
      encodeLen = (unsigned)scaled.size();
    }

    if (m_width != encWidth || m_height != encHeight) {
      m_width = encWidth;
      m_height = encHeight;
      if (!OpenEncoder(encWidth, encHeight))
        return false;
    }

    PluginCodec_RTP srcRTP(encodeFrom, encodeLen);
    PluginCodec_RTP dstRTP(toPtr, toLen);
    if (!EncodeVideoPacket(srcRTP, dstRTP, flags))
      return false;

    if (m_fullFrame->GetLength() > 0)
      toLen = (unsigned)dstRTP.GetPacketSize();
    else
      toLen = 0;
    return true;
  }

private:
  SwsContext * m_scaler;
};


///////////////////////////////////////////////////////////////////////////////

class H264_Decoder : public PluginVideoDecoder<MY_CODEC>, public FFMPEGCodec
{
  typedef PluginVideoDecoder<MY_CODEC> BaseClass;

public:
  H264_Decoder(const PluginCodec_Definition * defn)
    : BaseClass(defn)
    , FFMPEGCodec(MY_CODEC_LOG, new H264Frame)
  {
    PTRACE(4, MY_CODEC_LOG, "Created decoder");
  }


  virtual bool Construct()
  {
    // List some hardware accelerated decoders here
    static const char * const decoders[] = { "h264_nvmpi", "h264_cuvid" };
    for (size_t i = 0; i < sizeof(decoders)/sizeof(decoders[0]); ++i) {
      if (InitDecoder(decoders[i]) && OpenCodec())
        return true;
    }
    if (InitDecoder(AV_CODEC_ID_H264) && OpenCodec())
      return true;

    LogAllCodecs(false);
    return false;
  }


  virtual int GetStatistics(char * bufferPtr, unsigned bufferSize)
  {
    size_t len = BaseClass::GetStatistics(bufferPtr, bufferSize);
    len += snprintf(bufferPtr+len, bufferSize-len, "Width=%u\nHeight=%u\n", m_width, m_height);
    return (int)len;
  }


  virtual bool Transcode(const void * fromPtr,
                         unsigned & fromLen,
                         void * toPtr,
                         unsigned & toLen,
                         unsigned & flags)
  {
    if (!DecodeVideoPacket(PluginCodec_RTP(fromPtr, fromLen), flags))
      return false;

    if ((flags&PluginCodec_ReturnCoderLastFrame) == 0)
      return true;

    m_width = PICTURE_WIDTH;
    m_height = PICTURE_HEIGHT;

    PluginCodec_RTP out(toPtr, toLen);
    toLen = OutputImage(m_picture->data, m_picture->linesize, PICTURE_WIDTH, PICTURE_HEIGHT, out, flags);

    return true;
  }


  bool DecodeVideoFrame(const uint8_t * frame, size_t length, unsigned & flags)
  {
    return FFMPEGCodec::DecodeVideoFrame(frame, length, flags);
  }
};


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Definition CodecDefinition[] =
{
// The FFMPEG does not seem to have a mechanism to limit size of H.264 NALU's so we cannot use the mode 0.
//  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode1, H264_Encoder, H264_Decoder),
//  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_ffMode0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_ffMode1, H264_Encoder, H264_Decoder),
};


PLUGIN_CODEC_IMPLEMENT_CXX(MY_CODEC, CodecDefinition);


/////////////////////////////////////////////////////////////////////////////
