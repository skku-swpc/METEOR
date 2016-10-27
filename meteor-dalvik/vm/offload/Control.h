#ifndef OFFLOAD_CONTROL_H
#define OFFLOAD_CONTROL_H

#include <arpa/inet.h>

#include "Common.h"
#include "Thread.h"

/* Check if the offload link is well connected (low latency). */
bool offWellConnected();

/* Check if the offload engine still has a connection to the tcpmux daemon.
 * Probably not much should need to use this. */
bool offConnected();

/* Write data to the fifo buffer of the parallel thread. */
void offSendMessage(struct Thread* self, const char* data, uint32_t amt);

/* Basic write functions for integers.  Just helper functions that rely on the
 * underlying offSendMessage implementation. */
#define SIZED_WRITE(sz, hton)                                               \
  INLINE void offWriteU##sz(struct Thread* self, u##sz val) {               \
    val = hton(val);                                                        \
    offSendMessage(self, (const char*)&val, sz);                            \
  }

SIZED_WRITE(1, );
SIZED_WRITE(2, htons);
SIZED_WRITE(4, htonl);
SIZED_WRITE(8, htonll);

/* Read from the message buffer blocking if necessary until the requested amount
 * of data comes in. */
void offReadBuffer(struct Thread* self, char* buf, u4 size);

//TODO: We can actually handle the common case without a lock if we're a bit
// cleverer or change the semantics of the fifo read functions.
#define SIZED_READ(sz, ntoh)                                                \
  INLINE u##sz offReadU##sz(struct Thread* self) {                          \
    u##sz result;                                                           \
    pthread_mutex_lock(&self->offBufferLock);                               \
    if(auxFifoGetBufferSize(&self->offReadBuffer) >= sz) {                  \
      memcpy(&result, auxFifoGetBuffer(&self->offReadBuffer), sz);          \
      auxFifoPopBytes(&self->offReadBuffer, sz);                            \
      pthread_mutex_unlock(&self->offBufferLock);                           \
    } else {                                                                \
      pthread_mutex_unlock(&self->offBufferLock);                           \
      offReadBuffer(self, (char*)&result, sz);                              \
    }                                                                       \
    return ntoh(result);                                                    \
  }

SIZED_READ(1, );
SIZED_READ(2, ntohs);
SIZED_READ(4, ntohl);
SIZED_READ(8, ntohll);

void offCorkStream(struct Thread* self);
void offUncorkStream(struct Thread* self);

/* Blocks until all pending data has been sent out of the offload engine. */
void offFlushStream(struct Thread* self);

void* offControlLoop(void* junk);

bool offControlStartup(int afterZygote);

void offControlShutdown();

#endif // OFFLOAD_CONTROL_H
