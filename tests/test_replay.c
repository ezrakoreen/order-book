#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "replay.h"

static const char *replay_path = "/private/tmp/order-book-test-replay.txt";

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_replay: %s\n", message);
        exit(1);
    }
}

static void write_text_file(const char *contents) {
    FILE *file = fopen(replay_path, "w");

    expect(file != NULL, "test input file should open for writing");
    expect(fputs(contents, file) >= 0, "test input file should be written");
    expect(fclose(file) == 0, "test input file should close cleanly");
}

static void test_valid_replay_updates_book_state(void) {
    MatchingEngine engine;
    char error[256] = "";
    const PriceLevel *best_ask;

    write_text_file("ADD 1 B 100 10\n"
                    "ADD 2 S 105 7\n"
                    "MODIFY 2 104 9\n"
                    "MARKET 3 B 4\n"
                    "CANCEL 1\n");

    expect(engine_init(&engine), "engine should initialize");
    expect(replay_file(&engine, replay_path, error, sizeof(error)), "valid replay should succeed");

    expect(engine_best_bid(&engine) == NULL, "canceled bid should leave no bids");
    best_ask = engine_best_ask(&engine);
    expect(best_ask != NULL, "partially filled ask should remain");
    expect(best_ask->price == 104, "modified ask price should be retained");
    expect(best_ask->total_qty == 5, "market fill should reduce ask quantity");
    expect(engine_find_order(&engine, 2U) != NULL, "resting ask should remain findable");
    expect(engine_find_order(&engine, 2U)->qty == 5, "resting ask quantity should match level");

    engine_destroy(&engine);
}

static void test_invalid_command_reports_line_number(void) {
    MatchingEngine engine;
    char error[256] = "";

    write_text_file("ADD 1 B 100 10\n"
                    "NOPE 2 S 101 5\n");

    expect(engine_init(&engine), "engine should initialize for invalid command");
    expect(!replay_file(&engine, replay_path, error, sizeof(error)), "unknown command should fail");
    expect(strcmp(error, "line 2: unknown command") == 0, "unknown command error should include line");

    engine_destroy(&engine);
}

static void test_malformed_add_reports_expected_shape(void) {
    MatchingEngine engine;
    char error[256] = "";

    write_text_file("ADD 1 B 100\n");

    expect(engine_init(&engine), "engine should initialize for malformed add");
    expect(!replay_file(&engine, replay_path, error, sizeof(error)), "malformed ADD should fail");
    expect(strcmp(error, "line 1: expected ADD <id> <B|S> <price> <qty>") == 0,
           "malformed ADD error should describe expected shape");

    engine_destroy(&engine);
}

static void test_large_replay_smoke(void) {
    MatchingEngine engine;
    FILE *file;
    char error[256] = "";
    int i;

    file = fopen(replay_path, "w");
    expect(file != NULL, "large replay file should open for writing");
    for (i = 1; i <= 10000; ++i) {
        expect(fprintf(file, "ADD %d B 100 1\nCANCEL %d\n", i, i) > 0,
               "large replay command should be written");
    }
    expect(fclose(file) == 0, "large replay file should close cleanly");

    expect(engine_init(&engine), "engine should initialize for large replay");
    expect(replay_file(&engine, replay_path, error, sizeof(error)), "large replay should succeed");
    expect(engine_best_bid(&engine) == NULL, "large add/cancel replay should leave no bids");
    expect(engine_best_ask(&engine) == NULL, "large add/cancel replay should leave no asks");

    engine_destroy(&engine);
}

int main(void) {
    test_valid_replay_updates_book_state();
    test_invalid_command_reports_line_number();
    test_malformed_add_reports_expected_shape();
    test_large_replay_smoke();
    remove(replay_path);
    return 0;
}
