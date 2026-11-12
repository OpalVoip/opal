/*
 * VP9 video codec plugin for OPAL
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
 * Contributor(s): Robert Jongbloed (robertj@voxlucida.com.au)
 *
 */

#include "../common/platform.h"

#define MY_CODEC VP9  // Name of codec (use C variable characters)

#include "../../../src/codec/vp9mf_inc.cxx"

#include <codec/opalplugin.hpp>
#include "../common/critsect.h"

#ifdef _MSC_VER
#pragma warning(disable:4505)
#define snprintf _snprintf
#endif

/* Error concealment does not work with this packetisation. Disable it or
   vpx_codec_decode eventually returns error 5 (bitstream not supported). */
#ifdef VPX_CODEC_USE_ERROR_CONCEALMENT
#undef VPX_CODEC_USE_ERROR_CONCEALMENT
#endif

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_decoder.h"
#if defined(__has_include)
#  if __has_include(<vpx/vp9cx.h>)
#    include "vpx/vp9cx.h"
#  else
#    include "vpx/vp8cx.h" // libvpx publishes vpx_codec_vp9_cx() from here
#  endif
#  if __has_include(<vpx/vp9dx.h>)
#    include "vpx/vp9dx.h"
#  else
#    include "vpx/vp8dx.h" // libvpx publishes vpx_codec_vp9_dx() from here
#  endif
#else
#  include "vpx/vp8cx.h"
#  include "vpx/vp8dx.h"
#endif

#include <vector>
#include <stdio.h>


class VP9_CODEC { };

PLUGINCODEC_CONTROL_LOG_FUNCTION_DEF


PLUGINCODEC_LICENSE(
  "Robert Jongbloed, Vox Lucida Pty.Ltd.",                      // source code author
  "1.0",                                                        // source code version
  "robertj@voxlucida.com.au",                                   // source code email
  "http://www.voxlucida.com.au",                                // source code URL
  "Copyright (C) 2026 by Vox Lucida Pt.Ltd., All Rights Reserved", // source code copyright
  "MPL 1.0",                                                    // source code license
  PluginCodec_License_MPL,                                      // source code license

  "VP9 Video Codec",                                            // codec description
  "The WebM Project Authors",                                   // codec author
  "9",                                                          // codec version
  "https://www.webmproject.org/code/bug-reporting/",            // codec email
  "http://www.webmproject.org/",                                // codec URL
  "Copyright (c) The WebM Project Authors",                     // codec copyright information
  "Copyright (c) The WebM Project Authors",                     // codec license
  PluginCodec_License_BSD                                       // codec license code
);


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Option const MaxFR =
{
  PluginCodec_IntegerOption,          // Option type
  MaxFrameRateName,                   // User visible name
  false,                              // User Read/Only flag
  PluginCodec_MinMerge,               // Merge mode
  STRINGIZE(MAX_FR_SDP),              // Initial value
  MaxFrameRateSDP,                    // FMTP option name
  STRINGIZE(MAX_FR_SDP),              // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  STRINGIZE(MIN_FR_SDP),              // Minimum value
  STRINGIZE(MAX_FR_SDP)               // Maximum value
};

static struct PluginCodec_Option const MaxFS =
{
  PluginCodec_IntegerOption,          // Option type
  MaxFrameSizeName,                   // User visible name
  false,                              // User Read/Only flag
  PluginCodec_MinMerge,               // Merge mode
  STRINGIZE(MAX_FS_SDP),              // Initial value
  MaxFrameSizeSDP,                    // FMTP option name
  STRINGIZE(MAX_FS_SDP),              // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  STRINGIZE(MIN_FS_SDP),              // Minimum value
  STRINGIZE(MAX_FS_SDP)               // Maximum value
};

static struct PluginCodec_Option const ProfileId =
{
  PluginCodec_IntegerOption,          // Option type
  ProfileIdName,                      // User visible name
  false,                              // User Read/Only flag
  PluginCodec_EqualMerge,             // Merge mode
  STRINGIZE(SUPPORTED_PROFILE_ID),    // Initial value
  ProfileIdSDP,                       // FMTP option name
  STRINGIZE(SUPPORTED_PROFILE_ID),    // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  STRINGIZE(MIN_PROFILE_ID),          // Minimum value
  STRINGIZE(MAX_PROFILE_ID)           // Maximum value
};

