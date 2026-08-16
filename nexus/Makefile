# Nexus Userspace Makefile
# Nexus = userspace, built on Crius Kernel
# Requires: gcc, ld, nasm, xxd

CC = gcc
LD = ld

# Directories — relative to nexus/ (make -C nexus)
NEXUS_DIR    = .
BUILD_DIR    = ../build/nexus
TOOLS_DIR    = ../tools
FREESTANDING_HDRS ?= ../freestanding-c-hdrs/include

# Nexus libc include path — NO kernel headers
NEXUS_INCLUDES = -I$(NEXUS_DIR)/libc/include -I$(NEXUS_DIR)/../abi -isystem $(FREESTANDING_HDRS)

# Userspace C flags — lower half, large model, no kernel includes
USER_CFLAGS = -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding \
         -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC \
         -ffunction-sections -fdata-sections \
         -m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx \
         -mno-sse -mno-sse2 -mno-red-zone -mcmodel=large \
         -Os $(NEXUS_INCLUDES)

INIT_LDFLAGS = -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
          --gc-sections --strip-debug -T $(TOOLS_DIR)/nexus.ld

PROG_LDFLAGS = -m elf_x86_64 -nostdlib -static -n \
          --gc-sections --strip-debug -T $(TOOLS_DIR)/prog.ld

# ===== Nexus libc sources =====
LIBC_SRC = $(NEXUS_DIR)/libc/src/process.c \
           $(NEXUS_DIR)/libc/src/filesystem.c $(NEXUS_DIR)/libc/src/mount.c \
           $(NEXUS_DIR)/libc/src/system.c $(NEXUS_DIR)/libc/src/proc.c \
           $(NEXUS_DIR)/libc/src/registry.c
LIBC_OBJ = $(patsubst $(NEXUS_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIBC_SRC))

# ===== Init (PID 1) =====
INIT_SRC = $(NEXUS_DIR)/init/init.c
INIT_OBJ = $(patsubst $(NEXUS_DIR)/%.c,$(BUILD_DIR)/%.o,$(INIT_SRC)) \
           $(LIBC_OBJ) $(BUILD_DIR)/tools/prog_data.o

# ===== Userspace programs =====
PROG_SRC = $(NEXUS_DIR)/coreutils/builtin.c $(NEXUS_DIR)/coreutils/files.c \
           $(NEXUS_DIR)/coreutils/tasks.c $(NEXUS_DIR)/coreutils/disk.c \
           $(NEXUS_DIR)/coreutils/help.c $(NEXUS_DIR)/coreutils/svc.c \
           $(NEXUS_DIR)/editor/editor.c $(NEXUS_DIR)/shell/shell.c \
           $(NEXUS_DIR)/shell/shell_builtin.c $(NEXUS_DIR)/shell/shell_exec.c \
           $(NEXUS_DIR)/tests/security_test.c $(NEXUS_DIR)/tests/proctest.c \
           $(NEXUS_DIR)/tests/proctest_mem.c
PROG_OBJ = $(patsubst $(NEXUS_DIR)/%.c,$(BUILD_DIR)/%.o,$(PROG_SRC))

