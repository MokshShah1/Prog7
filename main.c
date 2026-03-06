#include "tdmm.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static inline uint64_t now_nanos(void)
{
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000000000ull + (uint64_t)timestamp.tv_nsec;
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

static const char *strategy_label(alloc_strat_e strategy)
{
    if (strategy == FIRST_FIT)
        return "first_fit";
    if (strategy == BEST_FIT)
        return "best_fit";
    if (strategy == WORST_FIT)
        return "worst_fit";
    if (strategy == BUDDY)
        return "buddy";
    if (strategy == MIXED)
        return "mixed";
    return "unknown";
}

static void run_benchmark(alloc_strat_e strategy)
{
    enum
    {
        request_count = 5000
    };
    void *allocated_ptrs[request_count];
    uint32_t requested_sizes[request_count];
    for (int i = 0; i < request_count; i++)
        allocated_ptrs[i] = NULL, requested_sizes[i] = 0;

    t_init(strategy);

    uint32_t rng_state = 0xC0FFEEu;
    uint64_t start_time = now_nanos();

    for (int i = 0; i < request_count; i++)
    {
        uint32_t req_bytes = (next_random_u32(&rng_state) % 4096) + 1;
        requested_sizes[i] = req_bytes;
        allocated_ptrs[i] = t_malloc(req_bytes);
    }
    for (int i = 0; i < request_count; i += 2)
        t_free(allocated_ptrs[i]);
    for (int i = 0; i < request_count; i += 2)
        allocated_ptrs[i] = t_malloc((next_random_u32(&rng_state) % 8192) + 1);
    for (int i = 0; i < request_count; i++)
        t_free(allocated_ptrs[i]);

    uint64_t end_time = now_nanos();

    printf("Strategy: %s\n", strategy_label(strategy));
    printf("  Total time: %.3f s\n", (double)(end_time - start_time) / 1e9);
    printf("  Bytes from OS: %zu\n", tdmm_bytes_from_os());
    printf("  Peak bytes in use: %zu\n", tdmm_peak_bytes_in_use());
    if (tdmm_bytes_from_os())
    {
        printf("  Peak utilization: %.4f\n\n",
               (double)tdmm_peak_bytes_in_use() / (double)tdmm_bytes_from_os());
    }
}

int main(void)
{
    run_benchmark(FIRST_FIT);
    run_benchmark(BEST_FIT);
    run_benchmark(WORST_FIT);
    run_benchmark(BUDDY);
    run_benchmark(MIXED);
    return 0;
}