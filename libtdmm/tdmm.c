#include "tdmm.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct heap_block
{
	uint32_t magic;
	uint32_t payload_bytes;
	uint8_t free_flag;
	uint8_t padding[3];
	struct heap_block *prev;
	struct heap_block *next;
} heap_block_t;

static const size_t alignment_bytes = 4u;
static const uint32_t header_magic = 0x54444D4Du;

static alloc_strat_e active_strategy = FIRST_FIT;
static heap_block_t *block_list_head = NULL;
static size_t os_page_bytes = 4096;

static size_t bytes_from_os = 0;
static size_t bytes_in_use = 0;
static size_t peak_bytes_in_use = 0;

// For MIXED strategy
static int mixed_counter = 0;

// Helper to round up
static size_t round_up(size_t value, size_t multiple)
{
	if (multiple == 0)
		return value;
	size_t remainder = value % multiple;
	if (remainder != 0)
		return value + (multiple - remainder);
	return value;
}

// Header size aligned
static size_t header_bytes(void)
{
	return round_up(sizeof(heap_block_t), alignment_bytes);
}

// Fatal error
static void fatal_with_errno(const char *message)
{
	int errnum = errno;
	fprintf(stderr, "tdmm error: %s: %s\n", message, strerror(errnum));
	_exit(1);
}

// Linked list helpers
static void insert_block_sorted(heap_block_t *block)
{
	heap_block_t *cursor = block_list_head;
	if (!cursor)
	{
		block_list_head = block;
		block->prev = block->next = NULL;
		return;
	}
	while (cursor && (uintptr_t)cursor < (uintptr_t)block)
		cursor = cursor->next;
	if (!cursor)
	{
		heap_block_t *tail = block_list_head;
		while (tail->next)
			tail = tail->next;
		tail->next = block;
		block->prev = tail;
		block->next = NULL;
		return;
	}
	block->next = cursor;
	block->prev = cursor->prev;
	if (cursor->prev)
		cursor->prev->next = block;
	else
		block_list_head = block;
	cursor->prev = block;
}

static void detach_block(heap_block_t *block)
{
	if (!block)
		return;
	if (block->prev)
		block->prev->next = block->next;
	else
		block_list_head = block->next;
	if (block->next)
		block->next->prev = block->prev;
	block->prev = block->next = NULL;
}

static bool touches_right_after(heap_block_t *left, heap_block_t *right)
{
	if (!left || !right)
		return false;
	char *left_end = (char *)left + header_bytes() + left->payload_bytes;
	return left_end == (char *)right;
}

// Merge contiguous free blocks
static void merge_neighbors(heap_block_t *block)
{
	if (!block)
		return;
	heap_block_t *next_block = block->next;
	if (next_block && block->free_flag && next_block->free_flag &&
		touches_right_after(block, next_block))
	{
		block->payload_bytes += header_bytes() + next_block->payload_bytes;
		detach_block(next_block);
		merge_neighbors(block);
		return;
	}
	heap_block_t *prev_block = block->prev;
	if (prev_block && prev_block->free_flag && block->free_flag &&
		touches_right_after(prev_block, block))
	{
		prev_block->payload_bytes += header_bytes() + block->payload_bytes;
		detach_block(block);
		merge_neighbors(prev_block);
	}
}

// Request memory from OS
static heap_block_t *request_more_memory(size_t min_payload_bytes)
{
	size_t total_needed = header_bytes() + min_payload_bytes;
	size_t map_bytes = round_up(total_needed, os_page_bytes);
	void *region_start = mmap(NULL, map_bytes, PROT_READ | PROT_WRITE,
							  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region_start == MAP_FAILED)
		fatal_with_errno("mmap failed");
	bytes_from_os += map_bytes;

	heap_block_t *block = (heap_block_t *)region_start;
	block->magic = header_magic;
	block->payload_bytes = (uint32_t)(map_bytes - header_bytes());
	block->free_flag = 1;
	block->prev = block->next = NULL;
	insert_block_sorted(block);
	return block;
}

