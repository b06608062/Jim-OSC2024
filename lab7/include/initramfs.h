#ifndef INITRAMFS_H
#define INITRAMFS_H

#include "types.h"
#include "vfs.h"

#define INITRAMFS_MAX_DIR_ENTRY 100

typedef struct initramfs_inode {
  node_type_t type;
  char *name;
  vnode_t *entry[INITRAMFS_MAX_DIR_ENTRY];
  char *data;
  size_t datasize;
} initramfs_inode_t;

int register_initramfs();
int initramfs_setup_mount(filesystem_t *fs, mount_t *_mount);
vnode_t *initramfs_create_vnode(mount_t *_mount, node_type_t type);

int initramfs_write(file_t *file, const void *buf, size_t len);
int initramfs_read(file_t *file, void *buf, size_t len);
int initramfs_open(vnode_t *file_node, file_t **target);
int initramfs_close(file_t *file);
long initramfs_getsize(vnode_t *vd);

int initramfs_lookup(vnode_t *dir_node, vnode_t **target,
                     const char *component_name);
int initramfs_create(vnode_t *dir_node, vnode_t **target,
                     const char *component_name);
int initramfs_mkdir(vnode_t *dir_node, vnode_t **target,
                    const char *component_name);

#endif /* INITRAMFS_H */