#ifndef MMU_H
#define MMU_H

#include "dlist.h"
#include "thread.h"
#include "types.h"

// tcr_el1: The control register for stage 1 of the EL1&0 translation regime.
// T0SZ[5:0]   The size offset for ttbr0_el1 is 2**(64-T0SZ):
// 0x0000_0000_0000_0000 -> 0x0000_FFFF_FFFF_FFFF
// T1SZ [21:16] The size offset for ttbr1_el1 is 2**(64-T1SZ):
// 0xFFFF_0000_0000_0000 -> 0xFFFF_FFFF_FFFF_FFFF
#define TCR_CONFIG_REGION_48bit (((64 - 48) << 0) | ((64 - 48) << 16))
// TG0[15:14]  Granule size for the TTBR0_EL1: 0b00 = 4KB
// TG1[31:30]  Granule size for the TTBR1_EL1: 0b10 = 4KB
#define TCR_CONFIG_4KB ((0b00 << 14) | (0b10 << 30))
#define TCR_CONFIG_DEFAULT (TCR_CONFIG_REGION_48bit | TCR_CONFIG_4KB)

// ((MAIR_DEVICE_nGnRnE << (MAIR_IDX_DEVICE_nGnRnE * 8)) |
// (MAIR_NORMAL_NOCACHE << (MAIR_IDX_NORMAL_NOCACHE * 8)))
// mair_el1: Provides the memory attribute encodings corresponding
// to the possible AttrIndx values for stage 1 translations at EL1.
// ATTR0[7:0]: 0b0000dd00 Device memory,   dd = 0b00   Device-nGnRnE memory
// ATTR1[14:8] 0booooiiii Normal memory, oooo = 0b0100 Outer Non-cacheable,
// iiii = 0b0100 Inner Non-cacheable
#define MAIR_DEVICE_nGnRnE 0b00000000
#define MAIR_NORMAL_NOCACHE 0b01000100
#define MAIR_IDX_DEVICE_nGnRnE 0
#define MAIR_IDX_NORMAL_NOCACHE 1

#define PD_TABLE 0b11L       // Table Entry Armv8_a_address_translation p.14
#define PD_BLOCK 0b01L       // Block Entry
#define PD_UNX (1L << 54)    // non-executable page frame for EL0 if set
#define PD_KNX (1L << 53)    // non-executable page frame for EL1 if set
#define PD_ACCESS (1L << 10) // a page fault is generated if not set
#define PD_RDONLY (1L << 7)  // 0 for read-write, 1 for read-only.
#define PD_UK_ACCESS                                                           \
  (1L << 6) // 0 for only kernel access, 1 for user/kernel access.

// Used for EL1
#define BOOT_PGD_ATTR (PD_TABLE)
#define BOOT_PUD_ATTR (PD_ACCESS | PD_TABLE)

#define BOOT_PTE_ATTR_nGnRnE                                                   \
  (PD_UNX | PD_KNX | PD_ACCESS | PD_UK_ACCESS |                                \
   (MAIR_IDX_DEVICE_nGnRnE << 2) | PD_BLOCK)
#define BOOT_PTE_ATTR_NOCACHE                                                  \
  (PD_ACCESS | (MAIR_IDX_NORMAL_NOCACHE << 2) | PD_BLOCK)

#define MMU_PGD_BASE 0x2000L
#define MMU_PGD_ADDR (MMU_PGD_BASE + 0x0000L)
#define MMU_PUD_ADDR (MMU_PGD_BASE + 0x1000L)
#define MMU_PTE_ADDR (MMU_PGD_BASE + 0x2000L)

#define PHYS_TO_VIRT(x) (x + 0xffff000000000000)
#define VIRT_TO_PHYS(x) (x - 0xffff000000000000)
#define ENTRY_ADDR_MASK 0xfffffffff000L

#define PERIPHERAL_START 0x3c000000L
#define PERIPHERAL_END 0x3f000000L
#define USER_SPACE 0x0L
#define USER_STACK_BASE 0xfffffffff000L
#define USER_SIGNAL_WRAPPER_VA 0xfffffff00000L

typedef struct vm_area_struct {
  double_linked_node_t node;
  uint64_t virt_addr;
  uint64_t phys_addr;
  uint64_t area_size;
  uint64_t rwx; // 1, 2, 4
  int is_alloced;
} vm_area_struct_t;

#define MEMFAIL_DATA_ABORT_LOWER 0b100100 // esr_el1
#define MEMFAIL_INST_ABORT_LOWER 0b100000 // EC, bits [31:26]

#define TF_LEVEL0 0b000100 // iss IFSC, bits [5:0]
#define TF_LEVEL1 0b000101
#define TF_LEVEL2 0b000110
#define TF_LEVEL3 0b000111

typedef struct {
  uint32_t iss : 25, // Instruction specific syndrome
      il : 1,        // Instruction length bit
      ec : 6;        // Exception class
} esr_el1_t;

void *set_2M_kernel_mmu(void *x0);
void map_one_page(size_t *virt_pgd_p, size_t va, size_t pa, size_t flag);
void mmu_add_vma(thread_t *t, size_t va, size_t size, size_t pa, size_t rwx,
                 int is_alloced);
void mmu_del_vma(thread_t *t);
void mmu_free_page_tables(size_t *page_table, int level);
void mmu_memfail_abort_handler(esr_el1_t *esr_el1);

#endif /* MMU_H */