#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BENCHMARK_COMMAND_TYPE_COUNT = 4,
    BENCHMARK_FILL_BUCKET_COUNT = 5,
    BENCHMARK_NAME_MAX = 64
};

typedef struct BenchmarkLatencyStats {
    size_t count;
    uint64_t latency_ns_avg;
    uint64_t latency_ns_p50;
    uint64_t latency_ns_p99;
    uint64_t latency_ns_p999;
} BenchmarkLatencyStats;

typedef struct BenchmarkResult {
    char name[BENCHMARK_NAME_MAX];
    size_t messages;
    double messages_per_second;
    BenchmarkLatencyStats overall;
    BenchmarkLatencyStats by_command[BENCHMARK_COMMAND_TYPE_COUNT];
    BenchmarkLatencyStats by_fill_bucket[BENCHMARK_FILL_BUCKET_COUNT];
} BenchmarkResult;

bool benchmark_file(const char *path, BenchmarkResult *result, char *error, size_t error_size);
bool benchmark_scenario(const char *name,
                        size_t messages,
                        BenchmarkResult *result,
                        char *error,
                        size_t error_size);
void benchmark_print_result(const BenchmarkResult *result);
void benchmark_print_scenarios(void);
size_t benchmark_scenario_count(void);
const char *benchmark_scenario_name(size_t index);

#endif
