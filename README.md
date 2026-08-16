# Crius

Crius is an x86-64 operating system written in C and assembly, using the [Limine](https://limine-bootloader.org/) boot protocol.

> **Status:** early development. The kernel and a minimal userspace (Nexus) build and run under QEMU.

## What's included

- **Kernel** (`kernel/`) — 64-bit higher-half kernel with GDT/IDT/APIC, PMM/VMM, scheduler, system calls, VFS, ramfs, ext2, framebuffer console, PS/2 keyboard, serial and IDE block devices.
- **Nexus** (`nexus/`) — a small userspace with libc, init, shell, coreutils, editor and tests.

## Quick start

```bash
make        # build the kernel, userspace and create crius.iso
make run    # boot the ISO in QEMU
```

## Build

The following tools are required. Exact package names depend on your distribution:

- `gcc`, `ld`, `nasm`, `make` — build toolchain
- `git`, `curl` — for downloading Limine and the freestanding headers
- `xorriso` — for creating `crius.iso`
- `qemu-system-x86_64` (or your distribution's x86-64 QEMU binary) — for `make run`
- `dd`, `sfdisk` (usually from `util-linux`), and `mkfs.ext2` (usually from `e2fsprogs`) — `make run` also creates a `disk.img` test image

```bash
make kernel     # build kernel.elf only
make nexus      # build userspace nexus.elf only
```

The Makefile downloads Limine v12.3.3 binaries and the freestanding-c-hdrs headers automatically.

## Platform setup

### Fedora

```bash
sudo dnf install gcc make nasm binutils xorriso qemu-system-x86 curl git util-linux e2fsprogs
```

### Debian / Ubuntu

```bash
sudo apt install gcc make nasm binutils xorriso qemu-system-x86 curl git util-linux e2fsprogs
```

### Windows

The official Crius build environment is Linux-based. On Windows, the recommended approach is **WSL2**:

1. Install WSL2 with Debian or Ubuntu:
   ```powershell
   wsl --install -d Debian
   ```
2. Inside WSL2, follow the **Debian / Ubuntu** instructions above.
3. For better performance and compatibility, keep the project inside the WSL2 filesystem (`/home/<user>/...`) instead of `/mnt/c/...`.

## Project layout

```
abi/        Kernel/userspace ABI definitions
kernel/     Kernel source and headers
nexus/      Userspace programs and libc
tools/      Build helpers and linker scripts
```

## License

This project is distributed under a custom license. See the `LICENSE` file for details.
