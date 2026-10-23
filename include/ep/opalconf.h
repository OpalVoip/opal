/*
 * opalconf.h
 *
 * OPAL Multipoint Conferencing Unit
 * 
 * This is a more fully featured conference endpoint than the simple
 * OpalMixerEndPoint. Introducing concepts such as moderators,
 * presenters and viewers.
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

#ifndef _OPAL_CONF_H
#define _OPAL_CONF_H

#include <ep/opalmixer.h>

class OpalConfEndPoint;

#if OPAL_HAS_MIXER

struct OpalConfNodeInfo : public OpalMixerNodeInfo
{
  OpalConfNodeInfo(const PString & moderatorPIN = PString::Empty(), const char * name = nullptr);
  virtual OpalMixerNodeInfo * Clone() const { return new OpalConfNodeInfo(*this); }

  PString m_moderatorPIN;
};


#if OPAL_VIDEO

class OpalConfVideoMixer : public OpalVideoStreamMixer
{
  PCLASSINFO(OpalConfVideoMixer, OpalVideoStreamMixer)
public:
  OpalConfVideoMixer(OpalMixerNode & node);
  ~OpalConfVideoMixer() { StopPushThread(); }

  virtual OpalVideoMixerStrategy & GetVideoStrategy() override;

private:
  OpalOriginalVideoStrategy m_original;
};

#endif // OPAL_VIDEO


class OpalConfConnection : public OpalMixerConnection
{
    PCLASSINFO(OpalConfConnection, OpalMixerConnection);
  public:
    OpalConfConnection(
      PSafePtr<OpalMixerNode> node,
      OpalCall & call,
      OpalConfEndPoint & endpoint,
      void * userData,
      unsigned options,
      OpalConnection::StringOptions * stringOptions
    );

    virtual bool SendUserInputString(const PString & value) override;
    virtual PStringSet GetRoles() const override;

    enum ParticipantType
    {
      ParticipantViewer,
      ParticipantPresenter,
      ParticipantModerator
    };

    ParticipantType GetParticipantType() const { return m_participantType; }
    void SetParticipantType(ParticipantType type);

  protected:
    OpalConfEndPoint & m_endpoint;
    ParticipantType    m_participantType;
};


class OpalConfEndPoint : public OpalMixerEndPoint
{
    PCLASSINFO(OpalConfEndPoint, OpalMixerEndPoint);
  public:
    OpalConfEndPoint(OpalManager & manager);

    virtual OpalMixerConnection * CreateConnection(
      PSafePtr<OpalMixerNode> node,
      OpalCall & call,
      void * userData,
      unsigned options,
      OpalConnection::StringOptions * stringOptions
    ) override;

    void ConfList(ostream & strm);
    PString MemberList(OpalMixerNode & node);
    void MemberList(ostream & strm, OpalMixerNode & node);
    OpalConfConnection * FindMember(OpalMixerNode & node, const PString & member);
    bool MemberAdd(OpalMixerNode & node, const PString & newMember);
    bool MemberRemove(OpalMixerNode & node, const PString & member);

    virtual void OnModeratorCommand(OpalConfConnection & connection, const PCaselessString & cmd);

#if OPAL_VIDEO
    virtual OpalVideoStreamMixer * CreateVideoMixer(OpalMixerNode & node) override;
#endif // OPAL_VIDEO

  friend bool OpalConfConnection::SendUserInputString(const PString & value);
};


#endif // OPAL_HAS_MIXER

#endif  // _OPAL_CONF_H


// End of File ///////////////////////////////////////////////////////////////
