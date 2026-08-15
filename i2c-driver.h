/**
 * @file    i2c-driver.h
 * @brief   Bare-metal I2C (BSC) driver for Raspberry Pi 4 (BCM2711)
 * @target  Raspberry Pi 4 (BCM2711), bare-metal, AArch64, GCC toolchain
 *
 * Hardware-verified fixes:
 *   [H-01] BSC base addresses: 0x7Exxxxxx -> 0xFExxxxxx (ARM physical)
 *   [H-02] I2C_CORE_CLOCK_HZ: 150MHz -> 500MHz (vcgencmd verified)
 *   [H-03] I2C_CDIV_100KHZ=5000, I2C_CDIV_400KHZ=1250
 *   [H-04] uintptr_t for BaseAddr (AArch64 pointer width)
 *   [H-05] DEL/CLKT not writable on BSC1 — writes skipped in driver
 */

#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#ifdef __cplusplus///////////////////////////////////////////////////////////////////////////////////////////////////////
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 *  BSC Instance Base Addresses (ARM physical, Low-Peripheral mode)
 *  BCM2711 TRM Table 1-1: ARM phys base = 0xFE000000
 * ========================================================================= */
#define I2C_BSC0_BASE_ADDR      ((uint32_t)0xFE205000U)
#define I2C_BSC1_BASE_ADDR      ((uint32_t)0xFE804000U)
#define I2C_BSC3_BASE_ADDR      ((uint32_t)0xFE205600U)
#define I2C_BSC4_BASE_ADDR      ((uint32_t)0xFE205800U)
#define I2C_BSC5_BASE_ADDR      ((uint32_t)0xFE205A80U)
#define I2C_BSC6_BASE_ADDR      ((uint32_t)0xFE205C00U)
#define I2C_MAX_INSTANCES       6U

/* =========================================================================
 *  BSC Register Map
 * ========================================================================= */
typedef struct {
    volatile uint32_t C;    /* 0x00 Control                                  */
    volatile uint32_t S;    /* 0x04 Status                                   */
    volatile uint32_t DLEN; /* 0x08 Data Length                              */
    volatile uint32_t A;    /* 0x0C Slave Address                            */
    volatile uint32_t FIFO; /* 0x10 Data FIFO                                */
    volatile uint32_t DIV;  /* 0x14 Clock Divider                            */
    volatile uint32_t DEL;  /* 0x18 Data Delay  (BSC1: read-only/reserved)   */
    volatile uint32_t CLKT; /* 0x1C Clock Stretch Timeout (BSC1: reserved)   */
} I2C_RegMap;

#define I2C_REGMAP(base)    ((I2C_RegMap *)(uintptr_t)(base))

/* =========================================================================
 *  Control Register (C) Bit Masks
 * ========================================================================= */
#define I2C_C_I2CEN_MASK    (1U << 15U)////////////////////////////////////////////////////////////////////////////////////////////////
#define I2C_C_INTR_MASK     (1U << 10U)
#define I2C_C_INTT_MASK     (1U <<  9U)
#define I2C_C_INTD_MASK     (1U <<  8U)
#define I2C_C_ST_MASK       (1U <<  7U)
#define I2C_C_CLEAR_MASK    (3U <<  4U)
#define I2C_C_READ_MASK     (1U <<  0U)
#define I2C_C_INT_ALL_MASK  (I2C_C_INTR_MASK | I2C_C_INTT_MASK | I2C_C_INTD_MASK)

/* =========================================================================
 *  Status Register (S) Bit Masks
 * ========================================================================= */
#define I2C_S_CLKT_MASK     (1U <<  9U)
#define I2C_S_ERR_MASK      (1U <<  8U)
#define I2C_S_RXF_MASK      (1U <<  7U)
#define I2C_S_TXE_MASK      (1U <<  6U)
#define I2C_S_RXD_MASK      (1U <<  5U)
#define I2C_S_TXD_MASK      (1U <<  4U)
#define I2C_S_RXR_MASK      (1U <<  3U)
#define I2C_S_TXW_MASK      (1U <<  2U)
#define I2C_S_DONE_MASK     (1U <<  1U)
#define I2C_S_TA_MASK       (1U <<  0U)
#define I2C_S_W1C_MASK      (I2C_S_CLKT_MASK | I2C_S_ERR_MASK | I2C_S_DONE_MASK)

/* =========================================================================
 *  Other Register Masks
 * ========================================================================= */
