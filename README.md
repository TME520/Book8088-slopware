# Book8088-slopware

AI-hallucinated software for the mighty Book 8088 (I do what I want)

Lightweight programs for a Book 8088: Intel 8088, 640 KB RAM, HDD, CGA, MS-DOS 7 (EGA/VGA PCs work too).

Ready-to-run LPS Companion binaries are at the repository root. Copy them straight onto the Book 8088:

- [`LPSCOMP.EXE`](LPSCOMP.EXE) — the companion
- [`ROSARY.DAT`](ROSARY.DAT) — the rosary prayer texts
- [`MEALS.DAT`](MEALS.DAT) — the Meal Planner card pictures for EGA/VGA
- [`MEALSCGA.DAT`](MEALSCGA.DAT) — the same pictures dithered for CGA (the Book 8088's screen)
- [`HELLO.EXE`](HELLO.EXE) — BIOS smoke test

Copy `LPSCOMP.EXE`, `ROSARY.DAT`, `MEALS.DAT` and `MEALSCGA.DAT` **into the same directory**, and always update them together. Without `ROSARY.DAT` everything works except the Rosary, which then says the file is missing; since LPS-DOS-115-CHG03 (French accents) an older `ROSARY.DAT` is refused as damaged. Without `MEALS.DAT` the Meal Planner runs in text mode on EGA; without `MEALSCGA.DAT` the CGA planner shows empty card frames and says the file is missing. On a mono screen (MDA/Hercules) the planner is in text mode and needs neither file. The real-hardware checklist is [`lps-companion/BOOK8088-TEST.md`](lps-companion/BOOK8088-TEST.md).

Since LPS-DOS-115-CHG04 the data folder (`C:\LPS\`) holds the same JSON saves as the PicoCalc, `SAVE_A.JSN` and `SAVE_B.JSN`, so you can copy your LPS data between the PicoCalc SD card (`LPS/`) and `C:\LPS\` in either direction. Old `SAVE_A.BIN`/`SAVE_B.BIN` files are read once, converted, and left untouched.

Sources and rebuild instructions are in [`lps-companion/`](lps-companion/). A local `make dos rosary meals mealscga hello` writes new copies there; those copies stay untracked. The root files above are the ones to download.

| Program | Directory | What it is |
|---|---|---|
| LPS Companion | [`lps-companion/`](lps-companion/) | MS-DOS port of [LPS Companion v1.15](https://github.com/TME520/LPS-Companion-PicoCalc) (Open Watcom, real-mode 8086, 80×25 text, EGA and CGA Meal Planner). Run [`LPSCOMP.EXE`](LPSCOMP.EXE) (with [`ROSARY.DAT`](ROSARY.DAT), [`MEALS.DAT`](MEALS.DAT) and [`MEALSCGA.DAT`](MEALSCGA.DAT) beside it) and [`HELLO.EXE`](HELLO.EXE) from this repository root. |
