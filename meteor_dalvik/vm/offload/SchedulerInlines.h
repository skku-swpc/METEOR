#include "Dalvik.h"

#define BASE_PROCESSING_DELAY 5000 // 5ms

INLINE u8 schedulerGetTime() {
  return dvmGetRelativeTimeUsec();
}

INLINE void offSchedulerSafePoint(Thread* self) {
  if(gDvm.offDisabled) return;
  if(++self->offTimeCounter & 0x3FF) return;
  if(!gDvm.isServer && self->offProtection == 0 && !self->offFlagMigration &&
     offWellConnected()) {
    u8 threshold;
    if(gDvm.offSyncTimeSamples > 10) {
      threshold = 1 * gDvm.offSyncTime;
    } else {
      threshold = 1 * gDvm.offSyncTime * gDvm.offSyncTimeSamples +
          1 * (gDvm.offNetRTT + gDvm.offNetRTTVar) *
              (10 - gDvm.offSyncTimeSamples);
      threshold /= 10;
    }
    u8 okTime = schedulerGetTime() - self->offUnsafeTime;
    self->offFlagMigration = threshold < okTime;
    if(self->offFlagMigration) {
      ALOGI("FLAGGING %d FOR MIGRATE WITH %lld %lld %lld",
            self->threadId, okTime, threshold, self->offUnsafeTime);
    }
  }
}

INLINE void offSchedulerUnsafePoint(Thread* self) {
  if(gDvm.offDisabled) return;
  self->offFlagMigration = false;
  self->offUnsafeTime = schedulerGetTime();
  self->offTimeCounter = 0;
}

/* This function doesn't really belong here... */
INLINE void offStackFramePopped(Thread* self) {
  InterpSaveState* sst = &self->interpSave;
  if(sst->curFrame == NULL || self->offSyncStackStop == NULL) {
    self->offSyncStackStop = NULL;
  } else {
    void* nfp = SAVEAREA_FROM_FP(sst->curFrame)->prevFrame;
    if(nfp == NULL) {
      self->offSyncStackStop = NULL;
    } else {
      self->offSyncStackStop = nfp > self->offSyncStackStop ?
          nfp : self->offSyncStackStop;
    }
  }
}

