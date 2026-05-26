#include "benchmark.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "replay.h"

enum {
    BENCH_LINE_MAX = 256,
    DEFAULT_DEEP_LEVELS = 10000
};

typedef enum BenchmarkScenario {
    SCENARIO_ADD_ONLY,
    SCENARIO_ADD_CANCEL,
    SCENARIO_CANCEL_ONLY,
    SCENARIO_MODIFY_QTY_DOWN,
    SCENARIO_MODIFY_REPRICE,
    SCENARIO_LIMIT_CROSS,
    SCENARIO_MARKET_MATCH_1,
    SCENARIO_MARKET_SWEEP_10,
    SCENARIO_DEEP_BOOK_BEST_PRICE,
    SCENARIO_MIXED
} BenchmarkScenario;

typedef struct ScenarioDefinition {
    const char *name;
    BenchmarkScenario scenario;
} ScenarioDefinition;

typedef struct BenchCommand {
    ReplayCommand command;
    size_t line_no;
} BenchCommand;

typedef struct LatencyCollector {
    uint64_t *values;
    size_t count;
    size_t capacity;
    uint64_t sum;
} LatencyCollector;

static const ScenarioDefinition scenario_definitions[] = {
    {"add_only", SCENARIO_ADD_ONLY},
    {"add_cancel", SCENARIO_ADD_CANCEL},
    {"cancel_only", SCENARIO_CANCEL_ONLY},
    {"modify_qty_down", SCENARIO_MODIFY_QTY_DOWN},
    {"modify_reprice", SCENARIO_MODIFY_REPRICE},
    {"limit_cross", SCENARIO_LIMIT_CROSS},
    {"market_match_1", SCENARIO_MARKET_MATCH_1},
    {"market_sweep_10", SCENARIO_MARKET_SWEEP_10},
    {"deep_book_best_price", SCENARIO_DEEP_BOOK_BEST_PRICE},
    {"mixed", SCENARIO_MIXED}
};

static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0U) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static uint64_t elapsed_ns(const struct timespec *start, const struct timespec *end) {
    uint64_t seconds = (uint64_t)(end->tv_sec - start->tv_sec);
    uint64_t nanos;

    if (end->tv_nsec >= start->tv_nsec) {
        nanos = (uint64_t)(end->tv_nsec - start->tv_nsec);
    } else {
        seconds -= 1U;
        nanos = (uint64_t)(1000000000L + end->tv_nsec - start->tv_nsec);
    }

    return seconds * 1000000000ULL + nanos;
}

