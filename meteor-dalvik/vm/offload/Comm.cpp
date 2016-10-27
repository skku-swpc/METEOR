#include "Dalvik.h"
#include "alloc/HeapInternal.h"

#include <sys/time.h>

#define TIMER_BEGIN()                                                       \
  u8             _end_time_us_;                                             \
  struct timeval _start_time_;                                              \
  struct timeval _end_time_;                                                \
  gettimeofday(&_start_time_, NULL)

#define TIMER_END(name, bytes, fields)                                      \
  gettimeofday(&_end_time_, NULL);                                          \
  _end_time_us_ = (_end_time_.tv_sec - _start_time_.tv_sec) * 1000000 +     \
                     (_end_time_.tv_usec - _start_time_.tv_usec);           \
  ALOGI("TIMER %s %lld.%06lld [%d bytes, %d fields]", name,                 \
        _end_time_us_ / 1000000, _end_time_us_ % 1000000,                   \
        (bytes), (fields))

#define TIMER_RESET() \
  gettimeofday(&_start_time_, NULL)

static bool isClassObject(Object* obj) {
  return obj->clazz == gDvm.classJavaLangClass;
}

ObjectInfo* offIdObjectInfo(u4 objId) {
  if(objId == COMM_INVALID_ID) return NULL;
  if(GET_ID_NUM(objId) == COMM_CLASS_BID) return &offIdToClass(objId)->offInfo;
  u4 bid = GET_ID_NUM(objId);
  u4 ind = STRIP_ID_MASK(objId);

  ObjectInfo* info = offTableGet(&gDvm.objTables[bid], ind);
  return info && info->obj ? info : NULL;
}

Object* offIdToObject(u4 objId) {
  ObjectInfo* info = offIdObjectInfo(objId);
  return info ? info->obj : NULL;
}

static u4 getMaxFieldIndex(Object* obj) {
  if(isClassObject(obj)) {
    return ((ClassObject*)obj)->sfieldCount;
  } else if(*obj->clazz->descriptor == '[') {
    return ((ArrayObject*)obj)->length;
  } else {
    return (obj->clazz->objectSize - sizeof(Object)) >> 2;
  }
}

static bool addTrackedObjectIdLocked(Object* obj, u4 objId) {
  obj->objId = objId;
  u4 bid = GET_ID_NUM(objId);
  u4 ind = STRIP_ID_MASK(objId);

  /* Put the object into the table. */
  ObjectInfo* info = offTableLockedGet(&gDvm.objTables[bid], ind);
  if(info->obj && info->obj != obj) return false;

  assert(!info->obj);
  u4 maxFields = getMaxFieldIndex(obj);

  info->obj = obj;
  info->dirty = 0;
  info->bits = maxFields > 32 ? (u4*)calloc((maxFields - 1) >> 5, 4)
                              : (u4*)NULL;
  info->isQueued = false;
  info->isVolatileOwner = info->isLockOwner = bid == GET_ID_NUM(gDvm.idMask);
  return true;
}

void offAddTrackedObject(Object* obj) {
  if(isClassObject(obj) &&
     ((ClassObject*)obj)->super != gDvm.classJavaLangReflectProxy) {
    /* We start tracking a class object when it is initialized.  We add it to
     * the write queue to indicate that it was initialized to the other end. */
    ObjectInfo* info = &((ClassObject*)obj)->offInfo;
    if(!info->isQueued) {
      pthread_mutex_lock(&gDvm.offCommLock);
      if(!info->isQueued) {
        info->isQueued = true;
        offAddToWriteQueueLocked(obj);
      }
      pthread_mutex_unlock(&gDvm.offCommLock);
    }
  } else if(obj->objId == COMM_INVALID_ID) {
    /* Otherwise we do the normal process of allocating an object identifier and
     * setting up a data structure for it. */
    pthread_mutex_lock(&gDvm.offCommLock);
    while(!addTrackedObjectIdLocked(obj, ADD_ID_MASK(gDvm.nextId++)));

    u4 maxField = getMaxFieldIndex(obj);
    ObjectInfo* info = offIdObjectInfo(obj->objId);
    memset(&info->dirty, 0xFF, sizeof(info->dirty));
    if(maxField > 32) {
      memset(info->bits, 0xFF, ((maxField - 1) >> 5) << 2);
    }

    offAddToWriteQueueLocked(obj);
    pthread_mutex_unlock(&gDvm.offCommLock);
  }
}

