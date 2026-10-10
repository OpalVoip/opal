/*
 * ice.h
 *
 * Interactive Connectivity Establishment
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
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Vox Lucida Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 */

#ifndef OPAL_SIP_ICE_H
#define OPAL_SIP_ICE_H

#ifdef P_USE_PRAGMA
#pragma interface
#endif

#include <opal_config.h>

#if OPAL_ICE

#include <opal/mediasession.h>
#include <ptclib/pstunsrvr.h>


/**String option key to an integer indicating the time in seconds to
   wait for any ICE/STUN messages. Default 5.
  */
#define OPAL_OPT_ICE_TIMEOUT "ICE-Timeout"

/**Enable ICE-Lite.
   Defaults to true, which preserves the historical answer-only behaviour.
   Set to false to run a full RFC 8445 ICE agent on this media transport.
*/
#define OPAL_OPT_ICE_LITE "ICE-Lite"

/**Execute trickle ICE.
   Defaults to true.
   When full ICE is enabled, candidates learned after the offer are included
   in a later offer. End-of-candidates is emitted once gathering finishes.
*/
#define OPAL_OPT_TRICKLE_ICE "Trickle-ICE"

/**Support network cost in ICE candidate selection.
   Defaults to false.
*/
#define OPAL_OPT_NETWORK_COST_ICE "Network-Cost-ICE"

/**Offer and check only relay candidates.
   Defaults to false. Host and server-reflexive candidates are still gathered
   when this is false.
*/
#define OPAL_OPT_ICE_RELAY_ONLY "ICE-Relay-Only"

/**RFC 7675 consent freshness.
   Defaults to false. When true, media stops if authenticated consent expires.
   Intended for DTLS-SRTP / WebRTC transports. Classic SIP ICE uses STUN
   binding indications as NAT keepalives and does not stop media.
*/
#define OPAL_OPT_ICE_CONSENT "ICE-Consent"

/**Time to wait for STUN and TURN gathering before end-of-candidates.
   Defaults to 5 seconds. Gathering never runs on the SIP signalling thread.
*/
#define OPAL_OPT_ICE_GATHER_TIMEOUT "ICE-Gather-Timeout"


/** RFC 8445 checklist for one ICE component.
    The media transport owns the sockets. This object owns candidate lists,
    pair states, nomination and restart, and can be driven without a network.
*/
class OpalICEAgent : public PObject
{
    PCLASSINFO(OpalICEAgent, PObject);
  public:
    enum State {
      New,
      Gathering,
      Checking,
      Connected,
      Completed,
      Failed,
      Closed
    };

    enum PairState {
      Frozen,
      Waiting,
      InProgress,
      Succeeded,
      FailedPair
    };

    enum Role {
      UnknownRole,
      Controlling,
      Controlled,
      Lite
    };

    struct Pair {
      PNatCandidate local;
      PNatCandidate remote;
      PairState     state;
      bool          nominated;
      bool          triggered;
      bool          nominateOnSend;
      unsigned      attempts;
      PTime         lastSent;
      PBYTEArray    transactionId;
      int           rttMs;
      uint64_t      priority;

      Pair()
        : state(Frozen)
        , nominated(false)
        , triggered(false)
        , nominateOnSend(false)
        , attempts(0)
        , rttMs(-1)
        , priority(0)
      {
      }
    };

    struct InboundResult {
      bool          handled;
      bool          nominated;
      PNatCandidate local;
      PNatCandidate remote;

      InboundResult() : handled(false), nominated(false) { }
    };

    struct CheckResult {
      bool          matched;
      bool          nominated;
      bool          roleConflict;
      bool          checklistFailed;
      bool          addedPeerReflexive;
      PNatCandidate local;
      PNatCandidate remote;
      int           rttMs;

      CheckResult()
        : matched(false)
        , nominated(false)
        , roleConflict(false)
        , checklistFailed(false)
        , addedPeerReflexive(false)
        , rttMs(-1)
      {
      }
    };

    explicit OpalICEAgent(unsigned component = 1);

    static unsigned TypePreference(PNatCandidate::Types type);
    static unsigned CandidatePriority(PNatCandidate::Types type, unsigned localPreference, unsigned component);
    static uint64_t PairPriority(unsigned controllingPriority, unsigned controlledPriority);
    static PString Foundation(PNatCandidate::Types type, const PString & base, const PString & protocol, const PString & server);
    static PString Username(const PString & remoteUfrag, const PString & localUfrag);
    static bool MustSwitchRole(bool localControlling, uint64_t localTieBreak, uint64_t remoteTieBreak);
    static const char * StateName(State state);

