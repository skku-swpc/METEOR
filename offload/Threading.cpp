#include "offload/Threading.h"

#include "Dalvik.h"
#include "alloc/Heap.h"
#include "alloc/HeapInternal.h"

static int compareThreadData(const void* A, const void* B) {
  u4 tida = ((Thread*)A)->threadId;
  u4 tidb = ((Thread*)B)->threadId;
  return tida == tidb ? 0 : (tida < tidb ? -1 : 1);
}

static void addThreadToHash(Thread* thread) {
  dvmHashTableLock(gDvm.offThreadTable); {
    dvmHashTableLookup(gDvm.offThreadTable, thread->threadId,
                       thread, compareThreadData, true);
  } dvmHashTableUnlock(gDvm.offThreadTable);
}

static Thread* lookupThreadInHash(u4 threadId) {
  Thread* result;
  dvmHashTableLock(gDvm.offThreadTable); {
    // This is a bit of a hack but whatever, it gets the job done without having
    // to allocate an entire Thread structure.
    result = (Thread*)dvmHashTableLookup(gDvm.offThreadTable, threadId,
                         ((char*)&threadId) - offsetof(Thread, threadId),
                         compareThreadData, false);
  } dvmHashTableUnlock(gDvm.offThreadTable);
  return result;
}

/* Runs the main thread daemmon loop looking for incoming messages from its
 * parallel thread on what action it should take. */
static void* thread_daemon(void* pself) {
  Thread* self = (Thread*)pself;
  while(1) {
    u1 event = offReadU1(self);
    if(!gDvm.offConnected) {
      ALOGI("THREAD %d LOST CONNECTION", self->threadId);
      return NULL;
    }
ALOGI("THREAD %d GOT EVENT %d", self->threadId, event);
    switch(event) {
      case OFF_ACTION_RESUME: {
        /* We got a resume message, drop back to our caller. */
        return NULL;
      } break;
      case OFF_ACTION_LOCK: {
        offPerformLock(self);
      } break;
      case OFF_ACTION_NOTIFY: {
        offPerformNotify(self);
      } break;
      case OFF_ACTION_BROADCAST: {
        offPerformNotifyAll(self);
      } break;
      case OFF_ACTION_DEX_QUERYDEX: {
        offPerformQueryDex(self);
      } break;
      case OFF_ACTION_SYNC: {
        offSyncPull();
        offWriteU1(self, 0);
      } break;
      case OFF_ACTION_INTERRUPT: {
        offPerformInterrupt(self);
      } break;
      case OFF_ACTION_TRIMGC: {
        dvmLockHeap();
        self->offTrimSignaled = true;
        if (gDvm.gcHeap->gcRunning) {
          dvmWaitForConcurrentGcToComplete();
        }
        dvmCollectGarbageInternal(GC_BEFORE_OOM);
        self->offTrimSignaled = false;
        dvmUnlockHeap();
      } break;
      case OFF_ACTION_GRABVOL: {
        offPerformGrabVolatiles(self);
      } break;
      case OFF_ACTION_MIGRATE: {
        if(offPerformMigrate(self)) {
          return NULL;
        }
      } break;
      case OFF_ACTION_CLINIT: {
        offPerformClinit(self);
      } break;
      case OFF_ACTION_DEATH: {
        self->offFlagDeath = true;
        return NULL;
      } break;
      default: {
        ALOGE("Unknown action %d sent to thread %d",
             event, self->threadId);
        dvmAbort();
      }
    }
  }
}

static void* thread_attach(void* args) {
  char nameBuf[32];
  u4 tid = (u4)args;
  sprintf(nameBuf, "off-%d", tid);

  /* Indicate the threadId to the attaching thread.  The real thread struct will
   * be overwritten in dvmAttachCurrentThread. */
  Thread pself;
  memset(&pself, 0, sizeof(pself));
  pself.threadId = tid;
  pthread_setspecific(gDvm.pthreadKeySelf, &pself);

  JavaVMAttachArgs jniArgs;
  jniArgs.version = JNI_VERSION_1_2;
  jniArgs.name = nameBuf;
//TODO: Fix this!  Need to encode as jobject.
  jniArgs.group = (jobject)dvmGetSystemThreadGroup();

  ALOGI("ATTACHING THREAD %d", tid);
  dvmAttachCurrentThread(&jniArgs, (tid & 1 ? true : false));

  Thread* self = dvmThreadSelf();
  self->offLocal = false;
  self->offLocalOnly = false;
  addThreadToHash(self);

  // Signal the thread that created us to continue on.
  pthread_mutex_lock(&gDvm.offThreadLock);
  pthread_cond_broadcast(&gDvm.offThreadCreateCond);
  pthread_mutex_unlock(&gDvm.offThreadLock);

  void* result = thread_daemon(self);

  ALOGI("DETACHING THREAD %d", tid);

  /* Detaching will cause a sync-pull... this probably doesn't work as intended
   * still if the server creates threads. */
  bool entered = offRecoveryEnterHazard(self);
  dvmDetachCurrentThread();
  if(entered) offRecoveryClearHazard(self);

  return result;
}

