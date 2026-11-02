# ICE in OPAL

OPAL implements Interactive Connectivity Establishment on the existing media
transport. `OpalICEMediaTransport` owns the UDP sockets, demultiplexes STUN
from RTP, and applies the nominated pair by changing the socket send address.
`OpalICEAgent` is the per-component checklist (pairing, checks, nomination,
restart). RTP, RTCP, SRTP and DTLS stay above that transport.

ICE is off unless an application asks for it. `Offer-ICE` defaults to false,
so SDP from existing applications is unchanged. `ICE-Lite` defaults to true,
which keeps the historical answer-only behaviour. Set `ICE-Lite` to false for
a full RFC 8445 agent.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `Offer-ICE` | false | Put `ice-ufrag`, `ice-pwd` and candidates in SDP |
| `Disable-ICE` | false | Use a plain UDP transport and skip ICE entirely |
| `ICE-Lite` | true | Answer checks only. `false` runs full ICE |
| `Trickle-ICE` | true | Send host candidates immediately; later candidates go in a subsequent offer. `a=end-of-candidates` is added when gathering finishes |
| `ICE-Timeout` | manager setting, at least 15 seconds | Connectivity-check budget |
| `ICE-Gather-Timeout` | 5 seconds | How long gathering waits for STUN and TURN before end-of-candidates |
| `ICE-Relay-Only` | false | Checklist and SDP contain only relay candidates |
| `ICE-Consent` | false | RFC 7675 consent on DTLS-SRTP/WebRTC. When consent expires, media is not sent |
| `Network-Cost-ICE` | false | Prefer a lower network-cost candidate when ICE-Lite sees aggressive nomination |
| `ICE-Disable-mDNS` | false | Ignore `.local` candidate addresses |

STUN and TURN servers are the existing NAT methods:

```cpp
manager.SetSTUNServer("stun.example.com:3478");
manager.SetNATServer("TURN", "turn.example.com:3478", true, 0, iface);
// TURN credentials are set on the TURN NAT method and are not traced
```

Each call has its own media transport and therefore its own ICE agent.

A console or sample endpoint enables full ICE with:

```text
Offer-ICE=true
ICE-Lite=false
```

## OpenPhone

In OpenPhone use **Edit → Options → Networking**. The **ICE** controls sit in the **NAT/Firewall** section next to the STUN/TURN method list:

- **Offer ICE in SDP** sets `Offer-ICE`
- **Full ICE agent** clears `ICE-Lite` (unchecked keeps the historical lite answerer)
- **Trickle ICE** sets `Trickle-ICE`

Activate a STUN or TURN method and set its server in that same section. Applying with full ICE enabled and no active STUN or TURN server shows a warning; Apply still succeeds so host-only LAN ICE remains possible. Advanced options such as `ICE-Relay-Only` and `ICE-Consent` stay under SIP → More Options….

## What the agent does

Host candidates come from the bound socket. A wildcard bind also advertises
the host's other addresses of that family, and an IPv6 socket is added when
the primary socket is IPv4. Server-reflexive candidates are STUN Binding
responses on that host socket. Relay candidates are TURN allocations from
`PTURNUDPSocket` and are used only for pairs whose local candidate is relay.
Gathering runs on an owned thread, not on the SIP thread.

The checklist pairs local and remote candidates, freezes pairs that share a
foundation, and sends Binding requests paced at 50 ms with a 500 ms
retransmission interval. The offerer is controlling. The answerer is
controlled, unless the remote SDP says `a=ice-lite`, in which case the local
full agent is controlling and only that side sends checks. Role conflicts
use STUN error 487 and the existing tie-breaker. The controlling agent
nominates with `USE-CANDIDATE`. A nominated pair calls
`InternalSetRemoteAddress(..., e_RemoteAddressFromICE)`.

If the remote answer has no ICE credentials, ICE is disabled and the SDP
media address is used. If both sides exchanged credentials and every pair
fails, the SDP address is not installed.

An ICE restart is a changed remote `ice-ufrag`. Local credentials are
regenerated, the checklist is cleared, and the same sockets are reused.

NAT keepalives are Binding indications on the nominated pair. Consent
freshness, when `ICE-Consent` is true, is an authenticated Binding request.
Those are different: an indication does not authorise media, and classic SIP
does not stop sending when indications are lost.

## Example SDP

Offer (full ICE, gathering still in progress, trickle):

```sdp
v=0
o=- 1 1 IN IP4 192.0.2.10
s=-
c=IN IP4 192.0.2.10
t=0 0
m=audio 50000 RTP/AVP 0
a=rtpmap:0 PCMU/8000
a=rtcp-mux
a=ice-ufrag:aBcDeFgHiJkL
a=ice-pwd:0123456789abcdef0123456789abcdef
a=ice-options:trickle
a=candidate:1a2b3c4d 1 udp 2130706431 192.0.2.10 50000 typ host
```

Answer from a full ICE peer:

