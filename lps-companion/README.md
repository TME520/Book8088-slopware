# LPS Companion for MS-DOS

**English** | [Francais](#lps-companion-pour-ms-dos)

A full port of [LPS Companion v1.11](https://github.com/TME520/LPS-Companion-PicoCalc) (PicoCalc) to real-mode 16-bit MS-DOS
for Intel **8088**-class PCs with **640 KB** conventional memory. Keyboard only
(no mouse). Text mode **80×25** (content laid out in 40 columns, same as the
Companion). Data lives under `C:\LPS\` by default and uses the **same**
`SAVE_A.BIN` / `SAVE_B.BIN`, `EXPORT\*.ics` and `WEIGHTS\*.csv` formats as the
PicoCalc firmware and LPS Console.

Ready-to-run files are at the **repository root** (the directory above this one): [`../LPSCOMP.EXE`](../LPSCOMP.EXE), [`../ROSARY.DAT`](../ROSARY.DAT) and [`../HELLO.EXE`](../HELLO.EXE). Copy **both `LPSCOMP.EXE` and `ROSARY.DAT` into the same directory** on the Book 8088; `ROSARY.DAT` holds the rosary prayer texts. Without it everything works except the Rosary, which then shows *ROSARY.DAT not found: copy it next to LPSCOMP.EXE*. `make dos`, `make rosary` and `make hello` rebuild into this directory only; those local copies are gitignored. After a source change, copy the new files up (`cp LPSCOMP.EXE ROSARY.DAT HELLO.EXE ..`) so the root copies stay current. CI rebuilds and fails if the root files differ.

The screen is the full **80×25** text mode (no 43/50-line modes). Home is two panes: the menu on the left, and a day summary (date, weight, reminders, kanban counts) on the right. All 16 activities sit on one page in two columns. Kanban shows TODO, DOING and DONE side by side. The rosary uses 80-character lines; the timer stays centered. Keys, English and French wording, palettes, and the SAVE/ICS/CSV bytes are unchanged. On the 8088 the video driver rewrites only the cells that changed.

As in PicoCalc v1.12, XP and keepsakes no longer appear on any screen (LPS-DOS-115-CHG01). The line under the title bar shows the long date and ISO week of the viewed day, for example **Sunday 8 November 2026 (week 45)** / **Dimanche 8 Novembre 2026 (semaine 45)**; it stays blank for an undated day. XP is still banked in `SAVE_A`/`SAVE_B` so old and new saves stay compatible, and the ICS description keeps an empty `XP:` line, byte for byte like the PicoCalc.

As in PicoCalc v1.12, the notepad is gone (LPS-DOS-115-CHG02) and the Home menu is renumbered **0 Date, 1 Activities, 2 Weight tracker, 3 Kanban, 4 Christian Rosary, 5 Daily review, 6 Config, 7 Reminders, 8 Timer, 9 Close the day**; an archived day offers **1 Activities, 2 Weight tracker**. A note already saved by an older version is kept in the save and still exported to the ICS (`\NNote: ...`) when its day is closed; it is no longer shown or editable.

![Home screen](captures/01-home-en.png)

![Home screen, French](captures/06-home-fr.png)

## Requirements

- MS-DOS 3.3+ / FreeDOS / MS-DOS 7.x (FAT12/16)
- Intel 8088 or compatible (EXE built with Open Watcom `-0`)
- ~75 KB free disk for `LPSCOMP.EXE` (~65 KB) and `ROSARY.DAT` (~10 KB); data folder grows with history
- Colour text (CGA/EGA/VGA) for palettes; mono still readable
- Open Watcom C/C++ (v2) to build; gcc to run the host tests

## Feature parity (Companion v1.11)

| Feature | Status |
|---|---|
| Date entry (2000–2099) | Yes |
| 16 activities, 2 pages | Yes |
| Notepad | Removed, as in v1.12 (legacy notes kept in SAVE and ICS) |
| Weight expected/measured | Yes |
| Kanban 24 tasks TODO/DOING/DONE | Yes |
| Christian Rosary (4 modes, FR/EN) | Yes (accents ASCII-folded for CP437) |
| Daily review | Yes |
| Config language EN/FR + 5 palettes | Yes (text attributes) |
| 3 reminders | Yes |
| Timer 5/15/30 (BIOS tick) | Yes |
| Close day (XP and keepsakes hidden, as in v1.12) | Yes |
| 31-day history ←/→ | Yes |
| Dual A/B SAVE + CRC + v1–v6 migrate | Yes (read **and** write) |
| ICS + weights CSV byte-compatible | Yes |

## Build

Install [Open Watcom V2](https://github.com/open-watcom/open-watcom-v2/releases) for your host.
The Linux x64 release file is a zip archive (it also has an ELF installer stub; unzipping is enough):

```sh
curl -fL -o /tmp/ow-c-linux-x64 \
  https://github.com/open-watcom/open-watcom-v2/releases/download/2026-11-01-Build/open-watcom-2_0-c-linux-x64
unzip -q /tmp/ow-c-linux-x64 -d "$HOME/watcom"
# The zip stores host tools without the executable bit.
chmod a+x "$HOME/watcom"/binl64/* "$HOME/watcom"/binl/* 2>/dev/null || true
export WATCOM="$HOME/watcom"
export PATH="$WATCOM/binl64:$PATH"   # binl/ on a 32-bit host
export INCLUDE="$WATCOM/h"
export LIB="$WATCOM/lib286"
```

On Windows, use the official installer and point `WATCOM` at that directory
(`binnt` / `binw` instead of `binl64`; the Makefile picks `binl64` or `binl`).

From this directory:

```sh
make dos      # LPSCOMP.EXE  (-0 -bt=dos -ml, 8086/8088, soft FP, 4 KB stack)
make rosary   # ROSARY.DAT   prayer texts, built by tools/mkrosary (host gcc)
make hello    # HELLO.EXE    BIOS smoke test
make test     # host gcc tests (no WATCOM required; builds ROSARY.DAT first)
make clean
```

`make` (no target) builds both EXEs and `ROSARY.DAT`, and runs the host tests. `make dos` and
`make hello` prepend `$WATCOM/binl64` (or `binl`) to `PATH` for that command:
`wcl` looks up `wcc` and `wlink` on `PATH`, not beside the `wcl` binary.
`make captures` regenerates text dumps under `captures/` with the host UI driver.

The same DOS link line, without Make:

```sh
wcl -0 -bt=dos -ml -zt256 -ox -fpc -k4096 -dLPS_DOS -i=src -fe=LPSCOMP.EXE \
  src/state.c src/ics.c src/save.c src/paths.c src/rosary.c \
  src/app.c src/plat_dos.c src/main.c
```

### Host tests (gcc, no DOS)

Run from this directory so the test finds `tests/fixtures/v12_payload.bin` and
`ROSARY.DAT`:

```sh
gcc -std=c89 -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -DLPS_HOST \
  -o tools/mkrosary tools/mkrosary.c src/rosary_text.c src/state.c
./tools/mkrosary ROSARY.DAT
gcc -std=c89 -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -DLPS_HOST \
  -o tests/test_storage \
  tests/test_storage.c src/state.c src/ics.c src/save.c src/paths.c \
  src/rosary.c src/rosary_text.c src/app.c src/plat_host.c
./tests/test_storage
```

### Memory layout (LPS-DOS-115-CHG03b)

The EXE uses Open Watcom's **large model** (`-ml`): far data pointers and one
code segment per module. The 64 KB DGROUP (near data + 4 KB stack) used to be
almost full (63,088 bytes); it is now about 14 KB, which leaves room for the
JSON save, more activities and the EGA Meal Planner.

- The prayer texts are not in the EXE. `tools/mkrosary` builds `ROSARY.DAT`
  from `src/rosary_text.c`, the single source of the texts, and LPSCOMP loads
  it into the far heap when the Rosary opens and frees it on leaving.
  `LPSCOMP` looks for it next to itself (the path in `argv[0]`), then in the
  current directory. A missing or damaged file shows a message on Home; it
  never crashes.
- The big work buffers (two `State` copies, the encoded save, both SAVE
  records, the ICS and CSV export buffers) are one far-heap block allocated
  at start-up (`lps_buffers_init`), as are the `App` and the screen shadow.
  The CSV buffer used to be an 8 KB local on the 4 KB stack.
- `ROSARY.DAT` layout (little-endian): `LPSROSRY` magic, u16 version 1,
  u16 string count (138: 69 texts × EN/FR), u32 payload size, u32 CRC-32 of the
  payload, then a u16 offset table and the NUL-terminated ASCII strings.

Check the segment sizes with `wcl ... -fm=LPSCOMP.MAP` and look at `DGROUP`.

## Book 8088 / real hardware notes

On the first Book 8088 test, `LPSCOMP.EXE` cleared the screen then hung with a
blinking cursor. Root cause: the DOS default **stack was only 2 KB**, while
`app_start` / `commit` put a **~6 KB `Blob` and ~4.6 KB `State` on the stack**
→ immediate stack smash after `int 10h` mode setup (blank screen, cursor, no UI).

Fixes kept in this tree:

- `Blob` / `State` scratch moved to **static BSS** (`g_blob`, `g_scratch`);
  since LPS-DOS-115-CHG03b they live in the far heap (see *Memory layout*)
- Linker stack raised off the 2 KB default. It was **8 KB** (`-k8192`) in that
  first fix; after the SAVE buffers below moved to BSS, the link uses **4 KB**
  (`-k4096`), which is what `make dos` passes
- Video: detect MDA vs CGA via BIOS equipment word + mode `0Fh`; use **B000**
  or **B800** accordingly; only set mode `03h`/`07h` if not already text
- Early BIOS teletype banner (`LPS COMPANION v1.11`) before dirs / UI
- Soft FP (`-fpc`) — no 8087 required
- **`HELLO.EXE`** (built with `make hello`): tiny BIOS-only smoke test (prints mode / equipment)

### Keyboard freeze on Date / Activities (fixed)

Second Book 8088 bug: first save (confirm date or toggle activity) hung the UI.
Cause: `save_write_ab` / `decode_state` put **~14 KB of locals on the stack**
(record + verify + `State`) while DGROUP stack was only a few KB → smash on
first commit. Fix: all large SAVE/decode buffers are **static BSS**, shared where
safe; keyboard is pure **int 16h** (no `conio`, no mouse int 33h); AZERTY
unshifted top-row maps to digits for Date/Weight.

### Deferred SAVE (Book 8088 HDD)

Edits (date, activities, weight, kanban, …) update **RAM only** and set a dirty
flag. Disk write of one `SAVE_A`/`SAVE_B` slot (~4.6 KB) happens on:
Esc back to Home (if dirty), confirming Date/Weight/Config, Close the day,
and Quit (`Q`). Status shows `Saving...` / `Saved`. Post-write verify-read was
removed (too slow on 8088 + tiny HDD).

### What to try on the Book

1. Copy **`HELLO.EXE`** and **`LPSCOMP.EXE`** from the repository root to the HDD or a floppy.
2. Run `HELLO` — you must see `LPS COMPANION - HELLO` and video info; any key quits.
3. Run `LPSCOMP` or `LPSCOMP C:\LPS` — you should briefly see the banner, then the home screen.
4. Quit with **Q** from Home.
5. If the banner appears but the UI does not, note whether the machine is mono or colour and open an issue.

![DOSBox 8086 home](captures/07-dosbox-8086-home.png)

Other screens (activities, kanban, rosary, timer) are in `captures/`.

## Install on a real machine / DOSBox

1. Copy the repository-root `LPSCOMP.EXE` **and `ROSARY.DAT`** into the same directory on a floppy or the hard disk (and `HELLO.EXE` for the smoke test).
2. Create `C:\LPS` (or pass the path as argv: `LPSCOMP D:\DATA`).
3. Optional: `SET LPSDIR=C:\LPS`
4. Run `LPSCOMP` or `LPSCOMP C:\LPS`.
5. Quit from Home with **Q**.

DOSBox-X or DOSBox Staging. `dosbox.conf` mounts the current directory as `C:` and sets an 8086 CPU with EGA, so the EXE has to sit in this directory. Copy the root binaries down (they are gitignored here) or use a local `make dos`, then start DOSBox:

```sh
cp ../LPSCOMP.EXE ../ROSARY.DAT ../HELLO.EXE .
dosbox-x -conf dosbox.conf
```

Equivalent by hand, mounting the repository root:

```
mount C /path/to/Book8088-slopware
C:
LPSCOMP.EXE C:\LPS
```

Use `machine=ega` and `cputype=8086` (or `8088`) to match the Book 8088.
DOSBox 0.74 cannot set `cputype=8086`; use DOSBox-X or DOSBox Staging.

## Keys (same ideas as Companion / LPS Console)

Global: **Up/Down** select, **Enter** confirm, **Esc** back. **Q** quits on Home.

Home digits, as in PicoCalc v1.12:

| Key | Today | Archived day |
|---|---|---|
| `0` | Date | — |
| `1` | Activities | Activities (read-only) |
| `2` | Weight tracker | Weight tracker (read-only) |
| `3` | Kanban | — |
| `4` | Christian Rosary | — |
| `5` | Daily review | — |
| `6` | Config | — |
| `7` | Reminders | — |
| `8` | Timer | — |
| `9` | Close the day | — |

**Left/Right** browse the last 31 closed days (read-only).

The PicoCalc README documents the full per-screen binding list; this port follows
the same map (F1–F5 on Kanban, N/Del on Kanban and Reminders, etc.).

## Data layout

```
C:\LPS\
  SAVE_A.BIN     dual-slot snapshots (magic LPR1 + generation + CRC)
  SAVE_B.BIN
  EXPORT\YYYY-MM-DD_LPS-Companion.ics
  WEIGHTS\YYYY.csv
```

Files are interchangeable with the PicoCalc SD card and with LPS Console
imports/exports. Do not change the on-disk layout: `SAVE_A`/`SAVE_B`, ICS, and
CSV stay byte-compatible with the firmware.

## Source layout

| File | Role |
|---|---|
| `src/state.c` | Dates, ISO week, long date, banked XP, encode/decode v1–v6 |
| `src/save.c` | A/B SAVE read/write |
| `src/ics.c` | ICS + weights CSV |
| `src/app.c` | Full UI state machine |
| `src/rosary.c` | Rosary step engine; loads `ROSARY.DAT` |
| `src/rosary_text.c` | Rosary texts (single source, not linked into the EXE) |
| `tools/mkrosary.c` | Writes `ROSARY.DAT` from `src/rosary_text.c` |
| `src/plat_dos.c` | BIOS video/keyboard/timer |
| `src/plat_host.c` | Linux host for tests |
| `src/hello.c` | `HELLO.EXE` BIOS smoke test |
| `tests/test_storage.c` | Host encode/decode, ICS, CSV, A/B, `ROSARY.DAT`, v1.2 fixture |
| `tests/fixtures/v12_payload.bin` | Synthetic Companion v1.2 payload |

## Captures

PNG screenshots under `captures/` (home EN/FR, a few feature screens, and a
DOSBox 8086 home grab). Text dumps are not stored; `make captures` rewrites
`captures/*.txt` from the host UI driver.

---

# LPS Companion pour MS-DOS

Port complet de [LPS Companion v1.11](https://github.com/TME520/LPS-Companion-PicoCalc)
vers MS-DOS 16 bits (8088, 640 Ko), clavier uniquement, mode texte 80×25.
Memes formats `SAVE_A/B.BIN`, ICS et CSV que le firmware PicoCalc et LPS Console.

Les fichiers prets a lancer sont a la **racine du depot** : [`../LPSCOMP.EXE`](../LPSCOMP.EXE), [`../ROSARY.DAT`](../ROSARY.DAT) et [`../HELLO.EXE`](../HELLO.EXE). Copiez **`LPSCOMP.EXE` et `ROSARY.DAT` dans le meme dossier** sur le Book 8088 ; `ROSARY.DAT` contient les textes du rosaire. Sans lui, tout fonctionne sauf le rosaire, qui affiche *ROSARY.DAT introuvable : copiez-le a cote de LPSCOMP.EXE*. `make dos` / `make rosary` / `make hello` reconstruisent seulement dans ce dossier (copies non versionnees). Apres une modification des sources, recopiez-les a la racine (`cp LPSCOMP.EXE ROSARY.DAT HELLO.EXE ..`). La CI reconstruit et echoue si les fichiers a la racine different.

L'ecran utilise tout le mode texte **80×25**. L'accueil a deux volets : le menu a gauche, le resume du jour a droite (date, poids, rappels, kanban). Les 16 activites tiennent sur une page en deux colonnes. Le kanban montre A FAIRE, EN COURS et FINI cote a cote. Le rosaire utilise des lignes de 80 caracteres ; le minuteur reste centre. Les touches, les textes, les palettes et les octets SAVE/ICS/CSV ne changent pas. Sur 8088, seules les cellules modifiees sont reecrites.

Comme sur la PicoCalc v1.12, l'XP et les souvenirs n'apparaissent plus sur aucun ecran (LPS-DOS-115-CHG01). La ligne sous la barre de titre affiche la date longue et la semaine ISO du jour affiche, par exemple **Dimanche 8 Novembre 2026 (semaine 45)** ; elle reste vide pour un jour sans date. L'XP reste enregistree dans `SAVE_A`/`SAVE_B` pour la compatibilite, et la description ICS garde une ligne `XP:` vide, comme sur la PicoCalc.

Comme sur la PicoCalc v1.12, le bloc notes disparait (LPS-DOS-115-CHG02) et le menu d'accueil est renumerote : **0 Date, 1 Activites, 2 Suivi du poids, 3 Kanban, 4 Rosaire chretien, 5 Revue journaliere, 6 Config, 7 Rappels, 8 Minuteur, 9 Terminer le jour**. Un jour archive propose **1 Activites, 2 Suivi du poids**. Une note deja enregistree par une ancienne version reste dans la sauvegarde et part toujours dans l'ICS (`\NNote: ...`) a la cloture du jour ; elle n'est plus affichee ni modifiable.

## Book 8088 / materiel reel

Premier essai Book 8088 : ecran vide + curseur. Cause : **pile DOS 2 Ko** trop
petite pour `Blob` (~6 Ko) / `State` (~4,6 Ko) alloues sur la pile au demarrage.
Correctifs : tampons en BSS, pile a **4 Ko** (`-k4096`; elle avait ete montee a
8 Ko avant le deplacement des gros tampons SAVE), video CGA/MDA, banniere BIOS,
`HELLO.EXE`. Deuxieme bug : gel clavier sur Date/Activites — tampons SAVE encore
trop gros sur la pile ; tout est en BSS, clavier en int 16h pur. Les editions
restent en RAM (drapeau dirty) et un seul slot A/B est ecrit en quittant un
ecran, en confirmant Date/Poids/Config, en cloturant le jour, ou avec **Q**.

## Compilation

Open Watcom V2, variable `WATCOM` pointant sur la racine du compilateur
(voir la section **Build** ci-dessus). Puis, dans ce dossier :

```sh
make dos      # LPSCOMP.EXE (-0 -bt=dos -ml)
make rosary   # ROSARY.DAT (textes du rosaire)
make hello    # HELLO.EXE
make test     # tests gcc, sans Watcom
```

## Utilisation

Copier `LPSCOMP.EXE`, `ROSARY.DAT` (dans le meme dossier) et `HELLO.EXE` depuis la racine du depot, creer `C:\LPS`, lancer `LPSCOMP` (ou `LPSCOMP chemin`).
**Q** quitte depuis l'accueil. Les touches reprennent celles du Companion.

DOSBox-X / DOSBox Staging (`cputype=8086`, `machine=ega`) : copier les EXE et `ROSARY.DAT` de la racine ici, puis `dosbox-x -conf dosbox.conf`.

## Empreinte

`LPSCOMP.EXE` fait environ **65 Ko** (modele large, 8086, pile 4 Ko) et `ROSARY.DAT` environ **10 Ko**. `HELLO.EXE` ~3 Ko.

Depuis LPS-DOS-115-CHG03b, le segment DGROUP de 64 Ko (donnees proches + pile) passe d'environ 63 Ko a environ 14 Ko : les textes du rosaire sont dans `ROSARY.DAT`, charge en memoire lointaine a l'ouverture du rosaire, et les gros tampons (copies de `State`, sauvegarde, exports ICS/CSV) sont alloues en memoire lointaine au demarrage. Un `ROSARY.DAT` absent ou abime affiche un message, sans plantage.

## Ecarts connus

- Accents francais ASCII-ises pour le code page 437 (le sens des chaines est
  conserve ; le Companion PicoCalc garde Latin-1/UTF-8).
- Pas d'avertissement batterie (pas de materiel equivalent).
- Config n'a pas d'entree « import SD » : le disque DOS **est** le stockage.
