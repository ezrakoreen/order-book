#include "replay.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    REPLAY_LINE_MAX = 256
};

static void set_error(char *error, size_t error_size, size_t line_no, const char *message) {
    if (error == NULL || error_size == 0U) {
        return;
    }

    if (line_no == 0U) {
        snprintf(error, error_size, "%s", message);
    } else {
        snprintf(error, error_size, "line %zu: %s", line_no, message);
    }
}

static char *next_token(char **cursor) {
    char *token;

    token = strtok(*cursor, " \t\r\n");
    *cursor = NULL;
    return token;
}

static bool parse_u64(const char *token, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (token == NULL || token[0] == '\0' || token[0] == '-') {
        return false;
    }

    errno = 0;
    parsed = strtoull(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0') {
        return false;
    }

    *value = (uint64_t)parsed;
    return true;
}

static bool parse_positive_int(const char *token, int *value) {
    char *end = NULL;
    long parsed;

    if (token == NULL || token[0] == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtol(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0' || parsed <= 0L || parsed > INT_MAX) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static bool parse_side(const char *token, char *side) {
    if (token == NULL || token[1] != '\0' || (token[0] != 'B' && token[0] != 'S')) {
        return false;
    }

    *side = token[0];
    return true;
}

static bool has_extra_token(char **cursor) {
    return next_token(cursor) != NULL;
}

static bool replay_add(MatchingEngine *engine, char **cursor, char *error, size_t error_size, size_t line_no) {
    uint64_t id;
    char side;
    int price;
    int qty;

    if (!parse_u64(next_token(cursor), &id) ||
        !parse_side(next_token(cursor), &side) ||
        !parse_positive_int(next_token(cursor), &price) ||
        !parse_positive_int(next_token(cursor), &qty) ||
        has_extra_token(cursor)) {
        set_error(error, error_size, line_no, "expected ADD <id> <B|S> <price> <qty>");
        return false;
    }

    if (!engine_add_limit(engine, id, side, price, qty)) {
        set_error(error, error_size, line_no, "ADD rejected by matching engine");
        return false;
    }

    return true;
}

static bool replay_market(MatchingEngine *engine,
                          char **cursor,
                          char *error,
                          size_t error_size,
                          size_t line_no) {
    uint64_t id;
    char side;
    int qty;

    if (!parse_u64(next_token(cursor), &id) ||
        !parse_side(next_token(cursor), &side) ||
        !parse_positive_int(next_token(cursor), &qty) ||
        has_extra_token(cursor)) {
        set_error(error, error_size, line_no, "expected MARKET <id> <B|S> <qty>");
        return false;
    }

    if (!engine_add_market(engine, id, side, qty)) {
        set_error(error, error_size, line_no, "MARKET rejected by matching engine");
        return false;
    }

    return true;
}

static bool replay_cancel(MatchingEngine *engine,
                          char **cursor,
                          char *error,
                          size_t error_size,
                          size_t line_no) {
    uint64_t id;

    if (!parse_u64(next_token(cursor), &id) || has_extra_token(cursor)) {
        set_error(error, error_size, line_no, "expected CANCEL <id>");
        return false;
    }

    if (!engine_cancel(engine, id)) {
        set_error(error, error_size, line_no, "CANCEL rejected by matching engine");
        return false;
    }

    return true;
}

static bool replay_modify(MatchingEngine *engine,
                          char **cursor,
                          char *error,
                          size_t error_size,
                          size_t line_no) {
    uint64_t id;
    int price;
    int qty;

    if (!parse_u64(next_token(cursor), &id) ||
        !parse_positive_int(next_token(cursor), &price) ||
        !parse_positive_int(next_token(cursor), &qty) ||
        has_extra_token(cursor)) {
        set_error(error, error_size, line_no, "expected MODIFY <id> <price> <qty>");
        return false;
    }

    if (!engine_modify(engine, id, price, qty)) {
        set_error(error, error_size, line_no, "MODIFY rejected by matching engine");
        return false;
    }

    return true;
}

static bool replay_line(MatchingEngine *engine,
                        char *line,
                        size_t line_no,
                        char *error,
                        size_t error_size) {
    char *cursor = line;
    char *command = next_token(&cursor);

    if (command == NULL || command[0] == '#') {
        return true;
    }

    if (strcmp(command, "ADD") == 0) {
        return replay_add(engine, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "MARKET") == 0) {
        return replay_market(engine, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "CANCEL") == 0) {
        return replay_cancel(engine, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "MODIFY") == 0) {
        return replay_modify(engine, &cursor, error, error_size, line_no);
    }

    set_error(error, error_size, line_no, "unknown command");
    return false;
}

bool replay_file(MatchingEngine *engine, const char *path, char *error, size_t error_size) {
    FILE *file;
    char line[REPLAY_LINE_MAX];
    size_t line_no = 0U;

    if (engine == NULL || path == NULL) {
        set_error(error, error_size, 0U, "invalid replay arguments");
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        set_error(error, error_size, 0U, "failed to open input file");
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t len = strlen(line);

        line_no += 1U;
        if (len > 0U && line[len - 1U] != '\n' && !feof(file)) {
            int ch;

            while ((ch = fgetc(file)) != '\n' && ch != EOF) {
            }
            set_error(error, error_size, line_no, "line is too long");
            fclose(file);
            return false;
        }

        if (!replay_line(engine, line, line_no, error, error_size)) {
            fclose(file);
            return false;
        }
    }

    if (ferror(file)) {
        set_error(error, error_size, 0U, "failed while reading input file");
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

static void print_level_orders(const PriceLevel *level) {
    const Order *order;

    for (order = level->head; order != NULL; order = order->next) {
        printf(" %llu:%d", (unsigned long long)order->id, order->qty);
    }
}

static void print_bids_desc(const PriceLevel *level) {
    if (level == NULL) {
        return;
    }

    print_bids_desc(level->right);
    printf("BID price=%d qty=%d orders=", level->price, level->total_qty);
    print_level_orders(level);
    printf("\n");
    print_bids_desc(level->left);
}

static void print_asks_asc(const PriceLevel *level) {
    if (level == NULL) {
        return;
    }

    print_asks_asc(level->left);
    printf("ASK price=%d qty=%d orders=", level->price, level->total_qty);
    print_level_orders(level);
    printf("\n");
    print_asks_asc(level->right);
}

void replay_print_snapshot(const MatchingEngine *engine) {
    if (engine == NULL || engine->book == NULL) {
        return;
    }

    printf("SNAPSHOT\n");
    print_bids_desc(engine->book->bids);
    print_asks_asc(engine->book->asks);
}
