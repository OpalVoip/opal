/*
 * aacmf_inc.cxx
 *
 * AAC-LC media format constants shared by the plugin.
 *
 * Open Phone Abstraction Library
 *
 * Copyright (c) 2026 Vox Lucida
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
 */

#include <codec/opalplugin.h>
#include <codec/known.h>


///////////////////////////////////////////////////////////////////////////////

#define AAC_SAMPLE_RATE       48000
#define AAC_CHANNELS          2
#define AAC_FRAME_SAMPLES     1024   // AAC-LC access unit
#define AAC_BIT_RATE          128000
#define AAC_MAX_FRAME_BYTES   2048

// PCM-16S-48kHz is framed at 1 ms. 32 ms is an integer number of those
// frames, and the resulting PCM buffer (6144 bytes) holds one decoded
// AAC access unit (1024 stereo samples, 4096 bytes). The codec still
// encodes 1024-sample frames and buffers the difference.
#define AAC_OPAL_FRAME_US     32000
#define AAC_OPAL_SAMPLES      (AAC_SAMPLE_RATE * AAC_OPAL_FRAME_US / 1000000)

// AudioSpecificConfig for AAC-LC, 48 kHz, stereo: object type 2,
// frequency index 3, channel config 2.
#define AAC_ASC_BYTE0         0x11
#define AAC_ASC_BYTE1         0x90


// End of File ///////////////////////////////////////////////////////////////