static struct PluginCodec_Option const TemporalSpatialTradeOff =
{
  PluginCodec_IntegerOption,          // Option type
  PLUGINCODEC_OPTION_TEMPORAL_SPATIAL_TRADE_OFF, // User visible name
  false,                              // User Read/Only flag
  PluginCodec_AlwaysMerge,            // Merge mode
  "31",                               // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  "1",                                // Minimum value
  "31"                                // Maximum value
};

static struct PluginCodec_Option const SpatialResampling =
{
  PluginCodec_BoolOption,             // Option type
  SpatialResamplingsName,             // User visible name
  false,                              // User Read/Only flag
  PluginCodec_AndMerge,               // Merge mode
  "0",                                // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0                                   // H.245 generic capability code and bit mask
};

static struct PluginCodec_Option const SpatialResamplingUp =
{
  PluginCodec_IntegerOption,          // Option type
  SpatialResamplingUpName,            // User visible name
  false,                              // User Read/Only flag
  PluginCodec_AlwaysMerge,            // Merge mode
  "100",                              // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  "0",                                // Minimum value
  "100"                               // Maximum value
};

static struct PluginCodec_Option const SpatialResamplingDown =
{
  PluginCodec_IntegerOption,          // Option type
  SpatialResamplingDownName,          // User visible name
  false,                              // User Read/Only flag
  PluginCodec_AlwaysMerge,            // Merge mode
  "0",                                // Initial value
  NULL,                               // FMTP option name
  NULL,                               // FMTP default value
  0,                                  // H.245 generic capability code and bit mask
  "0",                                // Minimum value
  "100"                               // Maximum value
};

static struct PluginCodec_Option const * OptionTable[] = {
  &MaxFR,
  &MaxFS,
  &ProfileId,
  &TemporalSpatialTradeOff,
  &SpatialResampling,
  &SpatialResamplingUp,
  &SpatialResamplingDown,
  NULL
};


///////////////////////////////////////////////////////////////////////////////

class VP9Format : public PluginCodec_VideoFormat<VP9_CODEC>
{
  public:
    VP9Format()
      : PluginCodec_VideoFormat<VP9_CODEC>(VP9FormatName, VP9EncodingName, "VP9 Video Codec (RFC 9628)", MaxBitRate, OptionTable)
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

static VP9Format const VP9MediaFormatInfo;


///////////////////////////////////////////////////////////////////////////////

static bool IsError(vpx_codec_err_t err, const char * fn)
{
  if (err == VPX_CODEC_OK)
    return false;

  PTRACE(1, MY_CODEC_LOG, "Error " << err << " in " << fn << " - " << vpx_codec_err_to_string(err));
  return true;
}

#define IS_ERROR(fn, args) IsError(fn args, #fn)


///////////////////////////////////////////////////////////////////////////////

class VP9Encoder : public PluginVideoEncoder<VP9_CODEC>
{
  private:
    typedef PluginVideoEncoder<VP9_CODEC> BaseClass;

  protected:
    vpx_codec_enc_cfg_t        m_config;
    unsigned                   m_maxQ;
    unsigned                   m_rateControlPeriod;
    vpx_codec_ctx_t            m_codec;
    vpx_codec_iter_t           m_iterator;
    const vpx_codec_cx_pkt_t * m_packet;
    size_t                     m_offset;
    unsigned                   m_pictureId;

    CriticalSection m_mutex;

  public:
    VP9Encoder(const PluginCodec_Definition * defn)
      : BaseClass(defn)
      , m_maxQ(63)
      , m_rateControlPeriod(1000)
      , m_iterator(NULL)
      , m_packet(NULL)
      , m_offset(0)
      , m_pictureId(rand() & 0x7fff)
    {
      memset(&m_codec, 0, sizeof(m_codec));
    }


