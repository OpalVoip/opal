/*
 * h264mf.h
 *
 * H.264 Media Format descriptions
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

#include <opal/mediafmt.h>
#include <h323/h323caps.h>

#if OPAL_VIDEO

class OpalH264FormatInternal : public OpalVideoFormatInternal
{
  public:
    enum PacketizationMode {
      Aligned,
      NonInterleaved,
      Interleaved // Unused as yet
    };

    OpalH264FormatInternal(const char * formatName, PacketizationMode mode);
    virtual PObject * Clone() const;
    virtual bool ToNormalisedOptions();
    virtual bool ToCustomisedOptions();
};


typedef OpalMediaFormatStatic<OpalVideoFormat> OpalH2640Format;
#if OPAL_H323
extern const char H264_Identifer[];
typedef OpalMediaFormatStaticH323<OpalVideoFormat, H323GenericVideoCapabilityTemplate<H264_Identifer, GetOpalH264_MODE1> > OpalH2641Format;
#else
typedef OpalMediaFormatStatic<OpalVideoFormat> OpalH2641Format;
#endif



#endif // OPAL_VIDEO


// End of File ///////////////////////////////////////////////////////////////
