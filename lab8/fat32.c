#include "include/fat32.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/heap.h"
#include "include/sdhost.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"
#include "include/vfs.h"

extern fat32_metadata_t *fat32_md;
extern double_linked_node_t *fat32_cache_list_head;

file_operations_t fat32_file_operations = {fat32fs_write, fat32fs_read,
                                           fat32fs_open,  fat32fs_close,
                                           vfs_lseek64,   fat32fs_getsize};
vnode_operations_t fat32_vnode_operations = {fat32fs_lookup, fat32fs_create,
                                             fat32fs_mkdir};

void fat32fs_cache_init() {
  fat32_cache_list_head = simple_malloc(sizeof(double_linked_node_t), 0);
  double_linked_init(fat32_cache_list_head);
}

void fat32fs_cache_list_push(uint32_t block_idx, void *buf,
                             uint8_t dirty_flag) {
  fat32_cache_block_t *node =
      memory_pool_allocator(sizeof(fat32_cache_block_t), 0);
  node->block_idx = block_idx;
  memcpy((void *)node->block, buf, BLOCK_SIZE);
  node->dirty_flag = dirty_flag;
  double_linked_add_after((double_linked_node_t *)node, fat32_cache_list_head);
}

fat32_cache_block_t *fat32fs_cache_list_find(uint32_t block_idx) {
  double_linked_node_t *cur;
  double_linked_for_each(cur, fat32_cache_list_head) {
    fat32_cache_block_t *node = (fat32_cache_block_t *)cur;
    if (node->block_idx == block_idx) {
      return node;
    }
  }
  return NULL;
}

void fat32fs_readblock(uint32_t block_idx, void *buf) {
  fat32_cache_block_t *cache_node = fat32fs_cache_list_find(block_idx);
  if (cache_node) {
    memcpy(buf, (void *)cache_node->block, BLOCK_SIZE);
  } else {
    readblock(block_idx, buf);
    fat32fs_cache_list_push(block_idx, buf, 0);
  }
}

void fat32fs_writeblock(uint32_t block_idx, void *buf) {
  fat32_cache_block_t *cache_node = fat32fs_cache_list_find(block_idx);
  if (cache_node) {
    cache_node->dirty_flag = 1;
    memcpy((void *)cache_node->block, buf, BLOCK_SIZE);
  } else {
    fat32fs_cache_list_push(block_idx, buf, 1);
  }
}

uint32_t fat32fs_get_first_cluster(fat32_dirent_sfn_t *dirent) {
  return ((uint32_t)dirent->firstClusterHigh << 16) | dirent->firstClusterLow;
}

uint32_t fat32fs_clusteridx_2_datablockidx(uint32_t cluster_idx) {
  return fat32_md->data_region_block_idx +
         (cluster_idx - fat32_md->root_dir_cluster_idx) *
             fat32_md->n_sectors_per_cluster;
}

uint32_t fat32fs_clusteridx_2_fatblockidx(uint32_t cluster_idx) {
  return fat32_md->fat_region_block_idx + (cluster_idx / N_ENTRY_PER_FAT);
}

uint32_t fat32fs_get_free_fat_entry() {
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  int found_cluster_idx = -1;
  for (uint32_t i = 0; found_cluster_idx == -1; i += N_ENTRY_PER_FAT) {
    fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(i), (void *)fat_buf);
    for (uint32_t j = 0; j < N_ENTRY_PER_FAT; j++) {
      if (fat_buf[j] == FREE_CLUSTER) {
        fat_buf[j] = EOC;
        fat32fs_writeblock(fat32fs_clusteridx_2_fatblockidx(i),
                           (void *)fat_buf);
        found_cluster_idx = i + j;
        break;
      }
    }
  }
  return found_cluster_idx;
}

void fat32fs_get_SFN_fname(fat32_dirent_sfn_t *dirent, char *filename) {
  uint32_t idx = 0;
  for (uint32_t i = 0; i < 8 && dirent->name[i] != ' '; ++i) {
    filename[idx++] = dirent->name[i];
  }
  if (!(dirent->attr & DIR_ATTR)) {
    filename[idx++] = '.';
    for (uint32_t i = 0; i < 3 && dirent->ext[i] != ' '; ++i) {
      filename[idx++] = dirent->ext[i];
    }
  }
  filename[idx++] = '\0';
}

