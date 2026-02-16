/*
 * h265mf.cxx
 *
 * H.265 Media Format descriptions
 *
 * Open Phone Abstraction Library
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2025 Vox Lucida
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

#include <ptlib.h>

#include <opal_config.h>

#if OPAL_VIDEO

#include "h265mf_inc.cxx"

#include <codec/h265mf.h>


OpalH265FormatInternal::OpalH265FormatInternal(const char * name)
  : OpalVideoFormatInternal(name, RTP_DataFrame::DynamicBase, H265EncodingName,
                            PVideoFrameInfo::MaxWidth, PVideoFrameInfo::MaxHeight, 30, 16000000)
{
  OpalMediaOption * option;
  static const char * const tiers[] = {
    H265_TIER_STR_MAIN,
    H265_TIER_STR_HIGH
  };
  AddOption(option = new OpalMediaOptionEnum(TierName, false, tiers, PARRAYSIZE(tiers), OpalMediaOption::MinMerge, DefaultTierInt));

  static const char * const levels[] = {
    H265_LEVEL_STR_1,
    H265_LEVEL_STR_2,
    H265_LEVEL_STR_2_1,
    H265_LEVEL_STR_3,
    H265_LEVEL_STR_3_1, // Default is 4
    H265_LEVEL_STR_4,
    H265_LEVEL_STR_4_1,
    H265_LEVEL_STR_5,
    H265_LEVEL_STR_5_1,
    H265_LEVEL_STR_5_2,
    H265_LEVEL_STR_6,
    H265_LEVEL_STR_6_1,
    H265_LEVEL_STR_6_2,
  };
  AddOption(option = new OpalMediaOptionEnum(LevelName, false, levels, PARRAYSIZE(levels),
                                              OpalMediaOption::MinMerge, 4));
#if OPAL_SDP
  option = new OpalMediaOptionUnsigned(TierFMTPName, true, OpalMediaOption::MinMerge, DefaultTierInt, 0, 1);
  option->SetFMTP(TierFMTPName, STRINGIZE(DefaultTierInt));
  AddOption(option);

  option = new OpalMediaOptionUnsigned(LevelFMTPName, true, OpalMediaOption::MinMerge, DefaultLevelInt, 0, 255);
  option->SetFMTP(LevelFMTPName, STRINGIZE(DefaultLevelInt));
  AddOption(option);
#endif //OPAL_SDP
}


PObject * OpalH265FormatInternal::Clone() const
{
  return new OpalH265FormatInternal(*this);
}


bool OpalH265FormatInternal::ToNormalisedOptions()
{
  return AdjustByOptionMaps(PTRACE_PARAM("ToNormalised", ) MyToNormalised) && OpalVideoFormatInternal::ToNormalisedOptions();
}

bool OpalH265FormatInternal::ToCustomisedOptions()
{
  return AdjustByOptionMaps(PTRACE_PARAM("ToCustomised", ) MyToCustomised) && OpalVideoFormatInternal::ToCustomisedOptions();
}


const OpalVideoFormat & GetOpalH265()
{
  static OpalMediaFormatStatic<OpalVideoFormat> const format(new OpalH265FormatInternal(H265_FormatName));
  return format;
}


struct OpalKeyFrameDetectorH265 : OpalVideoFormat::FrameDetector
{
  bool m_gotVPS;
  bool m_gotSPS;
  bool m_gotPPS;

  OpalKeyFrameDetectorH265()
    : m_gotVPS(false)
    , m_gotSPS(false)
    , m_gotPPS(false)
  { }

  virtual OpalVideoFormat::FrameType GetFrameType(const BYTE * rtp, PINDEX size)
  {
    if (size > 2) {
      switch (*rtp++ >> 1) {
        case 1: // Coded slice of a non-IDR picture
        case 2: // Coded slice data partition A
          if ((*rtp & 0x80) != 0) // High bit 1 indicates MB zero
            return OpalVideoFormat::e_InterFrame;
          break;

        case 19: // Coded slice of an IDR picture
        case 20:
          if (m_gotVPS && m_gotSPS && m_gotPPS)
            return OpalVideoFormat::e_IntraFrame;
          break;

        case 32: // Video parameter set
          m_gotVPS = true;
          break;

        case 33: // Sequence parameter set
          m_gotSPS = true;
          break;

        case 34: // Picture parameter set
          m_gotPPS = true;
          break;

        case 48: // Aggregation Packets
          while (size > 2) {
            PINDEX aggLen = *(PUInt16b *)rtp;
            rtp += 2;
            size -= 2;
            if (aggLen > size)
              break;

            OpalVideoFormat::FrameType type = GetFrameType(rtp, aggLen);
            if (type != OpalVideoFormat::e_NonFrameBoundary)
              return type;

            rtp += aggLen;
            size -= aggLen;
          }
      }
    }

    return OpalVideoFormat::e_NonFrameBoundary;
  }
};

PFACTORY_CREATE(OpalVideoFormat::FrameDetectFactory, OpalKeyFrameDetectorH265, H265EncodingName);


#endif // OPAL_VIDEO


// End of File ///////////////////////////////////////////////////////////////
