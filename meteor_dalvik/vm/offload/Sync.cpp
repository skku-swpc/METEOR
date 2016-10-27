#include "offload/Sync.h"

#include "Dalvik.h"

void offInsertIntoLockList(Thread* self, Object* obj) {
  u4 i, j;
  Object* lst = obj;
  for(i = j = 0; i < sizeof(self->offLockList) / sizeof(Object*); i++) {
    Object* tmp = self->offLockList[i];
    if(tmp == NULL || tmp == obj ||
        !dvmIsValidObject(tmp) || !offCheckLockOwnership(tmp) ||
        (LW_SHAPE(tmp->lock) == LW_SHAPE_FAT &&
         LW_MONITOR(tmp->lock)->lastOwner != self)) {
      tmp = NULL;
    }
    
    if(lst) {
      self->offLockList[j++] = lst;
    }
    lst = tmp;
  }
  for(; j < sizeof(self->offLockList) / sizeof(Object*); j++) {
    self->offLockList[j] = NULL;
  }
}

/* This method will acquire mutual exclusion on a lock without giving the
 * appearance that any thread owns the lock.  There are a few times where we
 * want to be able to acquire a lock without making it look like we did so to
 * other threads. */
static void lockMonitorNoOwn(Thread* self, Object* obj) {
  useconds_t sleepDelay = 0;
  const useconds_t maxSleepDelay = 1 << 20;
  ThreadStatus oldStatus = (ThreadStatus)0;

  u4* thinp = &obj->lock;
  u4 thin = *thinp;
  if(LW_SHAPE(thin) == LW_SHAPE_THIN) {
    Monitor* mon = dvmCreateMonitor(obj);
    dvmLockMutex(&mon->lock);
    u4 nthin = (thin & (LW_HASH_STATE_MASK << LW_HASH_STATE_SHIFT)) |
               (u4)mon | LW_SHAPE_FAT;

    /* We need to fatten the lock. */
    assert(LW_LOCK_OWNER(thin) != self->threadId);
    for(;;) {
      thin = *thinp;
      if(LW_LOCK_OWNER(thin) == 0) {
        if(android_atomic_acquire_cas(thin, nthin, (int32_t*)thinp) == 0) {
          break;
        }
      } else if(LW_SHAPE(thin) != LW_SHAPE_THIN) {
        // TODO: There is probably some way we're suppose to delete mon.
        ALOGW("Leaked monitor by surprise monitor fatten");
        oldStatus = dvmChangeStatus(self, THREAD_MONITOR);
        dvmLockMutex(&LW_MONITOR(thin)->lock);
        dvmChangeStatus(self, oldStatus);
        break;
      } else {
        oldStatus = dvmChangeStatus(self, THREAD_MONITOR);
        if(sleepDelay == 0) {
          sched_yield();
          sleepDelay = 1000;
        } else {
          usleep(sleepDelay);
          if(sleepDelay < maxSleepDelay / 2) {
              sleepDelay *= 2;
          }
        }
        dvmChangeStatus(self, oldStatus);
      }
    }
  } else {
    oldStatus = dvmChangeStatus(self, THREAD_MONITOR);
    dvmLockMutex(&LW_MONITOR(thin)->lock);
    dvmChangeStatus(self, oldStatus);
  }
}

static void unlockMonitorNoOwn(Thread* self, Object* obj) {
  assert(LW_SHAPE(obj->lock) == LW_SHAPE_FAT);
  dvmUnlockMutex(&LW_MONITOR(obj->lock)->lock);
}

static void sendSyncMessage(Thread* self, u1 type, u4 objId) {
  u1 buf[5];
  buf[0] = type;
  objId = htonl(objId);
  memcpy(buf + 1, &objId, 4);
  offSendMessage(self, (char*)buf, sizeof(buf));
}

int offGetLocalWaiters(Object* obj) {
  Thread* thread;
  int result = 0;
  if(LW_SHAPE(obj->lock) != LW_SHAPE_THIN) {
    Monitor* mon = LW_MONITOR(obj->lock);
    for(thread = mon->waitSet; thread; thread = thread->waitNext) {
      result++;
    }
  }
  return result;
}

