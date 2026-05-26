#include "book.h"

#include <stdlib.h>
#include <string.h>

enum {
    ORDER_MAP_EMPTY = 0,
    ORDER_MAP_OCCUPIED = 1,
    ORDER_MAP_TOMBSTONE = 2
};

#ifndef ORDER_POOL_ITEMS_PER_BLOCK
#define ORDER_POOL_ITEMS_PER_BLOCK 128U
#endif

#ifndef LEVEL_POOL_ITEMS_PER_BLOCK
#define LEVEL_POOL_ITEMS_PER_BLOCK 64U
#endif

/*
 * Design choice:
 * - Orders are linked directly inside each price level queue. That keeps the
 *   per-order removal path O(1) after the hash lookup and avoids wrapper nodes.
 * - Price levels live in a simple BST for the MVP. It is not self-balancing,
 *   but it keeps the code dependency-free and deterministic while Milestone 1
 *   focuses only on correctness.
 */

static size_t next_power_of_two(size_t value) {
    size_t power = 16U;

    while (power < value) {
        power <<= 1U;
    }

    return power;
}

static size_t hash_u64(uint64_t value) {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return (size_t)value;
}

static bool order_map_init(OrderMap *map, size_t capacity_hint) {
    size_t capacity;

    if (map == NULL) {
        return false;
    }

    capacity = next_power_of_two(capacity_hint == 0U ? 64U : capacity_hint);
    map->entries = calloc(capacity, sizeof(*map->entries));
    if (map->entries == NULL) {
        return false;
    }

    map->capacity = capacity;
    map->size = 0U;
    map->tombstones = 0U;
    return true;
}

static void order_map_destroy(OrderMap *map) {
    if (map == NULL) {
        return;
    }

    free(map->entries);
    map->entries = NULL;
    map->capacity = 0U;
    map->size = 0U;
    map->tombstones = 0U;
}

static size_t order_map_find_slot(const OrderMap *map, uint64_t key, bool *found) {
    size_t index;
    size_t first_tombstone = map->capacity;

    index = hash_u64(key) & (map->capacity - 1U);
    for (;;) {
        OrderMapEntry *entry = &map->entries[index];

        if (entry->state == ORDER_MAP_EMPTY) {
            *found = false;
            return first_tombstone != map->capacity ? first_tombstone : index;
        }

        if (entry->state == ORDER_MAP_OCCUPIED && entry->key == key) {
            *found = true;
            return index;
        }

        if (entry->state == ORDER_MAP_TOMBSTONE && first_tombstone == map->capacity) {
            first_tombstone = index;
        }

        index = (index + 1U) & (map->capacity - 1U);
    }
}

static bool order_map_resize(OrderMap *map, size_t new_capacity) {
    OrderMapEntry *old_entries = map->entries;
    size_t old_capacity = map->capacity;
    size_t i;

    map->entries = calloc(new_capacity, sizeof(*map->entries));
    if (map->entries == NULL) {
        map->entries = old_entries;
        return false;
    }

    map->capacity = new_capacity;
    map->size = 0U;
    map->tombstones = 0U;

    for (i = 0U; i < old_capacity; ++i) {
        if (old_entries[i].state == ORDER_MAP_OCCUPIED) {
            bool found = false;
            size_t slot = order_map_find_slot(map, old_entries[i].key, &found);

            map->entries[slot].key = old_entries[i].key;
            map->entries[slot].value = old_entries[i].value;
            map->entries[slot].state = ORDER_MAP_OCCUPIED;
            map->size += 1U;
        }
    }

    free(old_entries);
    return true;
}

static bool order_map_ensure_capacity(OrderMap *map) {
    size_t used = map->size + map->tombstones;
    if ((used + 1U) * 10U >= map->capacity * 7U) {
        return order_map_resize(map, map->capacity << 1U);
    }
    return true;
}

static Order *order_map_get(const OrderMap *map, uint64_t key) {
    bool found = false;
    size_t slot;

    if (map == NULL || map->capacity == 0U) {
        return NULL;
    }

    slot = order_map_find_slot(map, key, &found);
    return found ? map->entries[slot].value : NULL;
}

static bool order_map_put(OrderMap *map, uint64_t key, Order *value) {
    bool found = false;
    size_t slot;

    if (!order_map_ensure_capacity(map)) {
        return false;
    }

    slot = order_map_find_slot(map, key, &found);
    if (found) {
        return false;
    }

    if (map->entries[slot].state == ORDER_MAP_TOMBSTONE) {
        map->tombstones -= 1U;
    }

    map->entries[slot].key = key;
    map->entries[slot].value = value;
    map->entries[slot].state = ORDER_MAP_OCCUPIED;
    map->size += 1U;
    return true;
}

