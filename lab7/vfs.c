#include "include/vfs.h"
#include "include/allocator.h"
#include "include/dev_framebuffer.h"
#include "include/dev_uart.h"
#include "include/initramfs.h"
#include "include/tmpfs.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern mount_t *rootfs;
extern filesystem_t reg_fs[MAX_FS_REG];
extern file_operations_t reg_dev[MAX_DEV_REG];

int register_filesystem(filesystem_t *fs) {
  for (int i = 0; i < MAX_FS_REG; ++i) {
    if (!reg_fs[i].name) {
      reg_fs[i].name = fs->name;
      reg_fs[i].setup_mount = fs->setup_mount;
      return i;
    }
  }
  return -1;
}

int register_dev(file_operations_t *fo) {
  for (int i = 0; i < MAX_FS_REG; ++i) {
    if (!reg_dev[i].open) {
      reg_dev[i] = *fo;
      return i;
    }
  }
  return -1;
}

filesystem_t *find_filesystem(const char *fs_name) {
  for (int i = 0; i < MAX_FS_REG; ++i) {
    if (strcmp(reg_fs[i].name, fs_name) == 0) {
      return &reg_fs[i];
    }
  }
  return NULL;
}

int vfs_write(file_t *file, const void *buf, size_t len) {
  return file->f_ops->write(file, buf, len);
}

int vfs_read(file_t *file, void *buf, size_t len) {
  return file->f_ops->read(file, buf, len);
}

int vfs_open(const char *pathname, int flags, file_t **target) {
  vnode_t *node;
  if (vfs_lookup(pathname, &node) != 0 && (flags & O_CREAT)) {
    int last_slash_idx = 0;
    for (int i = 0; i < strlen(pathname); ++i) {
      if (pathname[i] == '/') {
        last_slash_idx = i;
      }
    }
    char dirname[MAX_PATH_NAME + 1];
    strcpy(dirname, pathname);
    dirname[last_slash_idx] = 0;
    if (vfs_lookup(dirname, &node) != 0) {
      return -1;
    }
    uart_sendline("[vfs_open] Create file...\n");
    if (node->v_ops->create(node, &node, pathname + last_slash_idx + 1) != 0) {
      return -1;
    }
    *target = memory_pool_allocator(sizeof(file_t), 0);
    node->f_ops->open(node, target);
    (*target)->flags = flags;
    return 0;
  } else {
    *target = memory_pool_allocator(sizeof(file_t), 0);
    node->f_ops->open(node, target);
    (*target)->flags = flags;
    return 0;
  }
  return -1;
}

int vfs_close(file_t *file) {
  file->f_ops->close(file);
  return 0;
}

long vfs_lseek64(file_t *file, long offset, int whence) {
  if (whence == SEEK_SET) {
    if (offset >= file->vnode->f_ops->getsize(file->vnode)) {
      uart_sendline("[vfs_lseek64] Offset exceeds file size\n");
      return -1;
    }
    file->f_pos = offset;
    return file->f_pos;
  }
  return -1;
}

int vfs_lookup(const char *pathname, vnode_t **target) {
  if (strlen(pathname) == 0) {
    *target = rootfs->root;
    return 0;
  }
  vnode_t *dirnode = rootfs->root;
  char component_name[FILE_NAME_MAX + 1];
  int c_idx = 0;
  for (int i = 1; i < strlen(pathname); ++i) {
    if (pathname[i] == '/') {
      component_name[c_idx] = 0;
      if (dirnode->v_ops->lookup(dirnode, &dirnode, component_name) != 0) {
        return -1;
      }
      while (dirnode->mount) {
        dirnode = dirnode->mount->root;
      }
      c_idx = 0;
    } else {
      component_name[c_idx++] = pathname[i];
    }
  }
  component_name[c_idx] = 0;
  if (dirnode->v_ops->lookup(dirnode, &dirnode, component_name) != 0) {
    return -1;
  }
  while (dirnode->mount) {
    dirnode = dirnode->mount->root;
  }
  *target = dirnode;
  return 0;
}

int vfs_create(const char *pathname) {
  vnode_t *dir_node;
  vnode_t *new_node;
  int last_slash_idx = 0;
  int len = strlen(pathname);
  for (int i = 0; i < len; ++i) {
    if (pathname[i] == '/') {
      last_slash_idx = i;
    }
  }
  char dirname[MAX_PATH_NAME + 1];
  memcpy(dirname, pathname, last_slash_idx);
  dirname[last_slash_idx] = '\0';
  if (vfs_lookup(dirname, &dir_node) != 0) {
    return -1;
  }
  char *filename = (char *)(pathname + last_slash_idx + 1);
  if (dir_node->v_ops->create(dir_node, &new_node, filename) != 0) {
    return -1;
  }
  return 0;
}

int vfs_mkdir(const char *pathname) {
  vnode_t *dir_node;
  vnode_t *new_node;
  int last_slash_idx = 0;
  int len = strlen(pathname);
  for (int i = 0; i < len; ++i) {
    if (pathname[i] == '/') {
      last_slash_idx = i;
    }
  }
  char dirname[MAX_PATH_NAME + 1];
  memcpy(dirname, pathname, last_slash_idx);
  dirname[last_slash_idx] = '\0';
  if (vfs_lookup(dirname, &dir_node) != 0) {
    return -1;
  }
  char *dirname_new = (char *)(pathname + last_slash_idx + 1);
  if (dir_node->v_ops->mkdir(dir_node, &new_node, dirname_new) != 0) {
    return -1;
  }
  return 0;
}

