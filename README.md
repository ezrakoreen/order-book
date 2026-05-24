# Matching Engine

This repository contains a C17 matching engine MVP built in milestones.

Milestones 1, 2, 3, and 4 are implemented:

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
```

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
```
