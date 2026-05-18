# Matching Engine

This repository contains a C17 matching engine MVP built in milestones.

Milestone 1 is implemented:

- create and destroy an order book
- add buy and sell limit orders
- remove orders by ID
- maintain FIFO at each price level with intrusive doubly linked lists
- maintain best bid and best ask
- look up orders by ID with an internal hash map

Design notes:

- Orders embed `prev` and `next` pointers directly, which keeps per-level queues allocation-free and enables O(1) unlink after hash lookup.
- Price levels are stored in a simple binary search tree to keep the MVP dependency-free and easy to reason about.
- `MemoryPool` is used for orders and levels so allocation strategy stays local to the engine instead of leaking into later milestones.

Build and test:

```sh
make
make test
```
