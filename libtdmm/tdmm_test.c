#include "tdmm.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static uint64_t get_time_nanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return nanos;
}

static uint32_t random_u32(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static const char *get_strategy_label(alloc_strat_e strategy)
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

static int is_aligned_4(const void *pointer)
{
    uintptr_t addr = (uintptr_t)pointer;
    if ((addr % 4u) == 0u)
    {
        return 1;
    }
    return 0;
}

static int run_unit_tests(void)
{
    int success = 1;
    void *ptr_a;
    void *ptr_b;
    void *ptr_c;

    t_init(FIRST_FIT);

    ptr_a = t_malloc(1);
    if (ptr_a == NULL)
    {
        printf("[unit] FAIL: t_malloc(1) returned NULL\n");
        success = 0;
    }
    else
    {
        if (!is_aligned_4(ptr_a))
        {
            printf("[unit] FAIL: pointer not 4-byte aligned\n");
            success = 0;
        }
    }

    ptr_b = t_malloc(7);
    if (ptr_b == NULL)
    {
        printf("[unit] FAIL: t_malloc(7) returned NULL\n");
        success = 0;
    }
    else
    {
        if (!is_aligned_4(ptr_b))
        {
            printf("[unit] FAIL: pointer not 4-byte aligned (ptr_b)\n");
            success = 0;
        }
    }

    t_free(NULL);

    t_free(ptr_a);
    t_free(ptr_a);

    ptr_c = (void *)((uintptr_t)ptr_b + 4u);
    t_free(ptr_c);

    t_free(ptr_b);

    if (tdmm_bytes_in_use() != 0)
    {
        printf("[unit] FAIL: bytes_in_use not 0 after freeing\n");
        success = 0;
    }

    if (success)
    {
        printf("[unit] PASS\n");
        return 0;
    }

    return 1;
}

static void write_utilization_csv(alloc_strat_e strategy)
{
    const int request_count = 5000;

    void **allocated_ptrs = malloc(sizeof(void *) * request_count);
    uint32_t *allocation_sizes = malloc(sizeof(uint32_t) * request_count);
    if (!allocated_ptrs || !allocation_sizes)
    {
        printf("[util] malloc failed\n");
        return;
    }

    uint32_t rng_state;
    FILE *csv_file;
    char file_path[128];
    int i;

    for (i = 0; i < request_count; i++)
    {
        allocated_ptrs[i] = NULL;
        allocation_sizes[i] = 0;
    }

    t_init(strategy);

    snprintf(file_path, sizeof(file_path), "util_%s.csv", get_strategy_label(strategy));
    csv_file = fopen(file_path, "w");
    if (csv_file == NULL)
    {
        printf("[util] could not open %s\n", file_path);
        free(allocated_ptrs);
        free(allocation_sizes);
        return;
    }

    fprintf(csv_file, "event,action,req_bytes,aligned_in_use,bytes_from_os,utilization,free_payload,overhead_est\n");

    rng_state = 0xC0FFEEu;

    for (i = 0; i < request_count; i++)
    {
        uint32_t random_value;
        uint32_t requested_size;
        size_t os_bytes;
        size_t in_use_bytes;
        size_t free_bytes;
        size_t overhead_bytes;
        double utilization_ratio;

        random_value = random_u32(&rng_state);
        requested_size = (random_value % 4096u) + 1u;

        allocation_sizes[i] = requested_size;
        allocated_ptrs[i] = t_malloc((size_t)requested_size);

        os_bytes = tdmm_bytes_from_os();
        in_use_bytes = tdmm_bytes_in_use();
        free_bytes = tdmm_bytes_free_payload();

        overhead_bytes = 0;
        if (os_bytes >= in_use_bytes + free_bytes)
        {
            overhead_bytes = os_bytes - in_use_bytes - free_bytes;
        }

        utilization_ratio = 0.0;
        if (os_bytes != 0)
        {
            utilization_ratio = (double)in_use_bytes / (double)os_bytes;
        }

        fprintf(csv_file, "%d,alloc,%" PRIu32 ",%zu,%zu,%.6f,%zu,%zu\n",
                i, requested_size, in_use_bytes, os_bytes, utilization_ratio, free_bytes, overhead_bytes);
    }

    for (i = 0; i < request_count; i += 2)
    {
        size_t os_bytes;
        size_t in_use_bytes;
        size_t free_bytes;
        size_t overhead_bytes;
        double utilization_ratio;

        t_free(allocated_ptrs[i]);
        allocated_ptrs[i] = NULL;

        os_bytes = tdmm_bytes_from_os();
        in_use_bytes = tdmm_bytes_in_use();
        free_bytes = tdmm_bytes_free_payload();

        overhead_bytes = 0;
        if (os_bytes >= in_use_bytes + free_bytes)
        {
            overhead_bytes = os_bytes - in_use_bytes - free_bytes;
        }

        utilization_ratio = 0.0;
        if (os_bytes != 0)
        {
            utilization_ratio = (double)in_use_bytes / (double)os_bytes;
        }

        fprintf(csv_file, "%d,free,%" PRIu32 ",%zu,%zu,%.6f,%zu,%zu\n",
                request_count + i, allocation_sizes[i], in_use_bytes, os_bytes, utilization_ratio, free_bytes, overhead_bytes);
    }

    fclose(csv_file);

    free(allocated_ptrs);
    free(allocation_sizes);

    printf("[util] wrote %s\n", file_path);
}

static void run_speed_benchmark(void)
{
    static const size_t test_sizes[] = {1, 4096, 8388608};
    static const char *size_labels[] = {"1B", "4KB", "8MB"};
    static const int num_sizes = 3;

    static const alloc_strat_e strategies[] = {
        FIRST_FIT, BEST_FIT, WORST_FIT, BUDDY, MIXED};
    static const int num_strategies = 5;

    const int iterations = 1000;

    FILE *csv = fopen("speed_results.csv", "w");
    if (csv == NULL)
    {
        printf("[speed] could not open speed_results.csv\n");
        return;
    }

    fprintf(csv, "strategy,size_label,size_bytes,avg_malloc_ns,avg_free_ns\n");

    int s, z, iter;
    for (s = 0; s < num_strategies; s++)
    {
        for (z = 0; z < num_sizes; z++)
        {
            t_init(strategies[s]);

            uint64_t total_malloc_ns = 0;
            uint64_t total_free_ns = 0;

            for (iter = 0; iter < iterations; iter++)
            {
                uint64_t t0 = get_time_nanos();
                void *p = t_malloc(test_sizes[z]);
                uint64_t t1 = get_time_nanos();

                t_free(p);
                uint64_t t2 = get_time_nanos();

                total_malloc_ns += (t1 - t0);
                total_free_ns += (t2 - t1);
            }

            double avg_malloc = (double)total_malloc_ns / (double)iterations;
            double avg_free = (double)total_free_ns / (double)iterations;

            fprintf(csv, "%s,%s,%zu,%.1f,%.1f\n",
                    get_strategy_label(strategies[s]),
                    size_labels[z],
                    test_sizes[z],
                    avg_malloc,
                    avg_free);

            printf("[speed] %s @ %s : malloc %.1f ns, free %.1f ns\n",
                   get_strategy_label(strategies[s]),
                   size_labels[z],
                   avg_malloc,
                   avg_free);
        }
    }

    fclose(csv);
    printf("[speed] wrote speed_results.csv\n");
}

int main(void)
{
    int test_result;

    test_result = run_unit_tests();

    write_utilization_csv(FIRST_FIT);
    write_utilization_csv(BEST_FIT);
    write_utilization_csv(WORST_FIT);
    write_utilization_csv(MIXED);
    write_utilization_csv(BUDDY);

    run_speed_benchmark();

    if (test_result != 0)
    {
        return 1;
    }

    return 0;
}