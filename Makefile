include common.mk

KERN = kernel
USER = user
MKFS = mkfs
KERNEL_ELF = kernel-qemu
FS_IMG = fs.img
CPUNUM = 1

.PHONY: clean $(KERN) $(USER) $(MKFS)

$(KERN):
	$(MAKE) build --directory=$@

$(USER):
	$(MAKE) init --directory=$@

$(MKFS):
	$(MAKE) build --directory=$@
	$(MKFS)/mkfs $(FS_IMG)

# QEMU相关配置
QEMU     =  qemu-system-riscv64
QEMUOPTS =  -machine virt -bios none -kernel $(KERNEL_ELF) 
QEMUOPTS += -m 128M -smp $(CPUNUM) -nographic
QEMUOPTS += -drive file=$(FS_IMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# 调试
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

build: $(USER) $(KERN) $(MKFS)

# qemu运行
qemu: $(USER) $(KERN) $(MKFS)
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $(USER) $(KERN) $(MKFS) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

clean:
	$(MAKE) --directory=$(KERN) clean
	$(MAKE) --directory=$(MKFS) clean
	rm -f $(KERNEL_ELF) $(FS_IMG) .gdbinit