/*
 * vxml.cxx
 *
 * VXML control for for Opal
 *
 * A H.323 IVR application.
 *
 * Copyright (C) 2002 Equivalence Pty. Ltd.
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
 * The Original Code is Portable Windows Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 */

#ifdef P_USE_PRAGMA
#pragma implementation "opalvxml.h"
#endif

#include <ptlib.h>

#include <opal_config.h>

#include <ep/ivr.h>

#include <opal/call.h>
#include <codec/opalwavfile.h>


///////////////////////////////////////////////////////////////

#if OPAL_IVR

OpalVXMLSession::OpalVXMLSession(OpalIVRConnection & conn)
  : m_connection(conn)
{
  PTRACE_CONTEXT_ID_FROM(conn);
}


void OpalVXMLSession::OnEndDialog()
{
  m_connection.OnEndDialog();
  PVXMLSession::OnEndDialog();
}


void OpalVXMLSession::OnEndSession()
{
  PTRACE(3, "IVR\tEnd of session, releasing connection.");
  m_connection.Release();
}


OpalVXMLSession::TransferStatus OpalVXMLSession::OnTransfer(const PString & destination, TransferType type)
{
  if (type == BridgedTransfer)
    return TransferUnsupported;

  OpalCall & call = m_connection.GetCall();
  if (!call.Transfer(destination) && !call.Transfer(destination, &m_connection))
    return TransferFailed;
  return TransferInProgress;
}


#endif // OPAL_IVR


// End of File /////////////////////////////////////////////////////////////
