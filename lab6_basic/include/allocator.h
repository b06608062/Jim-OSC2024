#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "dlist.h"
#include "types.h"

#define SMALL_SIZES_COUNT 6
#define SMALLEST_SIZE 32

void memory_pool_init();
int memory_pool_find_pool_index(uint32_t size);
void *memory_pool_allocator(uint32_t size, int show_info);
void memory_pool_free(void *address, int show_info);

#endif /* ALLOCATOR_H */