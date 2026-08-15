/**
 * @file    uart.h
 * @brief   PL011 UART0 Bare Metal Driver for Raspberry Pi 4 (BCM2711)
 *
 * UART0 (PL011):
 *   ARM physical: 0xFE201000 (verified from iomem: fe201000.serial)
 *
 * Clock source (verified from clk_summary):
 *   uart clock = 48 MHz
 *
 * GPIO pins:
 *   GPIO14 = TXD0 (ALT0) — header pin 8
 *   GPIO15 = RXD0 (ALT0) — header pin 10
 *
 * Baud rate calculation (PL011 TRM §3.3.6):
 *   BRD  = UART_CLK / (16 × baud)
 *   IBRD = floor(BRD)
 *   FBRD = round(frac(BRD) × 64)
 *
 *   For 115200 baud @ 48 MHz:
 *   BRD  = 48000000 / (16 × 115200) = 26.0416...
 *   IBRD = 26
 *   FBRD = round(0.0416... × 64) = round(2.666...) = 3
 *
 * BUG FIXES:
 *   [BUG-U-01] uart_init(): baud rate divisor missing rounding.
 *              Original: divisor_fixed = (UART_CLOCK_HZ * 4U) / baud
 *                        = 192000000 / 115200 = 1666 (truncated)
 *                        ibrd = 1666/64 = 26, fbrd = 1666%64 = 2  ← WRONG
 *              Fixed:    divisor_fixed = ((UART_CLOCK_HZ * 4U) + (baud/2U)) / baud
 *                        = 192057600 / 115200 = 1667 (rounded)
 *                        ibrd = 1667/64 = 26, fbrd = 1667%64 = 3  ← CORRECT
 *              Impact:   fbrd=2 gives baud=115942 (0.65% fast) vs fbrd=3 at
 *                        baud=115384 (0.16% error). At 115200 both are within
 *                        UART tolerance (+/-2%), but rounding is correct per spec.
 *
 *   [BUG-U-02] uart_put_hex(): already prepends "0x" internally.
 *              i2c-app.c was calling uart_puts("Target 0x") THEN uart_put_hex()
 *              producing "Target 0x0x00000050" (double prefix).
 *              Fix: removed the internal "0x" from uart_put_hex() — callers
 *              control the prefix. OR callers must not add "0x" before calling.
 *              Chosen fix: remove internal "0x" from uart_put_hex() so it only
 *              prints the 8 hex digits. Callers add prefix as needed.
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "gpio.h"

/* ========================================================================== */
/*  UART0 Base Address                                                         */
/* ========================================================================== */

#define UART0_BASE              (PERI_BASE + 0x201000UL)    /* 0xFE201000 */

/* ========================================================================== */
/*  Register Access                                                            */
/* ========================================================================== */

#define UART_REG(offset)        (*(volatile uint32_t *)(UART0_BASE + (offset)))

#define UART0_DR                UART_REG(0x00)  /* Data Register               */
#define UART0_RSRECR            UART_REG(0x04)  /* Receive Status / Error Clear*/
#define UART0_FR                UART_REG(0x18)  /* Flag Register               */
#define UART0_ILPR              UART_REG(0x20)  /* IrDA Low-Power Counter      */
#define UART0_IBRD              UART_REG(0x24)  /* Integer Baud Rate Divisor   */
#define UART0_FBRD              UART_REG(0x28)  /* Fractional Baud Rate Div    */
#define UART0_LCRH              UART_REG(0x2C)  /* Line Control Register       */
#define UART0_CR                UART_REG(0x30)  /* Control Register            */
#define UART0_IFLS              UART_REG(0x34)  /* Interrupt FIFO Level Select */
#define UART0_IMSC              UART_REG(0x38)  /* Interrupt Mask Set/Clear    */
#define UART0_RIS               UART_REG(0x3C)  /* Raw Interrupt Status        */
#define UART0_MIS               UART_REG(0x40)  /* Masked Interrupt Status     */
#define UART0_ICR               UART_REG(0x44)  /* Interrupt Clear Register    */
#define UART0_DMACR             UART_REG(0x48)  /* DMA Control Register        */

/* ========================================================================== */
/*  Flag Register (FR) Bits                                                    */
/* ========================================================================== */

#define UART_FR_TXFE            (1U << 7)   /* TX FIFO empty                   */
#define UART_FR_RXFF            (1U << 6)   /* RX FIFO full                    */
#define UART_FR_TXFF            (1U << 5)   /* TX FIFO full                    */
#define UART_FR_RXFE            (1U << 4)   /* RX FIFO empty                   */
#define UART_FR_BUSY            (1U << 3)   /* UART busy transmitting          */

/* ========================================================================== */
/*  Line Control Register (LCRH) Bits                                         */
/* ========================================================================== */

