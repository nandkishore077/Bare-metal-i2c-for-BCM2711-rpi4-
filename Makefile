# =============================================================================
# Makefile — BCM2711 (Raspberry Pi 4) bare-metal I2C driver
#
# Toolchain : aarch64-none-elf (bare-metal, no libc)
#             Obtain from: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#             Ubuntu/Debian package: gcc-aarch64-linux-gnu (hosted, works for
#             syntax checking; use aarch64-none-elf for production).
#
# Targets:
#   make              — build kernel8.img (default)
#   make all          — same as make
#   make clean        — remove build artefacts
#   make objdump      — disassemble kernel8.elf
#   make nm           — symbol table of kernel8.elf
#   make size         — section sizes
#   make flash        — copy kernel8.img to SD_CARD_PATH (set below)
#   make qemu         — run in QEMU (raspi3b machine, functional approximation)
#
# Output files (all under BUILD_DIR):
#   startup.o         — assembled startup.S
#   irq.o             — GIC dispatcher
#   i2c-driver.o      — BSC driver implementation
#   i2c-app.o         — application entry point (bare_main)
#   kernel8.elf       — linked ELF (use for JTAG/OpenOCD)
#   kernel8.map       — full linker map (section sizes, symbol addresses)
#   kernel8.img       — flat binary loaded by GPU firmware (copy to SD card)
# =============================================================================

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------

# Override on the command line if your toolchain prefix differs:
#   make CROSS=aarch64-linux-gnu-
CROSS   ?= aarch64-none-elf-

CC      := $(CROSS)gcc
AS      := $(CROSS)gcc          # Use gcc to assemble: handles .S preprocessing
LD      := $(CROSS)gcc          # Link via gcc so it finds libgcc if needed
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
NM      := $(CROSS)nm
SIZE    := $(CROSS)size

# -----------------------------------------------------------------------------
# Directories
# -----------------------------------------------------------------------------

SRC_DIR   := .
BUILD_DIR := build

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------

# Assembly sources (preprocessed by gcc -x assembler-with-cpp)
ASM_SRCS := startup.S

# C sources
# [BUG-M-01] irq.c removed — does not exist; IRQ_Dispatch lives in startup.S
C_SRCS   := i2c-driver.c  \
             i2c-app.c

# Object files (all land in BUILD_DIR)
ASM_OBJS := $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_SRCS))
C_OBJS   := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ALL_OBJS := $(ASM_OBJS) $(C_OBJS)

# Linker script
LDSCRIPT := bcm2711.ld

# Final artefacts
TARGET_ELF := $(BUILD_DIR)/kernel8.elf
TARGET_MAP := $(BUILD_DIR)/kernel8.map
TARGET_IMG := $(BUILD_DIR)/kernel8.img

# -----------------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------------

# Target: Cortex-A72, AArch64
# [BUG-M-02] -mstrict-align replaced with  (AArch64 name)
# [BUG-M-NEW]  is a C compiler flag only — the assembler
#             (gas) does not accept it.  Split into two flag groups:
#               ARCH_FLAGS     — flags valid for BOTH gcc and gas (used in ASFLAGS)
#               ARCH_CFLAGS    — C-compiler-only flags (appended to CFLAGS only)
ARCH_FLAGS := \
    -march=armv8-a          \
    -mtune=cortex-a72

# C-compiler-only arch flags (NOT passed to the assembler)
ARCH_CFLAGS := \
    

# Freestanding: no libc, no crt0 from the toolchain
BARE_FLAGS := \
    -ffreestanding          \
    -nostdlib               \
    -nostartfiles           \
    -fno-builtin

# Optimisation: -O2 for release; swap to -Og for debugging
OPT_FLAGS := -O2

# Debugging symbols (include always; stripped from .img by objcopy)
DBG_FLAGS := -g3

# Warning flags
WARN_FLAGS := \
    -Wall                   \
    -Wextra                 \
    -Wpedantic              \
    -Wshadow                \
    -Wundef                 \
    -Wdouble-promotion      \
    -Wno-unused-parameter

# Dependency generation (for incremental builds)
DEP_FLAGS := -MMD -MP

# Include paths
INC_FLAGS := -I$(SRC_DIR)

# Combined CFLAGS
CFLAGS := \
    $(ARCH_FLAGS)   \
    $(ARCH_CFLAGS)  \
    $(BARE_FLAGS)   \
    $(OPT_FLAGS)    \
    $(DBG_FLAGS)    \
    $(WARN_FLAGS)   \
    $(DEP_FLAGS)    \
    $(INC_FLAGS)    \
    -std=c11

# ASFLAGS: assembler flags — ARCH_CFLAGS deliberately excluded (gas rejects them)
ASFLAGS := \
    $(ARCH_FLAGS)           \
    -x assembler-with-cpp   \
    $(DBG_FLAGS)            \
    $(DEP_FLAGS)            \
    $(INC_FLAGS)

# -----------------------------------------------------------------------------
# Linker flags
# -----------------------------------------------------------------------------

LDFLAGS := \
    $(ARCH_FLAGS)                   \
    $(BARE_FLAGS)                   \
    -T $(LDSCRIPT)                  \
    -Wl,-Map=$(TARGET_MAP)          \
    -Wl,--print-memory-usage        \
    -Wl,--gc-sections               \
    -Wl,--build-id=none             \
    -Wl,--no-warn-rwx-segments

# Link libgcc for compiler-generated helpers (__udivdi3, __aeabi_* etc.)
# aarch64-none-elf-gcc includes it implicitly when linking via gcc driver.

