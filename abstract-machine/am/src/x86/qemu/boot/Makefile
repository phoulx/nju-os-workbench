SRCS := start.S main.c
bootblock.o: $(SRCS) Makefile
	@echo + CC $(SRCS)
	@$(CROSS_COMPILE)gcc -ggdb -static -m32 -fno-pic -Os -nostdlib -Ttext 0x7c00 -I$(AM_HOME)/am/src -o bootblock.o $(SRCS)
	@cp bootblock.o boot.o
	@python3 genboot.py bootblock.o

clean:
	rm -rf *.o