static int compare_u64(const void *left, const void *right) {
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;

    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static uint64_t percentile(const uint64_t *latencies, size_t count, unsigned numerator, unsigned denominator) {
    size_t index;

    if (count == 0U) {
        return 0U;
    }

    index = ((count * numerator) + denominator - 1U) / denominator;
    if (index == 0U) {
        index = 1U;
    }

    return latencies[index - 1U];
}

static bool collector_append(LatencyCollector *collector, uint64_t value, char *error, size_t error_size) {
    uint64_t *grown;
    size_t next_capacity;

    if (collector->count == collector->capacity) {
        next_capacity = collector->capacity == 0U ? 1024U : collector->capacity * 2U;
        grown = realloc(collector->values, next_capacity * sizeof(*collector->values));
        if (grown == NULL) {
            set_error(error, error_size, "failed to allocate benchmark latencies");
            return false;
        }
        collector->values = grown;
        collector->capacity = next_capacity;
    }

    collector->values[collector->count] = value;
    collector->count += 1U;
    collector->sum += value;
    return true;
}

static void collector_destroy(LatencyCollector *collector) {
    free(collector->values);
    collector->values = NULL;
    collector->count = 0U;
    collector->capacity = 0U;
    collector->sum = 0U;
}

static void collector_finish(LatencyCollector *collector, BenchmarkLatencyStats *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->count = collector->count;
    if (collector->count == 0U) {
        return;
    }

    qsort(collector->values, collector->count, sizeof(*collector->values), compare_u64);
    stats->latency_ns_avg = collector->sum / collector->count;
    stats->latency_ns_p50 = percentile(collector->values, collector->count, 50U, 100U);
    stats->latency_ns_p99 = percentile(collector->values, collector->count, 99U, 100U);
    stats->latency_ns_p999 = percentile(collector->values, collector->count, 999U, 1000U);
}

static size_t command_index(ReplayCommandType type) {
    switch (type) {
        case REPLAY_COMMAND_ADD:
            return 0U;
        case REPLAY_COMMAND_MARKET:
            return 1U;
        case REPLAY_COMMAND_CANCEL:
            return 2U;
        case REPLAY_COMMAND_MODIFY:
            return 3U;
    }

    return 0U;
}

static const char *command_label(size_t index) {
    static const char *labels[BENCHMARK_COMMAND_TYPE_COUNT] = {"ADD", "MARKET", "CANCEL", "MODIFY"};

    return index < BENCHMARK_COMMAND_TYPE_COUNT ? labels[index] : "UNKNOWN";
}

static size_t fill_bucket(size_t fills) {
    if (fills == 0U) {
        return 0U;
    }
    if (fills == 1U) {
        return 1U;
    }
    if (fills <= 5U) {
        return 2U;
    }
    if (fills <= 20U) {
        return 3U;
    }
    return 4U;
}

static const char *fill_bucket_label(size_t index) {
    static const char *labels[BENCHMARK_FILL_BUCKET_COUNT] = {
        "fills_0",
        "fills_1",
        "fills_2_5",
        "fills_6_20",
        "fills_20_plus"
    };

    return index < BENCHMARK_FILL_BUCKET_COUNT ? labels[index] : "fills_unknown";
}

static bool append_command(BenchCommand **commands,
                           size_t *count,
                           size_t *capacity,
                           const ReplayCommand *command,
                           size_t line_no,
                           char *error,
                           size_t error_size) {
    BenchCommand *grown;
    size_t next_capacity;

    if (*count == *capacity) {
        next_capacity = *capacity == 0U ? 1024U : *capacity * 2U;
        grown = realloc(*commands, next_capacity * sizeof(**commands));
        if (grown == NULL) {
            set_error(error, error_size, "failed to allocate benchmark commands");
            return false;
        }
        *commands = grown;
        *capacity = next_capacity;
    }

    (*commands)[*count].command = *command;
    (*commands)[*count].line_no = line_no;
    *count += 1U;
    return true;
}

static ReplayCommand make_add(uint64_t id, char side, int price, int qty) {
    ReplayCommand command;

    command.type = REPLAY_COMMAND_ADD;
    command.id = id;
    command.side = side;
    command.price = price;
    command.qty = qty;
    return command;
}

static ReplayCommand make_market(uint64_t id, char side, int qty) {
    ReplayCommand command;

    command.type = REPLAY_COMMAND_MARKET;
    command.id = id;
    command.side = side;
    command.price = 0;
    command.qty = qty;
    return command;
}

static ReplayCommand make_cancel(uint64_t id) {
    ReplayCommand command;

    command.type = REPLAY_COMMAND_CANCEL;
    command.id = id;
    command.side = '\0';
    command.price = 0;
    command.qty = 0;
    return command;
}

static ReplayCommand make_modify(uint64_t id, int price, int qty) {
    ReplayCommand command;

    command.type = REPLAY_COMMAND_MODIFY;
    command.id = id;
    command.side = '\0';
    command.price = price;
    command.qty = qty;
    return command;
}

static bool load_commands(const char *path,
                          BenchCommand **commands,
                          size_t *count,
                          char *error,
                          size_t error_size) {
    FILE *file;
    char line[BENCH_LINE_MAX];
    size_t capacity = 0U;
    size_t line_no = 0U;

    file = fopen(path, "r");
    if (file == NULL) {
        set_error(error, error_size, "failed to open input file");
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t len = strlen(line);
        ReplayCommand command;
        bool has_command;

        line_no += 1U;
        if (len > 0U && line[len - 1U] != '\n' && !feof(file)) {
            int ch;

            while ((ch = fgetc(file)) != '\n' && ch != EOF) {
            }
            snprintf(error, error_size, "line %zu: line is too long", line_no);
            fclose(file);
            free(*commands);
            *commands = NULL;
            *count = 0U;
            return false;
        }

        if (!replay_parse_line(line, line_no, &command, &has_command, error, error_size)) {
            fclose(file);
            free(*commands);
            *commands = NULL;
            *count = 0U;
            return false;
        }

        if (has_command &&
            !append_command(commands, count, &capacity, &command, line_no, error, error_size)) {
            fclose(file);
            free(*commands);
            *commands = NULL;
            *count = 0U;
            return false;
        }
    }

    if (ferror(file)) {
        set_error(error, error_size, "failed while reading input file");
        fclose(file);
        free(*commands);
        *commands = NULL;
        *count = 0U;
        return false;
    }

    fclose(file);
    return true;
}

static bool find_scenario(const char *name, BenchmarkScenario *scenario) {
    size_t i;

    for (i = 0U; i < sizeof(scenario_definitions) / sizeof(scenario_definitions[0]); ++i) {
        if (strcmp(name, scenario_definitions[i].name) == 0) {
            *scenario = scenario_definitions[i].scenario;
            return true;
        }
    }

    return false;
}

static bool generate_scenario(BenchmarkScenario scenario,
                              size_t messages,
                              BenchCommand **commands,
                              size_t *count,
                              char *error,
                              size_t error_size) {
    size_t capacity = 0U;
    size_t i;

    for (i = 0U; i < messages; ++i) {
        ReplayCommand command;
        uint64_t id = (uint64_t)i + 1ULL;

        switch (scenario) {
            case SCENARIO_ADD_ONLY:
                command = make_add(id, 'B', 100, 1);
                break;
            case SCENARIO_ADD_CANCEL:
                if ((i % 2U) == 0U) {
                    command = make_add(((uint64_t)i / 2ULL) + 1ULL, 'B', 100, 1);
                } else {
                    command = make_cancel(((uint64_t)i / 2ULL) + 1ULL);
                }
                break;
            case SCENARIO_CANCEL_ONLY:
                command = make_cancel(id);
                break;
            case SCENARIO_MODIFY_QTY_DOWN:
                command = make_modify(id, 100, 1);
                break;
            case SCENARIO_MODIFY_REPRICE:
                command = make_modify(id, 99, 1);
                break;
            case SCENARIO_LIMIT_CROSS:
                command = make_add(1000000000ULL + id, 'B', 101, 1);
                break;
            case SCENARIO_MARKET_MATCH_1:
                command = make_market(2000000000ULL + id, 'B', 1);
                break;
            case SCENARIO_MARKET_SWEEP_10:
                command = make_market(3000000000ULL + id, 'B', 10);
                break;
            case SCENARIO_DEEP_BOOK_BEST_PRICE:
                if ((i % 2U) == 0U) {
                    command = make_add(4000000000ULL + ((uint64_t)i / 2ULL), 'B', 90 - (int)(i % 50U), 1);
                } else {
                    command = make_cancel(4000000000ULL + ((uint64_t)i / 2ULL));
                }
                break;
            case SCENARIO_MIXED:
                switch (i % 8U) {
                    case 0U:
                        command = make_add(5000000000ULL + id, 'B', 100, 2);
                        break;
                    case 1U:
                        command = make_add(6000000000ULL + id, 'S', 101, 2);
                        break;
                    case 2U:
                        command = make_modify(5000000000ULL + id - 2ULL, 99, 1);
                        break;
                    case 3U:
                        command = make_cancel(6000000000ULL + id - 2ULL);
                        break;
                    case 4U:
                        command = make_add(7000000000ULL + id, 'S', 100, 1);
                        break;
                    case 5U:
                        command = make_add(8000000000ULL + id, 'B', 101, 1);
                        break;
                    case 6U:
                        command = make_add(9000000000ULL + id, 'S', 100, 1);
                        break;
                    default:
                        command = make_market(10000000000ULL + id, 'B', 1);
                        break;
                }
                break;
        }

        if (!append_command(commands, count, &capacity, &command, i + 1U, error, error_size)) {
            free(*commands);
            *commands = NULL;
            *count = 0U;
            return false;
        }
    }

    return true;
}

static bool prepopulate_scenario(MatchingEngine *engine,
                                 BenchmarkScenario scenario,
                                 size_t messages,
                                 char *error,
                                 size_t error_size) {
    size_t i;

    switch (scenario) {
        case SCENARIO_ADD_ONLY:
        case SCENARIO_ADD_CANCEL:
        case SCENARIO_MIXED:
            return true;
        case SCENARIO_CANCEL_ONLY:
            for (i = 0U; i < messages; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'B', 100, 1)) {
                    set_error(error, error_size, "failed to prepopulate cancel_only");
                    return false;
                }
            }
            return true;
        case SCENARIO_MODIFY_QTY_DOWN:
            for (i = 0U; i < messages; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'B', 100, 2)) {
                    set_error(error, error_size, "failed to prepopulate modify_qty_down");
                    return false;
                }
            }
            return true;
        case SCENARIO_MODIFY_REPRICE:
            for (i = 0U; i < messages; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'B', 100, 1)) {
                    set_error(error, error_size, "failed to prepopulate modify_reprice");
                    return false;
                }
            }
            return true;
        case SCENARIO_LIMIT_CROSS:
        case SCENARIO_MARKET_MATCH_1:
            for (i = 0U; i < messages; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'S', 100, 1)) {
                    set_error(error, error_size, "failed to prepopulate matching scenario");
                    return false;
                }
            }
            return true;
        case SCENARIO_MARKET_SWEEP_10:
            for (i = 0U; i < messages * 10U; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'S', 100, 1)) {
                    set_error(error, error_size, "failed to prepopulate market_sweep_10");
                    return false;
                }
            }
            return true;
        case SCENARIO_DEEP_BOOK_BEST_PRICE:
            for (i = 0U; i < DEFAULT_DEEP_LEVELS; ++i) {
                if (!engine_add_limit(engine, (uint64_t)i + 1ULL, 'B', 20000 - (int)i, 1)) {
                    set_error(error, error_size, "failed to prepopulate deep_book_best_price");
                    return false;
                }
            }
            return true;
    }

    set_error(error, error_size, "unknown benchmark scenario");
    return false;
}

