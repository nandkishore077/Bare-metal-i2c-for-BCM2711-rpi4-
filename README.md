# BCM2711 Bare-Metal I2C (BSC1) + UART0 (PL011) Drivers

Bare-metal, register-level I2C and UART drivers for the Raspberry Pi 4
(BCM2711), AArch64, no OS. UART0 is used as the debug console for the I2C
driver's self-test suite. All register addresses and clock values below are
hardware-verified (`/proc/iomem`, `vcgencmd measure_clock core`), not taken
from the datasheet alone.

## Hardware target

| | |
|---|---|
| SoC | Broadcom BCM2711 (Raspberry Pi 4 Model B) |
| Mode | Bare-metal, AArch64, low-peripheral addressing (`0xFE000000`) |
| Toolchain | `aarch64-linux-gnu-gcc` / `-ld` / `-objcopy` |
| I2C core clock | 500 MHz (verified: `vcgencmd measure_clock core`) |
| UART clock | 48 MHz (verified: `clk_summary`) |

## Repository structure

```
.
├── gpio.h            # GPIO function-select / pull-up-down helpers (shared)
├── uart.h            # PL011 UART0 driver — debug console
├── i2c-driver.h       # BSC register map, types, public API
├── i2c-driver.c       # BSC1 I2C driver implementation
├── i2c-app.c          # 18-test-case self-test application (main)
├── boot.S             # AArch64 startup / stack init / BSS clear
├── linker.ld          # Places kernel8.img entry at 0x80000
├── Makefile           # Cross-compiles kernel8.img
└── config.txt         # SD card boot config (arm_64bit, UART)
```

> `boot.S`, `linker.ld`, `Makefile`, and `config.txt` aren't shown in the
> snippets above — add your existing versions here, or ask if you want
> these generated to match this file layout.

## Features

### I2C — BSC1 driver (`i2c-driver.c` / `.h`)

- Handle-based API: `I2C_Init`, `I2C_DeInit`, `I2C_MasterTransmit_IT`,
  `I2C_MasterReceive_IT`, `I2C_GetStatus`, `I2C_GetError`, `I2C_ClearError`,
  `I2C_SetClockSpeed`, `I2C_SetDataDelay`, `I2C_SetClockStretchTimeout`,
  `I2C_RegisterCallback` / `UnregisterCallback`, `I2C_IRQHandler`
- GIC-400 configuration API (`I2C_GIC_Init` / `I2C_GIC_DeInit`) — Group 0,
  level-sensitive, priority/target-CPU set per BCM2711 QA7 §4.4
- 100 kHz / 400 kHz clock divider presets, standard-mode default
- Bus: BSC1 on **GPIO2 (SDA1) / GPIO3 (SCL1)**, ALT0, internal pull-ups

### UART0 — PL011 debug driver (`uart.h`)

- **Base `0xFE201000`**, 115200 8N1, FIFOs enabled, polled (no interrupts)
- **GPIO14 (TXD0) / GPIO15 (RXD0)**, ALT0; RX pulled up, TX no pull
- `uart_init(baud)` computes `IBRD`/`FBRD` with correct rounding
  (`BRD = UARTCLK / (16 × baud)`, PL011 TRM §3.3.6)
- No `printf`/variadic args anywhere in this codebase — `-O2` AArch64
  `va_start` requires 16-byte stack alignment that bare-metal startup
  doesn't guarantee, so all logging goes through non-variadic
  `uart_print()` / `uart_print_hex()` / `uart_print_uint()` helpers instead

## Wiring

**I2C1** — connect to your slave device, common ground:

| RPi4 pin | Signal |
|---|---|
| Pin 3 (GPIO2) | SDA1 |
| Pin 5 (GPIO3) | SCL1 |
| Pin 6/9/… | GND |

**UART0** — connect to a USB-serial adapter, 115200 8N1, common ground:

| RPi4 pin | Signal | Adapter side |
|---|---|---|
| Pin 8 (GPIO14) | TXD0 | → adapter RX |
| Pin 10 (GPIO15) | RXD0 | ← adapter TX |
| Pin 6/9/… | GND | GND |

## Build

```bash
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
make            # produces kernel8.img
```

## Flash / boot

1. Copy `kernel8.img` and `config.txt` to the SD card's boot partition.
2. `config.txt` must set:
   ```
   arm_64bit=1
   kernel=kernel8.img
   enable_uart=1
   ```
3. Boot the Pi with a serial adapter attached to GPIO14/15 as wired above.
4. Open a terminal at 115200 8N1 (`screen /dev/ttyUSB0 115200` or similar).

## Test suite (`i2c-app.c`)

18 test cases run automatically on boot and print `[PASS]`/`[FAIL]` over
UART, ending with a pass/fail/total summary:

| TC | Covers |
|---|---|
| 01 | `I2C_Init` |
| 02 | `I2C_GetStatus` |
| 03 | `I2C_SetClockSpeed` (100 kHz) |
| 04 | `I2C_SetDataDelay` |
| 05 | `I2C_SetClockStretchTimeout` |
| 06 | `I2C_RegisterCallback` |
| 07 | `I2C_GIC_Init` |
| 08–10 | Transmit / receive / verify against slave `0x50` |
| 11–12 | `I2C_GetError` / `I2C_ClearError` |
| 13–15 | Transmit / receive / verify against absent slave `0x68` (expects NACK) |
| 16 | `I2C_UnregisterCallback` |
| 17 | `I2C_GIC_DeInit` |
| 18 | `I2C_DeInit` |

BSC1 interrupts route through the legacy ARM interrupt controller
(`0xFE00B200`), not the GIC-400, so data transfers in the test app use a
polling fallback on the `S` register (`App_WaitTx` / `App_WaitRx`) rather
than a live IRQ path; `I2C_GIC_Init`/`DeInit` are still exercised and
verified independently in TC-07/TC-17.

## Hardware-verified fixes

Issues found and corrected against real hardware behavior (not simulation):

**I2C**
| ID | Issue | Fix |
|---|---|---|
| H-01 | BSC base used legacy bus addr `0x7Exxxxxx` | ARM phys `0xFExxxxxx` |
| H-02 | Core clock assumed 150 MHz | Verified 500 MHz via `vcgencmd` |
| H-03 | CDIV miscalculated for target clock | `5000` (100 kHz) / `1250` (400 kHz) |
| H-04 | Pointer truncation risk on AArch64 | `BaseAddr` stored as `uintptr_t` |
| H-05 | `DEL`/`CLKT` writes stall the AXI bus on BSC1 | Writes skipped; values kept in config only |
| C-04 | IRQ handler left `ST` bit set when masking interrupts | Cleared explicitly |

**UART**
| ID | Issue | Fix |
|---|---|---|
| BUG-U-01 | Baud divisor truncated instead of rounded (`FBRD=2`, 0.65% error) | Rounded division (`FBRD=3`, 0.16% error) |
| BUG-U-02 | `uart_put_hex()` prepended `"0x"` internally, causing `"0x0x00000050"` when callers also added a prefix | Prefix removed from the helper; callers control it |

## Known constraint

Do **not** call `uart_init()` if the GPU firmware / 2nd-stage bootloader has
already configured UART0 (e.g. `uart_2ndstage=1` in `config.txt`) — writing
`UART0_CR = 0` to reconfigure it will kill the firmware-established UART
state. `i2c-app.c` relies on this and skips `uart_init()` entirely.

## License

MIT — free to use, modify, and share.
