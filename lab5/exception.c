#include "include/exception.h"
#include "include/irq.h"
#include "include/shell.h"
#include "include/signal.h"
#include "include/syscall.h"
#include "include/thread.h"
#include "include/timer.h"
#include "include/types.h"
#include "include/uart.h"

extern irq_task_min_heap_t *irq_task_heap;
extern double_linked_node_t *timer_list_head;
extern int current_irq_task_priority;
extern uint32_t lock_count;
extern double_linked_node_t *run_queue;
extern thread_t *current_thread;
extern int back_to_shell;
extern kernel_context_t kernel_context;

void irq_router(trapframe_t *tpf) {
  if (*IRQ_PENDING_1 & IRQ_PENDING_1_AUX_INT &&
      *CORE0_IRQ_SOURCE & INTERRUPT_SOURCE_GPU) {
    if (*AUX_MU_IIR & 0x04) { // Check if it's a receive interrupt
      *AUX_MU_IER &= ~0x01;
      irq_task_min_heap_push(
          irq_task_heap,
          create_irq_task(uart_rx_handler, NULL, UART_IRQ_PRIORITY));
      irq_task_run_preemptive();
    } else if (*AUX_MU_IIR & 0x02) { // Check if it's a transmit interrupt
      *AUX_MU_IER &= ~0x02;
      irq_task_min_heap_push(
          irq_task_heap,
          create_irq_task(uart_tx_handler, NULL, UART_IRQ_PRIORITY));
      irq_task_run_preemptive();
    }
  }
  if (*CORE0_IRQ_SOURCE & INTERRUPT_SOURCE_CNTPNSIRQ) {
    core_timer_disable();
    core_timer_handler();
    irq_task_run_preemptive();
    core_timer_enable();
    if (run_queue->next->next != run_queue)
      schedule();
  }
  if (back_to_shell) {
    uart_sendline("irq_task_heap->size: %d\n", irq_task_heap->size);
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
  while (irq_task_heap->size > 0) {
    lock();
    irq_task_t *task = irq_task_min_heap_get_min(irq_task_heap);
    if (current_irq_task_priority > task->priority) {
      irq_task_min_heap_pop(irq_task_heap);
      int prev_task_priority = current_irq_task_priority;
      current_irq_task_priority = task->priority;
      unlock();

      ((void (*)(char *))task->callback)(task->callback_arg);
      if (task->callback_arg != NULL) {
        memory_pool_free(task->callback_arg);
      }
      memory_pool_free(task);

      lock();
      current_irq_task_priority = prev_task_priority;
      unlock();
    } else {
      unlock();
      break;
    }
  }
}

void lock() {
  el1_interrupt_disable();
  lock_count++;
}

void unlock() {
  lock_count--;
  if (lock_count == 0)
    el1_interrupt_enable();
}