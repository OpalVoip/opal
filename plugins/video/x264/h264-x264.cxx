/*
 * H.264 Plugin codec for OPAL
 *
 * This the starting point for creating new H.264 plug in codecs for
 * OPAL in C++. The requirements of H.264 over H.323 and SIP are quite
 * complex. The mapping of width/height/frame rate/bit rate to the
 * H.264 profile/level with possible overrides (e.g. MaxMBS for maximum
 * macro blocks per second) which are not universally supported by all
 * endpoints make precise control of the video difficult and in some
 * cases impossible.
 *
 *
 * Copyright (C) 2010 Vox Lucida Pty Ltd, All Rights Reserved
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
 * Contributor(s): Matthias Schneider (ma30002000@yahoo.de)
 *                 Michele Piccini (michele@piccini.com)
 *                 Derek Smithies (derek@indranet.co.nz)
 *                 Robert Jongbloed (robertj@voxlucida.com.au)
 *
 */

#include "../common/platform.h"

#define MY_CODEC x264  // Name of codec (use C variable characters)
class MY_CODEC { };

#define OPAL_H323 1
#define OPAL_SIP 1

#include "../../../src/codec/h264mf_inc.cxx"

#include "../common/ffmpeg.h"

#include "../common/h264frame.h"
#include "x264wrap.h"

#include <algorithm>


///////////////////////////////////////////////////////////////////////////////

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF

static const char MyDescription[] = "ITU-T H.264 - Video Codec (x264 & FFMPEG)";     // Human readable description of codec