static int getRemoteWaiters(Object* obj) {
  if(obj->objId == COMM_INVALID_ID || gDvm.initializing) return 0;

  int result;
  pthread_mutex_lock(&gDvm.offCommLock); {
    ObjectInfo* info = offIdObjectInfo(auxObjectToId(obj));
    result = info->remoteWaitCount;
  } pthread_mutex_unlock(&gDvm.offCommLock);
  return result;
}

static void setRemoteWaiters(Object* obj, int waitCount) {
  pthread_mutex_lock(&gDvm.offCommLock); {
    ObjectInfo* info = offIdObjectInfo(auxObjectToId(obj));
    info->remoteWaitCount = waitCount;
  } pthread_mutex_unlock(&gDvm.offCommLock);
}

static bool noSync(Object* obj) {
  // We need to allow class prep to complete successfully.  Prior to classes
  // being initialized they can't be part of the offloading engine anyway.
  if(obj->clazz == gDvm.classJavaLangClass &&
     ((ClassObject*)obj)->status < CLASS_INITIALIZED) {
    return true;
  }
  return false;
}

bool offCheckLockOwnership(Object* obj) {
  assert(dvmThreadSelf()->status == THREAD_RUNNING);
  if(noSync(obj) || obj->objId == COMM_INVALID_ID ||
     gDvm.initializing) return true;

  bool result;
  pthread_mutex_lock(&gDvm.offCommLock); {
    ObjectInfo* info = offIdObjectInfo(auxObjectToId(obj));
    result = info ? info->isLockOwner : true;
  } pthread_mutex_unlock(&gDvm.offCommLock);
  return result;
}

static void setObjectOwnership(Object* obj, bool own) {
  pthread_mutex_lock(&gDvm.offCommLock); {
    ObjectInfo* info = offIdObjectInfo(auxObjectToId(obj));
    assert(info);
    assert(info->isLockOwner != own);
    info->isLockOwner = own;
  } pthread_mutex_unlock(&gDvm.offCommLock);
}

void offTakeOwnershipSuspend(Object* obj, u4 waiters) {
  setRemoteWaiters(obj, waiters);
  setObjectOwnership(obj, true);
  if(LW_SHAPE(obj->lock) == LW_SHAPE_FAT) {
    LW_MONITOR(obj->lock)->owner = NULL;
    LW_MONITOR(obj->lock)->lockCount = 0;
  }
}

typedef struct OwnArgs {
  Object* obj;
  int waiters;
} OwnArgs;

static void setOwnership(OwnArgs* args, FifoBuffer* fb) {
  UNUSED_PARAMETER(fb);
  offTakeOwnershipSuspend(args->obj, args->waiters);
}

/* The contract here is that this endpoint will have lock ownership of the
 * object until the next suspend.  Anytime you check for suspension you'll need
 * to recheck this function. */
void offEnsureLockOwnership(Thread* self, Object* obj) {
retry:
  assert(self->status == THREAD_RUNNING);
  if(noSync(obj) || auxObjectToId(obj) == COMM_INVALID_ID) return;
  if(!offRecoveryEnterHazard(self)) return;

  while(!offCheckLockOwnership(obj) && gDvm.offConnected) {
ALOGI("THREAD %d ASKING FOR OWNERSHIP FOR %d", self->threadId, obj->objId);

    sendSyncMessage(self, OFF_ACTION_LOCK, auxObjectToId(obj));
    offThreadWaitForResume(self);
    u1 result = offReadU1(self);

    if(!gDvm.offConnected) {
ALOGI("THREAD %d LOST SERVER", self->threadId);
      continue;
    }

ALOGI("THREAD %d REQUEST FOR OWNERSHIP FOR %d WAS %s", self->threadId,
      obj->objId, result ? "GRANTED" : "DENIED");
    if(result) {
      OwnArgs args;
      args.obj = obj;
      args.waiters = offReadU4(self);
      offSyncPullDo(NULL, NULL,
                    (void(*)(void*, FifoBuffer*))setOwnership, &args);
    }
  }
  offRecoveryClearHazard(self);
  if(!offCheckLockOwnership(obj)) goto retry;
}

