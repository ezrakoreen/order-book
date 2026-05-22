#include <stdio.h>
#include <stdlib.h>

#include "book.h"

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_book: %s\n", message);
        exit(1);
    }
}

static void test_add_and_best_prices(void) {
    OrderBook *book = order_book_create();
    const PriceLevel *best_bid;
    const PriceLevel *best_ask;

    expect(book != NULL, "book should be created");
    expect(order_book_add(book, 1U, 'B', 100, 10), "first bid should be accepted");
    expect(order_book_add(book, 2U, 'B', 101, 5), "second bid should be accepted");
    expect(order_book_add(book, 3U, 'S', 105, 7), "first ask should be accepted");
    expect(order_book_add(book, 4U, 'S', 103, 12), "second ask should be accepted");

    best_bid = order_book_best_bid(book);
    best_ask = order_book_best_ask(book);

    expect(best_bid != NULL && best_bid->price == 101, "best bid price should be 101");
    expect(best_bid->total_qty == 5, "best bid quantity should match resting order");
    expect(best_ask != NULL && best_ask->price == 103, "best ask price should be 103");
    expect(best_ask->total_qty == 12, "best ask quantity should match resting order");
    expect(order_book_find(book, 2U) != NULL, "lookup by order id should succeed");

    order_book_destroy(book);
}

static void test_fifo_same_price(void) {
    OrderBook *book = order_book_create();
    const PriceLevel *level;

    expect(book != NULL, "book should be created");
    expect(order_book_add(book, 10U, 'B', 100, 10), "first same-price order should be accepted");
    expect(order_book_add(book, 11U, 'B', 100, 15), "second same-price order should be accepted");
    expect(order_book_add(book, 12U, 'B', 100, 20), "third same-price order should be accepted");

    level = order_book_best_bid(book);
    expect(level != NULL, "best bid level should exist");
    expect(level->head != NULL && level->head->id == 10U, "head should remain first inserted order");
    expect(level->head->next != NULL && level->head->next->id == 11U, "second order should keep FIFO position");
    expect(level->tail != NULL && level->tail->id == 12U, "tail should be most recent order");
    expect(level->tail->prev != NULL && level->tail->prev->id == 11U, "reverse links should be maintained");
    expect(level->total_qty == 45, "price level total quantity should aggregate all orders");

    order_book_destroy(book);
}

static void test_remove_and_best_price_updates(void) {
    OrderBook *book = order_book_create();
    const PriceLevel *best_bid;
    const PriceLevel *best_ask;

    expect(book != NULL, "book should be created");
    expect(order_book_add(book, 20U, 'S', 105, 20), "ask should be accepted");
    expect(order_book_add(book, 21U, 'S', 106, 30), "second ask should be accepted");
    expect(order_book_add(book, 30U, 'B', 99, 10), "bid should be accepted");
    expect(order_book_add(book, 31U, 'B', 99, 5), "second bid should be accepted");
    expect(order_book_add(book, 32U, 'B', 98, 7), "third bid should be accepted");

    expect(order_book_remove(book, 31U), "middle order should be removable");
    best_bid = order_book_best_bid(book);
    expect(best_bid != NULL && best_bid->price == 99, "best bid should stay at 99 while orders remain");
    expect(best_bid->head != NULL && best_bid->head->id == 30U, "remaining FIFO head should stay intact");
    expect(best_bid->tail != NULL && best_bid->tail->id == 30U, "single remaining order should be both head and tail");
    expect(best_bid->total_qty == 10, "quantity should shrink after removal");
    expect(order_book_find(book, 31U) == NULL, "removed order should disappear from lookup");

    expect(order_book_remove(book, 20U), "best ask should be removable");
    best_ask = order_book_best_ask(book);
    expect(best_ask != NULL && best_ask->price == 106, "best ask should move to next level");

    expect(order_book_remove(book, 30U), "remaining best bid order should be removable");
    best_bid = order_book_best_bid(book);
    expect(best_bid != NULL && best_bid->price == 98, "best bid should fall to next price level");

    expect(order_book_remove(book, 32U), "last bid should be removable");
    expect(order_book_best_bid(book) == NULL, "best bid should be null after last bid leaves");

    order_book_destroy(book);
}

static void test_modify_quantity_preserves_position(void) {
    OrderBook *book = order_book_create();
    const PriceLevel *level;

    expect(book != NULL, "book should be created");
    expect(order_book_add(book, 40U, 'B', 100, 10), "first order should be accepted");
    expect(order_book_add(book, 41U, 'B', 100, 15), "second order should be accepted");
    expect(order_book_modify_quantity(book, 40U, 6), "quantity decrease should be accepted");

    level = order_book_best_bid(book);
    expect(level != NULL, "best bid should still exist after quantity modify");
    expect(level->head != NULL && level->head->id == 40U, "modified order should keep FIFO position");
    expect(level->tail != NULL && level->tail->id == 41U, "later order should remain tail");
    expect(level->total_qty == 21, "level quantity should reflect modified order");
    expect(order_book_find(book, 40U)->qty == 6, "lookup should expose modified quantity");

    order_book_destroy(book);
}

int main(void) {
    test_add_and_best_prices();
    test_fifo_same_price();
    test_remove_and_best_price_updates();
    test_modify_quantity_preserves_position();
    return 0;
}
