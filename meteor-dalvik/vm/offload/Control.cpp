#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

/* <linux/tcp.h> was giving me a problem on some systems.  Copied below are the
 * definitions we need from it. */
#define TCP_NODELAY   1 /* Turn off Nagle's algorithm. */
#define TCP_CORK    3 /* Never send partially complete segments */

#include "Dalvik.h"

#define RTT_INFINITE 10*1000*1000 // 10 seconds

#define CONTROL_DIAGNOSTIC_PORT_S "5554"
#define CONTROL_TRANSPORT_PORT_S "5555"

#define MAX_CONTROL_VPACKET_SIZE (1<<16)

// TODO: Move compression only into tcpmux layer.
#define USE_COMPRESSION

#ifdef USE_COMPRESSION
#include "zlib.h"
#define Z_LEVEL Z_DEFAULT_COMPRESSION
#endif

#define SETOPT(s, opt, val) do { \
    int v = (val); \
    if(setsockopt((s), IPPROTO_TCP, (opt), (void*)&v, sizeof(int))) { \
      perror("setsockopt"); /* It should be ok. */ \
      dvmAbort(); \
    } \
  } while(0)

#define CHECK_RESULT(_str, _res)                                            \
    if((_res) == -1) {                                                      \
      ALOGW("Unexpected failure with %s (%s)", (_str), strerror(errno));     \
      break;                                                                \
    } else if((_res) == 0) {                                                \
      break;                                                                \
    }

typedef struct MsgHeader {
  u4 id;
  u4 sz;
} MsgHeader;

static u4 readFdFull(int fd) {
  ssize_t res;
  u4 ret = 0; u4 amt = 0;
  while(amt < sizeof(ret)) {
    res = read(gDvm.offNetPipe[0], ((char*)&ret) + amt, sizeof(ret) - amt);
    CHECK_RESULT("read", res);
    if(res < 0) {
      perror("read");
      dvmAbort();
    }
    amt += res;
  }
  return ret;
}

static void writeFdFull(int fd, u4 val) {
  ssize_t res;
  u4 amt = 0;
  while(amt < sizeof(val)) {
    res = write(fd, ((char*)&val) + amt, sizeof(val) - amt);
    if(res < 0) {
      perror("write");
      dvmAbort();
    }
    amt += res;
  }
}

static int signalThread(void* pthread, void* arg) {
  UNUSED_PARAMETER(arg);

  Thread* thread = (Thread*)pthread;
  pthread_mutex_lock(&thread->offBufferLock); {
    pthread_cond_signal(&thread->offBufferCond);
  } pthread_mutex_unlock(&thread->offBufferLock);
  return 0;
}

