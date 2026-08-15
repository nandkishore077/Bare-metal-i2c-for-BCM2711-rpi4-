/**
 * @file    i2c-driver.c
 * @brief   Bare-metal I2C (BSC) driver for Raspberry Pi 4 (BCM2711)
 * @target  Raspberry Pi 4 (BCM2711), bare-metal, AArch64, GCC toolchain
 *
 * Logical fixes applied:
 *   [C-01] Init sequence: disable->clearFIFO->clearS->A=0->DIV->I2CEN
 *   [C-02] DEL/CLKT registers not writable on BSC1 — writes skipped
 *   [C-03] GIC: Group 0 enabled (bare-metal, no TrustZone)
 *   [C-04] IRQ handler: ST bit cleared when disabling interrupts
 *   [C-05] TX: FIFO cleared before prefill
 *   [C-06] SetClockSpeed: lookup table replaces runtime division
 *   [C-07] uart_printf: division-free using pow10 subtraction table
 */

#include "i2c-driver.h"
#include "uart.h"
#include "gpio.h"

/* =========================================================================
 *  Bare-metal print helpers — NO variadic args, NO va_list, NO stack issues.
 *
 *  Why: __builtin_va_list on AArch64 with -O2 requires 16-byte aligned stack.
 *  The bare-metal stack may only be 8-byte aligned at function entry depending
 *  on call depth, causing an alignment fault inside va_start.
 *
 *  Solution: Replace uart_printf with simple non-variadic helpers.
 *    uart_print(str)      — print a string literal
 *    uart_print_hex(val)  — print 8 hex digits (no prefix)
 *    uart_print_uint(val) — print decimal using pow10 subtraction (no division)
 * ========================================================================= */

static void uart_print(const char *s)
{
    uart_puts(s);
}

static void uart_print_hex(uint32_t val)
{
    const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4)
    {
        uart_putc(hex[(val >> (uint32_t)i) & 0xFU]);
    }
}

static void uart_print_uint(uint32_t val)
{
    static const uint32_t pow10[10] = {
        1000000000U, 100000000U, 10000000U, 1000000U,
        100000U, 10000U, 1000U, 100U, 10U, 1U
    };
    uint32_t pi;
    bool     lead = true;

    if (val == 0U) { uart_putc('0'); return; }

    for (pi = 0U; pi < 10U; pi++)
    {
        uint32_t d = 0U;
        while (val >= pow10[pi]) { val -= pow10[pi]; d++; }
        if (d > 0U || !lead) { uart_putc((char)('0' + d)); lead = false; }
    }
}

/* Convenience macro: uart_printf("text") -> uart_print("text")
 * For calls with arguments, use uart_print + uart_print_hex/uint inline. */
#define uart_printf_str(s)         uart_print(s)
#define uart_printf_hex(s, v)      do { uart_print(s); uart_print_hex(v);  uart_putc('\n'); } while(0)
#define uart_printf_uint(s, v)     do { uart_print(s); uart_print_uint(v); uart_putc('\n'); } while(0)

/* =========================================================================
 *  Private: base address lookup table
 *  Index == I2C_InstanceType enum value
 * ========================================================================= */
static const uint32_t I2C_BaseAddrLookup[I2C_MAX_INSTANCES] = {
    I2C_BSC0_BASE_ADDR,
    I2C_BSC1_BASE_ADDR,
    I2C_BSC3_BASE_ADDR,
    I2C_BSC4_BASE_ADDR,
    I2C_BSC5_BASE_ADDR,
    I2C_BSC6_BASE_ADDR
};

static inline bool I2C_IsInstanceValid(I2C_InstanceType Instance)
{
    return ((uint32_t)Instance < I2C_MAX_INSTANCES);
}

/* =========================================================================
 *  [C1] I2C_Init
 * ========================================================================= */
