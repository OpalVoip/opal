/*
 * sample_lid.cxx
 *
 * Example Line Interface Device plugin.
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * This plugin does not drive hardware. It shows the pieces OPAL loads:
 * one PluginLID_Definition, a context object, and the functions a device
 * implements. Copy this file and replace the in-memory line with the
 * driver for the hardware.
 *
 * A NULL pointer in the definition means that call is unimplemented.
 * OPAL treats those calls as PluginLID_UnimplementedFunction.
 *
 * The plugin reports one device named "sample".
 */

#include <lids/lidplugin.h>

#include <cstring>


static void CopyText(char * dest, unsigned size, const char * src)
{
  if (dest == NULL || size == 0)
    return;
  if (src == NULL)
    src = "";
  unsigned i = 0;
  while (i + 1 < size && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}


class Context
{
  public:
    PLUGIN_LID_CTOR()
      : m_open(false)
      , m_offHook(false)
      , m_audio(false)
      , m_frameSize(320)
    {
      CopyText(m_readFormat, sizeof(m_readFormat), "PCM-16");
      CopyText(m_writeFormat, sizeof(m_writeFormat), "PCM-16");
    }

    PLUGIN_LID_DTOR()
    {
      Close();
    }

    // Devices are listed by index until this returns PluginLID_NoMoreNames.
    PLUGIN_FUNCTION_ARG3(GetDeviceName, unsigned, index, char *, name, unsigned, size)
    {
      if (name == NULL || size == 0)
        return PluginLID_InvalidParameter;
      if (index > 0)
        return PluginLID_NoMoreNames;

      CopyText(name, size, "sample");
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG1(Open, const char *, device)
    {
      Close();
      if (device == NULL || std::strcmp(device, "sample") != 0)
        return PluginLID_NoSuchDevice;

      m_open = true;
      m_offHook = false;
      m_audio = false;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG0(Close)
    {
      m_open = false;
      m_offHook = false;
      m_audio = false;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG1(GetLineCount, unsigned *, count)
    {
      if (count == NULL)
        return PluginLID_InvalidParameter;
      *count = 1;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(IsLineTerminal, unsigned, line, PluginLID_Boolean *, isTerminal)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (isTerminal == NULL)
        return PluginLID_InvalidParameter;
      *isTerminal = TRUE;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG3(IsLinePresent, unsigned, line, PluginLID_Boolean, forceTest, PluginLID_Boolean *, present)
    {
      (void)forceTest;
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (present == NULL)
        return PluginLID_InvalidParameter;
      *present = TRUE;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(IsLineOffHook, unsigned, line, PluginLID_Boolean *, offHook)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (offHook == NULL)
        return PluginLID_InvalidParameter;
      *offHook = m_offHook ? TRUE : FALSE;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(SetLineOffHook, unsigned, line, PluginLID_Boolean, newState)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      m_offHook = newState != FALSE;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(IsLineRinging, unsigned, line, unsigned long *, cadence)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (cadence != NULL)
        *cadence = 0;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG3(GetSupportedFormat, unsigned, index, char *, mediaFormat, unsigned, size)
    {
      if (mediaFormat == NULL || size == 0)
        return PluginLID_InvalidParameter;
      if (index > 0)
        return PluginLID_NoMoreNames;

      CopyText(mediaFormat, size, "PCM-16");
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(SetReadFormat, unsigned, line, const char *, mediaFormat)
    {
      return SetFormat(line, m_readFormat, sizeof(m_readFormat), mediaFormat);
    }

    PLUGIN_FUNCTION_ARG2(SetWriteFormat, unsigned, line, const char *, mediaFormat)
    {
      return SetFormat(line, m_writeFormat, sizeof(m_writeFormat), mediaFormat);
    }

    PLUGIN_FUNCTION_ARG3(GetReadFormat, unsigned, line, char *, mediaFormat, unsigned, size)
    {
      return GetFormat(line, m_readFormat, mediaFormat, size);
    }

    PLUGIN_FUNCTION_ARG3(GetWriteFormat, unsigned, line, char *, mediaFormat, unsigned, size)
    {
      return GetFormat(line, m_writeFormat, mediaFormat, size);
    }

    PLUGIN_FUNCTION_ARG1(StopReading, unsigned, line)
    {
      return CheckLine(line);
    }

    PLUGIN_FUNCTION_ARG1(StopWriting, unsigned, line)
    {
      return CheckLine(line);
    }

    PLUGIN_FUNCTION_ARG2(SetReadFrameSize, unsigned, line, unsigned, frameSize)
    {
      return SetFrameSize(line, frameSize);
    }

    PLUGIN_FUNCTION_ARG2(SetWriteFrameSize, unsigned, line, unsigned, frameSize)
    {
      return SetFrameSize(line, frameSize);
    }

    PLUGIN_FUNCTION_ARG2(GetReadFrameSize, unsigned, line, unsigned *, frameSize)
    {
      return GetFrameSize(line, frameSize);
    }

    PLUGIN_FUNCTION_ARG2(GetWriteFrameSize, unsigned, line, unsigned *, frameSize)
    {
      return GetFrameSize(line, frameSize);
    }

    PLUGIN_FUNCTION_ARG3(ReadFrame, unsigned, line, void *, buffer, unsigned *, count)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (buffer == NULL || count == NULL)
        return PluginLID_InvalidParameter;
      if (!m_audio)
        return PluginLID_OperationNotAllowed;

      std::memset(buffer, 0, m_frameSize);
      *count = m_frameSize;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG4(WriteFrame, unsigned, line, const void *, buffer, unsigned, count, unsigned *, written)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (buffer == NULL || written == NULL)
        return PluginLID_InvalidParameter;
      if (!m_audio)
        return PluginLID_OperationNotAllowed;

      *written = count;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(EnableAudio, unsigned, line, PluginLID_Boolean, enable)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      m_audio = enable != FALSE;
      return PluginLID_NoError;
    }

    PLUGIN_FUNCTION_ARG2(IsAudioEnabled, unsigned, line, PluginLID_Boolean *, enable)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (enable == NULL)
        return PluginLID_InvalidParameter;
      *enable = m_audio ? TRUE : FALSE;
      return PluginLID_NoError;
    }

  private:
    PluginLID_Errors CheckLine(unsigned line) const
    {
      if (!m_open)
        return PluginLID_DeviceNotOpen;
      if (line != 0)
        return PluginLID_NoSuchLine;
      return PluginLID_NoError;
    }

    PluginLID_Errors SetFormat(unsigned line, char * dest, unsigned destSize, const char * mediaFormat)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (mediaFormat == NULL || std::strcmp(mediaFormat, "PCM-16") != 0)
        return PluginLID_UnsupportedMediaFormat;

      CopyText(dest, destSize, mediaFormat);
      return PluginLID_NoError;
    }

    PluginLID_Errors GetFormat(unsigned line, const char * format, char * mediaFormat, unsigned size) const
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (mediaFormat == NULL || size == 0)
        return PluginLID_InvalidParameter;

      CopyText(mediaFormat, size, format);
      return PluginLID_NoError;
    }

    PluginLID_Errors SetFrameSize(unsigned line, unsigned frameSize)
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (frameSize == 0)
        return PluginLID_InvalidParameter;

      m_frameSize = frameSize;
      return PluginLID_NoError;
    }

    PluginLID_Errors GetFrameSize(unsigned line, unsigned * frameSize) const
    {
      PluginLID_Errors error = CheckLine(line);
      if (error != PluginLID_NoError)
        return error;
      if (frameSize == NULL)
        return PluginLID_InvalidParameter;

      *frameSize = m_frameSize;
      return PluginLID_NoError;
    }

    bool     m_open;
    bool     m_offHook;
    bool     m_audio;
    unsigned m_frameSize;
    char     m_readFormat[32];
    char     m_writeFormat[32];
};


// The function order matches PluginLID_Definition in lids/lidplugin.h.
static struct PluginLID_Definition definition[] =
{
  {
    PLUGIN_LID_VERSION,
    0,

    "sample",
    "Example line interface device",
    "OPAL",
    "Sample",
    "1",
    "",
    "",

    "OPAL",
    "",
    "",
    "Copyright (C) 2026 OPAL",
    "MPL 1.0",
    "1.0",

    NULL,

    Context::Create,
    Context::Destroy,
    Context::GetDeviceName,
    Context::Open,
    Context::Close,
    Context::GetLineCount,
    Context::IsLineTerminal,
    Context::IsLinePresent,
    Context::IsLineOffHook,
    Context::SetLineOffHook,
    NULL, // HookFlash
    NULL, // HasHookFlash
    Context::IsLineRinging,
    NULL, // RingLine
    NULL, // IsLineDisconnected
    NULL, // SetLineToLineDirect
    NULL, // IsLineToLineDirect
    Context::GetSupportedFormat,
    Context::SetReadFormat,
    Context::SetWriteFormat,
    Context::GetReadFormat,
    Context::GetWriteFormat,
    Context::StopReading,
    Context::StopWriting,
    Context::SetReadFrameSize,
    Context::SetWriteFrameSize,
    Context::GetReadFrameSize,
    Context::GetWriteFrameSize,
    Context::ReadFrame,
    Context::WriteFrame,
    NULL, // GetAudioLevelDB
    Context::EnableAudio,
    Context::IsAudioEnabled,
    NULL, // SetRecordVolume
    NULL, // SetPlayVolume
    NULL, // GetRecordVolume
    NULL, // GetPlayVolume
    NULL, // GetAEC
    NULL, // SetAEC
    NULL, // GetVAD
    NULL, // SetVAD
    NULL, // GetCallerID
    NULL, // SetCallerID
    NULL, // SendVisualMessageWaitingIndicator
    NULL, // PlayDTMF
    NULL, // ReadDTMF
    NULL, // GetRemoveDTMF
    NULL, // SetRemoveDTMF
    NULL, // IsToneDetected
    NULL, // WaitForToneDetect
    NULL, // WaitForTone
    NULL, // SetToneParameters
    NULL, // PlayTone
    NULL, // IsTonePlaying
    NULL, // StopTone
    NULL, // DialOut
    NULL, // GetWinkDuration
    NULL, // SetWinkDuration
    NULL, // SetCountryCode
    NULL, // GetSupportedCountry
    NULL, // SetLineConnected
    NULL  // IsLineConnected
  }
};

PLUGIN_LID_IMPLEMENTATION(definition);
