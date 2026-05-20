CC := cc
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -Iinclude

LIB_SRCS := src/order.c src/book.c src/engine.c src/mempool.c

TEST_BOOK_SRCS := tests/test_book.c $(LIB_SRCS)
TEST_ENGINE_SRCS := tests/test_engine.c $(LIB_SRCS)
TEST_MEMPOOL_SRCS := tests/test_mempool.c src/mempool.c

.PHONY: all test clean

all: test_book test_engine test_mempool

test_book: $(TEST_BOOK_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_BOOK_SRCS)

test_engine: $(TEST_ENGINE_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_ENGINE_SRCS)

test_mempool: $(TEST_MEMPOOL_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_MEMPOOL_SRCS)

test: test_book test_engine test_mempool
	./test_book
	./test_engine
	./test_mempool

clean:
	rm -f test_book test_engine test_mempool
