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
 *      Author  W. Eric Norum
 *              norume@aps.anl.gov
 *              630 252 4793
 */

#define epicsExportSharedSymbols
#include "epicsMessageQueue.h"
#include <ellLib.h>
#include <epicsAssert.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <stdexcept>
# include <string.h>

/*
 * Event cache
 */
struct eventNode {
    ELLNODE         link;
    epicsEventId    event;
};

/*
 * List of threads waiting to send or receive a message
 */
struct threadNode {
    ELLNODE             link;
    struct eventNode   *evp;
    void               *buf;
    unsigned int        size;
    volatile bool       eventSent;
};

/*
 * Message info
 */
struct epicsMessageQueueOSD {
    ELLLIST         sendQueue;
    ELLLIST         receiveQueue;
    ELLLIST         eventFreeList;
    int             numberOfSendersWaiting;
    
    epicsMutexId    mutex;
    unsigned long   capacity;
    unsigned long   maxMessageSize;

    unsigned long  *buf;
    char           *firstMessageSlot;
    char           *lastMessageSlot;
    volatile char  *inPtr;
    volatile char  *outPtr;
    unsigned long   slotSize;

    bool            full;
};

epicsShareFunc epicsMessageQueueId epicsShareAPI epicsMessageQueueCreate(
    unsigned int capacity,
    unsigned int maxMessageSize)
{
    epicsMessageQueueId pmsg;
    unsigned int slotBytes, slotLongs;

    pmsg = (epicsMessageQueueId)callocMustSucceed(1, sizeof(*pmsg), "epicsMessageQueueCreate");
    pmsg->capacity = capacity;
    pmsg->maxMessageSize = maxMessageSize;
    slotLongs = 1 + ((maxMessageSize + sizeof(unsigned long) - 1) / sizeof(unsigned long));
    slotBytes = slotLongs * sizeof(unsigned long);
    if (pmsg->capacity == 0) {
        pmsg->buf = NULL;
        pmsg->inPtr = pmsg->outPtr = pmsg->firstMessageSlot = NULL;
        pmsg->lastMessageSlot = NULL;
        pmsg->full = true;
    }
    else {
        pmsg->buf = (unsigned long *)callocMustSucceed(pmsg->capacity, slotBytes, "epicsMessageQueueCreate");
        pmsg->inPtr = pmsg->outPtr = pmsg->firstMessageSlot = (char *)&pmsg->buf[0];
        pmsg->lastMessageSlot = (char *)&pmsg->buf[(capacity - 1) * slotLongs];
        pmsg->full = false;
    }
    pmsg->slotSize = slotBytes;
    pmsg->mutex = epicsMutexMustCreate();
    ellInit(&pmsg->sendQueue);
    ellInit(&pmsg->receiveQueue);
    ellInit(&pmsg->eventFreeList);
    return pmsg;
}

epicsShareFunc void epicsShareAPI
epicsMessageQueueDestroy(epicsMessageQueueId pmsg)
{
    struct eventNode *evp;

    while ((evp = (struct eventNode *)ellGet(&pmsg->eventFreeList)) != NULL) {
        epicsEventDestroy(evp->event);
        free(evp);
    }
    epicsMutexDestroy(pmsg->mutex);
    free(pmsg->buf);
    free(pmsg);
}

static struct eventNode *
getEventNode(epicsMessageQueueId pmsg)
{
    struct eventNode *evp;

    evp = (struct eventNode *)ellGet(&pmsg->eventFreeList);
    if (evp == NULL) {
        evp = (struct eventNode *)callocMustSucceed(1, sizeof(*evp), "epicsMessageQueueGetEventNode");
        evp->event = epicsEventMustCreate(epicsEventEmpty);
    }
    return evp;
}