    ~VP9Encoder()
    {
      vpx_codec_destroy(&m_codec);
    }


    virtual bool Construct()
    {
      if (IS_ERROR(vpx_codec_enc_config_default, (vpx_codec_vp9_cx(), &m_config, 0)))
        return false;

      m_maxBitRate = m_config.rc_target_bitrate*1000;
      m_maxQ = m_config.rc_max_quantizer;

      m_config.g_profile = SUPPORTED_PROFILE_ID;
      m_config.g_w = 0; // Forces OnChangedOptions to initialise encoder
      m_config.g_h = 0;
      m_config.g_error_resilient = 1;
      m_config.g_pass = VPX_RC_ONE_PASS;
      m_config.g_lag_in_frames = 0;
      m_config.rc_end_usage = VPX_CBR;
      m_config.g_timebase.num = 1;
      m_config.g_timebase.den = PLUGINCODEC_VIDEO_CLOCK;
      m_config.rc_undershoot_pct = 95;

      if (!OnChangedOptions())
        return false;

      PTRACE(4, MY_CODEC_LOG, "Encoder opened: " << vpx_codec_version_str());
      return true;
    }


    virtual bool SetOption(const char * optionName, const char * optionValue)
    {
      if (strcasecmp(optionName, PLUGINCODEC_OPTION_RATE_CONTROL_PERIOD) == 0)
        return SetOptionUnsigned(m_rateControlPeriod, optionValue, 100, 60000);

      if (strcasecmp(optionName, ProfileId.m_name) == 0)
        return SetOptionUnsigned(m_config.g_profile, optionValue, SUPPORTED_PROFILE_ID, SUPPORTED_PROFILE_ID);

      if (strcasecmp(optionName, SpatialResampling.m_name) == 0)
        return SetOptionBoolean(m_config.rc_resize_allowed, optionValue);

      if (strcasecmp(optionName, SpatialResamplingUp.m_name) == 0)
        return SetOptionUnsigned(m_config.rc_resize_up_thresh, optionValue, 0, 100);

      if (strcasecmp(optionName, SpatialResamplingDown.m_name) == 0)
        return SetOptionUnsigned(m_config.rc_resize_down_thresh, optionValue, 0, 100);

      return BaseClass::SetOption(optionName, optionValue);
    }


    virtual bool OnChangedOptions()
    {
      WaitAndSignal lock(m_mutex);

      m_config.g_profile = SUPPORTED_PROFILE_ID;
      m_config.kf_mode = VPX_KF_AUTO;
      if (m_keyFramePeriod != 0)
        m_config.kf_min_dist = m_config.kf_max_dist = m_keyFramePeriod;
      else {
        m_config.kf_min_dist = 0;
        m_config.kf_max_dist = 10*PLUGINCODEC_VIDEO_CLOCK/m_frameTime;  // No slower than once every 10 seconds
      }

      m_config.rc_target_bitrate = m_maxBitRate/1000;
      m_config.rc_buf_sz = m_rateControlPeriod;
      m_config.rc_buf_initial_sz = m_rateControlPeriod*3/5;
      m_config.rc_buf_optimal_sz = m_rateControlPeriod*4/5;

      // Take simple temporal/spatial trade off and set multiple variables
      if (m_tsto < 31) {
        m_config.rc_dropframe_thresh = 60 - m_tsto;     // Range from 60% to 30%
        m_config.rc_max_quantizer = m_maxQ*m_tsto/31;
      }
      else {
        m_config.rc_dropframe_thresh = 0;   // m_tsto==31 is maintain frame rate, so threshold is zero
        m_config.rc_max_quantizer = m_maxQ; // Allow Q to get as bad as it can get
      }

      PTRACE(4, MY_CODEC_LOG, "Config: " << m_width << 'x' << m_height << ","
                              " profile=" << m_config.g_profile << ","
                              " rc_target_bitrate=" << m_config.rc_target_bitrate << ","
                              " kf_dist=" << m_config.kf_min_dist << '-' << m_config.kf_max_dist << ","
                              " rc_dropframe_thresh=" << m_config.rc_dropframe_thresh << ","
                              " rc_resize_allowed=" << m_config.rc_resize_allowed << ","
                              " rc_resize_up_thresh=" << m_config.rc_resize_up_thresh << ","
                              " rc_resize_down_thresh=" << m_config.rc_resize_down_thresh << ","
                              " rc_min_quantizer=" << m_config.rc_min_quantizer << ","
                              " rc_max_quantizer=" << m_config.rc_max_quantizer << ","
                              " rc_buf_sz=" << m_config.rc_buf_sz << ","
                              " rc_buf_initial_sz=" << m_config.rc_buf_initial_sz << ","
                              " rc_buf_optimal_sz=" << m_config.rc_buf_optimal_sz << ","
                              " rc_undershoot_pct=" << m_config.rc_undershoot_pct << ","
                              " rc_overshoot_pct=" << m_config.rc_overshoot_pct);

      if (m_config.g_w == m_width && m_config.g_h == m_height)
        return !IS_ERROR(vpx_codec_enc_config_set, (&m_codec, &m_config));

      m_config.g_w = m_width;
      m_config.g_h = m_height;
      vpx_codec_destroy(&m_codec);
      return !IS_ERROR(vpx_codec_enc_init, (&m_codec, vpx_codec_vp9_cx(), &m_config, 0));
    }


