/*
 * main.cxx
 *
 * OPAL application source file for EXTREMELY simple Multipoint Conferencing Unit
 *
 * Copyright (c) 2008 Vox Lucida Pty. Ltd.
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

#include "precompile.h"
#include "main.h"


extern const char Manufacturer[] = "Vox Gratia";
extern const char Application[] = "OPAL Conference";
typedef OpalConsoleProcess<MyManager, Manufacturer, Application> MyApp;
PCREATE_PROCESS(MyApp);

/* Debug commands:
    -ttttodebugstream --sip udp$*:25060 --h323 tcp$*:21720 --moderator 123 --no-video
    -ttttodebugstream --sip udp$*:25060 --h323 tcp$*:21720 --moderator 123 --size hd720
    -ttttodebugstream --sip udp$*:25060 --h323 tcp$*:21720 --script-file test-cli.txt
 */


MyManager::MyManager()
  : m_confEP(NULL)
{
}

#if OPAL_HAS_MIXER && OPAL_IVR

PString MyManager::GetArgumentSpec() const
{
  PString spec = OpalManagerCLI::GetArgumentSpec();
  spec.Replace("V-version", "-version"); // Want the 'V' option back

  return "[Application options:]"
         "a-attendant: VXML script to run for incoming calls not directed\r"
                      "to a specific conference.\n"
         "m-moderator: PIN to allow to become a moderator and have talk\r"
                      "rights if absent, all participants are moderators.\n"
         "n-name:      Default name for ad-hoc conference.\n"
         "f-factory:   Name for factory URI name.\n"
#if OPAL_VIDEO
         "s-size:      Set default video size for ad-hoc conference.\n"
         "V-no-video.  Disable video for ad-hoc conference.\n"
#endif
         "-pass-thru.  Enable media pass through optimisation.\n"
         + spec;
}


bool MyManager::Initialise(PArgList & args, bool verbose, const PString &)
{
  if (!OpalManagerCLI::Initialise(args, verbose, "mcu:<du>"))
    return false;

#if OPAL_PCSS
  // Set up PCSS to do speaker playback
  new MyPCSSEndPoint(*this);
#endif

  // Set up IVR to do recording or WAV file play
  OpalIVREndPoint * ivr = new OpalIVREndPoint(*this);

  // Set up conference mixer
  m_confEP = new OpalConfEndPoint(*this);

  LockedStream lockedOutput(*this);
  ostream & output = lockedOutput;

  if (args.HasOption('a')) {
    PFilePath path = args.GetOptionString('a');
    ivr->SetDefaultVXML(path);
    AddRouteEntry(".*:.* = ivr:<da>");
    output << "Using attendant IVR " << path << endl;
  }

  // Add PCSS endpoint for monitoring and testing, we do not include it in
  // the console manager list of endpoints as it is not for the end user.
  OpalPCSSEndPoint * pcss = new OpalPCSSEndPoint(*this);
  pcss->SetDeferredAnswer(false);
  AddRouteEntry("mcu:.* = " OPAL_PREFIX_PCSS":<da>");

  MyConfNodeInfo info(args);

#if OPAL_VIDEO
  output << "Video mixer resolution: " << info.m_width << 'x' << info.m_height << endl;
#endif

  if (args.HasOption('n')) {
    info.m_name = args.GetOptionString('n');
    m_confEP->SetAdHocNodeInfo(info);
    output << "Ad-hoc conferences enabled with default name: " << info.m_name << endl;
  }

  if (args.HasOption('f')) {
    info.m_name = args.GetOptionString('f');
    m_confEP->SetFactoryNodeInfo(info);
    output << "Factory conferences enabled with name: " << info.m_name << endl;
  }


  m_cli->SetPrompt("MCU> ");

  m_cli->SetCommand("conf add", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfAdd),
                    "Add a new conference:",
#if OPAL_VIDEO
                    "[ -V ] [ -s size ] [ -m pin ] <name> [ <name> ... ]\n"
                    "  -V or --no-video       : Disable video\n"
                    "  -s or --size           : Set video size\n"
#else
                    "[ -m pin ] <name>\n"
#endif
                    "\n"
                    "  -m or --moderator pin  : PIN to allow to become a moderator and have talk rights\n"
                    "                         : if absent, all participants are moderators.\n"
                    "        --no-pass-thru   : Disable media pass through optimisation.\n"
                   );
  m_cli->SetCommand("conf list", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfList),
                    "List conferences");
  m_cli->SetCommand("conf remove", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfRemove),
                    "Remove conference:",
                    "{ <name> | <guid> }");
  m_cli->SetCommand("conf monitor", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfMonitor),
                    "Toggle monitoring a conference:",
                    "{ <conf-name> | <guid> }");
  m_cli->SetCommand("conf record", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfRecord),
                    "Record conference:",
                    "{ <conf-name> | <guid> } { <filename> | stop }");
  m_cli->SetCommand("conf play", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdConfPlay),
                    "Play file to conference:",
                    "{ <conf-name> | <guid> } <filename>");

  m_cli->SetCommand("member add", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdMemberAdd),
                    "Add a member to conference:",
                    "{ <name> | <guid> } <address>");
  m_cli->SetCommand("member list", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdMemberList),
                    "List members in conference:",
                    "{ <name> | <guid> }");
  m_cli->SetCommand("member remove", PCREATE_NOTIFIER_EXT(*this, MyManager, CmdMemberRemove),
                    "Remove conference member:",
                    "{ <conf-name> | <guid> } <member-name>\n"
                    "member remove <call-token>");

  return true;
}


