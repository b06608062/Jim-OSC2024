#include "include/syscall.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/exception.h"
#include "include/mbox.h"
#include "include/signal.h"
#include "include/thread.h"
#include "include/types.h"
#include "include/uart.h"

extern thread_t *current_thread;
extern thread_t thread_table[];

int getpid(trapframe_t *tpf) {
  tpf->x0 = current_thread->pid;
  return current_thread->pid;
}

size_t uartread(trapframe_t *tpf, char buf[], size_t size) {
  int i = 0;
  for (; i < size; ++i) {
    buf[i] = uart_async_getc();
  }
  tpf->x0 = i;
  return i;
}

size_t uartwrite(trapframe_t *tpf, const char buf[], size_t size) {
  int i = 0;
  for (; i < size; ++i) {
    uart_async_putc(buf[i]);
  }
  tpf->x0 = i;
  return i;
}

int exec(trapframe_t *tpf, const char *name, char *const argv[]) {
  current_thread->user_data_size = cpio_get_file_size(name);
  char *new_data = cpio_get_file_data(name);
  for (uint32_t i = 0; i < current_thread->user_data_size; ++i) {
    current_thread->user_space[i] = new_data[i];
  }
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    current_thread->signal_handler[i] = signal_default_handler;
  }
  tpf->elr_el1 = (uint64_t)current_thread->user_space;
  tpf->sp_el0 = (uint64_t)current_thread->user_stack + USTACK_SIZE;
  tpf->x0 = 0;
  return 0;
}

int fork(trapframe_t *tpf) {
  lock();
  thread_t *child_thread = thread_create(current_thread->user_space);
  thread_t *parent_thread = current_thread;
  uint64_t kernel_stack_offset = (uint64_t)child_thread->kernel_stack -
                                 (uint64_t)parent_thread->kernel_stack;
  uint64_t user_stack_offset =
      (uint64_t)child_thread->user_stack - (uint64_t)parent_thread->user_stack;

  child_thread->user_data_size = current_thread->user_data_size;
  for (int i = 0; i < USTACK_SIZE; ++i) {
    child_thread->user_stack[i] = current_thread->user_stack[i];
  }
  for (int i = 0; i < KSTACK_SIZE; ++i) {
    child_thread->kernel_stack[i] = current_thread->kernel_stack[i];
  }
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    child_thread->signal_handler[i] = current_thread->signal_handler[i];
  }

  store_context(get_current());
  if (parent_thread->pid != current_thread->pid) {
    goto child;
  }

  // parent
  child_thread->context = current_thread->context;
  child_thread->context.sp += kernel_stack_offset;
  child_thread->context.fp += kernel_stack_offset;
  unlock();
  tpf->x0 = child_thread->pid;
  return child_thread->pid;

child:

  // child
  tpf = (trapframe_t *)((uint64_t)tpf + kernel_stack_offset);
  tpf->sp_el0 += user_stack_offset;
  tpf->x0 = 0;
  return 0;
}

int syscall_mbox_call(trapframe_t *tpf, uint8_t ch, uint32_t *mbox) {
  lock();
  // for (int i = 0; i < mbox[0] / 4; ++i) {
  //   uart_sendline("Request: mbox[%d] = 0x%x.\n", i, mbox[i]);
  // }
  uint64_t r = (((uint64_t)mbox & ~0xF) | (ch & 0xF));
  do {
    asm volatile("nop");
  } while (*MBOX_STATUS & MBOX_FULL);
  *MBOX_WRITE = r;
  while (1) {
    do {
      asm volatile("nop");
    } while (*MBOX_STATUS & MBOX_EMPTY);
    if (r == *MBOX_READ) {
      // for (int i = 0; i < mbox[0] / 4; ++i) {
      //   uart_sendline("Response: mbox[%d] = 0x%x.\n", i, mbox[i]);
      // }
      tpf->x0 = (mbox[1] == MBOX_RESPONSE);
      unlock();
      return mbox[1] == MBOX_RESPONSE;
    }
  }
  tpf->x0 = 0;
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
  uint64_t signal_user_stack = tpf->sp_el0 % USTACK_SIZE == 0
                                   ? tpf->sp_el0 - USTACK_SIZE
                                   : tpf->sp_el0 & (~(USTACK_SIZE - 1));
  buddy_system_free(signal_user_stack);
  load_context(&current_thread->signal_context);
}