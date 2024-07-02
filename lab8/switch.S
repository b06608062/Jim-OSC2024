.global switch_to
switch_to:
    stp x19, x20, [x0, 16 * 0]
    stp x21, x22, [x0, 16 * 1]
    stp x23, x24, [x0, 16 * 2]
    stp x25, x26, [x0, 16 * 3]
    stp x27, x28, [x0, 16 * 4]
    stp fp, lr, [x0, 16 * 5]
    mov x9, sp
    str x9, [x0, 16 * 6]

    ldp x19, x20, [x1, 16 * 0]
    ldp x21, x22, [x1, 16 * 1]
    ldp x23, x24, [x1, 16 * 2]
    ldp x25, x26, [x1, 16 * 3]
    ldp x27, x28, [x1, 16 * 4]
    ldp fp, lr, [x1, 16 * 5]
    ldp x9, x0, [x1, 16 * 6]
    mov sp,  x9

    msr tpidr_el1, x1

    dsb ish // ensure write has completed
    msr ttbr0_el1, x0 // switch translation based address.
    tlbi vmalle1is // invalidate all TLB entries
    dsb ish // ensure completion of TLB invalidatation
    isb // clear pipeline

    ret

.global store_context
store_context:
    stp x19, x20, [x0, 16 * 0]
    stp x21, x22, [x0, 16 * 1]
    stp x23, x24, [x0, 16 * 2]
    stp x25, x26, [x0, 16 * 3]
    stp x27, x28, [x0, 16 * 4]
    stp fp, lr, [x0, 16 * 5]
    mov x9, sp
    str x9, [x0, 16 * 6]
    ret

.global load_context
load_context:
    ldp x19, x20, [x0, 16 * 0]
    ldp x21, x22, [x0, 16 * 1]
    ldp x23, x24, [x0, 16 * 2]
    ldp x25, x26, [x0, 16 * 3]
    ldp x27, x28, [x0, 16 * 4]
    ldp fp, lr, [x0, 16 * 5]
    ldr x9, [x0, 16 * 6]
    mov sp,  x9
    ret

.global get_current
get_current:
    mrs x0, tpidr_el1
    ret


.global save_kernel_context
save_kernel_context:
    stp x1, x2, [x0 ,16 * 0]
    stp x3, x4, [x0 ,16 * 1]
    stp x5, x6, [x0 ,16 * 2]
    stp x7, x8, [x0 ,16 * 3]
    stp x9, x10, [x0 ,16 * 4]
    stp x11, x12, [x0 ,16 * 5]
    stp x13, x14, [x0 ,16 * 6]
    stp x15, x16, [x0 ,16 * 7]
    stp x17, x18, [x0 ,16 * 8]
    stp x19, x20, [x0 ,16 * 9]
    stp x21, x22, [x0 ,16 * 10]
    stp x23, x24, [x0 ,16 * 11]
    stp x25, x26, [x0 ,16 * 12]
    stp x27, x28, [x0 ,16 * 13]
    stp x29, x30, [x0 ,16 * 14]
    mrs x1, spsr_el1
    str x1, [x0, 16 * 15 + 8]
    mrs x1, elr_el1
    str x1, [x0, 16 * 16]
    mov x1, sp
    str x1, [x0, 16 * 16 + 8]
    ldp x1, x2, [x0, 16 * 0]
    ret


.global load_kernel_context
load_kernel_context:
    ldp x1, x2, [x0 ,16 * 0]
    ldp x3, x4, [x0 ,16 * 1]
    ldp x5, x6, [x0 ,16 * 2]
    ldp x7, x8, [x0 ,16 * 3]
    ldp x9, x10, [x0 ,16 * 4]
    ldp x11, x12, [x0 ,16 * 5]
    ldp x13, x14, [x0 ,16 * 6]
    ldp x15, x16, [x0 ,16 * 7]
    ldp x17, x18, [x0 ,16 * 8]
    ldp x19, x20, [x0 ,16 * 9]
    ldp x21, x22, [x0 ,16 * 10]
    ldp x23, x24, [x0 ,16 * 11]
    ldp x25, x26, [x0 ,16 * 12]
    ldp x27, x28, [x0 ,16 * 13]
    ldp x29, x30, [x0 ,16 * 14]
    ldr x1, [x0, 16 * 15 + 8]
    msr spsr_el1, x1
    ldr x1, [x0, 16 * 16]
    msr elr_el1, x1
    ldr x1, [x0, 16 * 16 + 8]
    mov sp, x1
    ldp x1, x2, [x0 ,16 * 0]
    ret