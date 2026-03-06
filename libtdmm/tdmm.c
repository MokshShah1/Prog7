#include "tdmm.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct memory_block
{
	uint32_t magic_number;
	uint32_t payload_size;
	uint8_t is_free;
	uint8_t padding[3];
	struct memory_block *previous;
	struct memory_block *next;
} memory_block_t;

static const size_t alignment_bytes = 4u;
static const uint32_t header_magic = 0x54444D4Du;

static alloc_strat_e current_strategy = FIRST_FIT;
static memory_block_t *first_block = NULL;
static size_t os_page_size = 4096;

static size_t total_bytes_from_os = 0;
static size_t current_bytes_in_use = 0;
static size_t peak_bytes_in_use = 0;

static int mixed_counter = 0;

static size_t round_up_to_alignment(size_t value, size_t multiple)
{
	if (multiple == 0)
	{
		return value;
	}
	size_t remainder = value % multiple;
	if (remainder != 0)
	{
		return value + (multiple - remainder);
	}
	return value;
}

static size_t header_size_bytes(void)
{
	return round_up_to_alignment(sizeof(memory_block_t), alignment_bytes);
}

static void fatal_error(const char *message)
{
	int errnum = errno;
	fprintf(stderr, "tdmm error: %s: %s\n", message, strerror(errnum));
	_exit(1);
}

static void insert_block_sorted(memory_block_t *block)
{
	memory_block_t *cursor = first_block;
	if (!cursor)
	{
		first_block = block;
		block->previous = NULL;
		block->next = NULL;
		return;
	}

	while (cursor && (uintptr_t)cursor < (uintptr_t)block)
	{
		cursor = cursor->next;
	}

	if (!cursor)
	{
		memory_block_t *tail = first_block;
		while (tail->next)
		{
			tail = tail->next;
		}
		tail->next = block;
		block->previous = tail;
		block->next = NULL;
		return;
	}

	block->next = cursor;
	block->previous = cursor->previous;
	if (cursor->previous)
	{
		cursor->previous->next = block;
	}
	else
	{
		first_block = block;
	}
	cursor->previous = block;
}

static void remove_block(memory_block_t *block)
{
	if (!block)
	{
		return;
	}
	if (block->previous)
	{
		block->previous->next = block->next;
	}
	else
	{
		first_block = block->next;
	}
	if (block->next)
	{
		block->next->previous = block->previous;
	}
	block->previous = NULL;
	block->next = NULL;
}

static bool blocks_touch(memory_block_t *left, memory_block_t *right)
{
	if (!left || !right)
	{
		return false;
	}
	char *left_end = (char *)left + header_size_bytes() + left->payload_size;
	return left_end == (char *)right;
}

static void merge_adjacent_free_blocks(memory_block_t *block)
{
	if (!block)
	{
		return;
	}

	memory_block_t *next_block = block->next;
	if (next_block && block->is_free && next_block->is_free && blocks_touch(block, next_block))
	{
		block->payload_size += header_size_bytes() + next_block->payload_size;
		remove_block(next_block);
		merge_adjacent_free_blocks(block);
		return;
	}

	memory_block_t *prev_block = block->previous;
	if (prev_block && prev_block->is_free && block->is_free && blocks_touch(prev_block, block))
	{
		prev_block->payload_size += header_size_bytes() + block->payload_size;
		remove_block(block);
		merge_adjacent_free_blocks(prev_block);
	}
}

static memory_block_t *request_os_memory(size_t min_payload_size)
{
	size_t total_needed = header_size_bytes() + min_payload_size;
	size_t map_size = round_up_to_alignment(total_needed, os_page_size);

	void *region = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED)
	{
		fatal_error("mmap failed");
	}
	total_bytes_from_os += map_size;

	memory_block_t *block = (memory_block_t *)region;
	block->magic_number = header_magic;
	block->payload_size = (uint32_t)(map_size - header_size_bytes());
	block->is_free = 1;
	block->previous = NULL;
	block->next = NULL;

	insert_block_sorted(block);
	return block;
}