static void message_loop(int s) {
  ALOGI("Starting message loop");

  u1 magic_value = 0x55;
  if(1 != write(s, &magic_value, 1)) return;
  if(1 != read(s, &magic_value, 1)) return;
  if(magic_value != 0x55) {
    ALOGW("Bad magic value from server");
    return;
  }
  gDvm.offConnected = true;
  gDvm.offRecovered = false;

  int res;
  Queue wthreads = auxQueueCreate();
  Thread* wthread;

  MsgHeader rhdr;
  int rst = 0; u4 rsz = sizeof(rhdr); u4 rpos = 0;
  char rbuf[2*MAX_CONTROL_VPACKET_SIZE];

  MsgHeader whdr;
  int wst = -1; u4 wsz = sizeof(whdr); u4 wpos = 0;
  u4 owsz = -1;
  char* wbuf = NULL;

#ifdef USE_COMPRESSION
  char rbuftmp[2*MAX_CONTROL_VPACKET_SIZE];
  char wtmpbuf[2*MAX_CONTROL_VPACKET_SIZE];

  z_stream wstrm;
  wstrm.zalloc = Z_NULL; wstrm.zfree = Z_NULL; wstrm.opaque = Z_NULL;
  deflateInit(&wstrm, Z_LEVEL);

  z_stream rstrm;
  rstrm.zalloc = Z_NULL; rstrm.zfree = Z_NULL; rstrm.opaque = Z_NULL;
  inflateInit(&rstrm);
#endif

  // These four variables are for debugging purposes.
  long long read_bytes = 0;
  long long read_acked_bytes = 0;
  long long sent_bytes = 0;
  long long sent_acked_bytes = 0;
  long long cread_bytes = 0;
  long long cread_acked_bytes = 0;
  long long csent_bytes = 0;
  long long csent_acked_bytes = 0;

  SETOPT(s, TCP_CORK, 1);

  int nfd = s < gDvm.offNetPipe[0] ? gDvm.offNetPipe[0] + 1 : s + 1;
  while(1) {
    if(wst == -1 && !auxQueueEmpty(&wthreads)) {
      Thread* wthread = (Thread*)auxQueuePeek(&wthreads).v;
      pthread_mutex_lock(&wthread->offBufferLock); {
        wst = 0;
        wsz = sizeof(whdr);
        wpos = 0;
        whdr.id = htonl(wthread->threadId);

        whdr.sz = auxFifoGetBufferSize(&wthread->offWriteBuffer);
        whdr.sz = htonl(whdr.sz < MAX_CONTROL_VPACKET_SIZE ?
                        whdr.sz : MAX_CONTROL_VPACKET_SIZE);
        owsz = ntohl(whdr.sz);

        wbuf = auxFifoGetBuffer(&wthread->offWriteBuffer);
      } pthread_mutex_unlock(&wthread->offBufferLock);

      sent_bytes += ntohl(whdr.sz);
#ifdef USE_COMPRESSION
      /* Perform the compression with zlib. */
      wstrm.avail_in = ntohl(whdr.sz);
      wstrm.next_in = (unsigned char*)wbuf;
      wstrm.avail_out = sizeof(wtmpbuf);
      wstrm.next_out = (unsigned char*)wtmpbuf;
      deflate(&wstrm, Z_SYNC_FLUSH);
      wbuf = wtmpbuf;
      whdr.sz = htonl(sizeof(wtmpbuf) - wstrm.avail_out);
#endif
      csent_bytes += ntohl(whdr.sz);
    } else if(wst == -1 && sent_bytes != sent_acked_bytes) {
      ALOGI("WRITE[b, db, cb, dcb] = [%lld, %lld, %lld, %lld]",
           sent_bytes, sent_bytes - sent_acked_bytes,
           csent_bytes, csent_bytes - csent_acked_bytes);
      sent_acked_bytes = sent_bytes;
      csent_acked_bytes = csent_bytes;
    }

    fd_set rdst; FD_ZERO(&rdst);
    fd_set wrst; FD_ZERO(&wrst);
    FD_SET(s, &rdst);
    FD_SET(gDvm.offNetPipe[0], &rdst);
    if(!auxQueueEmpty(&wthreads)) {
      FD_SET(s, &wrst);
    }
    res = select(nfd, &rdst, &wrst, NULL, NULL);
    CHECK_RESULT("select", res);

    if(FD_ISSET(s, &rdst)) {
      if(rst == 0) {
        res = read(s, ((char*)&rhdr) + rpos, rsz - rpos);
        CHECK_RESULT("read", res);
        rpos += res;

        if(rpos == rsz) {
          rhdr.id = ntohl(rhdr.id);
          rhdr.sz = ntohl(rhdr.sz);
          rst = 1;
          rpos = 0;
          rsz = rhdr.sz;

          if(rsz > MAX_CONTROL_VPACKET_SIZE) {
            ALOGE("Invalid message size %d", rsz);
            dvmAbort();
          }
        }
      } else if(rst == 1) {
        res = read(s, rbuf + rpos, rsz - rpos);
        CHECK_RESULT("read", res);
        rpos += res;

        if(rpos == rsz) {
#ifdef USE_COMPRESSION
          /* Do the decompression and push the data to the thread. */
          rstrm.avail_in = rsz;
          rstrm.next_in = (unsigned char*)rbuf;
          rstrm.avail_out = sizeof(rbuftmp);
          rstrm.next_out = (unsigned char*)rbuftmp;
          inflate(&rstrm, Z_SYNC_FLUSH);
#endif

          cread_bytes += rsz;
          read_bytes += sizeof(rbuftmp) - rstrm.avail_out;
          if(read_bytes - read_acked_bytes > (1<<10)) {
            ALOGI("READ [b, db, cb, dcb] = [%lld, %lld, %lld, %lld]",
                 read_bytes, read_bytes - read_acked_bytes,
                 cread_bytes, cread_bytes - cread_acked_bytes);
            read_acked_bytes = read_bytes;
            cread_acked_bytes = cread_bytes;
          }

#ifdef USE_COMPRESSION
          if(rstrm.avail_out != sizeof(rbuftmp)) {
#else
          if(rsz != 0) {
#endif
            Thread* rthread = rhdr.id ? offIdToThread(rhdr.id) :
                                        &gDvm.gcThreadContext;
            pthread_mutex_lock(&rthread->offBufferLock); {
#ifdef USE_COMPRESSION
              auxFifoPushData(&rthread->offReadBuffer, rbuftmp,
                              sizeof(rbuftmp) - rstrm.avail_out);
#else
              auxFifoPushData(&rthread->offReadBuffer, rbuf, rsz);
#endif
              pthread_cond_signal(&rthread->offBufferCond);
            } pthread_mutex_unlock(&rthread->offBufferLock);
          }

          rst = 0;
          rpos = 0;
          rsz = sizeof(MsgHeader);
        }
      }
    }
    if(FD_ISSET(s, &wrst)) {
      if(wst == 0) {
        res = write(s, ((char*)&whdr) + wpos, wsz - wpos);
        CHECK_RESULT("write", res);
        wpos += res;

        if(wpos == wsz) {
          wsz = ntohl(whdr.sz);
          wpos = 0;
          wst = 1;
        }
      } else if(wst == 1) {
        res = write(s, wbuf, wsz - wpos);
        CHECK_RESULT("write", res);
        wbuf += res;
        wpos += res;

        if(wpos == wsz) {
          wst = -1;

          wthread = (Thread*)auxQueuePop(&wthreads).v;
          pthread_mutex_lock(&wthread->offBufferLock); {
            auxFifoPopBytes(&wthread->offWriteBuffer, owsz);
            if(auxFifoEmpty(&wthread->offWriteBuffer)) {
              /* If the write buffer is empty signal the thread so if it was
               * waiting for a flush it will wake up. */
              pthread_cond_signal(&wthread->offBufferCond);
            } else {
              auxQueuePushV(&wthreads, wthread);
            }
          } pthread_mutex_unlock(&wthread->offBufferLock);

          if(auxQueueEmpty(&wthreads) && wthread->offCorkLevel == 0) {
            /* We have nothing more to send right now.  Let any partial packets
             * go over the wire now. */
            SETOPT(s, TCP_NODELAY, 1);
            SETOPT(s, TCP_NODELAY, 0);
          }
        }
      }
    }
    if(FD_ISSET(gDvm.offNetPipe[0], &rdst)) {
      wthread = (Thread*)readFdFull(gDvm.offNetPipe[0]);
      if(wthread == NULL) {
        /* We have been signaled to bail. */
        return;
      }
      auxQueuePushV(&wthreads, wthread);
    }
  }

  auxQueueDestroy(&wthreads);

  /* Singal that we're no longer connected and wake up anybody who is waiting
   * for the remote endpoint to do something. */
  gDvm.offConnected = false;
  gDvm.offNetStatTime = 0;
  gDvm.offNetRTT = gDvm.offNetRTTVar = RTT_INFINITE;

  /* Wake up those waiting on data. */
  dvmHashTableLock(gDvm.offThreadTable); {
    dvmHashForeach(gDvm.offThreadTable, signalThread, NULL);
  } dvmHashTableUnlock(gDvm.offThreadTable);

  /* Wake up those waiting on their turn to pull. */
  pthread_mutex_lock(&gDvm.offCommLock); {
    pthread_cond_broadcast(&gDvm.offPullCond);
  } pthread_mutex_unlock(&gDvm.offCommLock);

  offRecoveryWaitForClearance(NULL);
}

void* offControlLoop(void* junk) {
  union {
    struct sockaddr_in addrin;
    struct sockaddr addr;
  } addr;
  
  int iter;
  int s = -1;
  for(iter = 0; ; iter = iter < 7 ? iter + 1 : 7) {
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == -1) {
      perror("socket");
      return NULL;
    }
    
    if(!gDvm.isServer) {
      struct timespec req;
      req.tv_sec = 1 << iter;
      req.tv_nsec = 0;
      nanosleep(&req, NULL);

      struct addrinfo* rp;
      for(rp = gDvm.offTransportAddr; rp; rp = rp->ai_next) {
        if(!connect(s, rp->ai_addr, rp->ai_addrlen)) {
          break;
        }
      }
      if(!rp) {
        close(s); s = -1;
        continue;
      }
    } else {
      int one = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

      const char* listen_port = getenv("OFF_LISTEN_PORT");
      listen_port = listen_port ? listen_port : CONTROL_TRANSPORT_PORT_S;
      
      addr.addrin.sin_family = AF_INET;
      addr.addrin.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.addrin.sin_port = htons(atoi(listen_port));
      if(bind(s, &addr.addr, sizeof(addr.addrin)) < 0) {
        perror("bind");
        return NULL;
      }
      if(listen(s, 5) < 0) {
        perror("listen");
        return NULL;
      }
      ALOGI("Ready to accept connections on %s", listen_port);
      while(1) {
        union {
          struct sockaddr_in addrin;
          struct sockaddr addr;
        } cli_addr;
        socklen_t cli_len = sizeof(cli_addr.addrin);
        int s_cli = accept(s, &cli_addr.addr, &cli_len);
        if(s_cli == -1) {
          perror("accept");
        } else {
          int pid = fork();
          if(pid == -1) {
            perror("fork");
          } else if(pid == 0) {
            close(s);
            s = s_cli;
            break;
          }
          close(s_cli);
        }
      }
    }

    /* Create the write event pipe.  We don't want to do it earlier before the
     * server/zygote forks as the messages will cross processes. */
    pipe(gDvm.offNetPipe);
    message_loop(s);
    close(gDvm.offNetPipe[0]);
    close(gDvm.offNetPipe[1]);

    /* Cleanup all transport state for the client.  Otherwise just quit. */
    if(gDvm.isServer) {
      goto bail;
    }

    if(gDvm.offControlShutdown) {
      goto bail;
    } else {
      ALOGW("Lost connection to server\n");
    }

    close(s); s = -1;
  }

bail:
  if(s != -1) close(s);

  return NULL;
}

