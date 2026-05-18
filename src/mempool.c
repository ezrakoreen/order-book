#include "mempool.h"

#include <stdlib.h>
#include <string.h>

static size_t mempool_item_size(size_t requested_size) {
    return requested_size < sizeof(void *) ? sizeof(void *) : requested_size;
}

bool mempool_init(MemoryPool *pool, size_t item_size, size_t items_per_block) {
    if (pool == NULL || item_size == 0U) {
        return false;
    }

    pool->item_size = mempool_item_size(item_size);
    pool->items_per_block = items_per_block == 0U ? 64U : items_per_block;
    pool->free_list = NULL;
    pool->blocks = NULL;
    return true;
}

void mempool_destroy(MemoryPool *pool) {
    MemoryPoolBlock *block;
    MemoryPoolBlock *next;

    if (pool == NULL) {
        return;
    }

    block = pool->blocks;
    while (block != NULL) {
        next = block->next;
        free(block->storage);
        free(block);
        block = next;
    }

    pool->free_list = NULL;
    pool->blocks = NULL;
    pool->item_size = 0U;
    pool->items_per_block = 0U;
}

static MemoryPoolBlock *mempool_add_block(MemoryPool *pool) {
    MemoryPoolBlock *block = malloc(sizeof(*block));
    if (block == NULL) {
        return NULL;
    }

    block->storage = calloc(pool->items_per_block, pool->item_size);
    if (block->storage == NULL) {
        free(block);
        return NULL;
    }

    block->used = 0U;
    block->next = pool->blocks;
    pool->blocks = block;
    return block;
}

void *mempool_alloc(MemoryPool *pool) {
    MemoryPoolBlock *block;
    unsigned char *slot;

    if (pool == NULL) {
        return NULL;
    }

    if (pool->free_list != NULL) {
        void *item = pool->free_list;
        memcpy(&pool->free_list, item, sizeof(pool->free_list));
        memset(item, 0, pool->item_size);
        return item;
    }

    block = pool->blocks;
    if (block == NULL || block->used == pool->items_per_block) {
        block = mempool_add_block(pool);
        if (block == NULL) {
            return NULL;
        }
    }

    slot = (unsigned char *)block->storage + (block->used * pool->item_size);
    block->used += 1U;
    memset(slot, 0, pool->item_size);
    return slot;
}

void mempool_free(MemoryPool *pool, void *item) {
    if (pool == NULL || item == NULL) {
        return;
    }

    memcpy(item, &pool->free_list, sizeof(pool->free_list));
    pool->free_list = item;
}
