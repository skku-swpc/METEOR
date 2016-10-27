#ifndef AUXILIARY_FIFOBUFFER_H
#define AUXILIARY_FIFOBUFFER_H

#include "Common.h"
#include "auxiliary/Queue.h"
#include "auxiliary/Vector.h"

typedef struct FifoBuffer {
  u4 pos_head;
  u4 pos_tail;
  Queue buffers;
  Vector freeBufs;
} FifoBuffer;

FifoBuffer auxFifoCreate();

void auxFifoDestroy(FifoBuffer* fb);

bool auxFifoEmpty(FifoBuffer* fb);

u4 auxFifoSize(FifoBuffer* fb);

u4 auxFifoGetBufferSize(FifoBuffer* fb);

char* auxFifoGetBuffer(FifoBuffer* fb);

void auxFifoReadBuffer(FifoBuffer* fb, char* buf, u4 bytes);

void auxFifoPopBytes(FifoBuffer* fb, u4 bytes);

void auxFifoPushData(FifoBuffer* fb, char* buf, u4 bytes);

#endif // AUXILIARY_FIFOBUFFER_H