static bool order_map_erase(OrderMap *map, uint64_t key) {
    bool found = false;
    size_t slot;

    if (map == NULL || map->capacity == 0U) {
        return false;
    }

    slot = order_map_find_slot(map, key, &found);
    if (!found) {
        return false;
    }

    map->entries[slot].value = NULL;
    map->entries[slot].state = ORDER_MAP_TOMBSTONE;
    map->size -= 1U;
    map->tombstones += 1U;
    return true;
}

static PriceLevel *price_level_min(PriceLevel *root) {
    while (root != NULL && root->left != NULL) {
        root = root->left;
    }
    return root;
}

static PriceLevel *price_level_max(PriceLevel *root) {
    while (root != NULL && root->right != NULL) {
        root = root->right;
    }
    return root;
}

static void *book_alloc(OrderBook *book, MemoryPool *pool, size_t size) {
    if (book->allocator == ORDER_BOOK_ALLOCATOR_MALLOC) {
        return calloc(1U, size);
    }

    return mempool_alloc(pool);
}

static void book_free(OrderBook *book, MemoryPool *pool, void *item) {
    if (item == NULL) {
        return;
    }

    if (book->allocator == ORDER_BOOK_ALLOCATOR_MALLOC) {
        free(item);
        return;
    }

    mempool_free(pool, item);
}

static PriceLevel *price_level_detach_min(PriceLevel **root) {
    PriceLevel *node = *root;

    if (node->left == NULL) {
        *root = node->right;
        node->right = NULL;
        return node;
    }

    return price_level_detach_min(&node->left);
}

static PriceLevel *price_level_insert_for_book(OrderBook *book,
                                               PriceLevel **root,
                                               int price,
                                               bool *created) {
    PriceLevel **cursor = root;

    while (*cursor != NULL) {
        if (price < (*cursor)->price) {
            cursor = &(*cursor)->left;
        } else if (price > (*cursor)->price) {
            cursor = &(*cursor)->right;
        } else {
            *created = false;
            return *cursor;
        }
    }

    *cursor = book_alloc(book, &book->level_pool, sizeof(PriceLevel));
    if (*cursor == NULL) {
        *created = false;
        return NULL;
    }

    (*cursor)->price = price;
    *created = true;
    return *cursor;
}

static bool price_level_remove_node(OrderBook *book, PriceLevel **root, int price) {
    PriceLevel *node = *root;

    if (node == NULL) {
        return false;
    }

    if (price < node->price) {
        return price_level_remove_node(book, &node->left, price);
    }
    if (price > node->price) {
        return price_level_remove_node(book, &node->right, price);
    }

    if (node->left == NULL) {
        *root = node->right;
    } else if (node->right == NULL) {
        *root = node->left;
    } else {
        PriceLevel *successor = price_level_detach_min(&node->right);
        successor->left = node->left;
        successor->right = node->right;
        *root = successor;
    }

    book_free(book, &book->level_pool, node);
    return true;
}

static void price_level_free_tree(OrderBook *book, PriceLevel *root) {
    Order *order;

    if (root == NULL) {
        return;
    }

    price_level_free_tree(book, root->left);
    price_level_free_tree(book, root->right);

    order = root->head;
    while (order != NULL) {
        Order *next = order->next;
        book_free(book, &book->order_pool, order);
        order = next;
    }

    book_free(book, &book->level_pool, root);
}

static void order_list_append(PriceLevel *level, Order *order) {
    order->prev = level->tail;
    order->next = NULL;
    order->level = level;

    if (level->tail != NULL) {
        level->tail->next = order;
    } else {
        level->head = order;
    }

    level->tail = order;
    level->total_qty += order->qty;
}

static void order_list_remove(PriceLevel *level, Order *order) {
    if (order->prev != NULL) {
        order->prev->next = order->next;
    } else {
        level->head = order->next;
    }

    if (order->next != NULL) {
        order->next->prev = order->prev;
    } else {
        level->tail = order->prev;
    }

    level->total_qty -= order->qty;
    order->prev = NULL;
    order->next = NULL;
    order->level = NULL;
}

static void order_book_refresh_bbo(OrderBook *book) {
    book->best_bid = price_level_max(book->bids);
    book->best_ask = price_level_min(book->asks);
}

OrderBook *order_book_create(void) {
    return order_book_create_with_allocator(ORDER_BOOK_ALLOCATOR_POOL);
}

