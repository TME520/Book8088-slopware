# Book8088-slopware

AI-hallucinated software for the mighty Book 8088 (I do what I want)

Lightweight programs for a Book 8088: Intel 8088, 640 KB RAM, HDD, EGA, MS-DOS 7.

Ready-to-run LPS Companion binaries are at the repository root. Copy them straight onto the Book 8088:

- [`LPSCOMP.EXE`](LPSCOMP.EXE) — the companion
- [`ROSARY.DAT`](ROSARY.DAT) — the rosary prayer texts; keep it **in the same directory as `LPSCOMP.EXE`**
- [`HELLO.EXE`](HELLO.EXE) — BIOS smoke test

Copy both `LPSCOMP.EXE` and `ROSARY.DAT`. Without `ROSARY.DAT` everything works except the Rosary, which then says the file is missing. Always update the two together: since LPS-DOS-115-CHG03 (French accents) an older `ROSARY.DAT` is refused as damaged.

Since LPS-DOS-115-CHG04 the data folder (`C:\LPS\`) holds the same JSON saves as the PicoCalc, `SAVE_A.JSN` and `SAVE_B.JSN`, so you can copy your LPS data between the PicoCalc SD card (`LPS/`) and `C:\LPS\` in either direction. Old `SAVE_A.BIN`/`SAVE_B.BIN` files are read once, converted, and left untouched.

Sources and rebuild instructions are in [`lps-companion/`](lps-companion/). A local `make dos rosary` writes new copies there; those copies stay untracked. The root files above are the ones to download.

| Program | Directory | What it is |
|---|---|---|
| LPS Companion | [`lps-companion/`](lps-companion/) | MS-DOS port of [LPS Companion v1.15](https://github.com/TME520/LPS-Companion-PicoCalc) (Open Watcom, real-mode 8086, 80×25 text, EGA Meal Planner). Run [`LPSCOMP.EXE`](LPSCOMP.EXE) (with [`ROSARY.DAT`](ROSARY.DAT) and [`MEALS.DAT`](MEALS.DAT) beside it) and [`HELLO.EXE`](HELLO.EXE) from this repository root. |
