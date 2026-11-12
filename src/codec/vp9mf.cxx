/*
 * vp9mf.cxx
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

#include <ptlib.h>

#include <opal_config.h>

#if OPAL_VIDEO

#include "vp9mf_inc.cxx"

#include <opal/mediafmt.h>
#include <h323/h323caps.h>


class OpalVP9Format : public OpalVideoFormatInternal
{
  public:
    OpalVP9Format()
      : OpalVideoFormatInternal(VP9FormatName, RTP_DataFrame::DynamicBase, VP9EncodingName,
                                PVideoFrameInfo::MaxWidth, PVideoFrameInfo::MaxHeight, 30, 16000000)
    {
      OpalMediaOption * option = new OpalMediaOptionUnsigned(MaxFrameRateName, true, OpalMediaOption::MinMerge, MAX_FR_SDP, MIN_FR_SDP, MAX_FR_SDP);
      OPAL_SET_MEDIA_OPTION_FMTP(option, MaxFrameRateSDP, STRINGIZE(MAX_FR_SDP));
      AddOption(option);

      option = new OpalMediaOptionUnsigned(MaxFrameSizeName, true, OpalMediaOption::MinMerge, MAX_FS_SDP, MIN_FS_SDP, MAX_FS_SDP);
      OPAL_SET_MEDIA_OPTION_FMTP(option, MaxFrameSizeSDP, STRINGIZE(MAX_FS_SDP));
      AddOption(option);

      option = new OpalMediaOptionUnsigned(ProfileIdName, true, OpalMediaOption::EqualMerge, SUPPORTED_PROFILE_ID, MIN_PROFILE_ID, MAX_PROFILE_ID);
      OPAL_SET_MEDIA_OPTION_FMTP(option, ProfileIdSDP, STRINGIZE(SUPPORTED_PROFILE_ID));
      AddOption(option);
    }

    virtual PObject * Clone() const
    {
      return new OpalVP9Format(*this);
    }


    virtual bool ToNormalisedOptions()
    {
      return AdjustByOptionMaps(PTRACE_PARAM("ToNormalised",) MyToNormalised) && OpalVideoFormatInternal::ToNormalisedOptions();
    }

    virtual bool ToCustomisedOptions()
    {
      return AdjustByOptionMaps(PTRACE_PARAM("ToCustomised",) MyToCustomised) && OpalVideoFormatInternal::ToCustomisedOptions();
    }
};


const OpalVideoFormat & GetOpalVP9()
{
  static OpalMediaFormatStatic<OpalVideoFormat> const format(new OpalVP9Format());
  return format;
}


// RFC 9628 payload descriptor. A frame is intra when B is set and P is clear.
struct OpalKeyFrameDetectorVP9 : OpalVideoFormat::FrameDetector
{
  virtual OpalVideoFormat::FrameType GetFrameType(const BYTE * rtp, PINDEX size)
  {
    if (size < 1)
      return OpalVideoFormat::e_NonFrameBoundary;

    if ((rtp[0] & 0x08) == 0) // B: start of frame
      return OpalVideoFormat::e_NonFrameBoundary;

    return (rtp[0] & 0x40) == 0 ? OpalVideoFormat::e_IntraFrame : OpalVideoFormat::e_InterFrame;
  }
};

PFACTORY_CREATE(OpalVideoFormat::FrameDetectFactory, OpalKeyFrameDetectorVP9, "VP9");


#endif // OPAL_VIDEO


// End of File ///////////////////////////////////////////////////////////////
