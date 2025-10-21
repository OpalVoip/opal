/*
 * g711codec.cxx
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
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
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>

#ifdef P_USE_PRAGMA
#pragma implementation "g711codec.h"
#endif

#include <opal_config.h>

#include <codec/g711codec.h>

#define new PNEW

extern "C" {
  int ulaw2linear(int u_val);
  int linear2ulaw(int pcm_val);
  int alaw2linear(int u_val);
  int linear2alaw(int pcm_val);
};


static const PConstString PacketLossConcealment("PacketLossConcealment");

class OpalG711Format : public OpalAudioFormatInternal
{
public:
  OpalG711Format(const char * name, RTP_DataFrame::PayloadTypes pt, const char * encodingName)
    : OpalAudioFormatInternal(name, pt, encodingName, 8, 8, 240, 20, 256, 8000)
  {
#if OPAL_G711PLC 
    AddOption(new OpalMediaOptionBoolean(PacketLossConcealment, false, OpalMediaOption::AndMerge, true));
#endif
  }


  virtual PObject * Clone() const
  {
    return new OpalG711Format(*this);
  }
};


const OpalAudioFormat & GetOpalG711_ULAW_64K()
{
  static const OpalMediaFormatStatic<OpalAudioFormat> format(
    new OpalG711Format(OPAL_G711_ULAW_64K, RTP_DataFrame::PCMU, "PCMU"));
  return format;
}


const OpalAudioFormat & GetOpalG711_ALAW_64K()
{
  static const OpalMediaFormatStatic<OpalAudioFormat> format(
    new OpalG711Format(OPAL_G711_ALAW_64K, RTP_DataFrame::PCMA, "PCMA"));
  return format;
}


///////////////////////////////////////////////////////////////////////////////

Opal_G711_PCM::Opal_G711_PCM(const OpalMediaFormat & inputMediaFormat)
  : OpalStreamedTranscoder(inputMediaFormat, OpalPCM16, 8, 16)
{
#if OPAL_G711PLC 
  acceptEmptyPayload = inputMediaFormat.GetOptionBoolean(PacketLossConcealment);
  m_lastPayloadSize = 0;
#endif
}


#if OPAL_G711PLC 
PBoolean Opal_G711_PCM::Convert(const RTP_DataFrame & input, RTP_DataFrame & output)
{
  PTRACE(7, "G.711\tPLC in_psz=" << input.GetPayloadSize()
         << " sn=" << input.GetSequenceNumber() << ", ts=" << input.GetTimestamp());

  if (input.GetPayloadSize() == 0) {
    if (m_lastPayloadSize == 0)
      return true; // Nothing to interpolate yet

    output.SetPayloadSize(m_lastPayloadSize);
    m_plc.dofe((short*)output.GetPayloadPtr(), m_lastPayloadSize/sizeof(short));
    PTRACE(7, "G.711\tDOFE out_psz" << m_lastPayloadSize);
    return true;
  }

  if (!OpalStreamedTranscoder::Convert(input, output))
    return false;

  m_lastPayloadSize = output.GetPayloadSize();
  m_plc.addtohistory((short*)output.GetPayloadPtr(), m_lastPayloadSize/sizeof(short));
  PTRACE(7, "G.711\tPLC ADD out_psz=" << m_lastPayloadSize);

  return true;
}
#endif


///////////////////////////////////////////////////////////////////////////////

Opal_G711_uLaw_PCM::Opal_G711_uLaw_PCM()
  : Opal_G711_PCM(OpalG711_ULAW_64K)
{
  PTRACE(3, "Codec\tG711-uLaw-64k decoder created");
}


int Opal_G711_uLaw_PCM::ConvertOne(int sample) const
{
  return ulaw2linear(sample);
}


int Opal_G711_uLaw_PCM::ConvertSample(int sample)
{
  return ulaw2linear(sample);
}


///////////////////////////////////////////////////////////////////////////////

Opal_PCM_G711_uLaw::Opal_PCM_G711_uLaw()
  : OpalStreamedTranscoder(OpalPCM16, OpalG711_ULAW_64K, 16, 8)
{
  PTRACE(3, "Codec\tG711-uLaw-64k encoder created");
}


int Opal_PCM_G711_uLaw::ConvertOne(int sample) const
{
  return linear2ulaw(sample);
}


int Opal_PCM_G711_uLaw::ConvertSample(int sample)
{
  return linear2ulaw(sample);
}


///////////////////////////////////////////////////////////////////////////////

Opal_G711_ALaw_PCM::Opal_G711_ALaw_PCM()
  : Opal_G711_PCM(OpalG711_ALAW_64K)
{
  PTRACE(3, "Codec\tG711-ALaw-64k decoder created");
}


int Opal_G711_ALaw_PCM::ConvertOne(int sample) const
{
  return alaw2linear(sample);
}


int Opal_G711_ALaw_PCM::ConvertSample(int sample)
{
  return alaw2linear(sample);
}

///////////////////////////////////////////////////////////////////////////////

Opal_PCM_G711_ALaw::Opal_PCM_G711_ALaw()
  : OpalStreamedTranscoder(OpalPCM16, OpalG711_ALAW_64K, 16, 8)
{
  PTRACE(3, "Codec\tG711-ALaw-64k encoder created");
}


int Opal_PCM_G711_ALaw::ConvertOne(int sample) const
{
  return linear2alaw(sample);
}


int Opal_PCM_G711_ALaw::ConvertSample(int sample)
{
  return linear2alaw(sample);
}


/////////////////////////////////////////////////////////////////////////////
