#include "include/dev_framebuffer.h"
#include "include/allocator.h"
#include "include/exception.h"
#include "include/mbox.h"
#include "include/uart.h"
#include "include/utils.h"
#include "include/vfs.h"

extern volatile unsigned int mbox[];
extern unsigned int width, height, pitch, isrgb;
extern void *lfb;

file_operations_t dev_framebuffer_operations = {
    dev_framebuffer_write, (void *)op_deny, dev_framebuffer_open,
    dev_framebuffer_close, vfs_lseek64,     (void *)op_deny};

int init_dev_framebuffer() {
  mbox[0] = 35 * 4;
  mbox[1] = MBOX_REQUEST;

  mbox[2] = 0x48003; // set phy wh
  mbox[3] = 8;
  mbox[4] = 8;
  mbox[5] = 1024; // FrameBufferInfo.width
  mbox[6] = 768;  // FrameBufferInfo.height

  mbox[7] = 0x48004; // set virt wh
  mbox[8] = 8;
  mbox[9] = 8;
  mbox[10] = 1024; // FrameBufferInfo.virtual_width
  mbox[11] = 768;  // FrameBufferInfo.virtual_height

  mbox[12] = 0x48009; // set virt offset
  mbox[13] = 8;
  mbox[14] = 8;
  mbox[15] = 0; // FrameBufferInfo.x_offset
  mbox[16] = 0; // FrameBufferInfo.y.offset

  mbox[17] = 0x48005; // set depth
  mbox[18] = 4;
  mbox[19] = 4;
  mbox[20] = 32; // FrameBufferInfo.depth

  mbox[21] = 0x48006; // set pixel order
  mbox[22] = 4;
  mbox[23] = 4;
  mbox[24] = 1; // RGB, not BGR preferably

  mbox[25] = 0x40001; // get framebuffer, gets alignment on request
  mbox[26] = 8;
  mbox[27] = 8;
  mbox[28] = 4096; // FrameBufferInfo.pointer
  mbox[29] = 0;    // FrameBufferInfo.size

  mbox[30] = 0x40008; // get pitch
  mbox[31] = 4;
  mbox[32] = 4;
  mbox[33] = 0; // FrameBufferInfo.pitch

  mbox[34] = MBOX_TAG_LAST;

  // this might not return exactly what we asked for, could be
  // the closest supported resolution instead
  if (mbox_call(MBOX_CH_PROP) && mbox[20] == 32 && mbox[28] != 0) {
    mbox[28] &= 0x3FFFFFFF; // convert GPU address to ARM address
    width = mbox[5];        // get actual physical width
    height = mbox[6];       // get actual physical height
    pitch = mbox[33];       // get number of bytes per line
    isrgb = mbox[24];       // get the actual channel order
    lfb = (void *)PHYS_TO_VIRT((unsigned long)mbox[28]);
    uart_sendline("Set screen resolution to %dx%d\n", width, height);
    uart_sendline("Pitch: %d bytes\n", pitch);
    uart_sendline("RGB? %s\n", isrgb ? "yes" : "no");
    uart_sendline("FrameBufferInfo.pointer: 0x%p\n", mbox[28]);
  } else {
    uart_sendline("Unable to set screen resolution to 1024x768x32\n");
  }

  return register_dev(&dev_framebuffer_operations);
}

int dev_framebuffer_write(file_t *file, const void *buf, size_t len) {
  lock();
  if (file->f_pos + len > pitch * height) {
    len = pitch * height - file->f_pos;
  }
  memcpy(lfb + file->f_pos, buf, len);
  file->f_pos += len;
  unlock();
  return len;
}

int dev_framebuffer_open(vnode_t *file_node, file_t **target) {
  (*target)->vnode = file_node;
  (*target)->f_pos = 0;
  (*target)->f_ops = &dev_framebuffer_operations;
  return 0;
}

int dev_framebuffer_close(file_t *file) {
  memory_pool_free(file, 0);
  return 0;
}