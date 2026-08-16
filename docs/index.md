# Crius Technical Documentation

This document is a technical reference for the Crius x86-64 operating system. It describes the architecture, implementation, build process, and current limitations based on the actual source code.

---

## 1. Overview

Crius is a 64-bit higher-half kernel for the x86-64 architecture, paired with a minimal userspace environment called Nexus. It is written in C and x86-64 assembly and uses the Limine boot protocol.

The kernel runs in ring 0 in the higher-half virtual address space, mapped at `0xFFFFFFFF80000000`. Userspace programs run in ring 3 in the lower canonical half. The kernel implements a subset of a Unix-like system call interface, a virtual file system, in-memory and on-disk file systems, a round-robin scheduler with priorities, and a basic memory manager.

The userspace environment (Nexus) consists of a small C library, an `init` process, a shell, core utilities, a simple editor, and test programs.

---

## 2. Architecture

The system is divided into the kernel (`kernel/`), the userspace (`nexus/`), the shared ABI (`abi/`), build tooling (`tools/`), and the Limine configuration (`limine.conf`).

### 2.1 Kernel Architecture

The kernel is a monolithic, higher-half x86-64 kernel. The main subsystems are:

- **Boot and initialization**: `kernel/boot/limine_requests.c`, `kernel/boot/boot.c`, `kernel/boot/kernel_main.c`
- **x86-64 architecture support**: `kernel/arch/` (GDT, IDT, APIC, page fault handler, `iretq` helper, ISRs)
- **Memory management**: `kernel/mm/pmm.c`, `kernel/mm/vmm.c`, `kernel/mm/kmalloc.c`
- **Process management and scheduling**: `kernel/process/`, `kernel/scheduler/`
- **System calls**: `kernel/syscall/syscall.c`
- **Virtual file system and concrete file systems**: `kernel/fs/`
- **Drivers**: `kernel/drivers/` (framebuffer, serial, PS/2 keyboard, IDE, block device abstraction)

The kernel uses `gcc -std=gnu11 -ffreestanding -nostdinc` with the freestanding C headers from `freestanding-c-hdrs`. It links as a static ELF with `ld` using `kernel/linker.ld`.

### 2.2 Userspace Architecture

Nexus is built by `nexus/Makefile` into static ELF binaries. The entry point for every program is `nexus/libc/src/entry.c`. It receives an `exec_ctx` from the kernel in `RDI` and calls the program's `PROG_MAIN`.

Components:

- `nexus/libc/` — small libc (system calls, string, `printf`, memory allocation)
- `nexus/init/` — `init` process
- `nexus/shell/` — interactive shell
- `nexus/coreutils/` — `cat`, `ls`, `echo`, `mkdir`, `rm`, `pwd`, `clear`, `ps`, `help`, `reboot`, `write`, `append`
- `nexus/editor/` — simple line editor
- `nexus/tests/` — `proctest`, `security_test`

### 2.3 Build Flow

1. `make` clones `freestanding-c-hdrs` and downloads Limine binaries.
2. `make -C kernel` compiles all `kernel/**/*.c` and `kernel/arch/**/*.asm` files into `build/kernel.elf`.
3. `make -C nexus` compiles userspace programs into `build/nexus/bin/*.elf`.
4. The root Makefile creates `iso_root/`, copies the kernel, Nexus, and Limine files, and builds `crius.iso` with `xorriso`.

---

## 3. Source Tree

```
abi/                Shared kernel/userspace ABI (syscall numbers, structs)
kernel/             Kernel source and headers
  arch/             GDT, IDT, APIC, page fault, ISRs, exec_iretq
  boot/             Limine requests, early boot, kernel main
  drivers/          Framebuffer, serial, keyboard, IDE, block device
  fs/               VFS, ramfs, ext2, devfs, procfs
  include/          Public kernel headers
  mm/               PMM, VMM, kmalloc
  process/          fork, exec, process management
  scheduler/        Round-robin scheduler
  syscall/          System call dispatch
nexus/              Userspace environment (Nexus)
  init/             init process
  libc/             C library
  shell/            Shell (shell.c, shell_builtin.c, shell_exec.c)
  coreutils/        Userspace utilities
  editor/           Simple editor
  tests/            Tests
tools/              Linker scripts (kernel/linker.ld, nexus.ld, prog.ld), gen_progs.sh
limine.conf         Limine boot menu configuration
```

---

## 4. Boot

Crius uses the Limine boot protocol. The boot files live in `kernel/boot/`.

### 4.1 Limine Requests

`kernel/boot/limine_requests.c` defines the Limine requests:

