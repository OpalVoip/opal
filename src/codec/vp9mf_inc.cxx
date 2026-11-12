/*
 * vp9mf_inc.cxx
 *
 * VP9 Media Format descriptions
 *
 * Open Phone Abstraction Library
 *
 * Copyright (c) 2026 Vox Lucida Pty. Ltd.
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
 * The Original Code is Open Phone Abstraction Library
 *
 * The Initial Developer of the Original Code is Vox Lucida
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <codec/opalplugin.hpp>
#include <codec/known.h>

#include <stdio.h>


///////////////////////////////////////////////////////////////////////////////

#ifdef MY_CODEC
  #define MY_CODEC_LOG STRINGIZE(MY_CODEC)
#else
  #define MY_CODEC_LOG "VP9"
#endif

static const char VP9FormatName[] = OPAL_VP9;
static const char VP9EncodingName[] = "VP9";
static const char MaxFrameRateName[] = "Max Frame Rate";
static const char MaxFrameRateSDP[] = "max-fr";
static const char MaxFrameSizeName[] = "Max Frame Size";
static const char MaxFrameSizeSDP[] = "max-fs";
static const char ProfileIdName[] = "Profile ID";
static const char ProfileIdSDP[] = "profile-id";
static const char SpatialResamplingsName[] = "Spatial Resampling";
static const char SpatialResamplingUpName[] = "Spatial Resampling Up";
static const char SpatialResamplingDownName[] = "Spatial Resampling Down";

const unsigned MaxBitRate = 16000000;
#define MIN_FR_SDP 1
#define MAX_FR_SDP 30
#define MIN_FS_SDP 48
#define MAX_FS_SDP 65536
#define MIN_PROFILE_ID 0
#define MAX_PROFILE_ID 3
#define SUPPORTED_PROFILE_ID 0


/////////////////////////////////////////////////////////////////////////////

static bool MyToNormalised(PluginCodec_OptionMap & original, PluginCodec_OptionMap & changed)
{
  PluginCodec_OptionMap::iterator it = original.find(MaxFrameSizeName);
  if (it != original.end() && !it->second.empty()) {
    unsigned maxWidth = original.GetUnsigned(PLUGINCODEC_OPTION_MAX_RX_FRAME_WIDTH);
    unsigned maxHeight = original.GetUnsigned(PLUGINCODEC_OPTION_MAX_RX_FRAME_HEIGHT);
    unsigned maxFrameSize = PluginCodec_Utilities::String2Unsigned(it->second) % MAX_FS_SDP;
    if (PluginCodec_Utilities::ClampResolution(original, changed, maxWidth, maxHeight, maxFrameSize))
      PluginCodec_Utilities::Change(maxFrameSize, original, changed, MaxFrameSizeName);
  }

  it = original.find(MaxFrameRateName);
  if (it != original.end() && !it->second.empty())
    PluginCodec_Utilities::ClampMin(PLUGINCODEC_VIDEO_CLOCK/PluginCodec_Utilities::String2Unsigned(it->second), original, changed, PLUGINCODEC_OPTION_FRAME_TIME);

  // RFC 9628: an absent profile-id means profile 0. This codec is profile 0 only.
  it = original.find(ProfileIdName);
  if (it == original.end() || it->second.empty())
    PluginCodec_Utilities::Change((unsigned)SUPPORTED_PROFILE_ID, original, changed, ProfileIdName);
  else if (PluginCodec_Utilities::String2Unsigned(it->second) != SUPPORTED_PROFILE_ID)
    return false;

  return true;
}


static bool MyToCustomised(PluginCodec_OptionMap & original, PluginCodec_OptionMap & changed)
{
  PluginCodec_Utilities::Change(PluginCodec_Utilities::GetMacroBlocks(
                                    original.GetUnsigned(PLUGINCODEC_OPTION_MAX_RX_FRAME_WIDTH),
                                    original.GetUnsigned(PLUGINCODEC_OPTION_MAX_RX_FRAME_HEIGHT)),
                                original, changed, MaxFrameSizeName);

  PluginCodec_Utilities::Change(PLUGINCODEC_VIDEO_CLOCK/original.GetUnsigned(PLUGINCODEC_OPTION_FRAME_TIME),
                                original, changed, MaxFrameRateName);

  PluginCodec_Utilities::Change((unsigned)SUPPORTED_PROFILE_ID, original, changed, ProfileIdName);

  return true;
}


// End of File ///////////////////////////////////////////////////////////////
