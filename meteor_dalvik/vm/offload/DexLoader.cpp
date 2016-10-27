#include "Dalvik.h"
#include "libdex/OptInvocation.h"
#include "alloc/HeapInternal.h"
#include "alloc/MarkSweep.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

static
void makeClassLoader(DvmDex* pDvmDex) {
  // This is sort of a hack but much better than what existed before.  We need
  // to create a DexClassLoader that will load from the passed DvmDex object.
  // This involves filling out internal java structures to do our bidding and
  // is thus tied to the specific implementation of
  // dalvik.system.DexClassLoader and dalvik.system.DexFile.  Note that I
  // haven't changed these classes in any way.
  Thread* self = dvmThreadSelf();
  ++self->offProtection;

  // Load the required classes.
  ClassObject* loaderClazz =
      dvmFindSystemClass("Ldalvik/system/DexClassLoader;");
  ClassObject* classLoaderClazz =
      dvmFindSystemClass("Ljava/lang/ClassLoader;");
  ClassObject* fileClazz = dvmFindSystemClass("Ljava/io/File;");
  ClassObject* fileArrayClazz = dvmFindArrayClassForElement(fileClazz);
  ClassObject* zipClazz = dvmFindSystemClass("Ljava/util/zip/ZipFile;");
  ClassObject* zipArrayClazz = dvmFindArrayClassForElement(zipClazz);
  ClassObject* dexClazz = dvmFindSystemClass("Ldalvik/system/DexFile;");
  ClassObject* dexArrayClazz = dvmFindArrayClassForElement(dexClazz);
  
  // Create the 'magic cookie' for the dex file.
  DexOrJar* pDexOrJar = (DexOrJar*)calloc(1, sizeof(DexOrJar));
  pDexOrJar->isDex = true;
  pDexOrJar->okayToFree = false;
  pDexOrJar->pRawDexFile = (RawDexFile*)calloc(1, sizeof(RawDexFile));
  pDexOrJar->pRawDexFile->pDvmDex = pDvmDex;

  // Create and fill in fields of a dex file object.
  Object* dexFile = dvmAllocObject(dexClazz, ALLOC_DEFAULT);
  InstField* fld,* efld;
  for(fld = dexClazz->ifields,
      efld = dexClazz->ifields + dexClazz->ifieldCount; fld != efld; ++fld) {
    if(!strcmp("mCookie", fld->name)) {
      dvmSetFieldInt(dexFile, fld->byteOffset, (s4)pDexOrJar);
    }
  }

  // Create and fill in fields of a dex class loader object.
  Object* classLoader = dvmAllocObject(loaderClazz, ALLOC_DEFAULT);

  // We can let the superclass java.lang.ClassLoader load like normal.
  JValue unused;
  Method* loaderConstr = dvmFindDirectMethodByDescriptor(
      classLoaderClazz, "<init>", "()V");
  dvmCallMethod(dvmThreadSelf(), loaderConstr, classLoader, &unused);

  // TODO: Might want to set mFiles, mZips, mLibPaths to something.
  for(fld = loaderClazz->ifields, efld = loaderClazz->ifields +
          loaderClazz->ifieldCount; fld != efld; ++fld) {
    if(!strcmp("mFiles", fld->name)) {
      Object* obj = (Object*)
          dvmAllocArrayByClass(fileArrayClazz, 1, ALLOC_DEFAULT);
      dvmSetFieldObject(classLoader, fld->byteOffset, obj);
      dvmReleaseTrackedAlloc(obj, self);
    } else if(!strcmp("mZips", fld->name)) {
      Object* obj =
          (Object*)dvmAllocArrayByClass(zipArrayClazz, 1, ALLOC_DEFAULT);
      dvmSetFieldObject(classLoader, fld->byteOffset, obj);
      dvmReleaseTrackedAlloc(obj, self);
    } else if(!strcmp("mDexs", fld->name)) {
      ArrayObject* xdex = dvmAllocArrayByClass(dexArrayClazz, 1, ALLOC_DEFAULT);
      dvmSetObjectArrayElement(xdex, 0, dexFile);
      dvmSetFieldObject(classLoader, fld->byteOffset, (Object*)xdex);
      dvmReleaseTrackedAlloc((Object*)xdex, self);
    }
  }

  pDvmDex->classLoader = classLoader;
  dvmReleaseTrackedAlloc(dexFile, self);
  dvmReleaseTrackedAlloc(classLoader, self);

  --self->offProtection;
}

