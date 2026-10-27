# LED Control (Brick Pro) - cross-compile in the tg5040 toolchain container
CROSS   ?= aarch64-nextui-linux-gnu-
SYSROOT ?= /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
CC      = $(CROSS)gcc
CFLAGS  = -O2 -Wall -Wextra --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include/SDL2
LDFLAGS = --sysroot=$(SYSROOT) -lSDL2 -lSDL2_ttf -lm

.PHONY: all clean
all: build/ledbp

build/ledbp: main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf build
