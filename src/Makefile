# Makefile — Keringő OS (csupasz fém i386 kernel, multiboot)
# Eszköztár: a projekt tools/root előtagjából (nincs root-jog igénye)

T      := /home/z/my-project/tools/root
export LD_LIBRARY_PATH := $(T)/usr/lib/x86_64-linux-gnu
PATH   := $(T)/usr/bin:$(PATH)
export PATH

CC     := gcc
AS     := gcc
LD     := ld
OBJDIR := build

CFLAGS := -m32 -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -O2 -Wall -Wextra -I.
ASFLAGS := -m32 -I.

OBJS := $(patsubst %.S,$(OBJDIR)/%.o,$(wildcard *.S)) $(patsubst %.c,$(OBJDIR)/%.o,$(wildcard *.c))

all: kernel.elf

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c kernel.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ $(OBJS)
	@echo "kernel.elf: $$(stat -c%s $@) bájt"

iso: kernel.elf
	rm -rf $(OBJDIR)/isodir
	mkdir -p $(OBJDIR)/isodir/boot/grub
	cp kernel.elf $(OBJDIR)/isodir/boot/keringo.elf
	cp grub.cfg $(OBJDIR)/isodir/boot/grub/grub.cfg
	mkdir -p $(OBJDIR)/isodir/adatok
	cp adatok/* $(OBJDIR)/isodir/adatok/ 2>/dev/null || true
	cp adatok/*.TXT $(OBJDIR)/isodir/ 2>/dev/null || true
	cp adatok/KERINGO.CFG $(OBJDIR)/isodir/ 2>/dev/null || true
	grub-mkimage -d $(T)/usr/lib/grub/i386-pc -O i386-pc-eltorito -p '(cd)/boot/grub' \
		-o $(OBJDIR)/eltorito.img \
		biosdisk iso9660 multiboot normal configfile echo ls test minicmd gzio halt reboot vbe
	cp $(OBJDIR)/eltorito.img $(OBJDIR)/isodir/boot/zzboot.img
	xorrisofs -V KERINGO_05 -o keringo-0.5.0.iso \
		-b boot/zzboot.img -c /boot.catalog \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-J -r --sort-weight 1000000 /adatok --sort-weight 1000000 /0UDVOZLET.TXT --sort-weight 1000000 /KERINGO.CFG --sort-weight 0 /boot/zzboot.img \
		$(OBJDIR)/isodir
	@echo "keringo-0.5.0.iso: $$(stat -c%s keringo-0.5.0.iso) bájt"

run: iso
	$(T)/usr/bin/qemu-system-i386 -L $(T)/usr/share/qemu \
		-m 256 -vga std -display none \
		-serial stdio \
		-drive file=keringo-0.5.0.iso,media=cdrom,if=ide,index=0 \
		-boot d

clean:
	rm -rf $(OBJDIR) kernel.elf keringo-0.5.0.iso

.PHONY: all iso run clean