void fat32fs_fill_SFN_fname(fat32_dirent_sfn_t *dirent, const char *filename) {
  if (strcmp(filename, ".") == 0) {
    memset(dirent->name, ' ', 8);
    dirent->name[0] = '.';
    memset(dirent->ext, ' ', 3);
    return;
  }
  if (strcmp(filename, "..") == 0) {
    memset(dirent->name, ' ', 8);
    dirent->name[0] = '.';
    dirent->name[1] = '.';
    memset(dirent->ext, ' ', 3);
    return;
  }

  uint32_t n = strlen(filename);
  uint32_t dot_idx = n;
  for (uint32_t i = 0; i < n; ++i) {
    if (filename[i] == '.') {
      dot_idx = i;
      break;
    }
  }
  for (uint32_t i = 0; i < 8; ++i) {
    if (i < dot_idx) {
      dirent->name[i] = filename[i];
    } else {
      dirent->name[i] = ' ';
    }
  }
  for (uint32_t i = 0; i < 3; ++i) {
    if (dot_idx + i + 1 < n) {
      dirent->ext[i] = filename[dot_idx + i + 1];
    } else {
      dirent->ext[i] = ' ';
    }
  }
}

int register_fat32fs() {
  filesystem_t fs;
  fs.name = "fat32fs";
  fs.setup_mount = fat32fs_setup_mount;
  fs.syncfs = fat32fs_sync;
  return register_filesystem(&fs);
}

int fat32fs_setup_mount(filesystem_t *fs, mount_t *_mount) {
  mbr_t mbr;
  readblock(0, (void *)&mbr);
  if (mbr.signature[0] != 0x55 || mbr.signature[1] != 0xAA) {
    uart_sendline("[fat32fs_setup_mount] Invalid MBR signature.\n");
    return -1;
  }

  mbr_partition_entry_t *partition1 = &mbr.partitions[0];
  if (partition1->partition_type == 0x0B) { // FAT32 with CHS addressing
    fat32_bootsector_t fat32_bootsec;
    readblock(partition1->first_sector_lba, (void *)&fat32_bootsec);
    fat32_md = memory_pool_allocator(sizeof(fat32_metadata_t), 0);
    fat32_md->fat_region_block_idx =
        partition1->first_sector_lba + fat32_bootsec.n_reserved_sectors;
    fat32_md->data_region_block_idx =
        partition1->first_sector_lba + fat32_bootsec.n_reserved_sectors +
        fat32_bootsec.n_sectors_per_fat * fat32_bootsec.n_fats;
    fat32_md->n_fats = fat32_bootsec.n_fats;
    fat32_md->n_sectors = fat32_bootsec.n_sectors;
    fat32_md->n_sectors_per_cluster = fat32_bootsec.n_sectors_per_cluster;
    fat32_md->n_sectors_per_fat = fat32_bootsec.n_sectors_per_fat;
    fat32_md->root_dir_cluster_idx = fat32_bootsec.root_dir_cluster_idx;

    // Output fat32_md information
    uart_sendline("FAT32 Metadata:\n");
    uart_sendline("[fat32fs_setup_mount] FAT Region Block Index: %d\n",
                  fat32_md->fat_region_block_idx);
    uart_sendline("[fat32fs_setup_mount] Data Region Block Index: %d\n",
                  fat32_md->data_region_block_idx);
    uart_sendline("[fat32fs_setup_mount] Root Directory Cluster Index: %d\n",
                  fat32_md->root_dir_cluster_idx);
    uart_sendline("[fat32fs_setup_mount] Number of Sectors: %d\n",
                  fat32_md->n_sectors);
    uart_sendline("[fat32fs_setup_mount] Number of Sectors Per FAT: %d\n",
                  fat32_md->n_sectors_per_fat);
    uart_sendline("[fat32fs_setup_mount] Number of Sectors Per Cluster: %d\n",
                  fat32_md->n_sectors_per_cluster);
    uart_sendline("[fat32fs_setup_mount] Number of FATs: %d\n",
                  fat32_md->n_fats);
  } else {
    uart_sendline("[fat32fs_setup_mount] Unsupported partition type.\n");
    return -1;
  }

  _mount->fs = fs;
  _mount->root =
      fat32fs_create_vnode(0, DIR, NULL, -1, fat32_md->root_dir_cluster_idx, 0);

  fat32fs_traverse_directory(_mount->root, fat32_md->root_dir_cluster_idx, 0);

  return 0;
}

