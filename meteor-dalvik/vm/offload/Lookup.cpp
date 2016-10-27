#include "Dalvik.h"

bool offIsClassObject(Object* obj) {
  return obj->clazz == gDvm.classJavaLangClass;
}

u4 offClassToId(ClassObject* clazz) {
  u4 arrayDim = 0;
  ClassObject* encClazz = clazz;
  if(IS_CLASS_FLAG_SET(clazz, CLASS_ISARRAY)) {
    encClazz = clazz->elementClass;
    arrayDim = clazz->arrayDim;
  }
  if(encClazz->pDvmDex == NULL) {
    if(*encClazz->descriptor == 'L') {
      /* Proxy class?  Not handled. */
      return COMM_INVALID_ID;
    }
    return COMM_CLASS_BID << 30U |
           PRIMITIVE_DEX_ID << 24U |
           arrayDim << 16U |
           *encClazz->descriptor;
  } else {
    return COMM_CLASS_BID << 30U |
           offDexToId(encClazz->pDvmDex) << 24U |
           arrayDim << 16U |
           encClazz->idx;
  }
}

ClassObject* offIdToClass(u4 id) {
  u4 dexId = id >> 24U & 0x3FU;
  u4 arrayDim = id >> 16U & 0xFFU;
  u4 classIdx = id & 0xFFFFU;
  ClassObject* clazz;
  if(dexId == PRIMITIVE_DEX_ID) {
    clazz = dvmFindPrimitiveClass((char)classIdx);
  } else {
    clazz = auxClassByIdx(offIdToDex(dexId), classIdx, false);
  }
  while(arrayDim--) {
    clazz = dvmFindArrayClassForElement(clazz);
  }
  return clazz;
}
