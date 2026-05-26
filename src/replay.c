#include "replay.h"

#include <errno.h>
#include <limits.h>
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

static bool parse_add(ReplayCommand *command,
                      char **cursor,
                      char *error,
                      size_t error_size,
                      size_t line_no) {
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

    command->type = REPLAY_COMMAND_ADD;
    command->id = id;
    command->side = side;
    command->price = price;
    command->qty = qty;
    return true;
}

static bool parse_market(ReplayCommand *command,
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

    command->type = REPLAY_COMMAND_MARKET;
    command->id = id;
    command->side = side;
    command->price = 0;
    command->qty = qty;
    return true;
}

static bool parse_cancel(ReplayCommand *command,
                         char **cursor,
                         char *error,
                         size_t error_size,
                         size_t line_no) {
    uint64_t id;

    if (!parse_u64(next_token(cursor), &id) || has_extra_token(cursor)) {
        set_error(error, error_size, line_no, "expected CANCEL <id>");
        return false;
    }

    command->type = REPLAY_COMMAND_CANCEL;
    command->id = id;
    command->side = '\0';
    command->price = 0;
    command->qty = 0;
    return true;
}

static bool parse_modify(ReplayCommand *command,
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

    command->type = REPLAY_COMMAND_MODIFY;
    command->id = id;
    command->side = '\0';
    command->price = price;
    command->qty = qty;
    return true;
}

bool replay_parse_line(const char *line,
                       size_t line_no,
                       ReplayCommand *parsed,
                       bool *has_command,
                       char *error,
                       size_t error_size) {
    char scratch[REPLAY_LINE_MAX];
    char *cursor = scratch;
    char *command;

    if (line == NULL || parsed == NULL || has_command == NULL) {
        set_error(error, error_size, 0U, "invalid replay arguments");
        return false;
    }

    if (strlen(line) >= sizeof(scratch)) {
        set_error(error, error_size, line_no, "line is too long");
        return false;
    }

    memcpy(scratch, line, strlen(line) + 1U);
    command = next_token(&cursor);
    *has_command = false;

    if (command == NULL || command[0] == '#') {
        return true;
    }

    *has_command = true;
    if (strcmp(command, "ADD") == 0) {
        return parse_add(parsed, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "MARKET") == 0) {
        return parse_market(parsed, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "CANCEL") == 0) {
        return parse_cancel(parsed, &cursor, error, error_size, line_no);
    }
    if (strcmp(command, "MODIFY") == 0) {
        return parse_modify(parsed, &cursor, error, error_size, line_no);
    }

    set_error(error, error_size, line_no, "unknown command");
    return false;
}

bool replay_apply_command(MatchingEngine *engine,
                          const ReplayCommand *command,
                          size_t line_no,
                          char *error,
                          size_t error_size) {
    if (engine == NULL || command == NULL) {
        set_error(error, error_size, 0U, "invalid replay arguments");
        return false;
    }

    switch (command->type) {
        case REPLAY_COMMAND_ADD:
            if (engine_add_limit(engine, command->id, command->side, command->price, command->qty)) {
                return true;
            }
            set_error(error, error_size, line_no, "ADD rejected by matching engine");
            return false;
        case REPLAY_COMMAND_MARKET:
            if (engine_add_market(engine, command->id, command->side, command->qty)) {
                return true;
            }
            set_error(error, error_size, line_no, "MARKET rejected by matching engine");
            return false;
        case REPLAY_COMMAND_CANCEL:
            if (engine_cancel(engine, command->id)) {
                return true;
            }
            set_error(error, error_size, line_no, "CANCEL rejected by matching engine");
            return false;
        case REPLAY_COMMAND_MODIFY:
            if (engine_modify(engine, command->id, command->price, command->qty)) {
                return true;
            }
            set_error(error, error_size, line_no, "MODIFY rejected by matching engine");
            return false;
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

        {
            ReplayCommand command;
            bool has_command;

            if (!replay_parse_line(line, line_no, &command, &has_command, error, error_size)) {
                fclose(file);
                return false;
            }
            if (has_command &&
                !replay_apply_command(engine, &command, line_no, error, error_size)) {
                fclose(file);
                return false;
            }
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
