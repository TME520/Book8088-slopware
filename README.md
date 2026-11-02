# Book8088-slopware

AI-hallucinated software for the mighty Book 8088 (I do what I want)

Lightweight programs for a Book 8088: Intel 8088, 640 KB RAM, HDD, EGA, MS-DOS 7.

Ready-to-run LPS Companion binaries are at the repository root. Copy them straight onto the Book 8088:

- [`LPSCOMP.EXE`](LPSCOMP.EXE) — the companion
- [`HELLO.EXE`](HELLO.EXE) — BIOS smoke test

Sources and rebuild instructions are in [`lps-companion/`](lps-companion/). A local `make dos` writes new EXEs there; those copies stay untracked. The root files above are the ones to download.

| Program | Directory | What it is |
|---|---|---|
| LPS Companion | [`lps-companion/`](lps-companion/) | MS-DOS port of [LPS Companion v1.11](https://github.com/TME520/LPS-Companion-PicoCalc) (Open Watcom, real-mode 8086). Run [`LPSCOMP.EXE`](LPSCOMP.EXE) and [`HELLO.EXE`](HELLO.EXE) from this repository root. |
