# Makefile for Meroko, a TI Explorer I Lisp machine emulator
#
#   make             build the emulator and unpack the disk images
#   make run         build, then start the emulator
#   make reset-disks throw away the working disks and unpack fresh ones
#   make clean       remove build products (keeps the working disks)
#   make distclean   also remove working disks and run-time files
#
#   make ENET=1      include the (unfinished) Ethernet board; see README

CC       = gcc
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

# The Explorer's screen is 1024 x 808.
DEFINES  = -DDISPLAY_SDL -DFB_WIDTH=1024 -DFB_HEIGHT=808
ifdef ENET
DEFINES += -DENABLE_ENET
endif
# Other debugging defines: -DTRACELOG -DDEI_TRACELOG -DTRACELOG2 -DI_PARITY_CHECK

# -fgnu89-inline, -fwrapv and -fno-strict-aliasing keep this 2005-era code
# behaving the way its authors' compilers treated it.
CFLAGS   = -O2 -g -Wall -std=gnu11 -fgnu89-inline -fwrapv -fno-strict-aliasing \
           $(DEFINES) $(SDL_CFLAGS)
LDLIBS   = $(shell pkg-config --libs ncurses 2>/dev/null || echo -lncurses) $(SDL_LIBS)

OBJS = src/meroko.o src/nubus.o src/localbus.o src/raven_cpu.o src/nupi.o \
       src/mem8.o src/sib.o src/enet.o src/sdl.o

DISKS = disks/c0-d0.dsk disks/c0-d1.dsk disks/c2-d0.dsk

all: meroko disks tools/epart

meroko: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c $< -o $@

disks: $(DISKS)

# The emulator writes to the .dsk files, so they are only unpacked when
# missing (order-only prerequisite) and never overwritten behind your back.
disks/%.dsk: | disks/%.dsk.xz
	xz -dc disks/$*.dsk.xz > $@

reset-disks:
	rm -f $(DISKS)
	$(MAKE) disks

tools/epart: tools/epart.c
	$(CC) -O2 -std=gnu11 -o $@ $<

run: all
	./meroko

clean:
	rm -f src/*.o meroko tools/epart

distclean: clean
	rm -f $(DISKS) NVRAM.BIN DEBUG.LOG *.DUMP SCREENSHOT.BMP

.PHONY: all disks reset-disks run clean distclean
.PRECIOUS: $(DISKS)