// Get the path to the cached dex file for a given hash.
static int getCacheFile(char* path, u4 size, bool tmp, u1* hash) {
  char* BASE_PATH = getenv("OFFLOAD_DEX_CACHE");
  u4 basePathLen = strlen(BASE_PATH);
  if(basePathLen + kSHA1DigestLen * 2 + 6 >= size) return 0;
  memcpy(path, BASE_PATH, basePathLen);
  path[basePathLen] = '/';
  u4 i;
  for(i = 0; i < kSHA1DigestLen; i++) {
    sprintf(path + basePathLen + 1 + 2 * i, "%02x", hash[i]);
  }
  if(tmp) {
    sprintf(path + basePathLen + 1 + kSHA1DigestLen * 2, "%s", ".tmp");
  }
  return 1;
}

// Opens the cached version of the dex file with the passed signature and
// returns a file descriptor for it.  Returns -1 on failure.
static int openCachedDex(u1* hash, bool tmp) {
  char path[PATH_MAX];
  if(getCacheFile(path, sizeof(path), tmp, hash)) {
    return open(path, tmp ? O_CREAT|O_WRONLY|O_TRUNC : O_RDONLY, 0664);
  } else {
    return -1;
  }
}

static int moveTmpAndOpen(u1* hash) {
  char path[PATH_MAX];
  char pathtmp[PATH_MAX];
  if(getCacheFile(path, sizeof(path), false, hash) &&
     getCacheFile(pathtmp, sizeof(pathtmp), true, hash)) {
    if(link(pathtmp, path)) {
      perror("moveTmpAndOpen (link)");
      return -1;
    }
    if(unlink(pathtmp)) {
      perror("moveTmpAndOpen (unlink)");
      return -1;
    }
  }
  return open(path, O_RDONLY);
}

void offRegisterDex(DvmDex* pDvmDex, Object* loader, const char* cacheFile) {
  u4 dexId;
  pthread_mutex_lock(&gDvm.dexLoadLock); {
    dexId = auxVectorSize(&gDvm.dexList);
    pDvmDex->id = dexId;
    pDvmDex->classLoader = loader;
    pDvmDex->cacheFile = strdup(cacheFile);
    auxVectorPushV(&gDvm.dexList, pDvmDex);

    // TODO: If we want to let the server load in dex files we need to make this
    // smarter to know if the dex file was loaded remotely so we don't just
    // keep pushing the same dex files back and forth.
    if(!gDvm.initializing && !gDvm.isServer) {
      auxVectorPushV(&gDvm.dexPushList, pDvmDex);
    } else {
      ++gDvm.dexBootstrapCount;
    }
  } pthread_mutex_unlock(&gDvm.dexLoadLock);
}

void offPushDexFiles(Thread* self) {
  bool doPush = false;
  Vector vamp;
  memset(&vamp, 0, sizeof(vamp)); /* memset to get rid of warning. */

ALOGI("PUSHHY %d", auxVectorSize(&gDvm.dexList));
  pthread_mutex_lock(&gDvm.dexLoadLock); {
    /* If someone is already pushing out dex files we need to wait until it's
     * completed before we move on */
    while(gDvm.dexPushing) {
      ThreadStatus oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);
      pthread_cond_wait(&gDvm.dexPushedCond, &gDvm.dexLoadLock);
      dvmChangeStatus(self, oldStatus);
    }

    /* Check if there are things to push out ourselves. */
    doPush = !auxVectorEmpty(&gDvm.dexPushList);
    if(doPush) {
      vamp = gDvm.dexPushList;
      gDvm.dexPushing = true;
      gDvm.dexPushList = auxVectorCreate(0);
    }
  } pthread_mutex_unlock(&gDvm.dexLoadLock);

  if(doPush) {
    u4 i;
    for(i = 0; i < auxVectorSize(&vamp); i++) {
ALOGI("PUSHING DEX");
      /* Send a dex load message along with the dex id and signature so the
       * remote end can load it if it already owns the dex file. */
      DvmDex* pDvmDex = (DvmDex*)auxVectorGet(&vamp, i).v;
      offWriteU4(self, offDexToId(pDvmDex));
      offSendMessage(self, (char*)pDvmDex->pHeader->signature, kSHA1DigestLen);
      offThreadWaitForResume(self);
    }

    pthread_mutex_lock(&gDvm.dexLoadLock); {
      gDvm.dexPushing = false;
      pthread_cond_broadcast(&gDvm.dexPushedCond);
    } pthread_mutex_unlock(&gDvm.dexLoadLock);
    auxVectorDestroy(&vamp);
  }
  offWriteU4(self, (u4)-1);
}

