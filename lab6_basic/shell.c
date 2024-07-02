#include "include/shell.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/dtb.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/mbox.h"
#include "include/power.h"
#include "include/thread.h"
#include "include/timer.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern char cmd[];
extern int preempt;
extern char *user_space;
extern char *user_stack;
extern char *kernel_stack;
extern int back_to_shell;
extern kernel_context_t kernel_context;

void show_banner() {
  uart_sendline("======================================================\n");
  uart_sendline("||                                                  ||\n");
  uart_sendline("||           Raspberry Pi Mini UART Shell           ||\n");
  uart_sendline("||                                                  ||\n");
  uart_sendline("======================================================\n");
  uart_sendline("Type 'help' for a list of available commands.\n");
}

void shell_run() {
  char *saveptr;
  int cur_pos = 0, end_pos = 0;
  show_banner();

  uart_interrupts_enable();
  while (1) {
    uart_sendline("# ");
    end_pos = 0, cur_pos = 0;
    char c = '\0';
    for (int i = 0; i < MAX_CMD_LEN; ++i) {
      cmd[i] = '\0';
    }

    do {
      c = uart_async_getc();
      if (c == '\n') {
        uart_sendline("\n");
        cmd[end_pos] = '\0';
        break;
      } else if (c == '\b' || c == 0x7F) {
        if (cur_pos > 0) {
          cur_pos--, end_pos--;
          char *start_ptr = &cmd[cur_pos];
          char *end_ptr = &cmd[end_pos] - 1;
          for (char *ptr = start_ptr; ptr <= end_ptr; ptr++) {
            *ptr = *(ptr + 1);
          }
          cmd[end_pos] = '\0';
          uart_sendline("\b\x1b[s\x1b[K%s\x1b[u", &cmd[cur_pos]);
        }
      } else if (c == '\x1b') {
        c = uart_async_getc(), c = uart_async_getc();
        if (c == 'D' && cur_pos > 0) {
          cur_pos--;
          uart_sendline("\x1b[D");
        } else if (c == 'C' && cur_pos < end_pos) {
          cur_pos++;
          uart_sendline("\x1b[C");
        }
      } else {
        char *end_ptr = &cmd[end_pos] - 1;
        char *insert_ptr = &cmd[cur_pos];
        for (char *ptr = end_ptr; ptr >= insert_ptr; --ptr) {
          *(ptr + 1) = *ptr;
        }
        *insert_ptr = c;
        cur_pos++, end_pos++;
        uart_sendline("\x1b[s\x1b[K%s\x1b[u\x1b[C", insert_ptr);
      }
    } while (end_pos < MAX_CMD_LEN - 1);

    char *token = strtok(cmd, " ", &saveptr);
    if (token == NULL)
      continue;
    if (strcmp(token, "help") == 0) {
      do_cmd_help();
    } else if (strcmp(token, "hello") == 0) {
      do_cmd_hello();
    } else if (strcmp(token, "info") == 0) {
      do_cmd_info();
    } else if (strcmp(token, "clear") == 0) {
      do_cmd_clear();
    } else if (strcmp(token, "reboot") == 0) {
      do_cmd_reboot();
    } else if (strcmp(token, "cancel") == 0) {
      do_cmd_cancel();
    } else if (strcmp(token, "ls") == 0) {
      do_cmd_ls();
    } else if (strcmp(token, "cat") == 0) {
      char *filename = strtok(NULL, " ", &saveptr);
      if (filename) {
        do_cmd_cat(filename);
      } else {
        uart_sendline("Usage: cat <filename>\n");
      }
    } else if (strcmp(token, "simple_malloc") == 0) {
      do_cmd_simple_malloc();
    } else if (strcmp(token, "dtb") == 0) {
      do_cmd_dtb();
    } else if (strcmp(token, "set") == 0) {
      char *msg = strtok(NULL, " ", &saveptr);
      char *secs = strtok(NULL, " ", &saveptr);
      char *priority = strtok(NULL, " ", &saveptr);
      if (msg && secs) {
        if (priority)
          do_cmd_setTimeout(msg, atoi(secs), TIMER_IRQ_DEFAULT_PRIORITY);
        else
          do_cmd_setTimeout(msg, atoi(secs), atoi(priority));
      } else {
        uart_sendline("Usage: set <message> <seconds> [priority]\n");
      }
    } else if (strcmp(token, "preempt") == 0) {
      continue;
      // lab3
      do_cmd_preempt();
    } else if (strcmp(token, "freelist") == 0) {
      char *show_bitmap = strtok(NULL, " ", &saveptr);
      do_cmd_freelist(atoi(show_bitmap));
    } else if (strcmp(token, "malloc") == 0) {
      char *size = strtok(NULL, " ", &saveptr);
      do_cmd_malloc(atoi(size));
    } else if (strcmp(token, "free") == 0) {
      unsigned long addr = atoi(strtok(NULL, " ", &saveptr)) * PAGE_SIZE;
      if (addr == 0) {
        uart_sendline("Invalid frame index.\n");
        continue;
      }
      do_cmd_free(addr);
    } else if (strcmp(token, "sfree") == 0) {
      unsigned long addr = str_to_hex(strtok(NULL, " ", &saveptr));
      if (addr == 0) {
        uart_sendline("Invalid sfree address.\n");
        continue;
      }
      do_cmd_sfree(addr);
    } else if (strcmp(token, "thread") == 0) {
      do_cmd_thread();
    } else if (strcmp(token, "exec") == 0) {
      char *progname = strtok(NULL, " ", &saveptr);
      if (progname) {
        do_cmd_exec(progname);
      } else {
        uart_sendline("Usage: exec <progname>\n");
      }
    } else if (strcmp(token, "exec_test") == 0) {
      do_cmd_exec_test();
    } else if (strcmp(token, "exit") == 0) {
      uart_sendline("Exiting...\n");
      break;
    } else {
      uart_sendline("Unknown command: %s\n", cmd);
    }
  }
}

