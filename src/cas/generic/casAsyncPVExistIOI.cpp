
/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/*
 *      $Id$
 *
 *      Author  Jeffrey O. Hill
 *              johill@lanl.gov
 *              505 665 1831
 */

#define epicsExportSharedSymbols
#include "casAsyncPVExistIOI.h"

casAsyncPVExistIOI::casAsyncPVExistIOI (
        casAsyncPVExistIO & intf, const casCtx & ctx ) :
	casAsyncIOI ( ctx ),
    msg ( *ctx.getMsg() ),
    asyncPVExistIO ( intf ),
	retVal ( pverDoesNotExistHere ),
	dgOutAddr ( ctx.getClient()->fetchLastRecvAddr () ),
    protocolRevision ( ctx.getClient()->protocolRevision () ),
    sequenceNumber ( ctx.getClient()->datagramSequenceNumber () )
{
    ctx.getClient()->installAsynchIO ( *this );
}

caStatus casAsyncPVExistIOI::postIOCompletion ( 
    const pvExistReturn & retValIn )
{
	this->retVal = retValIn; 
	return this->insertEventQueue ();
}

caStatus casAsyncPVExistIOI::cbFuncAsyncIO ()
{
    caStatus 	status;
    
    if ( this->msg.m_cmmd == CA_PROTO_SEARCH ) {
        //
        // pass output DG address parameters
        //
        status = this->client.asyncSearchResponse (
            this->dgOutAddr, this->msg, this->retVal,
            this->protocolRevision, this->sequenceNumber );
        if ( status == S_cas_sendBlocked ) {
            return status;
        }
    }
    else {
        errPrintf ( S_cas_invalidAsynchIO, __FILE__, __LINE__,
            " - client request type = %u", this->msg.m_cmmd );
		status = S_cas_invalidAsynchIO;
    }

    if ( status != S_cas_sendBlocked ) {
        this->client.uninstallAsynchIO ( *this );
    }

    return status;
}
