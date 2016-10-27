#include "offload/Engine.h"

#include "Dalvik.h"
#include "interp/InterpDefs.h"
#include "offload/Control.h"

#include <sys/types.h>
#include <sys/wait.h>

#define READWRITEFUNC(type, size, ntoh, hton)                                 \
  static void write##size(FifoBuffer* fb, type v) {                           \
    v = hton(v);                                                              \
    auxFifoPushData(fb, (char*)&v, sizeof(v));                                \
  }                                                                           \
  static type read##size(FifoBuffer* fb) {                                    \
    type v;                                                                   \
    auxFifoReadBuffer(fb, (char*)&v, sizeof(v));                              \
    return ntoh(v);                                                           \
  }
READWRITEFUNC(u4, U4, ntohl, htonl);

void offJumpIntoInterp(Thread* self) {
  InterpSaveState* sst = &self->interpSave;

#if defined(WITH_TRACKREF_CHECKS)
  sst->debugTrackedRefStart =
      dvmReferenceTableEntries(&self->internalLocalRefTable);
#endif
  self->debugIsMethodEntry = false;

  const StackSaveArea* saveArea = SAVEAREA_FROM_FP(sst->curFrame);
  sst->method = saveArea->method;
  sst->pc = saveArea->xtra.currentPc;

  if(sst->method->clazz->status < CLASS_INITIALIZING ||
     sst->method->clazz->status == CLASS_ERROR) {
    ALOGE("ERROR: tried to execute code in unprepared class '%s' (%d)\n",
         sst->method->clazz->descriptor, sst->method->clazz->status);
    dvmDumpThread(self, false);
    dvmAbort();
  }

ALOGI("Enter interp %s %s %d", saveArea->method->clazz->descriptor,
saveArea->method->name, saveArea->xtra.currentPc - saveArea->method->insns);

  dvmInterpretPortable(self);

saveArea = SAVEAREA_FROM_FP(sst->curFrame);
ALOGI("Leave interp %s %s %d %d", saveArea->method->clazz->descriptor,
saveArea->method->name, saveArea->xtra.currentPc - saveArea->method->insns,
dvmCheckException(self));
}

static void doActivate(Thread* self, FifoBuffer* fb) {
  offPromoteMonitors(self);
  self->offLocal = true;

  u4 objId;
  for(objId = readU4(fb); objId != (u4)-1; objId = readU4(fb)) {
ALOGI("THREAD %d: RECEIVE OWNERSHIP OF %d", self->threadId, objId);
    ObjectInfo* info = offIdObjectInfo(objId);
    assert(info && info->obj && !info->isLockOwner);
    offTakeOwnershipSuspend(info->obj, readU4(fb));
    offInsertIntoLockList(self, info->obj);
  }
}

static bool activate(Thread* self) {
  bool res = offSyncPullDo(NULL, NULL,
                           (void(*)(void*, FifoBuffer*))doActivate, self);
  offSchedulerUnsafePoint(self);
  return res;
}

static void doDeactivate(Thread* self, FifoBuffer* fb) {
  offReduceMonitors(self);
  self->offLocal = false;

  u4 i;
  for (i = sizeof(self->offLockList) / sizeof(Object*); i--; ) {
    Object* obj = self->offLockList[i];
    if(!obj || !dvmIsValidObject(obj) || obj->objId == COMM_INVALID_ID ||
       dvmGetObjectLockHolder(obj) ||
       (LW_SHAPE(obj->lock) == LW_SHAPE_FAT &&
        LW_MONITOR(obj->lock)->lastOwner != self)) {
      continue;
    }
    u4 objId = auxObjectToId(obj);
    ObjectInfo* info = offIdObjectInfo(objId);
    assert(info && info->obj == obj);
    if(!info->isLockOwner) continue;

ALOGI("THREAD %d: SEND OWNERSHIP OF %d", self->threadId, objId);
    writeU4(fb, objId);
    writeU4(fb, offGetLocalWaiters(info->obj));
    info->isLockOwner = false;
  }
  writeU4(fb, (u4)-1);
}

static void deactivate(Thread* self) {
  offSyncPushDoIf(NULL, NULL, (void(*)(void*, FifoBuffer*))doDeactivate, self);
}

void offMigrateThread(Thread* self) {
  if(gDvm.offDisabled) return;
  if(!gDvm.isServer && !offWellConnected()) return;
  if(self->offProtection) return;
  if(!offRecoveryCheckEnterHazard(self)) return;

  ALOGI("Migrating thread %d", self->threadId);

  self->offLocalOnly = false;
  self->migrationCounter++;
  self->offDeactivateBreakFrames = self->breakFrames;
  offWriteU1(self, OFF_ACTION_MIGRATE);
  deactivate(self);
  offThreadWaitForResume(self);

  if(!self->offLocal && !self->offFlagDeath) {
    /* We lost the server.  Move back to running locally. */
    offPromoteMonitors(self);
    self->offLocal = true;
  }

  offRecoveryClearHazard(self);
  ALOGI("Excep %d", dvmCheckException(self));
}

bool offPerformMigrate(Thread* self) {
  u4 originalBreaks = self->offDeactivateBreakFrames;
  if(!activate(self)) return false;

  while(self->breakFrames > originalBreaks) {
    u4 newBreaks = self->breakFrames;
    offJumpIntoInterp(self);
    assert(self->breakFrames <= newBreaks);
    if(self->offFlagDeath) {
      return true;
    } else if(self->breakFrames == newBreaks) {
      self->offDeactivateBreakFrames = originalBreaks;
      offWriteU1(self, OFF_ACTION_MIGRATE);
      deactivate(self);
      return false;
    }
  }

  ALOGI("COLLAPSING DOWN %d %d", self->breakFrames, originalBreaks);
  return true;
}

void offMigrateClinit(Thread* self, ClassObject* clazz) {
  assert(gDvm.isServer);

  self->offLocalOnly = false;
  offWriteU1(self, OFF_ACTION_CLINIT);
  offWriteU4(self, auxObjectToId(clazz));
  deactivate(self);
  offThreadWaitForResume(self);
  if(!activate(self)) {
    ALOGE("LOST CONNECTION TO CLIENT ABORTING");
    dvmAbort();
  }
}

void offPerformClinit(Thread* self) {
  assert(!gDvm.isServer);

  u4 objId = offReadU4(self);
  if(!activate(self)) return;

  ClassObject* clazz = (ClassObject*)offIdToObject(objId);
  dvmInitClass(clazz);
  offWriteU1(self, OFF_ACTION_RESUME);
  deactivate(self);
}

bool offEngineStartup() {
  if(!gDvm.isServer && !getenv("ENABLE_OFFLOAD")) {
    pid_t pid = fork();
    if(pid == 0) {
      execl("/system/bin/cometmanager", "cometmanager", "check", (char*)NULL);
      ALOGW("exec fail %s", strerror(errno));
      exit(127);
    }

    int status = 1;
    while(1) {
      int res = waitpid(pid, &status, 0);
      if(res == -1) break;
      if(WIFEXITED(status)) break;
    }
    if(!WIFSIGNALED(status) && WEXITSTATUS(status) != 1) {
      ALOGI("Offloading enabled");
      gDvm.offDisabled = false;
    } else {
      ALOGI("Offloading disabled");
      gDvm.offDisabled = true;
    }
  } else {
    ALOGI("Offloading enabled");
    gDvm.offDisabled = false;
  }

  return true;
}

void offEngineShutdown() {
}
