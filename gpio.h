/**
 * @file    gpio.h
 * @brief   GPIO configuration for Raspberry Pi 4 (BCM2711) bare metal
 *
 * Addresses: Verified from /proc/iomem on live hardware
 *   GPIO base: 0xFE200000 (legacy 0x7E200000)
 *
 * BCM2711 uses the NEW pull-up/down register scheme:
 *   GPIO_PUP_PDN_CNTRL_REG0 at offset 0xE4
 *   (NOT the old GPPUD/GPPUDCLK method from BCM2835)
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

/* ============================================================================
 * Base Address (ARM physical, verified from iomem)
 * ========================================================================= */

#define PERI_BASE               (0xFE000000UL)
#define GPIO_BASE               (PERI_BASE + 0x200000UL)    /* 0xFE200000 */

/* ============================================================================
 * Register Access
 * ========================================================================= */

#define GPIO_REG(offset)        (*(volatile uint32_t *)(GPIO_BASE + (offset)))

/* Function Select Registers (3 bits per GPIO, 10 GPIOs per register) */
#define GPFSEL0                 GPIO_REG(0x00)  /* GPIO  0-9  */
#define GPFSEL1                 GPIO_REG(0x04)  /* GPIO 10-19 */
#define GPFSEL2                 GPIO_REG(0x08)  /* GPIO 20-29 */
#define GPFSEL3                 GPIO_REG(0x0C)  /* GPIO 30-39 */
#define GPFSEL4                 GPIO_REG(0x10)  /* GPIO 40-49 */
#define GPFSEL5                 GPIO_REG(0x14)  /* GPIO 50-57 */

/* Output Set/Clear */
#define GPSET0                  GPIO_REG(0x1C)  /* GPIO  0-31 set    */
#define GPSET1                  GPIO_REG(0x20)  /* GPIO 32-57 set    */
#define GPCLR0                  GPIO_REG(0x28)  /* GPIO  0-31 clear  */
#define GPCLR1                  GPIO_REG(0x2C)  /* GPIO 32-57 clear  */

/* Pin Level */
#define GPLEV0                  GPIO_REG(0x34)  /* GPIO  0-31 level  */
#define GPLEV1                  GPIO_REG(0x38)  /* GPIO 32-57 level  */

/* BCM2711 Pull-Up/Down Control (2 bits per GPIO) */
#define GPIO_PUP_PDN_CNTRL0    GPIO_REG(0xE4)  /* GPIO  0-15 */
#define GPIO_PUP_PDN_CNTRL1    GPIO_REG(0xE8)  /* GPIO 16-31 */
#define GPIO_PUP_PDN_CNTRL2    GPIO_REG(0xEC)  /* GPIO 32-47 */
#define GPIO_PUP_PDN_CNTRL3    GPIO_REG(0xF0)  /* GPIO 48-57 */

/* ============================================================================
 * Function Select Values (3-bit field per GPIO)
 * ========================================================================= */

#define GPIO_FSEL_INPUT         (0x00U)
#define GPIO_FSEL_OUTPUT        (0x01U)
#define GPIO_FSEL_ALT0          (0x04U)
#define GPIO_FSEL_ALT1          (0x05U)
#define GPIO_FSEL_ALT2          (0x06U)
#define GPIO_FSEL_ALT3          (0x07U)
#define GPIO_FSEL_ALT4          (0x03U)
#define GPIO_FSEL_ALT5          (0x02U)

/* ============================================================================
 * Pull-Up/Down Values (2-bit field per GPIO)
 * ========================================================================= */

#define GPIO_PUD_NONE           (0x00U)
#define GPIO_PUD_UP             (0x01U)
#define GPIO_PUD_DOWN           (0x02U)

/* ============================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * @brief  Set a GPIO pin's function (input, output, alt0-5)
 * @param  pin   GPIO number (0-57)
 * @param  func  One of GPIO_FSEL_xxx values
 */
static inline void gpio_set_function(uint32_t pin, uint32_t func)
{
    volatile uint32_t *gpfsel = (volatile uint32_t *)(GPIO_BASE + (pin / 10) * 4);
    uint32_t shift = (pin % 10) * 3;
    uint32_t reg = *gpfsel;

    reg &= ~(0x07U << shift);      /* Clear 3-bit field */
    reg |=  (func  << shift);      /* Set new function  */
    *gpfsel = reg;
}

/**
 * @brief  Set a GPIO pin's pull-up/down resistor
 * @param  pin   GPIO number (0-57)
 * @param  pud   One of GPIO_PUD_xxx values
 */
static inline void gpio_set_pull(uint32_t pin, uint32_t pud)
{
    volatile uint32_t *pup_reg = (volatile uint32_t *)(GPIO_BASE + 0xE4 + (pin / 16) * 4);
    uint32_t shift = (pin % 16) * 2;
    uint32_t reg = *pup_reg;

    reg &= ~(0x03U << shift);      /* Clear 2-bit field */
    reg |=  (pud   << shift);      /* Set new pull      */
    *pup_reg = reg;
}

#endif /* GPIO_H */
