#include <stdio.h>
#include <stdlib.h>

#include "engine.h"

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_engine: %s\n", message);
        exit(1);
    }
}

static void test_exposes_book_state(void) {
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
}

static void test_incoming_buy_crosses_asks_fifo(void) {
    MatchingEngine engine;
    const PriceLevel *best_ask;

    expect(engine_init(&engine), "engine should initialize for buy cross");
    expect(engine_add_limit(&engine, 1U, 'S', 100, 10), "first ask should rest");
    expect(engine_add_limit(&engine, 2U, 'S', 100, 15), "second ask should rest");
    expect(engine_add_limit(&engine, 3U, 'B', 101, 12), "crossing buy should execute");

    expect(engine_find_order(&engine, 1U) == NULL, "older ask should fill first");
    expect(engine_find_order(&engine, 2U) != NULL, "newer ask should remain after partial fill");
    expect(engine_find_order(&engine, 2U)->qty == 13, "newer ask should have reduced quantity");
    expect(engine_find_order(&engine, 3U) == NULL, "fully filled incoming buy should not rest");

    best_ask = engine_best_ask(&engine);
    expect(best_ask != NULL && best_ask->price == 100 && best_ask->total_qty == 13,
           "best ask should keep partial residual");

    engine_destroy(&engine);
}

static void test_incoming_sell_crosses_bids_and_rests_residual(void) {
    MatchingEngine engine;
    const PriceLevel *best_ask;

    expect(engine_init(&engine), "engine should initialize for sell cross");
    expect(engine_add_limit(&engine, 1U, 'B', 101, 10), "best bid should rest");
    expect(engine_add_limit(&engine, 2U, 'B', 100, 20), "lower bid should rest");
    expect(engine_add_limit(&engine, 3U, 'S', 100, 25), "crossing sell should execute and rest residual");

    expect(engine_find_order(&engine, 1U) == NULL, "best bid should fill first");
    expect(engine_find_order(&engine, 2U) != NULL, "next bid should be partially filled");
    expect(engine_find_order(&engine, 2U)->qty == 5, "next bid should keep residual quantity");
    expect(engine_find_order(&engine, 3U) == NULL, "incoming sell should be fully filled");

    expect(engine_add_limit(&engine, 4U, 'S', 100, 10), "non-crossing sell should rest");
    best_ask = engine_best_ask(&engine);
    expect(best_ask != NULL && best_ask->price == 100 && best_ask->total_qty == 5,
           "resting ask should become best ask");

    engine_destroy(&engine);
}

static void test_market_order_never_rests(void) {
    MatchingEngine engine;
    const PriceLevel *best_ask;

    expect(engine_init(&engine), "engine should initialize for market");
    expect(engine_add_limit(&engine, 1U, 'S', 100, 10), "ask should rest before market");
    expect(engine_add_market(&engine, 2U, 'B', 15), "market buy should execute available liquidity");

    expect(engine_find_order(&engine, 1U) == NULL, "resting ask should fill");
    expect(engine_find_order(&engine, 2U) == NULL, "market order should not rest");
    best_ask = engine_best_ask(&engine);
    expect(best_ask == NULL, "market residual should not create a level");

    engine_destroy(&engine);
}

int main(void) {
    test_exposes_book_state();
    test_incoming_buy_crosses_asks_fifo();
    test_incoming_sell_crosses_bids_and_rests_residual();
    test_market_order_never_rests();
    return 0;
}