    unsigned GetComponent() const { return m_component; }
    State GetState() const { return m_state; }
    Role GetRole() const { return m_role; }
    bool IsControlling() const { return m_role == Controlling; }
    bool IsLite() const { return m_role == Lite; }
    const PString & GetFailureReason() const { return m_failureReason; }
    const vector<PNatCandidate> & LocalCandidates() const { return m_local; }
    const vector<Pair> & Checklist() const { return m_pairs; }

    void SetRole(Role role);
    void SetLocalLite(bool lite);
    void SetRemoteLite(bool lite);
    void SetOfferer(bool offerer);
    void SetRelayOnly(bool relayOnly);
    void SetConsentRequired(bool required);
    void AssignRole();

    void AddLocalCandidate(const PNatCandidate & candidate);
    bool AddRemoteCandidate(const PNatCandidate & candidate);
    void SetRemoteCredentials(const PString & ufrag, const PString & password);
    bool RemoteCredentialsChanged(const PString & ufrag) const;
    void FormChecklist();
    void Restart();
    void Close();

    int NextCheck(const PTime & now, const PTimeInterval & rto, unsigned maxAttempts);
    void NoteCheckSent(int index, const PBYTEArray & transactionId, const PTime & now, bool nominate);
    CheckResult OnCheckResult(const PBYTEArray & transactionId,
                              bool success,
                              unsigned errorCode,
                              const PIPSocketAddressAndPort & mapped,
                              const PTime & now);
    InboundResult OnInboundRequest(const PIPSocketAddressAndPort & from, unsigned remotePriority, bool useCandidate);
    void Unfreeze(const PString & localFoundation, const PString & remoteFoundation);
    bool GetNominated(PNatCandidate & local, PNatCandidate & remote) const;
    bool ConsentExpired(const PTime & now, const PTimeInterval & expiry) const;
    void NoteConsent(const PTime & now);
    bool ConsentRequired() const { return m_consentRequired; }
    void CopyLocalCandidates(PNatCandidateList & candidates) const;
    void PrintSelectedPath(ostream & strm) const;

  protected:
    int FindTransaction(const PBYTEArray & transactionId) const;
    int FindRemoteAddress(const PIPSocketAddressAndPort & address) const;
    bool KnownLocalAddress(const PIPSocketAddressAndPort & address) const;
    void SortPairs();
    void FailIfExhausted();
    void SelectNominated(int index);

    unsigned              m_component;
    State                 m_state;
    Role                  m_role;
    bool                  m_localLite;
    bool                  m_remoteLite;
    bool                  m_offerer;
    bool                  m_relayOnly;
    bool                  m_consentRequired;
    bool                  m_haveConsent;
    PTime                 m_lastConsent;
    PString               m_remoteUsername;
    PString               m_remotePassword;
    PString               m_failureReason;
    vector<PNatCandidate> m_local;
    vector<PNatCandidate> m_remote;
    vector<Pair>          m_pairs;
    PNatCandidate         m_selectedLocal;
    PNatCandidate         m_selectedRemote;
};


/** Class for low level transport of media that uses ICE
  */
class OpalICEMediaTransport : public OpalUDPMediaTransport
{
    PCLASSINFO(OpalICEMediaTransport, OpalUDPMediaTransport);
  public:
    OpalICEMediaTransport(OpalConnection & connection, const PString & name);
    ~OpalICEMediaTransport();

    P_DECLARE_BITWISE_ENUM(Options, 2, (
      OptNone,
      OptLite,
      OptTrickle
    ));

    virtual bool Open(OpalMediaSession & session, PINDEX count, const PString & localInterface, const OpalTransportAddress & remoteAddress) override;
    virtual bool Write(const void * data, PINDEX length, SubChannels subchannel = e_Media, const PIPSocketAddressAndPort * remote = NULL, int * mtu = NULL) override;
    virtual bool IsEstablished() const override;
    virtual void SetCandidates(const PString & user, const PString & pass, const PNatCandidateList & candidates, unsigned options = OptNone) override;
    virtual void AddCandidate(const PNatCandidate & candidate) override;
    virtual bool GetCandidates(PString & user, PString & pass, PNatCandidateList & candidates, unsigned & options, bool offering) override;
#if OPAL_STATISTICS
    virtual void GetStatistics(OpalMediaStatistics & statistics) const override;
#endif