void MyManager::OnEstablishedCall(OpalCall & call)
{
  PStringStream strm;
  strm << "Call from " << call.GetPartyA() << " entered conference at " << call.GetPartyB();
  m_cli->Broadcast(strm);
}


void MyManager::OnClearedCall(OpalCall & call)
{
  PStringStream strm;
  strm << "Call from " << call.GetPartyA() << " left conference at " << call.GetPartyB();
  m_cli->Broadcast(strm);
}


void MyManager::CmdConfAdd(PCLI::Arguments & args, P_INT_PTR)
{
  args.Parse("s-size:V-no-video.-m-moderator:");
  if (args.GetCount() == 0) {
    args.WriteUsage();
    return;
  }

  for (PINDEX i = 0; i < args.GetCount(); ++i) {
    if (m_confEP->FindNode(args[i]) != NULL) {
      args.WriteError() << "Conference name \"" << args[i] << "\" already exists." << endl;
      return;
    }
  }

  PSafePtr<OpalMixerNode> node = m_confEP->AddNode(MyConfNodeInfo(args, args[0]));

  if (node == NULL)
    args.WriteError() << "Could not create conference \"" << args[0] << '"' << endl;
  else {
    for (PINDEX i = 1; i < args.GetCount(); ++i) {
      if (!node->AddName(args[i]))
        args.GetContext() << "Could not add conference alias \"" << args[i] << '"' << endl;
    }
    args.GetContext() << "Added conference " << *node << endl;
  }
}


void MyManager::CmdConfList(PCLI::Arguments & args, P_INT_PTR)
{
  m_confEP->ConfList(args.GetContext());
}


bool MyManager::CmdConfXXX(PCLI::Arguments & args, PSafePtr<OpalMixerNode> & node, PINDEX argCount)
{
  if (args.GetCount() < argCount) {
    args.WriteUsage();
    return false;
  }

  node = m_confEP->FindNode(args[0]);
  if (node == NULL) {
    args.WriteError() << "Conference \"" << args[0] << "\" does not exist" << endl;
    return false;
  }

  return true;
}


void MyManager::CmdConfRemove(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 1))
    return;

  m_confEP->RemoveNode(*node);
  args.GetContext() << "Removed conference " << *node << endl;
}


void MyManager::CmdConfMonitor(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 1))
    return;

  if (m_monitorToken.IsEmpty()) {
    if (SetUpCall(PSTRSTRM("mcu:" << node->GetGUID() << ";Listen-Only"),
                  PSTRSTRM("pc:*|" P_NULL_AUDIO_DEVICE
                          ";OPAL-" OPAL_OPT_VIDEO_PREVIEW_DEVICE "=Null"
                          ";OPAL-" OPAL_OPT_CALLING_DISPLAY_NAME "=" << *node->GetNames().begin()),
                  m_monitorToken))
      args.GetContext() << "Started monitoring to conference " << *node << endl;
    else
      args.WriteError() << "Could not start";
  }
  else {
    ClearCall(m_monitorToken);
    m_monitorToken.MakeEmpty();
    args.GetContext() << "Stopped monitoring to conference " << *node << endl;
  }
}


