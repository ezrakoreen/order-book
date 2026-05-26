#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmark.h"

static const char *benchmark_path = "/private/tmp/order-book-test-benchmark.txt";

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_benchmark: %s\n", message);
        exit(1);
    }
}

static void write_text_file(const char *contents) {
    FILE *file = fopen(benchmark_path, "w");

    expect(file != NULL, "test input file should open for writing");
    expect(fputs(contents, file) >= 0, "test input file should be written");
    expect(fclose(file) == 0, "test input file should close cleanly");
}

static void test_benchmark_reports_message_count_and_latency_stats(void) {
    BenchmarkResult result;
    char error[256] = "";

    write_text_file("# ignored\n"
                    "ADD 1 B 100 10\n"
                    "ADD 2 S 101 4\n"
                    "MARKET 3 B 2\n"
                    "CANCEL 1\n");

    expect(benchmark_file(benchmark_path, &result, error, sizeof(error)), "benchmark should succeed");
    expect(result.messages == 4U, "benchmark should count executable messages only");
    expect(result.messages_per_second > 0.0, "benchmark should report positive throughput");
    expect(result.overall.latency_ns_p50 <= result.overall.latency_ns_p99,
           "p50 should not exceed p99");
    expect(result.overall.latency_ns_p99 <= result.overall.latency_ns_p999,
           "p99 should not exceed p999");
    expect(result.by_command[0].count == 2U, "ADD command count should be tracked");
    expect(result.by_command[1].count == 1U, "MARKET command count should be tracked");
    expect(result.by_command[2].count == 1U, "CANCEL command count should be tracked");
    expect(result.by_fill_bucket[0].count == 3U, "zero-fill bucket should include resting adds/cancel");
    expect(result.by_fill_bucket[1].count == 1U, "one-fill bucket should include market match");
}

static void test_scenario_benchmark_reports_named_slices(void) {
    BenchmarkResult result;
    char error[256] = "";

    expect(benchmark_scenario("market_sweep_10", 8U, &result, error, sizeof(error)),
           "scenario benchmark should succeed");
    expect(strcmp(result.name, "market_sweep_10") == 0, "scenario name should be retained");
    expect(result.messages == 8U, "scenario should run requested message count");
    expect(result.by_command[1].count == 8U, "market scenario should report MARKET messages");
    expect(result.by_fill_bucket[3].count == 8U, "sweep scenario should report 6-20 fill bucket");
}

static void test_benchmark_reports_parse_errors(void) {
    BenchmarkResult result;
    char error[256] = "";

    write_text_file("ADD 1 B 100 10\n"
                    "NOPE 2 S 101 5\n");

    expect(!benchmark_file(benchmark_path, &result, error, sizeof(error)),
           "invalid benchmark input should fail");
    expect(strcmp(error, "line 2: unknown command") == 0, "parse error should include line number");
}

int main(void) {
    test_benchmark_reports_message_count_and_latency_stats();
    test_scenario_benchmark_reports_named_slices();
    test_benchmark_reports_parse_errors();
    remove(benchmark_path);
    return 0;
}