void format_command(const char *command, const char *description) {
  int command_width = 30;
  int len = strlen(command);
  uart_sendline(command);
  for (int i = len; i < command_width; ++i) {
    uart_sendline(" ");
  }
  uart_sendline("-  %s\n", description);
}

void do_cmd_help() {
  uart_sendline("Supported commands:\n");
  uart_sendline("\x1B[32m");
  format_command(" help", "Show commands.");
  format_command(" hello", "Print 'Hello, <your name>!'");
  format_command(" info", "Show board and memory info.");
  format_command(" reboot", "Reboot the device.");
  format_command(" cancel", "Cancel reboot.");
  format_command(" clear", "Clear the screen.");
  format_command(" ls", "List directory contents.");
  format_command(" cat <filename>", "Display file content.");
  format_command(" simple_malloc", "Demonstrate memory allocation.");
  format_command(" dtb", "Show device tree.");
  format_command(" set <msg> <secs> [priority]", "Schedule timer message.");
  format_command(" preempt", "Test preemptive IRQ.");
  format_command(" freelist [show_bitmap]",
                 "Show buddy system free lists and bitmap.");
  format_command(" malloc <size>", "Allocate memory.");
  format_command(" free <frame_index>", "Free buddy system memory.");
  format_command(" sfree <address>", "Free memory pool address.");
  format_command(" thread", "Run thread test.");
  format_command(" exec <progname>", "Execute user program.");
  format_command(" exec_test", "Execute test program.");
  format_command(" exit", "Exit the shell.");
  uart_sendline("\x1B[0m");
}

void do_cmd_hello() {
  char *name = "Jim";
  uart_sendline("Async Hello, %s!\n", name);
}

void do_cmd_info() {
  mbox_get_board_revision();
  mbox_get_arm_memory();
}

void do_cmd_clear() {
  uart_sendline("\n\n\r\x1B[2J\x1B[H");
  show_banner();
}

void do_cmd_reboot() { reboot(); }

void do_cmd_cancel() { cancel_reboot(); }

void do_cmd_ls() { cpio_parse_header(NULL); }

void do_cmd_cat(const char *filename) {
  if (cpio_parse_header(filename) == -1) {
    uart_sendline("File %s not found.\n", filename);
  }
}

void do_cmd_simple_malloc() {
  simple_malloc(0x7, 1);
  simple_malloc(0x17, 1);
  simple_malloc(0x24, 1);
  simple_malloc(0x1000, 1);
}

