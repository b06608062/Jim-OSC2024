#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
typedef struct trapframe {
  uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15,
      x16, x17, x18, x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29, x30;
  uint64_t spsr_el1, elr_el1, sp_el0;
} trapframe_t;

int getpid(trapframe_t *tpf);
size_t uartread(trapframe_t *tpf, char buf[], size_t size);
size_t uartwrite(trapframe_t *tpf, const char buf[], size_t size);
int exec(trapframe_t *tpf, const char *name, char *const argv[]);
int fork(trapframe_t *tpf);
int syscall_mbox_call(trapframe_t *tpf, uint8_t ch, uint32_t *mbox_user);
void exit(trapframe_t *tpf, int status);
void kill(trapframe_t *tpf, int pid);
void signal_register(int signal, void (*handler)());
void signal_kill(int pid, int signal);
void signal_return(trapframe_t *tpf);
void *mmap(trapframe_t *tpf, void *addr, size_t len, int prot, int flags,
           int fd, int file_offset);
int sys_open(trapframe_t *tpf, const char *pathname, int flags);
int sys_close(trapframe_t *tpf, int fd);
long sys_write(trapframe_t *tpf, int fd, const void *buf, size_t count);
long sys_read(trapframe_t *tpf, int fd, void *buf, size_t count);
int sys_mkdir(trapframe_t *tpf, const char *pathname, uint32_t mode);
int sys_mount(trapframe_t *tpf, const char *src, const char *target,
              const char *filesystem, size_t flags, const void *data);
int sys_chdir(trapframe_t *tpf, const char *path);
long sys_lseek64(trapframe_t *tpf, int fd, long offset, int whence);
int sys_ioctl(trapframe_t *tpf, int fb, unsigned long request, void *info);

#endif /* SYSCALL_H */