    bool NeedEncode()
    {
      if (m_packet != NULL)
        return false;

      WaitAndSignal lock(m_mutex);

      while ((m_packet = vpx_codec_get_cx_data(&m_codec, &m_iterator)) != NULL) {
        if (m_packet->kind == VPX_CODEC_CX_FRAME_PKT)
          return false;
      }

      m_iterator = NULL;
      return true;
    }


    int LastQuantizer()
    {
#if defined(VP9E_GET_LAST_QUANTIZER_64)
      int quality = -1;
      IS_ERROR(vpx_codec_control_VP9E_GET_LAST_QUANTIZER_64, (&m_codec, VP9E_GET_LAST_QUANTIZER_64, &quality));
      return quality;
#elif defined(VP9E_GET_LAST_QUANTIZER_SVC_LAYERS)
      // Current libvpx reports the base-layer quantizer through this control.
      int layers[VPX_SS_MAX_LAYERS];
      for (unsigned i = 0; i < VPX_SS_MAX_LAYERS; ++i)
        layers[i] = -1;
      IS_ERROR(vpx_codec_control_VP9E_GET_LAST_QUANTIZER_SVC_LAYERS, (&m_codec, VP9E_GET_LAST_QUANTIZER_SVC_LAYERS, layers));
      return layers[0];
#else
      return -1;
#endif
    }


    virtual int GetStatistics(char * bufferPtr, unsigned bufferSize)
    {
      size_t len = BaseClass::GetStatistics(bufferPtr, bufferSize);
      len += snprintf(bufferPtr+len, bufferSize-len, "Width=%u\nHeight=%u\n", m_width, m_height);

      WaitAndSignal lock(m_mutex);

      int quality = LastQuantizer();
      if (quality >= 0 && len < bufferSize)
        len += snprintf(bufferPtr+len, bufferSize-len, "Quality=%u\n", quality);

      return (int)len;
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      if (NeedEncode()) {
        PluginCodec_RTP srcRTP(fromPtr, fromLen);
        PluginCodec_Video_FrameHeader * video = srcRTP.GetVideoHeader();

        if (m_width != video->width || m_height != video->height) {
          PTRACE(4, MY_CODEC_LOG, "Changing resolution from " <<
                 m_width << 'x' << m_height << " to " << video->width << 'x' << video->height);
          m_width = video->width;
          m_height = video->height;
          if (!OnChangedOptions())
            return false;
        }

        vpx_image_t image;
        vpx_img_wrap(&image, VPX_IMG_FMT_I420, video->width, video->height, 2, srcRTP.GetVideoFrameData());

        {
          WaitAndSignal lock(m_mutex);

          if (IS_ERROR(vpx_codec_encode, (&m_codec, &image,
                                          srcRTP.GetTimestamp(), m_frameTime,
                                          (flags&PluginCodec_CoderForceIFrame) != 0 ? VPX_EFLAG_FORCE_KF : 0,
                                          VPX_DL_REALTIME)))
            return false;
        }

        if (NeedEncode()) {
          // Skipping frame completely.
          flags = PluginCodec_ReturnCoderLastFrame;
          toLen = 0;
          return true;
        }
      }

      flags = 0;
      if ((m_packet->data.frame.flags&VPX_FRAME_IS_KEY) != 0)
        flags |= PluginCodec_ReturnCoderIFrame;

      PluginCodec_RTP dstRTP(toPtr, toLen);
      Packetise(dstRTP);
      toLen = (unsigned)dstRTP.GetPacketSize();

      if (m_offset >= m_packet->data.frame.sz) {
        flags |= PluginCodec_ReturnCoderLastFrame;
        dstRTP.SetMarker(true);
        m_packet = NULL;
        m_offset = 0;
      }

      return true;
    }