void offThreadCreatedLocal(Thread* thread) {
  if(gDvm.optimizing) return;
  addThreadToHash(thread);
}

Thread* offIdToThread(u4 threadId) {
  if(threadId == 0) return NULL;

  Thread* result = lookupThreadInHash(threadId);
  if(!result) {
    offCheckLockNotHeld(&gDvm.threadListLock);

    /* Thread doesn't exist, we need to create it. */
    pthread_mutex_lock(&gDvm.offThreadLock); {
      pthread_t pthr;
      if(!(result = lookupThreadInHash(threadId)) &&
         pthread_create(&pthr, NULL, thread_attach, (void*)threadId)) {
        ALOGW("Thread creation failed for tid %d", threadId);
        pthread_mutex_unlock(&gDvm.offThreadLock);
        return NULL;
      }

      /* Wait for the thread to start up and add itself to the hash. */
      if(!result) do {
        pthread_cond_wait(&gDvm.offThreadCreateCond, &gDvm.offThreadLock);
      } while(!(result = lookupThreadInHash(threadId)));
    } pthread_mutex_unlock(&gDvm.offThreadLock);
  }
  return result;
}

void* offThreadWaitForResume(Thread* self) {
  void* result = NULL;
  if(offRecoveryCheckEnterHazard(self)) {
    result = thread_daemon(self);
    offRecoveryClearHazard(self);
  }
  return result;
}

/* Basically we need to cleanup any state kept around for this thread and let
 * the other side know the thread is exiting if needed. */
void offThreadExited(Thread* self) {
  u4 threadId = self->threadId;

  /* Inform the other endpoint that we're exiting if started locally.  We can
   * avoid this if the other endpoint doesn't even know about this thread. */
  if((threadId & 0x8000) == (gDvm.isServer ? 0x8000 : 0)) {
    if(!self->offLocalOnly) {
      offWriteU1(self, OFF_ACTION_DEATH);
      offSyncPush();

      ALOGI("THREAD %d WAITING FOR SIGNOFF", threadId);
      offThreadWaitForResume(self);
      ALOGI("THREAD %d SIGNING OFF", threadId);
    }
  }

  /* Remove the thread from the thread table. */
  if(gDvm.offThreadTable) {
    dvmHashTableLock(gDvm.offThreadTable);
    dvmHashTableRemove(gDvm.offThreadTable, threadId, self);
    dvmHashTableUnlock(gDvm.offThreadTable);
  }

  offFlushStream(self);
  if((threadId & 0x8000) == (gDvm.isServer ? 0x8000 : 0)) {
    dvmClearBit(gDvm.threadIdMap, ((threadId & 0x7FFF) >> 1) - 1);
  } else {
    /* Note that it's OK that we removed the thread from the hash already.
     * At this point the thread has been removed from the creator's list but the
     * thread id hasn't been returned for re-use.  There is no more data coming
     * for this logical thread.  When we send this final resume any subsequent
     * messages with the same thread id correspond to a new thread. */
    ALOGI("THREAD %d SIGNALLING SIGNOFF", threadId);
    offWriteU1(self, OFF_ACTION_RESUME);
    offFlushStream(self);
  }
  ALOGI("THREAD %d LEAVING", threadId);
}

bool offThreadingStartup() {
  if(pthread_mutex_init(&gDvm.offThreadLock, NULL)) {
    ALOGE("Failed to create threading lock mutex");
    dvmAbort();
  }
  if(pthread_cond_init(&gDvm.offThreadCreateCond, NULL)) {
    ALOGE("Failed to create threading wait condition");
    dvmAbort();
  }
  gDvm.offThreadTable = dvmHashTableCreate(32, NULL);

  /* Setup the thread context used to communicate during a trim gc.  This
   * doesn't actually represent a real thread. */
  memset(&gDvm.gcThreadContext, 0, sizeof(Thread));
  gDvm.gcThreadContext.threadId = 0;
  gDvm.gcThreadContext.offLocal = true;
  gDvm.gcThreadContext.offWriteBuffer = auxFifoCreate();
  gDvm.gcThreadContext.offReadBuffer = auxFifoCreate();
  pthread_mutex_init(&gDvm.gcThreadContext.offBufferLock, NULL);
  pthread_cond_init(&gDvm.gcThreadContext.offBufferCond, NULL);

  return true;
}

void offThreadingShutdown() {
  dvmHashTableFree(gDvm.offThreadTable);
  gDvm.offThreadTable = NULL;
  pthread_cond_destroy(&gDvm.offThreadCreateCond);
  pthread_mutex_destroy(&gDvm.offThreadLock);
}
