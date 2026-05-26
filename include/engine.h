#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "book.h"

typedef struct MatchingEngine {
    OrderBook *book;
    bool verbose;
    size_t last_fill_count;
} MatchingEngine;

bool engine_init(MatchingEngine *engine);
bool engine_init_with_allocator(MatchingEngine *engine, OrderBookAllocator allocator);
void engine_destroy(MatchingEngine *engine);
void engine_set_verbose(MatchingEngine *engine, bool verbose);

bool engine_add_limit(MatchingEngine *engine, uint64_t id, char side, int price, int qty);
bool engine_add_market(MatchingEngine *engine, uint64_t id, char side, int qty);
bool engine_cancel(MatchingEngine *engine, uint64_t id);
bool engine_modify(MatchingEngine *engine, uint64_t id, int price, int qty);
size_t engine_last_fill_count(const MatchingEngine *engine);
Order *engine_find_order(const MatchingEngine *engine, uint64_t id);
const PriceLevel *engine_best_bid(const MatchingEngine *engine);
const PriceLevel *engine_best_ask(const MatchingEngine *engine);

#endif
