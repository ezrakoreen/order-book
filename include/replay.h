#ifndef REPLAY_H
#define REPLAY_H

#include <stdbool.h>
#include <stddef.h>

#include "engine.h"

bool replay_file(MatchingEngine *engine, const char *path, char *error, size_t error_size);
void replay_print_snapshot(const MatchingEngine *engine);

#endif