void do_cmd_dtb() { dtb_traverse_device_tree(); }

void do_cmd_setTimeout(const char *msg, int secs, int priority) {
  uart_sendline("Set message = '%s', delay = %d, priority = %d\n", msg, secs,
                priority);
  add_timer_task(create_timer_task(secs, uart_sendline, msg, priority));
}

void do_cmd_preempt() {
  add_timer_task(create_timer_task(5, start_preemption_test, NULL, 1));
  add_timer_task(create_timer_task(10, stop_preemption_test, NULL, 0));
}

void start_preemption_test(char *arg) {
  uart_sendline("Starting preemption test.\n");
  preempt = 1;
  while (preempt) {
  }
}

void stop_preemption_test(char *arg) {
  uart_sendline("Stopping preemption test.\n");
  preempt = 0;
}

void do_cmd_freelist(int show_bitmap) {
  buddy_system_print_freelists(show_bitmap);
}

void do_cmd_malloc(unsigned int size) {
  if (size == 0 || size > (1 << MAX_LEVEL) * PAGE_SIZE) {
    uart_sendline("Invalid allocation size.\n");
    return;
  }
  if (size <= 1024) {
    memory_pool_allocator(size, 1);
  } else {
    uart_sendline("[Allocator] Allocated frame %u\n",
                  (buddy_system_allocator(size) - BUDDY_MEMORY_BASE) /
                      PAGE_SIZE);
  }
  buddy_system_print_freelists(0);
}

void do_cmd_free(unsigned long addr) {
  buddy_system_free(BUDDY_MEMORY_BASE + addr);
  uart_sendline("[Allocator] Free frame %u\n", addr / PAGE_SIZE);
  buddy_system_print_freelists(0);
}

void do_cmd_sfree(unsigned long addr) {
  addr += BUDDY_MEMORY_BASE;
  memory_pool_free((void *)addr, 1);
  buddy_system_print_freelists(0);
}

void do_cmd_exec(const char *progname) {
  char *progdata = cpio_get_file_data(progname);
  unsigned long progsize = cpio_get_file_size(progname);
  if (progdata == NULL) {
    return;
  }
  exec_thread(progdata, progsize);
}

void do_cmd_thread() {
  for (int i = 0; i < 5; ++i) {
    thread_create(thread_test, 0x1000);
  }
  schedule();
}

void do_cmd_exec_test() {
  char *progdata = cpio_get_file_data("example.img");
  unsigned long progsize = cpio_get_file_size("example.img");
  char *user_space = (char *)buddy_system_allocator(progsize);
  char *user_stack = (char *)buddy_system_allocator(USTACK_SIZE);
  char *kernel_stack = (char *)buddy_system_allocator(KSTACK_SIZE);
  for (int i = 0; i < progsize; ++i) {
    user_space[i] = progdata[i];
  }
  add_timer_task(
      create_timer_task(5, back_to_kernel, "Back to kernel!!!\n", 0));
  save_kernel_context(&kernel_context);
  if (!back_to_shell) {
    uart_sendline("Executing user program...\n");
    // eret to exception level 0
    asm volatile(
        "msr elr_el1, %0\n"   // elr_el1: Set the address to return address
                              // for el1 -> el0.
        "msr spsr_el1, xzr\n" // spsr_el1: Enable interrupt in EL0.
        "msr sp_el0, %1\n"    // sp_el0: Set the user program stack pointer.
        "mov sp, %2\n"        // Set the stack pointer for the current EL.
        "eret\n"              // Return to EL0.
        :
        : "r"((unsigned long)user_space),
          "r"((unsigned long)user_stack + USTACK_SIZE),
          "r"((unsigned long)kernel_stack + KSTACK_SIZE)
        : "memory");
  }
  buddy_system_free((unsigned long)user_space);
  buddy_system_free((unsigned long)user_stack);
  buddy_system_free((unsigned long)kernel_stack);
  user_space = NULL;
  user_stack = NULL;
  kernel_stack = NULL;
  back_to_shell = 0;
}

void back_to_kernel(char *arg) {
  uart_sendline("%s", arg);
  back_to_shell = 1;
}