- `LIMINE_BASE_REVISION_REQUEST`
- `LIMINE_HHDM_REQUEST`
- `LIMINE_MEMMAP_REQUEST`
- `LIMINE_FRAMEBUFFER_REQUEST`
- `LIMINE_RSDP_REQUEST`
- `LIMINE_MODULE_REQUEST`
- `LIMINE_KERNEL_ADDRESS_REQUEST`

### 4.2 Early Boot

`kernel/boot/boot.c` performs the following initialization, in order:

1. Loads GDT with flat 64-bit segments.
2. Sets up the IDT with handlers from `kernel/arch/isr.asm` and `kernel/arch/idt.c`.
3. Initializes the APIC and the PIT/scheduler timer.
4. Initializes the Physical Memory Manager (`pmm_init`) from the Limine memory map.
5. Initializes the Virtual Memory Manager (`vmm_init`) using the HHDM offset.
6. Initializes `kmalloc`.
7. Initializes the scheduler and creates the initial kernel task.
8. Initializes the framebuffer (`fb_init`), serial (`serial_init`), PS/2 keyboard, and IDE block devices.
9. Initializes the VFS (`vfs_init`) and mounts `devfs`, a `ramfs` root, and `procfs`.
10. Loads the Nexus `init.elf` from the ramfs/initrd or disk and starts userspace execution.

### 4.3 Kernel Main

`kernel/boot/kernel_main.c` contains the final stage of boot: it sets up the userspace environment, maps the init process, and drops into the scheduler. If no init is found, it prints an error and halts.

---

## 5. Kernel

### 5.1 x86-64 Architecture Support (`kernel/arch/`)

- `gdt.c` — reloads flat 64-bit GDT and TSS for ring 0/ring 3.
- `idt.c` — installs IDT gates and handles exceptions (page faults, GP, divide by zero).
- `apic.c` — APIC timer and local APIC setup.
- `isr.asm` — low-level ISR stubs for exceptions and the timer.
- `exec_iretq.asm` — helper for entering userspace via `iretq`.
- `page_fault.c` — top-level page fault handler; forwards COW faults to `vmm_handle_cow_fault`.

### 5.2 Timer and Interrupts

The APIC timer (`kernel/arch/apic.c`) drives `scheduler_tick()` on each interrupt. The IDT gates point to the ISR stubs in `isr.asm`, which save and restore the context and call the C handler.

### 5.3 Device Drivers

- **Framebuffer** (`kernel/drivers/fb/`): `fb_draw.c`, `fb_ansi.c`, `fb_dev.c`. The framebuffer console parses ANSI sequences and provides `/dev/stdout` and `/dev/fbinfo`.
- **Serial** (`kernel/drivers/serial.c`): 16550-compatible serial output, used for `klog`.
- **PS/2 Keyboard** (`kernel/drivers/keyboard.c`): reads scancode set 1.
- **IDE** (`kernel/drivers/ide.c`): PIO mode IDE driver for PATA devices.
- **Block Device** (`kernel/drivers/block_device.c`): abstract `struct block_device`; IDE drives and partitions are registered here.

### 5.4 Console and Logging

`serial_puts()` writes to the serial port. The framebuffer console uses `/dev/stdout`. `klog()` is exposed to userspace as `SYS_KLOG`.

---

## 6. Memory Management

### 6.1 Physical Memory Manager (`kernel/mm/pmm.c`)

The PMM is a page-frame allocator. It is initialized from the Limine memory map. It maintains a free list of 4 KiB pages and supports reference counting for copy-on-write.

Exported functions:

- `pmm_init(struct limine_memmap_response *)`
- `pmm_alloc_page()`, `pmm_alloc_pages(size_t count)`
- `pmm_free_page(uint64_t phys)`, `pmm_free_pages(uint64_t phys, size_t count)`
- `pmm_incref(uint64_t phys)`, `pmm_get_refcount(uint64_t phys)`
- `pmm_stats(size_t *total, size_t *free)`

### 6.2 Virtual Memory Manager (`kernel/mm/vmm.c`)

The VMM manages x86-64 page tables. Kernel pages are mapped at the higher-half base `0xFFFFFFFF80000000`. User pages are mapped in the lower half.

Key functions:

- `vmm_init(uint64_t hhdm_offset)`
- `vmm_create_pml4()`
- `vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags)`
- `vmm_map_range(...)`
- `vmm_get_phys(...)`
- `vmm_switch_pml4(uint64_t pml4_phys)`
- `vmm_copy_userspace(uint64_t dst_pml4, uint64_t src_pml4)`
- `vmm_free_user_pages(uint64_t pml4_phys)`
- `vmm_handle_cow_fault(uint64_t pml4_phys, uint64_t virt)`

