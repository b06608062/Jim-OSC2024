#ifndef THREAD_H
#define THREAD_H

#include "dlist.h"
#include "types.h"

#define PID_MAX 1024
#define USTACK_SIZE 0x10000
#define KSTACK_SIZE 0x10000
#define SIGNAL_MAX 64

typedef struct thread_context {
  uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
  uint64_t fp, lr, sp;
  void *pgd;
} thread_context_t;

typedef struct signal_context {
  uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
  uint64_t fp, lr, sp;
  void *pgd;
} signal_context_t;

typedef enum {
  THREAD_IDLE,
  THREAD_READY,
  THREAD_RUNNING,
  THREAD_ZOMBIE
} thread_state_t;

typedef struct thread {
  double_linked_node_t node;
  thread_context_t context;
  thread_state_t state;
  int pid;
  // char *user_space;
  uint32_t user_data_size;
  // char *user_stack;
  char *kernel_stack;
  signal_context_t signal_context;
  void (*signal_handler[SIGNAL_MAX + 1])();
  int signal_count[SIGNAL_MAX + 1];
  void (*current_signal_handler)();
  int signal_running;
  double_linked_node_t vma_list;
} thread_t;

extern void switch_to(void *current_context, void *next_context);
extern void store_context(void *current_context);
extern void load_context(void *current_context);
extern thread_context_t *get_current();

void thread_init();
thread_t *thread_create(void *entry_point, uint32_t size);
int exec_thread(char *data, uint32_t size);
void schedule();
void kill_zombies();
void thread_exit();
void thread_test();
void idle();
void schedule_timer(char *arg);

#endif // THREAD_H