int fat32fs_sync() {
  double_linked_node_t *cur;
  double_linked_for_each(cur, fat32_cache_list_head) {
    fat32_cache_block_t *node = (fat32_cache_block_t *)cur;
    double_linked_remove((double_linked_node_t *)node);
    if (node->dirty_flag) {
      writeblock(node->block_idx, (void *)node->block);
    }
    memory_pool_free(node, 0);
  }
  return 0;
}

vnode_t *fat32fs_create_vnode(mount_t *_mount, node_type_t type,
                              const char *name, uint32_t dirent_cluster,
                              uint32_t first_cluster, uint32_t size) {
  vnode_t *v = memory_pool_allocator(sizeof(vnode_t), 0);
  v->mount = _mount;
  v->v_ops = &fat32_vnode_operations;
  v->f_ops = &fat32_file_operations;
  v->type = FAT32;
  fat32_inode_t *inode = memory_pool_allocator(sizeof(fat32_inode_t), 0);
  simple_memset(inode, 0, sizeof(fat32_inode_t));
  inode->type = type;
  if (name != NULL) {
    inode->name = simple_malloc(strlen(name) + 1, 0);
    strcpy(inode->name, name);
  }
  inode->dirent_cluster = dirent_cluster;
  inode->first_cluster = first_cluster;
  inode->size = size;
  v->internal = inode;
  return v;
}

void fat32fs_traverse_directory(vnode_t *parent, uint32_t cluster_idx,
                                int depth) {
  uint8_t buf[BLOCK_SIZE];
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  fat32_inode_t *parent_inode = (fat32_inode_t *)parent->internal;
  int child_idx = 0;

  uart_sendline("No | Size | Cluster | FAT | Name\n");
  while (cluster_idx < 0xFFFFFF8) {
    uint32_t block_idx = fat32fs_clusteridx_2_datablockidx(cluster_idx);
    fat32fs_readblock(block_idx, (void *)buf);
    for (uint32_t i = 0; i < BLOCK_SIZE; i += DIRENT_SIZE) {
      if (buf[i] == 0x00) {
        break;
      }
      if (buf[i] == 0xE5) {
        continue;
      }
      fat32_dirent_sfn_t *dirent = (fat32_dirent_sfn_t *)&buf[i];
      if (dirent->attr == SFN_ATTR || dirent->attr == DIR_ATTR) {
        char filename[13];
        fat32fs_get_SFN_fname(dirent, filename);
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
          uart_sendline("Skip %s\n", filename);
          continue;
        }
        uint32_t file_first_cluster = fat32fs_get_first_cluster(dirent);
        readblock(fat32fs_clusteridx_2_fatblockidx(file_first_cluster),
                  (void *)fat_buf);
        uart_sendline("%d | %d | %d | 0x%x | %s\n", i / DIRENT_SIZE,
                      dirent->fileSize,
                      fat32fs_clusteridx_2_datablockidx(file_first_cluster),
                      fat_buf[file_first_cluster % N_ENTRY_PER_FAT], filename);
        vnode_t *child = fat32fs_create_vnode(
            0, (dirent->attr & DIR_ATTR) ? DIR : FILE, filename, cluster_idx,
            file_first_cluster, dirent->fileSize);
        parent_inode->entry[child_idx++] = child;
        if (dirent->attr & DIR_ATTR) {
          fat32fs_traverse_directory(child, file_first_cluster, depth + 1);
        }
      }
    }
    readblock(fat32fs_clusteridx_2_fatblockidx(cluster_idx), (void *)fat_buf);
    cluster_idx = fat_buf[cluster_idx % N_ENTRY_PER_FAT];
  }
}