PLUGINCODEC_LICENSE(
  "Matthias Schneider\n"
  "Robert Jongbloed, Vox Lucida Pty.Ltd.",                      // source code author
  "1.0",                                                        // source code version
  "robertj@voxlucida.com.au",                                   // source code email
  "http://www.voxlucida.com.au",                                // source code URL
  "Copyright (C) 2006 by Matthias Schneider\n"
  "Copyright (C) 2011 by Vox Lucida Pt.Ltd., All Rights Reserved", // source code copyright
  "MPL 1.0",                                                    // source code license
  PluginCodec_License_MPL,                                      // source code license
  
  MyDescription,                                                // codec description
  "x264: Laurent Aimar, ffmpeg: Michael Niedermayer",           // codec author
  "", 							        // codec version
  "fenrir@via.ecp.fr, ffmpeg-devel-request@mplayerhq.hu",       // codec email
  "http://developers.videolan.org/x264.html, \
   http://ffmpeg.mplayerhq.hu", 				// codec URL
  "x264: Copyright (C) 2003 Laurent Aimar, \
   ffmpeg: Copyright (c) 2002-2003 Michael Niedermayer",        // codec copyright information
  "x264: GNU General Public License as published Version 2, \
   ffmpeg: GNU Lesser General Public License, Version 2.1",     // codec license
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

static struct PluginCodec_Option const * const MyOptionTable_Flash[] = {
  &Profile,
  &Level,
  &ConstraintFlags,
  &MaxNaluSize,
  NULL
};


///////////////////////////////////////////////////////////////////////////////

class H264_PluginMediaFormat : public PluginCodec_VideoFormat<MY_CODEC>
{
public:
  typedef PluginCodec_VideoFormat<MY_CODEC> BaseClass;

  H264_PluginMediaFormat(const char * formatName, OptionsTable options, const char * encodingName = H264EncodingName)
    : BaseClass(formatName, encodingName, MyDescription, LevelInfo[sizeof(LevelInfo)/sizeof(LevelInfo[0])-1].m_MaxBitRate, options)
  {
    m_h323CapabilityType = PluginCodec_H323Codec_generic;
    m_h323CapabilityData = &MyH323GenericData;
  }


  virtual bool IsValidForProtocol(const char * protocol) const
  {
    if (m_options == MyOptionTable_Flash)
      return false;
    if (m_options != MyOptionTable_0)
      return true;
    return strcasecmp(protocol, PLUGINCODEC_OPTION_PROTOCOL_SIP) == 0;
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
static H264_PluginMediaFormat const MyMediaFormatInfo_Mode0 (H264_Mode0_FormatName, MyOptionTable_0);
static H264_PluginMediaFormat const MyMediaFormatInfo_Mode1 (H264_Mode1_FormatName, MyOptionTable_1);
static H264_PluginMediaFormat const MyMediaFormatInfo_High  (H264_High_FormatName,  MyOptionTable_High);
static H264_PluginMediaFormat const MyMediaFormatInfo_xMode0("x" OPAL_H264_MODE0,   MyOptionTable_0);
static H264_PluginMediaFormat const MyMediaFormatInfo_xMode1("x" OPAL_H264_MODE1,   MyOptionTable_1);
static H264_PluginMediaFormat const MyMediaFormatInfo_xHigh ("x" OPAL_H264_High,    MyOptionTable_High);


///////////////////////////////////////////////////////////////////////////////

class H264_Encoder : public H264_EncoderBase
{
    typedef PluginVideoEncoder<MY_CODEC> BaseClass;

  protected:
    unsigned m_rateControlPeriod;

    H264Encoder m_encoder;

  public:
    H264_Encoder(const PluginCodec_Definition * defn)
      : H264_EncoderBase(defn)
      , m_rateControlPeriod(1000)
    {
      PTRACE(4, MY_CODEC_LOG, "Created encoder");
    }


    virtual bool Construct()
    {
      /* Complete construction of object after it has been created. This
         allows you to fail the create operation (return false), which cannot
         be done in the normal C++ constructor. */

      // ABR with bit rate tolerance = 1 is CBR...
      if (m_encoder.Load(this))
        return true;

      PTRACE(1, MY_CODEC_LOG, "Could not open encoder.");
      return false;
    }


    virtual bool SetOption(const char * optionName, const char * optionValue)
    {
      if (strcasecmp(optionName, PLUGINCODEC_OPTION_RATE_CONTROL_PERIOD) == 0)
        return SetOptionUnsigned(m_rateControlPeriod, optionValue, 100, 60000);

      // Base class sets bit rate and frame time
      return H264_EncoderBase::SetOption(optionName, optionValue);
    }


    virtual bool OnChangedOptions()
    {
      if (!H264_EncoderBase::OnChangedOptions())
        return false;

      m_encoder.SetProfileLevel(m_profile, m_level, m_constraints);
      m_encoder.SetFrameWidth(m_width);
      m_encoder.SetFrameHeight(m_height);
      m_encoder.SetFrameRate(PLUGINCODEC_VIDEO_CLOCK/m_frameTime);      
      m_encoder.SetTargetBitrate(m_maxBitRate/1000);
      m_encoder.SetRateControlPeriod(m_rateControlPeriod);
      m_encoder.SetTSTO(m_tsto);
      m_encoder.SetMaxKeyFramePeriod(m_keyFramePeriod != 0 ? m_keyFramePeriod : 10*PLUGINCODEC_VIDEO_CLOCK/m_frameTime); // Every 10 seconds

      unsigned mode = m_isH323 ? m_packetisationModeH323 : m_packetisationModeSDP;
      if (mode == 0) {
        unsigned size = std::min(m_maxRTPSize-PluginCodec_RTP_MinHeaderSize, m_maxNALUSize);
        // RTP payload size and NALU size equal indicates mode zero use to encoder
        m_encoder.SetMaxRTPPayloadSize(size);
        m_encoder.SetMaxNALUSize(size);
      }
      else {
        // RTP payload size and NALU size equal indicates mode zero use to encoder
        // So, make sure never the case here as we are mode one
        m_encoder.SetMaxRTPPayloadSize(m_maxRTPSize);
        m_encoder.SetMaxNALUSize(std::min(m_maxNALUSize, m_maxRTPSize-1));
      }

      m_encoder.ApplyOptions();

      PTRACE(3, MY_CODEC_LOG, "Applied options: "
                              "prof=" << m_profile << " "
                              "lev=" << m_level << " "
                              "res=" << m_width << 'x' << m_height << " "
                              "fps=" << (PLUGINCODEC_VIDEO_CLOCK/m_frameTime) << " "
                              "bps=" << m_maxBitRate << " "
                              "period=" << m_rateControlPeriod << " "
                              "RTP=" << m_maxRTPSize << " "
                              "NALU=" << m_maxNALUSize << " "
                              "TSTO=" << m_tsto << " "
                              "Mode=" << mode);
      return true;
    }


    /// Get options that are "active" and may be different from the last SetOptions() call.
    virtual bool GetActiveOptions(PluginCodec_OptionMap & options)
    {
      options.SetUnsigned(m_frameTime, PLUGINCODEC_OPTION_FRAME_TIME);
      return true;
    }


    virtual int GetStatistics(char * bufferPtr, unsigned bufferSize)
    {
      size_t len = BaseClass::GetStatistics(bufferPtr, bufferSize);
      len += snprintf(bufferPtr+len, bufferSize-len, "Width=%u\nHeight=%u\n", m_width, m_height);

#ifdef WHEN_WE_FIGURE_OUT_HOW_TO_GET_QUALITY_FROM_X264
      int quality = m_encoder.GetQuality();
      if (quality >= 0 && len < bufferSize)
        len += snprintf(bufferPtr+len, bufferSize-len, "Quality=%u\n", quality);
#endif

      return (int)len;
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      if (!m_encoder.EncodeFrames((const unsigned char *)fromPtr, fromLen,
                                  (unsigned char *)toPtr, toLen,
                                   PluginCodec_RTP_GetHeaderLength(toPtr),
                                   flags))
        return false;

#if defined(X264_LICENSED)
      m_width = m_encoder.GetWidth();
      m_height = m_encoder.GetHeight();
#else
      PluginCodec_RTP srcRTP(fromPtr, fromLen);
      PluginCodec_Video_FrameHeader * header = (PluginCodec_Video_FrameHeader *)srcRTP.GetPayloadPtr();
      m_width = header->width&~1;
      m_height = header->height&~1;
#endif
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
      if (!InitDecoder(AV_CODEC_ID_H264))
        return false;

#ifdef FF_IDCT_H264
      m_context->idct_algo = FF_IDCT_H264;
#else
      m_context->idct_algo = FF_IDCT_AUTO;
#endif
#ifdef AV_CODEC_FLAG2_DROP_FRAME_TIMECODE
      m_context->flags2 |= AV_CODEC_FLAG2_DROP_FRAME_TIMECODE;
#endif
      m_context->flags2 |= AV_CODEC_FLAG2_CHUNKS;

      if (!OpenCodec())
        return false;

      return true;
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
      if (((H264Frame *)m_fullFrame)->GetProfile() == H264_PROFILE_INT_BASELINE && m_context->has_b_frames > 0) {
        PTRACE(5, MY_CODEC_LOG, "Resetting B-Frame count to zero as Baseline profile");
        m_context->has_b_frames = 0;
      }

      return FFMPEGCodec::DecodeVideoFrame(frame, length, flags);
    }
};


///////////////////////////////////////////////////////////////////////////////

class H264_FlashEncoder : public H264_Encoder, protected H264FlashPacketizer
{
  public:
    H264_FlashEncoder(const PluginCodec_Definition * defn)
      : H264_Encoder(defn)
    {
      m_profile = H264_PROFILE_INT_MAIN;
      m_maxRTPSize = 256000;
    }


    virtual bool OnChangedOptions()
    {
      m_maxNALUSize = m_maxRTPSize; // Make sure the same so mode zero used
      m_packetisationModeSDP = m_packetisationModeH323 = 0;
      return H264_Encoder::OnChangedOptions();
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      return FlashTranscode(fromPtr, fromLen, toPtr, toLen, flags);
    }


    bool GetNALU(const void * fromPtr, unsigned & fromLen, const uint8_t * & naluPtr, unsigned & naluLen, unsigned & flags)
    {
      if (m_naluBuffer.empty())
        m_naluBuffer.resize(m_maxRTPSize);

      naluLen = (unsigned)m_naluBuffer.size();
      if (!m_encoder.EncodeFrames((const unsigned char *)fromPtr, fromLen,
                                  m_naluBuffer.data(), naluLen, PluginCodec_RTP_MinHeaderSize, flags))
        return false;

      naluPtr = m_naluBuffer.data() + PluginCodec_RTP_MinHeaderSize;
      naluLen -= PluginCodec_RTP_MinHeaderSize;
      return true;
    }
};


class H264_FlashDecoder : public PluginVideoDecoder<MY_CODEC>, public FFMPEGCodec
{
    typedef PluginVideoDecoder<MY_CODEC> BaseClass;

    std::vector<uint8_t> m_sps_pps;

  public:
    H264_FlashDecoder(const PluginCodec_Definition * defn)
      : BaseClass(defn)
      , FFMPEGCodec(MY_CODEC_LOG, NULL)
    {
      PTRACE(4, MY_CODEC_LOG, "Created flash decoder");
    }


    virtual bool Construct()
    {
      if (!InitDecoder(AV_CODEC_ID_H264))
        return false;

      m_context->error_concealment = 0;

      if (!OpenCodec())
        return false;

      return true;
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      if (fromLen == PluginCodec_RTP_MinHeaderSize)
        return true;

      if (fromLen < PluginCodec_RTP_MinHeaderSize + 4) {
          PTRACE(3, MY_CODEC_LOG, "Packet too small: " << fromLen << " bytes");
          return true;
      }

      PluginCodec_RTP rtp(fromPtr, fromLen);
      const uint8_t * payloadPtr = rtp.GetPayloadPtr();
      size_t payloadLen = rtp.GetPayloadSize();

      if (memcmp(payloadPtr, FlashSPS_PPS, FlashHeaderSize) == 0) {
        payloadPtr += FlashHeaderSize;
        payloadLen -= FlashHeaderSize;
        if (m_sps_pps.size() != payloadLen || memcmp(m_sps_pps.data(), payloadPtr, payloadLen) != 0) {
          CloseCodec();
          m_sps_pps.assign(payloadPtr, payloadPtr+payloadLen);
          m_context->extradata = m_sps_pps.data();
          m_context->extradata_size = (int)payloadLen;
          if (!OpenCodec())
            return false;
          PTRACE(4, MY_CODEC_LOG, "Re-opened decoder with new SPS/PPS: " << payloadLen << " bytes");
        }
        return true;
      }

      if (!DecodeVideoFrame(payloadPtr+FlashHeaderSize, payloadLen-FlashHeaderSize, flags))
        return false;

      if ((flags&PluginCodec_ReturnCoderLastFrame) == 0)
        return true;

      PluginCodec_RTP out(toPtr, toLen);
      toLen = OutputImage(m_picture->data, m_picture->linesize, PICTURE_WIDTH, PICTURE_HEIGHT, out, flags);

      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Definition CodecDefinition[] =
{
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode1, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_High, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_xMode0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_xMode1, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_xHigh, H264_Encoder, H264_Decoder),
};


PLUGIN_CODEC_IMPLEMENT_CXX(MY_CODEC, CodecDefinition);


/////////////////////////////////////////////////////////////////////////////
