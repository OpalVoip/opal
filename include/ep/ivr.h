/*
 * ivr.h
 *
 * Interactive Voice Response support.
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2001 Equivalence Pty. Ltd.
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
 */

#ifndef OPAL_OPAL_IVR_H
#define OPAL_OPAL_IVR_H

#ifdef P_USE_PRAGMA
#pragma interface
#endif

#include <opal_config.h>

#if OPAL_IVR

#include <ep/opalvxml.h>
#include <ep/localep.h>

class OpalIVRConnection;


#define OPAL_IVR_PREFIX "ivr"

/// IVR supports these codec types natively, defualt to none.
#define OPAL_OPT_IVR_NATIVE_CODEC "IVR-Native-Codec"

/// IVR Text To Speech engine name.
#define OPAL_OPT_IVR_TEXT_TO_SPEECH "IVR-TTS"

/// IVR Speech Recognition engine name.
#define OPAL_OPT_IVR_SPEECH_RECOGNITION "IVR-SR"

/// IVR recording directory.
#define OPAL_OPT_IVR_RECORDING_DIR "IVR-Recording-Dir"

/** IVR Text To Speech cache directory.
    Note, that if this is set to a different directory to the shared cache
    between all connections in OpalIVREndPoint, then that shared cache is no
    longer used. Unexpected behaviour could occur if multiple connections use
    the same directory, but not that shared cache manager instance.
  */
#define OPAL_OPT_IVR_TTS_CACHE_DIR "IVR-Cache-Dir"

/** IVR properties of the form IVR-Property=<name>:<value>.
    For example IVR-Property=documentmaxage:0
    Multiple properties can be set via the option being added multiple times.
 */
#define OPAL_OPT_IVR_PROPERTY "IVR-Property"


/**Interactive Voice Response endpoint.
 */
class OpalIVREndPoint : public OpalLocalEndPoint
{
    PCLASSINFO(OpalIVREndPoint, OpalLocalEndPoint);
  public:
  /**@name Construction */
  //@{
    /**Create a new endpoint.
     */
    OpalIVREndPoint(
      OpalManager & manager,  ///<  Manager of all endpoints.
      const char * prefix = OPAL_IVR_PREFIX ///<  Prefix for URL style address strings
    );

    /**Destroy endpoint.
     */
    ~OpalIVREndPoint();
  //@}

  /**@name Overrides from OpalEndPoint */
  //@{
    /**Set up a connection to a remote party.
       This is called from the OpalManager::MakeConnection() function once
       it has determined that this is the endpoint for the protocol.

       The general form for this party parameter is:

            [proto:][alias@][transport$]address[:port]

       where the various fields will have meanings specific to the endpoint
       type. For example, with H.323 it could be "h323:Fred@site.com" which
       indicates a user Fred at gatekeeper size.com. Whereas for the PSTN
       endpoint it could be "pstn:5551234" which is to call 5551234 on the
       first available PSTN line.

       The proto field is optional when passed to a specific endpoint. If it
       is present, however, it must agree with the endpoints protocol name or
       false is returned.

       This function usually returns almost immediately with the connection
       continuing to occur in a new background thread.

       If false is returned then the connection could not be established. For
       example if a PSTN endpoint is used and the assiciated line is engaged
       then it may return immediately. Returning a non-NULL value does not
       mean that the connection will succeed, only that an attempt is being
       made.

       The default behaviour sets up an IVR connection.
     */
    virtual PSafePtr<OpalConnection> MakeConnection(
      OpalCall & call,          ///<  Owner of connection
      const PString & party,    ///<  Remote party to call
      void * userData = NULL,   ///<  Arbitrary data to pass to connection
      unsigned int options = 0, ///<  options to pass to conneciton
      OpalConnection::StringOptions * stringOptions = NULL
    ) override;

    /**Get the data formats this endpoint is capable of operating.
       This provides a list of media data format names that may be used by an
       OpalMediaStream may be created by a connection from this endpoint.

       Note that a specific connection may not actually support all of the
       media formats returned here, but should return no more.

       The default behaviour returns the basic media formats for PCM/YUV.
      */
    virtual OpalMediaFormatList GetMediaFormats() const override;

    /** Get available string option names.
      */
    virtual PStringList GetAvailableStringOptions() const override;
  //@}

  /**@name Customisation call backs */
  //@{
    /**Create a connection for the PCSS endpoint.
       The default implementation is to create a OpalPCSSConnection.
      */
    virtual OpalIVRConnection * CreateConnection(
      OpalCall & call,        ///<  Owner of connection
      void * userData,        ///<  Arbitrary data to pass to connection
      const PString & vxml,   ///<  vxml to execute
      unsigned int options,
      OpalConnection::StringOptions * stringOptions = NULL
    );
  //@}