int fat32fs_write(file_t *file, const void *buf, size_t len) {
  fat32_inode_t *inode = (fat32_inode_t *)file->vnode->internal;
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  uint8_t ker_buf[BLOCK_SIZE];
  uint32_t cluster_idx = inode->first_cluster;
  int count = file->f_pos / BLOCK_SIZE;

  while (count--) {
    fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(cluster_idx),
                      (void *)fat_buf);
    uint32_t cluster_idx = fat_buf[cluster_idx % N_ENTRY_PER_FAT];
    // todo: check if cluster_idx is EOC
  }

  uint32_t ori_pos = file->f_pos;
  uint32_t remain_len = len;
  const uint8_t *src = (const uint8_t *)buf;
  while (remain_len > 0) {
    fat32fs_readblock(fat32fs_clusteridx_2_datablockidx(cluster_idx), ker_buf);
    uint32_t offset_in_block = file->f_pos % BLOCK_SIZE;
    uint32_t available_in_block = BLOCK_SIZE - offset_in_block;
    uint32_t shift =
        (remain_len < available_in_block) ? remain_len : available_in_block;
    uart_sendline("shift: %d, remain_len: %d, available_in_block: %d\n", shift,
                  remain_len, available_in_block);
    memcpy(ker_buf + offset_in_block, src + (file->f_pos - ori_pos), shift);
    fat32fs_writeblock(fat32fs_clusteridx_2_datablockidx(cluster_idx), ker_buf);
    file->f_pos += shift;
    remain_len -= shift;
    if (remain_len > 0) {
      fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(cluster_idx),
                        (void *)fat_buf);
      uint32_t cluster_idx = fat_buf[cluster_idx % N_ENTRY_PER_FAT];
      // todo: check if cluster_idx is EOC
    }
  }

  if (file->f_pos > inode->size) {
    inode->size = file->f_pos;
    uint32_t block_idx =
        fat32fs_clusteridx_2_datablockidx(inode->dirent_cluster);
    uint8_t dir_buf[BLOCK_SIZE];
    fat32fs_readblock(block_idx, dir_buf);
    for (uint32_t i = 0; i < BLOCK_SIZE; i += DIRENT_SIZE) {
      if (dir_buf[i] == 0x00) {
        break;
      }
      if (dir_buf[i] == 0xE5) { // deleted file
        continue;
      }
      fat32_dirent_sfn_t *dirent = (fat32_dirent_sfn_t *)&dir_buf[i];
      if (dirent->attr == SFN_ATTR) {
        char filename[13];
        fat32fs_get_SFN_fname(dirent, filename);
        if (strcmp(filename, inode->name) == 0) {
          dirent->fileSize = file->f_pos;
          fat32fs_writeblock(block_idx, dir_buf);
          break;
        }
      }
    }
  }

  return file->f_pos - ori_pos;
}

int fat32fs_read(file_t *file, void *buf, size_t len) {
  fat32_inode_t *inode = (fat32_inode_t *)file->vnode->internal;
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  uint32_t cluster_idx = inode->first_cluster;
  int count = file->f_pos / BLOCK_SIZE;
  while (count--) {
    fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(cluster_idx),
                      (void *)fat_buf);
    cluster_idx = fat_buf[cluster_idx % N_ENTRY_PER_FAT];
  }

  uint8_t ker_buf[BLOCK_SIZE];
  uint32_t remain_len = len;
  uint32_t ori_pos = file->f_pos;
  while (remain_len > 0 && file->f_pos < inode->size && cluster_idx != EOC) {
    fat32fs_readblock(fat32fs_clusteridx_2_datablockidx(cluster_idx), ker_buf);
    uint32_t offset_in_block = file->f_pos % BLOCK_SIZE;
    uint32_t available_in_block = BLOCK_SIZE - offset_in_block;
    uint32_t shift =
        (remain_len < available_in_block) ? remain_len : available_in_block;
    shift = (file->f_pos + shift < inode->size) ? shift
                                                : (inode->size - file->f_pos);
    memcpy((void *)buf + (file->f_pos - ori_pos),
           (void *)ker_buf + offset_in_block, shift);
    file->f_pos += shift;
    remain_len -= shift;

    if (remain_len > 0 && file->f_pos < inode->size) {
      fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(cluster_idx),
                        (void *)fat_buf);
      cluster_idx = fat_buf[cluster_idx % N_ENTRY_PER_FAT];
    }
  }
  return file->f_pos - ori_pos;
}

int fat32fs_open(vnode_t *file_node, file_t **target) {
  uart_sendline("[fat32fs_open] %s\n",
                ((fat32_inode_t *)file_node->internal)->name);
  (*target)->vnode = file_node;
  (*target)->f_pos = 0;
  (*target)->f_ops = file_node->f_ops;
  return 0;
}