I2C_StatusType I2C_Init(I2C_HandleType *pHandle, const I2C_ConfigType *pConfig)
{
    uint32_t             baseAddr;
    volatile I2C_RegMap *pReg;

    uart_print("[I2C_Init] Entry\n");

    if ((pHandle == NULL) || (pConfig == NULL))
    {
        uart_print("[I2C_Init] ERROR: NULL pointer\n");
        return I2C_STATUS_INVALID_PARAM;
    }

    if (!I2C_IsInstanceValid(pConfig->Instance))
    {
        uart_print("[I2C_Init] ERROR: invalid instance\n");
        return I2C_STATUS_INVALID_PARAM;
    }

    baseAddr = I2C_BaseAddrLookup[(uint32_t)pConfig->Instance];
    pReg     = (volatile I2C_RegMap *)(uintptr_t)baseAddr;

    uart_print("[I2C_Init] BSC instance "); uart_print_uint((uint32_t)pConfig->Instance); uart_print("  base=0x"); uart_print_hex(baseAddr); uart_putc('\n');

    /* GPIO ALT0 + pull-up for BSC1 (GPIO2=SDA1, GPIO3=SCL1) */
    if (pConfig->Instance == I2C_INSTANCE_BSC1)
    {
        gpio_set_function(2U, GPIO_FSEL_ALT0);
        uart_print("[I2C_Init] GPIO2 ALT0 done\n");
        gpio_set_function(3U, GPIO_FSEL_ALT0);
        uart_print("[I2C_Init] GPIO3 ALT0 done\n");
        gpio_set_pull(2U, GPIO_PUD_UP);
        uart_print("[I2C_Init] GPIO2 pull-up done\n");
        gpio_set_pull(3U, GPIO_PUD_UP);
        uart_print("[I2C_Init] GPIO3 pull-up done\n");
        __asm__ volatile ("dsb sy" ::: "memory");
        uart_print("[I2C_Init] dsb done\n");
    }

    /* Populate handle — field by field to avoid ldp/stp alignment fault */
    uart_print("[I2C_Init] writing pReg\n");
    pHandle->pReg     = pReg;
    uart_print("[I2C_Init] writing BaseAddr\n");
    pHandle->BaseAddr = (uintptr_t)baseAddr;
    uart_print("[I2C_Init] writing Config.Instance\n");
    pHandle->Config.Instance             = pConfig->Instance;
    uart_print("[I2C_Init] writing Config.ClockDivider\n");
    pHandle->Config.ClockDivider         = pConfig->ClockDivider;
    uart_print("[I2C_Init] writing Config.RisingEdgeDelay\n");
    pHandle->Config.RisingEdgeDelay      = pConfig->RisingEdgeDelay;
    uart_print("[I2C_Init] writing Config.FallingEdgeDelay\n");
    pHandle->Config.FallingEdgeDelay     = pConfig->FallingEdgeDelay;
    uart_print("[I2C_Init] writing Config.ClockStretchTimeout\n");
    pHandle->Config.ClockStretchTimeout  = pConfig->ClockStretchTimeout;
    uart_print("[I2C_Init] writing State\n");
    pHandle->State                       = I2C_STATE_RESET;
    uart_print("[I2C_Init] writing pTxBuffer\n");
    pHandle->pTxBuffer                   = NULL;
    uart_print("[I2C_Init] writing TxSize/TxCount\n");
    pHandle->TxSize                      = 0U;
    pHandle->TxCount                     = 0U;
    uart_print("[I2C_Init] writing pRxBuffer\n");
    pHandle->pRxBuffer                   = NULL;
    uart_print("[I2C_Init] writing RxSize/RxCount\n");
    pHandle->RxSize                      = 0U;
    pHandle->RxCount                     = 0U;
    uart_print("[I2C_Init] writing SlaveAddr/ErrorCode/Callback\n");
    pHandle->SlaveAddr                   = 0U;
    pHandle->ErrorCode                   = I2C_ERROR_NONE;
    pHandle->Callback                    = NULL;
    uart_print("[I2C_Init] handle populated\n");

    /* Init sequence per BCM2711 TRM */
    uart_print("[I2C_Init] writing C=0\n");
    I2C_REG_WRITE(pReg, C, 0x00000000U);
    uart_print("[I2C_Init] C=0 done\n");

    uart_print("[I2C_Init] writing C=CLEAR\n");
    I2C_REG_WRITE(pReg, C, I2C_C_CLEAR_MASK);
    uart_print("[I2C_Init] C=CLEAR done\n");

    uart_print("[I2C_Init] writing S=W1C\n");
    I2C_REG_WRITE(pReg, S, I2C_S_W1C_MASK);
    uart_print("[I2C_Init] S=W1C done\n");

    uart_print("[I2C_Init] writing A=0\n");
    I2C_REG_WRITE(pReg, A, 0x00000000U);
    uart_print("[I2C_Init] A=0 done\n");

    uart_print("[I2C_Init] writing DIV=0x"); uart_print_hex((uint32_t)pConfig->ClockDivider); uart_putc('\n');
    I2C_REG_WRITE(pReg, DIV, (uint32_t)pConfig->ClockDivider & I2C_CDIV_MASK);
    uart_print("[I2C_Init] DIV done\n");

    /*
     * [C-02] DEL (0x18) and CLKT (0x1C) are NOT writable on BSC1.
     * Writing these offsets stalls the AXI bus — skip entirely.
     */
    uart_print("[I2C_Init] skipping DEL/CLKT (not writable on BSC1)\n");

    uart_print("[I2C_Init] enabling I2CEN\n");
    I2C_REG_WRITE(pReg, C, I2C_C_I2CEN_MASK);
    uart_print("[I2C_Init] I2CEN done\n");

    pHandle->State = I2C_STATE_READY;

    uart_print("[I2C_Init] OK  C=0x"); uart_print_hex(I2C_REG_READ(pReg, C)); uart_print("  S=0x"); uart_print_hex(I2C_REG_READ(pReg, S)); uart_putc('\n');

    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [C1] I2C_DeInit
 * ========================================================================= */
I2C_StatusType I2C_DeInit(I2C_HandleType *pHandle)
{
    uart_print("[I2C_DeInit] Entry\n");

    if (pHandle == NULL) { return I2C_STATUS_INVALID_PARAM; }

    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX))
    {
        uart_print("[I2C_DeInit] ERROR: busy\n");
        return I2C_STATUS_BUSY;
    }

    uart_print("[I2C_DeInit] pReg=0x");
    uart_print_hex((uint32_t)(uintptr_t)pHandle->pReg);
    uart_putc('\n');

    /* Step 1: Keep I2CEN=1, clear FIFO (CLEAR only works with I2CEN=1) */
    uart_print("[I2C_DeInit] writing I2CEN+CLEAR\n");
    I2C_REG_WRITE(pHandle->pReg, C, I2C_C_I2CEN_MASK | I2C_C_CLEAR_MASK);
    uart_print("[I2C_DeInit] FIFO cleared\n");

    /* Step 2: Clear all W1C status flags */
    uart_print("[I2C_DeInit] writing S=W1C\n");
    I2C_REG_WRITE(pHandle->pReg, S, I2C_S_W1C_MASK);
    uart_print("[I2C_DeInit] S cleared\n");

    /* Step 3: Disable controller */
    uart_print("[I2C_DeInit] writing C=0\n");
    I2C_REG_WRITE(pHandle->pReg, C, 0x00000000U);
    uart_print("[I2C_DeInit] C=0 done\n");

    /*
     * Null pReg immediately after disabling — prevents any spurious
     * interrupt or re-entrant code from accessing the peripheral again.
     */
    pHandle->pReg   = NULL;
    pHandle->State  = I2C_STATE_RESET;
    uart_print("[I2C_DeInit] state reset\n");

    pHandle->pTxBuffer = NULL;
    pHandle->TxSize    = 0U;
    pHandle->TxCount   = 0U;
    pHandle->pRxBuffer = NULL;
    pHandle->RxSize    = 0U;
    pHandle->RxCount   = 0U;
    pHandle->SlaveAddr = 0U;
    pHandle->ErrorCode = I2C_ERROR_NONE;
    pHandle->Callback  = NULL;
    uart_print("[I2C_DeInit] OK\n");
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [C3] I2C_GetError
 * ========================================================================= */
