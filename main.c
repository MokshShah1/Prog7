#include "tdmm.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static uint64_t get_current_time_nanos(void)
{
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000000000ull + timestamp.tv_nsec;
}

static uint32_t generate_random_u32(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static const char *get_strategy_name(alloc_strat_e strategy)
{
    if (strategy == FIRST_FIT)
    {
        return "first_fit";
    }
    if (strategy == BEST_FIT)
    {
        return "best_fit";
    }
    if (strategy == WORST_FIT)
    {
        return "worst_fit";
    }
    if (strategy == MIXED)
    {
        return "mixed";
    }
    if (strategy == BUDDY)
    {
        return "buddy";
    }
    return "unknown";
}

static void run_memory_benchmark(alloc_strat_e strategy)
{
    const int total_requests = 5000;
    void *allocated_ptrs[total_requests];
    uint32_t allocation_sizes[total_requests];

    for (int i = 0; i < total_requests; i++)
    {
        allocated_ptrs[i] = NULL;
    }

    t_init(strategy);
    uint32_t rng_state = 0xC0FFEEu;
    uint64_t start_time = get_current_time_nanos();

    for (int i = 0; i < total_requests; i++)
    {
        allocation_sizes[i] = (generate_random_u32(&rng_state) % 4096) + 1;
        allocated_ptrs[i] = t_malloc(allocation_sizes[i]);
    }

    for (int i = 0; i < total_requests; i += 2)
    {
        t_free(allocated_ptrs[i]);
    }

    uint64_t end_time = get_current_time_nanos();
    double elapsed_seconds = (double)(end_time - start_time) / 1e9;

    printf("Strategy: %s\n", get_strategy_name(strategy));
    printf("  Time: %.3f s\n", elapsed_seconds);
    printf("  Bytes from OS: %zu\n", tdmm_bytes_from_os());
    printf("  Peak in use: %zu\n\n", tdmm_peak_bytes_in_use());
}

int main(void)
{
    run_memory_benchmark(FIRST_FIT);
    run_memory_benchmark(BEST_FIT);
    run_memory_benchmark(WORST_FIT);
    run_memory_benchmark(MIXED);
    return 0;
}