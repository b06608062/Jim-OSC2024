#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/dtb.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/mmu.h"
#include "include/shell.h"
#include "include/thread.h"
#include "include/timer.h"
#include "include/uart.h"

extern char *dtb_ptr;
extern char _start;
extern char _end;
extern char *heap_ptr;
extern int init_done;

unsigned long get_stack_pointer() {
  unsigned long sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return sp;
}

int main(char *arg) {
  dtb_ptr = PHYS_TO_VIRT(arg);
  uart_init();
  uart_sendline("Code start at address: 0x%p.\n", (unsigned long)&_start);
  uart_sendline("Code end at address: 0x%p.\n", (unsigned long)&_end);
  uart_sendline("Stack pointer at address: 0x%p.\n", get_stack_pointer());
  uart_sendline("DTB header at address: 0x%p.\n", (unsigned long)dtb_ptr);
  dtb_initramfs_init();
  heap_init();
  uart_getc();
  startup_memory_block_table_init();
  buddy_system_init();
  memory_pool_init();
  buddy_system_print_freelists(0);
  uart_sendline("============================\n");

  thread_init();
  timer_init();
  irq_task_list_init();
  init_done = 1;
  uart_sendline("Heap pointer now at address: 0x%p.\n",
                (unsigned long)heap_ptr);

  core_timer_enable();
  el1_interrupt_enable();

  shell_run();
  return 0;
}