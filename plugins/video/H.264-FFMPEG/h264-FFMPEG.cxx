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

#include <algorithm>


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
  {
    PTRACE(4, MY_CODEC_LOG, "Created encoder");
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


  virtual bool OnChangedOptions()
  {
    if (!H264_EncoderBase::OnChangedOptions())
      return false;

    av_dict_set(&m_options, "preset", "ultrafast", 0);
    av_dict_set(&m_options, "tune", "zerolatency", 0);
    av_dict_set(&m_options, "crf", "35", 0);

    dynamic_cast<H264Frame *>(m_fullFrame)->SetPacketisationMode(m_isH323 ? m_packetisationModeH323 : m_packetisationModeSDP);
    return SetEncoderOptions(m_width, m_height, m_frameTime, m_maxBitRate, m_maxRTPSize, m_tsto, m_keyFramePeriod);
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


  virtual bool Transcode(const void * fromPtr,
                         unsigned & fromLen,
                         void * toPtr,
                         unsigned & toLen,
                         unsigned & flags)
  {
    PluginCodec_RTP inRTP(fromPtr, fromLen);
    PluginCodec_Video_FrameHeader * header = inRTP.GetVideoHeader();
    if (m_width != header->width || m_height != header->height) {
      m_width = header->width;
      m_height = header->height;
      if (!SetEncoderOptions(m_width, m_height, m_frameTime, m_maxBitRate, m_maxRTPSize, m_tsto, m_keyFramePeriod))
        return false;
    }

    PluginCodec_RTP dstRTP(toPtr, toLen);
    if (!EncodeVideoPacket(inRTP, dstRTP, flags))
      return false;

    if (m_fullFrame->GetLength() > 0)
      toLen = (unsigned)dstRTP.GetPacketSize();
    else
      toLen = 0;
    return true;
  }
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
