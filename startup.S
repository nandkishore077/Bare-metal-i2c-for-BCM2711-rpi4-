.section ".text.boot"
.global _start
.type   _start, %function

_start:
    /* Park secondary cores — only core 0 continues */
    mrs     x0, mpidr_el1
    and     x0, x0, #0x03
    cbz     x0, core0_boot

park_loop:
    wfe
    b       park_loop

core0_boot:
    /* Set stack FIRST before any register access */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* HCR_EL2: RW=1 so EL1 runs AArch64 */
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    isb

    /* SCTLR_EL1: safe reset value — MMU off, caches off */
    ldr     x0, =0x04C00800
    msr     sctlr_el1, x0
    isb

    /*
     * SPSR_EL2: D=1 A=1 I=0 F=1 EL1h
     *   bits[3:0] = 0b0101 = EL1h (SP_EL1)
     *   bit[6]    = F (FIQ mask)
     *   bit[8]    = D (debug mask)
     *   bit[9]    = A (SError mask)
     *   IRQs (bit[7] = I) intentionally 0 so they unmask after eret
     * 0x345 = 0b0011_0100_0101
     */
    mov     x0, #0x345
    msr     spsr_el2, x0

    /* ELR_EL2: return address after eret = el1_entry */
    adr     x0, el1_entry
    msr     elr_el2, x0

    eret

el1_entry:
    /* Now at EL1 — reset stack pointer */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Install AArch64 vector table */
    ldr     x0, =_vector_table
    msr     vbar_el1, x0
    isb

    /* Unmask IRQs (DAIF bit I = 0) */
    msr     daifclr, #0x2
    isb

    /* Zero BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
bss_loop:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       bss_loop
bss_done:

    /*
     * [CHANGE-1] Call main() to match AI-generated i2c-app.c.
     *            Working version called bare_main — changed to main.
     */
    bl      main

hang:
    wfe
    b       hang


/* ======================================================================== */
/*  AArch64 Vector Table — must be 2 KiB aligned                            */
/*  Each entry is 128 bytes (32 instructions max).                           */
/* ======================================================================== */
.section ".text.vectors"
.balign 2048
.global _vector_table
_vector_table:

    /* ---- Current EL with SP_EL0 (not used) ---- */
    .balign 128
    b       _hang           /* Sync  */
    .balign 128
    b       _hang           /* IRQ   */
    .balign 128
    b       _hang           /* FIQ   */
    .balign 128
    b       _hang           /* SErr  */

    /* ---- Current EL with SP_ELx (EL1h) — ACTIVE ---- */
    .balign 128
    b       _hang           /* Sync  */
    .balign 128
    b       _irq_handler    /* IRQ   ← live entry */
    .balign 128
    b       _hang           /* FIQ   */
    .balign 128
    b       _hang           /* SErr  */

    /* ---- Lower EL AArch64 (not used) ---- */
    .balign 128
    b       _hang
    .balign 128
    b       _hang
    .balign 128
    b       _hang
    .balign 128
    b       _hang

    /* ---- Lower EL AArch32 (not used) ---- */
    .balign 128
    b       _hang
    .balign 128
    b       _hang
    .balign 128
    b       _hang
    .balign 128
    b       _hang


/* ======================================================================== */
/*  IRQ Handler                                                               */
/*  Saves all caller/callee-saved registers, dispatches to C ISR,            */
/*  then restores and returns with eret.                                      */
/* ======================================================================== */
.type _irq_handler, %function
_irq_handler:

    /* Save x0/x1 first */
    stp     x0,  x1,  [sp, #-16]!

    /* Save ELR_EL1 and SPSR_EL1 into x0/x1, then push */
    mrs     x0,  elr_el1
    mrs     x1,  spsr_el1
    stp     x0,  x1,  [sp, #-16]!

    /* Save all remaining general-purpose registers */
    stp     x2,  x3,  [sp, #-16]!
    stp     x4,  x5,  [sp, #-16]!
    stp     x6,  x7,  [sp, #-16]!
    stp     x8,  x9,  [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    stp     x20, x21, [sp, #-16]!
    stp     x22, x23, [sp, #-16]!
    stp     x24, x25, [sp, #-16]!
    stp     x26, x27, [sp, #-16]!
    stp     x28, x29, [sp, #-16]!
    stp     x30, xzr, [sp, #-16]!

    /*
     * GIC acknowledge:
     *   GICC base = 0xFF842000  (GIC base 0xFF840000 + GICC offset 0x2000)
     *   GICC_IAR  = GICC base + 0x00C
     *   IRQ ID    = IAR[9:0]
     */
    ldr     x19, =0xFF842000
    ldr     w0,  [x19, #0x00C]      /* read GICC_IAR */
    and     w20, w0, #0x3FF         /* extract IRQ ID into w20 */

    /* Ignore spurious interrupt (ID 1023) */
    cmp     w20, #1023
    b.eq    _irq_eoi

    /*
     * [CHANGE-2] Dispatch BSC1 SPI interrupt to IRQ_Handler().
     *
     *   Working version: cmp w20, #149 / bl BSC1_ISR
     *
     *   BCM2711 BSC1 SPI number = 53
     *   GIC ID = 32 + 53 = 85
     *
     *   AI app.c: APP_BSC1_GIC_SPI_ID = 85U / void IRQ_Handler(void)
     *
     *   Changed: #149 → #85, BSC1_ISR → IRQ_Handler
     */
    cmp     w20, #85
    b.ne    _irq_eoi
    bl      IRQ_Handler

_irq_eoi:
    /*
     * End of Interrupt:
     *   GICC_EOIR = GICC base + 0x010
     *   Write back the full IAR value (including CPU ID field)
     */
    str     w20, [x19, #0x010]      /* write GICC_EOIR */

    /* Restore general-purpose registers in reverse push order */
    ldp     x30, xzr, [sp], #16
    ldp     x28, x29, [sp], #16
    ldp     x26, x27, [sp], #16
    ldp     x24, x25, [sp], #16
    ldp     x22, x23, [sp], #16
    ldp     x20, x21, [sp], #16
    ldp     x18, x19, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x8,  x9,  [sp], #16
    ldp     x6,  x7,  [sp], #16
    ldp     x4,  x5,  [sp], #16
    ldp     x2,  x3,  [sp], #16

    /* Restore ELR_EL1 and SPSR_EL1 */
    ldp     x0,  x1,  [sp], #16
    msr     elr_el1,  x0
    msr     spsr_el1, x1

    /* Restore x0/x1 */
    ldp     x0,  x1,  [sp], #16

    eret


/* ======================================================================== */
/*  Fault hang — all unused exception vectors branch here                    */
/* ======================================================================== */
_hang:
    wfe
    b       _hang

.end
