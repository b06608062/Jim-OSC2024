#ifndef DEV_FRAMEBUFFER_H
#define DEV_FRAMEBUFFER_H

#include "types.h"
#include "vfs.h"

typedef struct framebuffer_info {
  unsigned int width;
  unsigned int height;
  unsigned int pitch;
  unsigned int isrgb;
} framebuffer_info_t;

int init_dev_framebuffer();

int dev_framebuffer_write(file_t *file, const void *buf, size_t len);
int dev_framebuffer_open(vnode_t *file_node, file_t **target);
int dev_framebuffer_close(file_t *file);

#endif /* DEV_FRAMEBUFFER_H */