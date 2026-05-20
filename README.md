# Matching Engine

This repository contains a C17 matching engine MVP built in milestones.

Milestones 1 and 2 are implemented:

- create and destroy an order book
- add buy and sell limit orders
- remove orders by ID
- maintain FIFO at each price level with intrusive doubly linked lists
- maintain best bid and best ask
- look up orders by ID with an internal hash map
- match crossing limit orders against the opposite best price
- match market orders without resting residual quantity
- support partial fills while preserving price-time priority
- print trade events as `TRADE buy=<id> sell=<id> price=<ticks> qty=<qty>`

Design notes:

- Orders embed `prev` and `next` pointers directly, which keeps per-level queues allocation-free and enables O(1) unlink after hash lookup.
- Price levels are stored in a simple binary search tree to keep the MVP dependency-free and easy to reason about.
- `MemoryPool` is used for orders and levels so allocation strategy stays local to the engine instead of leaking into later milestones.
- Matching currently emits trade events directly to stdout. A later parser/replay milestone can route this behind a verbose flag or callback.

Engine API:

```c
MatchingEngine engine;

engine_init(&engine);
engine_add_limit(&engine, 1, 'S', 100, 25);
engine_add_limit(&engine, 2, 'B', 101, 10);
engine_add_market(&engine, 3, 'B', 15);
engine_destroy(&engine);
```

Trade output:

```text
TRADE buy=2 sell=1 price=100 qty=10
TRADE buy=3 sell=1 price=100 qty=15
```

Build and test:

```sh
make
make test
```