static bool run_commands(const char *name,
                         BenchCommand *commands,
                         size_t count,
                         BenchmarkScenario *scenario,
                         BenchmarkResult *result,
                         char *error,
                         size_t error_size) {
    LatencyCollector overall = {0};
    LatencyCollector by_command[BENCHMARK_COMMAND_TYPE_COUNT] = {{0}};
    LatencyCollector by_fill_bucket[BENCHMARK_FILL_BUCKET_COUNT] = {{0}};
    MatchingEngine engine;
    struct timespec total_start;
    struct timespec total_end;
    uint64_t total_ns;
    size_t i;

    memset(result, 0, sizeof(*result));
    snprintf(result->name, sizeof(result->name), "%s", name);

    if (!engine_init(&engine)) {
        set_error(error, error_size, "failed to initialize matching engine");
        return false;
    }

    if (scenario != NULL && !prepopulate_scenario(&engine, *scenario, count, error, error_size)) {
        engine_destroy(&engine);
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &total_start);
    for (i = 0U; i < count; ++i) {
        struct timespec start;
        struct timespec end;
        uint64_t latency;
        size_t type_index;
        size_t bucket_index;

        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
        if (!replay_apply_command(&engine,
                                  &commands[i].command,
                                  commands[i].line_no,
                                  error,
                                  error_size)) {
            engine_destroy(&engine);
            return false;
        }
        clock_gettime(CLOCK_MONOTONIC_RAW, &end);

        latency = elapsed_ns(&start, &end);
        type_index = command_index(commands[i].command.type);
        bucket_index = fill_bucket(engine_last_fill_count(&engine));

        if (!collector_append(&overall, latency, error, error_size) ||
            !collector_append(&by_command[type_index], latency, error, error_size) ||
            !collector_append(&by_fill_bucket[bucket_index], latency, error, error_size)) {
            engine_destroy(&engine);
            return false;
        }
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &total_end);

    total_ns = elapsed_ns(&total_start, &total_end);
    result->messages = count;
    result->messages_per_second =
        total_ns == 0U ? 0.0 : ((double)count * 1000000000.0) / (double)total_ns;
    collector_finish(&overall, &result->overall);
    for (i = 0U; i < BENCHMARK_COMMAND_TYPE_COUNT; ++i) {
        collector_finish(&by_command[i], &result->by_command[i]);
    }
    for (i = 0U; i < BENCHMARK_FILL_BUCKET_COUNT; ++i) {
        collector_finish(&by_fill_bucket[i], &result->by_fill_bucket[i]);
    }

    collector_destroy(&overall);
    for (i = 0U; i < BENCHMARK_COMMAND_TYPE_COUNT; ++i) {
        collector_destroy(&by_command[i]);
    }
    for (i = 0U; i < BENCHMARK_FILL_BUCKET_COUNT; ++i) {
        collector_destroy(&by_fill_bucket[i]);
    }
    engine_destroy(&engine);
    return true;
}

