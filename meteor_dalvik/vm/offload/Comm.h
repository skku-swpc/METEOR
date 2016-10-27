#ifndef OFFLOAD_COMM_H
#define OFFLOAD_COMM_H

#include "Inlines.h"
#include "alloc/Heap.h"

#define COMM_CLASS_BID 0x3U
#define GET_ID_MASK(id) ((id) & 3U << 30U)
#define GET_ID_NUM(id) (((id) >> 30U) & 0x3U)
#define STRIP_ID_MASK(id) ((id) & ~(3U << 30U))
#define ADD_ID_MASK(id) ((id) | gDvm.idMask)

struct Thread;
struct Object;
struct ArrayObject;
struct ClassObject;
struct InstField;
struct StaticField;

typedef struct ObjectInfo {
  /* The pointer to the Object. */
  struct Object* obj;

  /* Tracks dirtiness of low field indexes. */
  u4 dirty;

  /* Tracks the dirtiness of high field indexes. */
  u4* bits;

  /* Used by sync to avoid un-needed notifies. */
  u4 remoteWaitCount;

  /* Tracks if the object is in the write queue already. */
  bool isQueued;

  /* Tracks if the current endpoint owns this object for locking. */
  bool isLockOwner;

  /* Tracks if the current endpoint owns this object's volatiles. */
  volatile bool isVolatileOwner;
} ObjectInfo;

/* Get the object associated with the passed identifier. */
struct Object* offIdToObject(u4 objId);

/* Get the auxiliary object info structure for the passed id. */
struct ObjectInfo* offIdObjectInfo(u4 objId);

/* Add tracked object to the offload engine. */
void offAddTrackedObject(struct Object* obj);

/* Register the proxy class. */
void offRegisterProxy(struct ClassObject* clazz, char* str,
                      struct ClassObject** interfaces, u4 isz);

/* Push tracked changes to the remote endpoint. */
void offSyncPush();
void offSyncPushDoIf(bool(*before_func)(void*), void* before_arg,
              void(*after_func)(void*, struct FifoBuffer*), void* after_arg);

/* Pulls changes from remote endpoint.  Returns true if the pull completed
 * successfully (the server did not disconnect). */
bool offSyncPull();
bool offSyncPullDo(void(*before_func)(void*), void* before_arg,
              void(*after_func)(void*, struct FifoBuffer*), void* after_arg);

/* Protect tracked objects from the garbage collector. */
void offGcMarkOffloadRefs(const GcSpec* spec, bool remark);

/* Do the track set trim operation. */
void offGcDoTrackTrim();

bool offCommStartup();

void offCommShutdown();

#endif // OFFLOAD_COMM_H
