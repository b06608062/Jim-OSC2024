#ifndef DEV_UART_H
#define DEV_UART_H

#include "types.h"
#include "vfs.h"

int init_dev_uart();

int dev_uart_write(file_t *file, const void *buf, size_t len);
int dev_uart_read(file_t *file, void *buf, size_t len);
int dev_uart_open(vnode_t *file_node, file_t **target);
int dev_uart_close(file_t *file);

#endif