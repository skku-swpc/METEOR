#ifndef OFFLOAD_DEXLOADER_H
#define OFFLOAD_DEXLOADER_H

#include "Common.h"

struct DvmDex;
struct Thread;

/* Add the given dex to our dex list and write its identifier to it. */
void offRegisterDex(struct DvmDex* pDvmDex, Object* loader,
                    const char* fileName);

/* Get the DvmDex entry associated with the passed id. */
DvmDex* offIdToDex(u4 id);

/* Get the id associated with the passed DvmDex entry. */
u4 offDexToId(struct DvmDex* pDvmDex);

/* Pushes any dex files that haven't already been sent to the other side and
 * waits for any pending dex files to finish sending. */
void offPushDexFiles(Thread* self);

void offPullDexFiles(struct Thread* self);

void offPerformQueryDex(struct Thread* self);

void offGcMarkDexRefs(bool remark);

bool offDexLoaderStartup();
void offDexLoaderShutdown();

#endif // OFFLOAD_DEXLOADER_H
