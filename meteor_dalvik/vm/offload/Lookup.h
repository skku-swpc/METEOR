#ifndef OFFLOAD_LOOKUP_H
#define OFFLOAD_LOOKUP_H

#include "Common.h"

struct ClassObject;
struct Object;

bool offIsClassObject(struct Object* obj);

u4 offClassToId(struct ClassObject* clazz);

struct ClassObject* offIdToClass(u4 id);

#endif // OFFLOAD_LOOKUP_H
