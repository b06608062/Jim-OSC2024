#ifndef FAT32_H
#define FAT32_H

#include "dlist.h"
#include "types.h"
#include "vfs.h"

typedef struct mbr_partition_entry {
  //  Status or physical drive (bit 7 set is for active or
  //  bootable, old MBRs only accept 0x80, 0x00 means inactive, and 0x01–0x7F
  //  stand for invalid
  uint8_t status_flag; // offset : 0x00
  // CHS address of the first absolute sector in partition
  uint8_t first_sector_chsaddr[3]; // offset: 0x01
  uint8_t partition_type;          // offset: 0x04
  // CHS address of the last absolute sector in partition
  uint8_t last_sector_chsaddr[3]; // offset: 0x05
  // LBA of first absolute sector in the partition
  uint32_t first_sector_lba; // offset: 0x08
  // Number of sectors in partition
  uint32_t n_sector; // offset: 0x0C
} __attribute__((packed)) mbr_partition_entry_t;

typedef struct mbr {
  uint8_t bootstrapcode[446];          // Bootstrap code area
  mbr_partition_entry_t partitions[4]; // Partition table (4 entries)
  uint8_t signature[2];                // Boot signature (0x55AA)
} __attribute__((packed)) mbr_t;

#define SECTOR_SIZE 512
#define BLOCK_SIZE 512
#define DIRENT_SIZE 32
#define DIR_ATTR 0x10
#define SFN_ATTR 0x20
#define LFN_ATTR 0x0F

#define FREE_CLUSTER 0x0000000
#define EOC 0xFFFFFFF // Last cluster in file
#define N_ENTRY_PER_FAT 128

// https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#BPB20
// https://www.easeus.com/resource/fat32-disk-structure.html
typedef struct fat32_bootsector {
  // DOS 2.0 BPB
  uint8_t jumpcode[3]; // Jump instruction
  uint8_t oem_name[8]; // determines in which system the disk was formatted.
  uint16_t bytes_per_sector;
  uint8_t n_sectors_per_cluster;
  uint16_t n_reserved_sectors; // #logical sectors before the first FAT
  uint8_t n_fats;              // number of copies of FAT
  uint16_t n_root_dir_entries;
  uint16_t n_logical_sectors;
  uint8_t media_descriptor;
  uint16_t logical_sectors_per_fat;

  // DOS 3.31 BPB
  uint16_t physical_sectors_per_track;
  uint16_t n_heads;          // number of heads for disks
  uint32_t n_hidden_sectors; // count of hidden sectors preceding the partition
                             // hat contains this FAT volume
  uint32_t n_sectors;        // total logical sectors

  // DOS 7.1 Extended BPB for FAT32
  uint32_t n_sectors_per_fat;
  uint16_t mirror_flag;
  uint16_t fat32_drive_version;
  uint32_t root_dir_cluster_idx;   // Cluster number of root directory start,
                                   // typically 2 (first cluster)
  uint16_t fs_info_sector_num;     // Logical sector number of FS Information
                                   // Sector, typically 1,
  uint16_t backup_boot_sector_num; // First logical sector number of a copy of
                                   // the three FAT32 boot sectors, typically 6
  uint8_t rsv[12];
  uint8_t physical_drive_num;
  uint8_t unused;
  uint8_t ext_boot_signature;
  uint32_t volume_id;
  uint8_t volumne_label[11];
  uint8_t fs_type[8];
  uint8_t executable_code[420];
  uint8_t boot_record_signature[2];
} __attribute__((packed)) fat32_bootsector_t;

typedef struct fat32_metadata {
  uint32_t fat_region_block_idx;
  uint32_t data_region_block_idx;
  uint32_t root_dir_cluster_idx;
  uint32_t n_sectors;
  uint32_t n_sectors_per_fat;
  uint8_t n_sectors_per_cluster;
  uint8_t n_fats;
} fat32_metadata_t;

typedef struct fat32_dirent_sfn { // Short File Names (SFN)
  char name[8];
  char ext[3];
  uint8_t attr;              // File attributes
  uint8_t rsv;               // Reserved, must be 0
  uint8_t crtTimeTenth;      // Creation time in tenths of a second
  uint16_t crtTime;          // Creation time
  uint16_t crtDate;          // Creation date
  uint16_t lastAcccessDate;  // Last access date
  uint16_t firstClusterHigh; // high 2 bytes of first cluster number in FAT32
  uint16_t wrtTime;          // last modified time
  uint16_t wrtDate;          // last modified date
  uint16_t firstClusterLow;  // first cluster in FAT12 and FAT16. low 2 bytes of
                             // first cluster in FAT32.
  uint32_t fileSize;         // file size in bytes
} __attribute__((packed)) fat32_dirent_sfn_t;

#define FAT32FS_MAX_DIR_ENTRY 64

typedef struct fat32_inode {
  node_type_t type;
  char *name;
  vnode_t *entry[FAT32FS_MAX_DIR_ENTRY];
  uint32_t dirent_cluster;
  uint32_t first_cluster;
  uint32_t size;
} fat32_inode_t;

typedef struct fat32_cache_block {
  double_linked_node_t node;
  uint32_t block_idx;
  uint8_t block[BLOCK_SIZE];
  uint8_t dirty_flag;
} fat32_cache_block_t;

void fat32fs_cache_init();
void fat32fs_cache_list_push(uint32_t block_idx, void *buf, uint8_t dirty_flag);
fat32_cache_block_t *fat32fs_cache_list_find(uint32_t block_idx);

void fat32fs_readblock(uint32_t block_idx, void *buf);
void fat32fs_writeblock(uint32_t block_idx, void *buf);

uint32_t fat32fs_get_first_cluster(fat32_dirent_sfn_t *dirent);
uint32_t fat32fs_clusteridx_2_datablockidx(uint32_t cluster_idx);
uint32_t fat32fs_clusteridx_2_fatblockidx(uint32_t cluster_idx);
uint32_t fat32fs_get_free_fat_entry();

void fat32fs_get_SFN_fname(fat32_dirent_sfn_t *dirent, char *filename);
void fat32fs_fill_SFN_fname(fat32_dirent_sfn_t *dirent, const char *filename);

int register_fat32fs();
int fat32fs_setup_mount(filesystem_t *fs, mount_t *_mount);
int fat32fs_sync();
vnode_t *fat32fs_create_vnode(mount_t *_mount, node_type_t type,
                              const char *name, uint32_t dirent_cluster,
                              uint32_t first_cluster, uint32_t size);
void fat32fs_traverse_directory(vnode_t *parent, uint32_t cluster_idx,
                                int depth);

int fat32fs_write(file_t *file, const void *buf, size_t len);
int fat32fs_read(file_t *file, void *buf, size_t len);
int fat32fs_open(vnode_t *file_node, file_t **target);
int fat32fs_close(file_t *file);
long fat32fs_getsize(vnode_t *vd);

int fat32fs_lookup(vnode_t *dir_node, vnode_t **target,
                   const char *component_name);
int fat32fs_create(vnode_t *dir_node, vnode_t **target,
                   const char *component_name);
int fat32fs_mkdir(vnode_t *dir_node, vnode_t **target,
                  const char *component_name);

#endif /* FAT32_H */
