#include "include/timer.h"
#include "include/allocator.h"
#include "include/dlist.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern double_linked_node_t *timer_list_head;

void timer_init() {
  uint64_t tmp;
  asm volatile("mrs %0, cntkctl_el1" : "=r"(tmp));
  tmp |= 1;
  asm volatile("msr cntkctl_el1, %0" ::"r"(tmp));

  timer_list_head = simple_malloc(sizeof(double_linked_node_t), 0);
  double_linked_init(timer_list_head);
}

void core_timer_enable() {
  asm volatile("msr cntp_ctl_el0, %0" : : "r"(1));
  *CORE0_TIMER_IRQCNTL = 0x2;
};

void core_timer_disable() { *CORE0_TIMER_IRQCNTL = 0x0; };

timer_task_t *create_timer_task(int time, void *callback, const char *arg,
                                int priority) {
  timer_task_t *task = memory_pool_allocator(sizeof(timer_task_t), 0);
  unsigned long cntpct_el0 = 0;
  unsigned long cntfrq_el0 = 0;
  __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(cntpct_el0));
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(cntfrq_el0));
  if (priority == -1) {
    task->trigger_time = cntpct_el0 + time;
  } else {
    task->trigger_time = cntpct_el0 + cntfrq_el0 * time;
  }
  task->callback = callback;
  const char *prefix = "\n[TIMER IRQ] ";
  int total_length = strlen(prefix) + strlen(arg) + 1;
  char *buf = memory_pool_allocator(total_length, 0);
  strcpy(buf, prefix);
  strcat(buf, arg);
  task->callback_arg = buf;
  task->priority = priority;
  return task;
}

void add_timer_task(timer_task_t *new_task) {
  lock();
  double_linked_node_t *current;
  timer_task_t *entry;
  double_linked_for_each(current, timer_list_head) {
    entry = (timer_task_t *)current;
    if (new_task->trigger_time < entry->trigger_time) {
      break;
    }
  }
  double_linked_add_before(&new_task->node, current);
  core_timer_update();
  unlock();
}

void core_timer_handler() {
  if (double_linked_is_empty(timer_list_head)) {
    __asm__ __volatile__(
        "mrs x1, cntpct_el0\n" // Read current counter value into x1
        "mrs x2, cntfrq_el0\n" // Read the frequency of the counter into x2
        "mov x3, #10000\n"
        "mul x2, x2, x3\n"          // x2 = cntfrq_el0 * 10000
        "add x1, x1, x2\n"          // x1 = cntpct_el0 + cntfrq_el0 * 10000
        "msr cntp_cval_el0, x1\n"); // Set the compare value register to x1
    return;
  }
  add_timer_task_to_irq();
}

void add_timer_task_to_irq() {
  timer_task_t *task = (timer_task_t *)timer_list_head->next;
  double_linked_remove(timer_list_head->next);
  core_timer_update();
  // for preemptive
  // core_timer_enable();
  irq_task_list_insert(
      create_irq_task(task->callback, task->callback_arg, task->priority));
  memory_pool_free(task, 0);
}

void core_timer_update() {
  unsigned long current_time, freq, cval;
  asm volatile("mrs %0, cntpct_el0" : "=r"(current_time));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  if (double_linked_is_empty(timer_list_head)) {
    cval = current_time + freq * 10000;
  } else {
    timer_task_t *next_task = (timer_task_t *)(timer_list_head->next);
    if (next_task->trigger_time > current_time) {
      cval = next_task->trigger_time;
    } else {
      cval = current_time;
    }
  }
  asm volatile("msr cntp_cval_el0, %0" : : "r"(cval));
}