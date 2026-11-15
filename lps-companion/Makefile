# LPS Companion MS-DOS — Open Watcom (-0 8086/8088) + host gcc tests.
# Export WATCOM to the Open Watcom root (the directory that contains binl64/ and h/)
# before `make dos` or `make hello`. Host tests do not need it.

HOST_CC = gcc
HOST_WARN = -std=c89 -Wall -Wextra -Wno-unused-parameter -O2 -DLPS_HOST

# Text encoding (LPS-DOS-115-CHG03). The sources in src/ and tests/ are UTF-8,
# with the same French wording and accents as the PicoCalc. The screen is the
# PC text-mode font, CP437, so tools/cp437 copies every source into $(GEN) and
# rewrites each accented character inside a string literal as its CP437 byte
# (an octal escape). Every build - DOS, host tests, mkrosary - compiles those
# copies, so all of them see the same bytes. Characters CP437 lacks (capital
# A/E/O grave or circumflex...) are folded; an unknown one stops the build.
# Files written to disk stay UTF-8: ics.c converts CP437 back (cp437.c).
GEN = build/cp437
HOST_CFLAGS = $(HOST_WARN) -I$(GEN)

SRC_COMMON = state.c ics.c save.c paths.c rosary.c app.c cp437.c wrap.c
HEADERS = $(GEN)/lps.h $(GEN)/rosary.h
# rosary_text.c is the single source of the prayer texts. It is NOT linked into
# LPSCOMP.EXE: tools/mkrosary turns it into ROSARY.DAT (LPS-DOS-115-CHG03b).
ROSARY_SRC = $(GEN)/rosary_text.c
DOS_OBJS = $(addprefix $(GEN)/,$(SRC_COMMON) plat_dos.c main.c)
HOST_OBJS = $(addprefix $(GEN)/,$(SRC_COMMON) plat_host.c main.c)
COMMON_OBJS = $(addprefix $(GEN)/,$(SRC_COMMON))

# -0: 8086/8088, -fpc: software FP (no 8087), -k4096: stack.
# -ml (LPS-DOS-115-CHG03b): large model. Data pointers are far, so malloc()
# hands out far-heap memory: the big work buffers (lps_buffers_init), the App,
# the screen shadow and ROSARY.DAT all live outside the 64 KB DGROUP. Code is
# far too, so each module gets its own code segment instead of sharing one
# 64 KB _TEXT. -zt256: any static object over 256 bytes gets a far segment.
DOS_CFLAGS = -0 -bt=dos -ml -zt256 -zq -ox -fpc -k4096 -dLPS_DOS -i=$(GEN)

ifeq ($(wildcard $(WATCOM)/binl64/wcl),)
WBIN = $(WATCOM)/binl
else
WBIN = $(WATCOM)/binl64
endif
WCL = $(WBIN)/wcl

.PHONY: all dos host test clean captures hello rosary

# tools/cp437 and its table are plain ASCII C, built straight from src/.
tools/cp437: tools/cp437.c src/cp437.c src/lps.h
	$(HOST_CC) $(HOST_WARN) -Isrc -o tools/cp437 tools/cp437.c src/cp437.c

$(GEN)/%.c: src/%.c tools/cp437
	@mkdir -p $(GEN)
	./tools/cp437 $< $@
$(GEN)/%.h: src/%.h tools/cp437
	@mkdir -p $(GEN)
	./tools/cp437 $< $@
$(GEN)/test_storage.c: tests/test_storage.c tools/cp437
	@mkdir -p $(GEN)
	./tools/cp437 $< $@
$(GEN)/mkrosary.c: tools/mkrosary.c tools/cp437
	@mkdir -p $(GEN)
	./tools/cp437 $< $@
$(GEN)/capture_drive.c: tools/capture_drive.c tools/cp437
	@mkdir -p $(GEN)
	./tools/cp437 $< $@

all: dos hello rosary host test

dos: LPSCOMP.EXE

# ROSARY.DAT ships next to LPSCOMP.EXE. Built on the host, byte-for-byte
# deterministic, so CI can compare it with the committed root copy.
rosary: ROSARY.DAT

ROSARY.DAT: tools/mkrosary
	./tools/mkrosary ROSARY.DAT

tools/mkrosary: $(GEN)/mkrosary.c $(ROSARY_SRC) $(GEN)/state.c $(GEN)/cp437.c $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) -o tools/mkrosary $(GEN)/mkrosary.c $(ROSARY_SRC) $(GEN)/state.c $(GEN)/cp437.c

LPSCOMP.EXE: $(DOS_OBJS) $(HEADERS)
	@if [ -z "$(WATCOM)" ]; then \
	  echo "WATCOM is not set. Export WATCOM to the Open Watcom root (contains binl64/ and h/)."; \
	  exit 1; \
	fi
	WATCOM="$(WATCOM)" INCLUDE="$(WATCOM)/h" LIB="$(WATCOM)/lib286" PATH="$(WBIN):$$PATH" \
	  "$(WCL)" $(DOS_CFLAGS) -fe=LPSCOMP.EXE $(DOS_OBJS)

hello: HELLO.EXE

HELLO.EXE: src/hello.c
	@if [ -z "$(WATCOM)" ]; then \
	  echo "WATCOM is not set. Export WATCOM to the Open Watcom root (contains binl64/ and h/)."; \
	  exit 1; \
	fi
	WATCOM="$(WATCOM)" INCLUDE="$(WATCOM)/h" LIB="$(WATCOM)/lib286" PATH="$(WBIN):$$PATH" \
	  "$(WCL)" -0 -bt=dos -ms -zq -ox -fpc -k2048 -fe=HELLO.EXE src/hello.c

host: lpscomp-host

lpscomp-host: $(HOST_OBJS) $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) -o lpscomp-host $(HOST_OBJS)

test: tests/test_storage ROSARY.DAT
	./tests/test_storage

tests/test_storage: $(GEN)/test_storage.c $(COMMON_OBJS) $(ROSARY_SRC) $(GEN)/plat_host.c $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) -o tests/test_storage $(GEN)/test_storage.c $(COMMON_OBJS) $(ROSARY_SRC) $(GEN)/plat_host.c

captures: $(GEN)/capture_drive.c $(COMMON_OBJS) $(GEN)/plat_host.c $(HEADERS)
	./tools/drive_captures.sh

clean:
	rm -f LPSCOMP.EXE HELLO.EXE ROSARY.DAT lpscomp-host tests/test_storage tools/capture_drive tools/mkrosary tools/cp437 *.o src/*.o tests/*.o *.err *.map
	rm -rf build
