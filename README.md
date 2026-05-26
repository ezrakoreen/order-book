# Matching Engine

This repository contains a C17 matching engine MVP built in milestones.

Milestones 1, 2, 3, 4, 5, and 6 are implemented:

- create and destroy an order book
- add buy and sell limit orders
- remove orders by ID
- maintain FIFO at each price level with intrusive doubly linked lists
- maintain best bid and best ask
- look up orders by ID with an internal hash map
- match crossing limit orders against the opposite best price
- match market orders without resting residual quantity
- support partial fills while preserving price-time priority
- cancel resting orders by ID
- modify resting orders by ID
- replay order events from a text file
- optionally print trade events as `TRADE buy=<id> sell=<id> price=<ticks> qty=<qty>`
- optionally print a final book snapshot
- benchmark in-memory replay throughput and per-message latency percentiles
- allocate orders and price levels from fixed-size memory pools by default
- compare pooled allocation against malloc/free in benchmark mode

Design notes:

- Orders embed `prev` and `next` pointers directly, which keeps per-level queues allocation-free and enables O(1) unlink after hash lookup.
- Price levels are stored in a simple binary search tree to keep the MVP dependency-free and easy to reason about.
- `MemoryPool` is used for orders and levels so allocation strategy stays local to the engine instead of leaking into later milestones.
- Matching emits trade events only when verbose replay mode is enabled.
- Same-price quantity decreases preserve FIFO priority. Same-price quantity increases lose FIFO priority by canceling and re-adding the order. Price changes also cancel and re-add, so the order receives a new timestamp and may match immediately if it crosses. This follows Nasdaq and NYSE rules.

Engine API:

```c
MatchingEngine engine;

engine_init(&engine);
engine_add_limit(&engine, 1, 'S', 100, 25);
engine_add_limit(&engine, 2, 'B', 101, 10);
engine_modify(&engine, 1, 100, 20);
engine_cancel(&engine, 2);
engine_add_market(&engine, 3, 'B', 15);
engine_destroy(&engine);
```

Trade output:

```text
TRADE buy=2 sell=1 price=100 qty=10
TRADE buy=3 sell=1 price=100 qty=15
```

Replay:

```sh
./engine data/sample_orders.txt
./engine --verbose data/sample_orders.txt
./engine --snapshot data/sample_orders.txt
./engine --bench data/sample_orders.txt
./engine --bench-list
./engine --bench-scenario market_sweep_10 --messages 100000
./engine --bench-scenario add_cancel --bench-allocator malloc --messages 100000
./engine --bench-scenario add_cancel --bench-allocators --messages 100000
./engine --bench-all --messages 100000
./engine --bench-all --bench-allocators --messages 100000
```

Benchmark scenarios:

```text
add_only
add_cancel
cancel_only
modify_qty_down
modify_reprice
limit_cross
market_match_1
market_sweep_10
deep_book_best_price
mixed
```

Benchmark results from a local run on May 26, 2026, using `--bench-all --messages 100000`.
The benchmark reports full per-command and per-fill-bucket percentiles; this table summarizes
overall throughput and latency.

| scenario | throughput | avg ns | p50 ns | p99 ns | p999 ns |
| --- | ---: | ---: | ---: | ---: | ---: |
| add_only | 4.42M msg/s | 196 | 125 | 375 | 1875 |
| add_cancel | 9.00M msg/s | 83 | 83 | 209 | 1084 |
| cancel_only | 8.49M msg/s | 89 | 83 | 209 | 959 |
| modify_qty_down | 9.99M msg/s | 73 | 42 | 167 | 875 |
| modify_reprice | 3.80M msg/s | 234 | 209 | 458 | 1208 |
| limit_cross | 7.63M msg/s | 103 | 84 | 250 | 916 |
| market_match_1 | 9.09M msg/s | 83 | 83 | 208 | 833 |
| market_sweep_10 | 0.78M msg/s | 1259 | 1208 | 2250 | 5208 |
| deep_book_best_price | 0.02M msg/s | 62165 | 85042 | 123333 | 147417 |
| mixed | 6.21M msg/s | 133 | 125 | 375 | 959 |

The deep-book result is intentionally much slower because the current MVP uses an unbalanced
binary search tree for price levels.

Allocator benchmark results from local runs on May 26, 2026, using
`--bench-scenario mixed --messages 100000`. The scenario generator is
deterministic, so every allocator and block-size run used the same command stream.
Pool block sizes were tested in 50 interleaved rounds, with price-level blocks fixed
at 64. Malloc/free was measured as one standalone 50-run baseline.

The ratio table reports memory-pool throughput divided by the common malloc/free
baseline for the same statistic.

| order block size | reps | avg ratio | p50 ratio | p99 ratio |
| ---: | ---: | ---: | ---: | ---: |
| 32 | 50 | 1.326x | 1.319x | 1.339x |
| 64 | 50 | 1.292x | 1.309x | 1.320x |
| 128 | 50 | 1.315x | 1.328x | 1.337x |
| 256 | 50 | 1.305x | 1.304x | 1.333x |
| 512 | 50 | 1.324x | 1.317x | 1.330x |
| 1024 | 50 | 1.324x | 1.319x | 1.339x |
| 4096 | 50 | 1.326x | 1.331x | 1.342x |
| 8192 | 50 | 1.316x | 1.331x | 1.344x |
| 16384 | 50 | 1.355x | 1.348x | 1.344x |

The throughput table reports M messages per second.

| allocator | order block size | reps | avg M msg/s | p50 M msg/s | p99 M msg/s | p999 M msg/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| memory_pool | 32 | 50 | 15.61 | 15.87 | 16.71 | 16.71 |
| memory_pool | 64 | 50 | 15.21 | 15.75 | 16.47 | 16.47 |
| memory_pool | 128 | 50 | 15.48 | 15.97 | 16.68 | 16.68 |
| memory_pool | 256 | 50 | 15.37 | 15.69 | 16.63 | 16.63 |
| memory_pool | 512 | 50 | 15.59 | 15.84 | 16.60 | 16.60 |
| memory_pool | 1024 | 50 | 15.58 | 15.87 | 16.71 | 16.71 |
| memory_pool | 4096 | 50 | 15.61 | 16.01 | 16.75 | 16.75 |
| memory_pool | 8192 | 50 | 15.50 | 16.01 | 16.77 | 16.77 |
| memory_pool | 16384 | 50 | 15.96 | 16.22 | 16.77 | 16.77 |
| malloc/free | n/a | 50 | 11.77 | 12.03 | 12.48 | 12.48 |

The order block size is the number of `Order` slots reserved each time the pool
grows. Larger blocks amortize backing allocations and keep more orders contiguous,
which is is why we see that median throughput tends to increase as block size increases.
However, they can reserve more memory than small books need.

Input format:

```text
ADD <id> <B|S> <price> <qty>
MARKET <id> <B|S> <qty>
CANCEL <id>
MODIFY <id> <price> <qty>
```

Build and test:

```sh
make
make test
make bench
make debug
```
