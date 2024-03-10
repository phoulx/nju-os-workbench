.PHONY: build-arg

smp        ?= 1
LDFLAGS    += -N -Ttext-segment=0x00100000
QEMU_FLAGS += -serial mon:stdio \
              -machine accel=tcg \
              -smp "$(smp),cores=$(smp),sockets=1" \
              -drive format=raw,file=$(IMAGE)

build-arg: image
	@( echo -n $(mainargs); ) | dd if=/dev/stdin of=$(IMAGE) bs=512 count=2 seek=1 conv=notrunc status=none

BOOT_HOME := $(AM_HOME)/am/src/x86/qemu/boot

image: $(IMAGE).elf
	@$(MAKE) -s -C $(BOOT_HOME)
	@echo + CREATE "->" $(IMAGE_REL)
	@( cat $(BOOT_HOME)/bootblock.o; head -c 1024 /dev/zero; cat $(IMAGE).elf ) > $(IMAGE)
