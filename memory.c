#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "debug.h"

void *reallocate(void *previous, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(previous);
    return NULL;
  }

  void *ret = realloc(previous, newSize);
  ASSERT_MEM(ret);
  return ret;
}
