//
// fileDescriptorManager.process(delay);
// (the name of the global symbol has leaked in here)
//

//
// Example EPICS CA server
//
#include "exServer.h"

//
// static list of pre-created PVs
//
pvInfo exServer::pvList[] = {
    pvInfo (1.0e-1, "jane", 10.0f, 0.0f, excasIoSync, 1u),
    pvInfo (2.0, "fred", 10.0f, -10.0f, excasIoSync, 1u),
    pvInfo (1.0e-1, "janet", 10.0f, 0.0f, excasIoAsync, 1u),
    pvInfo (2.0, "freddy", 10.0f, -10.0f, excasIoAsync, 1u),
    pvInfo (2.0, "alan", 10.0f, -10.0f, excasIoSync, 100u),
    pvInfo (20.0, "albert", 10.0f, -10.0f, excasIoSync, 1000u)
};

const unsigned exServer::pvListNElem = NELEMENTS (exServer::pvList);

//
// static on-the-fly PVs
//
pvInfo exServer::bill (-1.0, "bill", 10.0f, -10.0f, excasIoSync, 1u);
pvInfo exServer::billy (-1.0, "billy", 10.0f, -10.0f, excasIoAsync, 1u);

//
// exServer::exServer()
//
exServer::exServer ( const char * const pvPrefix, 
                    unsigned aliasCount, bool scanOnIn ) : 
    simultAsychIOCount (0u),
    scanOn (scanOnIn)
{
    unsigned i;
    exPV *pPV;
    pvInfo *pPVI;
    pvInfo *pPVAfter = &exServer::pvList[pvListNElem];
    char pvAlias[256];
    const char * const pNameFmtStr = "%.100s%.20s";
    const char * const pAliasFmtStr = "%.100s%.20s%u";

    exPV::initFT();

    //
    // pre-create all of the simple PVs that this server will export
    //
    for (pPVI = exServer::pvList; pPVI < pPVAfter; pPVI++) {
        pPV = pPVI->createPV (*this, true, scanOnIn);
        if (!pPV) {
            fprintf(stderr, "Unable to create new PV \"%s\"\n",
                pPVI->getName());
        }


        //
        // Install canonical (root) name
        //
        sprintf(pvAlias, pNameFmtStr, pvPrefix, pPVI->getName());
        this->installAliasName(*pPVI, pvAlias);

        //
        // Install numbered alias names
        //
        for (i=0u; i<aliasCount; i++) {
            sprintf(pvAlias, pAliasFmtStr, pvPrefix,  
                    pPVI->getName(), i);
            this->installAliasName(*pPVI, pvAlias);
        }
    }

    //
    // Install create on-the-fly PVs 
    // into the PV name hash table
    //
    sprintf(pvAlias, pNameFmtStr, pvPrefix, bill.getName());
    this->installAliasName(bill, pvAlias);
    sprintf(pvAlias, pNameFmtStr, pvPrefix, billy.getName());
    this->installAliasName(billy, pvAlias);
}

//
// exServer::~exServer()
//
exServer::~exServer()
{
    pvInfo *pPVI;
    pvInfo *pPVAfter = 
        &exServer::pvList[NELEMENTS(exServer::pvList)];

    //
    // delete all pre-created PVs (eliminate bounds-checker warnings)
    //
    for (pPVI = exServer::pvList; pPVI < pPVAfter; pPVI++) {
        pPVI->deletePV ();
    }
    
    this->stringResTbl.traverse ( &pvEntry::destroy );
}

//
// exServer::installAliasName()
//
void exServer::installAliasName(pvInfo &info, const char *pAliasName)
{
    pvEntry *pEntry;

    pEntry = new pvEntry(info, *this, pAliasName);
    if (pEntry) {
        int resLibStatus;
        resLibStatus = this->stringResTbl.add(*pEntry);
        if (resLibStatus==0) {
            return;
        }
        else {
            delete pEntry;
        }
    }
    fprintf(stderr, 
"Unable to enter PV=\"%s\" Alias=\"%s\" in PV name alias hash table\n",
        info.getName(), pAliasName);
}

//
// exServer::pvExistTest()
//
pvExistReturn exServer::pvExistTest // X aCC 361
    (const casCtx& ctxIn, const char *pPVName)
{
    //
    // lifetime of id is shorter than lifetime of pName
    //
    stringId id(pPVName, stringId::refString);
    pvEntry *pPVE;

    //
    // Look in hash table for PV name (or PV alias name)
    //
    pPVE = this->stringResTbl.lookup(id);
    if (!pPVE) {
        return pverDoesNotExistHere;
    }

    pvInfo &pvi = pPVE->getInfo();

    //
    // Initiate async IO if this is an async PV
    //
    if (pvi.getIOType() == excasIoSync) {
        return pverExistsHere;
    }
    else {
        if (this->simultAsychIOCount>=maxSimultAsyncIO) {
            return pverDoesNotExistHere;
        }

        this->simultAsychIOCount++;

        exAsyncExistIO  *pIO;
        pIO = new exAsyncExistIO ( pvi, ctxIn, *this );
        if (pIO) {
            return pverAsyncCompletion;
        }
        else {
            return pverDoesNotExistHere;
        }
    }
}

