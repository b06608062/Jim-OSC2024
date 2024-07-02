#include "include/mmu_S.h"

.section ".text.boot"

.global _start

_start:
    bl from_el2_to_el1

set_mmu_configuration:
    ldr x4, = TCR_CONFIG_DEFAULT
    msr tcr_el1, x4

    ldr x4, = ((MAIR_DEVICE_nGnRnE << (MAIR_IDX_DEVICE_nGnRnE * 8)) | (MAIR_NORMAL_NOCACHE << (MAIR_IDX_NORMAL_NOCACHE * 8)))
    msr mair_el1, x4

    ldr x4, = MMU_PGD_ADDR
    ldr x1, = MMU_PUD_ADDR

    ldr x2, = BOOT_PGD_ATTR
    orr x2, x1, x2
    str x2, [x4]

    // ldr x2, = 0x3B400000
    // mov sp, x2
    mov sp, 0x3C000000
    bl set_2M_kernel_mmu

    msr ttbr0_el1, x4
    msr ttbr1_el1, x4
    
    mrs x2, sctlr_el1
    orr x2 , x2, 1
    msr sctlr_el1, x2

    ldr x2, = set_exception_vector_table
    br x2

set_exception_vector_table:
    adr x1, exception_vector_table
    msr vbar_el1, x1

setup_stack:
    ldr x1, = _stack_top
    mov sp, x1

setup_bss:
    ldr x1, = _bss_top
    ldr w2, = _bss_size

init_bss:
    cbz w2, run_main
    str xzr, [x1], #8
    sub w2, w2, #1
    cbnz w2, init_bss

run_main:
    bl main

proc_hang:
    wfe
    b proc_hang

from_el2_to_el1:
    mov x1, (1 << 31)
    msr hcr_el2, x1
    mov x1, 0x3c5
    msr spsr_el2, x1
    msr elr_el2, lr
    eret
