/*
 * h265mf_inc.cxx
 *
 * H.265 Media Format descriptions
 *
 * Open Phone Abstraction Library
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2008 Vox Lucida
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
#include <vector>


///////////////////////////////////////////////////////////////////////////////

#ifdef MY_CODEC
  #define MY_CODEC_LOG STRINGIZE(MY_CODEC)
#else
  #define MY_CODEC_LOG "H.265"
#endif

static const char H265_FormatName[]       = OPAL_H265;
static const char H265EncodingName[] = "H265";

static const char TierName[] = "Tier";
static char const TierFMTPName[] = "tier-flag";
static char const LevelName[] = PLUGINCODEC_OPTION_LEVEL;
static char const LevelFMTPName[] = "level-id";
static char const ConstraintFlagsName[] = "Constraint Flags";
static char const ConstraintFMTPName[] = "interop-constraints";

#define DefaultTierStr      H265_TIER_STR_MAIN
#define DefaultTierInt      H265_TIER_INT_MAIN
#define DefaultLevelStr     H265_LEVEL_STR_3_1
#define DefaultLevelInt     93

#define H265_TIER_STR_MAIN  "Main"
#define H265_TIER_STR_HIGH  "High"

#define H265_TIER_INT_MAIN  0
#define H265_TIER_INT_HIGH  1

#define H265_LEVEL_STR_1    "1"
#define H265_LEVEL_STR_2    "2"
#define H265_LEVEL_STR_2_1  "2.1"
#define H265_LEVEL_STR_3    "3"
#define H265_LEVEL_STR_3_1  "3.1"
#define H265_LEVEL_STR_4    "4"
#define H265_LEVEL_STR_4_1  "4.1"
#define H265_LEVEL_STR_5    "5"
#define H265_LEVEL_STR_5_1  "5.1"
#define H265_LEVEL_STR_5_2  "5.2"
#define H265_LEVEL_STR_6    "6"
#define H265_LEVEL_STR_6_1  "6.1"
#define H265_LEVEL_STR_6_2  "6.2"

static struct
{
  char     m_Name[9];
  unsigned m_H265;
} const TierInfo[] = {
  { H265_TIER_STR_MAIN, H265_TIER_INT_MAIN },
  { H265_TIER_STR_HIGH, H265_TIER_INT_HIGH }
};

static struct LevelInfoStruct
{
  char     m_Name[4];
  unsigned m_H265;
  unsigned m_MaxFrameSize;   // In macroblocks
  unsigned m_MaxWidthHeight; // sqrt(m_MaxFrameSize*8)*16
  unsigned m_MaxMBPS;        // In macroblocks/second
  unsigned m_MaxBitRate;
} const LevelInfo[] = {
  // From H.265 specification
  //                     Lev    FS   W/H     MBPS             BR
  { H265_LEVEL_STR_1,    30,    99,  448,    1485,     64 * 1200 },
  { H265_LEVEL_STR_2,    60,   396,  896,   11880,   2000 * 1200 },
  { H265_LEVEL_STR_2_1,  63,   792, 1264,   19800,   4000 * 1200 },
  { H265_LEVEL_STR_3,    90,  1620, 1808,   40500,  10000 * 1200 },
  { H265_LEVEL_STR_3_1,  93,  3600, 2704,  108000,  14000 * 1200 },
  { H265_LEVEL_STR_4,   120,  8192, 4096,  245760,  20000 * 1200 },
  { H265_LEVEL_STR_4_1, 123,  8192, 4096,  245760,  50000 * 1200 },
  { H265_LEVEL_STR_5,   150, 22080, 6720,  589824, 135000 * 1200 },
  { H265_LEVEL_STR_5_1, 153, 36864, 8320,  983040, 240000 * 1200 },
  { H265_LEVEL_STR_5_2, 156, 36864, 8320, 2073600, 240000 * 1200 },
  { H265_LEVEL_STR_6,   180, 22080, 6720,  589824, 135000 * 1200 },
  { H265_LEVEL_STR_6_1, 183, 36864, 8320,  983040, 240000 * 1200 },
  { H265_LEVEL_STR_6_2, 186, 36864, 8320, 2073600, 240000 * 1200 },
};


///////////////////////////////////////////////////////////////////////////////

static bool MyToNormalised(PluginCodec_OptionMap & original, PluginCodec_OptionMap & changed)
{
  unsigned tier = original.GetUnsigned(TierFMTPName);
  if (tier > 1)
    return false;

  unsigned tierIndex = 0;
  while (tierIndex < sizeof(TierInfo)/sizeof(TierInfo[0])-1 && TierInfo[tierIndex].m_H265 < tier)
    ++tierIndex;

  unsigned level = original.GetUnsigned(LevelFMTPName, 93); // Level 3.1
  if (level > 255)
    return false;

  unsigned levelIndex = 0;
  while (levelIndex < sizeof(LevelInfo)/sizeof(LevelInfo[0])-1 && LevelInfo[levelIndex].m_H265 < level)
    ++levelIndex;

  PluginCodec_Utilities::Change(TierInfo[tierIndex].m_Name, original, changed, TierName);
  PluginCodec_Utilities::Change(LevelInfo[levelIndex].m_Name, original, changed, LevelName);
  return true;
}


static bool MyToCustomised(PluginCodec_OptionMap & original, PluginCodec_OptionMap & changed)
{
  std::string str = original[TierName];
  unsigned tierIndex = sizeof(TierInfo)/sizeof(TierInfo[0])-1;
  while (tierIndex > 0 && str != TierInfo[tierIndex].m_Name)
    --tierIndex;

  str = original[LevelName];
  if (str.empty())
    str = H265_LEVEL_STR_3_1;

  size_t levelIndex = sizeof(LevelInfo)/sizeof(LevelInfo[0])-1;
  while (levelIndex > 0 && str != LevelInfo[levelIndex].m_Name)
    --levelIndex;

  PluginCodec_Utilities::Change(TierInfo[tierIndex].m_H265, original, changed, TierFMTPName);
  PluginCodec_Utilities::Change(LevelInfo[levelIndex].m_H265, original, changed, LevelFMTPName);
  return true;
}


// End of File ///////////////////////////////////////////////////////////////
