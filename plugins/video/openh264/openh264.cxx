/*
 * OpenH264 Plugin codec for OPAL
 *
 * Copyright (C) 2014 Vox Lucida Pty Ltd, All Rights Reserved
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

#define MY_CODEC openH264  // Name of codec (use C variable characters)
class MY_CODEC { };

#define OPAL_H323 1
#define OPAL_SIP 1

#include "../../../src/codec/h264mf_inc.cxx"
#include "../common/h264frame.h"
#include "wels/codec_api.h"
#include "wels/codec_ver.h"

#include <iomanip>
#include <algorithm>


///////////////////////////////////////////////////////////////////////////////

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF

static const char MyDescription[] = "ITU-T H.264 - Video Codec (OpenH264)";     // Human readable description of codec

PLUGINCODEC_LICENSE(
  "Robert Jongbloed, Vox Lucida Pty.Ltd.",                      // source code author
  "1.0",                                                        // source code version
  "robertj@voxlucida.com.au",                                   // source code email
  "http://www.voxlucida.com.au",                                // source code URL
  "Copyright (C) 2011 by Vox Lucida Pt.Ltd., All Rights Reserved", // source code copyright
  "MPL 1.0",                                                    // source code license
  PluginCodec_License_MPL,                                      // source code license
  
  MyDescription,                                                // codec description
  "Cisco Systems",                                              // codec author
  g_strCodecVer, 						// codec version
  "",                                                           // codec email
  "http://www.openh264.org", 				        // codec URL
  "Copyright (c) 2013, Cisco Systems. All rights reserved.",    // codec copyright information
  "BSD",                                                        // codec license
  PluginCodec_License_BSD                                       // codec license code
);


///////////////////////////////////////////////////////////////////////////////

#define CAMERA_VIDEO_REAL_TIME_STR       "Camera"
#define SCREEN_CONTENT_REAL_TIME_STR     "Screen"
#define CAMERA_VIDEO_NON_REAL_TIME_STR   "NonRealTimeCamera"
#define SCREEN_CONTENT_NON_REAL_TIME_STR "NonRealTimeScreen"
#define INPUT_CONTENT_TYPE_ALL_STR       "InputContent"

static struct PluginCodec_Option const EncodingType =
{
  PluginCodec_EnumOption,             // Option type
  "EncodingType",                     // User visible name
  false,                              // User Read/Only flag
  PluginCodec_NoMerge,                // Merge mode
  "Camera",                           // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  // Enum values, single string of value separated by colons
  CAMERA_VIDEO_REAL_TIME_STR ":"
  SCREEN_CONTENT_REAL_TIME_STR ":"
  CAMERA_VIDEO_NON_REAL_TIME_STR ":"
  SCREEN_CONTENT_NON_REAL_TIME_STR ":"
  INPUT_CONTENT_TYPE_ALL_STR
};

#define LOW_COMPLEXITY_STR    "Low"
#define MEDIUM_COMPLEXITY_STR "Medium"
#define HIGH_COMPLEXITY_STR   "High"

static struct PluginCodec_Option const EncodingComplexity =
{
  PluginCodec_EnumOption,             // Option type
  "EncodingComplexity",               // User visible name
  false,                              // User Read/Only flag
  PluginCodec_NoMerge,                // Merge mode
  "Low",                              // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  // Enum values, single string of value separated by colons
  LOW_COMPLEXITY_STR ":"
  MEDIUM_COMPLEXITY_STR ":"
  HIGH_COMPLEXITY_STR
};

static struct PluginCodec_Option const * const MyOptionTable_0[] = {
  &Profile,
  &Level,
  &ConstraintFlags,
  &EncodingType,
  &EncodingComplexity,
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
  &PacketizationModeSDP_0,
  &MediaPacketizationsH323_0,  // Note: must be last entry
  NULL
};

static struct PluginCodec_Option const * const MyOptionTable_1[] = {
  &Profile,
  &Level,
  &ConstraintFlags,
  &EncodingType,
  &EncodingComplexity,
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
static H264_PluginMediaFormat const MyMediaFormatInfo_Open0("Open" OPAL_H264_MODE0, MyOptionTable_0);
static H264_PluginMediaFormat const MyMediaFormatInfo_Open1("Open" OPAL_H264_MODE1, MyOptionTable_1);


///////////////////////////////////////////////////////////////////////////////

#if PLUGINCODEC_TRACING

static void TraceCallback(void*, int welsLevel, const char* string)
{
  unsigned ptraceLevel;
  if (welsLevel <= WELS_LOG_ERROR)
    ptraceLevel = 1;
  else if (welsLevel <= WELS_LOG_WARNING)
    ptraceLevel = strstr(string, "ParamValidationExt") != NULL ? 3 : 2;
  else if (welsLevel <= WELS_LOG_INFO)
    ptraceLevel = 4;
  else if (welsLevel <= WELS_LOG_DEBUG)
    ptraceLevel = 5;
  else
    ptraceLevel = 6;

  if (!PTRACE_CHECK(ptraceLevel))
    return;

  const char * msg;
  if (strncmp(string, "[OpenH264]", 10) == 0 && (msg = strchr(string, ':')) != NULL)
    ++msg;
  else
    msg = string;

  size_t len = strlen(msg);
  while (len > 0 && isspace(msg[len - 1]))
    --len;
  if (len == 0)
    return;

  char * buf = (char *)alloca(len+1);
  strncpy(buf, msg, len);
  buf[len] = '\0';
  PluginCodec_LogFunctionInstance(ptraceLevel, __FILE__, __LINE__, "OpenH264-Lib", buf);
}

static int TraceLevel = WELS_LOG_DETAIL;
static WelsTraceCallback TraceCallbackPtr = TraceCallback;

#endif // PLUGINCODEC_TRACING


static void CheckVersion(bool encoder)
{
  OpenH264Version version;
  WelsGetCodecVersionEx(&version);
  if (version.uMajor == OPENH264_MAJOR && version.uMinor == OPENH264_MINOR && version.uRevision == OPENH264_REVISION) {
    PTRACE(4, MY_CODEC_LOG, "Created " << (encoder ? "encoder" : "decoder")
           << ", plugin DLL/API version " << version.uMajor << '.' << version.uMinor << '.' << version.uRevision);
  }
  else {
    PTRACE(1, MY_CODEC_LOG, "WARNING! Created " << (encoder ? "encoder" : "decoder")
           << ", plugin with DLL version " << version.uMajor << '.' << version.uMinor << '.' << version.uRevision
           << " but API version " << OPENH264_MAJOR << '.' << OPENH264_MINOR << '.' << OPENH264_REVISION);
  }
}


static const char * GetLevelName(unsigned h264code)
{
  for (size_t levelIndex = 0; levelIndex < sizeof(LevelInfo)/sizeof(LevelInfo[0])-1; ++levelIndex) {
    if (LevelInfo[levelIndex].m_H264 == h264code)
      return LevelInfo[levelIndex].m_Name;
  }
  return "<unknown>";
}


///////////////////////////////////////////////////////////////////////////////

class H264_Encoder : public H264_EncoderBase
{
  protected:
    EUsageType  m_usageType;
    ECOMPLEXITY_MODE m_complexityMode;

    ISVCEncoder * m_encoder;
    H264Frame     m_encapsulation;
    int           m_quality;

  public:
    H264_Encoder(const PluginCodec_Definition * defn)
      : H264_EncoderBase(defn)
      , m_usageType(CAMERA_VIDEO_REAL_TIME)
      , m_complexityMode(LOW_COMPLEXITY)
      , m_encoder(NULL)
      , m_quality(-1)
    {
      CheckVersion(true);
    }


    ~H264_Encoder()
    {
      if (m_encoder != NULL)
        WelsDestroySVCEncoder(m_encoder);
    }

    virtual bool Construct()
    {
      if (WelsCreateSVCEncoder(&m_encoder) != cmResultSuccess) {
        PTRACE(1, MY_CODEC_LOG, "Could not create encoder.");
        return false;
      }

#if PLUGINCODEC_TRACING
      m_encoder->SetOption(ENCODER_OPTION_TRACE_CALLBACK, &TraceCallbackPtr);
      m_encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &TraceLevel);
#endif
      return true;
    }


    virtual bool SetOption(const char * optionName, const char * optionValue)
    {
      if (strcasecmp(optionName, EncodingType.m_name) == 0) {
        static const char * const UsageTypes[] = {
          CAMERA_VIDEO_REAL_TIME_STR,
          SCREEN_CONTENT_REAL_TIME_STR,
          CAMERA_VIDEO_NON_REAL_TIME_STR,
          SCREEN_CONTENT_NON_REAL_TIME_STR,
          INPUT_CONTENT_TYPE_ALL_STR,
          NULL
        };
        return SetOptionEnum(m_usageType, UsageTypes, optionValue);
      }

      if (strcasecmp(optionName, EncodingComplexity.m_name) == 0) {
        static const char * const ComplexityNames[] = {
          LOW_COMPLEXITY_STR,
          MEDIUM_COMPLEXITY_STR,
          HIGH_COMPLEXITY_STR,
          NULL
        };
        return SetOptionEnum(m_complexityMode, ComplexityNames, optionValue);
      }

      // Base class sets bit rate and frame time
      return H264_EncoderBase::SetOption(optionName, optionValue);
    }


    virtual bool OnChangedOptions()
    {
      if (!H264_EncoderBase::OnChangedOptions())
        return false;

      m_encoder->Uninitialize();

      SEncParamExt param;
      m_encoder->GetDefaultParams(&param);
      param.iUsageType = m_usageType;
      param.iComplexityMode = m_complexityMode;
      param.iPicWidth = m_width;
      param.iPicHeight = m_height;
      param.iMaxBitrate = m_maxBitRate;
      param.iTargetBitrate = (int)(m_maxBitRate*95LL/100);
      param.iMinQp = 12; // Get warnings that this has to be between 12 an 42
      param.iMaxQp = 11 + m_tsto;  // m_tsto is 1 to 31
      param.iRCMode = RC_BITRATE_MODE;
      param.fMaxFrameRate = (float)PLUGINCODEC_VIDEO_CLOCK/m_frameTime;
      param.uiIntraPeriod = m_keyFramePeriod;
      param.bPrefixNalAddingCtrl = false;

      param.sSpatialLayers[0].uiProfileIdc = (EProfileIdc)m_profile;
      param.sSpatialLayers[0].uiLevelIdc = (ELevelIdc)m_level;
      param.sSpatialLayers[0].iVideoWidth = m_width;
      param.sSpatialLayers[0].iVideoHeight = m_height;
      param.sSpatialLayers[0].fFrameRate = param.fMaxFrameRate;
      param.sSpatialLayers[0].iMaxSpatialBitrate = param.iMaxBitrate;
      param.sSpatialLayers[0].iSpatialBitrate = param.iTargetBitrate;

      unsigned mode = m_isH323 ? m_packetisationModeH323 : m_packetisationModeSDP;
      switch (mode) {
        case 0 :
          param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SIZELIMITED_SLICE;
          param.sSpatialLayers[0].sSliceArgument.uiSliceSizeConstraint = param.uiMaxNalSize =
                           std::min(m_maxRTPSize-PluginCodec_RTP_MinHeaderSize, m_maxNALUSize);

          break;

        case 1 :
          param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
          param.uiMaxNalSize = 0;
          break;

        default :
          PTRACE(1, MY_CODEC_LOG, "Unsupported packetisation mode: " << mode);
          return false;
      }

      m_encapsulation.SetPacketisationMode(mode);
      m_encapsulation.SetMaxPayloadSize(m_maxRTPSize);

      int err = m_encoder->InitializeExt(&param);

      static const char * const errMsg[] = { "Initialised", "Invalid parameter for", "Unknown error with", "Memory error in", "Initialisation failure in" };
      if (err < 0 || (size_t)err >= sizeof(errMsg)/sizeof(errMsg[0]))
        err = cmUnknownReason;

      PTRACE(err == cmResultSuccess ? 3 : 1, MY_CODEC_LOG,
             errMsg[err] << " encoder:"
             " " << m_width << 'x' << m_height << '@' << std::fixed << std::setprecision(1) << param.fMaxFrameRate << ","
             " " << param.iTargetBitrate << "bps,"
             " size=" << param.uiMaxNalSize << '(' << m_maxRTPSize << "),"
             " profile=" << m_profile << ","
             " level=" << GetLevelName(m_level) << '(' << m_level << "),"
             " tsto=" << m_tsto << " (" << param.iMaxQp << "),"
             " kfr=" << param.uiIntraPeriod << ","
             " pkt-mode=" << mode);
      return err == cmResultSuccess;
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

      if (m_quality >= 0 && len < bufferSize)
        len += snprintf(bufferPtr+len, bufferSize-len, "Quality=%u\n", m_quality);

      return (int)len;
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      bool forceIntraFrame = (flags&PluginCodec_CoderForceIFrame) != 0;
      flags = 0;

      if (!m_encapsulation.HasRTPFrames()) {
        PluginCodec_RTP from(fromPtr, fromLen);
        PluginCodec_Video_FrameHeader * header = from.GetVideoHeader();

        // if the incoming data has changed size, tell the encoder
        if (header->width != m_width || header->height != m_height) {
            m_width = header->width;
            m_height = header->height;
            OnChangedOptions();
        }

        unsigned planeWidth = (header->width+1)&~1;
        unsigned planeHeight = (header->height+1)&~1;

        SSourcePicture picture;
        picture.iColorFormat = videoFormatI420;	// color space type
        picture.iPicWidth = header->width;
        picture.iPicHeight = header->height;
        picture.iStride[0] = planeWidth;
        picture.iStride[1] = picture.iStride[2] = planeWidth/2;
        picture.pData[0] = from.GetVideoFrameData();
        picture.pData[1] = picture.pData[0] + planeWidth*planeHeight;
        picture.pData[2] = picture.pData[1] + planeWidth*planeHeight/4;
        picture.uiTimeStamp = 0;

        if (forceIntraFrame)
          m_encoder->ForceIntraFrame(true);

        SFrameBSInfo  bitstream;
        memset(&bitstream, 0, sizeof(bitstream));
        if (m_encoder->EncodeFrame(&picture, &bitstream) != cmResultSuccess) {
          PTRACE(1, MY_CODEC_LOG, "Fatal error encoding frame.");
          return false;
        }

        switch (bitstream.eFrameType) {
          case videoFrameTypeInvalid :
            PTRACE(1, MY_CODEC_LOG, "Fatal error encoding frame.");
            return false;

          case videoFrameTypeSkip :
            PTRACE(5, MY_CODEC_LOG, "Output frame skipped.");
            flags |= PluginCodec_ReturnCoderLastFrame;
            toLen = 0;
            return true;

          case videoFrameTypeIDR :
          case videoFrameTypeI :
            flags |= PluginCodec_ReturnCoderIFrame;
          case videoFrameTypeP :
          case videoFrameTypeIPMixed :
            uint32_t numberOfNALUs = 0;
            for (int layer = 0; layer < bitstream.iLayerNum; ++layer) {
              for (int nalu = 0; nalu < bitstream.sLayerInfo[layer].iNalCount; ++nalu)
                ++numberOfNALUs;
            }

            m_encapsulation.Reset();
            m_encapsulation.Allocate(numberOfNALUs);
            m_encapsulation.SetTimestamp(from.GetTimestamp());

            m_quality = -1;
            for (int layer = 0; layer < bitstream.iLayerNum; ++layer) {
              for (int nalu = 0; nalu < bitstream.sLayerInfo[layer].iNalCount; ++nalu) {
                size_t len = bitstream.sLayerInfo[layer].pNalLengthInByte[nalu];
                m_encapsulation.AddNALU(bitstream.sLayerInfo[layer].pBsBuf[4], len, bitstream.sLayerInfo[layer].pBsBuf);
                bitstream.sLayerInfo[layer].pBsBuf += len;
                if (bitstream.sLayerInfo[layer].uiQualityId > 0 && m_quality < bitstream.sLayerInfo[layer].uiQualityId)
                  m_quality = bitstream.sLayerInfo[layer].uiQualityId;
              }
            }
        }
      }

      // create RTP frame from destination buffer
      PluginCodec_RTP to(toPtr, toLen);
      if (!m_encapsulation.GetPacket(to, flags))
        return false;

      toLen = (unsigned)to.GetPacketSize();
      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

class H264_Decoder : public PluginVideoDecoder<MY_CODEC>
{
    typedef PluginVideoDecoder<MY_CODEC> BaseClass;

  protected:
    ISVCDecoder * m_decoder;
    SBufferInfo   m_bufferInfo;
    uint8_t     * m_bufferData[3];
    int           m_lastId;
    H264Frame     m_encapsulation;

  public:
    H264_Decoder(const PluginCodec_Definition * defn)
      : BaseClass(defn)
      , m_decoder(NULL)
      , m_lastId(-1)
    {
      memset(&m_bufferInfo, 0, sizeof(m_bufferInfo));
      memset(&m_bufferData, 0, sizeof(m_bufferData));
      CheckVersion(false);
    }


    ~H264_Decoder()
    {
      if (m_decoder != NULL)
        WelsDestroyDecoder(m_decoder);
    }


    virtual bool Construct()
    {
      if (WelsCreateDecoder(&m_decoder) != cmResultSuccess) {
        PTRACE(1, MY_CODEC_LOG, "Could not create decoder.");
        return false;
      }

#if PLUGINCODEC_TRACING
      m_decoder->SetOption(DECODER_OPTION_TRACE_CALLBACK, &TraceCallbackPtr);
      m_decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &TraceLevel);
#endif

      SDecodingParam param;
      memset(&param, 0, sizeof(param));
      param.eEcActiveIdc = ERROR_CON_DISABLE;		// Whether active error concealment feature in decoder
      param.sVideoProperty.size = sizeof(param.sVideoProperty);
      param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;

      int err = m_decoder->Initialize(&param);
      if (err != cmResultSuccess) {
        PTRACE(1, MY_CODEC_LOG, "Could not initialise decoder: error=" << err);
        return false;
      }

      PTRACE(4, MY_CODEC_LOG, "Opened decoder.");
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
      PluginCodec_RTP from(fromPtr, fromLen);

      if (from.GetPayloadSize() > 0 && !m_encapsulation.AddPacket(from, flags))
        return false;

      if (!from.GetMarker())
        return true;

      if (m_encapsulation.GetLength() > 0) {
        DECODING_STATE status = m_decoder->DecodeFrameNoDelay(m_encapsulation.GetBuffer(),
                                                        (int)m_encapsulation.GetLength(),
                                                        m_bufferData,
                                                        &m_bufferInfo);

        if (status != dsErrorFree) {
          if (status >= dsInvalidArgument) {
            PTRACE(1, MY_CODEC_LOG, "Fatal error decoding frame: status=0x" << std::hex << status);
            return false;
          }

          if (status & dsRefLost) {
            PTRACE(5, MY_CODEC_LOG, "Reference frame lost");
          }
          if (status & dsBitstreamError) {
            PTRACE(3, MY_CODEC_LOG, "Bit stream error decoding frame");
          }
          if (status & dsDepLayerLost) {
            PTRACE(5, MY_CODEC_LOG, "Dependent layer lost");
          }
          if (status & dsNoParamSets) {
            PTRACE(3, MY_CODEC_LOG, "No parameter sets received");
          }
          if (status & dsDataErrorConcealed) {
            PTRACE(4, MY_CODEC_LOG, "Data error concealed");
          }
          if (status >= dsDataErrorConcealed*2) {
            PTRACE(4, MY_CODEC_LOG, "Unknown error: status=0x" << std::hex << status);
          }
          if (status != dsDataErrorConcealed)
            flags = PluginCodec_ReturnCoderRequestIFrame;
        }

        m_encapsulation.Reset();
      }

      if (m_bufferInfo.iBufferStatus != 0) {
        PluginCodec_RTP out(toPtr, toLen);

        m_width = m_bufferInfo.UsrData.sSystemBuffer.iWidth;
        m_height = m_bufferInfo.UsrData.sSystemBuffer.iHeight;

        // Why make it so hard?
        int raster[3] = {
          m_bufferInfo.UsrData.sSystemBuffer.iStride[0],
          m_bufferInfo.UsrData.sSystemBuffer.iStride[1],
          m_bufferInfo.UsrData.sSystemBuffer.iStride[1]
        };
        toLen = OutputImage(m_bufferData,
                            raster,
                            m_bufferInfo.UsrData.sSystemBuffer.iWidth,
                            m_bufferInfo.UsrData.sSystemBuffer.iHeight,
                            out,
                            flags);

        if ((flags & PluginCodec_ReturnCoderBufferTooSmall) == 0) {
          int id = 0;
          long result = m_decoder->GetOption(DECODER_OPTION_IDR_PIC_ID, &id);
          if (result != cmResultSuccess)
            PTRACE(4, MY_CODEC_LOG, "Error determining key frame: 0x" << std::hex << result);
          else if (m_lastId != id) {
            m_lastId = id;
            flags |= PluginCodec_ReturnCoderIFrame;
          }

          m_bufferInfo.iBufferStatus = 0;
        }
      }

      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Definition CodecDefinition[] =
{
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Mode1, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Open0, H264_Encoder, H264_Decoder),
  PLUGINCODEC_VIDEO_CODEC_CXX(MyMediaFormatInfo_Open1, H264_Encoder, H264_Decoder),
};


PLUGIN_CODEC_IMPLEMENT_CXX(MY_CODEC, CodecDefinition);


/////////////////////////////////////////////////////////////////////////////
