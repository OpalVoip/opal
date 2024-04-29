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
   Defaults to true.
   NOTE!!! This is all that is supported at this time, do not set to false
           unless you are developing full ICE support ....
*/
#define OPAL_OPT_ICE_LITE "ICE-Lite"

/**Execute trickle ICE.
   Defaults to true.
   NOTE!!! As only ICE-Lite is supported at this time, this effectively
           means if a end-of-candidates entry is appended to offers/answers.
*/
#define OPAL_OPT_TRICKLE_ICE "Trickle-ICE"

/**Support network cost in ICE candidate selection.
   Defaults to false.
*/
#define OPAL_OPT_NETWORK_COST_ICE "Network-Cost-ICE"


/** Class for low level transport of media that uses ICE
  */
class OpalICEMediaTransport : public OpalUDPMediaTransport
{
    PCLASSINFO(OpalICEMediaTransport, OpalUDPMediaTransport);
  public:
    OpalICEMediaTransport(const PString & name);

    P_DECLARE_BITWISE_ENUM(Options, 2, (
      OptNone,
      OptLite,
      OptTrickle
    ));

    virtual bool Open(OpalMediaSession & session, PINDEX count, const PString & localInterface, const OpalTransportAddress & remoteAddress) override;
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

    virtual bool InternalRxData(SubChannels subchannel, const PBYTEArray & data) override;
    virtual bool InternalOpenPinHole(PUDPSocket & socket) override;
    virtual PChannel * AddWrapperChannels(SubChannels subchannel, PChannel * channel) override;
    virtual PTimeInterval GetTimeout(SubChannels subchannel) const override;

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

        OpalICEMediaTransport & m_owner;
        SubChannels             m_subchannel;
        vector<PNatCandidate>   m_localCandidates;
        ChecklistState          m_state;
        CandidatePairs          m_checklist;
        PNatCandidate           m_selectedCandidate;
    };
    std::vector<ICEChannel *> m_iceChannelCache;
    ICEChannel & GetICEChannel(SubChannels subchannel) const { return *m_iceChannelCache[subchannel]; }

    PSTUNServer m_server;
    PSTUNClient m_client;
};


#endif // OPAL_ICE

#endif // OPAL_SIP_ICE_H
