INLINE void offAddToWriteQueueLocked(Object* obj) {
  assert(obj);
  auxVectorPushL(&gDvm.offWriteQueue, obj);
}

INLINE void offMarkField(ObjectInfo* info, u4 fieldIndex) {
  int32_t val;
  int32_t* ptr;
  if(fieldIndex < 32) {
    val = 1 << fieldIndex;
    ptr = (int32_t*)&info->dirty;
  } else {
    val = 1U << (fieldIndex & 0x1F);
    ptr = (int32_t*)info->bits + ((fieldIndex - 32) >> 5);
  }
  if(~*ptr & val) {
    android_atomic_or(val, ptr);
  }

  if(!info->isQueued) {
    pthread_mutex_lock(&gDvm.offCommLock);
    if(!info->isQueued) {
      info->isQueued = true;
      offAddToWriteQueueLocked(info->obj);
    }
    pthread_mutex_unlock(&gDvm.offCommLock);
  }
}

/* Track a write to an instance member of an object. */
INLINE void offTrackInstanceWrite(const Object* obj, int offset) {
  if((gDvm.isServer && gDvm.initializing) ||
     obj->objId == COMM_INVALID_ID ||
     offset < (int)sizeof(Object)) {
    return;
  }

  ObjectInfo* info = offIdObjectInfo(obj->objId);
  assert(info && "failed to get object info");
  offMarkField(info, (offset - sizeof(Object)) >> 2);
}

INLINE void offTrackInstanceWriteVolatile(const Object* obj, int offset) {
  Thread* self = NULL;
  if((gDvm.isServer && gDvm.initializing) ||
     obj->objId == COMM_INVALID_ID ||
     offset < (int)sizeof(Object) ||
     !offRecoveryEnterHazard((self = dvmThreadSelf()))) {
    return;
  }

  ObjectInfo* info = offIdObjectInfo(obj->objId);
  assert(info && "failed to get object info");
  offGrabVolatiles(info, (offset - sizeof(Object)) >> 2);
  offMarkField(info, (offset - sizeof(Object)) >> 2);
  offRecoveryClearHazard(self);
}

/* Track a write to a global */
INLINE void offTrackGlobalWrite(const StaticField* field) {
  if(gDvm.isServer && gDvm.initializing) return;

  /* The common case is that we have a normal class and can just peer into its
   * offInfo structure.  Otherwise for proxy classes we need to do a normal
   * object info lookup like any other object. */
  ObjectInfo* info =
      field->clazz->super == gDvm.classJavaLangReflectProxy ?
      offIdObjectInfo(field->clazz->objId) :
      &field->clazz->offInfo;
  /* Make sure info is not null as is the case in some very rare (but ok)
   * instances. */
  if(info) {
    offMarkField(info, field - field->clazz->sfields);
  }
}

INLINE void offTrackGlobalWriteVolatile(const StaticField* field) {
  Thread* self = NULL;
  if(gDvm.isServer && gDvm.initializing) return;

  ObjectInfo* info =
      field->clazz->super == gDvm.classJavaLangReflectProxy ?
      offIdObjectInfo(field->clazz->objId) :
      &field->clazz->offInfo;
  if(info && offRecoveryEnterHazard((self = dvmThreadSelf()))) {
    offGrabVolatiles(info, field - field->clazz->sfields);
    offMarkField(info, field - field->clazz->sfields);
    offRecoveryClearHazard(self);
  }
}

/* Track a write to an array range [startIndex, endIndex). */
INLINE void offTrackArrayWrite(const ArrayObject* aobj,
                               u4 startIndex, u4 endIndex) {
  if((gDvm.isServer && gDvm.initializing) ||
      aobj->objId == COMM_INVALID_ID) return;

  u4 ind;
  ObjectInfo* info = offIdObjectInfo(aobj->objId);
  assert(info && "failed to get object info");

  // TODO: Do this more efficiently.
  for(ind = startIndex; ind < endIndex; ind++) {
    offMarkField(info, ind);
  }
}

INLINE void offPrepInstanceReadVolatile(const Object* obj, int offset) {
  Thread* self = NULL;
  if((gDvm.isServer && gDvm.initializing) ||
     obj->objId == COMM_INVALID_ID ||
     offset < (int)sizeof(Object) ||
     !offRecoveryEnterHazard((self = dvmThreadSelf()))) {
    return;
  }

  ObjectInfo* info = offIdObjectInfo(obj->objId);
  assert(info && "failed to get object info");
  offGrabVolatiles(info, (offset - sizeof(Object)) >> 2);
  offRecoveryClearHazard(self);
}

INLINE void offPrepGlobalReadVolatile(const StaticField* field) {
  Thread* self = NULL;
  if(gDvm.isServer && gDvm.initializing) return;
  ObjectInfo* info =
      field->clazz->super == gDvm.classJavaLangReflectProxy ?
      offIdObjectInfo(field->clazz->objId) :
      &field->clazz->offInfo;
  if(info && offRecoveryEnterHazard((self = dvmThreadSelf()))) {
    offGrabVolatiles(info, field - field->clazz->sfields);
    offRecoveryClearHazard(self);
  }
}

#ifdef DEBUG
INLINE void offCheckLockNotHeld(pthread_mutex_t* mutex) {
  int res = pthread_mutex_trylock(&gDvm.threadListLock);
  if(res == 0) {
    pthread_mutex_unlock(&gDvm.threadListLock);
  } else {
    /* Make sure that the reason we couldn't acquire the lock is because
     * someone else is holding it. */
    assert(res == EBUSY);
  }
}
#else
INLINE void offCheckLockNotHeld(pthread_mutex_t* mutex) {
}
#endif
