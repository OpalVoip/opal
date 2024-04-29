/*
 * t38proto.h
 *
 * T.38 protocol handler
 *
 * Open Phone Abstraction Library
 *
 * Copyright (c) 2001 Equivalence Pty. Ltd.
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
 * The Original Code is Open H323 Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 */

#ifndef OPAL_T38_SIPT38_H
#define OPAL_T38_SIPT38_H

#ifdef P_USE_PRAGMA
#pragma interface
#endif

#include <opal_config.h>

#if OPAL_SDP

#include <sdp/sdp.h>

#if OPAL_T38_CAPABILITY

/////////////////////////////////////////////////////////
//
//  SDP media description for fax media
//

class SDPFaxMediaDescription : public SDPMediaDescription
{
  PCLASSINFO(SDPFaxMediaDescription, SDPMediaDescription);
  public:
    SDPFaxMediaDescription(const OpalTransportAddress & address);
    virtual SDPMediaFormat * CreateSDPMediaFormat() override;
    virtual PString GetSDPPortList() const override;
    virtual void OutputAttributes(ostream & str) const override;
    virtual void SetAttribute(const PString & attr, const PString & value) override;
    virtual void ProcessMediaOptions(SDPMediaFormat & sdpFormat, const OpalMediaFormat & mediaFormat) override;
    virtual bool PostDecode(Direction defaultDirection, const OpalMediaFormatList & mediaFormats) override;

  protected:
    class Format : public SDPMediaFormat
    {
      public:
        Format(SDPFaxMediaDescription & parent) : SDPMediaFormat(parent) { }
        virtual bool FromSDP(const PString & portString);
    };

    PStringToString t38Attributes;
};

#endif // OPAL_T38_CAPABILITY

#endif // OPAL_SDP

#endif // OPAL_T38_SIPT38_H

