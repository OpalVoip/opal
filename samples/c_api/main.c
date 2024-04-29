/*
 * main.c
 *
 * An example of the "C" interface to OPAL
 *
 * Open Phone Abstraction Library
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
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Vox Lucida (Robert Jongbloed)
 *
 * Contributor(s): ______________________________________.
 *
 */

#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <limits.h>
#include <opal.h>


#define LOCAL_MEDIA 0
#define CERTIFICATES 0
#define SIP_INTERFACES "udp$0.0.0.0:15060\ntcp$0.0.0.0:15060"
//#define MEDIA_MASK "@video"
#define MEDIA_MASK "!G.711*"
//#define STUN_SERVER "stun.l.google.com:19302"


#if defined(_WIN32)

  #include <windows.h>

  #ifdef _WIN64
    #ifdef _DEBUG
      #define OPAL_DLL "OPAL64d.DLL"
    #else
      #define OPAL_DLL "OPAL64.DLL"
    #endif
  #else
    #ifdef _DEBUG
      #define OPAL_DLL "OPALd.DLL"
    #else
      #define OPAL_DLL "OPAL.DLL"
    #endif
  #endif

  #define OPEN_LIBRARY(name)             LoadLibrary(name)
  #define GET_LIBRARY_FUNCTION(dll, fn)  GetProcAddress(dll, fn)
  #define GET_ERRNO GetLastError()

  HINSTANCE hDLL;

  #define snprintf _snprintf
  #define strncasecmp strnicmp

  #define TRACE_FILE "debugstream"

  #if _MSC_VER
    #pragma warning(disable:4100)
  #endif

#else // _WIN32

  #include <memory.h>
  #include <dlfcn.h>
  #include <errno.h>

  #define OPAL_DLL "libopal.so"

  #define OPEN_LIBRARY(name)             dlopen(name, RTLD_NOW|RTLD_GLOBAL)
  #define GET_LIBRARY_FUNCTION(dll, fn)  dlsym(dll, (const char *)(fn));
  #define GET_ERRNO errno

  void * hDLL;

  #define TRACE_FILE "trace.txt"

#endif // _WIN32

OpalInitialiseFunction  InitialiseFunction;
OpalShutDownFunction    ShutDownFunction;
OpalGetMessageFunction  GetMessageFunction;
OpalSendMessageFunction SendMessageFunction;
OpalFreeMessageFunction FreeMessageFunction;
OpalHandle              hOPAL;

char * CurrentCallToken;
char * HeldCallToken;
char * PlayScript;

#ifndef bool
typedef enum bool { false, true } bool;
#endif


OpalMessage * MySendCommand(OpalMessage * command, const char * errorMessage)
{
  OpalMessage * response;
  if ((response = SendMessageFunction(hOPAL, command)) == NULL)
    return NULL;
  if (response->m_type != OpalIndCommandError)
    return response;

  if (response->m_param.m_commandError == NULL || *response->m_param.m_commandError == '\0')
    printf("%s.\n", errorMessage);
  else
    printf("%s: %s\n", errorMessage, response->m_param.m_commandError);

  FreeMessageFunction(response);

  return NULL;
}


#if LOCAL_MEDIA

#ifdef _MSC_VER
#pragma warning(disable:4100)
#endif

int MyReadMediaData(const char * token, const char * id, const char * format, void * userData, void * data, int size)
{
  static FILE * file = NULL;
  if (file == NULL) {
    if (strcmp(format, "PCM-16") == 0)
      file = fopen("ogm.wav", "rb");
    printf("Reading %s media for stream %s on call %s\n", format, id, token);
  }

  if (file != NULL)
    return fread(data, 1, size, file);

  memset(data, 0, size);
  return size;
}


int MyWriteMediaData(const char * token, const char * id, const char * format, void * userData, void * data, int size)
{
  static FILE * file = NULL;
  if (file == NULL) {
    char name[100];
    sprintf(name, "Media-%s-%s.%s", token, id, format);
    file = fopen(name, "wb");
    if (file == NULL) {
      printf("Could not create media output file \"%s\"\n", name);
      return -1;
    }
    printf("Writing %s media for stream %s on call %s\n", format, id, token);
  }

  return fwrite(data, 1, size, file);
}