bool benchmark_file(const char *path, BenchmarkResult *result, char *error, size_t error_size) {
    BenchCommand *commands = NULL;
    size_t count = 0U;
    bool ok;

    if (path == NULL || result == NULL) {
        set_error(error, error_size, "invalid benchmark arguments");
        return false;
    }

    if (!load_commands(path, &commands, &count, error, error_size)) {
        return false;
    }

    ok = run_commands("file", commands, count, NULL, result, error, error_size);
    free(commands);
    return ok;
}

bool benchmark_scenario(const char *name,
                        size_t messages,
                        BenchmarkResult *result,
                        char *error,
                        size_t error_size) {
    BenchmarkScenario scenario;
    BenchCommand *commands = NULL;
    size_t count = 0U;
    bool ok;

    if (name == NULL || result == NULL || messages == 0U) {
        set_error(error, error_size, "invalid benchmark arguments");
        return false;
    }
    if (!find_scenario(name, &scenario)) {
        set_error(error, error_size, "unknown benchmark scenario");
        return false;
    }
    if (!generate_scenario(scenario, messages, &commands, &count, error, error_size)) {
        return false;
    }

    ok = run_commands(name, commands, count, &scenario, result, error, error_size);
    free(commands);
    return ok;
}

static void print_latency_line(const char *label, const BenchmarkLatencyStats *stats) {
    printf("%s_count: %zu\n", label, stats->count);
    printf("%s_latency_ns_avg: %" PRIu64 "\n", label, stats->latency_ns_avg);
    printf("%s_latency_ns_p50: %" PRIu64 "\n", label, stats->latency_ns_p50);
    printf("%s_latency_ns_p99: %" PRIu64 "\n", label, stats->latency_ns_p99);
    printf("%s_latency_ns_p999: %" PRIu64 "\n", label, stats->latency_ns_p999);
}

