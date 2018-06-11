#include <stdlib.h>

#include "common.h"
#include "memory.h"

void *reallocate(void *previous, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(previous);
    return NULL;
  }

  return realloc(previous, newSize);
}
