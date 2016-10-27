#include "auxiliary/FifoBuffer.h"

#include <string.h>
#include <stdlib.h>

#define BUFFERSIZE (1 << 14) // 16Kb

FifoBuffer auxFifoCreate() {
  FifoBuffer fb;
  fb.pos_head = fb.pos_tail = 0;
  fb.buffers = auxQueueCreate();
  fb.freeBufs = auxVectorCreate(0);
  return fb;
}

void auxFifoDestroy(FifoBuffer* fb) {
  while(!auxQueueEmpty(&fb->buffers)) {
    free(auxQueuePop(&fb->buffers).v);
  }
  u4 i;
  for(i = 0; i < auxVectorSize(&fb->freeBufs); i++) {
    free(auxVectorGet(&fb->freeBufs, i).v);
  }
  auxQueueDestroy(&fb->buffers);
  auxVectorDestroy(&fb->freeBufs);
}

bool auxFifoEmpty(FifoBuffer* fb) {
  return auxQueueEmpty(&fb->buffers);
}

u4 auxFifoSize(FifoBuffer* fb) {
  if(auxQueueEmpty(&fb->buffers)) return 0;
  return BUFFERSIZE * (auxQueueSize(&fb->buffers) - 1) +
         (fb->pos_tail ? fb->pos_tail : BUFFERSIZE) - fb->pos_head;
}

u4 auxFifoGetBufferSize(FifoBuffer* fb) {
  u4 bsz = auxQueueSize(&fb->buffers);
  if(bsz == 0) return 0;
  if(bsz == 1 && fb->pos_tail != 0) return fb->pos_tail - fb->pos_head;
  return BUFFERSIZE - fb->pos_head;
}

char* auxFifoGetBuffer(FifoBuffer* fb) {
  return ((char*)auxQueuePeek(&fb->buffers).v) + fb->pos_head;
}

void auxFifoReadBuffer(FifoBuffer* fb, char* buf, u4 bytes) {
  while(bytes > 0) {
    u4 rbytes = auxFifoGetBufferSize(fb);
    assert(rbytes != 0 && "tried to read too much from buffer");
    if(rbytes > bytes) rbytes = bytes;
    memcpy(buf, auxFifoGetBuffer(fb), rbytes);
    auxFifoPopBytes(fb, rbytes);
    bytes -= rbytes;
    buf += rbytes;
  }
}

void auxFifoPopBytes(FifoBuffer* fb, u4 bytes) {
  if(fb->pos_head + bytes == BUFFERSIZE) {
    auxVectorPush(&fb->freeBufs, auxQueuePop(&fb->buffers));
    fb->pos_head = 0;
  } else {
    fb->pos_head += bytes;
    if(fb->pos_head == fb->pos_tail && auxQueueSize(&fb->buffers) == 1) {
      auxVectorPush(&fb->freeBufs, auxQueuePop(&fb->buffers));
      fb->pos_head = fb->pos_tail = 0;
    }
  }
}

void auxFifoPushData(FifoBuffer* fb, char* buf, u4 bytes) {
  while(bytes > 0) {
    u4 amt = fb->pos_tail + bytes < BUFFERSIZE ? bytes :
                                                 BUFFERSIZE - fb->pos_tail;
    if(fb->pos_tail == 0) {
      /* Need to push on a new buffer. */
      if(!auxVectorEmpty(&fb->freeBufs)) {
        auxQueuePush(&fb->buffers, auxVectorPop(&fb->freeBufs));
      } else {
        auxQueuePushV(&fb->buffers, malloc(BUFFERSIZE));
      }
    }
    memcpy(((char*)auxQueuePeekBack(&fb->buffers).v) + fb->pos_tail,
           buf, amt);
    fb->pos_tail = (fb->pos_tail + amt) & (BUFFERSIZE - 1);
    bytes -= amt;
    buf += amt;
  }
}
