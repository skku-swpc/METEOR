#include "Dalvik.h"

#ifdef WITH_OFFLOAD

ClassObject* auxClassByIdx(DvmDex* pDvmDex, u4 classIdx, bool init) {
  ClassObject* clazz = dvmDexGetResolvedClass(pDvmDex, classIdx);
  if(!clazz) {
    // We need a dummy ClassObject to be the referrer within the requested
    // DvmDex object.
    ClassObject dummy;
    dummy.accessFlags = CLASS_ISPREVERIFIED;
    dummy.descriptor = "Ltrivial;";
    dummy.classLoader = pDvmDex->classLoader;
    dummy.pDvmDex = pDvmDex;

    Thread* self = dvmThreadSelf();
    clazz = dvmResolveClass(&dummy, classIdx, false);
    if(dvmCheckException(self)) {
      ALOGE("Got exception resolving class [%d, %X] %s", pDvmDex->id, classIdx,
            dvmGetException(self)->clazz->descriptor);
      //dvmPrintExceptionStackTrace();
    }

    /* Let's try something different... */
/*
    const char* descriptor = dexStringByTypeIdx(pDvmDex->pDexFile, classIdx);
    clazz = dvmDefineClass(pDvmDex, descriptor, pDvmDex->classLoader);
    dvmDexSetResolvedClass(pDvmDex, classIdx, clazz);
*/
  }
  if(init && clazz->status != CLASS_INITIALIZED) {
    dvmInitClass(clazz);
  }
  return clazz;
}

Method* auxMethodByIdx(DvmDex* pDvmDex, u4 methodIdx,
                       bool isStatic, bool isDirect, bool init) {
  Method* meth;
  if(init) {
    // If we are forcing the method's class to be initialized then check if it's
    // already been cached in the initialized list.  Otherwise go through the
    // normal method resolution method that will initialized the class.
    meth = dvmDexGetResolvedMethod(pDvmDex, methodIdx);
    if(!meth) {
      ClassObject dummy;
      dummy.accessFlags = CLASS_ISPREVERIFIED;
      dummy.descriptor = "Ltrivial;";
      dummy.classLoader = pDvmDex->classLoader;
      dummy.pDvmDex = pDvmDex;

      meth = dvmResolveMethod(&dummy, methodIdx, isStatic ? METHOD_STATIC :
                              (isDirect ? METHOD_DIRECT : METHOD_VIRTUAL));
    }
  } else {
    // Otherwise if we don't want to force the class to initialize check if the
    // method is already cached in the unresolved method list.  This list gets
    // populated the first time the class is looked up (but prior to
    // initialization) so if not lookup the class.
    meth = dvmDexGetUnresolvedMethod(pDvmDex, methodIdx);
    if(!meth || !dvmIsClassLinked(meth->clazz)) {
      const DexMethodId* methIdInfo =
          dexGetMethodId(pDvmDex->pDexFile, methodIdx);
      const char* classDesc =
          dexStringByTypeIdx(pDvmDex->pDexFile, methIdInfo->classIdx);
      /* Force the class to be loaded. */
      dvmFindClassNoInit(classDesc, pDvmDex->classLoader);
      meth = dvmDexGetUnresolvedMethod(pDvmDex, methodIdx);
    }
  }
  if(!meth) ALOGW("Failed to lookup %d %d %d %d\n", pDvmDex->id, methodIdx,
                  isStatic, isDirect);
  return meth;
}

Field* auxFieldByIdx(DvmDex* pDvmDex, u4 fieldIdx, bool isStatic) {
  Field* field = dvmDexGetResolvedField(pDvmDex, fieldIdx);
  if(!field) {
    ClassObject dummy;
    dummy.accessFlags = CLASS_ISPREVERIFIED;
    dummy.descriptor = "Ltrivial;";
    dummy.classLoader = pDvmDex->classLoader;
    dummy.pDvmDex = pDvmDex;

    if(isStatic) {
      field = (Field*)dvmResolveStaticField(&dummy, fieldIdx);
    } else {
      field = (Field*)dvmResolveInstField(&dummy, fieldIdx);
    }
  }
  return field;
}

#endif
