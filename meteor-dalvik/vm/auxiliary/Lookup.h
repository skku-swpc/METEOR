#ifndef AUXILIARY_LOOKUP_H
#define AUXILIARY_LOOKUP_H

#include "Common.h"

struct ClassObject;
struct Method;
struct DvmDex;

#define PACK_METHOD_IDX(mtd) ((mtd)->idx | \
    (((mtd)->accessFlags & ACC_STATIC) ? 0x00800000U : 0U) | \
    (((mtd)->accessFlags & ACC_PRIVATE) || \
     ((mtd)->accessFlags & ACC_CONSTRUCTOR) ? 0x00400000U : 0U))
#define GET_METHOD_IDX(id) ((id) & 0x0000FFFFU)
#define GET_METHOD_STATIC(id) (((id) & 0x00800000U)!=0)
#define GET_METHOD_DIRECT(id) (((id) & 0x00400000U)!=0)

#define PACK_FIELD_IDX(fld) ((fld)->idx | \
    (((fld)->accessFlags & ACC_STATIC) ? 0x00800000U : 0U))
#define GET_FIELD_IDX(id) ((id) & 0x0000FFFFU)
#define GET_FIELD_STATIC(id) (((id) & 0x00800000U)!=0)

#ifdef WITH_OFFLOAD

struct ClassObject* auxClassByIdx(struct DvmDex* pDvmDex, u4 classIdx,
                                  bool init);

struct Method* auxMethodByIdx(struct DvmDex* pDvmDex, u4 methodIdx,
                              bool isStatic, bool isDirect, bool init);

struct Field* auxFieldByIdx(struct DvmDex* pDvmDex, u4 fieldIdx,
                            bool isStatic);

#endif

#endif // AUXILIARY_LOOKUP_H