#ifdef _MSC_VER
#pragma warning(default:4100)
#endif

#endif // LOCAL_MEDIA


bool InitialiseOPAL()
{
  OpalMessage   command;
  OpalMessage * response;
  unsigned      version;

  static const char OPALOptions[] = OPAL_PREFIX_ALL " --trace-option +append+file --trace-level 4 --output " TRACE_FILE;


  if ((hDLL = OPEN_LIBRARY(OPAL_DLL)) == NULL) {
    fprintf(stderr, "Could not file %s, error=%u\n", OPAL_DLL, GET_ERRNO);
    return false;
  }

  InitialiseFunction  = (OpalInitialiseFunction )GET_LIBRARY_FUNCTION(hDLL, OPAL_INITIALISE_FUNCTION  );
  ShutDownFunction    = (OpalShutDownFunction   )GET_LIBRARY_FUNCTION(hDLL, OPAL_SHUTDOWN_FUNCTION    );
  GetMessageFunction  = (OpalGetMessageFunction )GET_LIBRARY_FUNCTION(hDLL, OPAL_GET_MESSAGE_FUNCTION );
  SendMessageFunction = (OpalSendMessageFunction)GET_LIBRARY_FUNCTION(hDLL, OPAL_SEND_MESSAGE_FUNCTION);
  FreeMessageFunction = (OpalFreeMessageFunction)GET_LIBRARY_FUNCTION(hDLL, OPAL_FREE_MESSAGE_FUNCTION);

  if (InitialiseFunction  == NULL ||
      ShutDownFunction    == NULL ||
      GetMessageFunction  == NULL ||
      SendMessageFunction == NULL ||
      FreeMessageFunction == NULL) {
    fputs("OPAL.DLL is invalid\n", stderr);
    return false;
  }


  ///////////////////////////////////////////////
  // Initialisation

  version = OPAL_C_API_VERSION;
  if ((hOPAL = InitialiseFunction(&version, OPALOptions)) == NULL) {
    fputs("Could not initialise OPAL\n", stderr);
    return false;
  }

  // General options
  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetGeneralParameters;
  //command.m_param.m_general.m_audioRecordDevice = "Camera Microphone (2- Logitech";
  command.m_param.m_general.m_autoRxMedia = command.m_param.m_general.m_autoTxMedia = "audio";
#ifdef STUN_SERVER
  command.m_param.m_general.m_natMethod = "STUN";
  command.m_param.m_general.m_natServer = STUN_SERVER;
#endif
#ifdef MEDIA_MASK
  command.m_param.m_general.m_mediaMask = MEDIA_MASK;
#endif

#if CERTIFICATES
  command.m_param.m_general.m_caFiles = "*";
  command.m_param.m_general.m_certificate = "C:\\data\\temp\\general_cert.pem";
  command.m_param.m_general.m_privateKey = "C:\\data\\temp\\general_key.pem";
  command.m_param.m_general.m_autoCreateCertificate = 1;
#endif

#if LOCAL_MEDIA
  command.m_param.m_general.m_mediaReadData = MyReadMediaData;
  command.m_param.m_general.m_mediaWriteData = MyWriteMediaData;
  command.m_param.m_general.m_mediaDataHeader = OpalMediaDataPayloadOnly;
#endif

  if ((response = MySendCommand(&command, "Could not set general options")) == NULL)
    return false;

  FreeMessageFunction(response);

  // Options across all protocols
  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetProtocolParameters;

  command.m_param.m_protocol.m_userName = "robertj";
  command.m_param.m_protocol.m_displayName = "Robert Jongbloed";
  command.m_param.m_protocol.m_interfaceAddresses = "0.0.0.0";
  //command.m_param.m_protocol.m_defaultOptions = "Http-Proxy=http://192.168.1.10";

  if ((response = MySendCommand(&command, "Could not set protocol options")) == NULL)
    return false;

  FreeMessageFunction(response);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetProtocolParameters;

  command.m_param.m_protocol.m_prefix = "sip";
#ifdef SIP_INTERFACES
  command.m_param.m_protocol.m_interfaceAddresses = SIP_INTERFACES;
#endif
  //command.m_param.m_protocol.m_defaultOptions = "PRACK-Mode=0\nInitial-Offer=false";
#if CERTIFICATES
  command.m_param.m_protocol.m_caFiles = "*";
  command.m_param.m_protocol.m_certificate = "C:\\data\\temp\\sip_cert.pem";
  command.m_param.m_protocol.m_privateKey = "C:\\data\\temp\\sip_key.pem";
  command.m_param.m_protocol.m_autoCreateCertificate = 1;
#endif

  if ((response = MySendCommand(&command, "Could not set SIP options")) == NULL)
    return false;

  FreeMessageFunction(response);

  return true;
}


