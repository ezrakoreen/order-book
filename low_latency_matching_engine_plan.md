# Low-Latency C Matching Engine MVP

## Goal

Build a single-threaded limit order book + matching engine in C with price-time priority, deterministic replay, and latency/throughput benchmarks.

Target resume bullet:

> Built a low-latency C limit order book and matching engine supporting add/cancel/modify/market orders, custom memory pools, and p50/p99 latency benchmarking over replayed order streams.

---

# Core Requirements

## Order Types

Support:

- `ADD` limit order
- `CANCEL` order by ID
- `MODIFY` order quantity and/or price
- `MARKET` order
- `PRINT` book snapshot/debug output

## Matching Rules

Use price-time priority:

- Buy orders match against lowest ask where `ask_price <= buy_price`
- Sell orders match against highest bid where `bid_price >= sell_price`
- Older orders at same price execute first
- Partial fills are allowed
- Fully filled orders are removed
- Remaining limit order quantity rests on the book
- Market orders never rest

NASDAQ-style feeds track add/cancel/execute/replace order events, and price-time priority is standard matching-engine behavior. Use this as the real-world model.

---

# Repo Structure

```text
Makefile
README.md
include/
  order.h
  book.h
  engine.h
  parser.h
  bench.h
  mempool.h
src/
  main.c
  order.c
  book.c
  engine.c
  parser.c
  bench.c
  mempool.c
tests/
  test_engine.c
  test_book.c
  test_parser.c
data/
  sample_orders.txt
  generated_1m.txt
scripts/
  gen_orders.py
```

---

# Input Format

Use a simple text protocol first:

```text
ADD 1 B 100 50
ADD 2 S 101 25
ADD 3 S 99 10
CANCEL 1
MODIFY 2 101 40
MARKET 4 B 60
```

Format:

```text
ADD <order_id> <B|S> <price> <qty>
CANCEL <order_id>
MODIFY <order_id> <new_price> <new_qty>
MARKET <order_id> <B|S> <qty>
```

Use integer prices in ticks, not floats.

---

# Data Structures

## Order

```c
typedef struct Order {
    uint64_t id;
    char side;
    int price;
    int qty;
    uint64_t timestamp;

    struct Order* prev;
    struct Order* next;
    struct PriceLevel* level;
} Order;
```

## PriceLevel

```c
typedef struct PriceLevel {
    int price;
    int total_qty;

    Order* head;
    Order* tail;

    struct PriceLevel* left;
    struct PriceLevel* right;
} PriceLevel;
```

## OrderBook

```c
typedef struct OrderBook {
    PriceLevel* bids;
    PriceLevel* asks;

    PriceLevel* best_bid;
    PriceLevel* best_ask;

    HashMap order_map;
    MemoryPool order_pool;
    MemoryPool level_pool;

    uint64_t timestamp;
} OrderBook;
```

---

# Implementation Milestones

## Milestone 1: Basic Book

### Implement

- Create/destroy order book
- Add order to price level
- Remove order from price level
- Track best bid and best ask
- Lookup order by ID

### Acceptance Criteria

- Can add buy/sell orders
- Maintains FIFO order at same price
- Correctly updates best bid/ask

---

## Milestone 2: Matching Engine

### Implement

- Limit order matching
- Market order matching
- Partial fills
- Trade event output

Example output:

```text
TRADE buy=3 sell=1 price=100 qty=25
```

### Acceptance Criteria

- Incoming buy crosses asks correctly
- Incoming sell crosses bids correctly
- FIFO priority works
- Partial fills leave remaining quantity

---

## Milestone 3: Cancel/Modify

### Implement

- Cancel by order ID
- Modify quantity
- Modify price by canceling and re-adding with new timestamp

### Acceptance Criteria

- Cancel removes order in O(1) after hash lookup
- Modify price loses time priority
- Modify quantity at same price preserves priority unless increasing quantity

---

## Milestone 4: Parser + Replay

### Implement

- Parse input file line by line
- Feed events into engine
- `--verbose` flag for trades
- `--snapshot` flag

CLI:

```bash
./engine data/sample_orders.txt
./engine --verbose data/sample_orders.txt
```

### Acceptance Criteria

- Deterministic replay
- Invalid commands return clear errors
- Handles 1M generated events

---

## Milestone 5: Benchmarking

### Measure

- total messages processed
- messages/sec
- average latency
- p50 latency
- p99 latency
- p999 latency

Use:

```c
clock_gettime(CLOCK_MONOTONIC_RAW, ...)
```

CLI:

```bash
./engine --bench data/generated_1m.txt
```

Output:

```text
messages: 1000000
throughput: 3.2M msg/s
latency_ns_avg: 310
latency_ns_p50: 220
latency_ns_p99: 900
latency_ns_p999: 1800
```

### Acceptance Criteria

- Benchmarks exclude file I/O if possible
- Store per-message latency in an array
- Sort latencies to compute percentiles

---

## Milestone 6: Memory Pool

### Implement

Replace `malloc/free` for orders and price levels with fixed-size memory pools.

