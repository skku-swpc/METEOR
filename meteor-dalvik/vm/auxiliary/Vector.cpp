#include "auxiliary/Vector.h"

#include <stdlib.h>
#include <string.h>

Vector auxVectorCreate(u4 cap) {
  Vector ret;
  if(cap) {
    ret.size = 0;
    ret.cap = cap;
    ret.array = (AuxValue*)malloc(sizeof(AuxValue) * cap);
    assert(ret.array && "vector create malloc failed");
  } else {
    ret.size = ret.cap = 0;
    ret.array = NULL;
  }
  return ret;
}

Vector auxVectorInduct(void* array, u4 size) {
  Vector ret;
  ret.size = size;
  ret.cap = size;
  ret.array = (AuxValue*)array;
  return ret;
}

u4 auxVectorDestroy(Vector* v) {
  free(v->array);
  return v->size;
}

static inline void reallocate(Vector* v, u4 size) {
  u4 ncap = v->cap;
  while(ncap < size) ncap = ncap * 3 / 2 + 10;
  while(size * 9 < ncap * 4) ncap = ncap * 2 / 3;
  if(ncap != v->cap) {
    if(!ncap) {
      free(v->array); v->array = NULL;
    } else {
      v->array = (AuxValue*)realloc(v->array, sizeof(AuxValue) * ncap);
      assert(v->array && "failed to resize vector");
    }
  }
  v->cap = ncap;
  v->size = size;
}

u4 auxVectorPush(Vector* v, AuxValue val) {
  u4 sz = v->size;
  reallocate(v, sz + 1);
  v->array[sz] = val;
  return sz;
}

AuxValue auxVectorPop(Vector* v) {
  u4 sz = v->size;
  assert(sz && "can't pop from empty vector");
  --sz;
  AuxValue val = v->array[sz];
  reallocate(v, sz);
  return val;
}

AuxValue auxVectorPopFront(Vector* v) {
  u4 sz = v->size;
  assert(sz && "can't pop from empty vector");
  AuxValue val = v->array[0];
  u4 i;
  for(i = 1; i < sz; i++) v->array[i - 1] = v->array[i];
  reallocate(v, sz - 1);
  return val;
}

void auxVectorResize(Vector* v, u4 size) {
  u4 oldSize = v->size;
  reallocate(v, size);
  if(oldSize < size) {
    memset(v->array + oldSize, 0, sizeof(AuxValue) * (size - oldSize));
  }
}
