CC      = gcc
LD      = ld
AS      = nasm
OBJCOPY = objcopy
AR      = ar

INC = -Iarch/x86_64 -Idrivers -Ikernel -Ifs -I.

CFLAGS  = -m64 -ffreestanding -fno-pic -fno-stack-protector \
          -fno-builtin -mno-red-zone -mcmodel=large -O2 \
          -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mno-avx \
          -mno-avx2 -mno-avx512f -mgeneral-regs-only \
          -fcf-protection=none -Wall -Wextra $(INC)

LIB_CFLAGS  = -m64 -ffreestanding -fno-pic -fno-stack-protector \
              -fno-builtin -mno-red-zone -mcmodel=large -O2 \
              -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mno-avx \
              -mno-avx2 -mno-avx512f -mgeneral-regs-only \
              -fcf-protection=none -Wall -Wextra -Ilib/include

USER_CFLAGS = $(LIB_CFLAGS) -nostdlib -nostdinc -static

ASFLAGS = -f elf64

LIB_OBJS = lib/start.o lib/syscall.o lib/string.o lib/printf.o lib/malloc.o

KERNEL_OBJS = \
    arch/x86_64/entry.o \
    arch/x86_64/isr.o \
    arch/x86_64/gdt.o \
    arch/x86_64/idt.o \
    arch/x86_64/syscall.o \
    arch/x86_64/usermode.o \
    arch/x86_64/task.o \
    drivers/ata.o \
    drivers/keyboard.o \
    drivers/pic.o \
    drivers/pit.o \
    drivers/irq.o \
    drivers/pci.o \
    drivers/rtc.o \
    drivers/serial.o \
    kernel/kernel.o \
    kernel/console.o \
    kernel/memory.o \
    kernel/elf.o \
    fs/hexfs.o \
    user/hello_bin.o \
    user/shell_bin.o \
    user/init_bin.o \
    user/hsl_bin.o \
    user/hexinstall_bin.o

all: os.img


help:
	@echo "HexOS build system"
	@echo ""
	@echo "Targets:"
	@echo "  make               Build os.img"
	@echo "  make run           Run in QEMU (serial on stdio)"
	@echo "  make run-nographic Run QEMU without graphics window"
	@echo "  make debug         Run QEMU with -d int,cpu_reset -no-reboot"
	@echo "  make deps          Check required tools"
	@echo "  make clean         Remove build artifacts"
	@echo "  make help          This message"
	@echo ""
	@echo "Output:"
	@echo "  kernel.bin         Raw kernel binary"
	@echo "  os.img             8 MiB bootable image"

deps:
	@echo "Checking build dependencies..."
	@for cmd in nasm gcc ld objcopy ar dd qemu-system-x86_64; do \
	    if command -v $$cmd >/dev/null 2>&1; then \
	        printf "  [ok]   %-25s %s\n" "$$cmd" "$$(command -v $$cmd)"; \
	    else \
	        printf "  [FAIL] %-25s not found\n" "$$cmd"; \
	        exit 1; \
	    fi; \
	done
	@echo "All dependencies found."


boot.bin: boot/boot.asm
	$(AS) -f bin -o $@ $<


arch/x86_64/%.o: arch/x86_64/%.asm
	$(AS) $(ASFLAGS) -o $@ $<


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


lib/%.o: lib/%.c
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

lib/libhexc.a: $(LIB_OBJS)
	$(AR) rcs $@ $^


user/hello.elf: user/hello.c lib/start.o lib/libhexc.a user.ld
	$(CC) $(USER_CFLAGS) -T user.ld -o $@ user/hello.c \
	    lib/start.o lib/libhexc.a

user/shell.elf: user/shell.c lib/start.o lib/libhexc.a user.ld
	$(CC) $(USER_CFLAGS) -T user.ld -o $@ user/shell.c \
	    lib/start.o lib/libhexc.a

user/init.elf: user/init.c lib/start.o lib/libhexc.a user.ld
	$(CC) $(USER_CFLAGS) -T user.ld -o $@ user/init.c \
	    lib/start.o lib/libhexc.a

user/hsl.elf: user/hsl.c lib/start.o lib/libhexc.a user.ld
	$(CC) $(USER_CFLAGS) -T user.ld -o $@ user/hsl.c \
	    lib/start.o lib/libhexc.a

user/hsl_bin.o: user/hsl.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

user/hello_bin.o: user/hello.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

user/shell_bin.o: user/shell.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

user/init_bin.o: user/init.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

boot_bin.o: boot.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@


user/hexinstall.elf: user/hexinstall.c \
                     user/hello_bin.o user/shell_bin.o \
                     user/init_bin.o boot_bin.o \
                     lib/start.o lib/libhexc.a user.ld
	$(CC) $(USER_CFLAGS) -T user.ld -o $@ user/hexinstall.c \
	    user/hello_bin.o user/shell_bin.o user/init_bin.o boot_bin.o \
	    lib/start.o lib/libhexc.a

user/hexinstall_bin.o: user/hexinstall.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@


kernel.elf: $(KERNEL_OBJS) link.ld
	$(LD) -T link.ld -o $@ $(KERNEL_OBJS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@


os.img: boot.bin kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=16384
	dd if=boot.bin of=$@ conv=notrunc
	dd if=kernel.bin of=$@ bs=512 seek=1 conv=notrunc


run: os.img
	qemu-system-x86_64 -drive format=raw,file=os.img -serial stdio

run-nographic: os.img
	qemu-system-x86_64 -drive format=raw,file=os.img -serial stdio -display none

debug: os.img
	qemu-system-x86_64 -drive format=raw,file=os.img -serial stdio \
	    -no-reboot -d int,cpu_reset


clean:
	rm -f *.o *.elf *.bin *.img boot_bin.o
	rm -f arch/x86_64/*.o drivers/*.o kernel/*.o fs/*.o user/*.o
	rm -f user/*.elf lib/*.o lib/libhexc.a

.PHONY: all help deps run run-nographic debug clean