### Acceptance Criteria

- No per-order malloc in hot path
- Pool supports allocate/free
- Benchmark compares malloc vs memory pool

---

# Tests

Add tests for:

- Add one order
- Match exact quantity
- Partial fill
- FIFO at same price
- Better price executes first
- Cancel existing order
- Cancel missing order
- Modify price
- Market order consumes multiple levels
- Market order with insufficient liquidity

Run:

```bash
make test
```

---

# Makefile Targets

```make
make
make test
make bench
make clean
```

Use flags:

```make
-Wall -Wextra -Werror -O3 -march=native
```

Add debug target:

```make
make debug
```

with:

```make
-g -O0 -fsanitize=address,undefined
```

---

# Performance Notes

Prioritize:

- integer prices
- intrusive linked lists
- order ID hash lookup
- no allocation in hot path
- cache-friendly structs
- branch-light matching loop
- benchmark p99, not just average

---

# Suggested Internal Architecture

```text
Market Data Feed
        ↓
Parser → Order Gateway → Matching Engine
                         ↓
                    Trade Events
```

---

# Benchmarking Methodology

## Recommended Workflow

1. Generate synthetic order flow
2. Warm up the engine
3. Run benchmark
4. Record:
   - throughput
   - avg latency
   - p50
   - p99
   - p999
5. Compare:
   - malloc vs memory pool
   - optimized vs debug build

## Latency Collection

Store per-message latency:

```c
uint64_t* latencies = malloc(sizeof(uint64_t) * num_msgs);
```

Sort after replay:

```c
qsort(latencies, num_msgs, sizeof(uint64_t), cmp_u64);
```

Percentiles:

```text
p50  = latencies[num_msgs * 0.50]
p99  = latencies[num_msgs * 0.99]
p999 = latencies[num_msgs * 0.999]
```

---

# Suggested Hash Map Design

Initial version:

- separate chaining
- fixed bucket count
- key = order ID
- value = Order*

Future optimization:

- open addressing
- robin hood hashing
- cache-aligned buckets

---

# Suggested Memory Pool Design

## Goals

Avoid allocator overhead in hot path.

## Simple Design

Preallocate arrays:

```c
Order* orders = malloc(sizeof(Order) * MAX_ORDERS);
```

Maintain freelist:

```c
typedef struct FreeNode {
    struct FreeNode* next;
} FreeNode;
```

Allocation:

```c
Order* alloc_order(MemoryPool* pool);
```

Free:

```c
void free_order(MemoryPool* pool, Order* order);
```

---

# Suggested Development Order

## Week 1

- repo setup
- Makefile
- order structs
- price levels
- linked lists

## Week 2

- matching logic
- partial fills
- trade generation
- parser

## Week 3

- cancel/modify
- benchmarks
- latency tracking
- tests

## Week 4

- memory pool
- profiling
- optimizations
- README cleanup

---

# Optimization Ideas

After MVP:

- branch prediction hints
- cache line alignment
- SIMD parsing
- intrusive containers
- object recycling
- CPU affinity
- prefetching
- arena allocators

---

# Future Extensions

## Networking

- TCP order gateway
- UDP multicast feed
- binary protocol parser

## Concurrency

- lock-free SPSC queue
- dedicated market-data thread
- producer-consumer architecture

## Matching Improvements

- multi-symbol support
- iceberg orders
- stop orders
- hidden liquidity

## Data Structures

- AVL tree
- red-black tree
- skip list
- radix tree

## Performance Tooling

- perf
- flamegraphs
- cachegrind
- hardware counters

---

# README Checklist

Include:

1. Project overview
2. Features
3. Matching rules
4. Data structures
5. Build instructions
6. Usage examples
7. Benchmark results
8. Optimization notes
9. Future work

---

# Example Resume Bullets

## Version 1

- Built a low-latency C limit order book and matching engine supporting price-time priority, market/limit orders, and deterministic replay over 1M+ simulated events.

## Version 2

- Implemented a high-performance matching engine in C with custom memory pools and O(1) order lookup, achieving multi-million message/sec throughput and p99 latency benchmarking.

## Version 3

- Designed a cache-conscious exchange simulator in C with intrusive linked lists, replayable market data feeds, and percentile latency instrumentation.

---

# Codex Workflow

Use milestone-by-milestone prompting.

Example:

```text
Implement Milestone 1 only.

Requirements:
- Follow repo structure exactly
- Use C17
- Add unit tests
- No external dependencies
- Use intrusive doubly linked lists
- Add comments explaining design choices
```

Then iterate:

```text
Now implement Milestone 2 on top of the existing codebase.
Do not change public interfaces unless necessary.
Add tests for partial fills and FIFO matching.
```

---

# Final Deliverable Goal

By the end, the project should:

- compile cleanly with `-Wall -Wextra -Werror`
- support replaying large order streams
- expose deterministic matching behavior
- report realistic latency statistics
- demonstrate strong systems programming fundamentals
- be resume-worthy for quant SWE / low-latency infra recruiting