static void HandleMessages(unsigned timeout)
{
  OpalMessage command;
  OpalMessage * response;
  OpalMessage * message;


  while ((message = GetMessageFunction(hOPAL, timeout*1000)) != NULL) {
    switch (message->m_type) {
      case OpalIndRegistration :
        switch (message->m_param.m_registrationStatus.m_status) {
          case OpalRegisterRetrying :
            printf("Trying registration to %s.\n", message->m_param.m_registrationStatus.m_serverName);
            break;
          case OpalRegisterRestored :
            printf("Registration of %s restored.\n", message->m_param.m_registrationStatus.m_serverName);
            break;
          case OpalRegisterSuccessful :
            printf("Registration of %s successful.\n", message->m_param.m_registrationStatus.m_serverName);
            break;
          case OpalRegisterRemoved :
            printf("Unregistered %s.\n", message->m_param.m_registrationStatus.m_serverName);
            break;
          case OpalRegisterFailed :
            if (message->m_param.m_registrationStatus.m_error == NULL ||
                message->m_param.m_registrationStatus.m_error[0] == '\0')
              printf("Registration of %s failed.\n", message->m_param.m_registrationStatus.m_serverName);
            else
              printf("Registration of %s error: %s\n",
                     message->m_param.m_registrationStatus.m_serverName,
                     message->m_param.m_registrationStatus.m_error);
        }
        break;

      case OpalIndLineAppearance :
        switch (message->m_param.m_lineAppearance.m_state) {
          case OpalLineIdle :
            printf("Line %s available.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineTrying :
            printf("Line %s in use.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineProceeding :
            printf("Line %s calling.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineRinging :
            printf("Line %s ringing.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineConnected :
            printf("Line %s connected.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineSubcribed :
            printf("Line %s subscription successful.\n", message->m_param.m_lineAppearance.m_line);
            break;
          case OpalLineUnsubcribed :
            printf("Unsubscribed line %s.\n", message->m_param.m_lineAppearance.m_line);
            break;
        }
        break;

      case OpalIndIncomingCall :
        if (CurrentCallToken != NULL && strcmp(CurrentCallToken, message->m_param.m_incomingCall.m_callToken) == 0)
          break;
        printf("Incoming call from \"%s\", \"%s\" to \"%s\", handled by \"%s\".\n",
               message->m_param.m_incomingCall.m_remoteDisplayName,
               message->m_param.m_incomingCall.m_remoteAddress,
               message->m_param.m_incomingCall.m_calledAddress,
               message->m_param.m_incomingCall.m_localAddress);
        if (CurrentCallToken == NULL || strncmp(message->m_param.m_incomingCall.m_calledAddress, "ivr:", 4) == 0) {
          memset(&command, 0, sizeof(command));
          command.m_type = OpalCmdAnswerCall;
          command.m_param.m_answerCall.m_callToken = message->m_param.m_incomingCall.m_callToken;
          command.m_param.m_answerCall.m_overrides.m_userName = "test123";
          command.m_param.m_answerCall.m_overrides.m_displayName = "Test Called Party";
          if ((response = MySendCommand(&command, "Could not answer call")) != NULL)
            FreeMessageFunction(response);
        }
        else {
          memset(&command, 0, sizeof(command));
          command.m_type = OpalCmdClearCall;
          command.m_param.m_clearCall.m_callToken = message->m_param.m_incomingCall.m_callToken;
          command.m_param.m_clearCall.m_reason = OpalCallEndedByLocalBusy;
          if ((response = MySendCommand(&command, "Could not refuse call")) != NULL)
            FreeMessageFunction(response);
        }
        break;

      case OpalIndProceeding :
        puts("Proceeding.\n");
        break;

      case OpalIndAlerting :
        puts("Ringing.\n");
        break;

      case OpalIndEstablished :
        puts("Established.\n");

        if (PlayScript != NULL) {
          printf("Playing %s\n", PlayScript);

          memset(&command, 0, sizeof(command));
          command.m_type = OpalCmdTransferCall;
          command.m_param.m_callSetUp.m_callToken = CurrentCallToken;
          command.m_param.m_callSetUp.m_partyA = "pc:";
          command.m_param.m_callSetUp.m_partyB = PlayScript;
          if ((response = MySendCommand(&command, "Could not start playing")) != NULL)
            FreeMessageFunction(response);
        }
        break;

      case OpalIndCompletedIVR :
        if (PlayScript != NULL) {
          printf("Played %s\n", PlayScript);

          memset(&command, 0, sizeof(command));
          command.m_type = OpalCmdTransferCall;
          command.m_param.m_callSetUp.m_callToken = CurrentCallToken;
          command.m_param.m_callSetUp.m_partyA = "ivr:";
          command.m_param.m_callSetUp.m_partyB = "pc:*";
          if ((response = MySendCommand(&command, "Could not finish playing")) != NULL)
            FreeMessageFunction(response);
        }
        break;

      case OpalIndMediaStream :
        printf("Media stream %s %s using %s.\n",
               message->m_param.m_mediaStream.m_type,
               message->m_param.m_mediaStream.m_state == OpalMediaStateOpen ? "opened" : "closed",
               message->m_param.m_mediaStream.m_format);
        break;

      case OpalIndUserInput :
        printf("User Input: %s.\n", message->m_param.m_userInput.m_userInput);
        break;

      case OpalIndCallCleared :
        if (message->m_param.m_callCleared.m_reason == NULL)
          puts("Call cleared.\n");
        else
          printf("Call cleared: %s\n", message->m_param.m_callCleared.m_reason);
        break;

      case OpalIndPresenceChange :
        printf("Presence change:\n"
               "state        = %d\n"
               "entity       = %s\n"
               "target       = %s\n"
               "service      = \"%s\"\n"
               "contact      = \"%s\"",
               message->m_param.m_presenceStatus.m_state,
               message->m_param.m_presenceStatus.m_entity,
               message->m_param.m_presenceStatus.m_target,
               message->m_param.m_presenceStatus.m_service,
               message->m_param.m_presenceStatus.m_contact);
        if (message->m_param.m_presenceStatus.m_capabilities != NULL && *message->m_param.m_presenceStatus.m_capabilities != '\0')
          printf(strchr(message->m_param.m_presenceStatus.m_capabilities, '\n') != NULL
                      ? "%s:\n%s" : "%s = \"%s\"", "\ncapabilities", message->m_param.m_presenceStatus.m_capabilities);
        if (message->m_param.m_presenceStatus.m_activities != NULL && *message->m_param.m_presenceStatus.m_activities != '\0')
          printf(strchr(message->m_param.m_presenceStatus.m_activities, '\n') != NULL
                      ? "%s:\n%s" : "%s   = \"%s\"", "\nactivities", message->m_param.m_presenceStatus.m_activities);
        if (message->m_param.m_presenceStatus.m_note != NULL && *message->m_param.m_presenceStatus.m_note != '\0')
          printf(strchr(message->m_param.m_presenceStatus.m_note, '\n') != NULL
                      ? "%s:\n%s" : "%s         = \"%s\"", "\nnote", message->m_param.m_presenceStatus.m_note);
        puts("\n\n");
        break;

      case OpalIndReceiveIM :
        printf("Received Instant Message: from=%s to=%s id=%s\n%s\n",
               message->m_param.m_instantMessage.m_from,
               message->m_param.m_instantMessage.m_to,
               message->m_param.m_instantMessage.m_conversationId,
               message->m_param.m_instantMessage.m_textBody);
        break;

      case OpalIndSentIM :
        printf("Sent Instant Message: from=%s to=%s id=%s msg=%u disposition=%s\n",
               message->m_param.m_instantMessage.m_from,
               message->m_param.m_instantMessage.m_to,
               message->m_param.m_instantMessage.m_conversationId,
               message->m_param.m_instantMessage.m_messageId,
               message->m_param.m_instantMessage.m_textBody);
        break;

      default :
        break;
    }

    FreeMessageFunction(message);
  }
}


bool DoCall(const char * from, const char * to)
{
  // Example cmd line: call 612@ekiga.net
  OpalMessage command;
  OpalMessage * response;


  printf("Calling %s\n", to);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetUpCall;
  command.m_param.m_callSetUp.m_partyA = from;
  command.m_param.m_callSetUp.m_partyB = to;
  command.m_param.m_callSetUp.m_overrides.m_displayName = "Test Calling Party";
  if ((response = MySendCommand(&command, "Could not make call")) == NULL)
    return false;

  CurrentCallToken = strdup(response->m_param.m_callSetUp.m_callToken);
  FreeMessageFunction(response);
  return true;
}


bool DoMute(int on)
{
  // Example cmd line: mute 612@ekiga.net
  OpalMessage command;
  OpalMessage * response;


  printf("Mute %s\n", on ? "on" : "off");

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdMediaStream;
  command.m_param.m_mediaStream.m_callToken = CurrentCallToken;
  command.m_param.m_mediaStream.m_type = "audio out";
  command.m_param.m_mediaStream.m_state = on ? OpalMediaStatePause : OpalMediaStateResume;
  if ((response = MySendCommand(&command, "Could not mute call")) == NULL)
    return false;

  FreeMessageFunction(response);
  return true;
}


bool DoHold()
{
  // Example cmd line: hold 612@ekiga.net
  OpalMessage command;
  OpalMessage * response;


  printf("Hold\n");

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdHoldCall;
  command.m_param.m_callToken = CurrentCallToken;
  if ((response = MySendCommand(&command, "Could not hold call")) == NULL)
    return false;

  HeldCallToken = CurrentCallToken;
  CurrentCallToken = NULL;

  FreeMessageFunction(response);
  return true;
}


bool DoTransfer(const char * to)
{
  // Example cmd line: transfer fred@10.0.1.11 noris@10.0.1.15
  OpalMessage command;
  OpalMessage * response;


  printf("Transferring to %s\n", to);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdTransferCall;
  command.m_param.m_callSetUp.m_partyB = to;
  command.m_param.m_callSetUp.m_callToken = CurrentCallToken;
  if ((response = MySendCommand(&command, "Could not transfer call")) == NULL)
    return false;

  FreeMessageFunction(response);
  return true;
}


bool OpRegister(int argc, const char * const * argv)
{
  // Example cmd line: register robertj@ekiga.net secret
  OpalMessage command;
  OpalMessage * response;
  char * colon;
  const char * aor = argv[2];
  const char * pwd = argv[3];


  printf("Registering %s\n", aor);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdRegistration;

  if ((colon = strchr(aor, ':')) == NULL) {
    command.m_param.m_registrationInfo.m_protocol = "h323";
    command.m_param.m_registrationInfo.m_identifier = aor;
  }
  else {
    *colon = '\0';
    command.m_param.m_registrationInfo.m_protocol = aor;
    command.m_param.m_registrationInfo.m_identifier = colon+1;
  }

  command.m_param.m_registrationInfo.m_password = pwd;
  command.m_param.m_registrationInfo.m_timeToLive = 300;
  if ((response = MySendCommand(&command, "Could not register endpoint")) == NULL)
    return false;

  FreeMessageFunction(response);
  HandleMessages(15);
  return true;
}


bool OpSubscribe(int argc, const char * const * argv)
{
  // Example cmd line: subscribe "dialog;sla;ma" 1501@192.168.1.32 1502@192.168.1.32
  OpalMessage command;
  OpalMessage * response;
  const char * package = argv[2];
  const char * aor = argv[3];
  const char * from = argv[4];


  printf("Susbcribing %s\n", aor);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdRegistration;
  command.m_param.m_registrationInfo.m_protocol = "sip";
  command.m_param.m_registrationInfo.m_identifier = aor;
  command.m_param.m_registrationInfo.m_hostName = from;
  command.m_param.m_registrationInfo.m_eventPackage = package;
  command.m_param.m_registrationInfo.m_timeToLive = 300;
  if ((response = MySendCommand(&command, "Could not subscribe")) == NULL)
    return false;

  FreeMessageFunction(response);
  HandleMessages(60);
  return true;
}


bool OpRecord(int argc, const char * const * argv)
{
  // Example cmd line: call 612@ekiga.net
  OpalMessage command;
  OpalMessage * response;
  const char * to = argv[2];
  const char * file = argv[3];


  printf("Calling %s\n", to);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetUpCall;
  command.m_param.m_callSetUp.m_partyB = to;
  if ((response = MySendCommand(&command, "Could not make call")) == NULL)
    return false;

  CurrentCallToken = strdup(response->m_param.m_callSetUp.m_callToken);
  FreeMessageFunction(response);

  printf("Recording %s\n", file);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdStartRecording;
  command.m_param.m_recording.m_callToken = CurrentCallToken;
  command.m_param.m_recording.m_file = file;
  command.m_param.m_recording.m_channels = 2;
  if ((response = MySendCommand(&command, "Could not start recording")) == NULL)
    return false;

  HandleMessages(120);
  return true;
}


bool OpPlay(int argc, const char * const * argv)
{
  // Example cmd line: call 612@ekiga.net
  OpalMessage command;
  OpalMessage * response;
  const char * to = argv[2];
  const char * file = argv[3];


  printf("Playing %s to %s\n", file, to);

  PlayScript = (char *)malloc(1000); // Yes, this leaks, don't care!

  if (strstr(file, ".vxml") != NULL)
    snprintf(PlayScript, 999, "ivr:%s", file);
  else
    snprintf(PlayScript, 999,
             "ivr:<?xml version=\"1.0\"?>"
             "<vxml version=\"1.0\">"
               "<form id=\"PlayFile\">"
                 "<audio src=\"%s\"/>"
               "</form>"
             "</vxml>", file);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetUpCall;
  if (strncmp(to, "pc:", 3) == 0) {
    command.m_param.m_callSetUp.m_partyA = PlayScript;
    PlayScript = NULL;
  }
  command.m_param.m_callSetUp.m_partyB = to;
  if ((response = MySendCommand(&command, "Could not make call")) == NULL)
    return false;

  CurrentCallToken = strdup(response->m_param.m_callSetUp.m_callToken);
  FreeMessageFunction(response);

  HandleMessages(120);
  return true;
}


bool DoPresence(const char * local, int argc, const char * const * argv)
{
  // Example cmd line: presence fred@flintstone.com wilma@flintstone.com
  OpalMessage command;
  OpalMessage * response;
  char * attributes = NULL;
  size_t attrLen = 0;
  int arg;

  printf("Registering presentity %s\n", local);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdRegistration;
  command.m_param.m_registrationInfo.m_protocol = "pres";
  command.m_param.m_registrationInfo.m_identifier = local;
  command.m_param.m_registrationInfo.m_timeToLive = 300;

  for (arg = 0; arg < argc; ++arg) {
    if (strncasecmp(argv[arg], "pwd=", 4) == 0)
      command.m_param.m_registrationInfo.m_password = argv[arg] + 4;
    else if (strncasecmp(argv[arg], "host=", 5) == 0)
      command.m_param.m_registrationInfo.m_hostName = argv[arg] + 5;
    else {
      size_t equal = strspn(argv[arg], "ABCDEFGHIJKLMNOPQRSTOVWXYZabcdefghijklmnopqrstovwxyz0123456789-");
      if (argv[arg][equal] != '=')
        break;
      {
        size_t argLen = strlen(argv[arg]);
        char * newAttributes = (char *)realloc(attributes, attrLen+argLen+2);
        if (newAttributes == NULL)
          abort();
        attributes = newAttributes;
        strcpy(attributes+attrLen, argv[arg]);
        strcpy(attributes+attrLen+argLen, "\n");
        attrLen += argLen + 1;
      }
    }
  }
  command.m_param.m_registrationInfo.m_attributes = attributes;

  response = MySendCommand(&command, "Could not register presentity");

  free(attributes);

  if (response == NULL)
    return false;

  FreeMessageFunction(response);

  for (; arg < argc; ++arg) {
    printf("Subscribing to presentity %s\n", argv[arg]);
    memset(&command, 0, sizeof(command));
    command.m_type = OpalCmdSubscribePresence;
    command.m_param.m_presenceStatus.m_entity = local;
    command.m_param.m_presenceStatus.m_target = argv[arg];
    command.m_param.m_presenceStatus.m_state = OpalPresenceAuthRequest;

    if ((response = MySendCommand(&command, "Could not subscribe to presentity")) == NULL)
      return false;

    FreeMessageFunction(response);
  }

  return true;
}


bool UndoPresence(const char * local, int argc, const char * const * argv)
{
  OpalMessage command;
  OpalMessage * response;
  int arg;

  for (arg = 0; arg < argc; ++arg) {
    size_t equal = strspn(argv[arg], "ABCDEFGHIJKLMNOPQRSTOVWXYZabcdefghijklmnopqrstovwxyz0123456789-");
    if (argv[arg][equal] != '=')
      break;
  }

  for (; arg < argc; ++arg) {
    printf("Subscribing to presentity %s\n", argv[arg]);
    memset(&command, 0, sizeof(command));
    command.m_type = OpalCmdSubscribePresence;
    command.m_param.m_presenceStatus.m_entity = local;
    command.m_param.m_presenceStatus.m_target = argv[arg];
    command.m_param.m_presenceStatus.m_state = OpalPresenceNone;

    if ((response = MySendCommand(&command, "Could not unsubscribe to presentity")) == NULL)
      return false;

    FreeMessageFunction(response);
  }

  return true;
}


bool DoPresenceChange(const char * local, OpalPresenceStates state)
{
  OpalMessage command;
  OpalMessage * response;

  printf("Changing status for presentity %s\n", local);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSetLocalPresence;
  command.m_param.m_presenceStatus.m_entity = local;
  command.m_param.m_presenceStatus.m_state = state;
  command.m_param.m_presenceStatus.m_capabilities = "audio\nvideo\ntext\ntype=text/plain";
  command.m_param.m_presenceStatus.m_activities = state == OpalPresenceUnavailable ? "busy\ntravel" : "available";
  command.m_param.m_presenceStatus.m_note = state == OpalPresenceUnavailable ? "Leave me alone" : "Talk to me";

  if ((response = MySendCommand(&command, "Could not change status of presentity")) == NULL)
    return false;

  FreeMessageFunction(response);
  return true;
}


bool OpPresence(int argc, const char * const * argv)
{
  if (!DoPresence(argv[2], argc-3, argv+3))
    return false;
  HandleMessages(5);
  if (!DoPresenceChange(argv[2], OpalPresenceUnavailable))
    return false;
  HandleMessages(5);
  if (!DoPresenceChange(argv[2], OpalPresenceAvailable))
    return false;
  HandleMessages(5);
  if (!UndoPresence(argv[2], argc-3, argv+3))
    return false;
  HandleMessages(60);
  return true;
}


bool OpInstantMsg(int argc, const char * const * argv)
{
  OpalMessage command;
  OpalMessage * response;

  const char * from = argv[2];
  const char * to = argv[3];
  argc -= 4;
  argv += 4;

  printf("Sending message from %s to %s\n", from, to);

  memset(&command, 0, sizeof(command));
  command.m_type = OpalCmdSendIM;
  command.m_param.m_instantMessage.m_from = from;
  command.m_param.m_instantMessage.m_to = to;

  if (argc > 0) {
    if (strncasecmp(argv[0], "host=", 5) != 0)
      command.m_param.m_instantMessage.m_textBody = argv[0];
    else {
      command.m_param.m_instantMessage.m_host = &argv[0][5];
      if (argc == 1)
        return false;
      command.m_param.m_instantMessage.m_textBody = argv[1];
    }
  }

  if ((response = MySendCommand(&command, "Could not change status of presentity")) == NULL)
    return false;

  printf("Sending message number %u, with conversation id is %s\n",
         response->m_param.m_instantMessage.m_messageId,
         response->m_param.m_instantMessage.m_conversationId);
  FreeMessageFunction(response);

  HandleMessages(30);
  return true;
}


bool OpShutdown(int argc, const char * const * argv)
{
  HandleMessages(5);

  puts("Shutting down.\n");
  // Test shut down and re-initialisation
  ShutDownFunction(hOPAL);

  puts("Reinitialising.\n");
  if (!InitialiseOPAL()) {
    fputs("Could not re-initialise OPAL\n", stderr);
    return false;
  }

  if (argc > 3) {
    if (!DoCall(argv[2], argv[3]))
      return false;
  }
  else if (argc > 2) {
    if (!DoCall(NULL, argv[2]))
      return false;
  }

  HandleMessages(15);
  return true;
}


bool OpListen(int argc, const char * const * argv)
{
  puts("Listening.\n");
  HandleMessages(120);
  return true;
}


bool OpCall(int argc, const char * const * argv)
{
  if (argc > 3) {
    if (!DoCall(argv[2], argv[3]))
      return false;
  } else {
    if (!DoCall(NULL, argv[2]))
      return false;
  }

  HandleMessages(15);
  return true;
}


bool OpMute(int argc, const char * const * argv)
{
  if (!DoCall(NULL, argv[2]))
    return false;
  HandleMessages(15);
  if (!DoMute(1))
    return false;
  HandleMessages(15);
  if (!DoMute(0))
    return false;
  HandleMessages(15);
  return true;
}


bool OpHold(int argc, const char * const * argv)
{
  if (!DoCall(NULL, argv[2]))
    return false;
  HandleMessages(15);
  if (!DoHold())
    return false;
  HandleMessages(15);
  return true;
}


bool OpTransfer(int argc, const char * const * argv)
{
  if (!DoCall(NULL, argv[2]))
    return false;
  HandleMessages(15);
  if (!DoTransfer(argv[3]))
    return false;
  HandleMessages(15);
  return true;
}


bool OpConsult(int argc, const char * const * argv)
{
  if (!DoCall(NULL, argv[2]))
    return false;
  HandleMessages(15);
  if (!DoHold())
    return false;
  HandleMessages(15);
  if (!DoCall(NULL, argv[3]))
    return false;
  HandleMessages(15);
  if (!DoTransfer(HeldCallToken))
    return false;
  HandleMessages(15);
  return true;
}


typedef bool (*Operation)(int argc, const char * const * argv);

static struct
{
  const char * m_name;
  int          m_minArgs;
  Operation    m_func;
  const char * m_help;
} Operations []  = {
  { "shutdown",  2, OpShutdown,  "" },
  { "listen",    2, OpListen,    "" },
  { "call",      3, OpCall,      "<destination-URL> [ <local-URL> ]" },
  { "mute",      3, OpMute,      "<destination-URL>" },
  { "hold",      3, OpHold,      "<destination-URL>" },
  { "transfer",  4, OpTransfer,  "<destination-URL> <transfer-URL>" },
  { "consult",   4, OpConsult,   "<destination-URL> <transfer-URL>" },
  { "register",  3, OpRegister,  "<address-of-record> [ <pwd> ]" },
  { "subscribe", 3, OpSubscribe, "<package> <address-of-record> [ <pwd> ]" },
  { "record",    3, OpRecord,    "<destination-URL> <filename>" },
  { "play",      3, OpPlay,      "<destination-URL> <filename>" },
  { "presence",  4, OpPresence,  "<local-URL> [ <attr>=<value> ... ] <remote-URL> ...\n"
                                 "    attrib one of pwd/host/transport/sub-protocol etc" },
  { "im",        5, OpInstantMsg,"<from> <to> [ host=<host> ] <msg>" }
};


static Operation GetOperation(int argc, const char * name)
{
  for (size_t op = 0; op < sizeof(Operations)/sizeof(Operations[0]); op++) {
    if (strcmp(name, Operations[op].m_name) == 0)
      return argc < Operations[op].m_minArgs ? NULL : Operations[op].m_func;
  }

  return NULL;
}


int main(int argc, const char * const * argv)
{
  Operation operation;

  if (argc < 2 || (operation = GetOperation(argc, argv[1])) == NULL) {
    fputs("usage:\n", stderr);
    for (size_t op = 0; op < sizeof(Operations)/sizeof(Operations[0]); op++)
      fprintf(stderr, "  c_api %s %s\n", Operations[op].m_name, Operations[op].m_help);
    return true;
  }

  puts("Initialising.\n");

  if (!InitialiseOPAL())
    return true;

  if (operation(argc, argv))
    puts("Exiting.\n");
  else
    puts("Failed, exiting.\n");

  ShutDownFunction(hOPAL);
  return false;
}



// End of File ///////////////////////////////////////////////////////////////
