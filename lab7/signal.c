#include "include/signal.h"
#include "include/buddy_system.h"
#include "include/exception.h"
#include "include/syscall.h"
#include "include/thread.h"
#include "include/types.h"
#include "include/uart.h"

extern thread_t *current_thread;

void signal_default_handler() { kill(0, current_thread->pid); }

void check_signal(trapframe_t *tpf) {
  lock();
  if (current_thread->signal_running) {
    unlock();
    return;
  }
  current_thread->signal_running = 1;
  unlock();
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    store_context(&current_thread->signal_context);
    if (current_thread->signal_count[i] > 0) {
      lock();
      current_thread->signal_count[i]--;
      unlock();
      run_signal(tpf, i);
    }
  }
  lock();
  current_thread->signal_running = 0;
  unlock();
}

void run_signal(trapframe_t *tpf, int signal) {
  current_thread->current_signal_handler =
      current_thread->signal_handler[signal];
  uart_sendline("signal_handler_wrapper: 0x%p\nhandler: 0x%p\n",
                signal_handler_wrapper, current_thread->current_signal_handler);
  if (current_thread->current_signal_handler == signal_default_handler) {
    signal_default_handler();
    return;
  }

  asm("msr elr_el1, %0\n"
      "msr sp_el0, %1\n"
      "msr spsr_el1, %2\n"
      "mov x0, %3\n"
      "eret\n" ::"r"(USER_SIGNAL_WRAPPER_VA),
      "r"(tpf->sp_el0), "r"(tpf->spsr_el1),
      "r"(current_thread->current_signal_handler));
}

__attribute__((aligned(0x1000))) void signal_handler_wrapper() {
  // elr_el1 set to function -> call function by x0
  // system call sigreturn
  asm("blr x0\n"
      "mov x8,50\n"
      "svc 0\n");
}
