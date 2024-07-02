#include "include/mmu.h"
#include "include/allocator.h"
#include "include/buddy_system.h"
#include "include/exception.h"
#include "include/heap.h"
#include "include/thread.h"
#include "include/types.h"
#include "include/uart.h"
#include "include/utils.h"

extern thread_t *current_thread;
extern frame_array_node_t frame_array[];

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
      pte_table1[i] = (0x00000000 + addr) | BOOT_PTE_ATTR_nGnRnE;
    } else {
      pte_table1[i] = (0x00000000 + addr) | BOOT_PTE_ATTR_NOCACHE;
    }
    pte_table2[i] = (0x40000000 + addr) | BOOT_PTE_ATTR_nGnRnE; // 512 * 2MB
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
      table_p[idx] |=
          PD_KNX | PD_ACCESS | (MAIR_IDX_NORMAL_NOCACHE << 2) | PD_TABLE | flag;
      return;
    }
    if (!table_p[idx]) {
      size_t *newtable_p = (size_t *)buddy_system_allocator(0x1000);
      simple_memset((char *)newtable_p, 0, 0x1000);
      table_p[idx] = VIRT_TO_PHYS((size_t)newtable_p);
      table_p[idx] |= PD_ACCESS | (MAIR_IDX_NORMAL_NOCACHE << 2) | PD_TABLE;
    }
    table_p = (size_t *)PHYS_TO_VIRT((size_t)(table_p[idx] & ENTRY_ADDR_MASK));
  }
}

void mmu_add_vma(thread_t *t, size_t va, size_t size, size_t pa, size_t rwx,
                 int is_alloced) {
  size = size % 0x1000 ? size + (0x1000 - size % 0x1000) : size;
  vm_area_struct_t *new_area =
      memory_pool_allocator(sizeof(vm_area_struct_t), 0);
  new_area->virt_addr = va;
  new_area->phys_addr = pa;
  new_area->area_size = size;
  new_area->rwx = rwx;
  new_area->is_alloced = is_alloced;
  double_linked_add_before((double_linked_node_t *)new_area, &t->vma_list);
}

void mmu_del_vma(thread_t *t) {
  double_linked_node_t *cur;
  double_linked_for_each(cur, &t->vma_list) {
    vm_area_struct_t *vma = (vm_area_struct_t *)cur;
    if (vma->is_alloced) {
      int no_ref = 1;
      for (int i = 0; i < vma->area_size / PAGE_SIZE; ++i) {
        if (--frame_array[(vma->phys_addr + i * PAGE_SIZE) / PAGE_SIZE].ref) {
          no_ref = 0;
        }
      }
      if (no_ref) {
        buddy_system_free(PHYS_TO_VIRT(vma->phys_addr));
      }
    }
    memory_pool_free((void *)cur, 0);
  }
}

void mmu_free_page_tables(size_t *page_table, int level) {
  size_t *table_virt = (size_t *)PHYS_TO_VIRT((char *)page_table);
  for (int i = 0; i < 512; ++i) {
    if (table_virt[i] != 0) {
      size_t *next_table = (size_t *)(table_virt[i] & ENTRY_ADDR_MASK);
      if (table_virt[i] & PD_TABLE) {
        if (level != 2)
          mmu_free_page_tables(next_table, level + 1);
        table_virt[i] = 0L;
        buddy_system_free((uint64_t)PHYS_TO_VIRT((char *)next_table));
      }
    }
  }
}

void mmu_memfail_abort_handler(esr_el1_t *esr_el1) {
  uint64_t far_el1;
  __asm__ __volatile__("mrs %0, FAR_EL1" : "=r"(far_el1));
  uart_sendline("far_el1: 0x%p ", far_el1);
  double_linked_node_t *cur;
  vm_area_struct_t *the_area_ptr = NULL;
  double_linked_for_each(cur, &current_thread->vma_list) {
    vm_area_struct_t *vma = (vm_area_struct_t *)cur;
    if (far_el1 >= vma->virt_addr &&
        far_el1 < (vma->virt_addr + vma->area_size)) {
      the_area_ptr = vma;
      break;
    }
  }

  // Area is not part of process's address space
  if (!the_area_ptr) {
    uart_sendline("[Segmentation fault]\n");
    thread_exit();
    return;
  }

  size_t flag = 0;
  if (!(the_area_ptr->rwx & (0b1 << 2)))
    flag |= PD_UNX; // 4: executable
  if (!(the_area_ptr->rwx & (0b1 << 1)))
    flag |= PD_RDONLY; // 2: writable
  if (the_area_ptr->rwx & (0b1 << 0))
    flag |= PD_UK_ACCESS; // 1: readable / accessible

  size_t addr_offset = (far_el1 - the_area_ptr->virt_addr);
  addr_offset = (addr_offset % 0x1000) == 0
                    ? addr_offset
                    : addr_offset - (addr_offset % 0x1000);

  // For translation fault, only map one page frame for the fault address
  if ((esr_el1->iss & 0x3f) == TF_LEVEL0 ||
      (esr_el1->iss & 0x3f) == TF_LEVEL1 ||
      (esr_el1->iss & 0x3f) == TF_LEVEL2 ||
      (esr_el1->iss & 0x3f) == TF_LEVEL3) {
    uart_sendline("[Translation fault]\n");
    map_one_page(PHYS_TO_VIRT(current_thread->context.pgd),
                 the_area_ptr->virt_addr + addr_offset,
                 the_area_ptr->phys_addr + addr_offset, flag);
  } else {
    if (esr_el1->iss & 0b001111) {
      if (the_area_ptr->rwx & 0b10) {
        uart_sendline("[Copy on Write]\n");
        uart_sendline(
            "ref count: 0x%d\n",
            frame_array[(the_area_ptr->phys_addr + addr_offset) / PAGE_SIZE]
                .ref);
        if (frame_array[(the_area_ptr->phys_addr + addr_offset) / PAGE_SIZE]
                .ref > 1) {
          frame_array[(the_area_ptr->phys_addr + addr_offset) / PAGE_SIZE]
              .ref--;
          size_t new_page = buddy_system_allocator(PAGE_SIZE);
          frame_array[VIRT_TO_PHYS(new_page) / PAGE_SIZE].ref++;
          memcpy((char *)new_page,
                 (char *)PHYS_TO_VIRT(the_area_ptr->phys_addr + addr_offset),
                 PAGE_SIZE);
          the_area_ptr->phys_addr = VIRT_TO_PHYS(new_page);
          map_one_page(PHYS_TO_VIRT(current_thread->context.pgd),
                       the_area_ptr->virt_addr + addr_offset,
                       the_area_ptr->phys_addr + addr_offset, flag);
        } else {
          map_one_page(PHYS_TO_VIRT(current_thread->context.pgd),
                       the_area_ptr->virt_addr + addr_offset,
                       the_area_ptr->phys_addr + addr_offset, flag);
        }
      } else {
        uart_sendline("[Permission fault]\n");
        thread_exit();
      }
    } else {
      uart_sendline("[Other fault]\n");
      thread_exit();
    }
  }
  asm("tlbi vmalle1is");
  asm("dsb ish");
}