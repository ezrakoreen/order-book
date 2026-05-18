#include "engine.h"

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

    return order_book_add(engine->book, id, side, price, qty);
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
