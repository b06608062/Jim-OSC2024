#include "include/tmpfs.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/heap.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"
#include "include/vfs.h"

file_operations_t tmpfs_file_operations = {tmpfs_write, tmpfs_read,
                                           tmpfs_open,  tmpfs_close,
                                           vfs_lseek64, tmpfs_getsize};
vnode_operations_t tmpfs_vnode_operations = {tmpfs_lookup, tmpfs_create,
                                             tmpfs_mkdir};

int register_tmpfs() {
  filesystem_t fs;
  fs.name = "tmpfs";
  fs.setup_mount = tmpfs_setup_mount;
  return register_filesystem(&fs);
}

int tmpfs_setup_mount(filesystem_t *fs, mount_t *_mount) {
  _mount->fs = fs;
  _mount->root = tmpfs_create_vnode(0, DIR);
  return 0;
}

vnode_t *tmpfs_create_vnode(mount_t *_mount, node_type_t type) {
  vnode_t *v = memory_pool_allocator(sizeof(vnode_t), 0);
  v->mount = _mount;
  v->v_ops = &tmpfs_vnode_operations;
  v->f_ops = &tmpfs_file_operations;
  v->type = TMP;
  tmpfs_inode_t *inode = memory_pool_allocator(sizeof(tmpfs_inode_t), 0);
  simple_memset(inode, 0, sizeof(tmpfs_inode_t));
  inode->type = type;
  inode->data = (char *)buddy_system_allocator(0x1000);
  simple_memset(inode->data, 0, 0x1000);
  inode->datasize = 0;
  v->internal = inode;
  return v;
}

int tmpfs_write(file_t *file, const void *buf, size_t len) {
  tmpfs_inode_t *inode = file->vnode->internal;
  memcpy(inode->data + file->f_pos, buf, len);
  file->f_pos += len;
  if (file->f_pos > inode->datasize) {
    inode->datasize = file->f_pos;
  }
  return len;
}

int tmpfs_read(file_t *file, void *buf, size_t len) {
  tmpfs_inode_t *inode = file->vnode->internal;
  if (file->f_pos + len > inode->datasize) {
    len = inode->datasize - file->f_pos;
    memcpy(buf, inode->data + file->f_pos, len);
    file->f_pos += inode->datasize - file->f_pos;
    return len;
  } else {
    memcpy(buf, inode->data + file->f_pos, len);
    file->f_pos += len;
    return len;
  }
  return -1;
}

int tmpfs_open(vnode_t *file_node, file_t **target) {
  (*target)->vnode = file_node;
  (*target)->f_pos = 0;
  (*target)->f_ops = file_node->f_ops;
  return 0;
}

int tmpfs_close(file_t *file) {
  memory_pool_free(file, 0);
  return 0;
}

long tmpfs_getsize(vnode_t *vd) {
  tmpfs_inode_t *inode = vd->internal;
  return inode->datasize;
}

int tmpfs_lookup(vnode_t *dir_node, vnode_t **target,
                 const char *component_name) {
  tmpfs_inode_t *dir_inode = dir_node->internal;
  if (dir_inode->type != DIR) {
    uart_sendline("[tmpfs_lookup] Not a directory.\n");
    return -1;
  }
  int child_idx = 0;
  for (; child_idx <= MAX_DIR_ENTRY; child_idx++) {
    vnode_t *vnode = dir_inode->entry[child_idx];
    if (!vnode) {
      break;
    }
    tmpfs_inode_t *inode = vnode->internal;
    if (strcmp(component_name, inode->name) == 0) {
      *target = vnode;
      return 0;
    }
  }
  uart_sendline("[tmpfs_lookup] Cannot find file.\n");
  return -1;
}

int tmpfs_create(vnode_t *dir_node, vnode_t **target,
                 const char *component_name) {
  if (strlen(component_name) > FILE_NAME_MAX) {
    uart_sendline("[tmpfs_create] File name too long.\n");
    return -1;
  }
  tmpfs_inode_t *inode = dir_node->internal;
  if (inode->type != DIR) {
    uart_sendline("[tmpfs_create] Not a directory.\n");
    return -1;
  }
  int child_idx = 0;
  for (; child_idx <= MAX_DIR_ENTRY; child_idx++) {
    if (!inode->entry[child_idx]) {
      break;
    }
    tmpfs_inode_t *child_inode = inode->entry[child_idx]->internal;
    if (strcmp(child_inode->name, component_name) == 0 &&
        child_inode->type == FILE) {
      uart_sendline("[tmpfs_create] File already exists.\n");
      return -1;
    }
  }
  if (child_idx > MAX_DIR_ENTRY) {
    uart_sendline("[tmpfs_create] Directory is full.\n");
    return -1;
  }
  vnode_t *_vnode = tmpfs_create_vnode(0, FILE);
  inode->entry[child_idx] = _vnode;
  tmpfs_inode_t *newinode = _vnode->internal;
  strcpy(newinode->name, component_name);
  *target = _vnode;
  return 0;
}

int tmpfs_mkdir(vnode_t *dir_node, vnode_t **target,
                const char *component_name) {
  if (strlen(component_name) > FILE_NAME_MAX) {
    uart_sendline("[tmpfs_mkdir] Directory name too long.\n");
    return -1;
  }
  tmpfs_inode_t *inode = dir_node->internal;
  if (inode->type != DIR) {
    uart_sendline("[tmpfs_mkdir] Not a directory.\n");
    return -1;
  }
  int child_idx = 0;
  for (; child_idx <= MAX_DIR_ENTRY; child_idx++) {
    if (!inode->entry[child_idx]) {
      break;
    }
    tmpfs_inode_t *child_inode = inode->entry[child_idx]->internal;
    if (strcmp(child_inode->name, component_name) == 0 &&
        child_inode->type == DIR) {
      uart_sendline("[tmpfs_mkdir] Directory already exists.\n");
      return -1;
    }
  }
  if (child_idx > MAX_DIR_ENTRY) {
    uart_sendline("[tmpfs_mkdir] Directory is full.\n");
    return -1;
  }
  vnode_t *_vnode = tmpfs_create_vnode(0, DIR);
  inode->entry[child_idx] = _vnode;
  tmpfs_inode_t *newinode = _vnode->internal;
  strcpy(newinode->name, component_name);
  *target = _vnode;
  return 0;
}