static int
mySend(epicsMessageQueueId pmsg, void *message, unsigned int size, bool wait, bool haveTimeout, double timeout)
{
    char *myInPtr, *nextPtr;
    struct threadNode *pthr;

    if(size > pmsg->maxMessageSize)
        return -1;

    /*
     * See if message can be sent
     */
    epicsMutexLock(pmsg->mutex);
    if ((pmsg->numberOfSendersWaiting > 0)
     || (pmsg->full && (ellFirst(&pmsg->receiveQueue) == NULL))) {
        /*
         * Return if not allowed to wait
         */
        if (!wait) {
            epicsMutexUnlock(pmsg->mutex);
            return -1;
        }

        /*
         * Wait
         */
        struct threadNode threadNode;
        threadNode.evp = getEventNode(pmsg);
        threadNode.eventSent = false;
        ellAdd(&pmsg->sendQueue, &threadNode.link);
        pmsg->numberOfSendersWaiting++;
        epicsMutexUnlock(pmsg->mutex);
        if(haveTimeout)
            epicsEventWaitWithTimeout(threadNode.evp->event, timeout);
        else
            epicsEventWait(threadNode.evp->event);
        epicsMutexLock(pmsg->mutex);
        if(!threadNode.eventSent)
            ellDelete(&pmsg->sendQueue, &threadNode.link);
        pmsg->numberOfSendersWaiting--;
        ellAdd(&pmsg->eventFreeList, &threadNode.evp->link);
        if (pmsg->full && (ellFirst(&pmsg->receiveQueue) == NULL)) {
            epicsMutexUnlock(pmsg->mutex);
            return -1;
        }
    }

    /*
     * Copy message to waiting receiver
     */
    if ((pthr = (struct threadNode *)ellGet(&pmsg->receiveQueue)) != NULL) {
        memcpy(pthr->buf, message, size);
        pthr->size = size;
        pthr->eventSent = true;
        epicsEventSignal(pthr->evp->event);
        epicsMutexUnlock(pmsg->mutex);
        return 0;
    }

    /*
     * Copy to queue
     */
    myInPtr = (char *)pmsg->inPtr;
    if (myInPtr == pmsg->lastMessageSlot)
        nextPtr = pmsg->firstMessageSlot;
    else
        nextPtr = myInPtr + pmsg->slotSize;
    if (nextPtr == (char *)pmsg->outPtr)
        pmsg->full = true;
    *(volatile unsigned long *)myInPtr = size;
    memcpy((unsigned long *)myInPtr + 1, message, size);
    pmsg->inPtr = nextPtr;
    epicsMutexUnlock(pmsg->mutex);
    return 0;
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueTrySend(epicsMessageQueueId pmsg, void *message, unsigned int size)
{
    return mySend(pmsg, message, size, false, false, 0.0);
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueSend(epicsMessageQueueId pmsg, void *message, unsigned int size)
{
    return mySend(pmsg, message, size, true, false, 0.0);
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueSendWithTimeout(epicsMessageQueueId pmsg, void *message, unsigned int size, double timeout)
{
    return mySend(pmsg, message, size, true, true, timeout);
}

static int
myReceive(epicsMessageQueueId pmsg, void *message, bool wait, bool haveTimeout, double timeout)
{
    char *myOutPtr;
    unsigned long l;
    struct threadNode *pthr;

    /*
     * If there's a message on the queue, copy it
     */
    epicsMutexLock(pmsg->mutex);
    myOutPtr = (char *)pmsg->outPtr;
    if ((pmsg->capacity != 0) && ((myOutPtr != pmsg->inPtr) || pmsg->full)) {
        l = *(unsigned long *)myOutPtr;
        memcpy(message, (unsigned long *)myOutPtr + 1, l);
        if (myOutPtr == pmsg->lastMessageSlot)
            pmsg->outPtr = pmsg->firstMessageSlot;
        else
            pmsg->outPtr += pmsg->slotSize;
        if (pmsg->capacity)
            pmsg->full = false;

        /*
         * Wake up the oldest task waiting to send
         */
        if ((pthr = (struct threadNode *)ellGet(&pmsg->sendQueue)) != NULL) {
            pthr->eventSent = true;
            epicsEventSignal(pthr->evp->event);
        }
        epicsMutexUnlock(pmsg->mutex);
        return l;
    }

    /*
     * Return if not allowed to wait
     */
    if (!wait) {
        epicsMutexUnlock(pmsg->mutex);
        return -1;
    }

    /*
     * Wake up the oldest task waiting to send
     */
    if ((pthr = (struct threadNode *)ellGet(&pmsg->sendQueue)) != NULL) {
        pthr->eventSent = true;
        epicsEventSignal(pthr->evp->event);
    }

    /*
     * Wait for message to arrive
     */
    struct threadNode threadNode;
    threadNode.evp = getEventNode(pmsg);
    threadNode.buf = message;
    threadNode.eventSent = false;
    ellAdd(&pmsg->receiveQueue, &threadNode.link);
    epicsMutexUnlock(pmsg->mutex);
    if(haveTimeout)
        epicsEventWaitWithTimeout(threadNode.evp->event, timeout);
    else
        epicsEventWait(threadNode.evp->event);
    epicsMutexLock(pmsg->mutex);
    if(!threadNode.eventSent)
        ellDelete(&pmsg->receiveQueue, &threadNode.link);
    ellAdd(&pmsg->eventFreeList, &threadNode.evp->link);
    epicsMutexUnlock(pmsg->mutex);
    if(threadNode.eventSent)
        return threadNode.size;
    return -1;
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueTryReceive(epicsMessageQueueId pmsg, void *message)
{
    return myReceive(pmsg, message, false, false, 0.0);
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueReceive(epicsMessageQueueId pmsg, void *message)
{
    return myReceive(pmsg, message, true, false, 0.0);
}

epicsShareFunc int epicsShareAPI
epicsMessageQueueReceiveWithTimeout(epicsMessageQueueId pmsg, void *message, double timeout)
{
    return myReceive(pmsg, message, true, true, timeout);
}

epicsShareFunc int epicsShareAPI
epicsMessageQueuePending(epicsMessageQueueId pmsg)
{
    char *myInPtr, *myOutPtr;
    int nmsg;

    epicsMutexLock(pmsg->mutex);
    myInPtr = (char *)pmsg->inPtr;
    myOutPtr = (char *)pmsg->outPtr;
    if (pmsg->full)
        nmsg = pmsg->capacity;
    else if (myInPtr >= myOutPtr)
        nmsg = (myInPtr - myOutPtr) / pmsg->slotSize;
    else
        nmsg = pmsg->capacity  - (myOutPtr - myInPtr) / pmsg->slotSize;
    epicsMutexUnlock(pmsg->mutex);
    return nmsg;
}

epicsShareFunc void epicsShareAPI
epicsMessageQueueShow(epicsMessageQueueId pmsg, int level)
{
    printf("Message Queue Used:%d  Slots:%lu", epicsMessageQueuePending(pmsg), pmsg->capacity);
    if (level >= 1)
        printf("  Maximum size:%lu", pmsg->maxMessageSize);
    printf("\n");
}