bool offWellConnected() {
  if(!gDvm.offConnected) return false;
  
  u8 nw = dvmGetRelativeTimeUsec();
  if(gDvm.offNetStatTime == 0 || nw - gDvm.offNetStatTime > 1000000 * 60) {
    int s = -1;
    size_t pos = 0;
    struct {
      u4 rtt;
      u4 rttvar;
    } payload;

    pthread_mutex_lock(&gDvm.offNetStatLock);
    if(!(gDvm.offNetStatTime == 0 || nw - gDvm.offNetStatTime > 1000000 * 60)) {
      goto bail;
    }

    gDvm.offNetStatTime = nw;
    gDvm.offNetRTT = RTT_INFINITE;
    gDvm.offNetRTTVar = RTT_INFINITE;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == -1) goto bail;

    if(connect(s, gDvm.offDiagnosticAddr->ai_addr,
                  gDvm.offDiagnosticAddr->ai_addrlen)) {
      /* We'll just assume that there is no controller for now.  This is the
       * case when we're just directly connecting. */
      gDvm.offNetStatTime = dvmGetRelativeTimeUsec();
      gDvm.offNetRTT = gDvm.offNetRTTVar = 10000;
      goto bail;
    }

    while(pos < sizeof(payload)) {
      ssize_t amt = read(s, (char*)&payload + pos, sizeof(payload) - pos);
      if(amt <= 0) goto bail;
      pos += amt;
    }

    gDvm.offNetStatTime = dvmGetRelativeTimeUsec();
    gDvm.offNetRTT = ntohl(payload.rtt);
    gDvm.offNetRTTVar = ntohl(payload.rttvar);

/*
    if(gDvm.offNetRTT < RTT_INFINITE) {
      gDvm.offNetRTT = gDvm.offNetRTTVar = 10000;
    }
*/
    ALOGI("Got RTT fix %u (var %u)", gDvm.offNetRTT, gDvm.offNetRTTVar);

bail:
    if(s != -1) close(s);
    pthread_mutex_unlock(&gDvm.offNetStatLock);
  }

  return gDvm.offNetRTT != RTT_INFINITE;
  //return gDvm.offNetRTT + 2 * gDvm.offNetRTTVar < 150 * 1000;
}

