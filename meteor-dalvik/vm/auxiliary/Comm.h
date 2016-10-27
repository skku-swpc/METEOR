#ifndef AUXILIARY_COMM_H
#define AUXILIARY_COMM_H

#include "Common.h"
#include "Inlines.h"

#define PRIMITIVE_DEX_ID 0x3FU

struct Object;

// Get the id associated with the passed object or COMM_INVALID_ID if there is
// none.
INLINE u4 auxObjectToId(struct Object* obj) {
  return obj ? obj->objId : COMM_INVALID_ID;
}

#endif // AUXILIARY_COMM_H