static memory_block_t *choose_block(size_t needed_size)
{
	memory_block_t *cursor = first_block;
	memory_block_t *chosen_block = NULL;
	alloc_strat_e strategy = current_strategy;

	if (strategy == MIXED)
	{
		int mode = mixed_counter % 3;
		if (mode == 0)
		{
			strategy = FIRST_FIT;
		}
		else if (mode == 1)
		{
			strategy = BEST_FIT;
		}
		else
		{
			strategy = WORST_FIT;
		}
		mixed_counter++;
	}

	while (cursor)
	{
		if (cursor->is_free && cursor->payload_size >= needed_size)
		{
			if (strategy == FIRST_FIT)
			{
				return cursor;
			}
			if (!chosen_block)
			{
				chosen_block = cursor;
			}
			else if (strategy == BEST_FIT && cursor->payload_size < chosen_block->payload_size)
			{
				chosen_block = cursor;
			}
			else if (strategy == WORST_FIT && cursor->payload_size > chosen_block->payload_size)
			{
				chosen_block = cursor;
			}
		}
		cursor = cursor->next;
	}
	return chosen_block;
}

static void split_block_if_needed(memory_block_t *block, size_t needed_size)
{
	size_t header_size = header_size_bytes();
	if (block->payload_size < needed_size)
	{
		return;
	}

	size_t remaining_size = block->payload_size - needed_size;
	if (remaining_size < header_size + alignment_bytes)
	{
		return;
	}

	char *payload_start = (char *)block + header_size;
	memory_block_t *new_block = (memory_block_t *)(payload_start + needed_size);

	new_block->magic_number = header_magic;
	new_block->payload_size = (uint32_t)(remaining_size - header_size);
	new_block->is_free = 1;
	new_block->previous = block;
	new_block->next = block->next;
	if (block->next)
	{
		block->next->previous = new_block;
	}
	block->next = new_block;
	block->payload_size = (uint32_t)needed_size;
}

void t_init(alloc_strat_e strategy)
{
	current_strategy = strategy;
	first_block = NULL;
	total_bytes_from_os = 0;
	current_bytes_in_use = 0;
	peak_bytes_in_use = 0;
	mixed_counter = 0;

	long page_size = sysconf(_SC_PAGESIZE);
	os_page_size = (page_size > 0) ? (size_t)page_size : 4096;
}

void *t_malloc(size_t size)
{
	if (size == 0)
	{
		return NULL;
	}

	size_t needed_size = round_up_to_alignment(size, alignment_bytes);
	memory_block_t *block = choose_block(needed_size);

	if (!block)
	{
		request_os_memory(needed_size);
		block = choose_block(needed_size);
		if (!block)
		{
			return NULL;
		}
	}

	split_block_if_needed(block, needed_size);
	block->is_free = 0;
	current_bytes_in_use += block->payload_size;
	if (current_bytes_in_use > peak_bytes_in_use)
	{
		peak_bytes_in_use = current_bytes_in_use;
	}

	return (char *)block + header_size_bytes();
}

void t_free(void *ptr)
{
	if (!ptr)
	{
		return;
	}

	memory_block_t *block = (memory_block_t *)((char *)ptr - header_size_bytes());
	if (block->magic_number != header_magic)
	{
		fprintf(stderr, "tdmm warning: invalid pointer passed to t_free(%p)\n", ptr);
		return;
	}
	if (block->is_free)
	{
		return;
	}

	block->is_free = 1;
	if (current_bytes_in_use >= block->payload_size)
	{
		current_bytes_in_use -= block->payload_size;
	}
	else
	{
		current_bytes_in_use = 0;
	}

	merge_adjacent_free_blocks(block);
}

size_t tdmm_bytes_free_payload(void)
{
	size_t total_free = 0;
	memory_block_t *cursor = first_block;
	while (cursor)
	{
		if (cursor->is_free)
		{
			total_free += cursor->payload_size;
		}
		cursor = cursor->next;
	}
	return total_free;
}

size_t tdmm_bytes_from_os(void)
{
	return total_bytes_from_os;
}

size_t tdmm_bytes_in_use(void)
{
	return current_bytes_in_use;
}

size_t tdmm_peak_bytes_in_use(void)
{
	return peak_bytes_in_use;
}