#define I2C_SLAVE_ADDR_MASK  0x0000007FU////////////////
#define I2C_FIFO_DATA_MASK   0x000000FFU///////////
#define I2C_CDIV_MASK        0x0000FFFFU//////////
#define I2C_DEL_FEDL_SHIFT   16U///////////
#define I2C_DEL_REDL_SHIFT    0U/////////////
#define I2C_DEL_FIELD_MASK   0x0000FFFFU////////////
#define I2C_CLKT_TOUT_MASK   0x0000FFFFU////////

/* =========================================================================
 *  Clock Constants
 *  [H-02] 500 MHz verified via: vcgencmd measure_clock core
 *  [H-03] CDIV = 500_000_000 / target_hz
 * ========================================================================= */
#define I2C_FIFO_SIZE       16U////////////////
#define I2C_CORE_CLOCK_HZ   500000000U////////////
#define I2C_CDIV_100KHZ     5000U
#define I2C_CDIV_400KHZ     1250U

/* =========================================================================
 *  GIC-400 (BCM2711 QA7_rev3.4 §4.4)
 *  Base=0xFF840000  GICD=+0x1000  GICC=+0x2000
 * ========================================================================= */
#define I2C_GIC400_BASE             ((uint32_t)0xFF840000U)
#define I2C_GIC400_DIST_OFFSET      0x1000U
#define I2C_GIC400_CPUIF_OFFSET     0x2000U

#define I2C_GICD_CTLR_OFFSET        0x000U
#define I2C_GICD_ISENABLER_OFFSET   0x100U
#define I2C_GICD_ICENABLER_OFFSET   0x180U
#define I2C_GICD_IPRIORITYR_OFFSET  0x400U
#define I2C_GICD_ITARGETSR_OFFSET   0x800U
#define I2C_GICD_ICFGR_OFFSET       0xC00U
#define I2C_GICD_IGROUPR_OFFSET     0x080U

#define I2C_GICC_CTLR_OFFSET        0x0000U
#define I2C_GICC_PMR_OFFSET         0x0004U
#define I2C_GICC_IAR_OFFSET         0x000CU
#define I2C_GICC_EOIR_OFFSET        0x0010U

#define I2C_GICD_CTLR_ENABLE_GRP0  (1U << 0U)
#define I2C_GICD_CTLR_ENABLE_GRP1  (1U << 1U)
#define I2C_GICD_CTLR_ENABLE       I2C_GICD_CTLR_ENABLE_GRP0
#define I2C_GICC_CTLR_ENABLE       (1U << 0U)
#define I2C_GICC_PMR_ALLOW_ALL      0xFFU
#define I2C_GIC_SPURIOUS_IRQ_ID     1023U
#define I2C_GICC_IAR_ID_MSK         0x000003FFU

/* =========================================================================
 *  Register Access Macros
 * ========================================================================= */
#define I2C_REG_READ(ptr, reg)       ((ptr)->reg)
#define I2C_REG_WRITE(ptr, reg, val) ((ptr)->reg = (uint32_t)(val))
#define I2C_MMIO_READ(addr)          (*(volatile uint32_t *)(uintptr_t)(addr))
#define I2C_MMIO_WRITE(addr, val)    (*(volatile uint32_t *)(uintptr_t)(addr) = (uint32_t)(val))

/* =========================================================================
 *  Enumerations
 * ========================================================================= */
typedef enum {
    I2C_STATUS_OK            = 0,
    I2C_STATUS_ERROR         = 1,
    I2C_STATUS_BUSY          = 2,
    I2C_STATUS_TIMEOUT       = 3,
    I2C_STATUS_NACK          = 4,
    I2C_STATUS_INVALID_PARAM = 5
} I2C_StatusType;

typedef enum {
    I2C_INSTANCE_BSC0 = 0,
    I2C_INSTANCE_BSC1 = 1,
    I2C_INSTANCE_BSC3 = 2,
    I2C_INSTANCE_BSC4 = 3,
    I2C_INSTANCE_BSC5 = 4,
    I2C_INSTANCE_BSC6 = 5
} I2C_InstanceType;

typedef enum {
    I2C_STATE_RESET   = 0,
    I2C_STATE_READY   = 1,
    I2C_STATE_BUSY_TX = 2,
    I2C_STATE_BUSY_RX = 3,
    I2C_STATE_ERROR   = 4
} I2C_StateType;

/**
 * @brief I2C transfer direction.
 */
typedef enum {
    I2C_DIRECTION_WRITE = 0,  /**< Master-transmit (READ bit = 0) */
    I2C_DIRECTION_READ  = 1   /**< Master-receive  (READ bit = 1) */
} I2C_DirectionType;