int fat32fs_close(file_t *file) {
  memory_pool_free(file, 0);
  return 0;
}

long fat32fs_getsize(vnode_t *vd) {
  fat32_inode_t *inode = vd->internal;
  return inode->size;
}

int fat32fs_lookup(vnode_t *dir_node, vnode_t **target,
                   const char *component_name) {
  fat32_inode_t *dir_inode = dir_node->internal;
  if (dir_inode->type != DIR) {
    uart_sendline("[fat32fs_lookup] Not a directory.\n");
    return -1;
  }
  int child_idx = 0;
  for (; child_idx < FAT32FS_MAX_DIR_ENTRY; child_idx++) {
    vnode_t *vnode = dir_inode->entry[child_idx];
    if (!vnode) {
      break;
    }
    fat32_inode_t *inode = vnode->internal;
    if (strcmp(component_name, inode->name) == 0) {
      *target = vnode;
      return 0;
    }
  }
  uart_sendline("[fat32fs_lookup] Cannot find file.\n");
  return -1;
}

int fat32fs_create(vnode_t *dir_node, vnode_t **target,
                   const char *component_name) {
  uart_sendline("[fat32fs_create] Creating file: %s\n", component_name);

  fat32_inode_t *dir_inode = dir_node->internal;
  if (dir_inode->type != DIR) {
    uart_sendline("[fat32fs_create] Not a directory.\n");
    return -1;
  }

  int child_idx = 0;
  for (; child_idx < FAT32FS_MAX_DIR_ENTRY; child_idx++) {
    if (!dir_inode->entry[child_idx]) {
      break;
    }
  }
  if (child_idx == FAT32FS_MAX_DIR_ENTRY) {
    uart_sendline("[fat32fs_create] Directory is full.\n");
    return -1;
  }

  uint8_t buf[BLOCK_SIZE];
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  uint32_t dir_cluster = dir_inode->first_cluster;
  while (1) {
    uint32_t block_idx = fat32fs_clusteridx_2_datablockidx(dir_cluster);
    fat32fs_readblock(block_idx, (void *)buf);
    for (uint32_t i = 0; i < BLOCK_SIZE; i += DIRENT_SIZE) {
      fat32_dirent_sfn_t *dirent = (fat32_dirent_sfn_t *)&buf[i];
      if (dirent->name[0] == 0x00 || dirent->name[0] == 0xE5) {
        uint32_t new_cluster = fat32fs_get_free_fat_entry();
        if (new_cluster == -1) {
          uart_sendline("[fat32fs_create] No free cluster available.\n");
          return -1;
        }
        fat32fs_fill_SFN_fname(dirent, component_name);
        dirent->attr = SFN_ATTR;
        dirent->firstClusterHigh = (new_cluster >> 16) & 0xFFFF;
        dirent->firstClusterLow = new_cluster & 0xFFFF;
        dirent->fileSize = 0;
        fat32fs_writeblock(block_idx, (void *)buf);
        *target = fat32fs_create_vnode(0, FILE, component_name, dir_cluster,
                                       new_cluster, 0);
        dir_inode->entry[child_idx] = *target;
        return 0;
      }
    }
    fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                      (void *)fat_buf);
    uint32_t next_cluster = fat_buf[dir_cluster % N_ENTRY_PER_FAT];
    if (next_cluster >= 0xFFFFFF8) {
      break;
    }
    dir_cluster = next_cluster;
  }

  uart_sendline("[fat32fs_create] Need to create new directory block.\n");
  uint32_t new_dir_cluster = fat32fs_get_free_fat_entry();
  if (new_dir_cluster == -1) {
    uart_sendline("[fat32fs_create] No free cluster available for new "
                  "directory block.\n");
    return -1;
  }

  fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                    (void *)fat_buf);
  fat_buf[dir_cluster % N_ENTRY_PER_FAT] = new_dir_cluster;
  fat32fs_writeblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                     (void *)fat_buf);

  memset(buf, 0, BLOCK_SIZE);
  fat32fs_writeblock(fat32fs_clusteridx_2_datablockidx(new_dir_cluster),
                     (void *)buf);
  return fat32fs_create(dir_node, target, component_name);
}