I2C_ErrorFlagType I2C_GetError(const I2C_HandleType *pHandle)
{
    uint32_t          status;
    I2C_ErrorFlagType errors = I2C_ERROR_NONE;

    if (pHandle == NULL) { return I2C_ERROR_NONE; }

    status = I2C_REG_READ(pHandle->pReg, S);

    if ((status & I2C_S_ERR_MASK)  != 0U)
        errors = (I2C_ErrorFlagType)((uint32_t)errors | (uint32_t)I2C_ERROR_NACK);
    if ((status & I2C_S_CLKT_MASK) != 0U)
        errors = (I2C_ErrorFlagType)((uint32_t)errors | (uint32_t)I2C_ERROR_CLKT);

    uart_print("[I2C_GetError] S=0x"); uart_print_hex(status); uart_print(" errors=0x"); uart_print_hex((uint32_t)errors); uart_putc('\n');
    return errors;
}

/* =========================================================================
 *  [C3] I2C_ClearError
 * ========================================================================= */
I2C_StatusType I2C_ClearError(I2C_HandleType *pHandle, I2C_ErrorFlagType ErrorFlags)
{
    uint32_t clearBits = 0U;

    if (pHandle == NULL)           { return I2C_STATUS_INVALID_PARAM; }
    if (ErrorFlags == I2C_ERROR_NONE) { return I2C_STATUS_INVALID_PARAM; }

    if (((uint32_t)ErrorFlags & (uint32_t)I2C_ERROR_NACK) != 0U)
        clearBits |= I2C_S_ERR_MASK;
    if (((uint32_t)ErrorFlags & (uint32_t)I2C_ERROR_CLKT) != 0U)
        clearBits |= I2C_S_CLKT_MASK;

    I2C_REG_WRITE(pHandle->pReg, S, clearBits);
    pHandle->ErrorCode = I2C_ERROR_NONE;
    if (pHandle->State == I2C_STATE_ERROR) { pHandle->State = I2C_STATE_READY; }

    uart_print("[I2C_ClearError] cleared 0x"); uart_print_hex(clearBits); uart_putc('\n');
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [C3] I2C_GetStatus
 * ========================================================================= */
I2C_StatusType I2C_GetStatus(const I2C_HandleType *pHandle, I2C_StatusInfoType *pStatusInfo)
{
    uint32_t status;

    if ((pHandle == NULL) || (pStatusInfo == NULL)) { return I2C_STATUS_INVALID_PARAM; }

    status = I2C_REG_READ(pHandle->pReg, S);

    pStatusInfo->TransferActive      = ((status & I2C_S_TA_MASK)   != 0U);
    pStatusInfo->TransferDone        = ((status & I2C_S_DONE_MASK) != 0U);
    pStatusInfo->TxFifoEmpty         = ((status & I2C_S_TXE_MASK)  != 0U);
    pStatusInfo->RxFifoFull          = ((status & I2C_S_RXF_MASK)  != 0U);
    pStatusInfo->TxFifoHasSpace      = ((status & I2C_S_TXD_MASK)  != 0U);
    pStatusInfo->RxFifoHasData       = ((status & I2C_S_RXD_MASK)  != 0U);
    pStatusInfo->TxFifoNeedsWrite    = ((status & I2C_S_TXW_MASK)  != 0U);
    pStatusInfo->RxFifoNeedsRead     = ((status & I2C_S_RXR_MASK)  != 0U);
    pStatusInfo->AckError            = ((status & I2C_S_ERR_MASK)  != 0U);
    pStatusInfo->ClockStretchTimeout = ((status & I2C_S_CLKT_MASK) != 0U);

    uart_print("[I2C_GetStatus] S=0x"); uart_print_hex(status); uart_putc('\n');
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O1] I2C_MasterTransmit_IT
 * ========================================================================= */
I2C_StatusType I2C_MasterTransmit_IT(I2C_HandleType *pHandle,
                                      uint8_t SlaveAddr,
                                      const uint8_t *pData,
                                      uint16_t Size)
{
    volatile I2C_RegMap *pReg;
    uint32_t             ctrlVal;
    uint16_t             prefill;
    uint16_t             i;

    uart_print("[I2C_TX_IT] slave=0x"); uart_print_hex((uint32_t)SlaveAddr); uart_print(" size="); uart_print_uint((uint32_t)Size); uart_putc('\n');

    if ((pHandle == NULL) || (pData == NULL))      { return I2C_STATUS_INVALID_PARAM; }
    if ((Size == 0U) || (SlaveAddr > (uint8_t)I2C_SLAVE_ADDR_MASK))
                                                   { return I2C_STATUS_INVALID_PARAM; }
    if (pHandle->State != I2C_STATE_READY)         { return I2C_STATUS_BUSY; }

    pReg = pHandle->pReg;

    pHandle->State     = I2C_STATE_BUSY_TX;
    pHandle->pTxBuffer = pData;
    pHandle->TxSize    = Size;
    pHandle->TxCount   = 0U;
    pHandle->SlaveAddr = SlaveAddr;
    pHandle->ErrorCode = I2C_ERROR_NONE;

    I2C_REG_WRITE(pReg, S,    I2C_S_W1C_MASK);
    I2C_REG_WRITE(pReg, A,    (uint32_t)SlaveAddr & I2C_SLAVE_ADDR_MASK);
    I2C_REG_WRITE(pReg, DLEN, (uint32_t)Size);

    /* [C-05] Clear FIFO before prefill */
    I2C_REG_WRITE(pReg, C, I2C_C_I2CEN_MASK | I2C_C_CLEAR_MASK);

    prefill = (Size < (uint16_t)I2C_FIFO_SIZE) ? Size : (uint16_t)I2C_FIFO_SIZE;
    for (i = 0U; i < prefill; i++)
    {
        I2C_REG_WRITE(pReg, FIFO, (uint32_t)pData[i] & I2C_FIFO_DATA_MASK);
    }
    pHandle->TxCount = prefill;

    ctrlVal = I2C_C_I2CEN_MASK | I2C_C_ST_MASK | I2C_C_INTD_MASK;
    if (pHandle->TxCount < pHandle->TxSize) { ctrlVal |= I2C_C_INTT_MASK; }
    I2C_REG_WRITE(pReg, C, ctrlVal);

    uart_print("[I2C_TX_IT] started C=0x"); uart_print_hex(ctrlVal); uart_putc('\n');
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O1] I2C_MasterReceive_IT
 * ========================================================================= */
I2C_StatusType I2C_MasterReceive_IT(I2C_HandleType *pHandle,
                                     uint8_t SlaveAddr,
                                     uint8_t *pData,
                                     uint16_t Size)
{
    volatile I2C_RegMap *pReg;
    uint32_t             ctrlVal;

    uart_print("[I2C_RX_IT] slave=0x"); uart_print_hex((uint32_t)SlaveAddr); uart_print(" size="); uart_print_uint((uint32_t)Size); uart_putc('\n');

    if ((pHandle == NULL) || (pData == NULL))      { return I2C_STATUS_INVALID_PARAM; }
    if ((Size == 0U) || (SlaveAddr > (uint8_t)I2C_SLAVE_ADDR_MASK))
                                                   { return I2C_STATUS_INVALID_PARAM; }
    if (pHandle->State != I2C_STATE_READY)         { return I2C_STATUS_BUSY; }

    pReg = pHandle->pReg;

    pHandle->State     = I2C_STATE_BUSY_RX;
    pHandle->pRxBuffer = pData;
    pHandle->RxSize    = Size;
    pHandle->RxCount   = 0U;
    pHandle->SlaveAddr = SlaveAddr;
    pHandle->ErrorCode = I2C_ERROR_NONE;

    I2C_REG_WRITE(pReg, S,    I2C_S_W1C_MASK);
    I2C_REG_WRITE(pReg, A,    (uint32_t)SlaveAddr & I2C_SLAVE_ADDR_MASK);
    I2C_REG_WRITE(pReg, DLEN, (uint32_t)Size);

    ctrlVal = I2C_C_I2CEN_MASK | I2C_C_ST_MASK | I2C_C_CLEAR_MASK |
              I2C_C_READ_MASK  | I2C_C_INTD_MASK | I2C_C_INTR_MASK;
    I2C_REG_WRITE(pReg, C, ctrlVal);

    uart_print("[I2C_RX_IT] started C=0x"); uart_print_hex(ctrlVal); uart_putc('\n');
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O1] I2C_IRQHandler
 * ========================================================================= */
void I2C_IRQHandler(I2C_HandleType *pHandle)
{
    volatile I2C_RegMap *pReg;
    uint32_t             status;
    uint32_t             clearFlags = 0U;
    uint32_t             ctrlVal;
    bool                 errorDetected = false;

    if (pHandle == NULL) { return; }

    pReg   = pHandle->pReg;
    status = I2C_REG_READ(pReg, S);

    uart_print("[I2C_IRQ] S=0x"); uart_print_hex(status); uart_putc('\n');

    /* Error check */
    if ((status & I2C_S_ERR_MASK) != 0U)
    {
        pHandle->ErrorCode = (I2C_ErrorFlagType)(
            (uint32_t)pHandle->ErrorCode | (uint32_t)I2C_ERROR_NACK);
        clearFlags    |= I2C_S_ERR_MASK;
        errorDetected  = true;
        uart_print("[I2C_IRQ] NACK\n");
    }
    if ((status & I2C_S_CLKT_MASK) != 0U)
    {
        pHandle->ErrorCode = (I2C_ErrorFlagType)(
            (uint32_t)pHandle->ErrorCode | (uint32_t)I2C_ERROR_CLKT);
        clearFlags    |= I2C_S_CLKT_MASK;
        errorDetected  = true;
        uart_print("[I2C_IRQ] CLKT\n");
    }

    if (errorDetected)
    {
        /* [C-04] Clear ST when disabling interrupts */
        ctrlVal  = I2C_REG_READ(pReg, C);
        ctrlVal &= ~(I2C_C_INT_ALL_MASK | I2C_C_ST_MASK);
        I2C_REG_WRITE(pReg, C, ctrlVal);
        pHandle->State = I2C_STATE_ERROR;
        if ((status & I2C_S_DONE_MASK) != 0U) { clearFlags |= I2C_S_DONE_MASK; }
        I2C_REG_WRITE(pReg, S, clearFlags);
        if (pHandle->Callback != NULL)
        {
            if (((uint32_t)pHandle->ErrorCode & (uint32_t)I2C_ERROR_NACK) != 0U)
                pHandle->Callback(pHandle, I2C_EVENT_ERROR_NACK);
            if (((uint32_t)pHandle->ErrorCode & (uint32_t)I2C_ERROR_CLKT) != 0U)
                pHandle->Callback(pHandle, I2C_EVENT_ERROR_CLKT);
        }
        return;
    }

    /* TX: refill FIFO */
    if ((pHandle->State == I2C_STATE_BUSY_TX) && ((status & I2C_S_TXW_MASK) != 0U))
    {
        while ((pHandle->TxCount < pHandle->TxSize) &&
               ((I2C_REG_READ(pReg, S) & I2C_S_TXD_MASK) != 0U))
        {
            I2C_REG_WRITE(pReg, FIFO,
                          (uint32_t)pHandle->pTxBuffer[pHandle->TxCount] & I2C_FIFO_DATA_MASK);
            pHandle->TxCount++;
        }
        if (pHandle->TxCount >= pHandle->TxSize)
        {
            ctrlVal  = I2C_REG_READ(pReg, C);
            ctrlVal &= ~I2C_C_INTT_MASK;
            I2C_REG_WRITE(pReg, C, ctrlVal);
        }
        uart_print("[I2C_IRQ] TX done\n");
    }

    /* RX: drain FIFO */
    if ((pHandle->State == I2C_STATE_BUSY_RX) && ((status & I2C_S_RXR_MASK) != 0U))
    {
        while ((pHandle->RxCount < pHandle->RxSize) &&
               ((I2C_REG_READ(pReg, S) & I2C_S_RXD_MASK) != 0U))
        {
            pHandle->pRxBuffer[pHandle->RxCount] =
                (uint8_t)(I2C_REG_READ(pReg, FIFO) & I2C_FIFO_DATA_MASK);
            pHandle->RxCount++;
        }
        uart_print("[I2C_IRQ] RX data\n");
    }

    /* DONE */
    if ((status & I2C_S_DONE_MASK) != 0U)
    {
        clearFlags |= I2C_S_DONE_MASK;

        /* [C-04] Clear ST when disabling interrupts */
        ctrlVal  = I2C_REG_READ(pReg, C);
        ctrlVal &= ~(I2C_C_INT_ALL_MASK | I2C_C_ST_MASK);
        I2C_REG_WRITE(pReg, C, ctrlVal);

        /* Drain remaining RX bytes */
        if (pHandle->State == I2C_STATE_BUSY_RX)
        {
            while ((pHandle->RxCount < pHandle->RxSize) &&
                   ((I2C_REG_READ(pReg, S) & I2C_S_RXD_MASK) != 0U))
            {
                pHandle->pRxBuffer[pHandle->RxCount] =
                    (uint8_t)(I2C_REG_READ(pReg, FIFO) & I2C_FIFO_DATA_MASK);
                pHandle->RxCount++;
            }
        }

        I2C_EventType ev = (pHandle->State == I2C_STATE_BUSY_TX)
                           ? I2C_EVENT_TX_COMPLETE
                           : I2C_EVENT_RX_COMPLETE;

        pHandle->State = I2C_STATE_READY;
        I2C_REG_WRITE(pReg, S, clearFlags);

        uart_print("[I2C_IRQ] DONE\n");

        if (pHandle->Callback != NULL) { pHandle->Callback(pHandle, ev); }
        return;
    }

    if (clearFlags != 0U) { I2C_REG_WRITE(pReg, S, clearFlags); }
}

/* =========================================================================
 *  [O3] I2C_SetClockSpeed
 *  [C-06] Lookup table replaces runtime division (no libgcc __udivsi3)
 * ========================================================================= */
I2C_StatusType I2C_SetClockSpeed(I2C_HandleType *pHandle, uint32_t SclFrequencyHz)
{
    uint32_t cdiv;

    uart_print("[I2C_SetClock] entry\n");

    if (pHandle == NULL)      { return I2C_STATUS_INVALID_PARAM; }
    if (SclFrequencyHz == 0U) { return I2C_STATUS_INVALID_PARAM; }
    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX)) { return I2C_STATUS_BUSY; }

    if (SclFrequencyHz == 400000U)
        cdiv = I2C_CDIV_400KHZ;
    else if (SclFrequencyHz == 100000U)
        cdiv = I2C_CDIV_100KHZ;
    else
    {
        cdiv = I2C_CDIV_100KHZ;
        uart_print("[I2C_SetClock] non-standard freq, defaulting 100kHz\n");
    }
    cdiv &= ~1U; /* must be even */
    if (cdiv < 2U)    { cdiv = 2U; }
    if (cdiv > 0xFFFEU) { cdiv = 0xFFFEU; }

    I2C_REG_WRITE(pHandle->pReg, DIV, cdiv & I2C_CDIV_MASK);
    pHandle->Config.ClockDivider = (uint16_t)cdiv;

    uart_print("[I2C_SetClock] CDIV=0x"); uart_print_hex(cdiv); uart_putc('\n');
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O3] I2C_SetDataDelay
 *  [C-02] DEL not writable on BSC1 — store in config only
 * ========================================================================= */