bool offConnected() {
  return gDvm.offConnected;
}

void offSendMessage(Thread* self, const char* data, uint32_t amt) {
  if(amt == 0) return;

  bool empty;
  pthread_mutex_lock(&self->offBufferLock);
  empty = auxFifoEmpty(&self->offWriteBuffer);

  /* Now just write the buffer to the fifo and we're done! */
  auxFifoPushData(&self->offWriteBuffer, (char*)data, (u4)amt);
  pthread_mutex_unlock(&self->offBufferLock);

  if(empty) {
    /* The thread is now transitioning from a state of having nothing to write
     * to a state of having something to write. Go ahead and write the thread
     * to the writable thread pipe. */
    writeFdFull(gDvm.offNetPipe[1], (u4)self);
  }
}

void offReadBuffer(Thread* self, char* buf, u4 size) {
  if(size == 0) return;

  pthread_mutex_lock(&self->offBufferLock);
  while(size != 0) {
    if(!gDvm.offConnected) {
      memset(buf, 0x55, size);
      size = 0;
    } else if(!auxFifoEmpty(&self->offReadBuffer)) {
      /* Grab the next chunk out of the fifo. */
      u4 bytes = auxFifoGetBufferSize(&self->offReadBuffer);
      bytes = size < bytes ? size : bytes;
      memcpy(buf, auxFifoGetBuffer(&self->offReadBuffer), bytes);
      auxFifoPopBytes(&self->offReadBuffer, bytes);

      buf += bytes;
      size -= bytes;
    } else {
      /* Need to wait for new data to come in. */
      ThreadStatus status = dvmChangeStatus(self, THREAD_VMWAIT);
      pthread_cond_wait(&self->offBufferCond, &self->offBufferLock);

      /* We shouldn't change our status to THREAD_RUNNING while holding this
       * lock. */
      pthread_mutex_unlock(&self->offBufferLock);
      dvmChangeStatus(self, status);
      pthread_mutex_lock(&self->offBufferLock);
    }
  }
  pthread_mutex_unlock(&self->offBufferLock);
}