static bool clearOwnership(Object* obj) {
  Thread* self = dvmThreadSelf();
  unlockMonitorNoOwn(self, obj);
  offWriteU1(self, true);
  offWriteU4(self, offGetLocalWaiters(obj));
  setObjectOwnership(obj, false);
  return true;
}

void offPerformLock(Thread* self) {
  u4 objId = offReadU4(self);
  if(!gDvm.offConnected) return;
  Object* obj = offIdToObject(objId);

  if(!obj) {
    /* We haven't heard of the object yet.  The caller will most likely try
     * again. */
    offWriteU1(self, OFF_ACTION_RESUME);
    offWriteU1(self, false);
    return;
  }

  lockMonitorNoOwn(self, obj);
  if(!offCheckLockOwnership(obj)) {
    unlockMonitorNoOwn(self, obj);
    offWriteU1(self, OFF_ACTION_RESUME);
    offWriteU1(self, false);
    return;
  }

  /* Send over ownership.  If we find after suspending the VM that we no
   * longer own the object the sync will be cancelled. */
  offWriteU1(self, OFF_ACTION_RESUME);
  offSyncPushDoIf((bool(*)(void*))clearOwnership, obj, NULL, NULL);
}

void offObjectNotify(Thread* self, Object* obj) {
  if(noSync(obj)) return;

  /* Make sure we *really* need to send this message. */
  int waitCount = getRemoteWaiters(obj);
  if(waitCount > 0) {
    setRemoteWaiters(obj, waitCount - 1);
    sendSyncMessage(self, OFF_ACTION_NOTIFY, auxObjectToId(obj));
  }
}

void offObjectNotifyAll(Thread* self, Object* obj) {
  if(noSync(obj)) return;

  int waitCount = getRemoteWaiters(obj);
  if(waitCount > 0) {
    setRemoteWaiters(obj, 0);
    sendSyncMessage(self, OFF_ACTION_BROADCAST, auxObjectToId(obj));
  }
}

// TODO: We could in principle change the protocol to only require one sync but
// it should be pretty uncommon for the first interrupt to fail let alone the
// caller not being able to handle the interrupt itself afterwards.
void offPerformInterrupt(Thread* self) {
  u4 tid = offReadU4(self);
  if(!offSyncPull()) return;

  Thread* thread = offIdToThread(tid);

  u1 result = 0;
  if(thread->offLocal) {
    dvmThreadInterruptLocal(thread);
    result = 1;
  }

  offWriteU1(self, OFF_ACTION_RESUME);
  offWriteU1(self, result);
}

void offGrabVolatiles(ObjectInfo* info, u4 fieldIndex) {
  if(info->isVolatileOwner) return;

ALOGI("grabbing volatiles for %s %u (%s)", info->obj->clazz->descriptor,
     fieldIndex, (info->obj->clazz == gDvm.classJavaLangClass ?
                  ((ClassObject*)info->obj)->descriptor : "[n/a]"));
  u4 revision;
  Thread* self = dvmThreadSelf();

  offWriteU1(self, OFF_ACTION_GRABVOL);
  offWriteU4(self, info->obj->objId);
  revision = offReadU4(self);
  if(!gDvm.offConnected) return;
  if(revision == 0xFFFFFFFFU) {
    /* We need to pull new data down. */
    offSyncPull();
    pthread_mutex_lock(&gDvm.offVolatileLock);
    info->isVolatileOwner = true;
    pthread_mutex_unlock(&gDvm.offVolatileLock);
  } else {
    /* Otherwise just wait until we're at the current revision. */
    if((int)(revision - gDvm.offRecvRevision) > 0) {
      ThreadStatus status = dvmChangeStatus(self, THREAD_WAIT);
      pthread_mutex_lock(&gDvm.offCommLock);
      while((int)(revision - gDvm.offRecvRevision) && gDvm.offConnected) {
        pthread_cond_wait(&gDvm.offPullCond, &gDvm.offCommLock);
      }
      pthread_mutex_unlock(&gDvm.offCommLock);
      dvmChangeStatus(self, status);
    }
  }
}