    // RFC 9628 non-flexible descriptor: I, 15-bit picture ID, B/E, no layers.
    void Packetise(PluginCodec_RTP & rtp)
    {
      const size_t headerSize = 3;

      rtp[0] = 0x80; // I
      if ((m_packet->data.frame.flags & VPX_FRAME_IS_KEY) == 0)
        rtp[0] |= 0x40; // P: inter-picture predicted
      if (m_offset == 0)
        rtp[0] |= 0x08; // B: start of frame
      if ((m_packet->data.frame.flags & VPX_FRAME_IS_DROPPABLE) != 0)
        rtp[0] |= 0x01; // Z

      rtp[1] = (uint8_t)(0x80 | ((m_pictureId >> 8) & 0x7f)); // M and high 7 bits
      rtp[2] = (uint8_t)m_pictureId;

      size_t fragmentSize = GetPacketSpace(rtp, m_packet->data.frame.sz - m_offset + headerSize) - headerSize;
      if (m_offset + fragmentSize >= m_packet->data.frame.sz)
        rtp[0] |= 0x04; // E: end of frame

      rtp.CopyPayload((char *)m_packet->data.frame.buf+m_offset, fragmentSize, headerSize);
      m_offset += fragmentSize;

      if (m_offset >= m_packet->data.frame.sz && ++m_pictureId >= 0x8000)
        m_pictureId = 0;
    }
};


///////////////////////////////////////////////////////////////////////////////

struct Vp9Descriptor
{
  bool     start;
  bool     end;
  bool     key;
  unsigned spatialId;
  size_t   headerSize;
};


static bool ParseVp9Descriptor(const PluginCodec_RTP & rtp, Vp9Descriptor & desc)
{
  size_t size = rtp.GetPayloadSize();
  if (size < 1)
    return false;

  unsigned b0 = rtp[0];
  desc.start = (b0 & 0x08) != 0;
  desc.end = (b0 & 0x04) != 0;
  desc.key = (b0 & 0x40) == 0;
  desc.spatialId = 0;

  size_t offset = 1;
  bool flexible = (b0 & 0x10) != 0;

  if ((b0 & 0x80) != 0) { // I: picture ID
    if (offset >= size)
      return false;
    offset += (rtp[offset] & 0x80) != 0 ? 2 : 1;
    if (offset > size)
      return false;
  }

  if ((b0 & 0x20) != 0) { // L: layer indices, plus TL0PICIDX when F is clear
    if (offset >= size)
      return false;
    desc.spatialId = (rtp[offset] >> 1) & 7;
    offset += flexible ? 1 : 2;
    if (offset > size)
      return false;
  }

  if (flexible && (b0 & 0x40) != 0) { // P_DIFF, up to three
    unsigned count = 0;
    for (;;) {
      if (offset >= size || count >= 3)
        return false;
      unsigned diff = rtp[offset++];
      ++count;
      if ((diff & 0x01) == 0)
        break;
    }
  }

  if ((b0 & 0x02) != 0) { // V: scalability structure
    if (offset >= size)
      return false;
    unsigned ss = rtp[offset++];
    if ((ss & 0x10) != 0) {
      offset += ((ss >> 5) + 1) * 4; // WIDTH and HEIGHT per spatial layer
      if (offset > size)
        return false;
    }
    if ((ss & 0x08) != 0) {
      if (offset >= size)
        return false;
      unsigned pictures = rtp[offset++];
      for (unsigned i = 0; i < pictures; ++i) {
        if (offset >= size)
          return false;
        unsigned refs = (rtp[offset++] >> 2) & 3;
        offset += refs;
        if (offset > size)
          return false;
      }
    }
  }

  if (offset >= size)
    return false;

  desc.headerSize = offset;
  return true;
}


///////////////////////////////////////////////////////////////////////////////

class VP9Decoder : public PluginVideoDecoder<VP9_CODEC>
{
  private:
    typedef PluginVideoDecoder<VP9_CODEC> BaseClass;

