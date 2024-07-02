#include "include/syscall.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/exception.h"
#include "include/mbox.h"
#include "include/mmu.h"
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
  mmu_del_vma(current_thread);
  double_linked_init(&current_thread->vma_list);

  current_thread->user_data_size = cpio_get_file_size(name);
  char *new_data = cpio_get_file_data(name);
  current_thread->user_space =
      (char *)buddy_system_allocator(current_thread->user_data_size);
  current_thread->user_stack = (char *)buddy_system_allocator(USTACK_SIZE);

  asm("dsb ish\n"); // ensure write has completed
  mmu_free_page_tables(current_thread->context.pgd, 0);
  simple_memset((void *)PHYS_TO_VIRT(current_thread->context.pgd), 0, 0x1000);
  asm("tlbi vmalle1is\n" // invalidate all TLB entries
      "dsb ish\n"        // ensure completion of TLB invalidatation
      "isb\n");          // clear pipeline

  mmu_add_vma(current_thread, USER_SPACE, current_thread->user_data_size,
              (size_t)VIRT_TO_PHYS(current_thread->user_space), 0b111, 1);
  mmu_add_vma(current_thread, USER_STACK_BASE - USTACK_SIZE, USTACK_SIZE,
              (size_t)VIRT_TO_PHYS(current_thread->user_stack), 0b111, 1);
  mmu_add_vma(current_thread, PERIPHERAL_START,
              PERIPHERAL_END - PERIPHERAL_START, PERIPHERAL_START, 0b011, 0);
  mmu_add_vma(current_thread, USER_SIGNAL_WRAPPER_VA, 0x2000,
              (size_t)VIRT_TO_PHYS(signal_handler_wrapper), 0b101, 0);

  for (uint32_t i = 0; i < current_thread->user_data_size; ++i) {
    current_thread->user_space[i] = new_data[i];
  }
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    current_thread->signal_handler[i] = signal_default_handler;
  }

  tpf->elr_el1 = USER_SPACE;
  tpf->sp_el0 = USER_STACK_BASE;
  tpf->x0 = 0;
  return 0;
}

int fork(trapframe_t *tpf) {
  lock();
  thread_t *child_thread =
      thread_create(current_thread->user_space, current_thread->user_data_size);

  double_linked_node_t *cur;
  vm_area_struct_t *vma;
  double_linked_for_each(cur, &current_thread->vma_list) {
    vma = (vm_area_struct_t *)cur;
    if (vma->virt_addr == USER_SPACE ||
        vma->virt_addr == USER_STACK_BASE - USTACK_SIZE ||
        vma->virt_addr == USER_SIGNAL_WRAPPER_VA ||
        vma->virt_addr == PERIPHERAL_START) {
      continue;
    }
    char *new_alloc = (char *)buddy_system_allocator(vma->area_size);
    mmu_add_vma(child_thread, vma->virt_addr, vma->area_size,
                (size_t)VIRT_TO_PHYS(new_alloc), vma->rwx, 1);
    memcpy(new_alloc, (void *)PHYS_TO_VIRT(vma->phys_addr), vma->area_size);
  }
  mmu_add_vma(child_thread, USER_SPACE, child_thread->user_data_size,
              (size_t)VIRT_TO_PHYS(child_thread->user_space), 0b111, 1);
  mmu_add_vma(child_thread, USER_STACK_BASE - USTACK_SIZE, USTACK_SIZE,
              (size_t)VIRT_TO_PHYS(child_thread->user_stack), 0b111, 1);
  mmu_add_vma(child_thread, PERIPHERAL_START, PERIPHERAL_END - PERIPHERAL_START,
              PERIPHERAL_START, 0b011, 0);
  mmu_add_vma(child_thread, USER_SIGNAL_WRAPPER_VA, 0x2000,
              (size_t)VIRT_TO_PHYS(signal_handler_wrapper), 0b101, 0);

  int parent_pid = current_thread->pid;
  uint64_t kernel_stack_offset = (uint64_t)child_thread->kernel_stack -
                                 (uint64_t)current_thread->kernel_stack;

  // copy signal handler into new process
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    uart_sendline("current_thread->signal_handler[%d] = %p\n", i,
                  current_thread->signal_handler[i]);
    child_thread->signal_handler[i] = current_thread->signal_handler[i];
  }
  // copy data into new process
  for (uint32_t i = 0; i < child_thread->user_data_size; ++i) {
    child_thread->user_space[i] = current_thread->user_space[i];
  }
  // copy user stack into new process
  for (uint32_t i = 0; i < USTACK_SIZE; ++i) {
    child_thread->user_stack[i] = current_thread->user_stack[i];
  }
  // copy kernel stack into new process
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

int syscall_mbox_call(trapframe_t *tpf, uint8_t ch, uint32_t *mbox_user) {
  lock();
  uint32_t size_of_mbox = mbox_user[0];
  memcpy((char *)mbox, mbox_user, size_of_mbox);
  mbox_call(MBOX_CH_PROP);
  memcpy(mbox_user, (char *)mbox, size_of_mbox);
  tpf->x0 = 8;
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

// only need to implement the anonymous page mapping in this Lab.
void *mmap(trapframe_t *tpf, void *addr, size_t len, int prot, int flags,
           int fd, int file_offset) {
  // Ignore flags as we have demand pages

  // Req #3 Page size round up
  len = len % 0x1000 ? len + (0x1000 - len % 0x1000) : len;
  addr = (uint64_t)addr % 0x1000 ? addr + (0x1000 - (uint64_t)addr % 0x1000)
                                 : addr;

  // Req #2 check if overlap
  double_linked_node_t *cur;
  vm_area_struct_t *vma;
  vm_area_struct_t *the_area_ptr = NULL;
  double_linked_for_each(cur, &current_thread->vma_list) {
    vma = (vm_area_struct_t *)cur;
    // Detect existing vma overlapped
    if (!((uint64_t)(addr + len) <= vma->virt_addr ||
          (uint64_t)addr >= vma->virt_addr + vma->area_size)) {
      the_area_ptr = vma;
      break;
    }
  }
  // take as a hint to decide new region's start address
  if (the_area_ptr) {
    tpf->x0 = (uint64_t)mmap(
        tpf, (void *)(the_area_ptr->virt_addr + the_area_ptr->area_size), len,
        prot, flags, fd, file_offset);
    return (void *)tpf->x0;
  }
  // create new valid region, map and set the page attributes (prot)
  mmu_add_vma(current_thread, (uint64_t)addr, len,
              VIRT_TO_PHYS(buddy_system_allocator(len)), prot, 1);
  tpf->x0 = (uint64_t)addr;
  return (void *)tpf->x0;
}