void offPerformGrabVolatiles(Thread* self) {
  u4 objId = offReadU4(self);
  if(!gDvm.offConnected) return;
  ObjectInfo* info = offIdObjectInfo(objId);
  if(!info || !info->isVolatileOwner) {
    offWriteU4(self, gDvm.offSendRevision);
    return;
  }

  pthread_mutex_lock(&gDvm.offVolatileLock);
  if(!info->isVolatileOwner) {
    offWriteU4(self, gDvm.offSendRevision);
    pthread_mutex_unlock(&gDvm.offVolatileLock);
    return;
  }
  info->isVolatileOwner = false;
  pthread_mutex_unlock(&gDvm.offVolatileLock);

  offWriteU4(self, 0xFFFFFFFFU);
  offSyncPush();
}

void offPerformNotify(Thread* self) {
  u4 objId = offReadU4(self);
  if(!gDvm.offConnected) return;
  Object* obj = offIdToObject(objId);

  /* It's possible we haven't heard of this object yet.  In that case then
   * obviously nobody is waiting on it so we're done.  Also if the lock is thin
   * then nobody could be waiting locally either. */
  if(!obj || LW_SHAPE(obj->lock) == LW_SHAPE_THIN) return;

  /* Signal the first thread (if any) that is waiting on the object.  We're
   * abusing the semantics here a bit because we're not actually locking the
   * object.  Fortunately this doesn't appear to be necessary.  We circumvent
   * the monitor waitList field but that's ok because it has to be robust to
   * spurious wakeups anyway.  */
  Thread* thread;
  Monitor* mon = LW_MONITOR(obj->lock);
  for(thread = gDvm.threadList; thread; thread = thread->next) {
    if(thread->waitMonitor == mon) {
      dvmLockMutex(&thread->waitMutex);
      if(thread->waitMonitor == mon) {
        pthread_cond_signal(&thread->waitCond);
        dvmUnlockMutex(&thread->waitMutex);
        return;
      }
      dvmUnlockMutex(&thread->waitMutex);
    }
  }
}

/* Pretty much the same as notify. */
void offPerformNotifyAll(Thread* self) {
  u4 objId = offReadU4(self);
  if(!gDvm.offConnected) return;
  Object* obj = offIdToObject(objId);
  if(!obj || LW_SHAPE(obj->lock) == LW_SHAPE_THIN) return;

  Thread* thread;
  Monitor* mon = LW_MONITOR(obj->lock);
  for(thread = gDvm.threadList; thread; thread = thread->next) {
    if(thread->waitMonitor == mon) {
      dvmLockMutex(&thread->waitMutex);
      if(thread->waitMonitor == mon) {
        pthread_cond_signal(&thread->waitCond);
      }
      dvmUnlockMutex(&thread->waitMutex);
    }
  }
}

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

static void promoteMonitor(Object* obj, int count) {
  assert(auxObjectToId(obj) != COMM_INVALID_ID);
  assert(LW_SHAPE(obj->lock) != LW_SHAPE_THIN);
  assert(LW_MONITOR(obj->lock)->lockCount == count);
  assert(LW_MONITOR(obj->lock)->owner == dvmThreadSelf());
  dvmLockMutex(&LW_MONITOR(obj->lock)->lock);
  setObjectOwnership(obj, true);
ALOGI("THREAD %d: RECEIVE OWNERSHIP OF %d", dvmThreadSelf()->threadId, obj->objId);
}

static void reduceMonitor(Object* obj, int count) {
  assert(auxObjectToId(obj) != COMM_INVALID_ID);
  assert(LW_SHAPE(obj->lock) != LW_SHAPE_THIN);
  assert(LW_MONITOR(obj->lock)->lockCount == count);
  assert(LW_MONITOR(obj->lock)->owner == dvmThreadSelf());
  setObjectOwnership(obj, false);
ALOGI("THREAD %d: SEND OWNERSHIP OF %d", dvmThreadSelf()->threadId, obj->objId);
  dvmUnlockMutex(&LW_MONITOR(obj->lock)->lock);
}