void offPullDexFiles(Thread* self) {
  u4 dexId;
  for(dexId = offReadU4(self); dexId != (u4)-1; dexId = offReadU4(self)) {
    u1 hash[kSHA1DigestLen];
    offReadBuffer(self, (char*)hash, sizeof(hash));
    if(!gDvm.offConnected) return;

ALOGI("READ DEX %d", dexId);

    DvmDex* result = NULL;
    int fd = openCachedDex(hash, false);
    if(fd != -1) {
      /* Hooray we have the dex file cached, load that! */
      dvmDexFileOpenFromFd(fd, &result);
      close(fd);
    } else {
      /* That failed... now we need to request the whole dex file. */
      offWriteU1(self, OFF_ACTION_DEX_QUERYDEX);
      offWriteU4(self, dexId);
      offThreadWaitForResume(self);

      char buf[512];
      u4 length = offReadU4(self);
      if(!gDvm.offConnected) return;

      fd = openCachedDex(hash, true);
      while(length > 0) {
        u4 amt = length < sizeof(buf) ? length : sizeof(buf);
        offReadBuffer(self, buf, amt);
        if(!gDvm.offConnected) {
          close(fd);
          return;
        }
        
        u4 wamt = 0;
        while(wamt < amt) {
          ssize_t res = write(fd, buf + wamt, amt - wamt);
          if(res < 0) {
            ALOGE("Error loading dex from client");
            dvmAbort();
          }
          wamt += res;
        }
        length -= amt;
      }
      close(fd);

      offWriteU1(self, OFF_ACTION_RESUME);

      fd = moveTmpAndOpen(hash);
      dvmDexFileOpenFromFd(fd, &result);
      close(fd);
    }
    assert(result && "failed to load dex file");
    
    result->id = dexId;
    makeClassLoader(result);
    pthread_mutex_lock(&gDvm.dexLoadLock); {
      if(dexId >= auxVectorSize(&gDvm.dexList)) {
        auxVectorResize(&gDvm.dexList, dexId + 1);
      }
      auxVectorSetV(&gDvm.dexList, dexId, result);
    } pthread_mutex_unlock(&gDvm.dexLoadLock);

    offWriteU1(self, OFF_ACTION_RESUME);
  }
}

DvmDex* offIdToDex(u4 id) {
  DvmDex* result = (DvmDex*)auxVectorGet(&gDvm.dexList, id).v;
  assert(id < auxVectorSize(&gDvm.dexList) && result);
  return result;
}

u4 offDexToId(DvmDex* pDvmDex) {
  return pDvmDex->id;
}

void offPerformQueryDex(Thread* self) {
  u4 dexId = offReadU4(self);
  if(!gDvm.offConnected) return;
  DvmDex* dex = offIdToDex(dexId);
  assert(dex->cacheFile != NULL);

  int fd = open(dex->cacheFile, O_RDONLY);
  assert(fd != -1);

  MemMapping mMap;
  int res = sysMapFileInShmemReadOnly(fd, &mMap);
  if(res) dvmAbort();
  close(fd);

  offWriteU1(self, OFF_ACTION_RESUME);
  offWriteU4(self, dex->memMap.length);
  offSendMessage(self, (char*)mMap.addr, mMap.length);
  offThreadWaitForResume(self);

  sysReleaseShmem(&mMap);
}

void offGcMarkDexRefs(bool remark) {
  GcMarkContext* ctx = &gDvm.gcHeap->markContext;
  pthread_mutex_lock(&gDvm.dexLoadLock); {
    u4 i;
    for(i = 0; i < auxVectorSize(&gDvm.dexList); ++i) {
      DvmDex* pDvmDex = (DvmDex*)auxVectorGet(&gDvm.dexList, i).v;
      if(pDvmDex->classLoader) {
        dvmMarkObjectOnStack(pDvmDex->classLoader, ctx);
      }
    }
  } pthread_mutex_unlock(&gDvm.dexLoadLock);
}

bool offDexLoaderStartup() {
  if(pthread_mutex_init(&gDvm.dexLoadLock, NULL)) {
    ALOGE("Failed to create dex loader mutex");
    return false;
  }
  if(pthread_cond_init(&gDvm.dexPushedCond, NULL)) {
    ALOGE("Failed to create dex push condition variable");
    return false;
  }
  gDvm.dexList = auxVectorCreate(6);
  gDvm.dexPushList = auxVectorCreate(1);
  gDvm.dexPushing = false;
  gDvm.dexBootstrapCount = 0;
  return true;
}

void offDexLoaderShutdown() {
  pthread_mutex_destroy(&gDvm.dexLoadLock);
  pthread_cond_destroy(&gDvm.dexPushedCond);
  auxVectorDestroy(&gDvm.dexList);
  auxVectorDestroy(&gDvm.dexPushList);
}