static bool isFieldDirtyRaw(u4 dirty, u4* bits, u4 fieldIndex) {
  if(fieldIndex < 32) {
    return (dirty & 1U << fieldIndex) != 0;
  }
  return (bits[(fieldIndex - 32) >> 5] & (1U << (fieldIndex & 0x1F))) != 0;
}

static bool isFieldDirty(ObjectInfo* info, u4 fieldIndex) {
  return isFieldDirtyRaw(info->dirty, info->bits, fieldIndex);
}

/* Only allowed in sync operations.  Clear a dirty bit.  Intended to be used in
 * syncPull when the other endpoint overwrites something. */
static void clearDirty(ObjectInfo* info, u4 fieldIndex) {
  if(fieldIndex < 32) {
    info->dirty &= ~(1U << fieldIndex);
  } else {
    info->bits[(fieldIndex - 32) >> 5] &= ~(1U << (fieldIndex & 0x1F));
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

READWRITEFUNC(u1, U1, , );
READWRITEFUNC(u2, U2, ntohs, htons);
READWRITEFUNC(u4, U4, ntohl, htonl);
READWRITEFUNC(u8, U8, ntohll, htonll);

static void writeValue(FifoBuffer* fb, char type, JValue* val) {
  switch(type) {
    case 'Z': writeU1(fb, val->z); break;
    case 'B': writeU1(fb, val->b); break;
    case 'C': writeU2(fb, val->c); break;
    case 'S': writeU2(fb, val->s); break;
    case 'F': case 'I': writeU4(fb, val->i); break;
    case 'D': case 'J': writeU8(fb, val->j); break;
    case '[': case 'L': {
      Object* obj = (Object*)val->l;
      if(obj != NULL) {
        u4 objId = auxObjectToId(obj);
        if(objId == COMM_INVALID_ID) {
          offAddTrackedObject(obj);
          objId = auxObjectToId(obj);
        }
        assert(objId != COMM_INVALID_ID);

        writeU4(fb, auxObjectToId((Object*)obj->clazz));
        writeU4(fb, objId);
        if(*obj->clazz->descriptor == '[') {
          ArrayObject* aobj = (ArrayObject*)obj;
          writeU4(fb, aobj->length);
        }
      } else {
        writeU4(fb, COMM_INVALID_ID);
      }
    } break;
    default: assert(0 && "unexpected type");
  }
}

static void readValue(Thread* self, FifoBuffer* fb, char type,
                      JValue* val, bool sgn_extend) {
  switch(type) {
    case 'Z': val->z = readU1(fb); break;
    case 'B': val->b = readU1(fb); break;
    case 'C': val->c = readU2(fb); break;
    case 'S': val->s = readU2(fb); break;
    case 'F': case 'I': val->i = readU4(fb); break;
    case 'D': case 'J': val->j = readU8(fb); break;
    case '[': case 'L': {
      u4 clId = readU4(fb);
      if(clId == COMM_INVALID_ID) {
        val->l = NULL;
      } else {
        ClassObject* clazz = (ClassObject*)offIdToObject(clId);
        assert(clazz->clazz == gDvm.classJavaLangClass);
        u4 objId = readU4(fb);
        u4 arrLength = *clazz->descriptor == '[' ? readU4(fb) : 0;

        Object* obj = offIdToObject(objId);
        if(obj == NULL) {
          if(*clazz->descriptor == '[') {
            obj = (Object*)dvmAllocArrayByClass(clazz, arrLength,
                                                ALLOC_DEFAULT);
            memset(((ArrayObject*)obj)->contents, 0,
                   arrLength * auxTypeWidth(clazz->descriptor[1]));
          } else {
            obj = dvmAllocObject(clazz, ALLOC_DEFAULT);
            memset(((DataObject*)obj)->instanceData, 0,
                   clazz->objectSize - sizeof(Object));
          }
          addTrackedObjectIdLocked(obj, objId);
          dvmReleaseTrackedAlloc(obj, self);
        }
        assert(obj && "failed to allocate object");
        val->l = obj;
      }
    } break;
    default: assert(0 && "unexpected type");
  }
  if(sgn_extend) switch(type) {
    case 'B': val->i = val->b; break;
    case 'S': val->i = val->s; break;
    default: break;
  }
}

void offRegisterProxy(ClassObject* clazz, char* str,
                      ClassObject** interfaces, u4 isz) {
  /* We only allow proxies to be created on the client so if we get here it's
   * because we created a mirror proxy and we don't actually want to register it
   * again. */
  if(gDvm.isServer) return;

  offAddTrackedObject(clazz);
  assert(clazz->objId != COMM_INVALID_ID);
  pthread_mutex_lock(&gDvm.offProxyLock);
  u4 ssz = strlen(str);
  writeU4(&gDvm.offProxyFifo, clazz->objId);
  writeU4(&gDvm.offProxyFifo, ssz);
  auxFifoPushData(&gDvm.offProxyFifo, str, ssz);
  writeU4(&gDvm.offProxyFifo, isz);
  while(isz-- > 0) {
    writeU4(&gDvm.offProxyFifo, auxObjectToId(*interfaces++));
  }
  pthread_mutex_unlock(&gDvm.offProxyLock);
}

void expandProxies(FifoBuffer* fb) {
  while(!auxFifoEmpty(fb)) {
    u4 objId = readU4(fb);
    u4 ssz = readU4(fb);
    char* str = (char*)malloc(ssz + 1);
    auxFifoReadBuffer(fb, str, ssz);
    str[ssz] = 0;
    u4 isz = readU4(fb);
    u4 i;

    StringObject* objstr = dvmCreateStringFromCstr(str);
    ArrayObject* interfaces = dvmAllocArrayByClass(
        dvmFindArrayClassForElement(gDvm.classJavaLangClass),
        isz, ALLOC_DEFAULT);
    for(i = 0; i < isz; i++) {
      ((ClassObject**)(void*)interfaces->contents)[i] =
          (ClassObject*)offIdToObject(readU4(fb));
    }

    ClassObject* clazz = dvmGenerateProxyClass(objstr, interfaces, NULL);
    assert(clazz);
    addTrackedObjectIdLocked(clazz, objId);
    dvmReleaseTrackedAlloc((Object*)interfaces, NULL);
    dvmReleaseTrackedAlloc((Object*)objstr, NULL);
  }
}

void offSyncPush() {
  offSyncPushDoIf(NULL, NULL, NULL, NULL);
}

void offSyncPushDoIf(bool(*before_func)(void*), void* before_arg,
                     void(*after_func)(void*, FifoBuffer*), void* after_arg) {
  Thread* self = dvmThreadSelf();

  TIMER_BEGIN();
  dvmSuspendAllThreads(SUSPEND_FOR_GC);

  if(before_func && !before_func(before_arg)) {
    dvmResumeAllThreads(SUSPEND_FOR_GC);
    return;
  }

  /* Send revision header. */
  offWriteU4(self, gDvm.offSendRevision++);

  /* Send over proxy definitions. */
  offWriteU4(self, auxFifoSize(&gDvm.offProxyFifo));
  while(!auxFifoEmpty(&gDvm.offProxyFifo)) {
    u4 bytes = auxFifoGetBufferSize(&gDvm.offProxyFifo);
    offSendMessage(self, auxFifoGetBuffer(&gDvm.offProxyFifo), bytes);
    auxFifoPushData(&gDvm.offProxyFifoAll,
                    auxFifoGetBuffer(&gDvm.offProxyFifo), bytes);
    auxFifoPopBytes(&gDvm.offProxyFifo, bytes);
  }

  u4 i, j;
  FifoBuffer fb = auxFifoCreate();

  /* Send over stack information. */
  FifoBuffer sfb = auxFifoCreate();
  offPushAllStacks(&sfb);

  /* Send over new class status updates. */
  for(j = 0; j < auxVectorSize(&gDvm.offStatusUpdate); j++) {
    ClassObject* clazz = (ClassObject*)auxVectorGet(&gDvm.offStatusUpdate, j).l;

    JValue valobj; valobj.l = clazz;
    writeValue(&fb, 'L', &valobj);
    writeU4(&fb, (u4)clazz->status);
    writeU4(&fb, (u4)clazz->initThreadId);
  }
  auxVectorResize(&gDvm.offStatusUpdate, 0);
  JValue valobj; valobj.l = NULL;
  writeValue(&fb, 'L', &valobj);

  u4 dirty_fields = 0;
  for(i = 0; i < auxVectorSize(&gDvm.offWriteQueue); i++) {
    Object* obj = auxVectorGet(&gDvm.offWriteQueue, i).l; assert(obj);
    ObjectInfo* info = offIdObjectInfo(obj->objId); assert(info);

    /* Write the object definition. */
    JValue valobj; valobj.l = obj;
    writeValue(&fb, 'L', &valobj);

    /* Write the dirty bits. */
    u4 maxIndex = getMaxFieldIndex(obj);
    writeU4(&fb, info->dirty);
    if(maxIndex > 32) {
      u4 bsz = (maxIndex - 1) >> 5;
      for(j = 0; j < bsz; j++) info->bits[j] = htonl(info->bits[j]);
      auxFifoPushData(&fb, (char*)info->bits, bsz << 2);
      for(j = 0; j < bsz; j++) info->bits[j] = ntohl(info->bits[j]);
    }

    if(isClassObject(obj)) {
      /* To sync class objects we just write out all of its globals. */
      ClassObject* clazz = (ClassObject*)obj;

      StaticField* fld = clazz->sfields;
      StaticField* efld = fld + clazz->sfieldCount;
      for(; fld != efld; ++fld) {
        if(isFieldDirty(info, fld - clazz->sfields)) {
          writeValue(&fb, *fld->signature, (JValue*)&fld->value);
          dirty_fields++;
        }
      }
    } else {
      ClassObject* clazz = obj->clazz;
      if(*clazz->descriptor == '[') {
        /* For array objects we send the whole array contents for now. */
        ArrayObject* aobj = (ArrayObject*)obj;
        char* contents = (char*)aobj->contents;

        u4 typeWidth = auxTypeWidth(aobj->clazz->descriptor[1]);
        for(j = 0; j < aobj->length; j++, contents += typeWidth) {
          if(isFieldDirty(info, j)) {
            writeValue(&fb, aobj->clazz->descriptor[1], (JValue*)contents);
            dirty_fields++;
          }
        }
      } else {
        /* For normal class objects we write out all the instance fields. */
        for(; clazz; clazz = clazz->super) {
          InstField* fld = clazz->ifields;
          InstField* efld = fld + clazz->ifieldCount;
          for(; fld != efld; ++fld) {
            if(isFieldDirty(info,
               (fld->byteOffset - sizeof(Object)) >> 2)) {
              writeValue(&fb, *fld->signature,
                         (JValue*)(((char*)obj) + fld->byteOffset));
              dirty_fields++;
            }
          }
        }
      }
    }
  }
  /* Terminate the object list. */
  valobj.l = NULL;
  writeValue(&fb, 'L', &valobj);

  /* Clear the dirty bits. */
  for(i = 0; i < auxVectorSize(&gDvm.offWriteQueue); i++) {
    Object* obj = auxVectorGet(&gDvm.offWriteQueue, i).l;
    ObjectInfo* info = offIdObjectInfo(obj->objId);
    info->isQueued = false;

    u4 maxIndex = getMaxFieldIndex(obj);
    info->dirty = 0;
    if(maxIndex > 32) {
      memset(info->bits, 0, ((maxIndex - 1) >> 5) << 2);
    }
  }
  auxVectorResize(&gDvm.offWriteQueue, 0);

  if(after_func) after_func(after_arg, &sfb);
  dvmResumeAllThreads(SUSPEND_FOR_GC);

  /* Actually send over the data.  We need to send the size of the data so that
   * the other endpoint can buffer in the whole thing before suspending the vm.
   */
  u4 total_bytes = auxFifoSize(&fb) + auxFifoSize(&sfb);
  offWriteU4(self, total_bytes);
  while(!auxFifoEmpty(&fb)) {
    u4 bytes = auxFifoGetBufferSize(&fb);
    offSendMessage(self, auxFifoGetBuffer(&fb), bytes);
    auxFifoPopBytes(&fb, bytes);
  }
  auxFifoDestroy(&fb);
  while(!auxFifoEmpty(&sfb)) {
    u4 bytes = auxFifoGetBufferSize(&sfb);
    offSendMessage(self, auxFifoGetBuffer(&sfb), bytes);
    auxFifoPopBytes(&sfb, bytes);
  }
  auxFifoDestroy(&sfb);

  /* Send over any loaded dex files.  Usually there is nothing to do here.
   * However if there is work to do it will be done lockstep with the client.
   * We wish to avoid suspending the whole VM for this part of the sync. */
  offPushDexFiles(self);

  /* Wait for the sync to complete. */
  offThreadWaitForResume(self);
  TIMER_END("syncPush", total_bytes, dirty_fields);
  gDvm.offSyncTime = (15 * gDvm.offSyncTime + _end_time_us_) / 16;
  ++gDvm.offSyncTimeSamples;
}

bool offSyncPull() {
  return offSyncPullDo(NULL, NULL, NULL, NULL);
}

/* This function looks nearly identical to offSyncPush */
bool offSyncPullDo(void(*before_func)(void*), void* before_arg,
                   void(*after_func)(void*, FifoBuffer*), void* after_arg) {
  struct Thread* self = dvmThreadSelf();

  TIMER_BEGIN();

  /* Grab revision header and wait for our turn.  Do this before we suspend the
   * threads so whoever should be pulling next will go ahead. */
  u4 rev = offReadU4(self);

  /* Read in the proxy data if any. */
  u4 bytes;
  FifoBuffer fbproxy = auxFifoCreate();
  bytes = offReadU4(self);
  if(!gDvm.offConnected) return false;
  while(bytes > 0) {
    char cbuf[1024];
    u4 rbytes = bytes < sizeof(cbuf) ? bytes : sizeof(cbuf);
    offReadBuffer(self, cbuf, rbytes);
    auxFifoPushData(&fbproxy, cbuf, rbytes);
    auxFifoPushData(&gDvm.offProxyFifoAll, cbuf, rbytes);
    bytes -= rbytes;
  }

  /* Read in all of the data we need. */
  FifoBuffer fb = auxFifoCreate();
  bytes = offReadU4(self);
  if(!gDvm.offConnected) return false;
  while(bytes > 0) {
    char cbuf[1024];
    u4 rbytes = bytes < sizeof(cbuf) ? bytes : sizeof(cbuf);
    offReadBuffer(self, cbuf, rbytes);
    auxFifoPushData(&fb, cbuf, rbytes);
    bytes -= rbytes;
  }

  u4 total_bytes = auxFifoSize(&fbproxy) + auxFifoSize(&fb);
  TIMER_END("syncPull [read]", total_bytes, 0);

  /* Load in any dex files.  Usually there is nothing to do here.  This should
   * happen prior to any thread suspension. */
  offPullDexFiles(self);

  /* Wait for our turn to inflate. */
  if((int)(rev - gDvm.offRecvRevision) > 0) {
    ThreadStatus status = dvmChangeStatus(self, THREAD_WAIT);
    pthread_mutex_lock(&gDvm.offCommLock);
    while((int)(rev - gDvm.offRecvRevision) && gDvm.offConnected) {
      pthread_cond_wait(&gDvm.offPullCond, &gDvm.offCommLock);
    }
    pthread_mutex_unlock(&gDvm.offCommLock);
    dvmChangeStatus(self, status);
  }

  if(!gDvm.offConnected) return false;
  /* At this point all of the data has been received successfully so we can
   * perform the sync safetly even if the other endpoint is about to go down. */

  /* Suspend everything.  We need to lock the heap before the suspend so we can
   * be sure that we won't leave a thread working on the heap which will prevent
   * GCs from happening while we sync.  We don't need this requirement when
   * we're pushing however. */
  dvmLockHeap();
  while(gDvm.gcHeap->gcRunning) {
    dvmWaitForConcurrentGcToComplete();
  }
  dvmSuspendAllThreads(SUSPEND_FOR_GC);
  dvmUnlockHeap();

  TIMER_RESET();
  if(before_func) before_func(before_arg);

  expandProxies(&fbproxy);
  auxFifoDestroy(&fbproxy);

  /* Read in new class statuses. */
  for(;;) {
    JValue valobj; readValue(self, &fb, 'L', &valobj, false);
    ClassObject* clazz = (ClassObject*)valobj.l;
    if(clazz == NULL) break;

    if(clazz->status < CLASS_INITIALIZING) {
      /* We need to make sure that this class gets linked together and our
       * important register maps get created. */
      dvmDryInitClass(clazz);
    }
    clazz->status = (ClassStatus)readU4(&fb);
    clazz->initThreadId = readU4(&fb);
  }

  u4 j;
  u4* bits = NULL;
  u4 bitsSz = 0;
  u4 dirtyCount = 0;
  for(;;) {
    JValue valobj; readValue(self, &fb, 'L', &valobj, false);
    Object* obj = valobj.l;
    if(obj == NULL) break;
    ObjectInfo* info = offIdObjectInfo(obj->objId);

    /* Read in the dirty bits. */
    u4 maxIndex = getMaxFieldIndex(obj);
    u4 dirty = readU4(&fb);
    if(maxIndex > 32) {
      u4 bsz = (maxIndex - 1) >> 5;
      if(bsz > bitsSz) {
        bits = (u4*)realloc(bits, bsz << 2);
        bitsSz = bsz;
      }
      auxFifoReadBuffer(&fb, (char*)bits, bsz << 2);
      for(j = 0; j < bsz; j++) bits[j] = ntohl(bits[j]);
    }

    if(isClassObject(obj)) {
      ClassObject* clazz = (ClassObject*)obj;

      StaticField* fld = clazz->sfields;
      StaticField* efld = fld + clazz->sfieldCount;
      for(; fld != efld; ++fld) {
        u4 fieldIndex = fld - clazz->sfields;
        if(isFieldDirtyRaw(dirty, bits, fieldIndex)) {
          readValue(self, &fb, *fld->signature, (JValue*)&fld->value, true);
          clearDirty(info, fieldIndex);
          dirtyCount++;
        }
      }
    } else {
      ClassObject* clazz = obj->clazz;
      if(*clazz->descriptor == '[') {
        ArrayObject* aobj = (ArrayObject*)obj;
        char* contents = (char*)aobj->contents;

        u4 typeWidth = auxTypeWidth(clazz->descriptor[1]);
        for(j = 0; j < aobj->length; j++, contents += typeWidth) {
          if(isFieldDirtyRaw(dirty, bits, j)) {
            readValue(self, &fb, clazz->descriptor[1],
                      (JValue*)contents, false);
            clearDirty(info, j);
            dirtyCount++;
          }
        }
      } else {
        for(; clazz; clazz = clazz->super) {
          InstField* fld = clazz->ifields;
          InstField* efld = fld + clazz->ifieldCount;
          for(; fld != efld; ++fld) {
            u4 fieldIndex = (fld->byteOffset - sizeof(Object)) >> 2;
            if(isFieldDirtyRaw(dirty, bits, fieldIndex)) {
              readValue(self, &fb, *fld->signature,
                        (JValue*)(((char*)obj) + fld->byteOffset), true);
              clearDirty(info, fieldIndex);
              dirtyCount++;
            }
          }
        }
      }
    }
  }
  free(bits);

  offPullAllStacks(&fb);
  if(after_func) after_func(after_arg, &fb);

  assert(auxFifoSize(&fb) == 0);
  auxFifoDestroy(&fb);

  offWriteU1(self, OFF_ACTION_RESUME);
  TIMER_END("syncPull", total_bytes, dirtyCount);

  pthread_mutex_lock(&gDvm.offCommLock);
  gDvm.offRecvRevision++;
  pthread_cond_broadcast(&gDvm.offPullCond);
  pthread_mutex_unlock(&gDvm.offCommLock);
  dvmResumeAllThreads(SUSPEND_FOR_GC);

  return true;
}

/* We only need to do work when we're not making memory for a potential OOM. */
void offGcMarkOffloadRefs(const GcSpec* spec, bool remark) {
  if(!spec->doPreserve) {
    Thread* self = dvmThreadSelf();
ALOGI("STARTING TRIMGC ON %d", self->threadId);
    if(!self->offTrimSignaled) {
      offWriteU1(self, OFF_ACTION_TRIMGC);
    }
    offGcDoTrackTrim();
    return;
  }

  GcMarkContext* ctx = &gDvm.gcHeap->markContext;
  ctx->finger = (void *)ULONG_MAX;

  pthread_mutex_lock(&gDvm.offCommLock);
  u4 i, j;
  for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
    u4 sz = offTableSize(&gDvm.objTables[i]);
    for(j = 0; j < sz; ++j) {
      ObjectInfo* info = offTableGet(&gDvm.objTables[i], j);
      if(info && info->obj) {
        dvmMarkObjectOnStack(info->obj, ctx);
      }
    }
  }
  pthread_mutex_unlock(&gDvm.offCommLock);

  offGcMarkDexRefs(remark);
  dvmProcessMarkStack(ctx);
}

void offGcDoTrackTrim() {
  u4 i, j, k;
  u4 iterations;
  GcMarkContext* ctx = &gDvm.gcHeap->markContext;
  Thread* thctx = &gDvm.gcThreadContext;

  ctx->finger = (void *)ULONG_MAX;
  for(iterations = 1; ; iterations++) {
    u4 bv;
    bool work = false;
ALOGI("ITERATION A %d", iterations);

    /* First send over all marked tracked objects in a bit vector. */
    for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
      u4 sz = offTableSize(&gDvm.objTables[i]);
      offWriteU4(thctx, sz);
      for(j = 0; j < sz; j += 32) {
        u4 bv = 0;
        for(k = 0; k < 32 && j + k < sz; k++) {
          ObjectInfo* info = offTableGet(&gDvm.objTables[i], j + k);
          if(info && info->obj && dvmIsMarked(info->obj)) {
            bv |= 1U << k;
          }
        }
        offWriteU4(thctx, bv);
      }
    }

ALOGI("ITERATION B %d", iterations);
    /* Now read in the marked tracked objects from the remoted endpoint and mark
     * them. */
    for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
      u4 sz = offReadU4(thctx);
      for(j = 0; j < sz; j += 32) {
        bv = offReadU4(thctx);
        for(k = 0; k < 32 && j + k < sz; k++) {
          if(~bv & 1U << k) continue;

          ObjectInfo* info = offTableGet(&gDvm.objTables[i], j + k);
          if(info && info->obj && !dvmIsMarked(info->obj)) {
            work = true;
            dvmMarkObjectOnStack(info->obj, ctx);
          }
        }
      }
    }

ALOGI("ITERATION C %d %d", iterations, work);
    offWriteU1(thctx, work);
    work |= offReadU1(thctx);
ALOGI("ITERATION D %d %d", iterations, work);
    if(work) {
      dvmProcessMarkStack(ctx);
    } else {
      break;
    }
ALOGI("ITERATION E %d", iterations);
  }

  /* Ok now we can trim all of the objects that haven't been marked. */
  u4 trimmed = 0;
  u4 total = 0;
  for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
    u4 sz = offTableSize(&gDvm.objTables[i]);
    for(j = 0; j < sz; ++j) {
      ObjectInfo* info = offTableGet(&gDvm.objTables[i], j);
      if(info && info->obj && !dvmIsMarked(info->obj)) {
        trimmed++;
        info->obj->objId = COMM_INVALID_ID;
        info->obj = NULL;
        free(info->bits);
        info->bits = NULL;
      }
    }
    total += sz;
  }

  u4 purged = 0;
  u4 sz = auxVectorSize(&gDvm.offWriteQueue);
  for(i = 0; i < sz; i++) {
    Object* obj = auxVectorGet(&gDvm.offWriteQueue, i).l;
    if(!dvmIsMarked(obj)) {
      purged++;
      auxVectorSet(&gDvm.offWriteQueue, i--,
          auxVectorGet(&gDvm.offWriteQueue, --sz));
    }
  }
  auxVectorResize(&gDvm.offWriteQueue, sz);

  gDvm.nextId = 0;
  ALOGI("GC_TRACK_TRIM: Trimmed %d/%d objects in %d iterations "
        "purging %d objects from the write queue",
        trimmed, total, iterations, purged);
}

