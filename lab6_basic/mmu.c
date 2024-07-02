#include "include/mmu.h"
#include "include/buddy_system.h"
#include "include/heap.h"
#include "include/types.h"

void *set_2M_kernel_mmu(void *x0) {
  // Turn
  //   Two-level Translation (1GB) - in start.S
  // to
  //   Three-level Translation (2MB) - set PUD point to new table
  uint64_t *pud_table = (uint64_t *)MMU_PUD_ADDR;

  uint64_t *pte_table1 = (uint64_t *)MMU_PTE_ADDR;
  uint64_t *pte_table2 = (uint64_t *)(MMU_PTE_ADDR + 0x1000L);
  for (int i = 0; i < 512; ++i) {
    uint64_t addr = 0x200000L * i;
    if (addr >= PERIPHERAL_END) {
      pte_table1[i] = (0x00000000 + addr) + BOOT_PTE_ATTR_nGnRnE;
      continue;
    }
    pte_table1[i] = (0x00000000 + addr) |
                    BOOT_PTE_ATTR_NOCACHE; //   0 * 2MB // No definition for
                                           //   3-level attribute, use nocache.
    pte_table2[i] = (0x40000000 + addr) | BOOT_PTE_ATTR_NOCACHE; // 512 * 2MB
  }

  // set PUD
  pud_table[0] = (uint64_t)pte_table1 | BOOT_PUD_ATTR;
  pud_table[1] = (uint64_t)pte_table2 | BOOT_PUD_ATTR;

  return x0;
}

void map_one_page(size_t *virt_pgd_p, size_t va, size_t pa, size_t flag) {
  size_t *table_p = virt_pgd_p;
  for (int level = 0; level < 4; level++) {
    uint32_t idx = (va >> (39 - level * 9)) & 0x1ff;

    if (level == 3) {
      table_p[idx] = pa;
      table_p[idx] |= PD_ACCESS | PD_TABLE | (MAIR_IDX_NORMAL_NOCACHE << 2) |
                      PD_UK_ACCESS | PD_KNX | flag;
      return;
    }

    if (!table_p[idx]) {
      size_t *newtable_p = (size_t *)buddy_system_allocator(0x1000);
      simple_memset((void *)newtable_p, 0, 0x1000);
      table_p[idx] = VIRT_TO_PHYS((size_t)newtable_p);
      table_p[idx] |=
          PD_ACCESS | PD_TABLE | (MAIR_IDX_NORMAL_NOCACHE << 2) | flag;
    }

    table_p = (size_t *)PHYS_TO_VIRT((size_t)(table_p[idx] & ENTRY_ADDR_MASK));
  }
}

void mappages(size_t *virt_pgd_p, size_t va, size_t size, size_t pa,
              size_t flag) {
  pa = pa - (pa % 0x1000); // align
  for (size_t s = 0; s < size; s += 0x1000) {
    map_one_page(virt_pgd_p, va + s, pa + s, flag);
  }
}

void free_page_tables(size_t *page_table, int level) {
  size_t *table_virt = (size_t *)PHYS_TO_VIRT((char *)page_table);
  for (int i = 0; i < 512; ++i) {
    if (table_virt[i] != 0) {
      size_t *next_table = (size_t *)(table_virt[i] & ENTRY_ADDR_MASK);
      if (table_virt[i] & PD_TABLE) {
        if (level != 2)
          free_page_tables(next_table, level + 1);
        table_virt[i] = 0L;
        buddy_system_free((uint64_t)PHYS_TO_VIRT((char *)next_table));
      }
    }
  }
}
