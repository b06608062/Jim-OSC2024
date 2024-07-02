#ifndef TMPFS_H
#define TMPFS_H

#include "types.h"
#include "vfs.h"

#define FILE_NAME_MAX 15
#define MAX_DIR_ENTRY 16
#define MAX_FILE_SIZE 4096

typedef struct tmpfs_inode {
  node_type_t type;
  char name[FILE_NAME_MAX + 1];
  vnode_t *entry[MAX_DIR_ENTRY + 1];
  char *data;
  size_t datasize;
} tmpfs_inode_t;

int register_tmpfs();
int tmpfs_setup_mount(filesystem_t *fs, mount_t *_mount);
vnode_t *tmpfs_create_vnode(mount_t *_mount, node_type_t type);

int tmpfs_write(file_t *file, const void *buf, size_t len);
int tmpfs_read(file_t *file, void *buf, size_t len);
int tmpfs_open(vnode_t *file_node, file_t **target);
int tmpfs_close(file_t *file);
long tmpfs_getsize(vnode_t *vd);

int tmpfs_lookup(vnode_t *dir_node, vnode_t **target,
                 const char *component_name);
int tmpfs_create(vnode_t *dir_node, vnode_t **target,
                 const char *component_name);
int tmpfs_mkdir(vnode_t *dir_node, vnode_t **target,
                const char *component_name);

#endif /* TMPFS_H */