  protected:
    vpx_codec_iface_t  * m_iface;
    vpx_codec_ctx_t      m_codec;
    vpx_codec_flags_t    m_flags;
    vpx_codec_iter_t     m_iterator;
    std::vector<uint8_t> m_fullFrame;
    bool                 m_firstFrame;
    bool                 m_intraFrame;
    bool                 m_ignoreTillKeyFrame;
    bool                 m_frameComplete;
    unsigned             m_consecutiveErrors;

  public:
    VP9Decoder(const PluginCodec_Definition * defn)
      : BaseClass(defn)
      , m_iface(vpx_codec_vp9_dx())
      , m_flags(0)
      , m_iterator(NULL)
      , m_firstFrame(true)
      , m_intraFrame(false)
      , m_ignoreTillKeyFrame(false)
      , m_frameComplete(false)
      , m_consecutiveErrors(0)
    {
      memset(&m_codec, 0, sizeof(m_codec));
      m_fullFrame.reserve(10000);
    }


    ~VP9Decoder()
    {
      vpx_codec_destroy(&m_codec);
    }


    virtual bool Construct()
    {
      if (IS_ERROR(vpx_codec_dec_init, (&m_codec, m_iface, NULL, m_flags)))
        return false;

      PTRACE(4, MY_CODEC_LOG, "Decoder opened: " << vpx_codec_version_str());
      return true;
    }


    virtual int GetStatistics(char * bufferPtr, unsigned bufferSize)
    {
      size_t len = BaseClass::GetStatistics(bufferPtr, bufferSize);
      len += snprintf(bufferPtr+len, bufferSize-len, "Width=%u\nHeight=%u\n", m_width, m_height);
      return (int)len;
    }


    bool BadDecode(unsigned & flags, bool ok)
    {
      if (ok)
        return false;

      flags = PluginCodec_ReturnCoderRequestIFrame;
      m_ignoreTillKeyFrame = true;
      m_frameComplete = false;
      m_fullFrame.clear();
      return true;
    }


