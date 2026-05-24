CC := cc
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -Iinclude

LIB_SRCS := src/order.c src/book.c src/engine.c src/mempool.c
ENGINE_SRCS := src/main.c src/replay.c $(LIB_SRCS)

TEST_BOOK_SRCS := tests/test_book.c $(LIB_SRCS)
TEST_ENGINE_SRCS := tests/test_engine.c $(LIB_SRCS)
TEST_MEMPOOL_SRCS := tests/test_mempool.c src/mempool.c
TEST_REPLAY_SRCS := tests/test_replay.c src/replay.c $(LIB_SRCS)

.PHONY: all test clean

all: engine test_book test_engine test_mempool test_replay

engine: $(ENGINE_SRCS)
	$(CC) $(CFLAGS) -o $@ $(ENGINE_SRCS)

test_book: $(TEST_BOOK_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_BOOK_SRCS)

test_engine: $(TEST_ENGINE_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_ENGINE_SRCS)

test_mempool: $(TEST_MEMPOOL_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_MEMPOOL_SRCS)

test_replay: $(TEST_REPLAY_SRCS)
	$(CC) $(CFLAGS) -o $@ $(TEST_REPLAY_SRCS)

test: test_book test_engine test_mempool test_replay
	./test_book
	./test_engine
	./test_mempool
	./test_replay

clean:
	rm -f engine test_book test_engine test_mempool test_replay
