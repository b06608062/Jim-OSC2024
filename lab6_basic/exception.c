#include "include/exception.h"
#include "include/allocator.h"
#include "include/irq.h"
#include "include/shell.h"
#include "include/signal.h"
#include "include/syscall.h"
#include "include/thread.h"
#include "include/timer.h"
#include "include/types.h"
#include "include/uart.h"

extern double_linked_node_t *irq_task_list_head;
extern double_linked_node_t *timer_list_head;
extern int current_irq_task_priority;
extern uint32_t lock_count;
extern double_linked_node_t *run_queue;
extern int back_to_shell;
extern kernel_context_t kernel_context;
extern int init_done;

void irq_router(trapframe_t *tpf) {
  if (*IRQ_PENDING_1 & IRQ_PENDING_1_AUX_INT &&
      *CORE0_IRQ_SOURCE & INTERRUPT_SOURCE_GPU) {
    if (*AUX_MU_IIR & 0x04) { // Check if it's a receive interrupt
      *AUX_MU_IER &= ~0x01;
      irq_task_list_insert(
          create_irq_task(uart_rx_handler, NULL, UART_IRQ_PRIORITY));
      irq_task_run_preemptive();
    } else if (*AUX_MU_IIR & 0x02) { // Check if it's a transmit interrupt
      *AUX_MU_IER &= ~0x02;
      irq_task_list_insert(
          create_irq_task(uart_tx_handler, NULL, UART_IRQ_PRIORITY));
      irq_task_run_preemptive();
    }
  } else if (*CORE0_IRQ_SOURCE & INTERRUPT_SOURCE_CNTPNSIRQ) {
    core_timer_disable();
    core_timer_handler();
    irq_task_run_preemptive();
    core_timer_enable();
    if (run_queue->next->next != run_queue)
      schedule();
  }

  if (back_to_shell) {
    uart_sendline("current_irq_task_priority: %d\n", current_irq_task_priority);
    load_kernel_context(&kernel_context);
  }
  if ((tpf->spsr_el1 & 0b1100) == 0) {
    check_signal(tpf);
  }
};

void el0_sync_router(trapframe_t *tpf) {
  el1_interrupt_enable();
  uint64_t syscall_no = tpf->x8;
  // uart_sendline("syscall_no: %d\n", syscall_no);
  if (syscall_no == 0) {
    getpid(tpf);
  } else if (syscall_no == 1) {
    uartread(tpf, (char *)tpf->x0, tpf->x1);
  } else if (syscall_no == 2) {
    uartwrite(tpf, (char *)tpf->x0, tpf->x1);
  } else if (syscall_no == 3) {
    exec(tpf, (char *)tpf->x0, (char **)tpf->x1);
  } else if (syscall_no == 4) {
    fork(tpf);
  } else if (syscall_no == 5) {
    exit(tpf, tpf->x0);
  } else if (syscall_no == 6) {
    syscall_mbox_call(tpf, (unsigned char)tpf->x0, (unsigned int *)tpf->x1);
  } else if (syscall_no == 7) {
    kill(tpf, (int)tpf->x0);
  } else if (syscall_no == 8) {
    signal_register(tpf->x0, (void (*)())tpf->x1);
  } else if (syscall_no == 9) {
    signal_kill(tpf->x0, tpf->x1);
  } else if (syscall_no == 50) {
    signal_return(tpf);
  } else if (syscall_no == 87) {
    uart_sendline("syscall_no: %d\n", syscall_no);
    unsigned long spsr_el1, elr_el1, esr_el1;
    asm volatile("mrs %0, spsr_el1" : "=r"(spsr_el1));
    asm volatile("mrs %0, elr_el1" : "=r"(elr_el1));
    asm volatile("mrs %0, esr_el1" : "=r"(esr_el1));
    uart_sendline("spsr_el1 : 0x%x, elr_el1 : 0x%x, esr_el1 : 0x%x.\n",
                  spsr_el1, elr_el1, esr_el1);
  }
};

void invalid_exception_router() {
  // todo
  uart_sendline("Invalid exception.\n");
};

void irq_task_run_preemptive() {
  while (1) {
    lock();
    if (double_linked_is_empty(irq_task_list_head)) {
      unlock();
      break;
    }

    irq_task_t *the_task = (irq_task_t *)irq_task_list_head->next;
    if (current_irq_task_priority <= the_task->priority) {
      unlock();
      break;
    }
    double_linked_remove((double_linked_node_t *)the_task);
    int prev_irq_task_priority = current_irq_task_priority;
    current_irq_task_priority = the_task->priority;
    unlock();

    ((void (*)(void *))the_task->callback)(the_task->callback_arg);
    if (the_task->callback_arg != NULL) {
      memory_pool_free(the_task->callback_arg, 0);
    }
    memory_pool_free(the_task, 0);

    lock();
    current_irq_task_priority = prev_irq_task_priority;
    unlock();
  }
}

void lock() {
  el1_interrupt_disable();
  lock_count++;
}

void unlock() {
  lock_count--;
  if (lock_count == 0 && init_done)
    el1_interrupt_enable();
}

void irq_task_list_init() {
  irq_task_list_head = simple_malloc(sizeof(double_linked_node_t), 0);
  double_linked_init(irq_task_list_head);
}

void irq_task_list_insert(irq_task_t *task) {
  lock();
  double_linked_node_t *cur;
  double_linked_for_each(cur, irq_task_list_head) {
    irq_task_t *cur_task = (irq_task_t *)cur;
    if (cur_task->priority > task->priority) {
      double_linked_add_before(&task->node, cur);
      unlock();
      return;
    }
  }
  double_linked_add_before(&task->node, irq_task_list_head);
  unlock();
}

irq_task_t *create_irq_task(void *callback, void *arg, int priority) {
  irq_task_t *task = memory_pool_allocator(sizeof(irq_task_t), 0);
  task->callback = callback;
  task->callback_arg = arg;
  task->priority = priority;
  double_linked_init(&task->node);
  return task;
}