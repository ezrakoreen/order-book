#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmark.h"
#include "engine.h"
#include "replay.h"

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s [--verbose] [--snapshot] <input-file>\n"
            "       %s --bench [--bench-allocators] <input-file>\n"
            "       %s --bench-scenario <name> [--bench-allocator <memory_pool|malloc>] [--bench-allocators] [--messages <count>]\n"
            "       %s --bench-all [--bench-allocator <memory_pool|malloc>] [--bench-allocators] [--messages <count>]\n"
            "       %s --bench-list\n",
            program,
            program,
            program,
            program,
            program);
}

static bool parse_size_arg(const char *text, size_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-') {
        return false;
    }

    parsed = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || parsed == 0ULL) {
        return false;
    }

    *value = (size_t)parsed;
    return true;
}

int main(int argc, char **argv) {
    MatchingEngine engine;
    const char *path = NULL;
    const char *bench_scenario = NULL;
    bool verbose = false;
    bool snapshot = false;
    bool bench = false;
    bool bench_all = false;
    bool bench_allocators = false;
    bool bench_list = false;
    OrderBookAllocator bench_allocator = ORDER_BOOK_ALLOCATOR_POOL;
    size_t bench_messages = 1000000U;
    char error[256];
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--snapshot") == 0) {
            snapshot = true;
        } else if (strcmp(argv[i], "--bench") == 0) {
            bench = true;
        } else if (strcmp(argv[i], "--bench-scenario") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            bench_scenario = argv[++i];
        } else if (strcmp(argv[i], "--bench-all") == 0) {
            bench_all = true;
        } else if (strcmp(argv[i], "--bench-allocators") == 0) {
            bench_allocators = true;
        } else if (strcmp(argv[i], "--bench-allocator") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            i += 1;
            if (strcmp(argv[i], "memory_pool") == 0) {
                bench_allocator = ORDER_BOOK_ALLOCATOR_POOL;
            } else if (strcmp(argv[i], "malloc") == 0) {
                bench_allocator = ORDER_BOOK_ALLOCATOR_MALLOC;
            } else {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--bench-list") == 0) {
            bench_list = true;
        } else if (strcmp(argv[i], "--messages") == 0) {
            if (i + 1 >= argc || !parse_size_arg(argv[i + 1], &bench_messages)) {
                usage(argv[0]);
                return 2;
            }
            i += 1;
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

    if (bench_list) {
        if (bench || bench_scenario != NULL || bench_all || bench_allocators ||
            bench_allocator != ORDER_BOOK_ALLOCATOR_POOL || path != NULL || verbose || snapshot) {
            usage(argv[0]);
            return 2;
        }
        benchmark_print_scenarios();
        return 0;
    }

    if (bench || bench_scenario != NULL || bench_all) {
        BenchmarkAllocatorComparison comparison;
        BenchmarkResult result;

        if (verbose || snapshot || (bench_allocators && bench_allocator != ORDER_BOOK_ALLOCATOR_POOL)) {
            usage(argv[0]);
            return 2;
        }

        if (bench) {
            if (path == NULL || bench_scenario != NULL || bench_all) {
                usage(argv[0]);
                return 2;
            }
            if (bench_allocators) {
                if (!benchmark_file_allocator_comparison(path, &comparison, error, sizeof(error))) {
                    fprintf(stderr, "error: %s\n", error);
                    return 1;
                }
                benchmark_print_allocator_comparison(&comparison);
                return 0;
            }
            if (!benchmark_file(path, &result, error, sizeof(error))) {
                fprintf(stderr, "error: %s\n", error);
                return 1;
            }
            benchmark_print_result(&result);
            return 0;
        }

        if (bench_scenario != NULL) {
            if (path != NULL || bench_all) {
                usage(argv[0]);
                return 2;
            }
            if (bench_allocators) {
                if (!benchmark_scenario_allocator_comparison(bench_scenario,
                                                             bench_messages,
                                                             &comparison,
                                                             error,
                                                             sizeof(error))) {
                    fprintf(stderr, "error: %s\n", error);
                    return 1;
                }
                benchmark_print_allocator_comparison(&comparison);
                return 0;
            }
            if (!benchmark_scenario_with_allocator(bench_scenario,
                                                   bench_messages,
                                                   bench_allocator,
                                                   &result,
                                                   error,
                                                   sizeof(error))) {
                fprintf(stderr, "error: %s\n", error);
                return 1;
            }
            benchmark_print_result(&result);
            return 0;
        }

        if (path != NULL) {
            usage(argv[0]);
            return 2;
        }

        for (size_t index = 0U; index < benchmark_scenario_count(); ++index) {
            const char *name = benchmark_scenario_name(index);

            if (bench_allocators) {
                if (!benchmark_scenario_allocator_comparison(name,
                                                             bench_messages,
                                                             &comparison,
                                                             error,
                                                             sizeof(error))) {
                    fprintf(stderr, "error: %s: %s\n", name, error);
                    return 1;
                }
                if (index > 0U) {
                    printf("\n");
                }
                benchmark_print_allocator_comparison(&comparison);
                continue;
            }

            if (!benchmark_scenario_with_allocator(name,
                                                   bench_messages,
                                                   bench_allocator,
                                                   &result,
                                                   error,
                                                   sizeof(error))) {
                fprintf(stderr, "error: %s: %s\n", name, error);
                return 1;
            }
            if (index > 0U) {
                printf("\n");
            }
            benchmark_print_result(&result);
        }
        return 0;
    }

    if (path == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (bench_messages != 1000000U || bench_allocators ||
        bench_allocator != ORDER_BOOK_ALLOCATOR_POOL) {
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
