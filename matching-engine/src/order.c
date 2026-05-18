#include "order.h"

#include <stddef.h>

bool order_side_is_valid(char side) {
    return side == 'B' || side == 'S';
}

void order_init(Order *order, uint64_t id, char side, int price, int qty, uint64_t timestamp) {
    order->id = id;
    order->side = side;
    order->price = price;
    order->qty = qty;
    order->timestamp = timestamp;
    order->prev = NULL;
    order->next = NULL;
    order->level = NULL;
}
