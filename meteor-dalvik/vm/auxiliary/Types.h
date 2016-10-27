#ifndef AUXILIARY_TYPES_H
#define AUXILIARY_TYPES_H

#include <arpa/inet.h>
#include <string.h>

#include "Common.h"
#include "Inlines.h"

INLINE
u8 htonll(u8 x) {
  union {
    u8 ll;
    u4 l[2];
  } res;
  res.l[0] = htonl(x);
  res.l[1] = htonl(x >> 32U);
  return res.ll;
}

INLINE
u8 ntohll(u8 x) {
  union {
    u8 ll;
    u4 l[2];
  } res;
  res.ll = x;
  return ntohl(res.l[0]) | (u8)ntohl(res.l[1]) << 32U;
}

INLINE
u4 auxTypeWidth(char type) {
  switch(type) {
    case 'Z': return 1;
    case 'B': return 1;
    case 'C': return 2;
    case 'S': return 2;
    case 'I': return 4;
    case 'F': return 4;
    case 'J': return 8;
    case 'D': return 8;
    case 'L': return sizeof(void*);
    case '[': return sizeof(void*);
  }
  ALOGW("auxTypeWidth: Unexpected type");
  return -1;
}

INLINE
u8 auxValueTo8(JValue val, char type) {
  switch(type) {
    case 'Z': return val.z;
    case 'B': return val.b;
    case 'C': return val.c;
    case 'S': return val.s;
    case 'I': return val.i;
    case 'F': return val.i;
    case 'J': return val.j;
    case 'D': return val.j;
    case 'L': return (uintptr_t)val.l;
    case '[': return (uintptr_t)val.l;
    case 'V': return 0;
  }
  ALOGW("auxValueTo8: Unexpected type");
  return -1;
}

INLINE
JValue aux8ToValue(u8 val, char type) {
  JValue ret; memset(&ret, 0, sizeof(ret));
  switch(type) {
    case 'Z': ret.z = (bool)val; break;
    case 'B': ret.b = (u1)val; break;
    case 'C': ret.c = (u2)val; break;
    case 'S': ret.s = (s2)val; break;
    case 'I': ret.i = (s4)val; break;
    case 'F': ret.i = (s4)val; break;
    case 'J': ret.j = (s8)val; break;
    case 'D': ret.j = (s8)val; break;
    case 'L': ret.l = (Object*)(uintptr_t)val; break;
    case '[': ret.l = (Object*)(uintptr_t)val; break;
    case 'V': break;
    default:  ALOGW("aux8ToValue: Unexpected type");
  }
  return ret;
}

INLINE
JValue auxPtrToValue(u1* val, char type) {
  JValue ret;
  memset(&ret, 0, sizeof(ret));
  memcpy(&ret, val, auxTypeWidth(type));
  return ret;
}

void auxValuePrint(FILE* f, JValue val, char type);

#endif // AUXILIARY_TYPES_H