#define UART_LCRH_SPS           (1U << 7)
#define UART_LCRH_WLEN_8BIT     (3U << 5)
#define UART_LCRH_WLEN_7BIT     (2U << 5)
#define UART_LCRH_WLEN_6BIT     (1U << 5)
#define UART_LCRH_WLEN_5BIT     (0U << 5)
#define UART_LCRH_FEN           (1U << 4)   /* FIFO Enable                     */
#define UART_LCRH_STP2          (1U << 3)
#define UART_LCRH_EPS           (1U << 2)
#define UART_LCRH_PEN           (1U << 1)
#define UART_LCRH_BRK           (1U << 0)

/* ========================================================================== */
/*  Control Register (CR) Bits                                                 */
/* ========================================================================== */

#define UART_CR_RXE             (1U << 9)   /* Receive Enable                  */
#define UART_CR_TXE             (1U << 8)   /* Transmit Enable                 */
#define UART_CR_UARTEN          (1U << 0)   /* UART Enable                     */

/* ========================================================================== */
/*  Clock                                                                      */
/* ========================================================================== */

#define UART_CLOCK_HZ           (48000000UL)

/* ========================================================================== */
/*  API                                                                        */
/* ========================================================================== */

/**
 * @brief Initialize UART0 at the specified baud rate (8N1, FIFO enabled).
 *
 * Sequence per PL011 TRM §3.3:
 *   1. Configure GPIO14/15 to ALT0
 *   2. Disable UART
 *   3. Clear interrupts
 *   4. Set IBRD/FBRD (with rounding)
 *   5. Write LCRH (latches baud registers)
 *   6. Enable UART
 */
static void uart_init(uint32_t baud)
{
    uint32_t divisor_fixed;
    uint32_t ibrd;
    uint32_t fbrd;

    /* Step 1: GPIO14 → TXD0 (ALT0), GPIO15 → RXD0 (ALT0) */
    gpio_set_function(14U, GPIO_FSEL_ALT0);
    gpio_set_function(15U, GPIO_FSEL_ALT0);
    gpio_set_pull(14U, GPIO_PUD_NONE);   /* TX: no pull                        */
    gpio_set_pull(15U, GPIO_PUD_UP);     /* RX: pull-up to avoid float         */

    /* Step 2: Disable UART before reconfiguring */
    UART0_CR = 0U;

    /* Step 3: Clear all pending interrupts */
    UART0_ICR = 0x7FFU;

    /*
     * Step 4: Calculate baud rate divisors  (PL011 TRM §3.3.6)
     *
     * [BUG-U-01] FIX: Add rounding to avoid truncation error.
     *
     * Formula:  divisor_x64 = (UART_CLK × 4) / baud
     *           → this gives a value in units of 1/64 of the baud divisor
     *           → ibrd = divisor_x64 / 64
     *           → fbrd = divisor_x64 % 64
     *
     * Without rounding:  192000000 / 115200 = 1666  → fbrd = 1666%64 = 2  ✗
     * With rounding:    (192000000 + 57600)  / 115200 = 1667 → fbrd = 3   ✓
     *
     * The +baud/2 term shifts the truncation point to nearest-integer.
     */
    divisor_fixed = ((UART_CLOCK_HZ * 4U) + (baud / 2U)) / baud;
    ibrd = divisor_fixed / 64U;
    fbrd = divisor_fixed % 64U;

    UART0_IBRD = ibrd;
    UART0_FBRD = fbrd;

    /*
     * Step 5: Line control — 8N1, FIFO enabled.
     * LCRH write MUST follow IBRD/FBRD — it latches the baud rate registers
     * (PL011 TRM §3.3.6 Note).
     */
    UART0_LCRH = UART_LCRH_WLEN_8BIT | UART_LCRH_FEN;

    /* Step 6: Enable UART — TX + RX + master enable */
    UART0_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

/**
 * @brief Send one character. Blocks until TX FIFO has space.
 */
static void uart_putc(char c)
{
    while (UART0_FR & UART_FR_TXFF) {}
    UART0_DR = (uint32_t)c;
}

/**
 * @brief Send a null-terminated string. Translates \n → \r\n.
 */
static void uart_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str);
        str++;
    }
}

/**
 * @brief Receive one character. Blocks until RX FIFO has data.
 */
static char uart_getc(void)
{
    while (UART0_FR & UART_FR_RXFE) {}
    return (char)(UART0_DR & 0xFFU);
}

/**
 * @brief Send a 32-bit value as 8 uppercase hex digits (no prefix).
 *
 * [BUG-U-02] FIX: Removed the internal "0x" prefix.
 *
 * Original uart_put_hex() prepended "0x" automatically.
 * i2c-app.c called:  uart_puts("Target 0x");  uart_put_hex(addr);
 * Producing:         "Target 0x0x00000050"   ← double prefix
 *
 * Now uart_put_hex() prints only the 8 hex digits.
 * Callers add "0x" prefix in their uart_puts() call as needed.
 * Example:  uart_puts("0x");  uart_put_hex(val);  → "0x00000050"
 */
static void uart_put_hex(uint32_t val)
{
    const char hex[] = "0123456789ABCDEF";
    int i;

    /* 8 hex digits, MSB first — NO "0x" prefix (caller's responsibility) */
    for (i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0x0FU]);
    }
}

#endif /* UART_H */
