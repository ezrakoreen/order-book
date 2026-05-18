#ifndef BOOK_H
#define BOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mempool.h"
#include "order.h"

typedef struct PriceLevel {
    int price;
    int total_qty;

    Order *head;
    Order *tail;

    struct PriceLevel *left;
    struct PriceLevel *right;
} PriceLevel;

typedef struct OrderMapEntry {
    uint64_t key;
    Order *value;
    unsigned char state;
} OrderMapEntry;

typedef struct OrderMap {
    OrderMapEntry *entries;
    size_t capacity;
    size_t size;
    size_t tombstones;
} OrderMap;

typedef struct OrderBook {
    PriceLevel *bids;
    PriceLevel *asks;

    PriceLevel *best_bid;
    PriceLevel *best_ask;

    OrderMap order_map;
    MemoryPool order_pool;
    MemoryPool level_pool;

    uint64_t timestamp;
} OrderBook;

OrderBook *order_book_create(void);
void order_book_destroy(OrderBook *book);

bool order_book_add(OrderBook *book, uint64_t id, char side, int price, int qty);
bool order_book_remove(OrderBook *book, uint64_t id);

Order *order_book_find(const OrderBook *book, uint64_t id);
const PriceLevel *order_book_best_bid(const OrderBook *book);
const PriceLevel *order_book_best_ask(const OrderBook *book);

#endif
