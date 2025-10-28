/*
 * main.h
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

#ifndef _OPAL_MCU_MAIN_H
#define _OPAL_MCU_MAIN_H


class MyManager;
class MyMixerEndPoint;

#if OPAL_HAS_MIXER && OPAL_IVR

struct MyMixerNodeInfo : public OpalMixerNodeInfo
{
  MyMixerNodeInfo(const PArgList & args, const char * name = NULL);
  virtual OpalMixerNodeInfo * Clone() const { return new MyMixerNodeInfo(*this); }

  PString m_moderatorPIN;
};


#if OPAL_VIDEO

class MyVideoMixer : public OpalVideoStreamMixer
{
  PCLASSINFO(MyVideoMixer, OpalVideoStreamMixer)
public:
  MyVideoMixer(OpalMixerNode & node);
  ~MyVideoMixer() { StopPushThread(); }

  virtual bool GetMixPosition(
    const PString & id,
    bool firstStream,
    MixPosition & pos
  ) override;

  typedef std::list<PSafePtr<OpalConnection>> OrderedConnections;

private:
  OrderedConnections m_order;
  typedef std::map<PString, MixPosition> MixPositions;
  MixPositions m_positions;
};

#endif // OPAL_VIDEO


class MyMixerConnection : public OpalMixerConnection
{
    PCLASSINFO(MyMixerConnection, OpalMixerConnection);
  public:
    MyMixerConnection(
      PSafePtr<OpalMixerNode> node,
      OpalCall & call,
      MyMixerEndPoint & endpoint,
      void * userData,
      unsigned options,
      OpalConnection::StringOptions * stringOptions
    );

    virtual bool SendUserInputString(const PString & value) override;

  protected:
    MyMixerEndPoint & m_endpoint;
    PString           m_userInput;
};


class MyMixerEndPoint : public OpalMixerEndPoint
{
    PCLASSINFO(MyMixerEndPoint, OpalMixerEndPoint);
  public:
    MyMixerEndPoint(MyManager & manager);

    virtual OpalMixerConnection * CreateConnection(
      PSafePtr<OpalMixerNode> node,
      OpalCall & call,
      void * userData,
      unsigned options,
      OpalConnection::StringOptions * stringOptions
    ) override;

    virtual OpalMixerNode * CreateNode(
      const OpalMixerNodeInfo & info ///< Initial info for node
    ) override;

    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfAdd);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfList);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfRemove);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfRecord);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfPlay);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdConfMonitor);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdMemberAdd);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdMemberList);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyMixerEndPoint, CmdMemberRemove);

    bool CmdConfXXX(PCLI::Arguments & args, PSafePtr<OpalMixerNode> & node, PINDEX argCount);

#if OPAL_VIDEO
    virtual OpalVideoStreamMixer * CreateVideoMixer(OpalMixerNode & node) override;
#endif // OPAL_VIDEO

  private:
    MyManager & m_manager;
    PString m_monitorToken;

  friend bool MyMixerConnection::SendUserInputString(const PString & value);
};


#if OPAL_PCSS

class MyPCSSEndPoint : public OpalPCSSEndPoint
{
    PCLASSINFO(MyPCSSEndPoint, OpalPCSSEndPoint);
  public:
    MyPCSSEndPoint(OpalManager & manager)
      : OpalPCSSEndPoint(manager)
    {
    }

    virtual PBoolean OnShowIncoming(const OpalPCSSConnection & connection)
    {
      return AcceptIncomingCall(connection.GetToken());
    }

    virtual PBoolean OnShowOutgoing(const OpalPCSSConnection &)
    {
      return true;
    }
};

#endif // OPAL_PCSS

#endif // OPAL_HAS_MIXER && OPAL_IVR


class MyManager : public OpalManagerCLI
{
    PCLASSINFO(MyManager, OpalManagerCLI)

  public:
    MyManager();

#if OPAL_HAS_MIXER && OPAL_IVR

    PString GetArgumentSpec() const override;
    virtual bool Initialise(PArgList & args, bool verbose, const PString & defaultRoute = PString::Empty()) override;

    virtual void OnEstablishedCall(OpalCall & call) override;
    virtual void OnClearedCall(OpalCall & call) override;

    void Broadcast(const PString & str) override { m_cli->Broadcast(str); }
#endif

    MyMixerEndPoint * m_mixer;
};


#endif  // _OPAL_MCU_MAIN_H


// End of File ///////////////////////////////////////////////////////////////