void offCorkStream(Thread* self) {
  pthread_mutex_lock(&self->offBufferLock);
  self->offCorkLevel++;
  pthread_mutex_unlock(&self->offBufferLock);
}

void offUncorkStream(Thread* self) {
  pthread_mutex_lock(&self->offBufferLock);
  self->offCorkLevel--;
  pthread_mutex_unlock(&self->offBufferLock);
}

void offFlushStream(Thread* self) {
  ThreadStatus status = dvmChangeStatus(self, THREAD_VMWAIT);
  pthread_mutex_lock(&self->offBufferLock); {
    while(!auxFifoEmpty(&self->offWriteBuffer) && gDvm.offConnected) {
      pthread_cond_wait(&self->offBufferCond, &self->offBufferLock);
    }
  } pthread_mutex_unlock(&self->offBufferLock);
  dvmChangeStatus(self, status);
}

bool offControlStartup(int afterZygote) {
  bool res;
  if(!afterZygote) {
    gDvm.offRecovered = true;
    gDvm.offConnected = false;
    gDvm.offControlShutdown = false;

    const char* env_server = getenv("OFF_SERVER");
    gDvm.isServer = env_server && !strcmp("1", env_server);

    res = offThreadingStartup() &&
          offDexLoaderStartup() && offCommStartup() &&
          offSyncStartup() && offMethodRulesStartup() &&
          offRecoveryStartup();
  } else {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    if(getaddrinfo("127.0.0.1", CONTROL_TRANSPORT_PORT_S, &hints,
                   &gDvm.offTransportAddr) ||
       getaddrinfo("127.0.0.1", CONTROL_DIAGNOSTIC_PORT_S, &hints,
                   &gDvm.offDiagnosticAddr)) {
      ALOGE("Could not find transport and diagnostic addresses");
      return false;
    }

    gDvm.offNetStatTime = 0;
    gDvm.offNetRTT = gDvm.offNetRTTVar = RTT_INFINITE;
    pthread_mutex_init(&gDvm.offNetStatLock, NULL);
    
    res = offEngineStartup();
    if(res && !gDvm.offDisabled) {
      res = gDvm.offDisabled || gDvm.isServer ||
            !pthread_create(&gDvm.offControlThread, NULL, offControlLoop, NULL);
    }
  }
  return res;
}

void offControlShutdown() {
  gDvm.offControlShutdown = true;
  if(gDvm.offConnected) {
    /* If we never connected there's nothing really to clean up anyway. */
    writeFdFull(gDvm.offNetPipe[1], 0);
    pthread_join(gDvm.offControlThread, NULL);
  }

  offSyncShutdown();
  offEngineShutdown();
  offCommShutdown();
  offDexLoaderShutdown();
  offThreadingShutdown();
  offRecoveryShutdown();

  pthread_mutex_destroy(&gDvm.offNetStatLock);
}
