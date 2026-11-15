#!/bin/sh
# Drive the host build through the main screens; plat_host with capture writes
# captures/*.txt (UTF-8). Run through `make captures`, which first makes the
# CP437 copies of the sources in build/cp437 (LPS-DOS-115-CHG03).
set -e
cd "$(dirname "$0")/.."
G=build/cp437
gcc -std=c89 -Wall -I$G -DLPS_HOST -o tools/capture_drive $G/capture_drive.c \
  $G/state.c $G/ics.c $G/save.c $G/jsave.c $G/paths.c $G/rosary.c $G/app.c $G/cp437.c $G/wrap.c $G/plat_host.c
mkdir -p captures
rm -rf /tmp/lps-msdos-cap
mkdir -p /tmp/lps-msdos-cap
./tools/capture_drive
echo "Captures written under captures/"
ls -la captures | head
