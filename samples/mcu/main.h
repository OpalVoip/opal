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

#include <ep/opalconf.h>


#if OPAL_HAS_MIXER

class MyManager;

struct MyConfNodeInfo : public OpalConfNodeInfo
{
  MyConfNodeInfo(const PArgList & args, const char * name = NULL);
  virtual OpalMixerNodeInfo * Clone() const { return new MyConfNodeInfo(*this); }
};

class MyConfEndPoint : public OpalConfEndPoint
{
  PCLASSINFO(MyConfEndPoint, OpalConfEndPoint);
public:
  MyConfEndPoint(MyManager & manager);
  virtual OpalMixerNode * CreateNode(
    const OpalMixerNodeInfo & info ///< Initial info for node
  ) override;
  virtual void OnModeratorCommand(OpalConfConnection & connection, const PCaselessString & cmd) override;
protected:
  MyManager & m_manager;
};
#endif

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


class MyManager : public OpalManagerCLI
{
    PCLASSINFO(MyManager, OpalManagerCLI)

  public:
    MyManager();

    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfAdd);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfList);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfRemove);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfRecord);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfPlay);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdConfMonitor);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdMemberAdd);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdMemberList);
    PDECLARE_NOTIFIER(PCLI::Arguments, MyManager, CmdMemberRemove);

    bool CmdConfXXX(PCLI::Arguments & args, PSafePtr<OpalMixerNode> & node, PINDEX argCount);

    PString GetArgumentSpec() const override;
    virtual bool Initialise(PArgList & args, bool verbose, const PString & defaultRoute = PString::Empty()) override;

    virtual void OnEstablishedCall(OpalCall & call) override;
    virtual void OnClearedCall(OpalCall & call) override;

    void Broadcast(const PString & str) override { m_cli->Broadcast(str); }

    OpalConfEndPoint * m_confEP;
    PString m_monitorToken;
};


#endif  // _OPAL_MCU_MAIN_H


// End of File ///////////////////////////////////////////////////////////////
