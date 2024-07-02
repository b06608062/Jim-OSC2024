#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "dlist.h"
#include "heap.h"
#include "mmu.h"
#include "syscall.h"

#define CORE0_IRQ_SOURCE ((volatile unsigned int *)(PHYS_TO_VIRT(0x40000060)))
#define INTERRUPT_SOURCE_CNTPNSIRQ (1 << 1)
#define INTERRUPT_SOURCE_GPU (1 << 8)

#define UART_IRQ_PRIORITY 10
#define TIMER_IRQ_DEFAULT_PRIORITY 0

typedef struct irq_task {
  double_linked_node_t node;
  void *callback;
  void *callback_arg;
  int priority;
} irq_task_t;

static inline void el1_interrupt_enable() {
  __asm__ __volatile__("msr daifclr, 0xf");
}

static inline void el1_interrupt_disable() {
  __asm__ __volatile__("msr daifset, 0xf");
}

void irq_router(trapframe_t *tpf);
void el0_sync_router(trapframe_t *tpf);
void invalid_exception_router();
void irq_task_run_preemptive();
void lock();
void unlock();
void irq_task_list_init();
void irq_task_list_insert(irq_task_t *task);
irq_task_t *create_irq_task(void *callback, void *arg, int priority);

#endif /* EXCEPTION_H */