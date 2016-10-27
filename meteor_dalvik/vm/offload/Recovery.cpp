#include "Dalvik.h"

static void cleanupOffloadState(Thread* self);

// TODO: Right now there isn't anything that forces an offload cleanup in a
// timely manner.  Certain actions, like locking, might trigger it or it may be
// naturally triggered by what was going on during the connection loss.

void offRecoveryWaitForClearance(Thread* self) {
  if(gDvm.isServer) return;

  bool trigger = false;
  ThreadStatus oldStatus = (ThreadStatus)0;
  pthread_mutex_lock(&gDvm.offRecoveryLock); {
    if(!gDvm.offConnected && (self == NULL || gDvm.offRecoveryHazards != 0)) {
      if(self) oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);
      while(!gDvm.offConnected && !gDvm.offRecovered) {
        pthread_cond_wait(&gDvm.offRecoveryCond, &gDvm.offRecoveryLock);
      }
      if(self) oldStatus = dvmChangeStatus(self, oldStatus);
    } else if(!gDvm.offRecovered) {
      ++gDvm.offRecoveryHazards;
      trigger = true;
    }
  } pthread_mutex_unlock(&gDvm.offRecoveryLock);
  if(trigger) offRecoveryClearHazard(self);
}

bool offRecoveryCheckEnterHazard(Thread* self) {
  if(gDvm.isServer) return true;

  bool result = false;
  pthread_mutex_lock(&gDvm.offRecoveryLock);
  if(gDvm.offConnected) {
    ++gDvm.offRecoveryHazards;
    result = true;
  }
  pthread_mutex_unlock(&gDvm.offRecoveryLock);
  return result;
}
bool offRecoveryEnterHazard(Thread* self) {
  if(gDvm.isServer) return true;

  pthread_mutex_lock(&gDvm.offRecoveryLock);
  if(gDvm.offConnected) {
    ++gDvm.offRecoveryHazards;
    pthread_mutex_unlock(&gDvm.offRecoveryLock);
    return true;
  }
  pthread_mutex_unlock(&gDvm.offRecoveryLock);
  offRecoveryWaitForClearance(self);
  return false;
}

void offRecoveryClearHazard(Thread* self) {
  if(gDvm.isServer) return;

  pthread_mutex_lock(&gDvm.offRecoveryLock); {
    --gDvm.offRecoveryHazards;
    if(!gDvm.offConnected && 0 == gDvm.offRecoveryHazards) {
      cleanupOffloadState(self);
      gDvm.offRecovered = true;
      pthread_cond_broadcast(&gDvm.offRecoveryCond);
    }
  } pthread_mutex_unlock(&gDvm.offRecoveryLock);
}

bool offRecoveryStartup() {
  gDvm.offRecovered = false;
  gDvm.offRecoveryHazards = 0;
  return pthread_mutex_init(&gDvm.offRecoveryLock, NULL) == 0 &&
         pthread_cond_init(&gDvm.offRecoveryCond, NULL) == 0;
}

void offRecoveryShutdown() {
  pthread_mutex_destroy(&gDvm.offRecoveryLock);
  pthread_cond_destroy(&gDvm.offRecoveryCond);
}

static int cleanupThread(Thread* thread, void* parg) {
  UNUSED_PARAMETER(parg);
  thread->offFlagMigration = false;
  thread->offLocalOnly = true;
  assert(thread->offLocal);
  thread->offSyncStackStop = NULL;

  auxFifoDestroy(&thread->offWriteBuffer);
  auxFifoDestroy(&thread->offReadBuffer);
  thread->offWriteBuffer = auxFifoCreate();
  thread->offReadBuffer = auxFifoCreate();
  return 0;
}

static int addStatusUpdate(ClassObject* clazz, void* arg) {
  UNUSED_PARAMETER(arg);
  if(clazz->status >= CLASS_INITIALIZING) {
    auxVectorPushL(&gDvm.offStatusUpdate, clazz);
  }

  /* Mark all of the globals as dirty again. */
  clazz->offInfo.dirty = 0xFFFFFFFFU;
  if(clazz->sfieldCount > 32) {
    u4 sz = (clazz->sfieldCount - 1) >> 5;
    memset(clazz->offInfo.bits, 0xFF, sz << 2);
  }
  clazz->offInfo.isQueued = false;
  clazz->offInfo.remoteWaitCount = 0;
  clazz->offInfo.isLockOwner = true;
  clazz->offInfo.isVolatileOwner = true;
  offAddToWriteQueueLocked(clazz);
  return 0;
}

