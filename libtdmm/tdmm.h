#ifndef TDMM_H
#define TDMM_H

#include <stddef.h>

typedef enum
{
  FIRST_FIT,
  BEST_FIT,
  WORST_FIT,
  BUDDY,
  MIXED
} alloc_strat_e;

size_t tdmm_bytes_free_payload(void);

void t_init(alloc_strat_e strat);

void *t_malloc(size_t bytes);

void t_free(void *memory);

size_t tdmm_bytes_from_os(void);

size_t tdmm_bytes_in_use(void);

size_t tdmm_peak_bytes_in_use(void);

#endif