//
// exServer::pvAttach()
//
pvAttachReturn exServer::pvAttach // X aCC 361
    (const casCtx &ctx, const char *pName)
{
    //
    // lifetime of id is shorter than lifetime of pName
    //
    stringId id(pName, stringId::refString); 
    exPV *pPV;
    pvEntry *pPVE;

    pPVE = this->stringResTbl.lookup(id);
    if (!pPVE) {
        return S_casApp_pvNotFound;
    }

    pvInfo &pvi = pPVE->getInfo();

    //
    // If this is a synchronous PV create the PV now 
    //
    if (pvi.getIOType() == excasIoSync) {
        pPV = pvi.createPV(*this, false, this->scanOn);
        if (pPV) {
            return *pPV;
        }
        else {
            return S_casApp_noMemory;
        }
    }
    //
    // Initiate async IO if this is an async PV
    //
    else {
        if (this->simultAsychIOCount>=maxSimultAsyncIO) {
            return S_casApp_postponeAsyncIO;
        }

        this->simultAsychIOCount++;

        exAsyncCreateIO *pIO = 
            new exAsyncCreateIO(pvi, *this, ctx, this->scanOn);
        if (pIO) {
            return S_casApp_asyncCompletion;
        }
        else {
            return S_casApp_noMemory;
        }
    }
}

//
// pvInfo::createPV()
//
exPV *pvInfo::createPV ( exServer & /*cas*/,
                         bool preCreateFlag, bool scanOn )
{
    if (this->pPV) {
        return this->pPV;
    }

    exPV *pNewPV;

    //
    // create an instance of the appropriate class
    // depending on the io type and the number
    // of elements
    //
    if (this->elementCount==1u) {
        switch (this->ioType){
        case excasIoSync:
            pNewPV = new exScalarPV ( *this, preCreateFlag, scanOn );
            break;
        case excasIoAsync:
            pNewPV = new exAsyncPV ( *this, preCreateFlag, scanOn );
            break;
        default:
            pNewPV = NULL;
            break;
        }
    }
    else {
        if ( this->ioType == excasIoSync ) {
            pNewPV = new exVectorPV ( *this, preCreateFlag, scanOn );
        }
        else {
            pNewPV = NULL;
        }
    }
    
    //
    // load initial value (this is not done in
    // the constructor because the base class's
    // pure virtual function would be called)
    //
    // We always perform this step even if
    // scanning is disable so that there will
    // always be an initial value
    //
    if (pNewPV) {
        this->pPV = pNewPV;
        pNewPV->scan();
    }

    return pNewPV;
}

//
// exServer::show() 
//
void exServer::show (unsigned level) const
{
    //
    // server tool specific show code goes here
    //
    this->stringResTbl.show(level);

    //
    // print information about ca server libarary
    // internals
    //
    this->caServer::show(level);
}

//
// exAsyncExistIO::exAsyncExistIO()
//
exAsyncExistIO::exAsyncExistIO ( const pvInfo &pviIn, const casCtx &ctxIn,
        exServer &casIn ) :
    casAsyncPVExistIO ( ctxIn ), pvi ( pviIn ), 
        timer ( casIn.createTimer () ), cas ( casIn ) 
{
    this->timer.start ( *this, 0.00001 );
}

//
// exAsyncExistIO::~exAsyncExistIO()
//
exAsyncExistIO::~exAsyncExistIO()
{
    this->cas.removeIO ();
    this->timer.destroy ();
}

//
// exAsyncExistIO::expire()
// (a virtual function that runs when the base timer expires)
//
epicsTimerNotify::expireStatus exAsyncExistIO::expire ( const epicsTime & /*currentTime*/ )
{
    //
    // post IO completion
    //
    this->postIOCompletion ( pvExistReturn(pverExistsHere) );
    return noRestart;
}


//
// exAsyncCreateIO::exAsyncCreateIO()
//
exAsyncCreateIO::exAsyncCreateIO ( pvInfo &pviIn, exServer &casIn, 
    const casCtx &ctxIn, bool scanOnIn ) :
    casAsyncPVAttachIO ( ctxIn ), pvi ( pviIn ), 
        timer ( casIn.createTimer () ), 
        cas ( casIn ), scanOn ( scanOnIn ) 
{
    this->timer.start ( *this, 0.00001 );
}

//
// exAsyncCreateIO::~exAsyncCreateIO()
//
exAsyncCreateIO::~exAsyncCreateIO()
{
    this->cas.removeIO ();
    this->timer.destroy ();
}

//
// exAsyncCreateIO::expire()
// (a virtual function that runs when the base timer expires)
//
epicsTimerNotify::expireStatus exAsyncCreateIO::expire ( const epicsTime & /*currentTime*/ )
{
    exPV *pPV;

    pPV = this->pvi.createPV ( this->cas, false, this->scanOn );
    if ( pPV ) {
        this->postIOCompletion ( pvAttachReturn ( *pPV ) );
    }
    else {
        this->postIOCompletion ( pvAttachReturn ( S_casApp_noMemory ) );
    }
    return noRestart;
}

