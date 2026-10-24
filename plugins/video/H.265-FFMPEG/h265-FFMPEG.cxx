/*
 * H.265 Plugin codec for OPAL using FFMPEG
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

#define MY_CODEC ffH265  // Name of codec (use C variable characters)
class MY_CODEC { };

#include "../../../src/codec/h265mf_inc.cxx"
#include "../common/ffmpeg.h"
#include "../common/encframe.h"

#include <algorithm>


///////////////////////////////////////////////////////////////////////////////

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF

// Human readable description of codec
static const char MyDescription[] = "ITU-T H.265 - Video Codec (FFMPEG)";

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

static struct PluginCodec_Option const Tier =
{
  PluginCodec_EnumOption,             // Option type
  TierName,                           // User visible name
  false,                              // User Read/Only flag
  PluginCodec_MinMerge,               // Merge mode
  DefaultTierStr,                     // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  // Enum values, single string of value separated by colons
  H265_TIER_STR_MAIN ":" H265_TIER_STR_HIGH
};

static struct PluginCodec_Option const Level =
{
  PluginCodec_EnumOption,             // Option type
  LevelName,                          // User visible name
  false,                              // User Read/Only flag
  PluginCodec_MinMerge,               // Merge mode
  DefaultLevelStr,                    // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  // Enum values, single string of value separated by colons
  H265_LEVEL_STR_1   ":"
  H265_LEVEL_STR_2   ":"
  H265_LEVEL_STR_2_1 ":"
  H265_LEVEL_STR_3   ":"
  H265_LEVEL_STR_3_1 ":"
  H265_LEVEL_STR_4   ":"
  H265_LEVEL_STR_4_1 ":"
  H265_LEVEL_STR_5   ":"
  H265_LEVEL_STR_5_1 ":"
  H265_LEVEL_STR_5_2 ":"
  H265_LEVEL_STR_6   ":"
  H265_LEVEL_STR_6_1 ":"
  H265_LEVEL_STR_6_2
};

static struct PluginCodec_Option const ConstraintFlags =
{
  PluginCodec_IntegerOption,          // Option type
  ConstraintFlagsName,                // User visible name
  true,                               // User Read/Only flag
  PluginCodec_AndMerge,               // Merge mode
  "0",                                // Initial value
  NULL,                               // FMTP option name
  "0",                                // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  "0",                                // Minimum value
  "0xffffff"                          // Maximum value
};

static struct PluginCodec_Option const TemporalSpatialTradeOff =
{
  PluginCodec_IntegerOption,          // Option type
  PLUGINCODEC_OPTION_TEMPORAL_SPATIAL_TRADE_OFF, // User visible name
  false,                              // User Read/Only flag
  PluginCodec_AlwaysMerge,            // Merge mode
  "50",                               // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  "1",                                // Minimum value
  "100"                               // Maximum value
};

static struct PluginCodec_Option const * const MyOptionTable_265[] = {
  &Tier,
  &Level,
  &ConstraintFlags,
  &TemporalSpatialTradeOff,
  NULL
};


///////////////////////////////////////////////////////////////////////////////

class H265_PluginMediaFormat : public PluginCodec_VideoFormat<MY_CODEC>
{
public:
  typedef PluginCodec_VideoFormat<MY_CODEC> BaseClass;

  H265_PluginMediaFormat(const char * formatName)
    : BaseClass(formatName,
                H265EncodingName,
                MyDescription,
                1000000000,
                MyOptionTable_265)
  {
  }


  virtual bool IsValidForProtocol(const char * protocol) const
  {
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

static H265_PluginMediaFormat const MyMediaFormatInfo(H265_FormatName);
static H265_PluginMediaFormat const MyMediaFormatInfo_ff("ff" OPAL_H265);


///////////////////////////////////////////////////////////////////////////////

class H265Frame : public OpalPluginFrame
{
  size_t m_naluStart;
  bool m_fu;
  bool m_intra;
public:
  H265Frame()
    : m_naluStart(0)
    , m_fu(false)
    , m_intra(false)
  { }

  virtual const char * GetName() const
  {
    return "RFC7798";
  }

  virtual bool Reset(size_t len = 0)
  {
    m_naluStart = 0;
    m_fu = false;
    m_intra = false;
    return OpalPluginFrame::Reset(len);
  }

  unsigned GetPrefixLength(size_t offset)
  {
    const uint8_t * data = m_buffer + offset;
    size_t len = m_length - offset;
    if (--len == 0 || *data++ != 0)
      return 0;
    if (--len == 0 || *data++ != 0)
      return 0;
    if (*data == 1)
      return 3;
    if (--len == 0 || *data++ != 0)
      return 0;
    return *data == 1 ? 4 : 0;
  }

  size_t GetNaluSize(size_t start)
  {
    for (size_t nextNalu = start+1; nextNalu < m_length - 4; ++nextNalu) {
      if (GetPrefixLength(nextNalu) != 0)
        return nextNalu - start;
    }
    return m_length - start;
  }

  virtual bool GetPacket(PluginCodec_RTP & rtp, unsigned & flags)
  {
    flags = 0;

    if (m_naluStart >= m_length)
      return false;

    if (m_naluStart == 0 || m_length < 5) {
      m_naluStart = GetPrefixLength(0);
      if (m_naluStart == 0) {
        PTRACE(3, MY_CODEC_LOG, "Frame does not start with NALU prefix");
        return false;
      }
    }

    switch (m_buffer[m_naluStart] >> 1) {
      case 19: // IDR
      case 20:
      case 0x20: // VPS
      case 0x21: // SPS
      case 0x22: // PPS
        m_intra = true;
        break;
    }

    size_t naluSize = GetNaluSize(m_naluStart);
    size_t maxFragmentSize = m_maxPayloadSize - 3;
    if (m_fu || naluSize > maxFragmentSize) {
      naluSize = std::min(maxFragmentSize, naluSize);
      uint8_t fuPrefix[] = { 49 << 1, 0, (uint8_t)(m_fu ? 0 : 0x80) };
      if (!rtp.CopyPayload(fuPrefix, 3) || !rtp.CopyPayload(m_buffer + m_naluStart, naluSize, 3))
        return false;
      m_fu = naluSize == maxFragmentSize;
      m_naluStart += naluSize;
      naluSize = 0;
    }
    else if (naluSize + GetNaluSize(m_naluStart + naluSize + 3) + 6 >= m_maxPayloadSize) {
      if (!rtp.CopyPayload(m_buffer + m_naluStart, naluSize))
        return false;
      m_naluStart += naluSize;
      m_naluStart += GetPrefixLength(m_naluStart);
    }
    else {
      uint8_t apNaluPrefix[] = { 48 << 1, 0 };
      if (!rtp.CopyPayload(apNaluPrefix, sizeof(apNaluPrefix)))
        return false;
      size_t offset = sizeof(apNaluPrefix);
      while (offset + naluSize + 2 < m_maxPayloadSize) {
        uint8_t apPrefix[] = { (uint8_t)(naluSize >> 8), (uint8_t)naluSize };
        if (!rtp.CopyPayload(apPrefix, sizeof(apPrefix), offset))
          return false;
        offset += 2;
        if (!rtp.CopyPayload(m_buffer + m_naluStart, naluSize, offset))
          return false;
        offset += naluSize;
        m_naluStart += naluSize;
        m_naluStart += GetPrefixLength(m_naluStart);
        naluSize = GetNaluSize(m_naluStart);
      }
      naluSize = 0;
    }

    if (m_naluStart + naluSize >= m_length)
      flags |= PluginCodec_ReturnCoderLastFrame;

    return true;
  }

  bool AppendNALU(const uint8_t * nalu, size_t naluSize)
  {
    switch (nalu[0] >> 1) {
      case 19: // IDR
      case 20:
      case 0x20: // VPS
      case 0x21: // SPS
      case 0x22: // PPS
        m_intra = true;
        break;
    }

    static uint8_t naluPrefix[4] = { 0, 0, 0, 1 };
    return Append(naluPrefix, sizeof(naluPrefix)) && Append(nalu, naluSize);
  }

  virtual bool AddPacket(const PluginCodec_RTP & pkt, unsigned & flags)
  {
    if (flags&PluginCodec_CoderPacketLoss) {
      flags = PluginCodec_ReturnCoderRequestIFrame;
      Reset();
      return true;
    }

    size_t size = pkt.GetPayloadSize();
    if (size < 4)
      return false;

    const uint8_t * payload = pkt.GetPayloadPtr();

    switch (payload[0] >> 1) {
      case 48: // Aggregation Packet
        {
          size_t offset = 2;
          while (offset < size) {
            size_t naluSize = (payload[offset] << 8) | payload[offset+1];
            offset += 2;
            if (naluSize > size - offset || !AppendNALU(&payload[offset], naluSize))
              return false;
            offset += naluSize;
          }
        }
        break;

      case 49: // Fragmentation Unit
        return (payload[2] & 0x80) ? AppendNALU(payload+3, size-3) : Append(payload+3, size-3);

      default:
        return AppendNALU(payload, size);
    }

    return true;
  }

  virtual bool IsIntraFrame() const
  {
    return m_intra;
  }
};

///////////////////////////////////////////////////////////////////////////////

class H265_Encoder : public PluginVideoEncoder<MY_CODEC>, public FFMPEGCodec
{
  typedef PluginVideoEncoder<MY_CODEC> BaseClass;

protected:
  unsigned m_tier;
  unsigned m_level;
  unsigned m_constraints;

public:
  H265_Encoder(const PluginCodec_Definition * defn)
    : BaseClass(defn)
    , FFMPEGCodec(MY_CODEC_LOG, new H265Frame)
    , m_tier(DefaultTierInt)
    , m_level(DefaultLevelInt)
    , m_constraints(0)
  {
    PTRACE(4, MY_CODEC_LOG, "Created encoder");
  }


  virtual bool Construct()
  {
    // List some hardware accelerated encoders here
    static const char * const encoders[] = { "hevc_nvmpi", "hevc_nvenc", "hevc_qsv", "libx265" };
    for (size_t i = 0; i < sizeof(encoders)/sizeof(encoders[0]); ++i) {
      if (InitEncoder(encoders[i]) && OpenCodec())
        return true;
    }
    if (InitEncoder(AV_CODEC_ID_H265) && OpenCodec())
      return true;

    LogAllCodecs(true);
    return false;
  }


  virtual bool SetOption(const char * optionName, const char * optionValue)
  {
    if (strcasecmp(optionName, Tier.m_name) == 0) {
      for (size_t i = 0; i < sizeof(TierInfo)/sizeof(TierInfo[0]); ++i) {
        if (strcasecmp(optionValue, TierInfo[i].m_Name) == 0) {
          m_tier = TierInfo[i].m_H265;
          m_optionsSame = false;
          return true;
        }
      }
      return false;
    }

    if (strcasecmp(optionName, Level.m_name) == 0) {
      for (size_t i = 0; i < sizeof(LevelInfo)/sizeof(LevelInfo[0]); i++) {
        if (strcasecmp(optionValue, LevelInfo[i].m_Name) == 0) {
          m_level = LevelInfo[i].m_H265;
          m_optionsSame = false;
          return true;
        }
      }
      return false;
    }

    if (strcasecmp(optionName, ConstraintFlags.m_name) == 0)
      return SetOptionUnsigned(m_constraints, optionValue, 0, 0xffffff);

    // Base class sets bit rate and frame time
    return BaseClass::SetOption(optionName, optionValue);
  }


  virtual bool OnChangedOptions()
  {
    /* After all the options are set via SetOptions() this is called if any
       of them changed. This would do whatever is needed to set parameters
       for the actual codec. */

    // do some checks on limits
    size_t levelIndex = sizeof(LevelInfo)/sizeof(LevelInfo[0]);
    while (--levelIndex > 0) {
      if (m_level == LevelInfo[levelIndex].m_H265)
        break;
    }

    av_dict_set(&m_options, "preset", "ultrafast", 0);
    av_dict_set(&m_options, "tune", "zerolatency", 0);
    //av_dict_set(&m_options, "x265-params", "pass=1", 0);
    av_dict_set(&m_options, "crf", "35", 0);

    return SetEncoderOptions(m_width, m_height, m_frameTime, m_maxBitRate, m_maxRTPSize, m_tsto*51/100, m_keyFramePeriod);
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

class H265_Decoder : public PluginVideoDecoder<MY_CODEC>, public FFMPEGCodec
{
  typedef PluginVideoDecoder<MY_CODEC> BaseClass;

public:
  H265_Decoder(const PluginCodec_Definition * defn)
    : BaseClass(defn)
    , FFMPEGCodec(MY_CODEC_LOG, new H265Frame)
  {
    PTRACE(4, MY_CODEC_LOG, "Created decoder");
  }


  virtual bool Construct()
  {
    // List some hardware accelerated decoders here
    static const char * const decoders[] = { "hevc_nvmpi", "hevc" };
    for (size_t i = 0; i < sizeof(decoders)/sizeof(decoders[0]); ++i) {
      if (InitDecoder(decoders[i]) && OpenCodec())
        return true;
    }
    if (InitDecoder(AV_CODEC_ID_H265) && OpenCodec())
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
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo, H265_Encoder, H265_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_ff, H265_Encoder, H265_Decoder),
};


PLUGIN_CODEC_IMPLEMENT_CXX(MY_CODEC, CodecDefinition);


/////////////////////////////////////////////////////////////////////////////
