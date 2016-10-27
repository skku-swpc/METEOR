#ifndef OFFLOAD_ENGINE_H
#define OFFLOAD_ENGINE_H

#include "Common.h"

struct Thread;
struct ClassObject;

void offJumpIntoInterp(struct Thread* self);

void offMigrateThread(struct Thread* self);

/* Returns true if we should back up to resume interpreting. */
bool offPerformMigrate(struct Thread* self);

void offMigrateClinit(struct Thread* self, struct ClassObject* clazz);
void offPerformClinit(struct Thread* self);

bool offEngineStartup();
void offEngineShutdown();

#endif // OFFLOAD_ENGINE_H
