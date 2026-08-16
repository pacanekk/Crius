# Crius Kernel + Nexus Userspace — Root Makefile
# Crius = kernel, Nexus = userspace
# Requires: gcc, xorriso, qemu-system-x86_64, make, curl

# Directories
BUILD_DIR    = build
ISO_DIR      = iso_root
TOOLS_DIR    = tools
FREESTANDING_HDRS = freestanding-c-hdrs/include

# ===== Limine =====
LIMINE_SRC = limine-src
LIMINE_DIR = limine-bin
LIMINE_EFI = $(LIMINE_DIR)/BOOTX64.EFI
LIMINE_SYS = $(LIMINE_DIR)/limine-bios.sys
LIMINE_CD = $(LIMINE_DIR)/limine-bios-cd.bin
LIMINE_UEFI_CD = $(LIMINE_DIR)/limine-uefi-cd.bin
LIMINE_DEPLOY = $(LIMINE_SRC)/limine

# ===== Targets =====
all: crius.iso

$(FREESTANDING_HDRS):
	git clone https://github.com/osdev0/freestanding-c-hdrs.git freestanding-c-hdrs

# Build kernel and nexus separately
kernel: $(FREESTANDING_HDRS)
	$(MAKE) -C kernel FREESTANDING_HDRS=../$(FREESTANDING_HDRS)

nexus: $(FREESTANDING_HDRS)
	$(MAKE) -C nexus FREESTANDING_HDRS=../$(FREESTANDING_HDRS)

# Download pre-built limine binaries
LIMINE_VERSION = v12.3.3
$(LIMINE_SRC):
	curl -Lo limine-binary.zip https://github.com/limine-bootloader/limine/releases/download/$(LIMINE_VERSION)/limine-binary.zip
	mkdir -p $(LIMINE_SRC)
	unzip -d $(LIMINE_SRC) limine-binary.zip
	rm limine-binary.zip
	mkdir -p $(LIMINE_DIR)
	cp $(LIMINE_SRC)/limine-binary/limine-bios.sys $(LIMINE_DIR)/
	cp $(LIMINE_SRC)/limine-binary/limine-bios-cd.bin $(LIMINE_DIR)/
	cp $(LIMINE_SRC)/limine-binary/limine-uefi-cd.bin $(LIMINE_DIR)/
	cp $(LIMINE_SRC)/limine-binary/BOOTX64.EFI $(LIMINE_DIR)/
	$(MAKE) -C $(LIMINE_SRC)/limine-binary CC=gcc CFLAGS="-g -O2 -pipe -Wall -Wextra -Wno-error" limine
	cp $(LIMINE_SRC)/limine-binary/limine $(LIMINE_SRC)/limine

$(LIMINE_DIR): $(LIMINE_SRC)

# Build ISO — requires both kernel and nexus to be built
crius.iso: kernel nexus $(LIMINE_DIR) limine.conf
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/limine
	mkdir -p $(ISO_DIR)/boot
	cp build/kernel.elf $(ISO_DIR)/boot/kernel
	cp build/nexus/nexus.elf $(ISO_DIR)/boot/nexus
	cp limine.conf $(ISO_DIR)/boot/limine/limine.conf
	cp $(LIMINE_SYS) $(ISO_DIR)/boot/limine/
	cp $(LIMINE_CD) $(ISO_DIR)/boot/limine/
	cp $(LIMINE_UEFI_CD) $(ISO_DIR)/boot/limine/
	mkdir -p $(ISO_DIR)/EFI/BOOT
	cp $(LIMINE_EFI) $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o crius.iso
	$(LIMINE_DEPLOY) bios-install crius.iso

# Run in QEMU
run: crius.iso disk.img
	qemu-system-x86_64 -cpu Haswell,+smap -m 512M -cdrom crius.iso -drive format=raw,file=disk.img -boot d

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64 2>/dev/null
	echo -e 'label: dos\n,,L,*' | sfdisk disk.img 2>/dev/null
	mkfs.ext2 -F -b 1024 -E offset=$$((2048*512)) disk.img $$((129024/2)) 2>/dev/null

# Clean
clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) crius.iso limine-bin limine-src freestanding-c-hdrs $(TOOLS_DIR)/prog_data.c
	$(MAKE) -C kernel clean
	$(MAKE) -C nexus clean

.PHONY: all clean run kernel nexus
