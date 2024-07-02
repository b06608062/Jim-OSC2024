#include "include/initramfs.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/cpio.h"
#include "include/heap.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"
#include "include/vfs.h"

extern cpio_newc_header_t *cpio_header;

file_operations_t initramfs_file_operations = {
    initramfs_write, initramfs_read, initramfs_open,
    initramfs_close, vfs_lseek64,    initramfs_getsize};
vnode_operations_t initramfs_vnode_operations = {
    initramfs_lookup, initramfs_create, initramfs_mkdir};

int register_initramfs() {
  filesystem_t fs;
  fs.name = "initramfs";
  fs.setup_mount = initramfs_setup_mount;
  fs.syncfs = initramfs_sync;
  return register_filesystem(&fs);
}

int initramfs_setup_mount(filesystem_t *fs, mount_t *_mount) {
  _mount->fs = fs;
  _mount->root = initramfs_create_vnode(0, DIR);
  initramfs_inode_t *ramdir_inode = _mount->root->internal;
  int child_idx = 0;
  cpio_newc_header_t *header = (cpio_newc_header_t *)cpio_header;
  while (header != NULL) {
    if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, sizeof(header->c_magic)) !=
        0) {
      uart_sendline("[CPIO Error] Invalid CPIO header magic number.\n");
      return -1;
    }
    unsigned long filesize = cpio_hexstr_to_ulong(header->c_filesize, 8);
    unsigned long namesize = cpio_hexstr_to_ulong(header->c_namesize, 8);
    char *pathname = (char *)(header + 1);
    char *filedata = pathname + namesize;
    filedata += ((4 - ((unsigned long)filedata & 3)) & 3);
    if (strncmp(pathname, CPIO_NEWC_TRAILER, strlen(CPIO_NEWC_TRAILER)) == 0) {
      break;
    }
    if (!cpio_is_directory(header->c_mode)) {
      vnode_t *filevnode = initramfs_create_vnode(0, FILE);
      initramfs_inode_t *fileinode = filevnode->internal;
      fileinode->name = pathname;
      fileinode->data = filedata;
      fileinode->datasize = filesize;
      ramdir_inode->entry[child_idx++] = filevnode;
    }
    header = (cpio_newc_header_t *)(filedata + filesize);
    header = (cpio_newc_header_t *)((unsigned long)header +
                                    ((4 - ((unsigned long)header & 3)) & 3));
  }
  return 0;
}

int initramfs_sync() { return 0; }

vnode_t *initramfs_create_vnode(mount_t *_mount, node_type_t type) {
  vnode_t *v = memory_pool_allocator(sizeof(vnode_t), 0);
  v->mount = _mount;
  v->v_ops = &initramfs_vnode_operations;
  v->f_ops = &initramfs_file_operations;
  v->type = INITRAM;
  initramfs_inode_t *inode =
      memory_pool_allocator(sizeof(initramfs_inode_t), 0);
  simple_memset(inode, 0, sizeof(initramfs_inode_t));
  inode->type = type;
  v->internal = inode;
  return v;
}

int initramfs_write(file_t *file, const void *buf, size_t len) {
  uart_sendline("[initramfs_write] Cannot write to initramfs.\n");
  return -1;
}

int initramfs_read(file_t *file, void *buf, size_t len) {
  initramfs_inode_t *inode = file->vnode->internal;
  if (file->f_pos + len > inode->datasize) {
    len = inode->datasize - file->f_pos;
    memcpy(buf, inode->data + file->f_pos, inode->datasize - file->f_pos);
    file->f_pos += inode->datasize - file->f_pos;
    return inode->datasize - file->f_pos;
  } else {
    memcpy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;
    return len;
  }
  return -1;
}

int initramfs_open(vnode_t *file_node, file_t **target) {
  (*target)->vnode = file_node;
  (*target)->f_pos = 0;
  (*target)->f_ops = file_node->f_ops;
  return 0;
}

int initramfs_close(file_t *file) {
  memory_pool_free(file, 0);
  return 0;
}

long initramfs_getsize(vnode_t *vd) {
  initramfs_inode_t *inode = vd->internal;
  return inode->datasize;
}

int initramfs_lookup(vnode_t *dir_node, vnode_t **target,
                     const char *component_name) {
  initramfs_inode_t *dir_inode = dir_node->internal;
  if (dir_inode->type != DIR) {
    uart_sendline("[initramfs_lookup] Not a directory.\n");
    return -1;
  }
  int child_idx = 0;
  for (; child_idx < INITRAMFS_MAX_DIR_ENTRY; child_idx++) {
    vnode_t *vnode = dir_inode->entry[child_idx];
    if (!vnode) {
      break;
    }
    initramfs_inode_t *inode = vnode->internal;
    if (strcmp(component_name, inode->name) == 0) {
      *target = vnode;
      return 0;
    }
  }
  uart_sendline("[initramfs_lookup] Cannot find file.\n");
  return -1;
}

int initramfs_create(vnode_t *dir_node, vnode_t **target,
                     const char *component_name) {
  uart_sendline("[initramfs_create] Cannot create file in initramfs.\n");
  return -1;
}

int initramfs_mkdir(vnode_t *dir_node, vnode_t **target,
                    const char *component_name) {
  uart_sendline("[initramfs_mkdir] Cannot create directory in initramfs.\n");
  return -1;
}