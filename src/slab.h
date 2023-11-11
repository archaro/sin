// Slab Allocator
// Alternative to malloc/free for uniform block sizes
#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// This is in kilobytes - the size of each slab
#define INITIAL_SLAB_SIZE 64

typedef struct SlabBlock SlabBlock;
typedef struct Slab Slab;
typedef struct Allocator Allocator;

struct SlabBlock {
  struct SlabBlock *next; // Pointer to the next block of memory
};

struct Slab {
  size_t block_size;
  SlabBlock *blocks;      // Linked list of memory blocks
  void *free_list;         // Pointer to the first free block
};

struct Allocator {
  Slab entry_slab;
  Slab hashtable_slab;
  Slab item_slab;
};

// Internal functions
void init_slab(Slab *slab, size_t block_size);
void destroy_slab(Slab *slab);
SlabBlock *allocate_slab_block(size_t block_size, size_t blocksCount);
void *allocate_from_slab(Slab *slab);
void deallocate_to_slab(Slab *slab, void *block);

