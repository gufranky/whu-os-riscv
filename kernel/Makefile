include ../common.mk

MOUDLES = $(shell ls -d */)
OBJS    = $(shell ls -d ./*/*.o ./*/*/*.o)
KERNEL_ELF = ../kernel-qemu
KERNEL_LD  = kernel.ld

.PHONY: build clean $(MOUDLES) 

$(MOUDLES):
	$(MAKE) build --directory=$@

$(KERNEL_ELF): $(MODULES) $(KERNEL_LD)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) -o $(KERNEL_ELF) $(OBJS)

build: $(MOUDLES) $(KERNEL_ELF)

clean:
	for d in $(MOUDLES); \
		do \
			$(MAKE) clean --directory=$$d; \
		done
	rm -f ${KERNEL_ELF}