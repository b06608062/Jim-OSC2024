#include "include/thread.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/dlist.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/signal.h"
#include "include/timer.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern thread_t *current_thread;
extern double_linked_node_t *run_queue;
extern thread_t thread_table[];

void thread_init() {
  lock();
  run_queue = simple_malloc(sizeof(double_linked_node_t), 0);
  double_linked_init(run_queue);

  for (int i = 0; i <= PID_MAX; ++i) {
    thread_table[i].state = THREAD_IDLE;
    thread_table[i].pid = i;
  }

  asm volatile(
      "msr tpidr_el1, %0" ::"r"(simple_malloc(sizeof(thread_context_t), 0)));
  current_thread = thread_create(idle);
  unlock();
}

thread_t *thread_create(void *entry_point) {
  lock();
  thread_t *new_thread;
  for (int i = 0; i <= PID_MAX; ++i) {
    if (thread_table[i].state == THREAD_IDLE) {
      new_thread = &thread_table[i];
      break;
    }
  }
  // thread
  new_thread->context.lr = (uint64_t)entry_point;
  new_thread->state = THREAD_READY;
  new_thread->user_stack = (char *)buddy_system_allocator(USTACK_SIZE);
  new_thread->kernel_stack = (char *)buddy_system_allocator(KSTACK_SIZE);
  new_thread->context.sp = (uint64_t)new_thread->user_stack + USTACK_SIZE;
  new_thread->context.fp = new_thread->context.sp;

  // signal
  new_thread->signal_running = 0;
  new_thread->current_signal_handler = signal_default_handler;
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    new_thread->signal_handler[i] = signal_default_handler;
    new_thread->signal_count[i] = 0;
  }

  double_linked_add_before((double_linked_node_t *)new_thread, run_queue);
  unlock();
  return new_thread;
}

void schedule() {
  lock();
  if (current_thread->state == THREAD_RUNNING) {
    current_thread->state = THREAD_READY;
  }
  do {
    current_thread = (thread_t *)((double_linked_node_t *)current_thread)->next;
  } while ((double_linked_node_t *)current_thread == run_queue ||
           current_thread->state != THREAD_READY);
  current_thread->state = THREAD_RUNNING;
  switch_to(get_current(), &current_thread->context);
  unlock();
}

void kill_zombies() {
  lock();
  double_linked_node_t *cur;
  double_linked_for_each(cur, run_queue) {
    if (((thread_t *)cur)->state == THREAD_ZOMBIE) {
      double_linked_remove(cur);
      thread_t *thread = (thread_t *)cur;
      thread->state = THREAD_IDLE;
      buddy_system_free((uint64_t)thread->user_stack);
      buddy_system_free((uint64_t)thread->kernel_stack);
    }
  }
  unlock();
}

void thread_exit() {
  lock();
  current_thread->state = THREAD_ZOMBIE;
  unlock();
  schedule();
}

void thread_test() {
  for (int i = 0; i < 10; ++i) {
    uart_sendline("Thread id: %d %d.\n", current_thread->pid, i);
    delay(1000000);
    schedule();
  }
  thread_exit();
}

void idle() {
  while (1) {
    kill_zombies();
    schedule();
  }
}

int exec_thread(char *data, uint32_t file_size) {
  thread_t *new_thread = thread_create(data);
  new_thread->user_space = (char *)buddy_system_allocator(file_size);
  new_thread->user_data_size = file_size;
  new_thread->context.lr = (uint64_t)new_thread->user_space;
  for (int i = 0; i < file_size; ++i) {
    new_thread->user_space[i] = data[i];
  }
  current_thread = new_thread;
  add_timer_task(create_timer_task(1, schedule_timer, NULL, 0));

  // eret to exception level 0
  asm("msr tpidr_el1, %0\n" // Hold the "kernel(el1)" thread structure
                            // information.
      "msr elr_el1, %1\n"   // elr_el1: Set the address to return address for
                            // el1 -> el0.
      "msr spsr_el1, xzr\n" // enable interrupt in EL0. -> Used for thread
                            // scheduler.
      "msr sp_el0, %2\n"    // user program stack pointer set to new stack.
      "mov sp, %3\n" // sp is reference for the same el process. For example,
                     // el2 cannot use sp_el2, it has to use sp to find its
                     // own stack.
      "eret\n" ::"r"(&new_thread->context),
      "r"(new_thread->context.lr), "r"(new_thread->context.sp),
      "r"(new_thread->kernel_stack + KSTACK_SIZE));

  return 0;
}

void schedule_timer(char *arg) {
  uint64_t cntfrq_el0;
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(cntfrq_el0));
  add_timer_task(create_timer_task(cntfrq_el0 >> 5, schedule_timer, NULL, -1));
}