  protected:
    PString       m_localUsername;    // ICE username sent to remote
    PString       m_localPassword;    // ICE password sent to remote
    PString       m_remoteUsername;   // ICE username expected from remote
    PString       m_remotePassword;   // ICE password expected from remote
    PTimeInterval m_iceTimeout;
    bool          m_localLite;
    bool          m_remoteLite;
    bool          m_localTrickle;
    bool          m_remoteTrickle;
    bool          m_useNetworkCost;
    bool          m_offerer;
    bool          m_relayOnly;
    bool          m_consentFreshness;
    bool          m_iceDisabled;
    bool          m_checklistFailed;
    bool          m_gatheringComplete;
    atomic<bool>  m_stopGather;
    PTimeInterval m_gatherTimeout;
    PTimer        m_checkTimer;
    PTimer        m_consentTimer;
    PThread     * m_gatherThread;
    vector<PUDPSocket *> m_turnSockets;
    vector<PUDPSocket *> m_extraSockets;
    PUDPSocket  * m_allocatingSocket;

    PDECLARE_NOTIFIER(PTimer, OpalICEMediaTransport, OnCheckTimer);
    PDECLARE_NOTIFIER(PTimer, OpalICEMediaTransport, OnConsentTimer);

    virtual bool UseNatTraversal() const override;
    virtual bool InternalRxData(SubChannels subchannel, const PBYTEArray & data) override;
    virtual bool InternalOpenPinHole(PUDPSocket & socket) override;
    virtual PChannel * AddWrapperChannels(SubChannels subchannel, PChannel * channel) override;
    virtual PTimeInterval GetTimeout(SubChannels subchannel) const override;

    void GatherHostCandidates();
    void StartGathering();
  public:
    void GatherThreadMain();
  protected:
    void SendOneCheck();
    void SendConsentOrKeepalive();
    void AssignRoles();
    void ApplyNominated(SubChannels subchannel, const PNatCandidate & local, const PNatCandidate & remote);
    void TraceSelectedPath(SubChannels subchannel) const;
    PUDPSocket * SocketForCandidate(SubChannels subchannel, const PNatCandidate & local) const;
    void NoteTransaction(const PBYTEArray & id, SubChannels subchannel, int pairIndex, bool gather, bool consent);
    bool TakeTransaction(const PBYTEArray & id, SubChannels & subchannel, int & pairIndex, bool & gather, bool & consent);

    struct PendingTxn {
      SubChannels subchannel;
      int         pairIndex;
      bool        gather;
      bool        consent;
    };
    std::map<std::string, PendingTxn> m_pending;
    PDECLARE_MUTEX(m_gatherMutex);

    enum CandidateStates
    {
      e_CandidateInProgress,
      e_CandidateWaiting,
      e_CandidateFrozen,
      e_CandidateFailed,
      e_CandidateSucceeded
    };

#if OPAL_STATISTICS
    typedef OpalCandidateStatistics CandidateBase;
#else
    typedef PNatCandidate           CandidateBase;
#endif
    struct CandidatePair
    {
      CandidateBase   m_local;
      CandidateBase   m_remote;
      CandidateStates m_state;

      CandidatePair(const PNatCandidate & local, const PNatCandidate & remote)
        : m_local(local)
        , m_remote(remote)
        , m_state(e_CandidateWaiting)
      {
      }
    };
    typedef std::vector<CandidatePair> CandidatePairs;

    enum ChecklistState
    {
      // Note values and order are important
      e_Disabled,  // This is equivalent to RCF 8445 "failed"
      e_Completed,
      e_Offering,  // States above here are equivalent to RCF 8445 "running"
      e_OfferAnswered,
      e_Answering
    };

    class ICEChannel : public PIndirectChannel
    {
        PCLASSINFO(ICEChannel, PIndirectChannel);
      public:
        ICEChannel(OpalICEMediaTransport & owner, SubChannels subchannel, PChannel * channel);
        virtual PBoolean Read(void * buf, PINDEX len) override;
        void AddRemoteCandidate(const PNatCandidate & candidate PTRACE_PARAM(, const char * where));
        void GetLocalCandidates(PNatCandidateList & candidates);
        bool AddLocalCandidate(const OpalTransportAddress & address);
        bool HandleICE(const void * buf, PINDEX len);
        bool HandleICEFull(const void * buf, PINDEX len);

        OpalICEMediaTransport & m_owner;
        SubChannels             m_subchannel;
        vector<PNatCandidate>   m_localCandidates;
        ChecklistState          m_state;
        CandidatePairs          m_checklist;
        PNatCandidate           m_selectedCandidate;
        OpalICEAgent            m_agent;
        PIPSocketAddressAndPort m_lastFrom;
        bool                    m_haveLastFrom;
        bool                    m_relaySelected;
    };
    std::vector<ICEChannel *> m_iceChannelCache;
    ICEChannel & GetICEChannel(SubChannels subchannel) const { return *m_iceChannelCache[subchannel]; }

    PSTUNServer m_server;
    PSTUNClient m_client;
};


#endif // OPAL_ICE

#endif // OPAL_SIP_ICE_H