typedef struct ProxyInfo {
  ClassObject* clazz;
  char* str;
  u4 isz;
  ClassObject** interfaces;
} ProxyInfo;

static u4 readU4(FifoBuffer* fb) {
  u4 val;
  auxFifoReadBuffer(fb, (char*)&val, 4);
  return ntohl(val);
}

ProxyInfo* popProxyInfo(FifoBuffer* fb) {
  ProxyInfo* pi = (ProxyInfo*)malloc(sizeof(ProxyInfo));
  if(!pi) {
    ALOGE("Couldn't allocate proxy info");
    dvmAbort();
  }

  u4 objId, ssz, i;
  objId = readU4(fb);
  ssz = readU4(fb);
  pi->clazz = (ClassObject*)offIdToObject(objId);
  assert(pi->clazz->clazz == gDvm.classJavaLangClass);
  pi->str = (char*)malloc(ssz + 1);
  auxFifoReadBuffer(fb, pi->str, ssz);
  pi->str[ssz] = 0;
  pi->isz = readU4(fb);
  pi->interfaces = (ClassObject**)malloc(pi->isz * sizeof(ClassObject*));
  for(i = 0; i < pi->isz; i++) {
    objId = readU4(fb);
    ClassObject* clazz = (ClassObject*)offIdToObject(objId);
    assert(clazz->clazz == gDvm.classJavaLangClass);
    pi->interfaces[i] = clazz;
  }

  return pi;
}

static void cleanupOffloadState(Thread* self) {
  u4 i, j;

  ALOGI("THREAD %d CLEANING UP OFFLOAD STATE", self->threadId);

  dvmSuspendAllThreads(SUSPEND_FOR_GC);
  gDvm.offRecoveryHazards = 0;
  gDvm.nextId = 0;
  auxVectorDestroy(&gDvm.offWriteQueue);
  gDvm.offWriteQueue = auxVectorCreate(10);
  gDvm.offRecvRevision = gDvm.offSendRevision = 0;

  /* Copy proxy info into a safe place to replay back into offload state. */
  Vector proxyInfos = auxVectorCreate(0);
  while(!auxFifoEmpty(&gDvm.offProxyFifoAll)) {
    auxVectorPushV(&proxyInfos, popProxyInfo(&gDvm.offProxyFifoAll));
  }
  while(!auxFifoEmpty(&gDvm.offProxyFifo)) {
    auxVectorPushV(&proxyInfos, popProxyInfo(&gDvm.offProxyFifo));
  }
  assert(auxFifoEmpty(&gDvm.offProxyFifoAll) &&
         auxFifoEmpty(&gDvm.offProxyFifo));

  /* Tear down the object tables, info structures, and object ids. */
  for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
    u4 sz = offTableSize(&gDvm.objTables[i]);
    for(j = 0; j < sz; ++j) {
      ObjectInfo* info = offTableGet(&gDvm.objTables[i], j);
      if(info && info->obj) {
        info->obj->objId = COMM_INVALID_ID;
        free(info->bits);
      }
    }
    offTableDestroy(&gDvm.objTables[i]);
  }
  memset(gDvm.objTables, 0, sizeof(gDvm.objTables));

  /* Setup the class status update array from scratch. */
  auxVectorDestroy(&gDvm.offStatusUpdate);
  gDvm.offStatusUpdate = auxVectorCreate(dvmGetNumLoadedClasses());
  dvmHashTableLock(gDvm.loadedClasses);
  dvmHashForeach(gDvm.loadedClasses, (HashForeachFunc)addStatusUpdate, NULL);
  dvmHashTableUnlock(gDvm.loadedClasses);
  
  /* Setup the dex push list from scratch. */
  auxVectorResize(&gDvm.dexPushList, 0);
  for(i = gDvm.dexBootstrapCount; i < auxVectorSize(&gDvm.dexList); i++) {
    auxVectorPush(&gDvm.dexPushList, auxVectorGet(&gDvm.dexList, i));
  }

  /* Clean out thread state. */
  dvmHashTableLock(gDvm.offThreadTable);
  dvmHashForeach(gDvm.offThreadTable, (HashForeachFunc)cleanupThread, NULL);
  dvmHashTableUnlock(gDvm.offThreadTable);

  /* Replay the proxy information back into our state. */
  for(i = 0; i < auxVectorSize(&proxyInfos); i++) {
    ProxyInfo* pi = (ProxyInfo*)auxVectorGet(&proxyInfos, i).v;
    offRegisterProxy(pi->clazz, pi->str, pi->interfaces, pi->isz);
  }

  dvmResumeAllThreads(SUSPEND_FOR_GC);
}

