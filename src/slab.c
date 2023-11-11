#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "slab.h"
#include "item.h"

// Create the first slab of memory for a given block size.
// Used by the initAllocator() API call.
void init_slab(Slab *slab, size_t block_size) {
  slab->block_size = block_size;
  slab->blocks = allocate_slab_block(block_size, INITIAL_SLAB_SIZE * 1024);
  slab->free_list = slab->blocks ? (char*)slab->blocks + sizeof(SlabBlock) : NULL;
}

// Free all the blocks of a slab - called by destroyAllocator()
void destroy_slab(Slab *slab) {
  SlabBlock *block = slab->blocks;
  while (block) {
    SlabBlock *next = block->next;
    free(block);
    block = next;
  }
}

// Allocate a new slab of memory for this block size
SlabBlock *allocate_slab_block(size_t block_size, size_t blocksCount) {
  size_t totalSize = block_size * blocksCount;
  SlabBlock *block = (SlabBlock*)malloc(sizeof(SlabBlock) + totalSize);
  if (block) {
    block->next = NULL;
    void *memory = (char*)block + sizeof(SlabBlock);

    // Initialize free list within the block
    for (size_t i = 0; i < blocksCount - 1; i++) {
      void *currentBlock = (char*)memory + i * block_size;
      *((void**)currentBlock) = (char*)currentBlock + block_size;
    }
    *((void**)((char*)memory + (blocksCount - 1) * block_size)) = NULL;

    return block;
  }
  return NULL;
}

// Allocate a new chunk of memory - used by the allocate*() API calls
void *allocate_from_slab(Slab *slab) {
  if (slab->free_list == NULL) {
    // Slab is full, allocate a new block
    SlabBlock *newBlock = allocate_slab_block(slab->block_size, 
                                                    INITIAL_SLAB_SIZE * 1024);
    if (newBlock) {
      // Link the new block to the existing blocks
      newBlock->next = slab->blocks;
      slab->blocks = newBlock;
      slab->free_list = (char*)newBlock + sizeof(SlabBlock);
    } else {
      return NULL; // Allocation failed
    }
  }

  void *block = slab->free_list;
  slab->free_list = *((void**)block);
  return block;
}

// Return a block of memory to the slab - use by the deallocate() API calls
void deallocate_to_slab(Slab *slab, void *block) {
  *((void**)block) = slab->free_list;
  slab->free_list = block;
}

