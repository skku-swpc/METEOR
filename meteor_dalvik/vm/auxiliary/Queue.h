#ifndef AUXILIARY_QUEUE_H
#define AUXILIARY_QUEUE_H

#include "Inlines.h"
#include "auxiliary/Vector.h"

typedef struct Queue {
  u4 pos;
  u4 size;
  u4 cap;
  AuxValue* array;
} Queue;

Queue auxQueueCreate();

void auxQueueDestroy(Queue* q);

bool auxQueueEmpty(Queue* q);

u4 auxQueueSize(Queue* q);

void auxQueuePush(Queue* q, AuxValue val);

INLINE void auxQueuePushI(Queue* v, u4 vali) {
  AuxValue val; val.i = vali; auxQueuePush(v, val);
}
INLINE void auxQueuePushL(Queue* v, struct Object* vall) {
  AuxValue val; val.l = vall; auxQueuePush(v, val);
}
INLINE void auxQueuePushV(Queue* v, void* valv) {
  AuxValue val; val.v = valv; auxQueuePush(v, val);
}

AuxValue auxQueuePop(Queue* q);

AuxValue auxQueuePeek(Queue* q);

AuxValue auxQueuePeekBack(Queue* q);

#endif // AUXILIARY_QUEUE_H
