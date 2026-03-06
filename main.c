#include "tdmm.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static inline uint64_t now_nanos(void)
{
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000000000ull + timestamp.tv_nsec;
}

static uint32_t next_random_u32(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static const char *strategy_label(alloc_strat_e strat)
{
    switch (strat)
    {
    case FIRST_FIT:
        return "first_fit";
    case BEST_FIT:
        return "best_fit";
    case WORST_FIT:
        return "worst_fit";
    case MIXED:
        return "mixed";
    case BUDDY:
        return "buddy";
    default:
        return "unknown";
    }
}

static void run_benchmark(alloc_strat_e strat)
{
    enum
    {
        request_count = 5000
    };
    void *ptrs[request_count];
    uint32_t sizes[request_count];
    for (int i = 0; i < request_count; i++)
        ptrs[i] = NULL;

    t_init(strat);
    uint32_t rng = 0xC0FFEEu;
    uint64_t start = now_nanos();

    for (int i = 0; i < request_count; i++)
    {
        sizes[i] = (next_random_u32(&rng) % 4096) + 1;
        ptrs[i] = t_malloc(sizes[i]);
    }
    for (int i = 0; i < request_count; i += 2)
        t_free(ptrs[i]);

    uint64_t end = now_nanos();
    double seconds = (double)(end - start) / 1e9;

    printf("Strategy: %s\n", strategy_label(strat));
    printf("  Time: %.3f s\n", seconds);
    printf("  Bytes from OS: %zu\n", tdmm_bytes_from_os());
    printf("  Peak in use: %zu\n\n", tdmm_peak_bytes_in_use());
}

int main(void)
{
    run_benchmark(FIRST_FIT);
    run_benchmark(BEST_FIT);
    run_benchmark(WORST_FIT);
    run_benchmark(MIXED);
    return 0;
}