void offReduceMonitors(Thread* self) {
  LockedObjectData* lod;
  for(lod = self->pLockedObjects; lod; lod = lod->next) {
    reduceMonitor(lod->obj, lod->recursionCount);
  }
}

void offPromoteMonitors(Thread* self) {
  LockedObjectData* lod;
  for(lod = self->pLockedObjects; lod; lod = lod->next) {
    promoteMonitor(lod->obj, lod->recursionCount);
  }
}

void offTransmitMonitors(Thread* thread, FifoBuffer* fb) {
  LockedObjectData* lod;
  for(lod = thread->pLockedObjects; lod; lod = lod->next) {
    /* We need to fatten all transmitted locks. */
    if(LW_SHAPE(lod->obj->lock) == LW_SHAPE_THIN) {
      u4 thin = lod->obj->lock;

      Monitor* mon = dvmCreateMonitor(lod->obj);
      dvmLockMutex(&mon->lock);

      thin &= LW_HASH_STATE_MASK << LW_HASH_STATE_SHIFT;
      thin |= (u4)mon | LW_SHAPE_FAT;
      lod->obj->lock = thin;

      mon->owner = thread;
      mon->lockCount = lod->recursionCount;
    }
    if(LW_MONITOR(lod->obj->lock)->owner != thread) {
      /* Threads retain monitors in their locked object lists even when they
       * release the object while waiting.  Since such information isn't visible
       * to other threads anyway there is no purpose in sending this lock. */
      continue;
    }
    assert(LW_MONITOR(lod->obj->lock)->lockCount == lod->recursionCount);

    offAddTrackedObject(lod->obj);
    writeU4(fb, auxObjectToId(lod->obj));
    writeU4(fb, lod->recursionCount);
    writeU4(fb, offGetLocalWaiters(lod->obj));
  }
  writeU4(fb, (u4)-1);
}

void offReceiveMonitors(Thread* thread, FifoBuffer* fb) {
  /* First we need to clear ownership of feigned locks (if the ownership wasn't
   * clobbered already by receive monitors for another thread). */
  LockedObjectData* lod;
  for(lod = thread->pLockedObjects; lod; lod = lod->next) {
    assert(LW_SHAPE(lod->obj->lock) == LW_SHAPE_FAT);
    Monitor* mon = LW_MONITOR(lod->obj->lock);
    if(mon->owner == thread) {
      mon->owner = NULL;
      mon->lockCount = 0;
    }
  }

  lod = thread->pLockedObjects;
  thread->pLockedObjects = NULL;

  u4 objId;
  for(objId = readU4(fb); objId != (u4)-1; objId = readU4(fb)) {
    if(!lod) lod = (LockedObjectData*)calloc(1, sizeof(LockedObjectData));
    if(!lod) {
      ALOGE("malloc failed on locked object data");
      dvmAbort();
    }

    lod->obj = offIdToObject(objId);
    lod->recursionCount = readU4(fb);
    setRemoteWaiters(lod->obj, (int)readU4(fb));

    if(LW_SHAPE(lod->obj->lock) == LW_SHAPE_THIN) {
      assert(LW_LOCK_OWNER(lod->obj->lock) == 0);
      u4 thin = lod->obj->lock;

      Monitor* mon = dvmCreateMonitor(lod->obj);
      thin &= LW_HASH_STATE_MASK << LW_HASH_STATE_SHIFT;
      thin |= (u4)mon | LW_SHAPE_FAT;
      lod->obj->lock = thin;
    } 
    LW_MONITOR(lod->obj->lock)->owner = thread;
    LW_MONITOR(lod->obj->lock)->lockCount = lod->recursionCount;

    LockedObjectData* nxt = lod->next;
    lod->next = thread->pLockedObjects;
    thread->pLockedObjects = lod;
    lod = nxt;
  }

  /* Delete all the extra records we didn't need. */
  while(lod != NULL) {
    LockedObjectData* nxt = lod->next;
    free(lod);
    lod = nxt;
  }
}

bool offSyncStartup() {
  pthread_mutex_init(&gDvm.offVolatileLock, NULL);
  return true;
}

void offSyncShutdown() {
}