# -----------------------------------------------------------------------------
# SD card mount path (for 'make flash')
# Override: make flash SD_CARD_PATH=/media/youruser/bootfs
# -----------------------------------------------------------------------------
SD_CARD_PATH ?= /media/$(USER)/bootfs

# -----------------------------------------------------------------------------
# QEMU settings (for 'make qemu')
# raspi3b is the closest QEMU machine to RPi4; full BCM2711 support is
# not yet upstream.  Useful for basic boot / UART testing.
# -----------------------------------------------------------------------------
QEMU        ?= qemu-system-aarch64
QEMU_MACHINE := raspi3b
QEMU_FLAGS  := \
    -machine $(QEMU_MACHINE)        \
    -cpu cortex-a72                 \
    -kernel $(TARGET_IMG)           \
    -serial stdio                   \
    -display none                   \
    -no-reboot

# =============================================================================
# Build rules
# =============================================================================

.PHONY: all clean objdump nm size flash qemu

all: $(TARGET_IMG)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# -----------------------------------------------------------------------------
# Assemble .S → .o
# -----------------------------------------------------------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(AS) $(ASFLAGS) -c $< -o $@

# -----------------------------------------------------------------------------
# Compile .c → .o
# -----------------------------------------------------------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC      $<"
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------------------------------------------------------
# Link .o files → ELF
# -----------------------------------------------------------------------------
$(TARGET_ELF): $(ALL_OBJS) $(LDSCRIPT)
	@echo "  LD      $@"
	$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@
	@echo ""
	$(SIZE) $@

# -----------------------------------------------------------------------------
# Strip ELF → flat binary
#
# -O binary: raw binary, no ELF headers
# --strip-all: drop all symbols and debug info from the binary image
#   (the ELF retains full debug info for gdb/OpenOCD; only the .img is stripped)
# -----------------------------------------------------------------------------
$(TARGET_IMG): $(TARGET_ELF)
	@echo "  OBJCOPY $@"
	$(OBJCOPY) -O binary --strip-all $< $@
	@echo ""
	@echo "  Image:  $@  ($(shell wc -c < $@) bytes)"
	@echo "  ELF:    $(TARGET_ELF)  (for JTAG / OpenOCD / GDB)"

# -----------------------------------------------------------------------------
# Include generated dependency files (silent if missing on first build)
# -----------------------------------------------------------------------------
-include $(ALL_OBJS:.o=.d)

# =============================================================================
# Utility targets
# =============================================================================

clean:
	@echo "  CLEAN   $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

objdump: $(TARGET_ELF)
	@echo "  OBJDUMP $(TARGET_ELF)"
	$(OBJDUMP) -d -S $(TARGET_ELF) | less

nm: $(TARGET_ELF)
	@echo "  NM      $(TARGET_ELF)"
	$(NM) -n $(TARGET_ELF)

size: $(TARGET_ELF)
	$(SIZE) -A $(TARGET_ELF)

# -----------------------------------------------------------------------------
# Flash to SD card
#
# Copy kernel8.img to the FAT32 boot partition.  The GPU firmware
# (bootcode.bin / start4.elf) reads kernel8.img on every boot.
# Requires SD card mounted at SD_CARD_PATH.
# -----------------------------------------------------------------------------
flash: $(TARGET_IMG)
	@if [ ! -d "$(SD_CARD_PATH)" ]; then \
	    echo "ERROR: SD card not found at $(SD_CARD_PATH)"; \
	    echo "Mount the RPi4 SD card and retry, or run:"; \
	    echo "  make flash SD_CARD_PATH=/path/to/bootfs"; \
	    exit 1; \
	fi
	@echo "  FLASH   $(SD_CARD_PATH)/kernel8.img"
	cp $(TARGET_IMG) $(SD_CARD_PATH)/kernel8.img
	sync
	@echo "  Done. Unmount the SD card before inserting into RPi4."

# -----------------------------------------------------------------------------
# QEMU emulation (development / boot testing only — raspi3b ≠ BCM2711)
#
# BSC1 peripheral is not accurately emulated; use this only to verify
# that _start reaches bare_main() and the BSS zero loop completes.
# For real hardware bring-up use a Saleae logic analyser on SDA1/SCL1.
# -----------------------------------------------------------------------------
qemu: $(TARGET_IMG)
	@echo "  QEMU    $(QEMU_MACHINE) — Ctrl-A X to exit"
	$(QEMU) $(QEMU_FLAGS)

# =============================================================================
# Build system self-documentation
# =============================================================================

help:
	@echo ""
	@echo "BCM2711 bare-metal I2C driver — build targets"
	@echo "----------------------------------------------"
	@echo "  make              Build kernel8.img (default)"
	@echo "  make clean        Remove $(BUILD_DIR)/"
	@echo "  make objdump      Disassemble kernel8.elf (piped to less)"
	@echo "  make nm           Sorted symbol table of kernel8.elf"
	@echo "  make size         Section sizes of kernel8.elf"
	@echo "  make flash        Copy kernel8.img to SD card"
	@echo "  make qemu         Boot in QEMU raspi3b (approximate)"
	@echo ""
	@echo "Override variables:"
	@echo "  CROSS=aarch64-linux-gnu-   Use hosted toolchain"
	@echo "  OPT_FLAGS=-Og              Debug-friendly optimisation"
	@echo "  SD_CARD_PATH=/path/to/fat  SD card boot partition"
	@echo ""
