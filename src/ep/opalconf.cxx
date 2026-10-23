/*
 * opalconf.cxx
 *
 * OPAL Multipoint Conferencing Unit
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
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#include <ptlib.h>
#include <ep/opalconf.h>

#if OPAL_HAS_MIXER

///////////////////////////////////////////////////////////////

OpalConfNodeInfo::OpalConfNodeInfo(const PString & moderatorPIN, const char * name)
  : OpalMixerNodeInfo(name)
  , m_moderatorPIN(moderatorPIN)
{
  m_style = OpalVideoMixer::eUser;
  m_listenOnly = !m_moderatorPIN.IsEmpty();
}


///////////////////////////////////////////////////////////////

OpalConfEndPoint::OpalConfEndPoint(OpalManager & manager)
  : OpalMixerEndPoint(manager, "mcu")
{
}


OpalMixerConnection * OpalConfEndPoint::CreateConnection(PSafePtr<OpalMixerNode> node,
                                                        OpalCall & call,
                                                        void * userData,
                                                        unsigned options,
                                                        OpalConnection::StringOptions * stringOptions)
{
  return new OpalConfConnection(node, call, *this, userData, options, stringOptions);
}


void OpalConfEndPoint::ConfList(ostream & strm)
{
  for (const auto & node : GetNodes())
    strm << node << '\n';
}


PString OpalConfEndPoint::MemberList(OpalMixerNode & node)
{
  PStringStream strm;
  MemberList(strm, node);
  return strm;
}


void OpalConfEndPoint::MemberList(ostream & strm, OpalMixerNode & node)
{
  OpalConferenceState state;
  node.GetConferenceState(state);

  for (auto & it : state.m_users) {
    strm << it.m_callToken
      << it.m_uri << ' '
      << setfill(',') << it.m_roles << setfill(' ') << ' '
      << it.m_displayText.ToLiteral() << '\n';
  }
}


OpalConfConnection * OpalConfEndPoint::FindMember(OpalMixerNode & node, const PString & member)
{
  PURL const url(member);
  for (auto & conn : node.GetConnections()) {
    if (member == conn.GetToken() || member == conn.GetCall().GetToken() || url == PURL(conn.GetRemotePartyURL()))
      return dynamic_cast<OpalConfConnection *>(&conn);
  }
  return NULL;
}


bool OpalConfEndPoint::MemberAdd(OpalMixerNode & node, const PString & newMember)
{
  PString token;
  return GetManager().SetUpCall("mcu:"+node.GetGUID().AsString(), newMember, token);
}


bool OpalConfEndPoint::MemberRemove(OpalMixerNode & node, const PString & member)
{
  if (ClearCall(member))
    return true;

  auto connection = FindMember(node, member);
  if (!connection)
    return false;

  connection->ClearCall();
  return true;
}


///////////////////////////////////////////////////////////////

OpalConfConnection::OpalConfConnection(PSafePtr<OpalMixerNode> node,
                                       OpalCall & call,
                                       OpalConfEndPoint & ep,
                                       void * userData,
                                       unsigned options,
                                       OpalConnection::StringOptions * stringOptions)
  : OpalMixerConnection(node, call, ep, userData, options, stringOptions)
  , m_endpoint(ep)
  , m_participantType(ParticipantViewer)
{
  SetSendUserInputMode(SendUserInputAsString);
}


bool OpalConfConnection::SendUserInputString(const PString & value)
{
  PString pin = dynamic_cast<const OpalConfNodeInfo &>(m_node->GetNodeInfo()).m_moderatorPIN;
  if (value == pin)
    SetParticipantType(ParticipantModerator);
  else if (m_participantType == ParticipantModerator)
    m_endpoint.OnModeratorCommand(*this, value);
  return true;
}


PStringSet OpalConfConnection::GetRoles() const
{
  PStringSet roles;
  switch (m_participantType) {
    case ParticipantViewer:
      roles += "viewer";
      break;
    case ParticipantPresenter:
      roles += "presenter";
      break;
    case ParticipantModerator:
      roles += "moderator";
      break;
  }
  return roles;
}


void OpalConfConnection::SetParticipantType(ParticipantType type)
{
  if (m_participantType == type)
    return;

  SetListenOnly(type == ParticipantModerator);
  m_participantType = type;

  auto node = GetNode();
  if (PAssertNULL(node))
    m_endpoint.OnNodeStatusChanged(*node, OpalConferenceState::UserRoleChanged);
}


////////////////////////////////////////////////////////////////

void OpalConfEndPoint::OnModeratorCommand(OpalConfConnection & moderator, const PCaselessString & control)
{
  auto node = moderator.GetNode();
  if (!PAssertNULL(node))
    return;

  if (control == "list")
    moderator.OnUserInputString(MemberList(*node));
  else if (control.starts_with("remove "))
    MemberRemove(*node, control.Mid(7));
  else if (control.starts_with("promote ")) {
    auto conn = FindMember(*node, control.Mid(8));
    if (conn)
      conn->SetParticipantType(OpalConfConnection::ParticipantPresenter);
  }
  else if (control.starts_with("demote ")) {
    auto conn = FindMember(*node, control.Mid(8));
    if (conn)
      conn->SetParticipantType(OpalConfConnection::ParticipantViewer);
  }
  else if (control.starts_with("mute ")) {
    auto conn = FindMember(*node, control.Mid(8));
    if (conn)
      conn->SetAudioMute(false, true);
  }
}

#if OPAL_VIDEO

OpalVideoStreamMixer * OpalConfEndPoint::CreateVideoMixer(OpalMixerNode & node)
{
  return new OpalConfVideoMixer(node);
}


////////////////////////////////////////////////////////////////

class OpalConfVideoLayout : public OpalVideoLayoutStrategy
{
  public:
    typedef std::list<PSafePtr<OpalMixerConnection>> OrderedConnections;

    virtual bool GetMixPosition(OpalVideoMixer & mixer,
                                const OpalVideoMixer::Key_T & key,
                                bool firstStream,
                                OpalVideoMixer::MixPosition & pos) override;

  private:
    OrderedConnections m_order;
    std::map<PString, OpalVideoMixer::MixPosition> m_positions;
    OpalGridVideoLayout m_grid;
};


static std::unique_ptr<OpalVideoLayoutStrategy> ConferenceLayout(const OpalMixerNode & node)
{
  if (node.GetNodeInfo().m_style == OpalVideoMixer::eUser)
    return std::unique_ptr<OpalVideoLayoutStrategy>(new OpalConfVideoLayout);
  return OpalVideoMixer::CreateLayout(node.GetNodeInfo().m_style);
}


OpalConfVideoMixer::OpalConfVideoMixer(OpalMixerNode & node)
  : OpalVideoStreamMixer(node)
  , m_original(ConferenceLayout(node))
{
}


#if PTRACING
ostream & operator<<(ostream & strm, const OpalConfVideoLayout::OrderedConnections & connections)
{
  bool comma = false;
  for (auto connection = connections.begin(); connection != connections.end(); ++connection) {
    if (comma)
      strm << ", ";
    else
      comma = true;
    strm << (*connection)->GetCall().GetToken();
  }
  return strm;
}
#endif


bool OpalConfVideoLayout::GetMixPosition(OpalVideoMixer & mixer,
                                         const OpalVideoMixer::Key_T & key,
                                         bool firstStream,
                                         OpalVideoMixer::MixPosition & pos)
{
  OpalVideoStreamMixer * streamMixer = dynamic_cast<OpalVideoStreamMixer *>(&mixer);
  if (streamMixer == NULL)
    return m_grid.GetMixPosition(mixer, key, firstStream, pos);

  OpalMixerNode & node = streamMixer->GetMixerNode();

  if (firstStream) {
#if PTRACING
    bool changed = false;
#endif

    auto connections = node.GetConnections();
    if (connections.IsEmpty()) {
      m_order.clear();
      return false;
    }

    for (auto connection = connections.begin(); connection != connections.end(); ++connection) {
      if (std::find(m_order.begin(), m_order.end(), &*connection) == m_order.end()) {
        m_order.push_back(&*connection);
#if PTRACING
        changed = true;
#endif
      }
    }

    for (auto connection = m_order.begin(); connection != m_order.end(); ) {
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

    PString activeSpeaker = node.GetActiveSpeakerURL();
    if ((*m_order.begin())->GetRemotePartyURL() != activeSpeaker) {
      for (auto connection = m_order.begin(); connection != m_order.end(); ++connection) {
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

    m_positions.clear();
    OpalVideoMixer::MixPosition lastPosition;
    bool firstTile = true;
    for (OrderedConnections::iterator connection = m_order.begin(); connection != m_order.end(); ++connection) {
      OpalMediaStreamPtr videoStream = (*connection)->GetMediaStream(OpalMediaType::Video(), false);
      if (videoStream == NULL)
        continue;

      if (!m_grid.GetMixPosition(mixer, PString::Empty(), firstTile, lastPosition))
        break;
      firstTile = false;
      m_positions[videoStream->GetID()] = lastPosition;
    }
  }

  std::map<PString, OpalVideoMixer::MixPosition>::const_iterator it = m_positions.find(key);
  if (it == m_positions.end())
    return false;

  pos = it->second;
  return true;
}


OpalVideoMixerStrategy & OpalConfVideoMixer::GetVideoStrategy()
{
  // Caller holds m_mutex. Conference ordering stays in the layout; delivery
  // stays the original single mix unless a derived mixer publishes another strategy.
  if (m_videoStrategy != &m_original) {
    m_videoStrategy = &m_original;
    m_deliveryChanged = true;
  }
  return *m_videoStrategy;
}

#endif // OPAL_VIDEO

#else

  #pragma message("Cannot compile MCU test program without OPAL_HAS_MIXER and OPAL_IVR set!")

#endif


// End of File ///////////////////////////////////////////////////////////////