I2C_StatusType I2C_SetDataDelay(I2C_HandleType *pHandle,
                                 uint16_t RisingEdgeDelay,
                                 uint16_t FallingEdgeDelay)
{
    uint16_t halfCdiv;

    uart_print("[I2C_SetDataDelay] entry\n");

    if (pHandle == NULL) { return I2C_STATUS_INVALID_PARAM; }
    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX)) { return I2C_STATUS_BUSY; }

    halfCdiv = (pHandle->Config.ClockDivider == 0U)
               ? 16384U
               : (uint16_t)(pHandle->Config.ClockDivider >> 1U);

    if ((RisingEdgeDelay >= halfCdiv) || (FallingEdgeDelay >= halfCdiv))
    {
        uart_print("[I2C_SetDataDelay] ERROR: delay >= CDIV/2\n");
        return I2C_STATUS_INVALID_PARAM;
    }

    /* DEL register not writable on BSC1 — store values only */
    pHandle->Config.RisingEdgeDelay  = RisingEdgeDelay;
    pHandle->Config.FallingEdgeDelay = FallingEdgeDelay;

    uart_print("[I2C_SetDataDelay] OK (config only, BSC1 DEL not writable)\n");
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O3] I2C_SetClockStretchTimeout
 *  [C-02] CLKT not writable on BSC1 — store in config only
 * ========================================================================= */