Supported page table flags are defined in `kernel/include/mm/vmm.h`: `PAGE_PRESENT`, `PAGE_WRITABLE`, `PAGE_USER`, `PAGE_GLOBAL`, `PAGE_COW`, `PAGE_NX`.

### 6.3 Kernel Heap (`kernel/mm/kmalloc.c`)

`kmalloc` and `kfree` provide a simple dynamic allocator over pages returned by the PMM. It is used for kernel objects such as `struct task`, VFS nodes, and ramfs inodes.

### 6.4 Userspace Memory

Each process has its own PML4. `fork()` copies the user address space using `vmm_copy_userspace`, marking writable pages as COW. `exec()` creates a new PML4, maps the ELF segments, and maps an argument page at `0x70000000`.

---

## 7. Processes and Scheduling

### 7.1 Task Structure

The task structure is defined in `kernel/include/process/task.h`:

```c
struct task {
    enum task_state state;
    uint64_t rsp;
    uint64_t rip;
    uint64_t ticks;
    void *stack;
    void *kernel_stack;
    uint64_t user_rsp;
    int priority;
    uint64_t sleep_until;
    void *arg;

    char name[TASK_NAME_LEN];
    uint64_t cr3;
    int parent_pid;
    int first_child;
    int exit_code;
    int wait_for_pid;
    int next_sibling;
    void *pending_free_kstack;
    char cwd[128];
    struct file *fds[MAX_FDS];
};
```

### 7.2 Scheduler

The scheduler (`kernel/scheduler/scheduler.c`) is a preemptive, round-robin scheduler with static priorities. It runs on the APIC timer. `TASK_MAX_PRIORITY` is 5; lower values run first. The active task is selected from the ready queue, and the idle task runs when no other task is ready.

Public scheduler functions (`kernel/include/process/scheduler.h`):

- `scheduler_init()`, `scheduler_tick()`, `scheduler_yield()`
- `task_create()`, `task_create_args()`, `task_create_current()`
- `task_exit_code(int)`, `task_wait(int)`, `task_kill(int)`, `task_sleep(uint64_t ms)`
- `task_set_priority(int id, int priority)`
- `task_fork(uint64_t saved_rsp)`

### 7.3 Process Lifecycle

- `fork()` in `kernel/process/fork.c` duplicates the current task, copies the page tables with COW, and copies the file descriptor table.
- `exec()` in `kernel/process/exec.c` loads an ELF from a VFS path and replaces the current address space.
- `exit()` in `kernel/process/process.c` sets the task state to `TASK_ZOMBIE`, stores the exit code, and reparents children to `init`.
- `wait()` in `kernel/process/process.c` blocks until the specified child exits.
- `kill()` in `kernel/process/process.c` terminates a process and wakes a waiting parent.

---

## 8. System Calls

The Crius syscall ABI is defined in `abi/crius/abi.h`. All syscalls are invoked from userspace with `int $0x80`:

- `RAX` = syscall number
- `RDI`, `RSI`, `RDX`, `R10`, `R8` = arguments 1..5
- `RAX` = return value

The dispatch table is in `kernel/syscall/syscall.c`.

### 8.1 Syscall Numbers

| Number | Name | Purpose |
|--------|------|---------|
| 0 | `SYS_EXIT` | terminate current process |
| 1 | `SYS_KILL` | kill a process by PID |
| 2 | `SYS_SLEEP` | sleep for milliseconds |
| 3 | `SYS_YIELD` | yield the CPU |
| 4 | `SYS_SETPRIORITY` | set task priority |
| 5 | `SYS_GETPID` | get current PID |
| 6 | `SYS_GETPROCINFO` | get `struct proc_info` for a PID |
| 7 | `SYS_WAIT` | wait for a child |
| 8 | `SYS_EXEC` | execute a new program |
| 9 | `SYS_FORK` | fork the current process |
| 10 | `SYS_MKDIR` | create a directory |
| 11 | `SYS_UNLINK` | remove a file or directory |
| 12 | `SYS_STAT` | get file type and size |
| 13 | `SYS_CHDIR` | change working directory |
| 14 | `SYS_GETCWD` | get current working directory |
| 15 | `SYS_MOUNT` | mount a file system |
| 16 | `SYS_UMOUNT` | unmount a file system |
| 17 | `SYS_MOUNT_COUNT` | get number of mounts |
| 18 | `SYS_MOUNT_POINT` | get mount path by index |
| 19 | `SYS_MOUNT_DEVICE` | get mount device by index |
| 20 | `SYS_OPEN` | open a file and get an FD |
| 21 | `SYS_CLOSE` | close an FD |
| 22 | `SYS_READ` | read from an FD |
| 23 | `SYS_WRITE` | write to an FD |
| 24 | `SYS_IOCTL` | device control |
| 25 | `SYS_REBOOT` | reboot the machine |
| 26 | `SYS_UPTIME` | get total scheduler ticks |
| 27 | `SYS_KLOG` | write a message to the kernel log |
| 28 | `SYS_BOOT_HAS_FB` | check if a framebuffer is available |
| 29 | `SYS_MEMSTATS` | get `struct mem_stats` |