int vfs_mount(const char *target, const char *filesystem) {
  vnode_t *dirnode;
  filesystem_t *fs = find_filesystem(filesystem);
  if (!fs) {
    uart_sendline("[vfs_mount] Cannot find filesystem\n");
    return -1;
  }
  if (vfs_lookup(target, &dirnode) == -1) {
    return -1;
  } else {
    dirnode->mount = memory_pool_allocator(sizeof(mount_t), 0);
    fs->setup_mount(fs, dirnode->mount);
  }
  return 0;
}

// Virtual File System Make Node
int vfs_mknod(char *pathname, int id) {
  file_t *f = memory_pool_allocator(sizeof(file_t), 0);
  vfs_open(pathname, O_CREAT, &f);
  f->vnode->f_ops = &reg_dev[id];
  vfs_close(f);
  return 0;
}

void init_rootfs() {
  int idx = register_tmpfs();
  rootfs = memory_pool_allocator(sizeof(mount_t), 0);
  reg_fs[idx].setup_mount(&reg_fs[idx], rootfs);

  vfs_mkdir("/initramfs");
  register_initramfs();
  vfs_mount("/initramfs", "initramfs");

  vfs_mkdir("/dev");
  int uart_id = init_dev_uart();
  vfs_mknod("/dev/uart", uart_id);

  int framebuffer_id = init_dev_framebuffer();
  vfs_mknod("/dev/framebuffer", framebuffer_id);

  vfs_mkdir("/home");
  vfs_mkdir("/home/user");
  vfs_mkdir("/home/user/docs");
  vfs_create("/home/user/docs/file1");
  vfs_create("/home/user/docs/file2");
  vfs_create("/home/user/file3");
  vfs_create("/home/file4");
  parse_rootfs();
}

char *path_to_absolute(char *path, char *cwd) {
  if (path[0] != '/') {
    char tmp[MAX_PATH_NAME + 1];
    strcpy(tmp, cwd);
    if (strcmp(cwd, "/") != 0) {
      strcat(tmp, "/");
    }
    strcat(tmp, path);
    strcpy(path, tmp);
  }
  char absolute_path[MAX_PATH_NAME + 1] = {};
  int idx = 0;
  for (int i = 0; i < strlen(path); ++i) {
    if (path[i] == '/' && path[i + 1] == '.' && path[i + 2] == '.') {
      for (int j = idx; j >= 0; j--) {
        if (absolute_path[j] == '/') {
          absolute_path[j] = 0;
          idx = j;
        }
      }
      i += 2;
      continue;
    }
    if (path[i] == '/' && path[i + 1] == '.') {
      i++;
      continue;
    }
    absolute_path[idx++] = path[i];
  }
  absolute_path[idx] = 0;
  strcpy(path, absolute_path);
  return path;
}

int op_deny() { return -1; }

void parse_directory_structure(vnode_t *node, int level) {
  if (!node)
    return;
  for (int i = 0; i < level; ++i) {
    uart_sendline("|   ");
  }
  if (node->type == TMP) {
    tmpfs_inode_t *inode = (tmpfs_inode_t *)node->internal;
    uart_sendline("|-- %s", inode->name);
    if (inode->type == DIR) {
      uart_sendline(" {\n");
      if (node->mount && node->mount->root) {
        if (node->mount->root->type == TMP) {
          for (int i = 0; i <= MAX_DIR_ENTRY; ++i) {
            vnode_t *child =
                ((tmpfs_inode_t *)rootfs->root->internal)->entry[i];
            if (child) {
              parse_directory_structure(child, level + 1);
            }
          }
        } else {
          for (int i = 0; i < INITRAMFS_MAX_DIR_ENTRY; ++i) {
            vnode_t *child =
                ((initramfs_inode_t *)node->mount->root->internal)->entry[i];
            if (child) {
              parse_directory_structure(child, level + 1);
            }
          }
        }
      } else {
        for (int i = 0; i <= MAX_DIR_ENTRY; ++i) {
          vnode_t *child = inode->entry[i];
          if (child) {
            parse_directory_structure(child, level + 1);
          }
        }
      }
      for (int i = 0; i <= level; ++i) {
        uart_sendline("|   ");
      }
      uart_sendline("}\n");
    } else {
      uart_sendline("\n");
    }
  } else if (node->type == INITRAM) {
    initramfs_inode_t *inode = (initramfs_inode_t *)node->internal;
    uart_sendline("|-- %s", inode->name);
    if (inode->type == DIR) {
      uart_sendline(" {\n");
      for (int i = 0; i < INITRAMFS_MAX_DIR_ENTRY; ++i) {
        vnode_t *child = inode->entry[i];
        if (child) {
          parse_directory_structure(child, level + 1);
        }
      }
      for (int i = 0; i <= level; ++i) {
        uart_sendline("|   ");
      }
      uart_sendline("}\n");
    } else {
      uart_sendline("\n");
    }
  } else {
    uart_sendline("[parse_directory_structure] Unknown vnode type.\n");
  }
}

void parse_rootfs() {
  uart_sendline("/ {\n");
  for (int i = 0; i <= MAX_DIR_ENTRY; ++i) {
    vnode_t *child = ((tmpfs_inode_t *)rootfs->root->internal)->entry[i];
    if (child) {
      parse_directory_structure(child, 0);
    }
  }
  uart_sendline("}\n");
}