# Map: program name → source object file
echo_OBJ   = $(BUILD_DIR)/coreutils/builtin.o
clear_OBJ  = $(BUILD_DIR)/coreutils/builtin.o
reboot_OBJ = $(BUILD_DIR)/coreutils/builtin.o
ps_OBJ     = $(BUILD_DIR)/coreutils/tasks.o
kill_OBJ   = $(BUILD_DIR)/coreutils/tasks.o
sleep_OBJ  = $(BUILD_DIR)/coreutils/tasks.o
priority_OBJ = $(BUILD_DIR)/coreutils/tasks.o
ls_OBJ     = $(BUILD_DIR)/coreutils/files.o
cat_OBJ    = $(BUILD_DIR)/coreutils/files.o
write_OBJ  = $(BUILD_DIR)/coreutils/files.o
append_OBJ = $(BUILD_DIR)/coreutils/files.o
rm_OBJ     = $(BUILD_DIR)/coreutils/files.o
mkdir_OBJ  = $(BUILD_DIR)/coreutils/files.o
pwd_OBJ    = $(BUILD_DIR)/coreutils/files.o
disk_OBJ   = $(BUILD_DIR)/coreutils/disk.o
rdisk_OBJ  = $(BUILD_DIR)/coreutils/disk.o
wdisk_OBJ  = $(BUILD_DIR)/coreutils/disk.o
mount_OBJ  = $(BUILD_DIR)/coreutils/disk.o
umount_OBJ = $(BUILD_DIR)/coreutils/disk.o
edit_OBJ   = $(BUILD_DIR)/editor/editor.o
help_OBJ   = $(BUILD_DIR)/coreutils/help.o
svc_OBJ    = $(BUILD_DIR)/coreutils/svc.o
shell_OBJ  = $(BUILD_DIR)/shell/shell.o $(BUILD_DIR)/shell/shell_builtin.o $(BUILD_DIR)/shell/shell_exec.o $(BUILD_DIR)/editor/editor.o
security_test_OBJ = $(BUILD_DIR)/tests/security_test.o
proctest_OBJ = $(BUILD_DIR)/tests/proctest.o $(BUILD_DIR)/tests/proctest_mem.o

# Program lists
BIN_PROGRAMS = echo clear reboot ls cat write append rm mkdir pwd edit help shell security_test proctest
SBIN_PROGRAMS = ps kill sleep priority disk rdisk wdisk mount umount svc
PROGRAMS = $(BIN_PROGRAMS) $(SBIN_PROGRAMS)
PROGRAM_ELFS = $(foreach prog,$(PROGRAMS),$(BUILD_DIR)/bin/$(prog).elf)

# ===== Targets =====
all: $(BUILD_DIR)/nexus.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(FREESTANDING_HDRS):
	git clone https://github.com/osdev0/freestanding-c-hdrs.git freestanding-c-hdrs

# Compile nexus C files (libc + programs + init)
$(BUILD_DIR)/%.o: $(NEXUS_DIR)/%.c $(FREESTANDING_HDRS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Generate embedded program data from built ELF binaries
$(TOOLS_DIR)/prog_data.c: $(PROGRAM_ELFS) $(TOOLS_DIR)/gen_progs.sh
	$(TOOLS_DIR)/gen_progs.sh $@ $(BUILD_DIR) $(PROGRAMS)

# Compile generated prog_data.c
$(BUILD_DIR)/tools/prog_data.o: $(TOOLS_DIR)/prog_data.c $(FREESTANDING_HDRS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Link init (PID 1) — the nexus ELF
$(BUILD_DIR)/nexus.elf: $(INIT_OBJ) $(TOOLS_DIR)/nexus.ld
	$(LD) $(INIT_LDFLAGS) $(INIT_OBJ) -o $@

# Compile program entry point
$(BUILD_DIR)/prog_%.o: $(NEXUS_DIR)/libc/src/entry.c $(FREESTANDING_HDRS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -DPROG_MAIN=$*_main -c $< -o $@

# Link each program as separate ELF at 0x50000000
define link_program
$(BUILD_DIR)/bin/$(1).elf: $(BUILD_DIR)/prog_$(1).o $($(1)_OBJ) $(LIBC_OBJ) $(TOOLS_DIR)/prog.ld
	@mkdir -p $(BUILD_DIR)/bin
	$(LD) $(PROG_LDFLAGS) $(BUILD_DIR)/prog_$(1).o $($(1)_OBJ) $(LIBC_OBJ) -o $$@
endef
$(foreach prog,$(PROGRAMS),$(eval $(call link_program,$(prog))))

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TOOLS_DIR)/prog_data.c

.PHONY: all clean