### 8.2 Syscall Details

- `SYS_GETPROCINFO` writes `struct proc_info` to the user buffer. It includes `pid`, `state`, `name`, `ticks`, and `priority`.
- `SYS_EXEC` takes a path and an argument array. The kernel creates an `exec_ctx` on an argument page at `0x70000000`.
- `SYS_STAT` returns file type (`FILE_TYPE_FILE`, `FILE_TYPE_DIR`, `FILE_TYPE_DEV`) and size.
- `SYS_MOUNT` takes a device string and a mount path. It currently supports `ramfs`, `ext2`, and `devfs` (with device `devfs`).
- `SYS_IOCTL` for block devices supports `BLK_GET_INFO`, `BLK_READ_SECTOR`, and `BLK_WRITE_SECTOR`.
- `SYS_MEMSTATS` fills `struct mem_stats { size_t total_pages; size_t free_pages; }`.

Userspace wrappers are in `nexus/libc/src/proc.c`, `filesystem.c`, `mount.c`, `system.c`, and `process.c`.

---

## 9. File Systems

### 9.1 VFS

The Virtual File System is split across `kernel/fs/vfs/vfs.c` (path resolution and operation dispatch), `kernel/fs/vfs/vfs_mount.c` (mount table), and `kernel/include/fs/vfs.h`.

Key structures and functions:

- `struct filesystem_ops` — vtable with `mount`, `open`, `stat`, `create`, `mkdir`, `unlink`, `read_at`, `write_at`, `truncate`, `readdir`.
- `struct file` — file handle with `read`, `write`, `ioctl`, `readdir` ops, `fpos`, and `priv`.
- `vfs_init()`, `vfs_mount()`, `vfs_umount()`, `vfs_open()`, `vfs_close()`, `vfs_read()`, `vfs_write()`, `vfs_mkdir()`, `vfs_delete()`, `vfs_chdir()`, `vfs_pwd()`

The VFS supports overlapping mounts. The mount table stores the filesystem ops, private state, and an optional `struct block_device`. `find_mounts()` returns all matching mounts sorted by prefix length.

### 9.2 ramfs

`kernel/fs/ramfs/ramfs.c` and `ramfs_ops.c` implement an in-memory file system. Inodes and directory entries are stored in the kernel heap. ramfs is used as the root file system and for `/dev` special files before `devfs`.

Public API:

- `ramfs_init()`, `ramfs_mount()`
- `ramfs_create_device()`, `ramfs_mkdir()`, `ramfs_create_file()`

### 9.3 ext2

`kernel/fs/ext2/` contains a minimal ext2 driver (`ext2.c`, `ext2_ops.c`, `ext2_block.c`, `ext2_inode.c`, `ext2_alloc.c`, `ext2_dir.c`, `ext2_file.c`). It uses the block device abstraction (`kernel/drivers/block_device.c`) instead of calling IDE directly.

Path resolution (`ext2_resolve_path`, `ext2_split_path`) and per-file block allocation are in `ext2.c`.

### 9.4 devfs

`kernel/fs/devfs.c` implements device special files: `/dev/stdout`, `/dev/null`, `/dev/kbd`, `/dev/ttyS0`, `/dev/fb0`, `/dev/hda`, `/dev/proc/*`. It registers a `filesystem_ops` table and is mounted during `vfs_init()`.

### 9.5 procfs

`kernel/fs/procfs.c` provides process-related files. It is mounted during `vfs_init()`. It exposes per-process entries such as `/proc/<pid>/info` and `/proc/<pid>/mem`.

### 9.6 Block Device Abstraction

`kernel/include/drivers/block_device.h` defines `struct block_device` and the block I/O API. `kernel/drivers/block_device.c` implements the registry. IDE drives and their partitions are registered with `ide_register_block_devices()` in `kernel/drivers/ide.c`.

---

## 10. Userspace (Nexus)

### 10.1 libc

`nexus/libc/` provides the minimal runtime used by all userspace programs. It includes:

- `nexus/libc/src/process.c` — process syscalls (`fork`, `exec`, `wait`, `exit`, `kill`, `getpid`, `sleep`, `yield`)
- `nexus/libc/src/filesystem.c` — VFS syscalls (`open`, `close`, `read`, `write`, `mkdir`, `unlink`, `chdir`, `getcwd`, `stat`)
- `nexus/libc/src/mount.c` — mount syscalls
- `nexus/libc/src/system.c` — `reboot`, `uptime`, `klog`, `memstats`
- `nexus/libc/src/entry.c` — `_start` entry for every userspace binary
- `nexus/libc/include/crius/abi.h` — shared ABI header

The C library does not implement the full C standard. It provides only the functions used by the included userspace programs.

### 10.2 init

`nexus/init/init.c` is the first userspace process (`PID 1`). It sets up the initial file descriptors and spawns the shell.

### 10.3 Shell

The shell is in `nexus/shell/`:

- `shell.c` — main loop, command buffer, history, tab completion
- `shell_builtin.c` — built-ins (`cd`, `exit`, `export`, `echo`, `edit`, `which`)
- `shell_exec.c` — external program execution (`fork`, `exec`, `wait`)

### 10.4 Core Utilities

`nexus/coreutils/` contains one-file programs. Each is linked with `libc` and a per-program `PROG_MAIN`. They are built from `nexus/Makefile` using `tools/gen_progs.sh` and `tools/prog.ld`.

### 10.5 Editor and Tests

- `nexus/editor/editor.c` — a simple line editor.
- `nexus/tests/proctest.c` and `proctest_mem.c` — process subsystem tests.
- `nexus/tests/security_test.c` — security tests (bad pointers, unmapped memory).

---

## 11. Build and Development

### 11.1 Requirements

See `README.md` for the current list. In short:

- `gcc`, `ld`, `nasm`, `make`
- `git`, `curl`
- `xorriso`
- `qemu-system-x86_64` or equivalent
- `dd`, `sfdisk`, `mkfs.ext2` (for the optional `disk.img` test image)

### 11.2 Build Targets

| Target | Effect |
|--------|--------|
| `make` | build `crius.iso` |
| `make kernel` | build `build/kernel.elf` only |
| `make nexus` | build userspace binaries only |
| `make run` | run in QEMU with `disk.img` |
| `make disk.img` | create the 64 MB ext2 test image |
| `make clean` | remove build artifacts |

### 11.3 Linker Scripts

- `kernel/linker.ld` — kernel ELF; loads at higher-half.
- `tools/nexus.ld` — userspace base at `0x40000000`.
- `tools/prog.ld` — per-program layout used by `gen_progs.sh`.

### 11.4 Running

`make run` starts QEMU with the ISO and a 64 MB raw disk image. To boot the ISO in another emulator or on hardware, flash `crius.iso`.

---

## 12. Third-Party Components

### 12.1 Limine

Crius uses Limine `v12.3.3` as the boot loader. The root Makefile downloads the Limine binary release from GitHub. Limine provides the boot media, the `limine-bios-cd.bin`, `limine-uefi-cd.bin`, and `BOOTX64.EFI` files.

- Project: https://limine-bootloader.org/
- License: Limine has its own license, independent of Crius. See the Limine repository.

### 12.2 freestanding-c-hdrs

The build clones `https://github.com/osdev0/freestanding-c-hdrs.git` for the freestanding C standard headers. These headers are used by both the kernel and Nexus and are not part of the Crius source tree.

---

## 13. Limitations

The following limitations are derived from the current implementation:

- **Architecture**: x86-64 only. Only BIOS/legacy boot via Limine has been exercised; UEFI media files are produced but not extensively tested.
- **CPU**: single core only. There is no SMP or multi-CPU support.
- **Memory**: no swap, no demand paging beyond copy-on-write, and no memory hotplug. Physical memory is limited by the Limine memory map.
- **Processes**: a fixed `MAX_TASKS` (64) and `MAX_PROCS` (16) limit. The `init` process is hard-coded.
- **Scheduling**: simple round-robin with static priorities; no real-time classes or fair-share scheduling.
- **Networking**: none.
- **File systems**: ramfs is in-memory only. ext2 support is minimal and does not cover all ext2 feature flags, journal, or full write semantics. There is no permission/ownership model in ramfs.
- **Devices**: IDE driver uses PIO. AHCI, SATA, USB, and NVMe are not supported.
- **Userspace**: libc is intentionally minimal. No shared libraries, no dynamic linker, and no full POSIX compatibility.
- **Testing**: runtime testing has been done under QEMU only.