typedef enum {
    I2C_EVENT_TX_COMPLETE         = 0,
    I2C_EVENT_RX_COMPLETE         = 1,
    I2C_EVENT_ERROR_NACK          = 2,
    I2C_EVENT_ERROR_CLKT          = 3,
    I2C_EVENT_TX_FIFO_NEEDS_WRITE = 4,
    I2C_EVENT_RX_FIFO_NEEDS_READ  = 5
} I2C_EventType;

typedef enum {
    I2C_ERROR_NONE      = 0x00,
    I2C_ERROR_NACK      = 0x01,
    I2C_ERROR_CLKT      = 0x02,
    I2C_ERROR_NACK_CLKT = 0x03
} I2C_ErrorFlagType;

/* =========================================================================
 *  Data Structures
 * ========================================================================= */
typedef struct {
    I2C_InstanceType Instance;
    uint16_t         ClockDivider;
    uint16_t         RisingEdgeDelay;
    uint16_t         FallingEdgeDelay;
    uint16_t         ClockStretchTimeout;
} I2C_ConfigType;

struct I2C_Handle_Tag;
typedef void (*I2C_CallbackType)(struct I2C_Handle_Tag *pHandle, I2C_EventType Event);

typedef struct I2C_Handle_Tag {
    volatile I2C_RegMap *pReg;
    uintptr_t            BaseAddr;
    I2C_ConfigType       Config;
    I2C_StateType        State;
    const uint8_t       *pTxBuffer;
    uint16_t             TxSize;
    uint16_t             TxCount;
    uint8_t             *pRxBuffer;
    uint16_t             RxSize;
    uint16_t             RxCount;
    uint8_t              SlaveAddr;
    I2C_ErrorFlagType    ErrorCode;
    I2C_CallbackType     Callback;
} I2C_HandleType;

typedef struct {
    bool TransferActive;
    bool TransferDone;
    bool TxFifoEmpty;
    bool RxFifoFull;
    bool TxFifoHasSpace;
    bool RxFifoHasData;
    bool TxFifoNeedsWrite;
    bool RxFifoNeedsRead;
    bool AckError;
    bool ClockStretchTimeout;
} I2C_StatusInfoType;

typedef struct {
    uint32_t GicBaseAddr;
    uint32_t SpiInterruptId;
    uint8_t  Priority;
    uint8_t  TargetCpuMask;
} I2C_GIC_ConfigType;

/* =========================================================================
 *  API Prototypes
 * ========================================================================= */
I2C_StatusType    I2C_Init(I2C_HandleType *pHandle, const I2C_ConfigType *pConfig);
I2C_StatusType    I2C_DeInit(I2C_HandleType *pHandle);
I2C_ErrorFlagType I2C_GetError(const I2C_HandleType *pHandle);
I2C_StatusType    I2C_ClearError(I2C_HandleType *pHandle, I2C_ErrorFlagType ErrorFlags);
I2C_StatusType    I2C_GetStatus(const I2C_HandleType *pHandle, I2C_StatusInfoType *pStatusInfo);
I2C_StatusType    I2C_MasterTransmit_IT(I2C_HandleType *pHandle, uint8_t SlaveAddr,
                                         const uint8_t *pData, uint16_t Size);
I2C_StatusType    I2C_MasterReceive_IT(I2C_HandleType *pHandle, uint8_t SlaveAddr,
                                        uint8_t *pData, uint16_t Size);
void              I2C_IRQHandler(I2C_HandleType *pHandle);
I2C_StatusType    I2C_SetClockSpeed(I2C_HandleType *pHandle, uint32_t SclFrequencyHz);
I2C_StatusType    I2C_SetDataDelay(I2C_HandleType *pHandle,
                                    uint16_t RisingEdgeDelay, uint16_t FallingEdgeDelay);
I2C_StatusType    I2C_SetClockStretchTimeout(I2C_HandleType *pHandle, uint16_t TimeoutScl);
I2C_StatusType    I2C_GIC_Init(const I2C_GIC_ConfigType *pGicConfig);
I2C_StatusType    I2C_GIC_DeInit(const I2C_GIC_ConfigType *pGicConfig);
I2C_StatusType    I2C_RegisterCallback(I2C_HandleType *pHandle, I2C_CallbackType CallbackFn);
I2C_StatusType    I2C_UnregisterCallback(I2C_HandleType *pHandle);

#ifdef __cplusplus
}
#endif

#endif /* I2C_DRIVER_H */
