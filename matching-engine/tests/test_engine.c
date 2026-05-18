#include <stdio.h>
#include <stdlib.h>

#include "engine.h"

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_engine: %s\n", message);
        exit(1);
    }
}

int main(void) {
    MatchingEngine engine;
    const PriceLevel *best_bid;
    const PriceLevel *best_ask;

    expect(engine_init(&engine), "engine should initialize");
    expect(engine_add_limit(&engine, 1U, 'B', 100, 25), "engine should accept bid");
    expect(engine_add_limit(&engine, 2U, 'S', 101, 40), "engine should accept ask");

    best_bid = engine_best_bid(&engine);
    best_ask = engine_best_ask(&engine);

    expect(best_bid != NULL && best_bid->price == 100, "engine should expose best bid");
    expect(best_ask != NULL && best_ask->price == 101, "engine should expose best ask");
    expect(engine_find_order(&engine, 2U) != NULL, "engine lookup should delegate to book");

    engine_destroy(&engine);
    return 0;
}
