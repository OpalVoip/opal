/*
 * main.cxx
 *
 * ICE agent unit tests and a localhost STUN exchange.
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (C) 2010 Vox Lucida Pty. Ltd.
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
 */

#include <ptlib.h>
#include <opal/manager.h>
#include <sdp/ice.h>
#include <sdp/sdp.h>

#include <iostream>

class Test : public PProcess
{
    PCLASSINFO(Test, PProcess)
  public:
    Test();
    virtual void Main() override;

  private:
    unsigned m_failed;

    void Expect(bool condition, const PString & name);
    PNatCandidate MakeCandidate(PNatCandidate::Types type, const PString & address, WORD port, unsigned component);
};


PCREATE_PROCESS(Test);


Test::Test()
  : PProcess("Open Phone Abstraction Library", "ICE Test", OPAL_MAJOR, OPAL_MINOR, ReleaseCode, OPAL_PATCH, false, false, OPAL_OEM)
  , m_failed(0)
{
}


void Test::Expect(bool condition, const PString & name)
{
  if (condition)
    cout << "ok   " << name << endl;
  else {
    cout << "FAIL " << name << endl;
    ++m_failed;
  }
}


PNatCandidate Test::MakeCandidate(PNatCandidate::Types type, const PString & address, WORD port, unsigned component)
{
  PIPSocket::Address ip(address);
  // Keep both branches as const PString & so MSVC can resolve the ternary
  // against Foundation(..., const PString & server).
  static const PString stunServer("stun.example");
  const PString & server = type == PNatCandidate::HostType ? PString::Empty() : stunServer;
  PNatCandidate candidate(type,
                          (PNatMethod::Component)component,
                          OpalICEAgent::Foundation(type, address, "udp", server),
                          OpalICEAgent::CandidatePriority(type, ip.GetVersion() == 6 ? 32768 : 65535, component),
                          "udp");
  candidate.m_baseTransportAddress.SetAddress(ip, port);
  return candidate;
}


