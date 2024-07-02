#ifndef SIGNAL_H
#define SIGNAL_H

#include "syscall.h"

void signal_default_handler();
void check_signal(trapframe_t *tpf);
void run_signal(trapframe_t *tpf, int signal);
void signal_handler_wrapper();

#endif /* SIGNAL_H */
