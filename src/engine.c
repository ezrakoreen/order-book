#include "engine.h"

#include <stdio.h>

static int min_int(int left, int right) {
    return left < right ? left : right;
}

static bool limit_crosses(const MatchingEngine *engine, char side, int price) {
    const PriceLevel *best;

    if (side == 'B') {
        best = order_book_best_ask(engine->book);
        return best != NULL && best->price <= price;
    }

    best = order_book_best_bid(engine->book);
    return best != NULL && best->price >= price;
}

static PriceLevel *best_opposite_level(const MatchingEngine *engine, char side) {
    return side == 'B' ? engine->book->best_ask : engine->book->best_bid;
}

static void print_trade(uint64_t incoming_id, char incoming_side, const Order *resting, int qty) {
    uint64_t buy_id = incoming_side == 'B' ? incoming_id : resting->id;
    uint64_t sell_id = incoming_side == 'B' ? resting->id : incoming_id;

    printf("TRADE buy=%llu sell=%llu price=%d qty=%d\n",
           (unsigned long long)buy_id,
           (unsigned long long)sell_id,
           resting->price,
           qty);
}

static bool match_order(MatchingEngine *engine, uint64_t id, char side, int limit_price, int *qty) {
    while (*qty > 0 && (limit_price == 0 || limit_crosses(engine, side, limit_price))) {
        PriceLevel *level = best_opposite_level(engine, side);
        Order *resting;
        int fill_qty;

        if (level == NULL || level->head == NULL) {
            break;
        }

        resting = level->head;
        fill_qty = min_int(*qty, resting->qty);
        print_trade(id, side, resting, fill_qty);

        if (!order_book_fill(engine->book, resting, fill_qty)) {
            return false;
        }

        *qty -= fill_qty;
    }

    return true;
}

bool engine_init(MatchingEngine *engine) {
    if (engine == NULL) {
        return false;
    }

    engine->book = order_book_create();
    return engine->book != NULL;
}

void engine_destroy(MatchingEngine *engine) {
    if (engine == NULL) {
        return;
    }

    order_book_destroy(engine->book);
    engine->book = NULL;
}

bool engine_add_limit(MatchingEngine *engine, uint64_t id, char side, int price, int qty) {
    if (engine == NULL || engine->book == NULL) {
        return false;
    }

    if (id == 0U || !order_side_is_valid(side) || price <= 0 || qty <= 0 ||
        order_book_find(engine->book, id) != NULL) {
        return false;
    }

    if (!match_order(engine, id, side, price, &qty)) {
        return false;
    }

    if (qty == 0) {
        return true;
    }

    return order_book_add(engine->book, id, side, price, qty);
}

bool engine_add_market(MatchingEngine *engine, uint64_t id, char side, int qty) {
    if (engine == NULL || engine->book == NULL) {
        return false;
    }

    if (id == 0U || !order_side_is_valid(side) || qty <= 0 ||
        order_book_find(engine->book, id) != NULL) {
        return false;
    }

    return match_order(engine, id, side, 0, &qty);
}

Order *engine_find_order(const MatchingEngine *engine, uint64_t id) {
    if (engine == NULL || engine->book == NULL) {
        return NULL;
    }

    return order_book_find(engine->book, id);
}

const PriceLevel *engine_best_bid(const MatchingEngine *engine) {
    if (engine == NULL || engine->book == NULL) {
        return NULL;
    }

    return order_book_best_bid(engine->book);
}

const PriceLevel *engine_best_ask(const MatchingEngine *engine) {
    if (engine == NULL || engine->book == NULL) {
        return NULL;
    }

    return order_book_best_ask(engine->book);
}
