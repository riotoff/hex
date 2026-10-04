# HexOS — Architecture

## 1. Boot

```
BIOS → MBR (boot.asm) → long mode → _start → kernel_main
```

1. BIOS loads sector 0 to `0x7C00`.
2. `boot/boot.asm`:
   - Sets up segments, stack at `0x7C00`.
   - Reads E820 memory map into `0x5000`.
   - Enables A20 via port `0x92`.
   - Reads 512 sectors of the kernel from LBA 1 to `0x10000` using `int 0x13`.
   - Builds identity page tables at `0x1000` (PML4 → PDPT → PD).
   - Enables PAE, LME, paging, jumps to `lm_entry`.
3. `lm_entry`:
   - Loads data segments with `0x10`.
   - Sets `rsp = 0x2000000`.
   - Jumps to `0x10000`.
4. `arch/x86_64/entry.asm::_start`:
   - Zeroes `.bss`.
   - Calls `kernel_main`.

## 2. Memory map

| Range | Contents |
|---|---|
| `0x00000000–0x000FFFFF` | Reserved (BIOS, VGA text at `0xB8000`) |
| `0x00010000–0x00011760` | Kernel image (`.text`, `.rodata`, `.data`, `.bss`) |
| `0x00011760–0x00100000` | Free |
| `0x00100000–0x01100000` | Kernel heap (16 MiB) |
| `0x02000000` | Kernel stack (set by boot.asm) |
| `0x04000000+` | User programs |
| `0x00000000–0x40000000` | Identity mapped (1 GiB) |

Kernel stack is at 32 MiB because `.bss` ends around 1 MiB and the heap
extends up to about 17 MiB. Nothing else lives near 32 MiB.

`link.ld` places `.bss` at `0x100000` so it does not overlap the VGA
buffer at `0xB8000`. GCC with `-mcmodel=large` puts large arrays into a
separate `.lbss` section; the linker script collects both.

## 3. GDT and ring 3

Seven GDT entries (`arch/x86_64/gdt.c`):

| Index | Selector | Purpose |
|---|---|---|
| 0 | `0x00` | Null |
| 1 | `0x08` | Kernel code (DPL=0) |
| 2 | `0x10` | Kernel data (DPL=0) |
| 3 | `0x18` | 32-bit kernel code (reserved) |
| 4 | `0x20` | 32-bit kernel data (reserved) |
| 5 | `0x28` | TSS (holds `rsp0`) |
| 6 | `0x1B` | User code (DPL=3) |
| 7 | `0x23` | User data (DPL=3) |

`usermode.c::um_wrapper` builds an `iretq` frame: `ss, rsp, rflags, cs, rip`.
On the next interrupt from ring 3, the CPU switches to `TSS.rsp0` before
calling the handler.

## 4. Interrupts

- IDT with 256 entries (`arch/x86_64/idt.c`).
- Exception stubs 0–31, IRQ stubs 32–47, syscall at `0x80`.
- `isr_common` in `isr.asm` pushes all GPRs, calls `isr_handler`, pops, `iretq`.
- `isr_handler`:
  - `vector < 32` → panic (dump registers to serial, halt).
  - `vector == 0x80` → `syscall_dispatch`.
  - `vector == 32` (IRQ0, PIT) → `task_schedule_from_irq` (preemption).
  - else → `irq_dispatch`.

## 5. Syscalls

`int 0x80` with `rax = number`, `rdi/rsi/rdx = args`.
Implementation in `arch/x86_64/syscall.c`. See the table in README.

File descriptors:

- `0` — keyboard
- `1` — VGA + serial
- `2` — serial
- `3+` — HexFS file

Errors return negative values.

## 6. HexFS

On-disk layout:

| LBA | Contents |
|---|---|
| 0 | MBR (bootloader) |
| 1–511 | Kernel |
| 201 | Superblock |
| 202 | Inode bitmap |
| 203–234 | Data block bitmap (32 sectors = 16384 blocks) |
| 235–266 | Inode table (128 inodes × 128 B) |
| 267+ | Data blocks |

Inode (128 bytes):

```c
struct hexfs_inode {
    uint8_t  type;          // 0=free, 1=file, 2=dir
    uint8_t  reserved[3];
    uint32_t size;
    uint32_t direct[12];    // 10 direct + 1 single + 1 double indirect
    uint32_t parent;
    uint16_t mode;
    uint16_t uid, gid;
    uint32_t ctime, mtime, atime;
    uint8_t  reserved2[48];
};
```

Maximum file size: 8 MiB.

Directory entry (32 bytes):

```c
struct hexfs_dirent {
    uint32_t inode;
    uint8_t  type;
    char     name[27];
};
```

## 7. Scheduler

`arch/x86_64/task.c`. Round-robin, preemptive.

- Each kernel task has its own stack (16 KiB) and a `saved_regs` copy.
- PIT fires at 100 Hz, `task_schedule_from_irq` saves the current task's
  registers and restores the next task's registers.
- `task_yield()` triggers `int $0x20` to force a switch.
- `task_exit()` marks the task as ZOMBIE and yields forever.

User programs are not kernel tasks yet. `spawn` is synchronous: the
parent is paused, the child runs, and on `exit` the parent is restored.
This is documented as a limitation, see ROADMAP v3.0.

## 8. Userspace

- `user.ld` links programs at `0x04000000`.
- `lib/start.c` provides `_start`, which calls `main` and then `_exit`.
- libc (`libhexc.a`) wraps `int 0x80` calls.
- `malloc` is a bump allocator on a 64 KiB static buffer. See ROADMAP
  for a real `sys_brk`.

## 9. Serial log

Everything the kernel prints to VGA is also sent to COM1 at 115200 8N1.
With `qemu-system-x86_64 -serial stdio`, this gives a clean log.

Tags:

- `[mem]` — memory subsystem
- `[ata]` — ATA driver
- `[hexfs]` — filesystem
- `[install]` — auto-install on boot
- `[kernel]` — kernel main
- `[sc]` — syscall (debug only)
- `[task]` — scheduler
- `*** HEX KERNEL PANIC ***` — exception

## 10. Reading order

1. `boot/boot.asm`
2. `arch/x86_64/entry.asm`
3. `kernel/kernel.c::kernel_main`
4. `arch/x86_64/idt.c`
5. `arch/x86_64/syscall.c`
6. `fs/hexfs.c`
7. `lib/` and `user/`
