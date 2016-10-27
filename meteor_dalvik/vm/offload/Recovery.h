#ifndef OFFLOAD_RECOVERY_H
#define OFFLOAD_RECOVERY_H

struct Thread;

/* Wait until all threads are not in a vulnerable execution position.
 * Vulnerable here means that the thread may be acting on behalf of the remote
 * endpoint but that endpoint has been disconnected.  When a connection is lost
 * a clean-up of offload state will be done when all hazards have been cleared.
 * At that point (and only at that point) it is OK to connect to an offload
 * server again.
 *
 * If self is NULL no status changes will be done.
 */
void offRecoveryWaitForClearance(struct Thread* self);

bool offRecoveryCheckEnterHazard(struct Thread* self);

bool offRecoveryEnterHazard(struct Thread* self);

void offRecoveryClearHazard(struct Thread* self);

bool offRecoveryStartup();
void offRecoveryShutdown();

#endif