int fat32fs_mkdir(vnode_t *dir_node, vnode_t **target,
                  const char *component_name) {
  uart_sendline("[fat32fs_mkdir] Creating directory: %s\n", component_name);

  fat32_inode_t *dir_inode = dir_node->internal;
  if (dir_inode->type != DIR) {
    uart_sendline("[fat32fs_mkdir] Not a directory.\n");
    return -1;
  }

  int child_idx = 0;
  for (; child_idx < FAT32FS_MAX_DIR_ENTRY; child_idx++) {
    if (!dir_inode->entry[child_idx]) {
      break;
    }
  }
  if (child_idx == FAT32FS_MAX_DIR_ENTRY) {
    uart_sendline("[fat32fs_mkdir] Directory is full.\n");
    return -1;
  }

  uint8_t buf[BLOCK_SIZE];
  uint32_t fat_buf[N_ENTRY_PER_FAT];
  uint32_t dir_cluster = dir_inode->first_cluster;
  while (1) {
    uint32_t block_idx = fat32fs_clusteridx_2_datablockidx(dir_cluster);
    fat32fs_readblock(block_idx, (void *)buf);
    for (uint32_t i = 0; i < BLOCK_SIZE; i += DIRENT_SIZE) {
      fat32_dirent_sfn_t *dirent = (fat32_dirent_sfn_t *)&buf[i];
      if (dirent->name[0] == 0x00 || dirent->name[0] == 0xE5) {
        uint32_t new_cluster = fat32fs_get_free_fat_entry();
        if (new_cluster == -1) {
          uart_sendline("[fat32fs_mkdir] No free cluster available.\n");
          return -1;
        }
        fat32fs_fill_SFN_fname(dirent, component_name);
        dirent->attr = DIR_ATTR;
        dirent->firstClusterHigh = (new_cluster >> 16) & 0xFFFF;
        dirent->firstClusterLow = new_cluster & 0xFFFF;
        dirent->fileSize = 0;
        fat32fs_writeblock(block_idx, (void *)buf);

        memset(buf, 0, BLOCK_SIZE);
        fat32_dirent_sfn_t *dot = (fat32_dirent_sfn_t *)&buf[0];
        fat32_dirent_sfn_t *dotdot = (fat32_dirent_sfn_t *)&buf[DIRENT_SIZE];

        // Add '.' entry
        fat32fs_fill_SFN_fname(dot, ".");
        dot->attr = DIR_ATTR;
        dot->firstClusterHigh = (new_cluster >> 16) & 0xFFFF;
        dot->firstClusterLow = new_cluster & 0xFFFF;
        dot->fileSize = 0;

        // Add '..' entry
        fat32fs_fill_SFN_fname(dotdot, "..");
        dotdot->attr = DIR_ATTR;
        dotdot->firstClusterHigh = (dir_inode->first_cluster >> 16) & 0xFFFF;
        dotdot->firstClusterLow = dir_inode->first_cluster & 0xFFFF;
        dotdot->fileSize = 0;

        fat32fs_writeblock(fat32fs_clusteridx_2_datablockidx(new_cluster),
                           (void *)buf);

        // Create directory vnode
        *target = fat32fs_create_vnode(0, DIR, component_name, dir_cluster,
                                       new_cluster, 0);
        dir_inode->entry[child_idx] = *target;
        return 0;
      }
    }
    fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                      (void *)fat_buf);
    uint32_t next_cluster = fat_buf[dir_cluster % N_ENTRY_PER_FAT];
    if (next_cluster >= 0xFFFFFF8) {
      break;
    }
    dir_cluster = next_cluster;
  }

  uart_sendline("[fat32fs_mkdir] Need to create new directory block.\n");
  uint32_t new_dir_cluster = fat32fs_get_free_fat_entry();
  if (new_dir_cluster == -1) {
    uart_sendline(
        "[fat32fs_mkdir] No free cluster available for new directory block.\n");
    return -1;
  }
  fat32fs_readblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                    (void *)fat_buf);
  fat_buf[dir_cluster % N_ENTRY_PER_FAT] = new_dir_cluster;
  fat32fs_writeblock(fat32fs_clusteridx_2_fatblockidx(dir_cluster),
                     (void *)fat_buf);
  memset(buf, 0, BLOCK_SIZE);
  fat32fs_writeblock(fat32fs_clusteridx_2_datablockidx(new_dir_cluster),
                     (void *)buf);
  return fat32fs_mkdir(dir_node, target, component_name);
}