// Pick block according to strategy
static heap_block_t *pick_block(size_t needed_bytes)
{
	heap_block_t *cursor = block_list_head;
	heap_block_t *chosen = NULL;
	alloc_strat_e strat = active_strategy;

	// MIXED strategy
	if (strat == MIXED)
	{
		int mod = mixed_counter % 3;
		if (mod == 0)
			strat = FIRST_FIT;
		else if (mod == 1)
			strat = BEST_FIT;
		else
			strat = WORST_FIT;
		mixed_counter++;
	}

	while (cursor)
	{
		if (cursor->free_flag && cursor->payload_bytes >= needed_bytes)
		{
			if (strat == FIRST_FIT)
				return cursor;
			if (!chosen)
				chosen = cursor;
			else if (strat == BEST_FIT && cursor->payload_bytes < chosen->payload_bytes)
				chosen = cursor;
			else if (strat == WORST_FIT && cursor->payload_bytes > chosen->payload_bytes)
				chosen = cursor;
		}
		cursor = cursor->next;
	}
	return chosen;
}

// Split block if remaining space is useful
static void split_if_useful(heap_block_t *block, size_t needed_bytes)
{
	size_t header_size = header_bytes();
	if (block->payload_bytes < needed_bytes)
		return;

	size_t remaining = block->payload_bytes - needed_bytes;
	if (remaining < header_size + alignment_bytes)
		return;

	char *payload_start = (char *)block + header_size;
	heap_block_t *new_block = (heap_block_t *)(payload_start + needed_bytes);

	new_block->magic = header_magic;
	new_block->payload_bytes = (uint32_t)(remaining - header_size);
	new_block->free_flag = 1;
	new_block->prev = block;
	new_block->next = block->next;
	if (block->next)
		block->next->prev = new_block;
	block->next = new_block;
	block->payload_bytes = (uint32_t)needed_bytes;
}

// PUBLIC FUNCTIONS
void t_init(alloc_strat_e strat)
{
	active_strategy = strat;
	block_list_head = NULL;
	bytes_from_os = bytes_in_use = peak_bytes_in_use = 0;
	mixed_counter = 0;

	long page_size = sysconf(_SC_PAGESIZE);
	os_page_bytes = (page_size > 0) ? (size_t)page_size : 4096;
}

void *t_malloc(size_t size)
{
	if (size == 0)
		return NULL;
	size_t needed_bytes = round_up(size, alignment_bytes);
	heap_block_t *block = pick_block(needed_bytes);

	if (!block)
	{
		request_more_memory(needed_bytes);
		block = pick_block(needed_bytes);
		if (!block)
			return NULL;
	}

	split_if_useful(block, needed_bytes);
	block->free_flag = 0;
	bytes_in_use += block->payload_bytes;
	if (bytes_in_use > peak_bytes_in_use)
		peak_bytes_in_use = bytes_in_use;

	return (char *)block + header_bytes();
}

void t_free(void *ptr)
{
	if (!ptr)
		return;
	heap_block_t *block = (heap_block_t *)((char *)ptr - header_bytes());
	if (block->magic != header_magic)
	{
		fprintf(stderr, "tdmm warning: invalid pointer passed to t_free(%p)\n", ptr);
		return;
	}
	if (block->free_flag)
		return;

	block->free_flag = 1;
	bytes_in_use = (bytes_in_use >= block->payload_bytes) ? (bytes_in_use - block->payload_bytes) : 0;
	merge_neighbors(block);
}

size_t tdmm_bytes_free_payload(void)
{
	size_t total = 0;
	heap_block_t *cursor = block_list_head;
	while (cursor)
	{
		if (cursor->free_flag)
			total += cursor->payload_bytes;
		cursor = cursor->next;
	}
	return total;
}

size_t tdmm_bytes_from_os(void) { return bytes_from_os; }
size_t tdmm_bytes_in_use(void) { return bytes_in_use; }
size_t tdmm_peak_bytes_in_use(void) { return peak_bytes_in_use; }