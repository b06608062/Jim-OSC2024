#include "include/thread.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/dlist.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/mmu.h"
#include "include/signal.h"
#include "include/timer.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern thread_t *current_thread;
extern double_linked_node_t *run_queue;
extern thread_t thread_table[];
extern frame_array_node_t frame_array[];

void thread_init() {
  run_queue = simple_malloc(sizeof(double_linked_node_t), 0);
  double_linked_init(run_queue);

  for (int i = 0; i <= PID_MAX; ++i) {
    thread_table[i].state = THREAD_IDLE;
    thread_table[i].pid = i;
  }

  asm volatile(
      "msr tpidr_el1, %0" ::"r"(simple_malloc(sizeof(thread_context_t), 0)));
  current_thread = thread_create(idle, 0x1000);
  current_thread->context.pgd = (char *)MMU_PGD_BASE;
}

thread_t *thread_create(void *entry_point, uint32_t size) {
  lock();
  // find idle thread
  thread_t *new_thread;
  for (int i = 0; i <= PID_MAX; ++i) {
    if (thread_table[i].state == THREAD_IDLE) {
      new_thread = &thread_table[i];
      break;
    }
  }

  // thread setup
  new_thread->context.lr = (uint64_t)entry_point;
  new_thread->state = THREAD_READY;
  new_thread->user_data_size = size;
  new_thread->kernel_stack = (char *)buddy_system_allocator(KSTACK_SIZE);
  new_thread->context.pgd = (void *)buddy_system_allocator(0x1000);
  simple_memset(new_thread->context.pgd, 0, 0x1000);
  new_thread->context.sp = (uint64_t)new_thread->kernel_stack + KSTACK_SIZE;
  new_thread->context.fp = new_thread->context.sp;
  double_linked_init(&new_thread->vma_list);

  // signal setup
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

int exec_thread(char *data, uint32_t size) {
  thread_t *new_thread = thread_create(data, size);
  for (int i = 0; i <= size / PAGE_SIZE; ++i) {
    uint64_t new_page = buddy_system_allocator(PAGE_SIZE);
    mmu_add_vma(new_thread, USER_SPACE + i * PAGE_SIZE, PAGE_SIZE,
                VIRT_TO_PHYS(new_page), 0b111, 1);
    memcpy((char *)new_page, data + i * PAGE_SIZE, PAGE_SIZE);
    frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
  }
  for (int i = 0; i < USTACK_SIZE / PAGE_SIZE; ++i) {
    uint64_t new_page = buddy_system_allocator(PAGE_SIZE);
    mmu_add_vma(new_thread, USER_STACK_BASE - USTACK_SIZE + i * PAGE_SIZE,
                PAGE_SIZE, VIRT_TO_PHYS(new_page), 0b111, 1);
    frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
  }
  mmu_add_vma(new_thread, PERIPHERAL_START, PERIPHERAL_END - PERIPHERAL_START,
              PERIPHERAL_START, 0b011, 0);
  mmu_add_vma(new_thread, USER_SIGNAL_WRAPPER_VA, 0x2000,
              (size_t)VIRT_TO_PHYS(signal_handler_wrapper), 0b101, 0);

  new_thread->context.pgd = VIRT_TO_PHYS(new_thread->context.pgd);
  new_thread->context.sp = USER_STACK_BASE;
  new_thread->context.fp = USER_STACK_BASE;
  new_thread->context.lr = USER_SPACE;

  current_thread = new_thread;
  add_timer_task(create_timer_task(1, schedule_timer, "", 0));
  // eret to exception level 0
  asm("msr tpidr_el1, %0\n"
      "msr elr_el1, %1\n"
      "msr spsr_el1, xzr\n" // enable interrupt in EL0. You can do it by
                            // setting spsr_el1 to 0 before returning to EL0.
      "msr sp_el0, %2\n"
      "mov sp, %3\n"
      "dsb ish\n" // ensure write has completed
      "msr ttbr0_el1, %4\n"
      "tlbi vmalle1is\n" // invalidate all TLB entries
      "dsb ish\n"        // ensure completion of TLB invalidatation
      "isb\n"            // clear pipeline"
      "eret\n" ::"r"(&new_thread->context),
      "r"(new_thread->context.lr), "r"(new_thread->context.sp),
      "r"(new_thread->kernel_stack + KSTACK_SIZE),
      "r"(new_thread->context.pgd));

  return 0;
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
      mmu_del_vma(thread);
      mmu_free_page_tables(thread->context.pgd, 0);
      buddy_system_free((uint64_t)PHYS_TO_VIRT(thread->context.pgd));
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

void schedule_timer(char *arg) {
  uint64_t cntfrq_el0;
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(cntfrq_el0));
  add_timer_task(create_timer_task(cntfrq_el0 >> 5, schedule_timer, "", -1));
}