#ifndef BUDDY_SYSTEM_H
#define BUDDY_SYSTEM_H

#include "dlist.h"
#include "mmu.h"
#include "types.h"

#define BUDDY_MEMORY_BASE PHYS_TO_VIRT(0x0)
#define PAGE_SIZE 0x1000 // 4KB
#define MAX_LEVEL 14
#define TOTAL_MEMORY 0x3C000000
// #define MAX_LEVEL 10
// #define TOTAL_MEMORY 0x3B400000

typedef struct buddy_system_node {
  char *bitmap;
  double_linked_node_t head;
} buddy_system_node_t;

typedef struct frame_array_node {
  double_linked_node_t node;
  uint32_t size;
  uint32_t index;
  char *slot_bitmap;
  uint32_t slot_size;
  uint32_t slot_count;
} frame_array_node_t;

void buddy_system_init();
uint32_t buddy_system_find_level(uint32_t size);
uint32_t size_to_power_of_two(uint32_t size);
uint64_t buddy_system_allocator(uint32_t size);
void buddy_system_free(uint64_t address);
void buddy_system_print_bitmap();
void buddy_system_print_freelists(int show_bitmap);
void buddy_system_reserve_memory(uint64_t start, uint64_t end);
void buddy_system_reserve_memory_init();
void buddy_system_merge_bottom_up();
void buddy_system_freelists_init();

#endif /* BUDDY_SYSTEM_H */