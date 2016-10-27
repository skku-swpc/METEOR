#ifndef OFFLOAD_THREADING_H
#define OFFLOAD_THREADING_H

#include "Common.h"

struct Thread;

#define THREAD_ID_NONE 0xFFFFFFFFU

#define OFF_ACTION_RESUME        0
#define OFF_ACTION_LOCK          1
#define OFF_ACTION_NOTIFY        2
#define OFF_ACTION_BROADCAST     3
#define OFF_ACTION_DEX_QUERYDEX  4
#define OFF_ACTION_SYNC          5
#define OFF_ACTION_INTERRUPT     6
#define OFF_ACTION_TRIMGC        7
#define OFF_ACTION_GRABVOL       8
#define OFF_ACTION_MIGRATE       9
#define OFF_ACTION_CLINIT        10
#define OFF_ACTION_DEATH         11

/* This component maintains parallel threads between endpoints.  Each VM thread
 * should have no more than one corresponding system thread for the lifetime of
 * the VM thread.  This component manages a lot of the common messaging and
 * structure setup needed to accomplish this.
 */

/* Returns to the thread daemon until a RESUME message is sent to the calling
 * thread when the requested execution is completed on the other end.  There is
 * also a special case when MIGRATE will cause this function to return. */
void* offThreadWaitForResume(struct Thread* self);

/* Signal when a thread was created locally after the thread id was assigned. */
void offThreadCreatedLocal(struct Thread* self);

// Get (or create) the offloading thread with the passed id.
struct Thread* offIdToThread(u4 threadId);

// Remove the passed thread from the Offloader's view.  This should be called
// when a thread exits normally.
void offThreadExited(struct Thread* thread);

bool offThreadingStartup();
void offThreadingShutdown();

#endif // OFFLOAD_THREADING_H