void benchmark_print_result(const BenchmarkResult *result) {
    size_t i;

    if (result == NULL) {
        return;
    }

    printf("scenario: %s\n", result->name);
    printf("messages: %zu\n", result->messages);
    printf("throughput: %.2fM msg/s\n", result->messages_per_second / 1000000.0);
    print_latency_line("overall", &result->overall);

    for (i = 0U; i < BENCHMARK_COMMAND_TYPE_COUNT; ++i) {
        if (result->by_command[i].count > 0U) {
            print_latency_line(command_label(i), &result->by_command[i]);
        }
    }

    for (i = 0U; i < BENCHMARK_FILL_BUCKET_COUNT; ++i) {
        if (result->by_fill_bucket[i].count > 0U) {
            print_latency_line(fill_bucket_label(i), &result->by_fill_bucket[i]);
        }
    }
}

void benchmark_print_scenarios(void) {
    size_t i;

    for (i = 0U; i < benchmark_scenario_count(); ++i) {
        printf("%s\n", scenario_definitions[i].name);
    }
}

size_t benchmark_scenario_count(void) {
    return sizeof(scenario_definitions) / sizeof(scenario_definitions[0]);
}

const char *benchmark_scenario_name(size_t index) {
    if (index >= benchmark_scenario_count()) {
        return NULL;
    }

    return scenario_definitions[index].name;
}
