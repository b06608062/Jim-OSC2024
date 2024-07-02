#include "include/syscall.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/exception.h"
#include "include/mbox.h"
#include "include/signal.h"
#include "include/thread.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern thread_t *current_thread;
extern thread_t thread_table[];
extern volatile unsigned int mbox[];

int getpid(trapframe_t *tpf) {
  tpf->x0 = current_thread->pid;
  return current_thread->pid;
}

size_t uartread(trapframe_t *tpf, char buf[], size_t size) {
  int i = 0;
  // uart_sendline("rx &i: %p\n", &i);
  // uart_sendline("rx &buf: %p\n", buf);
  for (; i < size; ++i) {
    buf[i] = uart_async_getc();
  }
  tpf->x0 = i;
  return i;
}

size_t uartwrite(trapframe_t *tpf, const char buf[], size_t size) {
  int i = 0;
  // uart_sendline("tx &i: %p\n", &i);
  // uart_sendline("tx &buf: %p\n", buf);
  for (; i < size; ++i) {
    uart_async_putc(buf[i]);
  }
  tpf->x0 = i;
  return i;
}

int exec(trapframe_t *tpf, const char *name, char *const argv[]) {
  uart_sendline("exec: %s.\n", name);
  current_thread->user_data_size = cpio_get_file_size(name);
  char *new_data = cpio_get_file_data(name);
  for (uint32_t i = 0; i < current_thread->user_data_size; ++i) {
    current_thread->user_space[i] = new_data[i];
  }
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    current_thread->signal_handler[i] = signal_default_handler;
  }
  tpf->elr_el1 = USER_SPACE_BASE;
  tpf->sp_el0 = USER_STACK_BASE;
  tpf->x0 = 0;
  return 0;
}

int fork(trapframe_t *tpf) {
  lock();
  thread_t *child_thread =
      thread_create(current_thread->user_space, current_thread->user_data_size);

  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    child_thread->signal_handler[i] = current_thread->signal_handler[i];
  }

  // remap
  mappages(child_thread->context.pgd, USER_SPACE_BASE,
           child_thread->user_data_size,
           (size_t)VIRT_TO_PHYS(child_thread->user_space), 0);
  mappages(child_thread->context.pgd, USER_STACK_BASE - USTACK_SIZE,
           USTACK_SIZE, (size_t)VIRT_TO_PHYS(child_thread->user_stack), 0);
  mappages(child_thread->context.pgd, PERIPHERAL_START,
           PERIPHERAL_END - PERIPHERAL_START, PERIPHERAL_START, 0);

  // Additional Block for Read-Only block -> User space signal handler cannot be
  // killed
  mappages(child_thread->context.pgd, USER_SIGNAL_WRAPPER_VA, 0x2000,
           (size_t)VIRT_TO_PHYS(signal_handler_wrapper), PD_RDONLY);

  int parent_pid = current_thread->pid;
  uint64_t kernel_stack_offset = (uint64_t)child_thread->kernel_stack -
                                 (uint64_t)current_thread->kernel_stack;

  // copy data into new process
  for (uint32_t i = 0; i < child_thread->user_data_size; ++i) {
    child_thread->user_space[i] = current_thread->user_space[i];
  }

  // copy user stack into new process
  for (uint32_t i = 0; i < USTACK_SIZE; ++i) {
    child_thread->user_stack[i] = current_thread->user_stack[i];
  }

  // copy stack into new process
  for (uint32_t i = 0; i < KSTACK_SIZE; ++i) {
    child_thread->kernel_stack[i] = current_thread->kernel_stack[i];
  }

  store_context(get_current());
  if (parent_pid != current_thread->pid) {
    goto child;
  }

  // parent
  void *temp_pgd = child_thread->context.pgd;
  child_thread->context = current_thread->context;
  child_thread->context.pgd = VIRT_TO_PHYS(temp_pgd);
  child_thread->context.sp += kernel_stack_offset;
  child_thread->context.fp += kernel_stack_offset;

  unlock();

  tpf->x0 = child_thread->pid;
  return child_thread->pid;

child:

  // child
  tpf = (trapframe_t *)((uint64_t)tpf + kernel_stack_offset);
  tpf->x0 = 0;
  return 0;
}

int syscall_mbox_call(trapframe_t *tpf, unsigned char ch,
                      unsigned int *mbox_user) {
  lock();

  unsigned int size_of_mbox = mbox_user[0];
  memcpy((char *)mbox, mbox_user, size_of_mbox);
  mbox_call(MBOX_CH_PROP);
  memcpy(mbox_user, (char *)mbox, size_of_mbox);

  tpf->x0 = (mbox_user[1] == MBOX_RESPONSE);
  unlock();
  return 0;
}

void exit(trapframe_t *tpf, int status) { thread_exit(); }

void kill(trapframe_t *tpf, int pid) {
  lock();
  if (pid < 0 || pid >= PID_MAX || thread_table[pid].state == THREAD_IDLE ||
      thread_table[pid].state == THREAD_ZOMBIE) {
    unlock();
    return;
  }
  thread_table[pid].state = THREAD_ZOMBIE;
  unlock();
  schedule();
}

void signal_register(int signal, void (*handler)()) {
  if (signal < 0 || signal > SIGNAL_MAX)
    return;
  current_thread->signal_handler[signal] = handler;
}

void signal_kill(int pid, int signal) {
  lock();
  if (pid < 0 || pid > PID_MAX ||
      (thread_table[pid].state != THREAD_RUNNING &&
       thread_table[pid].state != THREAD_READY)) {
    unlock();
    return;
  }
  thread_table[pid].signal_count[signal]++;
  unlock();
}

void signal_return(trapframe_t *tpf) {
  load_context(&current_thread->signal_context);
}