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

static void test_malloc_allocator_matches_engine_basics(void) {
    MatchingEngine engine;

    expect(engine_init_with_allocator(&engine, ORDER_BOOK_ALLOCATOR_MALLOC),
           "malloc-backed engine should initialize");
    expect(engine_add_limit(&engine, 1U, 'S', 100, 10), "malloc engine should accept ask");
    expect(engine_add_limit(&engine, 2U, 'B', 101, 4), "malloc engine should match crossing bid");
    expect(engine_find_order(&engine, 1U) != NULL, "malloc engine should retain ask residual");
    expect(engine_find_order(&engine, 1U)->qty == 6, "malloc engine residual quantity should match");
    expect(engine_find_order(&engine, 2U) == NULL, "fully filled incoming order should not rest");

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

static void test_cancel_removes_order_by_id(void) {
    MatchingEngine engine;

    expect(engine_init(&engine), "engine should initialize for cancel");
    expect(engine_add_limit(&engine, 1U, 'B', 100, 10), "bid should rest before cancel");
    expect(engine_add_limit(&engine, 2U, 'B', 99, 15), "second bid should rest before cancel");

    expect(engine_cancel(&engine, 1U), "cancel should remove resting order");
    expect(engine_find_order(&engine, 1U) == NULL, "canceled order should disappear");
    expect(engine_best_bid(&engine) != NULL && engine_best_bid(&engine)->price == 99,
           "best bid should update after cancel");
    expect(!engine_cancel(&engine, 1U), "canceling a missing order should fail");

    engine_destroy(&engine);
}

static void test_modify_same_price_decrease_preserves_priority(void) {
    MatchingEngine engine;
    const PriceLevel *level;

    expect(engine_init(&engine), "engine should initialize for same-price decrease");
    expect(engine_add_limit(&engine, 10U, 'S', 100, 10), "first ask should rest");
    expect(engine_add_limit(&engine, 11U, 'S', 100, 15), "second ask should rest");
    expect(engine_modify(&engine, 10U, 100, 5), "same-price quantity decrease should modify in place");

    level = engine_best_ask(&engine);
    expect(level != NULL, "ask level should remain after modify");
    expect(level->head != NULL && level->head->id == 10U, "decrease should preserve FIFO priority");
    expect(level->tail != NULL && level->tail->id == 11U, "later order should remain behind");
    expect(level->total_qty == 20, "level quantity should shrink after modify");

    engine_destroy(&engine);
}

static void test_modify_same_price_increase_loses_priority(void) {
    MatchingEngine engine;
    const PriceLevel *level;

    expect(engine_init(&engine), "engine should initialize for same-price increase");
    expect(engine_add_limit(&engine, 20U, 'S', 100, 10), "first ask should rest");
    expect(engine_add_limit(&engine, 21U, 'S', 100, 15), "second ask should rest");
    expect(engine_modify(&engine, 20U, 100, 12), "same-price quantity increase should re-add");

    level = engine_best_ask(&engine);
    expect(level != NULL, "ask level should remain after quantity increase");
    expect(level->head != NULL && level->head->id == 21U, "increase should lose FIFO priority");
    expect(level->tail != NULL && level->tail->id == 20U, "increased order should move to tail");
    expect(level->total_qty == 27, "level quantity should include increased order");

    engine_destroy(&engine);
}

static void test_modify_price_readds_and_can_match(void) {
    MatchingEngine engine;

    expect(engine_init(&engine), "engine should initialize for price modify");
    expect(engine_add_limit(&engine, 30U, 'S', 105, 10), "ask should rest before price modify");
    expect(engine_add_limit(&engine, 31U, 'B', 100, 7), "bid should rest before price modify");
    expect(engine_modify(&engine, 30U, 100, 10), "modified ask should cross and execute");

    expect(engine_find_order(&engine, 31U) == NULL, "resting bid should fully fill");
    expect(engine_find_order(&engine, 30U) != NULL, "modified ask residual should rest");
    expect(engine_find_order(&engine, 30U)->qty == 3, "modified ask should keep residual quantity");
    expect(engine_best_ask(&engine) != NULL && engine_best_ask(&engine)->price == 100,
           "modified residual should rest at new price");

    engine_destroy(&engine);
}

int main(void) {
    test_exposes_book_state();
    test_malloc_allocator_matches_engine_basics();
    test_incoming_buy_crosses_asks_fifo();
    test_incoming_sell_crosses_bids_and_rests_residual();
    test_market_order_never_rests();
    test_cancel_removes_order_by_id();
    test_modify_same_price_decrease_preserves_priority();
    test_modify_same_price_increase_loses_priority();
    test_modify_price_readds_and_can_match();
    return 0;
}
