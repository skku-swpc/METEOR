#ifndef OFFLOAD_METHODRULES
#define OFFLOAD_METHODRULES

#include "Common.h"

struct Method;

/* These access flags are otherwise meaningless to methods. */
#define ACC_OFFLOADABLE       0x100000

void offLoadNativeMethod(struct Method* method);

bool offMethodRulesStartup();

#endif // OFFLOAD_METHODRULES
