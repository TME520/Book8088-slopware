# LPS Companion MS-DOS — Open Watcom (-0 8086/8088) + host gcc tests.
# Export WATCOM to the Open Watcom root (the directory that contains binl64/ and h/)
# before `make dos` or `make hello`. Host tests do not need it.

HOST_CC = gcc
HOST_CFLAGS = -std=c89 -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -DLPS_HOST

SRC_COMMON = src/state.c src/ics.c src/save.c src/paths.c src/rosary.c src/app.c
DOS_OBJS = $(SRC_COMMON) src/plat_dos.c src/main.c
HOST_OBJS = $(SRC_COMMON) src/plat_host.c src/main.c

# -0: 8086/8088, -ms: small, -fpc: software FP (no 8087), -k4096: stack
DOS_CFLAGS = -0 -bt=dos -ms -zq -ox -fpc -k4096 -dLPS_DOS -i=src

ifeq ($(wildcard $(WATCOM)/binl64/wcl),)
WBIN = $(WATCOM)/binl
else
WBIN = $(WATCOM)/binl64
endif
WCL = $(WBIN)/wcl

.PHONY: all dos host test clean captures hello

all: dos hello host test

dos: LPSCOMP.EXE

LPSCOMP.EXE: $(DOS_OBJS) src/lps.h src/rosary.h
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

lpscomp-host: $(HOST_OBJS) src/lps.h src/rosary.h
	$(HOST_CC) $(HOST_CFLAGS) -o lpscomp-host $(HOST_OBJS)

test: tests/test_storage
	./tests/test_storage

tests/test_storage: tests/test_storage.c $(SRC_COMMON) src/plat_host.c src/lps.h src/rosary.h
	$(HOST_CC) $(HOST_CFLAGS) -o tests/test_storage tests/test_storage.c $(SRC_COMMON) src/plat_host.c

captures:
	./tools/drive_captures.sh

clean:
	rm -f LPSCOMP.EXE HELLO.EXE lpscomp-host tests/test_storage tools/capture_drive *.o src/*.o tests/*.o *.err
