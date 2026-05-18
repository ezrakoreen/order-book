#ifndef ORDER_H
#define ORDER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PriceLevel PriceLevel;

typedef struct Order {
    uint64_t id;
    char side;
    int price;
    int qty;
    uint64_t timestamp;

    struct Order *prev;
    struct Order *next;
    PriceLevel *level;
} Order;

bool order_side_is_valid(char side);
void order_init(Order *order, uint64_t id, char side, int price, int qty, uint64_t timestamp);

#endif
