#include "include/syscall.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/dev_framebuffer.h"
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
extern frame_array_node_t frame_array[];
extern unsigned int width, height, pitch, isrgb;
extern uint32_t thread_count;

int getpid(trapframe_t *tpf) {
  tpf->x0 = current_thread->pid;
  return current_thread->pid;
}

size_t uartread(trapframe_t *tpf, char buf[], size_t size) {
  int i = 0;
  // uart_sendline("rx &buf: %p\n", buf);
  for (; i < size; ++i) {
    buf[i] = uart_async_getc();
  }
  tpf->x0 = i;
  return i;
}

size_t uartwrite(trapframe_t *tpf, const char buf[], size_t size) {
  int i = 0;
  // uart_sendline("tx &buf: %p\n", buf);
  for (; i < size; ++i) {
    uart_async_putc(buf[i]);
  }
  tpf->x0 = i;
  return i;
}

int exec(trapframe_t *tpf, const char *name, char *const argv[]) {
  uart_sendline("exec: name = %s\n", name);
  mmu_del_vma(current_thread);
  double_linked_init(&current_thread->vma_list);

  // reset file descriptor
  strcpy(current_thread->cwd, "/");
  for (int i = 0; i <= MAX_FD; ++i) {
    if (current_thread->fdt[i]) {
      vfs_close(current_thread->fdt[i]);
      current_thread->fdt[i] = NULL;
    }
  }

  vfs_open("/dev/uart", 0, &current_thread->fdt[0]);
  vfs_open("/dev/uart", 0, &current_thread->fdt[1]);
  vfs_open("/dev/uart", 0, &current_thread->fdt[2]);

  char abs_path[MAX_PATH_NAME];
  strcpy(abs_path, name);
  path_to_absolute(abs_path, current_thread->cwd);
  uart_sendline("exec: abs_path = %s\n", abs_path);
  vnode_t *target_file;
  vfs_lookup(abs_path, &target_file);
  current_thread->user_data_size = target_file->f_ops->getsize(target_file);
  // current_thread->user_data_size = cpio_get_file_size(name);
  // char *new_data = cpio_get_file_data(name);

  asm("dsb ish\n"); // ensure write has completed
  mmu_free_page_tables(current_thread->context.pgd, 0);
  simple_memset(PHYS_TO_VIRT((char *)(current_thread->context.pgd)), 0, 0x1000);
  asm("tlbi vmalle1is\n" // invalidate all TLB entries
      "dsb ish\n"        // ensure completion of TLB invalidatation
      "isb\n");          // clear pipeline

  file_t *f;
  vfs_open(abs_path, 0, &f);
  for (int i = 0; i <= current_thread->user_data_size / PAGE_SIZE; ++i) {
    uint64_t new_page = buddy_system_allocator(PAGE_SIZE);
    mmu_add_vma(current_thread, USER_SPACE + i * PAGE_SIZE, PAGE_SIZE,
                VIRT_TO_PHYS(new_page), 0b111, 1);
    // memcpy((char *)new_page, new_data + i * PAGE_SIZE, PAGE_SIZE);
    vfs_read(f, (char *)new_page, PAGE_SIZE);
    frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
  }
  vfs_close(f);

  for (int i = 0; i < USTACK_SIZE / PAGE_SIZE; ++i) {
    uint64_t new_page = buddy_system_allocator(PAGE_SIZE);
    mmu_add_vma(current_thread, USER_STACK_BASE - USTACK_SIZE + i * PAGE_SIZE,
                PAGE_SIZE, VIRT_TO_PHYS(new_page), 0b111, 1);
    frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
  }
  mmu_add_vma(current_thread, PERIPHERAL_START,
              PERIPHERAL_END - PERIPHERAL_START, PERIPHERAL_START, 0b011, 0);
  mmu_add_vma(current_thread, USER_SIGNAL_WRAPPER_VA, 0x2000,
              (size_t)VIRT_TO_PHYS(signal_handler_wrapper), 0b101, 0);

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
  thread_t *child_thread = thread_create(NULL, current_thread->user_data_size);
  double_linked_node_t *cur;
  vm_area_struct_t *vma;
  double_linked_for_each(cur, &current_thread->vma_list) {
    vma = (vm_area_struct_t *)cur;
    if (vma->virt_addr == USER_SIGNAL_WRAPPER_VA ||
        vma->virt_addr == PERIPHERAL_START) {
      continue;
    }
    mmu_add_vma(child_thread, vma->virt_addr, vma->area_size, vma->phys_addr,
                vma->rwx, 1);
    size_t flag = 0;
    if (!(vma->rwx & (0b1 << 2)))
      flag |= PD_UNX; // 4: executable
    if (vma->rwx & (0b1 << 0))
      flag |= PD_UK_ACCESS; // 1: readable / accessible
    flag |= PD_RDONLY;
    for (int i = 0; i < vma->area_size / PAGE_SIZE; ++i) {
      frame_array[vma->phys_addr / PAGE_SIZE + i].ref++;
      map_one_page((size_t *)PHYS_TO_VIRT(current_thread->context.pgd),
                   vma->virt_addr + i * PAGE_SIZE,
                   vma->phys_addr + i * PAGE_SIZE, flag);
      map_one_page((size_t *)(child_thread->context.pgd),
                   vma->virt_addr + i * PAGE_SIZE,
                   vma->phys_addr + i * PAGE_SIZE, flag);
    }
  }
  mmu_add_vma(child_thread, PERIPHERAL_START, PERIPHERAL_END - PERIPHERAL_START,
              PERIPHERAL_START, 0b011, 0);
  mmu_add_vma(child_thread, USER_SIGNAL_WRAPPER_VA, 0x2000,
              (size_t)VIRT_TO_PHYS(signal_handler_wrapper), 0b101, 0);
  int parent_pid = current_thread->pid;
  uint64_t kernel_stack_offset = (uint64_t)child_thread->kernel_stack -
                                 (uint64_t)current_thread->kernel_stack;

  // copy file handle
  for (int i = 0; i <= MAX_FD; ++i) {
    if (current_thread->fdt[i]) {
      child_thread->fdt[i] = memory_pool_allocator(sizeof(file_t), 0);
      *child_thread->fdt[i] = *current_thread->fdt[i];
    }
  }

  // copy signal handler into new process
  for (int i = 0; i <= SIGNAL_MAX; ++i) {
    // uart_sendline("current_thread->signal_handler[%d] = %p\n", i,
    //               current_thread->signal_handler[i]);
    child_thread->signal_handler[i] = current_thread->signal_handler[i];
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
  uart_sendline("mbox_user: 0x%p\n", mbox_user);
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
  uart_sendline("mmap: addr = 0x%p, len = %l, prot = %d, flags = %d, fd = %d, "
                "file_offset = %d\n",
                addr, len, prot, flags, fd, file_offset);
  // Ignore flags as we have demand pages

  // Req #3 Page size round up
  len = len % 0x1000 ? len + (0x1000 - len % 0x1000) : len;
  addr = (uint64_t)addr % 0x1000 ? addr + (0x1000 - (uint64_t)addr % 0x1000)
                                 : addr;
  // Req #2 check if overlap
  double_linked_node_t *cur;
  vm_area_struct_t *the_area_ptr = NULL;
  double_linked_for_each(cur, &current_thread->vma_list) {
    vm_area_struct_t *vma = (vm_area_struct_t *)cur;
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
  uint64_t new_page = buddy_system_allocator(len);
  mmu_add_vma(current_thread, (uint64_t)addr, len, VIRT_TO_PHYS(new_page), prot,
              1);
  for (int i = 0; i < len / PAGE_SIZE; ++i) {
    frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
  }
  uart_sendline("mmap: return addr = 0x%p\n", addr);
  tpf->x0 = (uint64_t)addr;
  return (void *)tpf->x0;
}

int sys_open(trapframe_t *tpf, const char *pathname, int flags) {
  uart_sendline("sys_open: pathname = %s, flags = %d\n", pathname, flags);
  char abs_path[MAX_PATH_NAME + 1];
  strcpy(abs_path, pathname);
  path_to_absolute(abs_path, current_thread->cwd);
  uart_sendline("sys_open: abs_path = %s\n", abs_path);
  for (int i = 0; i <= MAX_FD; ++i) {
    if (!current_thread->fdt[i]) {
      if (vfs_open(abs_path, flags, &current_thread->fdt[i]) != 0) {
        break;
      }
      tpf->x0 = i;
      return i;
    }
  }
  tpf->x0 = -1;
  return -1;
}

int sys_close(trapframe_t *tpf, int fd) {
  uart_sendline("sys_close: fd = %d\n", fd);
  if (current_thread->fdt[fd]) {
    vfs_close(current_thread->fdt[fd]);
    current_thread->fdt[fd] = NULL;
    tpf->x0 = 0;
    return 0;
  }
  tpf->x0 = -1;
  return -1;
}

long sys_write(trapframe_t *tpf, int fd, const void *buf, size_t count) {
  if (thread_count <= 2) {
    uart_sendline("sys_write: fd = %d, buf = %s, count = %d\n", fd, buf, count);
  }
  if (current_thread->fdt[fd]) {
    tpf->x0 = vfs_write(current_thread->fdt[fd], buf, count);
    return tpf->x0;
  }
  tpf->x0 = -1;
  return tpf->x0;
}

long sys_read(trapframe_t *tpf, int fd, void *buf, size_t count) {
  if (current_thread->fdt[fd]) {
    tpf->x0 = vfs_read(current_thread->fdt[fd], buf, count);
    uart_sendline("sys_read: fd = %d, buf = %s, count = %d\n", fd, buf, count);
    return tpf->x0;
  }
  tpf->x0 = -1;
  return tpf->x0;
}

int sys_mkdir(trapframe_t *tpf, const char *pathname, uint32_t mode) {
  uart_sendline("sys_mkdir: pathname = %s, mode = %d\n", pathname, mode);
  char abs_path[MAX_PATH_NAME + 1];
  strcpy(abs_path, pathname);
  path_to_absolute(abs_path, current_thread->cwd);
  uart_sendline("sys_mkdir: abs_path = %s\n", abs_path);
  tpf->x0 = vfs_mkdir(abs_path);
  return tpf->x0;
}

int sys_mount(trapframe_t *tpf, const char *src, const char *target,
              const char *filesystem, size_t flags, const void *data) {
  uart_sendline("sys_mount: src = %s, target = %s, filesystem = %s, \n", src,
                target, filesystem);
  char abs_path[MAX_PATH_NAME + 1];
  strcpy(abs_path, target);
  path_to_absolute(abs_path, current_thread->cwd);
  uart_sendline("sys_mount: abs_path = %s\n", abs_path);
  tpf->x0 = vfs_mount(abs_path, filesystem);
  return tpf->x0;
}

int sys_chdir(trapframe_t *tpf, const char *path) {
  uart_sendline("sys_chdir: path = %s\n", path);
  char abs_path[MAX_PATH_NAME + 1];
  strcpy(abs_path, path);
  path_to_absolute(abs_path, current_thread->cwd);
  uart_sendline("sys_chdir: abs_path = %s\n", abs_path);
  strcpy(current_thread->cwd, abs_path);
  return 0;
}

long sys_lseek64(trapframe_t *tpf, int fd, long offset, int whence) {
  if (thread_count <= 2) {
    uart_sendline("sys_lseek64: fd = %d, offset = %l, whence = %d\n", fd,
                  offset, whence);
  }
  tpf->x0 = vfs_lseek64(current_thread->fdt[fd], offset, whence);
  return tpf->x0;
}

int sys_ioctl(trapframe_t *tpf, int fb, unsigned long request, void *info) {
  if (request == 0) {
    framebuffer_info_t *fb_info = info;
    fb_info->height = height;
    fb_info->isrgb = isrgb;
    fb_info->pitch = pitch;
    fb_info->width = width;
  }
  tpf->x0 = 0;
  return tpf->x0;
}