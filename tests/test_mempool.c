#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "mempool.h"

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_mempool: %s\n", message);
        exit(1);
    }
}

static void test_allocations_are_max_aligned(void) {
    MemoryPool pool;
    void *items[8];
    size_t i;

    expect(mempool_init(&pool, 9U, 8U), "pool should initialize");

    for (i = 0U; i < 8U; ++i) {
        items[i] = mempool_alloc(&pool);
        expect(items[i] != NULL, "allocation should succeed");
        expect(((uintptr_t)items[i] % _Alignof(max_align_t)) == 0U,
               "allocation should be suitably aligned");
    }

    mempool_destroy(&pool);
}

int main(void) {
    test_allocations_are_max_aligned();
    return 0;
}
