#include "include/mmu_S.h"

.section ".text.boot"

.global _start

// Entry point definition
_start:
    bl from_el2_to_el1               // Transition from EL2 to EL1

set_mmu_configuration:
    // set paging configuration (up : 0xffff000000000000 low : 0x0000000000000000)
    ldr x4, = TCR_CONFIG_DEFAULT
    msr tcr_el1, x4

    // Set Used Memory Attributes
    ldr x4, =((MAIR_DEVICE_nGnRnE << (MAIR_IDX_DEVICE_nGnRnE * 8)) | (MAIR_NORMAL_NOCACHE << (MAIR_IDX_NORMAL_NOCACHE * 8)))
    msr mair_el1, x4

    // set and enable MMU
    ldr x4, = MMU_PGD_ADDR // PGD's page frame at 0x2000
    ldr x1, = MMU_PUD_ADDR // PUD's page frame at 0x3000

    ldr x2, = BOOT_PGD_ATTR
    orr x2, x1, x2         // combine the physical address of next level page with attribute.
    str x2, [x4]           // [MMU_PGD_ADDR] = MMU_PUD_ADDR | BOOT_PGD_ATTR

    ldr x2, = BOOT_PUD_ATTR
    mov x3, 0x00000000
    orr x3, x2, x3         // [MMU_PUD_ADDR] = 0x00000000 | BOOT_PUD_ATTR
    str x3, [x1]           // 1st 1GB mapped by the 1st entry of PUD
    mov x3, 0x40000000
    orr x3, x2, x3         // [MMU_PUD_ADDR + 8] = 0x40000000 | BOOT_PUD_ATTR
    str x3, [x1, 8]        // 2nd 1GB mapped by the 2nd entry of PUD

    msr ttbr0_el1, x4      // load PGD to the bottom translation-based register.
    msr ttbr1_el1, x4      // also load PGD to the upper translation based register.

    // ldr x2, =0x3B400000
    // mov sp, x2
    mov sp, 0x3C000000     // temp stack
    bl set_2M_kernel_mmu

    mrs x2, sctlr_el1      // sctlr_el1: Provides top level control of the system, including its memory system, at EL1 and EL0.
    orr x2 , x2, 1         // sctlr_el1[0]: EL1&0 stage 1 address translation enabled/disabled.
    msr sctlr_el1, x2

    // indirect branch to the upper virtual address
    ldr x2, =set_exception_vector_table
    br x2

// Set the exception vector table
set_exception_vector_table:
    adr x1, exception_vector_table  // Get the address of the exception vector table
    msr vbar_el1, x1                // Set the Vector Base Address Register for EL1

// Set up the stack top
setup_stack:
    ldr x1, =_stack_top             // Load the address of the stack top
    mov sp, x1                      // Set the stack pointer

// Initialize BSS segment
setup_bss:
    ldr x1, =_bss_top               // Load the start address of the BSS segment
    ldr w2, =_bss_size              // Load the size of the BSS segment

init_bss:
    cbz w2, run_main                // If BSS size is zero, jump directly to the main program
    str xzr, [x1], #8               // Zero out the BSS segment
    sub w2, w2, #1                  // Decrement the BSS size by 1
    cbnz w2, init_bss               // If BSS is not fully initialized, continue initialization

// Run the main program
run_main:
    bl main                         // Call the main function

// Loop hang process
proc_hang:
    wfe                             // Wait for event
    b proc_hang                     // Infinite loop

// Implementation of the transition from EL2 to EL1
from_el2_to_el1:
    mov x1, (1 << 31)               // Set the RW bit in HCR_EL2, ensuring EL1 is in AArch64 execution state
    msr hcr_el2, x1
    mov x1, 0x3c5                   // Set SPSR_EL2, specifying the return state as EL1h, with DAIF disabled
    msr spsr_el2, x1
    msr elr_el2, lr                 // Set the return address
    eret                            // Execute exception return, transitioning back from EL2 to EL1