I2C_StatusType I2C_SetClockStretchTimeout(I2C_HandleType *pHandle, uint16_t TimeoutScl)
{
    uart_print("[I2C_SetClkTO] entry\n");

    if (pHandle == NULL) { return I2C_STATUS_INVALID_PARAM; }
    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX)) { return I2C_STATUS_BUSY; }

    /* CLKT register not writable on BSC1 — store value only */
    pHandle->Config.ClockStretchTimeout = TimeoutScl;

    uart_print("[I2C_SetClkTO] OK (config only, BSC1 CLKT not writable)\n");
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O4] I2C_GIC_Init
 *  [C-03] Group 0 enabled for bare-metal AArch64 (no TrustZone)
 * ========================================================================= */
I2C_StatusType I2C_GIC_Init(const I2C_GIC_ConfigType *pGicConfig)
{
    uint32_t distBase;
    uint32_t cpuIfBase;
    uint32_t spiId;
    uint32_t regIndex;
    uint32_t bitOffset;
    uint32_t byteOffset;
    uint32_t regVal;

    if (pGicConfig == NULL)                                     { return I2C_STATUS_INVALID_PARAM; }
    if ((pGicConfig->GicBaseAddr == 0U) ||
        (pGicConfig->TargetCpuMask == 0U))                      { return I2C_STATUS_INVALID_PARAM; }

    distBase  = pGicConfig->GicBaseAddr + I2C_GIC400_DIST_OFFSET;
    cpuIfBase = pGicConfig->GicBaseAddr + I2C_GIC400_CPUIF_OFFSET;
    spiId     = pGicConfig->SpiInterruptId;

    uart_print("[I2C_GIC_Init] GICD=0x"); uart_print_hex(distBase); uart_print(" GICC=0x"); uart_print_hex(cpuIfBase); uart_putc('\n');

    /* Step 1: Enable distributor Group 0 */
    regVal  = I2C_MMIO_READ(distBase + I2C_GICD_CTLR_OFFSET);
    regVal |= I2C_GICD_CTLR_ENABLE_GRP0;
    I2C_MMIO_WRITE(distBase + I2C_GICD_CTLR_OFFSET, regVal);

    /* Step 2: Set interrupt as Group 0 (clear bit = Group 0) */
    regIndex  = spiId >> 5U;   /* spiId / 32 */
    bitOffset = spiId & 0x1FU; /* spiId % 32 */
    regVal = I2C_MMIO_READ(distBase + I2C_GICD_IGROUPR_OFFSET + (regIndex << 2U));
    regVal &= ~(1U << bitOffset);
    I2C_MMIO_WRITE(distBase + I2C_GICD_IGROUPR_OFFSET + (regIndex << 2U), regVal);

    /* Step 3: Enable SPI */
    I2C_MMIO_WRITE(distBase + I2C_GICD_ISENABLER_OFFSET + (regIndex << 2U),
                   (1U << bitOffset));

    /* Step 4: Priority (4 per register) */
    regIndex   = spiId >> 2U;   /* spiId / 4 */
    byteOffset = spiId & 0x3U;  /* spiId % 4 */
    regVal = I2C_MMIO_READ(distBase + I2C_GICD_IPRIORITYR_OFFSET + (regIndex << 2U));
    regVal &= ~(0xFFU << (byteOffset << 3U));
    regVal |= ((uint32_t)pGicConfig->Priority << (byteOffset << 3U));
    I2C_MMIO_WRITE(distBase + I2C_GICD_IPRIORITYR_OFFSET + (regIndex << 2U), regVal);

    /* Step 5: CPU target */
    regVal = I2C_MMIO_READ(distBase + I2C_GICD_ITARGETSR_OFFSET + (regIndex << 2U));
    regVal &= ~(0xFFU << (byteOffset << 3U));
    regVal |= ((uint32_t)pGicConfig->TargetCpuMask << (byteOffset << 3U));
    I2C_MMIO_WRITE(distBase + I2C_GICD_ITARGETSR_OFFSET + (regIndex << 2U), regVal);

    /* Step 6: Level-sensitive (2 bits per interrupt, clear both = level) */
    regIndex  = spiId >> 4U;        /* spiId / 16 */
    bitOffset = (spiId & 0xFU) << 1U; /* (spiId % 16) * 2 */
    regVal = I2C_MMIO_READ(distBase + I2C_GICD_ICFGR_OFFSET + (regIndex << 2U));
    regVal &= ~(3U << bitOffset);
    I2C_MMIO_WRITE(distBase + I2C_GICD_ICFGR_OFFSET + (regIndex << 2U), regVal);

    /* Step 7: Enable CPU interface */
    regVal  = I2C_MMIO_READ(cpuIfBase + I2C_GICC_CTLR_OFFSET);
    regVal |= I2C_GICC_CTLR_ENABLE;
    I2C_MMIO_WRITE(cpuIfBase + I2C_GICC_CTLR_OFFSET, regVal);

    /* Step 8: Allow all priorities */
    I2C_MMIO_WRITE(cpuIfBase + I2C_GICC_PMR_OFFSET, (uint32_t)I2C_GICC_PMR_ALLOW_ALL);

    uart_print("[I2C_GIC_Init] OK\n");
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O4] I2C_GIC_DeInit
 * ========================================================================= */
