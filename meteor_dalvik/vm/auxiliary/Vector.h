#ifndef AUXILIARY_VECTOR_H
#define AUXILIARY_VECTOR_H

#include "Common.h"
#include "Inlines.h"

struct Object;

typedef union AuxValue {
  u4 i;
  struct Object* l;
  void* v;
} AuxValue;

typedef struct Vector {
  u4 size;
  u4 cap;
  AuxValue* array;
} Vector;

// Create a new vector with the passed capacity.  If 0 is passed for the
// capacity nothing is actually allocated on the heap.
Vector auxVectorCreate(u4 cap);

// Create a vector out of the passed array allocated with malloc.  The passed
// array is now owned by the vector and cleanup should be done through
// auxVectorDestroy.
Vector auxVectorInduct(void* array, u4 size);

// Returns the size of the vector prior to destruction.
u4 auxVectorDestroy(Vector* v);

INLINE u4 auxVectorSize(Vector* v) {
  return v->size;
}

INLINE bool auxVectorEmpty(Vector* v) {
  return v->size == 0;
}

INLINE AuxValue* auxVectorArray(Vector* v) {
  return v->array;
}

// Returns the index that val was inserted at.
u4 auxVectorPush(Vector* v, AuxValue val);

INLINE u4 auxVectorPushI(Vector* v, u4 vali) {
  AuxValue val; val.i = vali; return auxVectorPush(v, val);
}
INLINE u4 auxVectorPushL(Vector* v, struct Object* vall) {
  AuxValue val; val.l = vall; return auxVectorPush(v, val);
}
INLINE u4 auxVectorPushV(Vector* v, void* valv) {
  AuxValue val; val.v = valv; return auxVectorPush(v, val);
}

INLINE void auxVectorSet(Vector* v, u4 ind, AuxValue val) {
  assert(ind < v->size && "index out of bounds");
  v->array[ind] = val;
}
INLINE void auxVectorSetI(Vector* v, u4 ind, u4 vali) {
  AuxValue val; val.i = vali; auxVectorSet(v, ind, val);
}
INLINE void auxVectorSetL(Vector* v, u4 ind, struct Object* vall) {
  AuxValue val; val.l = vall; auxVectorSet(v, ind, val);
}
INLINE void auxVectorSetV(Vector* v, u4 ind, void* valv) {
  AuxValue val; val.v = valv; auxVectorSet(v, ind, val);
}

INLINE AuxValue auxVectorGet(Vector* v, u4 ind) {
  assert(ind < v->size && "index out of range");
  return v->array[ind];
}

AuxValue auxVectorPop(Vector* v);

AuxValue auxVectorPopFront(Vector* v);

void auxVectorResize(Vector* v, u4 size);

#endif // AUXILIARY_VECTOR_H
