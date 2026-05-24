#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "replay.h"

static void usage(const char *program) {
    fprintf(stderr, "usage: %s [--verbose] [--snapshot] <input-file>\n", program);
}

int main(int argc, char **argv) {
    MatchingEngine engine;
    const char *path = NULL;
    bool verbose = false;
    bool snapshot = false;
    char error[256];
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--snapshot") == 0) {
            snapshot = true;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (path == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (!engine_init(&engine)) {
        fprintf(stderr, "error: failed to initialize matching engine\n");
        return 1;
    }

    engine_set_verbose(&engine, verbose);

    if (!replay_file(&engine, path, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        engine_destroy(&engine);
        return 1;
    }

    if (snapshot) {
        replay_print_snapshot(&engine);
    }

    engine_destroy(&engine);
    return 0;
}