void Test::Main()
{
#if !OPAL_ICE
  cerr << "OPAL was built without ICE" << endl;
  SetTerminationValue(1);
  return;
#else
  PArgList & args = GetArguments();
  args.Parse("h-help." PTRACE_ARGLIST);
  if (args.HasOption('h')) {
    args.Usage(cerr, "[ options ]");
    return;
  }
  PTRACE_INITIALISE(args);

  Expect(OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 65535, 1) ==
         ((126u << 24) | (65535u << 8) | 255u),
         "host candidate priority");
  Expect(OpalICEAgent::CandidatePriority(PNatCandidate::ServerReflexiveType, 65535, 1) <
         OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 1, 1),
         "type preference outranks local preference");
  Expect(OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 65535, 1) >
         OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 65535, 2),
         "RTP component outranks RTCP");

  uint64_t pair = OpalICEAgent::PairPriority(100, 200);
  Expect(pair == ((uint64_t)100 << 32) + 400, "pair priority uses the lesser value in the high bits");
  Expect(OpalICEAgent::PairPriority(200, 100) == pair + 1, "controlling candidate breaks a priority tie");

  PString foundation = OpalICEAgent::Foundation(PNatCandidate::HostType, "10.0.0.1:1000", "udp", "");
  Expect(foundation == OpalICEAgent::Foundation(PNatCandidate::HostType, "10.0.0.1:1000", "udp", ""),
         "foundation is stable");
  Expect(foundation != OpalICEAgent::Foundation(PNatCandidate::ServerReflexiveType, "10.0.0.1:1000", "udp", "stun.example"),
         "foundation changes with candidate type and server");
  Expect(foundation.GetLength() == 8, "foundation is 8 hex digits");

  Expect(OpalICEAgent::Username("remote", "local") == "remote:local", "ICE username is remote-ufrag:local-ufrag");
  Expect(OpalICEAgent::MustSwitchRole(true, 5, 10), "lower controlling tie-break switches role");
  Expect(!OpalICEAgent::MustSwitchRole(true, 10, 5), "higher controlling tie-break keeps the role");
  Expect(OpalICEAgent::MustSwitchRole(false, 10, 5), "higher controlled tie-break becomes controlling");
  Expect(!OpalICEAgent::MustSwitchRole(false, 5, 10), "lower controlled tie-break stays controlled");

  PNatCandidate parsed;
  Expect(SDPMediaDescription::ParseCandidate("abc 1 udp 2130706431 192.0.2.1 5000 typ host", parsed, true),
         "parse a host candidate");
  Expect(parsed.m_foundation == "abc" && parsed.m_component == 1 && parsed.m_type == PNatCandidate::HostType, "parsed candidate fields");
  Expect(!SDPMediaDescription::ParseCandidate("candidate:only-three-fields", parsed, true), "reject a malformed candidate");
  Expect(SDPMediaDescription::ParseCandidate("end-of-candidates", parsed, true) && parsed.m_type == PNatCandidate::FinalType,
         "parse end-of-candidates");

  OpalICEAgent offerer(1);
  OpalICEAgent answerer(1);
  offerer.SetOfferer(true);
  offerer.AssignRole();
  answerer.SetOfferer(false);
  answerer.AssignRole();
  Expect(offerer.GetRole() == OpalICEAgent::Controlling, "offerer is controlling");
  Expect(answerer.GetRole() == OpalICEAgent::Controlled, "answerer is controlled");

  answerer.SetRemoteLite(true);
  answerer.AssignRole();
  Expect(answerer.GetRole() == OpalICEAgent::Controlling, "full agent takes control against ICE-Lite");

  offerer.SetRemoteLite(false);
  offerer.SetOfferer(true);
  offerer.AssignRole();
  offerer.AddLocalCandidate(MakeCandidate(PNatCandidate::HostType, "127.0.0.1", 40000, 1));
  offerer.AddRemoteCandidate(MakeCandidate(PNatCandidate::HostType, "127.0.0.1", 40002, 1));
  offerer.SetRemoteCredentials("answer", "answer-password");
  offerer.FormChecklist();
  Expect(offerer.GetState() == OpalICEAgent::Checking, "checklist enters Checking");
  Expect(offerer.Checklist().size() == 1, "one local and one remote candidate make one pair");
  Expect(offerer.Checklist()[0].state == OpalICEAgent::Waiting, "only pair starts Waiting");

  int index = offerer.NextCheck(PTime(), 500, 7);
  Expect(index == 0, "a waiting pair is selected");
  PBYTEArray tid(12);
  memset(tid.GetPointer(12), 0x11, 12);
  offerer.NoteCheckSent(index, tid, PTime(), false);
  Expect(offerer.Checklist()[0].state == OpalICEAgent::InProgress, "sent check is InProgress");

  OpalICEAgent::CheckResult result = offerer.OnCheckResult(tid, true, 0, PIPSocketAddressAndPort(), PTime());
  Expect(result.matched && offerer.GetState() == OpalICEAgent::Connected, "a successful check connects");
  index = offerer.NextCheck(PTime(), 500, 7);
  Expect(index == 0 && offerer.Checklist()[0].state == OpalICEAgent::Succeeded, "controlling agent nominates the succeeded pair");
  PBYTEArray nominate(12);
  memset(nominate.GetPointer(12), 0x22, 12);
  offerer.NoteCheckSent(index, nominate, PTime(), true);
  result = offerer.OnCheckResult(nominate, true, 0, PIPSocketAddressAndPort(), PTime());
  Expect(result.nominated && offerer.GetState() == OpalICEAgent::Completed, "nominated check completes the checklist");

  PNatCandidate local, remote;
  Expect(offerer.GetNominated(local, remote) && remote.m_baseTransportAddress.GetPort() == 40002,
         "nominated remote address is kept");

  Expect(offerer.RemoteCredentialsChanged("other-ufrag"), "a new remote ufrag is an ICE restart");
  offerer.Restart();
  Expect(offerer.Checklist().empty() && offerer.GetState() == OpalICEAgent::Gathering, "restart clears pairs and keeps local candidates");
  Expect(!offerer.LocalCandidates().empty(), "restart keeps gathered local candidates");

  OpalICEAgent frozen(1);
  frozen.SetOfferer(true);
  frozen.AssignRole();
  frozen.AddLocalCandidate(MakeCandidate(PNatCandidate::HostType, "127.0.0.1", 41000, 1));
  frozen.AddLocalCandidate(MakeCandidate(PNatCandidate::ServerReflexiveType, "192.0.2.10", 41000, 1));
  frozen.AddRemoteCandidate(MakeCandidate(PNatCandidate::HostType, "198.51.100.20", 51000, 1));
  frozen.FormChecklist();
  Expect(frozen.Checklist().size() == 2, "two local candidates pair with one remote candidate");
  unsigned waiting = 0, frozenCount = 0;
  for (size_t i = 0; i < frozen.Checklist().size(); ++i) {
    if (frozen.Checklist()[i].state == OpalICEAgent::Waiting)
      ++waiting;
    if (frozen.Checklist()[i].state == OpalICEAgent::Frozen)
      ++frozenCount;
  }
  Expect(waiting == 2 && frozenCount == 0, "different foundations are both scheduled");

  PSTUNMessage garbage((const BYTE *)"not stun", 8, PIPSocketAddressAndPort());
  Expect(!garbage.IsValid(), "malformed STUN is rejected");

  PUDPSocket left, right;
  PIPSocket::Address loopback("127.0.0.1");
  Expect(left.Listen(loopback, 1, 0) && right.Listen(loopback, 1, 0), "bind two localhost sockets");

  PSTUN client;
  client.SetCredentials("bob:alice", "bob-password", "");
  client.SetIceRole(PSTUN::IceControlling);
  PSTUNMessage request(PSTUNMessage::BindingRequest);
  PSTUNIcePriority priority;
  priority.m_priority = OpalICEAgent::CandidatePriority(PNatCandidate::HostType, 65535, 1);
  request.AddAttribute(priority);
  client.AppendMessageIntegrity(request);
  request.AddFingerprint();

  PIPSocketAddressAndPort rightAddress;
  right.GetLocalAddress(rightAddress);
  Expect(request.Write(left, rightAddress), "send a connectivity check");

  PSTUNServer server;
  Expect(server.Open(&right), "open the answering STUN server");
  server.SetCredentials("bob:alice", "bob-password", "");
  server.SetIceRole(PSTUN::IceControlled);
  PSTUNServer::SocketInfo info(&right);
  PSTUNMessage received;
  right.SetReadTimeout(1000);
  Expect(server.Read(received, info) && received.IsRequest(), "answerer reads the connectivity check");
  Expect(server.OnReceiveMessage(received, info), "answerer authenticates and replies");

  left.SetReadTimeout(1000);
  PSTUNMessage response;
  Expect(response.Read(left) && response.IsSuccessResponse(), "offerer reads the binding response");
  Expect(client.IntegrityMatches(response), "response MESSAGE-INTEGRITY matches the ICE password");
  Expect(response.CheckFingerprint(true), "response has a valid fingerprint");
  PIPSocketAddressAndPort mapped;
  Expect(client.GetFromBindingResponse(response, mapped) && mapped.GetAddress() == loopback, "mapped address is the localhost source");

  OpalICEAgent second(1);
  second.SetOfferer(false);
  second.AssignRole();
  second.AddLocalCandidate(MakeCandidate(PNatCandidate::HostType, "127.0.0.1", 42000, 1));
  second.AddRemoteCandidate(MakeCandidate(PNatCandidate::HostType, "127.0.0.1", 42002, 1));
  second.FormChecklist();
  Expect(second.GetRole() == OpalICEAgent::Controlled && second.Checklist().size() == 1, "second agent checklist is independent");

  if (m_failed == 0)
    cout << "All ICE tests passed" << endl;
  else
    cout << m_failed << " ICE test(s) failed" << endl;
  SetTerminationValue(m_failed == 0 ? 0 : 1);
#endif
}
