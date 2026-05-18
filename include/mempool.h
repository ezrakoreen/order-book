#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct MemoryPoolBlock {
    void *storage;
    size_t used;
    struct MemoryPoolBlock *next;
} MemoryPoolBlock;

typedef struct MemoryPool {
    size_t item_size;
    size_t items_per_block;
    void *free_list;
    MemoryPoolBlock *blocks;
} MemoryPool;

bool mempool_init(MemoryPool *pool, size_t item_size, size_t items_per_block);
void mempool_destroy(MemoryPool *pool);
void *mempool_alloc(MemoryPool *pool);
void mempool_free(MemoryPool *pool, void *item);

#endif