    virtual bool Transcode(const void * fromPtr,
                             unsigned & fromLen,
                                 void * toPtr,
                             unsigned & toLen,
                             unsigned & flags)
    {
      vpx_image_t * image;

      bool noLostPackets = (flags & PluginCodec_CoderPacketLoss) == 0;

      flags = m_intraFrame ? PluginCodec_ReturnCoderIFrame : 0;

      if (m_firstFrame || (image = vpx_codec_get_frame(&m_codec, &m_iterator)) == NULL) {
        if (BadDecode(flags, noLostPackets))
          return true;

        PluginCodec_RTP srcRTP(fromPtr, fromLen);
        if (BadDecode(flags, Unpacketise(srcRTP)))
          return true;

        if ((!srcRTP.GetMarker() && !m_frameComplete) || m_fullFrame.empty())
          return true;

        m_frameComplete = false;

        vpx_codec_err_t err = vpx_codec_decode(&m_codec, &m_fullFrame[0], (unsigned)m_fullFrame.size(), NULL, 0);
        switch (err) {
          case VPX_CODEC_OK :
            m_consecutiveErrors = 0;
            break;

          case VPX_CODEC_UNSUP_BITSTREAM :
            if (m_consecutiveErrors++ > 10) {
              IsError(err, "vpx_codec_decode");
              return false;
            }

          // Non fatal errors
          case VPX_CODEC_UNSUP_FEATURE :
          case VPX_CODEC_CORRUPT_FRAME :
            PTRACE(3, MY_CODEC_LOG, "Decoder reported non-fatal error: " << vpx_codec_err_to_string(err));
            BadDecode(flags, false);
            return true;

          default:
            IsError(err, "vpx_codec_decode");
            return false;
        }

        m_intraFrame = false;
        vpx_codec_stream_info_t info;
        memset(&info, 0, sizeof(info));
        info.sz = sizeof(info);
        vpx_codec_err_t peekErr = vpx_codec_peek_stream_info(m_iface, &m_fullFrame[0], (unsigned)m_fullFrame.size(), &info);
        if (peekErr == VPX_CODEC_OK)
          m_intraFrame = info.is_kf != 0;
        else
          PTRACE(3, MY_CODEC_LOG, "vpx_codec_peek_stream_info: " << vpx_codec_err_to_string(peekErr));

        if (m_intraFrame)
          flags |= PluginCodec_ReturnCoderIFrame;

        m_fullFrame.clear();

        m_iterator = NULL;
        if ((image = vpx_codec_get_frame(&m_codec, &m_iterator)) == NULL)
          return true;

        m_firstFrame = false;
      }

      if (image->fmt != VPX_IMG_FMT_I420) {
        PTRACE(1, MY_CODEC_LOG, "Unsupported image format from decoder.");
        return false;
      }

      m_width = image->d_w;
      m_height = image->d_h;

      PluginCodec_RTP dstRTP(toPtr, toLen);
      toLen = OutputImage(image->planes, image->stride, image->d_w, image->d_h, dstRTP, flags);

      if ((flags & PluginCodec_ReturnCoderLastFrame) != 0)
        m_intraFrame = false;

      return true;
    }


    void Accumulate(const unsigned char * fragmentPtr, size_t fragmentLen)
    {
      if (fragmentLen == 0)
        return;

      size_t size = m_fullFrame.size();
      m_fullFrame.reserve(size+fragmentLen*2);
      m_fullFrame.resize(size+fragmentLen);
      memcpy(&m_fullFrame[size], fragmentPtr, fragmentLen);
    }


    bool Unpacketise(const PluginCodec_RTP & rtp)
    {
      if (rtp.GetPayloadSize() == 0)
        return true;

      Vp9Descriptor desc;
      if (!ParseVp9Descriptor(rtp, desc)) {
        PTRACE(3, MY_CODEC_LOG, "Malformed VP9 payload descriptor.");
        return false;
      }

      // Single spatial layer. Higher layers are not part of the frame we decode.
      if (desc.spatialId != 0)
        return true;

      if (m_ignoreTillKeyFrame) {
        if (!desc.start || !desc.key)
          return false;
        m_ignoreTillKeyFrame = false;
        m_fullFrame.clear();
        PTRACE(3, MY_CODEC_LOG, "Found next start of key frame.");
      }
      else if (desc.start && !m_fullFrame.empty()) {
        if (!desc.key) {
          PTRACE(3, MY_CODEC_LOG, "Start bit seen, but not completed previous frame, ignoring till next key frame.");
          return false;
        }

        PTRACE(3, MY_CODEC_LOG, "Start bit seen, but not completed previous frame, restarting key frame.");
        m_fullFrame.clear();
      }

      Accumulate(rtp.GetPayloadPtr()+desc.headerSize, rtp.GetPayloadSize()-desc.headerSize);
      if (desc.end)
        m_frameComplete = true;
      return true;
    }
};


///////////////////////////////////////////////////////////////////////////////

static struct PluginCodec_Definition VP9CodecDefinition[] =
{
  PLUGINCODEC_VIDEO_CODEC_CXX(VP9MediaFormatInfo, VP9Encoder, VP9Decoder)
};

PLUGIN_CODEC_IMPLEMENT_CXX(VP9_CODEC, VP9CodecDefinition);


/////////////////////////////////////////////////////////////////////////////
