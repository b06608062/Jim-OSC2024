#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/dlist.h"
#include "include/exception.h"
#include "include/fat32.h"
#include "include/heap.h"
#include "include/shell.h"
#include "include/thread.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/vfs.h"

// main.c
int init_done = 0;

// shell.c
char cmd[MAX_CMD_LEN];
int preempt = 0;
char *user_space = NULL;
char *user_stack = NULL;
char *kernel_stack = NULL;
int back_to_shell = 0;
kernel_context_t kernel_context;

// cpio.c
cpio_newc_header_t *cpio_header = NULL;
char *cpio_end = NULL;

// dtb.c
char *dtb_ptr = NULL;

// heap.c
char *heap_ptr = NULL;
startup_memory_block_t *startup_memory_block_table_start = NULL;
startup_memory_block_t *startup_memory_block_table_end = NULL;

// uart.c
circular_buffer_t tx_buffer = {.head = 0, .tail = 0},
                  rx_buffer = {.head = 0, .tail = 0};

// timer.c
double_linked_node_t *timer_list_head = NULL;

// exception.c
double_linked_node_t *irq_task_list_head = NULL;
int current_irq_task_priority = 999;
uint32_t lock_count = 0;

// buddy_system.c
buddy_system_node_t buddy_system[MAX_LEVEL + 1];
frame_array_node_t frame_array[TOTAL_MEMORY / PAGE_SIZE];

// allocator.c
double_linked_node_t pools[SMALL_SIZES_COUNT];
const uint32_t SMALL_SIZES[SMALL_SIZES_COUNT] = {32, 64, 128, 256, 512, 1024};

// thread.c
thread_t *current_thread = NULL;
double_linked_node_t *run_queue = NULL;
thread_t thread_table[PID_MAX + 1];

// vfs.c
mount_t *rootfs = NULL;
filesystem_t reg_fs[MAX_FS_REG];
file_operations_t reg_dev[MAX_DEV_REG];

// dev_framebuffer.c
unsigned int width, height, pitch, isrgb;
unsigned char *lfb;

// syscall.c
uint32_t thread_count = 0;

// sdhost.c
int is_hcs;

// fat32.c
fat32_metadata_t *fat32_md = NULL;
double_linked_node_t *fat32_cache_list_head = NULL;