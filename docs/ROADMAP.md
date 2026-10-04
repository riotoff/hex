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

## In progress

### v1.8 — GitHub release
- README, ARCHITECTURE, ROADMAP
- GPL-3.0 license
- `.gitignore`
- `make deps`, `make help`, `make debug`
- Screenshots
- Remove leftover hacks

## Planned

### v1.9 — HexFS versioning
Each file keeps a chain of previous versions.

- New inode type `INODE_FILE_VERSIONED`
- Inode points to a version chain on disk
- `hexlog file.txt` — show history
- `hexcheckout file.txt N` — revert to version N
- Snapshot on overwrite, optional GC

### v2.0 — HSL (Hex Shell Language)
An object shell, not a bash clone.

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

### v2.1 — hexlog
Structured event log.

- Ring buffer of syscalls, IRQs, task switches
- `hexlog last 100`
- `hexlog sc filter=write`
- `hexlog dump /var/log/hexlog.bin`

### v2.2 — Recovery boot menu
- Safe mode (no drivers, no FS)
- `fsck` for HexFS
- Snapshot restore
- Reinstall without touching `/home`
- Serial-only mode

## Long-term

### v3.0 — Real processes
- Per-process PML4
- `sys_fork`, `sys_exec`, `sys_wait`
- `sys_getpid`, `sys_getppid`
- `sys_brk`, `sys_mmap`
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
