// The memory manager

#include <stdlib.h>

#include "memory.h"
#include "log.h"

void* reallocate(void* ptr, size_t oldcount, size_t newcount) {
  // This function handles all memory allocation, reallocation, and release.
  // oldcount 	newcount 	  action
  //   0 	      not 0       allocate new block of memory (malloc)
  // not 0       	0 	      free memory (free)
  // not 0      < oldcount 	shrink existing allocation (realloc)
  // not 0      > oldcount 	grow existing allocation (realloc)
  // everything else is undefined

  if (newcount == 0) {
    free(ptr);
    return NULL;
  }

  // realloc() with ptr == NULL behaves like malloc().
  void *res = realloc(ptr, newcount);
  if (res == NULL) {
    logerr("Unable to allocate memory.  This is bad.");
    exit(EXIT_FAILURE);
  }
  return res;

  // We don't use oldcount yet.  But we will.
}
