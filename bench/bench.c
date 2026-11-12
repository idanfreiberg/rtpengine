#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"
#include "bencode.h"
#include "payloads.h"

#ifndef PATCHED
#define PATCHED 0
#endif
_Static_assert(sizeof(int) == 4 && sizeof(long) == 8, "benchmark requires LP64");

static const char *valid_i[] = {"0", "1", "8", "96", "101", "16000", "48000", "65535", "35000", "36001", "2", "20", "-1", "100", "123456", "2147483647"};
static const char *valid_u[] = {"0", "1", "8", "96", "101", "16000", "48000", "65535", "35000", "36001", "2", "20", "42", "100", "123456", "4294967295"};
static const char *valid_l[] = {"0", "1", "10", "20", "100", "1000", "30000", "60000", "3600", "86400", "-1", "300000", "2147483647", "4294967295", "1234567890", "9223372036854775807"};
static const char *valid_ul[] = {"0", "1", "3", "4", "7", "10", "32", "64", "128", "256", "512", "1024", "2048", "8192", "16384", "65535"};
static const char *valid_ll[] = {"0", "1", "8", "96", "101", "16000", "48000", "65535", "35000", "36001", "-1", "20", "100", "1234567890", "1700000000000", "9223372036854775807"};
static const char *valid_ull[] = {"0", "1", "8", "96", "101", "65535", "123456", "12345678", "1234567890", "4294967295", "1700000000000", "123456789012345", "1234567890123456", "12345678901234567", "123456789012345678", "9223372036854775807"};
static const char *invalid_i[] = {"4295003297", "2147483648", "-2147483649", "4294967296", "-4294931295", "9999999999", "99999999999", "-9999999999", "42x", "", " ", "+", "abc", "123x", "1.5", "42 "};
static const char *invalid_u[] = {"4295003297", "4294967296", "-1", " -1", "-0", "9999999999", "99999999999", "-9999999999", "42x", "", " ", "+", "abc", "123x", "1.5", "42 "};

#define BENCH(name, fn) \
static __attribute__((noinline)) uint64_t name(str *inputs, uint64_t iterations) { \
    uint64_t sum = 0; \
    for (uint64_t i = 0; i < iterations; i++) \
        sum += (uint64_t) fn(&inputs[i & 15], 77); \
    return sum; \
}
BENCH(run_i, str_to_i)
BENCH(run_u, str_to_u)
BENCH(run_l, str_to_l)
BENCH(run_ul, str_to_ul)
BENCH(run_ll, str_to_ll)
BENCH(run_ull, str_to_ull)

static __attribute__((noinline)) uint64_t run_bencode(str *unused, uint64_t iterations) {
    (void) unused;
    uint64_t sum = 0;
    for (uint64_t i = 0; i < iterations; i++) {
        bencode_buffer_t buffer;
        if (bencode_buffer_init(&buffer)) abort();
        str message = STR(payloads[i & 3]);
        bencode_item_t *item = bencode_decode_expect_str(&buffer, &message, BENCODE_DICTIONARY);
        if (!item) abort();
        bencode_item_t *command = bencode_dictionary_get_len(item, "command", 7);
        if (!command) abort();
        sum += item->str_len + command->iov[1].iov_len;
        bencode_buffer_free(&buffer);
    }
    return sum;
}

struct workload {
    const char *name;
    const char **values;
    uint64_t (*run)(str *, uint64_t);
};
static struct workload workloads[] = {
    {"valid_i", valid_i, run_i}, {"valid_u", valid_u, run_u},
    {"valid_l", valid_l, run_l}, {"valid_ul", valid_ul, run_ul},
    {"valid_ll", valid_ll, run_ll}, {"valid_ull", valid_ull, run_ull},
    {"ng_bencode", NULL, run_bencode},
    {"invalid_i", invalid_i, run_i}, {"invalid_u", invalid_u, run_u},
};

static void prepare(struct workload *w, str *inputs) {
    if (!w->values) return;
    for (int i = 0; i < 16; i++) inputs[i] = STR(w->values[i]);
}
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) abort();
    return (uint64_t) t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static int verify(void) {
    for (unsigned i = 0; i < 6; i++) {
        str inputs[16];
        prepare(&workloads[i], inputs);
        uint64_t expected = 0;
        for (int j = 0; j < 16; j++) {
            char *end;
            uint64_t value = i == 1 || i == 3 || i == 5
                ? strtoull(workloads[i].values[j], &end, 10)
                : (uint64_t) strtoll(workloads[i].values[j], &end, 10);
            if (*end) return 1;
            expected += value;
        }
        if (workloads[i].run(inputs, 16) != expected) { fprintf(stderr, "helper mismatch: %s\n", workloads[i].name); return 1; }
    }
    if (str_to_i(&STR_CONST("4295003297"), 77) != (PATCHED ? 77 : 36001)) return 1;
    if (str_to_u(&STR_CONST("-1"), 77) != (PATCHED ? 77U : UINT_MAX)) return 1;
    if (PATCHED) {
        for (unsigned i = 7; i < 9; i++) {
            str inputs[16];
            prepare(&workloads[i], inputs);
            if (workloads[i].run(inputs, 16) != 16 * 77) return 1;
        }
    }
    if (run_bencode(NULL, 4) == 0) return 1;
    bencode_buffer_t buffer;
    if (bencode_buffer_init(&buffer)) return 1;
    str broken = STR_CONST("d7:command99:offer");
    if (bencode_decode_expect_str(&buffer, &broken, BENCODE_DICTIONARY)) return 1;
    bencode_buffer_free(&buffer);
    puts("PASS: valid helper results, overflow control, valid NG decode, malformed NG rejection");
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--verify")) return verify();
    if (argc != 3) return 2;
    char *end;
    uint64_t iterations = strtoull(argv[2], &end, 10);
    if (*end || !iterations) return 2;
    for (unsigned i = 0; i < sizeof(workloads) / sizeof(*workloads); i++) {
        if (strcmp(argv[1], workloads[i].name)) continue;
        str inputs[16];
        prepare(&workloads[i], inputs);
        uint64_t start = now_ns();
        uint64_t checksum = workloads[i].run(inputs, iterations);
        uint64_t elapsed = now_ns() - start;
        printf("{\"case\":\"%s\",\"patched\":%d,\"iterations\":%llu,\"elapsed_ns\":%llu,\"checksum\":%llu,\"ns_per_op\":%.9f}\n",
            argv[1], PATCHED, (unsigned long long) iterations, (unsigned long long) elapsed,
            (unsigned long long) checksum, (double) elapsed / iterations);
        return 0;
    }
    return 2;
}
