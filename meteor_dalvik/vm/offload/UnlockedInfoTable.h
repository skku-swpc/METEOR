#ifndef UNLOCKED_INFO_TABLE_H
#define UNLOCKED_INFO_TABLE_H

#include "Inlines.h"
#include "offload/Comm.h"

#define CLZ(x) __builtin_clz(x)

typedef struct UnlockedInfoTable {
  u4 size;
  ObjectInfo* base[30];
} UnlockedInfoTable;

/*
INLINE UnlockedInfoTable offTableCreate() {
  UnlockedInfoTable ret;
  memset(&ret, 0, sizeof(ret));
  return ret;
}
*/

INLINE void offTableDestroy(UnlockedInfoTable* v) {
  u4 i = 0;
  for(i = 0; i < 30; i++) {
    free(v->base[i]);
    v->base[i] = NULL;
  }
}

INLINE u4 offTableSize(UnlockedInfoTable* v) {
  return v->size;
}

INLINE ObjectInfo* offTableLockedGet(UnlockedInfoTable* v, u4 ind) {
  u4 id = 28 - CLZ(ind | 0xF);
  v->size = v->size <= ind ? ind + 1 : v->size;
  if(v->base[id] == NULL) {
    v->base[id] = (ObjectInfo*)calloc(1 << (id ? id + 3 : 4),
                                      sizeof(ObjectInfo));
  }
  return v->base[id] + (id ? (ind ^ (8 << id)) : ind);
}

INLINE ObjectInfo* offTableGet(UnlockedInfoTable* v, u4 ind) {
  u4 id = 28 - CLZ(ind | 0xF);
  if(v->base[id] == NULL) {
    return NULL;
  }
  return v->base[id] + (id ? (ind ^ (8 << id)) : ind);
}

#endif // UNLOCKED_INFO_TABLE_H
