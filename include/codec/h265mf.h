/*
 * h265mf.h
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

#include <opal/mediafmt.h>

#if OPAL_VIDEO

class OpalH265FormatInternal : public OpalVideoFormatInternal
{
public:
  OpalH265FormatInternal(const char * name);
  virtual PObject * Clone() const;
  virtual bool ToNormalisedOptions();
  virtual bool ToCustomisedOptions();
};


#endif // OPAL_VIDEO


// End of File ///////////////////////////////////////////////////////////////