bool offCommStartup() {
  gDvm.nextId = 0;
  gDvm.idMask = gDvm.isServer ? 1U << 30 : 0;
  memset(gDvm.objTables, 0, sizeof(gDvm.objTables));
  pthread_mutex_init(&gDvm.offCommLock, NULL);
  pthread_cond_init(&gDvm.offPullCond, NULL);
  gDvm.offWriteQueue = auxVectorCreate(0);
  gDvm.offRecvRevision = gDvm.offSendRevision = 0;
  pthread_mutex_init(&gDvm.offProxyLock, NULL);
  gDvm.offProxyFifo = auxFifoCreate();
  gDvm.offProxyFifoAll = auxFifoCreate();
  gDvm.offStatusUpdate = auxVectorCreate(0);
  return true;
}

void offCommShutdown() {
  u4 i;
  pthread_mutex_destroy(&gDvm.offCommLock);
  for(i = 0; i < sizeof(gDvm.objTables) / sizeof(gDvm.objTables[0]); ++i) {
    offTableDestroy(&gDvm.objTables[i]);
  }
  auxVectorDestroy(&gDvm.offWriteQueue);
}

/* Special native files to add tracking code.

native/java_lang_System.c: OK
UtfString.c: OK
InlineNative.c: OK
Exception.c: OK
Debugger.c: OK
Ddm.c: OK

Jni.c: Still need to make GetArrayElements and GetPrimitiveArrayCritical copying
*/
