#ifndef REPLAY_H
#define REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "engine.h"

typedef enum ReplayCommandType {
    REPLAY_COMMAND_ADD,
    REPLAY_COMMAND_MARKET,
    REPLAY_COMMAND_CANCEL,
    REPLAY_COMMAND_MODIFY
} ReplayCommandType;

typedef struct ReplayCommand {
    ReplayCommandType type;
    uint64_t id;
    char side;
    int price;
    int qty;
} ReplayCommand;

bool replay_parse_line(const char *line,
                       size_t line_no,
                       ReplayCommand *command,
                       bool *has_command,
                       char *error,
                       size_t error_size);
bool replay_apply_command(MatchingEngine *engine,
                          const ReplayCommand *command,
                          size_t line_no,
                          char *error,
                          size_t error_size);
bool replay_file(MatchingEngine *engine, const char *path, char *error, size_t error_size);
void replay_print_snapshot(const MatchingEngine *engine);

#endif
