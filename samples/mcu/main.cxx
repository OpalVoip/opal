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
  : m_mixer(NULL)
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
  m_mixer = new MyMixerEndPoint(*this);

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

  MyMixerNodeInfo info(args);

#if OPAL_VIDEO
  output << "Video mixer resolution: " << info.m_width << 'x' << info.m_height << endl;
#endif

  if (args.HasOption('n')) {
    info.m_name = args.GetOptionString('n');
    m_mixer->SetAdHocNodeInfo(info);
    output << "Ad-hoc conferences enabled with default name: " << info.m_name << endl;
  }

  if (args.HasOption('f')) {
    info.m_name = args.GetOptionString('f');
    m_mixer->SetFactoryNodeInfo(info);
    output << "Factory conferences enabled with name: " << info.m_name << endl;
  }


  m_cli->SetPrompt("MCU> ");

  m_cli->SetCommand("conf add", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfAdd),
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
  m_cli->SetCommand("conf list", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfList),
                    "List conferences");
  m_cli->SetCommand("conf remove", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfRemove),
                    "Remove conference:",
                    "{ <name> | <guid> }");
  m_cli->SetCommand("conf monitor", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfMonitor),
                    "Toggle monitoring a conference:",
                    "{ <conf-name> | <guid> }");
  m_cli->SetCommand("conf record", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfRecord),
                    "Record conference:",
                    "{ <conf-name> | <guid> } { <filename> | stop }");
  m_cli->SetCommand("conf play", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdConfPlay),
                    "Play file to conference:",
                    "{ <conf-name> | <guid> } <filename>");

  m_cli->SetCommand("member add", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdMemberAdd),
                    "Add a member to conference:",
                    "{ <name> | <guid> } <address>");
  m_cli->SetCommand("member list", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdMemberList),
                    "List members in conference:",
                    "{ <name> | <guid> }");
  m_cli->SetCommand("member remove", PCREATE_NOTIFIER_EXT(m_mixer, MyMixerEndPoint, CmdMemberRemove),
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


///////////////////////////////////////////////////////////////

MyMixerNodeInfo::MyMixerNodeInfo(const PArgList & args, const char * name)
  : OpalMixerNodeInfo(name)
  , m_moderatorPIN(args.GetOptionString('m'))
{
  m_style = OpalVideoMixer::eUser;
  m_listenOnly = !m_moderatorPIN.IsEmpty();
  m_mediaPassThru = args.HasOption("pass-thru");
#if OPAL_VIDEO
  m_audioOnly = args.HasOption('V');
  if (args.HasOption('s'))
    PVideoFrameInfo::ParseSize(args.GetOptionString('s'), m_width, m_height);
#endif
}


///////////////////////////////////////////////////////////////

MyMixerEndPoint::MyMixerEndPoint(MyManager & manager)
  : OpalMixerEndPoint(manager, "mcu")
  , m_manager(manager)
{
}


OpalMixerConnection * MyMixerEndPoint::CreateConnection(PSafePtr<OpalMixerNode> node,
                                                        OpalCall & call,
                                                        void * userData,
                                                        unsigned options,
                                                        OpalConnection::StringOptions * stringOptions)
{
  return new MyMixerConnection(node, call, *this, userData, options, stringOptions);
}


OpalMixerNode * MyMixerEndPoint::CreateNode(const OpalMixerNodeInfo & info)
{
  PStringStream strm;
  strm << "Created new conference \"" << info.m_name << '"';
  m_manager.Broadcast(strm);

  return OpalMixerEndPoint::CreateNode(info);
}


void MyMixerEndPoint::CmdConfAdd(PCLI::Arguments & args, P_INT_PTR)
{
  args.Parse("s-size:V-no-video.-m-moderator:");
  if (args.GetCount() == 0) {
    args.WriteUsage();
    return;
  }

  for (PINDEX i = 0; i < args.GetCount(); ++i) {
    if (FindNode(args[i]) != NULL) {
      args.WriteError() << "Conference name \"" << args[i] << "\" already exists." << endl;
      return;
    }
  }

  PSafePtr<OpalMixerNode> node = AddNode(MyMixerNodeInfo(args, args[0]));

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


void MyMixerEndPoint::CmdConfList(PCLI::Arguments & args, P_INT_PTR)
{
  ostream & out = args.GetContext();
  PSafeArray<OpalMixerNode> nodes = GetNodes();
  for (PSafeArray<OpalMixerNode>::iterator node = nodes.begin(); node != nodes.end(); ++node)
    out << *node << '\n';
  out.flush();
}


bool MyMixerEndPoint::CmdConfXXX(PCLI::Arguments & args, PSafePtr<OpalMixerNode> & node, PINDEX argCount)
{
  if (args.GetCount() < argCount) {
    args.WriteUsage();
    return false;
  }

  node = FindNode(args[0]);
  if (node == NULL) {
    args.WriteError() << "Conference \"" << args[0] << "\" does not exist" << endl;
    return false;
  }

  return true;
}


void MyMixerEndPoint::CmdConfRemove(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 1))
    return;

  RemoveNode(*node);
  args.GetContext() << "Removed conference " << *node << endl;
}