```sdp
v=0
o=- 2 1 IN IP4 198.51.100.20
s=-
c=IN IP4 198.51.100.20
t=0 0
m=audio 51000 RTP/AVP 0
a=rtpmap:0 PCMU/8000
a=rtcp-mux
a=ice-ufrag:mNoPqRsTuVwX
a=ice-pwd:fedcba9876543210fedcba9876543210
a=candidate:9f8e7d6c 1 udp 2130706431 198.51.100.20 51000 typ host
a=candidate:11223344 1 udp 1694498815 203.0.113.10 45000 typ srflx raddr 198.51.100.20 rport 51000
a=end-of-candidates
```

An ICE-Lite peer adds session-level `a=ice-lite` and does not send checks.
A peer with no `a=ice-ufrag` is not an ICE peer; OPAL sends media to the
`c=`/`m=` address.

A completed check logs a path summary:

```text
ICE State: Completed
Local Candidate: srflx
Remote Candidate: srflx
Protocol: udp
Local Address: 203.0.113.10:45000
Remote Address: 198.51.100.20:51000
RTT: 24 ms
```

Passwords, TURN secrets and message-integrity keys are not logged.

## Selected path and RTP

Nomination does not recreate the RTP session, reset sequence numbers, or
touch the jitter buffer or SRTP keys. The UDP destination changes. With
`a=rtcp-mux` there is one component. Without it, RTP and RTCP have
independent pairs. A relay nomination sends through the TURN socket;
host and server-reflexive nominations send on the host socket.

## Tests

`icetest` covers priority, pair priority, foundation, username, role
conflict, candidate parsing, checklist transitions, nomination, restart,
two independent agents, a malformed STUN packet, and a localhost Binding
exchange against `PSTUNServer`.

```text
./icetest
```

The following are manual. Do not treat them as verified until they have
been run:

1. Two OPAL endpoints on one LAN with `Offer-ICE=true` and `ICE-Lite=false`.
2. Endpoints on different networks, with a STUN server set on both.
3. One endpoint behind an endpoint-independent NAT, STUN configured.
4. One endpoint behind an address-and-port-dependent NAT.
5. Both endpoints behind restrictive NATs, with TURN configured. Direct
   pairs should fail and the relay candidate should be nominated.
6. IPv6-only: bind the media interface to an IPv6 address.
7. Dual-stack: bind IPv4 any so host gathering also opens an IPv6 socket.
8. `RTCP-Mux=true` and a call without mux (two components).
9. SRTP (`RTP/SAVP`) or DTLS-SRTP. Confirm media still decrypts after
   nomination; ICE must not change keys.
10. A SIP endpoint with `Offer-ICE=false`, or a peer that answers without
    `ice-ufrag`. Media uses the SDP address.
11. An ICE-Lite peer (`a=ice-lite`). The OPAL full agent must be controlling.
12. Re-INVITE that changes `ice-ufrag` during a call.
13. Hang up while checks are still running. The transport destructor stops
    the gather thread and the check timer.
14. Two simultaneous calls.
15. coturn for the TURN cases. Browser, PJSIP and libnice interop are not
    claimed here.

Linux network namespaces are a reasonable way to build cases 3 to 5. This
tree does not ship that harness.

## Standards

Implemented:

- RFC 8445 candidate gathering for host, server-reflexive, peer-reflexive
  and relay, priority, foundations, checklist, nomination, role conflict,
  ICE restart
- RFC 8489 STUN Binding, short-term MESSAGE-INTEGRITY, FINGERPRINT, via PTLib
- RFC 8656 TURN Allocate / ChannelBind, via the existing `PTURNUDPSocket`
- RFC 8839 ICE credentials and candidates in SDP offer/answer
- RFC 8838 trickle to the extent of sending candidates gathered so far and
  `end-of-candidates` when gathering finishes. A later offer picks up
  candidates that were not ready. SIP INFO `application/trickle-ice-sdpfrag`
  (RFC 8840) is not implemented
- RFC 8863 one component when `a=rtcp-mux` is negotiated
- RFC 7675 consent freshness when `ICE-Consent` is true
- ICE-Lite peers, and local ICE-Lite when `ICE-Lite` remains true

Not implemented:

- RFC 6544 TCP candidates
- Aggressive nomination as the sender. Incoming aggressive nomination is
  still accepted, which is what current Firefox sends
- SIP INFO trickle fragments
- mDNS candidate resolution. `.local` names are ignored when
  `ICE-Disable-mDNS` is set

## Files

- `include/sdp/ice.h`, `src/sdp/ice.cxx` — transport, gathering, checks
- `src/sdp/ice_agent.cxx` — checklist state machine
- `src/sdp/sdp.cxx` — existing candidate SDP, unchanged on the wire for ICE-Lite
- `src/opal/mediasession.cxx` — `UseNatTraversal()` so full ICE keeps a host socket
- `samples/openphone/` — Networking → ICE options UI
- PTLib `pstun.h` / `pstun.cxx` — Binding indication, shared tie-breaker,
  short-term integrity without an empty REALM
- `samples/test/ice/main.cxx` — `icetest`