void MyManager::CmdConfRecord(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  for (auto & connection : node->GetConnections()) {
    if (connection.GetCall().GetPartyB().NumCompare("ivr:") == EqualTo)
      connection.Release();
  }

  if (args[1] *= "stop")
    return;

  PFilePath path = args[1];
  if (PFile::Exists(path)) {
    if (!PFile::Access(path, PFile::WriteOnly)) {
      args.WriteError() << "Cannot overwrite file \"" << path << '"' << endl;
      return;
    }
  }
  else {
    PDirectory dir = path.GetDirectory();
    if (!PFile::Access(dir, PFile::WriteOnly)) {
      args.WriteError() << "Cannot write to directory \"" << dir << '"' << endl;
      return;
    }
  }

  PString token;
  if (SetUpCall("mcu:"+node->GetGUID().AsString()+";Listen-Only",
                "ivr:<vxml>"
                      "<form>"
                        "<record name=\"msg\" dtmfterm=\"false\" dest=\"" + PURL(path).AsString() + "\"/>"
                      "</form>"
                    "</vxml>",
                token))
    args.GetContext() << "Started";
  else
    args.WriteError() << "Could not start";
  args.GetContext() << " recording conference " << *node << " to \"" << path << '"' << endl;
}


void MyManager::CmdConfPlay(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  PFilePath path = args[1];
  if (!PFile::Access(path, PFile::ReadOnly)) {
    args.WriteError() << "Cannot read from file \"" << path << '"' << endl;
    return;
  }

  PString token;
  if (SetUpCall(PSTRSTRM("mcu:" << node->GetGUID() << ";Listen-Only=0"),
                PSTRSTRM("ivr:"
                        "<vxml>"
                          "<form>"
                            "<audio src=\"" << PURL(path) << "\"/>"
                          "</form>"
                        "</vxml>"),
                token))
    args.GetContext() << "Started";
  else
    args.WriteError() << "Could not start";
  args.GetContext() << " playing \"" << path << "\" to conference " << *node << endl;
}


void MyManager::CmdMemberAdd(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  if (m_confEP->MemberAdd(*node, args[1]))
    args.GetContext() << "Adding";
  else
    args.WriteError() << "Could not add";
  args.GetContext() << " new member \"" << args[1] << "\" to conference " << *node << endl;
}


void MyManager::CmdMemberList(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (CmdConfXXX(args, node, 1))
    m_confEP->MemberList(args.GetContext(), *node);
}


void MyManager::CmdMemberRemove(PCLI::Arguments & args, P_INT_PTR)
{
  if (args.GetCount() == 1) {
    if (ClearCall(args[0]))
      args.GetContext() << "Removed member";
    else
      args.WriteError() << "Member does not exist";
    args.GetContext() << " using token " << args[0] << endl;
    return;
  }

  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  PCaselessString name = args[1];
  for (auto & connection : node->GetConnections()) {
    if (name == connection.GetRemotePartyName()) {
      connection.ClearCall();
      args.GetContext() << "Removed member using name \"" << name << '"' << endl;
      return;
    }
  }

  args.WriteError() << "No member in conference " << *node << " with name \"" << name << '"' << endl;
}


///////////////////////////////////////////////////////////////

MyConfEndPoint::MyConfEndPoint(MyManager & manager)
  : OpalConfEndPoint(manager)
  , m_manager(manager)
{ }


OpalMixerNode * MyConfEndPoint::CreateNode(const OpalMixerNodeInfo & info)
{
  m_manager.Broadcast(PSTRSTRM("Created new conference \"" << info.m_name << '"'));
  return OpalMixerEndPoint::CreateNode(info);
}


void MyConfEndPoint::OnModeratorCommand(OpalConfConnection & connection, const PCaselessString & cmd)
{
  m_manager.Broadcast(PSTRSTRM("Connection " << connection.GetToken() << ' '
                               << connection.GetRemotePartyURL() << " \""
                               << connection.GetRemotePartyName() << "\""
                               " promoted to moderator.\n"));
}


///////////////////////////////////////////////////////////////

MyConfNodeInfo::MyConfNodeInfo(const PArgList & args, const char * name)
  : OpalConfNodeInfo(args.GetOptionString('m'), name)
{
  m_mediaPassThru = args.HasOption("pass-thru");
#if OPAL_VIDEO
  m_audioOnly = args.HasOption('V');
  if (args.HasOption('s'))
    PVideoFrameInfo::ParseSize(args.GetOptionString('s'), m_width, m_height);
#endif
}


#else

  #pragma message("Cannot compile MCU test program without OPAL_HAS_MIXER and OPAL_IVR set!")

#endif


// End of File ///////////////////////////////////////////////////////////////