  /**@name Options and configuration */
  //@{
    /**Find a connection that uses the specified token.
       This searches the endpoint for the connection that contains the token
       as provided by functions such as MakeConnection().
      */
    PSafePtr<OpalIVRConnection> GetIVRConnectionWithLock(
      const PString & token,     ///<  Token to identify connection
      PSafetyMode mode = PSafeReadWrite
    ) { return PSafePtrCast<OpalConnection, OpalIVRConnection>(GetConnectionWithLock(token, mode)); }

    /**Get the default VXML to use.
      */
    const PString & GetDefaultVXML() const { return m_defaultVXML; }

    /** Set the default VXML to use.
      */
    void SetDefaultVXML(
      const PString & vxml
    );

    /** Called when VXML ends.
        Gives an opputunity for an application to do somthing when script ends,
        start a new script for instance.

        Default action does nothing, session will end and connection will be
        released.
      */
    virtual void OnEndDialog(
      OpalIVRConnection & connection  ///< Connection with ending dialog
    );

    /** Set the default text to speech engine used by the IVR  
      */
    void SetDefaultTextToSpeech(const PString & tts) { m_defaultTextToSpeech = tts; }

    /** Get the default text to speech engine used by the IVR  
      */
    const PString & GetDefaultTextToSpeech() const { return m_defaultTextToSpeech; }

    /** Set the default speech recognition engine used by the IVR  
      */
    void SetDefaultSpeechRecognition(const PString & sr) { m_defaultSpeechRecognition = sr; }

    /** Get the default speech recognition engine used by the IVR  
      */
    const PString & GetDefaultSpeechRecognition() const { return m_defaultSpeechRecognition; }

    /**Get the text to speach cache directory to use.
      */
    const PDirectory & GetCacheDir() const { return m_ttsCache->GetDirectory(); }

    /**Set the text to speach cache directory to use.
      */
    void SetCacheDir(
      const PDirectory & dir
    ) { m_ttsCache->SetDirectory(dir); }

    void SetRecordDirectory(const PDirectory & dir) { m_recordDirectory = dir; }
    const PDirectory & GetRecordDirectory() const   { return m_recordDirectory; }
  //@}

    // Allow users to override cache algorithm
    virtual PVXMLSession::CachePtr & GetTextToSpeechCache() { return m_ttsCache; }

  protected:
    PString                m_defaultVXML;
    PString                m_defaultTextToSpeech;
    PString                m_defaultSpeechRecognition;
    PDECLARE_MUTEX(        m_defaultsMutex);
    PVXMLSession::CachePtr m_ttsCache;
    PDirectory             m_recordDirectory;

  private:
    P_REMOVE_VIRTUAL(OpalIVRConnection *, CreateConnection(OpalCall &,const PString &,void *,const PString &,OpalConnection::StringOptions *),0);
};


/**Interactive Voice Response connection.
 */
class OpalIVRConnection : public OpalLocalConnection
{
    PCLASSINFO(OpalIVRConnection, OpalLocalConnection);
  public:
  /**@name Construction */
  //@{
    /**Create a new connection.
     */
    OpalIVRConnection(
      OpalCall & call,            ///<  Owner calll for connection
      OpalIVREndPoint & endpoint, ///<  Owner endpoint for connection
      void * userData,            ///<  Arbitrary data to pass to connection
      const PString & vxml,       ///<  vxml to execute
      unsigned int options,
      OpalConnection::StringOptions * stringOptions = NULL
    );

    /**Destroy connection.
     */
    ~OpalIVRConnection();
  //@}

  /**@name Overrides from OpalConnection */
  //@{
    /**Get indication of connection being to a "network".
       This indicates the if the connection may be regarded as a "network"
       connection. The distinction is about if there is a concept of a "remote"
       party being connected to and is best described by example: sip, h323,
       iax and pstn are all "network" connections as they connect to something
       "remote". While pc, pots and ivr are not as the entity being connected
       to is intrinsically local.
      */
    virtual bool IsNetworkConnection() const override { return false; }

    /// Call back for connection to act on changed string options
    virtual void OnApplyStringOptions() override;

    /**Clean up the termination of the connection.
       This function can do any internal cleaning up and waiting on background
       threads that may be using the connection object.

       Note that there is not a one to one relationship with the
       OnEstablishedConnection() function. This function may be called without
       that function being called. For example if SetUpConnection() was used
       but the call never completed.

       Classes that override this function should make sure they call the
       ancestor version for correct operation.

       An application will not typically call this function as it is used by
       the OpalManager during a release of the connection.

       The default behaviour calls the OpalEndPoint function of the same name.
      */
    virtual void OnReleased() override;