void MyMixerEndPoint::CmdConfMonitor(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 1))
    return;

  if (m_monitorToken.IsEmpty()) {
    if (GetManager().SetUpCall(PSTRSTRM("mcu:" << node->GetGUID() << ";Listen-Only"),
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


void MyMixerEndPoint::CmdConfRecord(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  PSafeArray<OpalConnection> connections = node->GetConnections();
  for (PSafeArray<OpalConnection>::iterator connection = connections.begin(); connection != connections.end(); ++connection) {
    if (connection->GetCall().GetPartyB().NumCompare("ivr:") == EqualTo)
      connection->Release();
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
  if (GetManager().SetUpCall("mcu:"+node->GetGUID().AsString()+";Listen-Only",
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


void MyMixerEndPoint::CmdConfPlay(PCLI::Arguments & args, P_INT_PTR)
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
  if (GetManager().SetUpCall(PSTRSTRM("mcu:" << node->GetGUID() << ";Listen-Only=0"),
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


void MyMixerEndPoint::CmdMemberAdd(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 2))
    return;

  PString token;
  if (GetManager().SetUpCall("mcu:"+node->GetGUID().AsString(), args[1], token))
    args.GetContext() << "Adding";
  else
    args.WriteError() << "Could not add";
  args.GetContext() << " new member \"" << args[1] << "\" to conference " << *node << endl;
}


void MyMixerEndPoint::CmdMemberList(PCLI::Arguments & args, P_INT_PTR)
{
  PSafePtr<OpalMixerNode> node;
  if (!CmdConfXXX(args, node, 1))
    return;

  ostream & out = args.GetContext();
  PSafeArray<OpalConnection> connections = node->GetConnections();
  for (PSafeArray<OpalConnection>::iterator connection = connections.begin(); connection != connections.end(); ++connection) {
    OpalCall & call = connection->GetCall();
    out << call.GetToken() << ' '
        << call.GetRemoteParty() << " \""
        << call.GetRemoteName() << '"' << '\n';
  }
  out.flush();
}


void MyMixerEndPoint::CmdMemberRemove(PCLI::Arguments & args, P_INT_PTR)
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
  PSafeArray<OpalConnection> connections = node->GetConnections();
  for (PSafeArray<OpalConnection>::iterator connection = connections.begin(); connection != connections.end(); ++connection) {
    if (name == connection->GetRemotePartyName()) {
      connection->ClearCall();
      args.GetContext() << "Removed member using name \"" << name << '"' << endl;
      return;
    }
  }

  args.WriteError() << "No member in conference " << *node << " with name \"" << name << '"' << endl;
}


///////////////////////////////////////////////////////////////

MyMixerConnection::MyMixerConnection(PSafePtr<OpalMixerNode> node,
                                     OpalCall & call,
                                     MyMixerEndPoint & ep,
                                     void * userData,
                                     unsigned options,
                                     OpalConnection::StringOptions * stringOptions)
  : OpalMixerConnection(node, call, ep, userData, options, stringOptions)
  , m_endpoint(ep)
{
}


bool MyMixerConnection::SendUserInputString(const PString & value)
{
  m_userInput += value;

  PString pin = dynamic_cast<const MyMixerNodeInfo &>(m_node->GetNodeInfo()).m_moderatorPIN;
  if (pin.NumCompare(m_userInput) != EqualTo)
    m_userInput.MakeEmpty();
  else if (pin == m_userInput) {
    m_userInput.MakeEmpty();
    SetListenOnly(false);

    PStringStream strm;
    strm << "Connection " << GetToken() << ' '
         << GetRemotePartyURL() << " \""
         << GetRemotePartyName() << "\""
            " promoted to moderator.\n";

    m_endpoint.m_manager.Broadcast(strm);
  }

  return OpalMixerConnection::SendUserInputString(value);
}

#if OPAL_VIDEO

OpalVideoStreamMixer * MyMixerEndPoint::CreateVideoMixer(OpalMixerNode & node)
{
  return new MyVideoMixer(node);
}


MyVideoMixer::MyVideoMixer(OpalMixerNode & node)
  : OpalVideoStreamMixer(node)
{
}


#if PTRACING
ostream & operator<<(ostream & strm, const MyVideoMixer::OrderedConnections & connections)
{
  bool comma = false;
  for (MyVideoMixer::OrderedConnections::const_iterator connection = connections.begin();
       connection != connections.end(); ++connection)
  {
    if (comma)
      strm << ", ";
    else
      comma = true;
    strm << (*connection)->GetCall().GetToken();
  }
  return strm;
}
#endif


bool MyVideoMixer::GetMixPosition(const PString & id, bool firstStream, MixPosition & pos)
{
  if (m_style != eUser)
    return OpalVideoStreamMixer::GetMixPosition(id, firstStream, pos);

  if (firstStream) {
    // Calculate all the positions on first stream. First we need to maintain
    // an order of connections based on last active speaker.

#if PTRACING
    bool changed = false;
#endif

    PSafeArray<OpalConnection> connections = m_node.GetConnections();
    if (connections.IsEmpty()) {
      m_order.clear();
      return false;
    }

    // Add new connections
    for (PSafeArray<OpalConnection>::iterator connection = connections.begin(); connection != connections.end(); ++connection) {
      if (std::find(m_order.begin(), m_order.end(), &*connection) == m_order.end()) {
        m_order.push_back(&*connection);
#if PTRACING
        changed = true;
#endif
      }
    }

    // Remove absent connections
    for (OrderedConnections::iterator connection = m_order.begin(); connection != m_order.end(); ) {
      if (connections.FindWithLock(**connection, PSafeReference) != NULL)
        ++connection;
      else {
        connection = m_order.erase(connection);
#if PTRACING
        changed = true;
#endif
      }
    }

    if (m_order.empty())
      return false;

    PString activeSpeaker = m_node.GetActiveSpeakerURL();
    if ((*m_order.begin())->GetRemotePartyURL() != activeSpeaker) {
      for (OrderedConnections::iterator connection = m_order.begin(); connection != m_order.end(); ++connection) {
        if ((*connection)->GetRemotePartyURL() == activeSpeaker) {
          m_order.push_front(*connection);
          m_order.erase(connection);
#if PTRACING
          changed = true;
#endif
          break;
        }
      }
    }

    PTRACE_IF(3, changed, "Order of connections changed: " << m_order);

    // Now calculate positions for each video stream based on the above order
    m_positions.clear();
    MixPosition lastPosition;
    for (OrderedConnections::iterator connection = m_order.begin(); connection != m_order.end(); ++connection) {
      OpalMediaStreamPtr videoStream = (*connection)->GetMediaStream(OpalMediaType::Video(), false);
      if (videoStream == NULL)
        continue;

      // We use the base class function to calculate the position in a simple NxN grid
      if (!OpalVideoMixer::GetMixPosition(PString::Empty(), connection == m_order.begin(), lastPosition))
        break;
      m_positions[videoStream->GetID()] = lastPosition;
    }
  }

  // Return the pre-calculated positions as the mixer asks for each stream to position
  MixPositions::const_iterator it = m_positions.find(id);
  if (it == m_positions.end())
    return false;

  pos = it->second;
  return true;
}

#endif // OPAL_VIDEO

#else

  #pragma message("Cannot compile MCU test program without OPAL_HAS_MIXER and OPAL_IVR set!")

#endif


// End of File ///////////////////////////////////////////////////////////////
