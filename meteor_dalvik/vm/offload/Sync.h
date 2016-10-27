#ifndef OFFLOAD_SYNC_H
#define OFFLOAD_SYNC_H

#include "Common.h"
#include "auxiliary/Vector.h"

struct Thread;
struct Object;
struct ObjectInfo;
struct FifoBuffer;

void offEnsureLockOwnership(struct Thread* self, struct Object* obj);
bool offCheckLockOwnership(struct Object* obj);

void offInsertIntoLockList(struct Thread* self, struct Object* obj);

/* Notify a remote endpoint that has one thread waiting on obj.  This doesn't
 * need to be synchronous.
 */
void offObjectNotify(struct Thread* self, struct Object* obj);

/* Notify all remote endpoints that have threads waiting on obj.  This doesn't
 * need to be synchronous.
 */
void offObjectNotifyAll(struct Thread* self, struct Object* obj);

void offPerformLock(struct Thread* self);

void offPerformNotify(struct Thread* self);

void offPerformNotifyAll(struct Thread* self);

/* Notify all endpoints that a thread has had its interrupted bit set or reset.
 */
void offPerformInterrupt(struct Thread* self);

/* Grab volatile ownership if needed and do necessarily heap sync. */
void offGrabVolatiles(struct ObjectInfo* info, u4 fieldIndex);

void offPerformGrabVolatiles(struct Thread* self);

/* To allow monitors to work well with the offload engine and existing codebase
 * every monitor can be in one of four statuses
 *
 * Owned-Free:      Any local thread can freely lock.
 * Owned-Locked:    A thread is actively holding the lock locally.
 * Unowned:         The lock is owned by the other endpoint.
 * Unowned-Feigned: A local thread is pretending to hold the lock.
 *
 * The basic semantics are that Owned-Free and Unowned locks do not appear to be
 * held by a thread when queried while Owned-Locked and Unowned-Feigned will
 * appear to be held by a thread.  Unowned-Feigned ought to imply that during
 * the last syncPull the parallel thread had the lock in Owned-Locked.  It
 * should be invariant that all monitors held by a thread are either
 * Owned-Locked or Unowned-Feigned and the former case should happen iff
 * the thread->offLocal flag is set.
 *
 * When a thread's execution becomes local it ought to transition all
 * Unowned-Feigned locks into Owned-Locked.  This should happen for an explicit
 * migrate or if the server is lost.
 */

/* Reduce all Owned-Locked monitors held by the calling thread to the
 * Unowned-Feigned state.
 */
void offReduceMonitors(struct Thread* self);

/* Promite all Unowned-Feigned monitors held by the calling thread to the
 * Owned-Locked state.
 */
void offPromoteMonitors(struct Thread* self);

/* Serialize all Owned-Locked monitors held by the calling thread.  This may
 * only send a delta of the monitor state and should be complimented with a call
 * to offReceiveMonitors().
 */
void offTransmitMonitors(struct Thread* thread, struct FifoBuffer* fb);
void offReceiveMonitors(struct Thread* thread, struct FifoBuffer* fb);

int offGetLocalWaiters(struct Object* obj);
void offTakeOwnershipSuspend(struct Object* obj, u4 waiters);

bool offSyncStartup();
void offSyncShutdown();

#endif // OFFLOAD_SYNC_H
