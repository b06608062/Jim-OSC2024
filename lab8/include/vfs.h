#ifndef VFS_H
#define VFS_H

#include "types.h"

#define MAX_PATH_NAME 255
#define O_CREAT 00000100
#define SEEK_SET 0

typedef enum { DIR, FILE } node_type_t;
typedef enum { TMP, INITRAM, FAT32 } vnode_type_t;

typedef struct vnode {
  struct mount *mount;
  struct vnode_operations *v_ops;
  struct file_operations *f_ops;
  vnode_type_t type;
  void *internal;
} vnode_t;

typedef struct file {
  struct vnode *vnode;
  size_t f_pos;
  struct file_operations *f_ops;
  int flags;
} file_t;

typedef struct mount {
  struct vnode *root;
  struct filesystem *fs;
} mount_t;

typedef struct filesystem {
  const char *name;
  int (*setup_mount)(struct filesystem *fs, struct mount *mount);
  int (*syncfs)();
} filesystem_t;

typedef struct file_operations {
  int (*write)(struct file *file, const void *buf, size_t len);
  int (*read)(struct file *file, void *buf, size_t len);
  int (*open)(struct vnode *file_node, struct file **target);
  int (*close)(struct file *file);
  long (*lseek64)(struct file *file, long offset, int whence);
  long (*getsize)(struct vnode *vd);
} file_operations_t;

typedef struct vnode_operations {
  int (*lookup)(struct vnode *dir_node, struct vnode **target,
                const char *component_name);
  int (*create)(struct vnode *dir_node, struct vnode **target,
                const char *component_name);
  int (*mkdir)(struct vnode *dir_node, struct vnode **target,
               const char *component_name);
} vnode_operations_t;

int register_filesystem(filesystem_t *fs);
int register_dev(file_operations_t *fo);
filesystem_t *find_filesystem(const char *fs_name);

int vfs_write(file_t *file, const void *buf, size_t len);
int vfs_read(file_t *file, void *buf, size_t len);
int vfs_open(const char *pathname, int flags, file_t **target);
int vfs_close(file_t *file);
long vfs_lseek64(file_t *file, long offset, int whence);

int vfs_lookup(const char *pathname, vnode_t **target);
int vfs_create(const char *pathname);
int vfs_mkdir(const char *pathname);

int vfs_mount(const char *target, const char *filesystem);
int vfs_mknod(char *pathname, int id);

#define MAX_FS_REG 0x50
#define MAX_DEV_REG 0x10

void init_rootfs();
char *path_to_absolute(char *path, char *cwd);
int op_deny();

void parse_directory_structure(vnode_t *node, int level);
void parse_rootfs();

#endif /* VFS_H */
