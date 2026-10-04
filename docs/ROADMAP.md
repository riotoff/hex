# Hex OS — Roadmap

## Done

### v1.0–v1.6
- Bootloader (MBR, E820, A20, long mode)
- GDT, TSS, ring 3
- IDT, PIC, PIT, PS/2 keyboard, RTC, serial
- Memory manager (bitmap + kernel heap)
- ATA PIO + bus-master DMA
- PCI enumerator
- HexFS v6 (inodes, indirect blocks, timestamps, uid/gid)
- Syscalls 0–14
- ELF64 loader
- User shell `/bin/sh`
- Kernel shell with ~60 commands

### v1.7
- `libhexc.a` (unistd, fcntl, stdio, stdlib, string, hexos)
- Boot menu: Boot / Install / Kernel shell
- `/bin/hexinstall` — format, install, write MBR
- Auto-launch `/bin/init`
- Nested spawn (depth 4)
- Kernel task scheduler (cooperative + preemptive via PIT)

### v1.8 — GitHub release
- README, ARCHITECTURE, ROADMAP
- GPL-3.0 license
- `.gitignore`
- `make deps`, `make help`, `make debug`, `make run-nographic`
- Screenshots
- Removal of leftover hacks (halt-on-exit, prompt on same line,
  clear via newlines)

### v1.9 — HexFS versioning
File system bumps to v7. Every overwrite creates a new version.
Old versions are kept and can be restored.

- Inode field `versions_lba` — pointer to a version chain on disk
- Version table: LBA 267–778, one block per version, up to 511 versions
- `hexfs_write` snapshots the current content before overwrite
- New API: `hexfs_count_versions`, `hexfs_list_versions`,
  `hexfs_read_version`, `hexfs_version_stat`
- Syscalls:
  - #16 `clear`
  - #17 `write_file`
  - #18 `hexlog` — list versions of a file
  - #19 `hexcheckout` — restore a specific version
- User shell:
  - `write PATH TEXT`
  - `history PATH`
  - `checkout PATH VER`
  - `clear` (real screen clear via syscall)
  - Line editor with history (up/down), cursor (left/right, home/end),
    delete, Ctrl+C, Ctrl+L

## In progress

### v1.10 — Userspace memory
Real memory allocation for user programs, plus filesystem hygiene.

- Per-process `brk`, stored in syscall state, saved/restored on spawn
- Syscall #20 `brk(new)` — returns current break
- `lib/start.c` initializes the break from the ELF load end
- `lib/malloc.c` — replace the static 64 KiB bump allocator with a
  proper linked-list allocator, `free` supported
- `hexfs_gc(ino, keep_n)` — keep last N versions, drop older ones
- Syscall #21 `hexgc`
- User command `gc PATH N`
- `diskinfo` reports used version-table blocks

## Planned

### v2.0 — HSL (Hex Shell Language)
An object shell, not a bash clone. Replaces `/bin/sh` eventually.

```hsl
files = ls /bin | where { .size > 1024 } | sort by .mtime desc
echo "top file: " + files[0].name
for f in files { cp $f /backup/ }
```

- Objects instead of strings
- `.field` for property access
- `|` pipes objects
- `{ ... }` lambdas
- `for x in ... {}` control flow
- Builtin types: `File`, `Dir`, `Process`, `Task`
- Interpreter in `user/hsl.c` — lexer, recursive-descent parser,
  evaluator, environment
- Requires v1.10 (`malloc` for AST and object trees)

### v2.1 — hexlog
Structured event log. Developer-friendly tracing.

- Ring buffer of syscalls, IRQs, task switches
- `hexlog last 100`
- `hexlog sc filter=write`
- `hexlog dump /var/log/hexlog.bin`
- Per-process syscall trace (like `strace`)

### v2.2 — Recovery boot menu
Boot into a safe/recovery mode.

- Safe mode (no drivers, no FS)
- `fsck` for HexFS
- Snapshot restore
- Reinstall without touching `/home`
- Serial-only mode

## Long-term

### v3.0 — Real processes
- Per-process PML4 (separate address spaces)
- `sys_fork`, `sys_exec`, `sys_wait`
- `sys_getpid`, `sys_getppid`
- `sys_mmap`
- `sys_signal`, `sys_kill`
- Real PID 1 (`/bin/init`)

### v3.1 — Networking
- e1000 driver
- ARP, IP, ICMP, UDP, TCP
- Sockets API
- `ping`, `wget`

### v3.2 — Modern hardware
- AHCI (SATA)
- USB (UHCI/EHCI)
- VBE framebuffer
- PS/2 mouse
- AC'97 / HDA

### v4.0 — Self-hosting
- Own C compiler
- Own assembler
- Own make
- Own text editor
- Compile the kernel inside Hex OS

### Beyond
- SMP + APIC
- Microkernel split
- ARM64 port
- GUI toolkit