OrderBook *order_book_create_with_allocator(OrderBookAllocator allocator) {
    OrderBook *book;

    if (allocator != ORDER_BOOK_ALLOCATOR_POOL && allocator != ORDER_BOOK_ALLOCATOR_MALLOC) {
        return NULL;
    }

    book = calloc(1U, sizeof(*book));
    if (book == NULL) {
        return NULL;
    }

    book->allocator = allocator;
    if (!order_map_init(&book->order_map, 64U)) {
        order_map_destroy(&book->order_map);
        free(book);
        return NULL;
    }

    if (allocator == ORDER_BOOK_ALLOCATOR_POOL &&
        (!mempool_init(&book->order_pool, sizeof(Order), ORDER_POOL_ITEMS_PER_BLOCK) ||
         !mempool_init(&book->level_pool, sizeof(PriceLevel), LEVEL_POOL_ITEMS_PER_BLOCK))) {
        order_map_destroy(&book->order_map);
        mempool_destroy(&book->order_pool);
        mempool_destroy(&book->level_pool);
        free(book);
        return NULL;
    }

    return book;
}

void order_book_destroy(OrderBook *book) {
    if (book == NULL) {
        return;
    }

    price_level_free_tree(book, book->bids);
    price_level_free_tree(book, book->asks);
    order_map_destroy(&book->order_map);
    mempool_destroy(&book->order_pool);
    mempool_destroy(&book->level_pool);
    free(book);
}

bool order_book_add(OrderBook *book, uint64_t id, char side, int price, int qty) {
    PriceLevel **tree;
    PriceLevel *level;
    Order *order;
    bool created = false;

    if (book == NULL || id == 0U || !order_side_is_valid(side) || price <= 0 || qty <= 0) {
        return false;
    }

    if (order_map_get(&book->order_map, id) != NULL) {
        return false;
    }

    tree = side == 'B' ? &book->bids : &book->asks;
    level = price_level_insert_for_book(book, tree, price, &created);
    if (level == NULL) {
        return false;
    }

    order = book_alloc(book, &book->order_pool, sizeof(Order));
    if (order == NULL) {
        if (created) {
            price_level_remove_node(book, tree, price);
        }
        return false;
    }

    book->timestamp += 1U;
    order_init(order, id, side, price, qty, book->timestamp);
    order_list_append(level, order);

    if (!order_map_put(&book->order_map, id, order)) {
        order_list_remove(level, order);
        book_free(book, &book->order_pool, order);
        if (created) {
            price_level_remove_node(book, tree, price);
        }
        return false;
    }

    order_book_refresh_bbo(book);
    return true;
}

bool order_book_remove(OrderBook *book, uint64_t id) {
    Order *order;
    PriceLevel *level;
    PriceLevel **tree;
    char side;
    int price;

    if (book == NULL) {
        return false;
    }

    order = order_map_get(&book->order_map, id);
    if (order == NULL) {
        return false;
    }

    level = order->level;
    side = order->side;
    price = order->price;
    tree = side == 'B' ? &book->bids : &book->asks;

    order_list_remove(level, order);
    order_map_erase(&book->order_map, id);
    book_free(book, &book->order_pool, order);

    if (level->head == NULL) {
        price_level_remove_node(book, tree, price);
    }

    order_book_refresh_bbo(book);
    return true;
}

bool order_book_fill(OrderBook *book, Order *order, int qty) {
    PriceLevel *level;
    PriceLevel **tree;
    char side;
    int price;

    if (book == NULL || order == NULL || order->level == NULL || qty <= 0 || qty > order->qty) {
        return false;
    }

    level = order->level;
    side = order->side;
    price = order->price;
    tree = side == 'B' ? &book->bids : &book->asks;

    order->qty -= qty;
    level->total_qty -= qty;

    if (order->qty == 0) {
        order_list_remove(level, order);
        order_map_erase(&book->order_map, order->id);
        book_free(book, &book->order_pool, order);

        if (level->head == NULL) {
            price_level_remove_node(book, tree, price);
        }
    }

    order_book_refresh_bbo(book);
    return true;
}

bool order_book_modify_quantity(OrderBook *book, uint64_t id, int qty) {
    Order *order;
    PriceLevel *level;
    int delta;

    if (book == NULL || qty <= 0) {
        return false;
    }

    order = order_map_get(&book->order_map, id);
    if (order == NULL || order->level == NULL) {
        return false;
    }

    level = order->level;
    delta = qty - order->qty;
    order->qty = qty;
    level->total_qty += delta;
    return true;
}

Order *order_book_find(const OrderBook *book, uint64_t id) {
    if (book == NULL) {
        return NULL;
    }

    return order_map_get(&book->order_map, id);
}

const PriceLevel *order_book_best_bid(const OrderBook *book) {
    return book == NULL ? NULL : book->best_bid;
}

const PriceLevel *order_book_best_ask(const OrderBook *book) {
    return book == NULL ? NULL : book->best_ask;
}
