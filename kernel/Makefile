# Crius Kernel Makefile
# Builds kernel.elf only — no userspace

CC = gcc
LD = ld
NASM = nasm

KERNEL_DIR   = .
BUILD_DIR    = ../build
FREESTANDING_HDRS ?= freestanding-c-hdrs/include

# Kernel C flags — upper half, kernel model
KERNEL_CFLAGS = -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding \
         -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC \
         -ffunction-sections -fdata-sections \
         -m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx \
         -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
         -g -O2 -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/../abi -isystem $(FREESTANDING_HDRS)

KERNEL_LDFLAGS = -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
          --gc-sections -T $(KERNEL_DIR)/linker.ld

# ===== Kernel sources =====
KERNEL_C_SRC := $(shell find $(KERNEL_DIR) -name '*.c' -not -path './build/*')
KERNEL_ASM_SRC := $(shell find $(KERNEL_DIR) -name '*.asm' -not -path './build/*')
KERNEL_OBJ = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRC)) \
      $(patsubst $(KERNEL_DIR)/%.asm,$(BUILD_DIR)/%.asm.o,$(KERNEL_ASM_SRC))

# ===== Targets =====
all: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile kernel C
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

# Compile kernel ASM
$(BUILD_DIR)/%.asm.o: $(KERNEL_DIR)/%.asm | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g -F dwarf $< -o $@

# Link kernel (Crius)
$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJ) $(KERNEL_DIR)/linker.ld
	$(LD) $(KERNEL_LDFLAGS) $(KERNEL_OBJ) -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