I2C_StatusType I2C_GIC_DeInit(const I2C_GIC_ConfigType *pGicConfig)
{
    uint32_t distBase;
    uint32_t spiId;
    uint32_t regIndex;
    uint32_t bitOffset;

    if (pGicConfig == NULL) { return I2C_STATUS_INVALID_PARAM; }

    distBase = pGicConfig->GicBaseAddr + I2C_GIC400_DIST_OFFSET;
    spiId    = pGicConfig->SpiInterruptId;

    regIndex  = spiId >> 5U;
    bitOffset = spiId & 0x1FU;
    I2C_MMIO_WRITE(distBase + I2C_GICD_ICENABLER_OFFSET + (regIndex << 2U),
                   (1U << bitOffset));

    uart_print("[I2C_GIC_DeInit] SPI disabled\n");
    return I2C_STATUS_OK;
}

/* =========================================================================
 *  [O5] I2C_RegisterCallback / I2C_UnregisterCallback
 * ========================================================================= */
I2C_StatusType I2C_RegisterCallback(I2C_HandleType *pHandle, I2C_CallbackType CallbackFn)
{
    if ((pHandle == NULL) || (CallbackFn == NULL)) { return I2C_STATUS_INVALID_PARAM; }
    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX))     { return I2C_STATUS_BUSY; }
    pHandle->Callback = CallbackFn;
    uart_print("[I2C_RegCB] registered\n");
    return I2C_STATUS_OK;
}

I2C_StatusType I2C_UnregisterCallback(I2C_HandleType *pHandle)
{
    if (pHandle == NULL)                           { return I2C_STATUS_INVALID_PARAM; }
    if ((pHandle->State == I2C_STATE_BUSY_TX) ||
        (pHandle->State == I2C_STATE_BUSY_RX))     { return I2C_STATUS_BUSY; }
    pHandle->Callback = NULL;
    uart_print("[I2C_UnregCB] removed\n");
    return I2C_STATUS_OK;
}