    /**Call back when media stream patch thread starts.

       Default behaviour calls OpalManager function of same name.
      */
    virtual void OnStartMediaPatch(
      OpalMediaPatch & patch    ///< Patch being started
    ) override;
    
    /**A call back function to monitor the progress of a transfer.

       The default behaviour calls the OpalEndPoint function of the same name.
       The default action of that function is to return false, thereby
       releasing the connection if the info["result"] == "success".
     */
    virtual bool OnTransferNotify(
      const PStringToString & info, ///< State of the transfer
      const OpalConnection * transferringConnection
                                    ///< Connection performing transfer, maybe "this"
    ) override;

    /**Initiate the transfer of an existing call (connection) to a new remote 
       party.

       If remoteParty is a valid call token, then the remote party is transferred
       to that party (consultation transfer) and both calls are cleared.
     */
    virtual bool TransferConnection(
      const PString & remoteParty   ///<  Remote party to transfer the existing call to
    ) override;

    /**Get the data formats this connection is capable of operating.
       This provides a list of media data format names that an
       OpalMediaStream may be created in within this connection.

       The default behaviour returns the formats the PSoundChannel can do,
       typically only PCM-16.
      */
    virtual OpalMediaFormatList GetMediaFormats() const override;

    /**Open a new media stream.
       This will create a media stream of an appropriate subclass as required
       by the underlying connection protocol. For instance H.323 would create
       an OpalRTPStream.

       The sessionID parameter may not be needed by a particular media stream
       and may be ignored. In the case of an OpalRTPStream it us used.

       Note that media streams may be created internally to the underlying
       protocol. This function is not the only way a stream can come into
       existance.

       The default behaviour creates a OpalIVRMediaStream.
     */
    virtual OpalMediaStream * CreateMediaStream(
      const OpalMediaFormat & mediaFormat, ///<  Media format for stream
      unsigned sessionID,                  ///<  Session number for stream
      PBoolean isSource                        ///<  Is a source stream
    ) override;

    /**Send a user input indication to the remote endpoint.
       This sends an arbitrary string as a user indication. If DTMF tones in
       particular are required to be sent then the SendIndicationTone()
       function should be used.

       The default behaviour plays the DTMF tones on the line.
      */
    virtual PBoolean SendUserInputString(
      const PString & value                   ///<  String value of indication
    ) override;
  //@}

    /** Called when VXML ends.
        Default action calls equivalent function in OpalIVREndPoint.
      */
    virtual void OnEndDialog();

    void SetVXML(const PString & vxml);
    const PString & GetVXML() const { return m_vxmlScript; }
    const OpalVXMLSession & GetVXMLSession() const { return m_vxmlSession; }
    OpalVXMLSession & GetVXMLSession() { return m_vxmlSession; }

    PTextToSpeech * GetTextToSpeech() const { return m_vxmlSession.GetTextToSpeech(); }
    PTextToSpeech * SetTextToSpeech(const PString & ttsName) { return m_vxmlSession.SetTextToSpeech(ttsName); }
    PTextToSpeech * SetTextToSpeech(PTextToSpeech * tts, PBoolean autoDelete = false) { return m_vxmlSession.SetTextToSpeech(tts, autoDelete); }

  protected:
    virtual bool StartVXML();
    virtual bool StartScript();

    OpalIVREndPoint      & m_ivrEndPoint;
    PString                m_vxmlScript;
    OpalVXMLSession        m_vxmlSession;
    atomic<bool>           m_vxmlStarted;
    PVXMLSession::CachePtr m_ttsCache;
};


/**This class describes a media stream that transfers data to/from an IVR
   vxml session.
  */
class OpalIVRMediaStream : public OpalRawMediaStream
{
    PCLASSINFO(OpalIVRMediaStream, OpalRawMediaStream);
  public:
  /**@name Construction */
  //@{
    /**Construct a new media stream for IVR session.
      */
    OpalIVRMediaStream(
      OpalIVRConnection & conn,
      const OpalMediaFormat & mediaFormat, ///<  Media format for stream
      unsigned sessionID,                  ///<  Session number for stream
      PBoolean isSource,                       ///<  Is a source stream
      PVXMLSession & vxml                  ///<  vxml session to use
    );
  //@}

  /**@name Overrides of OpalMediaStream class */
  //@{
    /**Open the media stream using the media format.

       The default behaviour simply sets the member variable "mediaFormat"
       and "defaultDataSize".
      */
    virtual PBoolean Open() override;

    /**Indicate if the media stream is synchronous.
       Returns false for IVR streams.
      */
    virtual PBoolean IsSynchronous() const override;
  //@}

  protected:
    virtual void InternalClose() override;

    PVXMLSession & m_vxmlSession;
};


#endif // OPAL_IVR

#endif // OPAL_OPAL_IVR_H


// End of File ///////////////////////////////////////////////////////////////
