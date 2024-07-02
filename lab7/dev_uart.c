#include "include/dev_uart.h"
#include "include/allocator.h"
#include "include/uart.h"
#include "include/vfs.h"

file_operations_t dev_file_operations = {dev_uart_write,  dev_uart_read,
                                         dev_uart_open,   dev_uart_close,
                                         (void *)op_deny, (void *)op_deny};

int init_dev_uart() { return register_dev(&dev_file_operations); }

int dev_uart_write(file_t *file, const void *buf, size_t len) {
  const char *cbuf = buf;
  for (int i = 0; i < len; i++)
    uart_async_putc(cbuf[i]);
  return len;
}

int dev_uart_read(file_t *file, void *buf, size_t len) {
  char *cbuf = buf;
  for (int i = 0; i < len; i++)
    cbuf[i] = uart_async_getc();
  return len;
}

int dev_uart_open(vnode_t *file_node, file_t **target) {
  (*target)->vnode = file_node;
  (*target)->f_ops = file_node->f_ops;
  return 0;
}

int dev_uart_close(file_t *file) {
  memory_pool_free(file, 0);
  return 0;
}