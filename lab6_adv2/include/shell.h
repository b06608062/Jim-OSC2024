#ifndef SHELL_H
#define SHELL_H

#define MAX_CMD_LEN 256

#include "types.h"

typedef struct kernel_context {
  uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15,
      x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29, x30;
  uint64_t spsr_el1, elr_el1, sp;
} kernel_context_t;

extern void save_kernel_context(void *kernel_context);
extern void load_kernel_context(void *kernel_context);

void show_banner();
void shell_run();
void format_command(const char *command, const char *description);
void do_cmd_help();
void do_cmd_hello();
void do_cmd_info();
void do_cmd_clear();
void do_cmd_reboot();
void do_cmd_cancel();
void do_cmd_ls();
void do_cmd_cat(const char *filename);
void do_cmd_simple_malloc();
void do_cmd_dtb();
void do_cmd_setTimeout(const char *msg, int secs, int priority);
void do_cmd_preempt();
void start_preemption_test(char *arg);
void stop_preemption_test(char *arg);
void do_cmd_freelist(int show_bitmap);
void do_cmd_malloc(unsigned int size);
void do_cmd_free(unsigned long addr);
void do_cmd_sfree(unsigned long addr);
void do_cmd_exec(const char *progname);
void do_cmd_thread();
void do_cmd_exec_test();
void back_to_kernel(char *arg);

#endif /* SHELL_H */
