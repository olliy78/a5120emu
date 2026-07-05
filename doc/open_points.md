# K1520 Emulator - Open Points

Updated: 2026-07-04
Branch: `formating-disks`
Status: A5120 boots CP/A fully to the interactive prompt; keyboard, clock, disk
read **and** write, and FORMAT.COM disk formatting all work. Remaining work is a
short tail of exotic disk formats plus a few known limits — not architecture blockers.

> The 2026-05-16 version of this file listed the boot/display path, GUI validation
> and keyboard extraction as the open frontier. **All of that is resolved** (see
> below). This rewrite reflects the current state.

## Resolved since 2026-05-16 (the old "remaining open points")

1. **Full CP/A cold boot.** The A5120 boots the complete chained bootloader (boot ROM
   → SYL loader → secondary loader → CP/A boot system → `@OS.COM`) and reaches the
   running OS and its interactive prompt (`CP/A, Version 25.09.89`; "Bitte Uhrzeit
   eingeben!"). The old blockers #1 (no screen output) and the whole ZVE1↔ZVE2 DMA
   handshake are done. Milestone detail: `doc/analyse_zre_rom_boot.md`,
   `doc/analyse_bootloader.md`, `doc/K1520_architecture.md` §8.5/§14.
2. **Keyboard (K7637 serial) works** — cold-start time entry, key echo, commands,
   Ctrl+C, cursor/function keys, all with realistic 9600-baud serial latency. Old
   open points #4 (firmware-level extraction) resolved by modelling the physical
   K7637 code set. Smoke tool: `build/kbd_test`.
3. **Clock runs in real time.** Two Z80CTC bugs fixed (IEI/IUS interrupt gating +
   per-T-state tick); the CP/A clock no longer runs ~1100× too fast.
4. **Disk write end-to-end** (`.img` and HFE), incl. cold-start from a freshly
   written HFE. `/WE`-edge-triggered write path + `FloppyDriveV2::flush()` fix.
5. **FORMAT.COM formatting works** — formats all 160 tracks with Verify, all four
   sector sizes (128/256/512/1024 B), `DIR` of a fresh disk → `No File`.
6. **GUI** builds and runs against the current `libk1520core.so` (old open point #2).

## Remaining open points

### 1) Disk formatting — exotic-format tail (active work on this branch)

The formatting pipeline (`tools/format_all.py` + `tools/format_driver`) covers the
native K5601 §3 formats and the §3.4 single-sided / 40-track geometries as both `.hfe`
and `.img`, plus the four foreign drive types via combo-boot disks. Full status:
`docs/format.md` §8. What is left:

- **(a) "Sektorfolge 1,4,7" interleave formats — Format 7 ("ZIK-NK") and W:6
  ("BAP2001")** report `Fehler 'S' SPUR DEFEKT` in Verify, on **both** `.hfe` and
  `.img`. Black-box diagnosis shows the emulator writes provably-correct sectors
  (IDs 1–16 sequential) and behaves identically on passing and failing tracks — the
  `'S'` is a FORMAT.COM-internal data-track verdict, not a differential emulator bug.
  Definitive root cause needs **disassembly of FORMAT.COM's `'S'`-verify path**
  (analogous to the FORMATB analysis in §8.1). Scope: 2 of ~30 formats.
  `docs/format.md` §8.4. Repro: `python3 tools/format_all.py 7 --type img --upto 5`.
- **(b) Double-step 40-track geometries T/U as `.img`** are skipped: the card only
  knows step pulses, so `cur_cyl_` = 2×logical and a logical-40-track `.img` would
  need a physical→logical mapping. Workaround: use `.hfe` (faithful bit-track model)
  for double-step disks. `docs/format.md` §8.3.
- **(c) Commit the CPABCGEN bootdisk deliverables** — currently untracked:
  `disks/empty_cpa780.hfe`, `disks/bootdisk_cpabcgen.hfe`, `tools/cpa_tools/`
  (`make_bootdisk.py` + CPABCGEN.COM/FORMAT.COM). Pipeline works (format → CPABCGEN
  → bootable disk that boots CP/A to `A>`); needs a commit.

### 2) Fresh gap-blank `.hfe` format hang (known limit, workaround active)

Formatting a **freshly `create`d, gap-empty `.hfe`** directly hangs (ZVE2 read
co-routine `0x1D0F/0x1D21` pre-reading an unformatted data track; index-interrupt
timing race with the BIOS motor watchdog). The "keep index mask-independent"
hypothesis was tested and disproved (`docs/format.md` §8.2). **Workaround in the
pipeline:** copy B: from a valid template, or use `.img` via `create` (0xE5 reads as
valid). A real fix needs cycle-level dual-CPU tracing.

### 3) FORMATB.COM Verify — OUT OF SCOPE (not an emulator bug)

FORMATB.COM (V02.04.87) formats correctly (image identical to FORMAT.COM) but its
Verify reports `'V'`: a genuine **software version incompatibility** with the CP/A
BIOS (V25.09.89) — the CDB flag convention was reorganised between the versions
(BIOS source: "Bit 0 Verify-nach-Schreiben auf Bit 6 verlegt"). Would fail on real
hardware with this BIOS too. **Do not investigate further — use FORMAT.COM.** Full
diagnosis: `docs/format.md` §8.1.

### 4) Native 8″ drive not modelled (8″ formats work via combo disks)

The emulator has no dedicated 8″ drive, but 8″ formats (MF3200 SD/FM, MF6400 DD/MFM)
are now testable via the combo-boot disks (`docs/format.md` §8.5, §11) because the
K5122 is format-agnostic and drive type is pure BIOS software. Remaining: a real 8″
`DriveProfile`/geometry if native 8″ boot media ever matters.

### 5) Post-boot VRAM wipe after ~50–65M idle cycles

After reaching the prompt, VRAM is wiped after tens of millions of idle cycles —
suspected leftover clock/timing drift and/or spurious residual ZVE2 floppy activity.
Low priority (cosmetic, well past the reached-prompt milestone).
`project_os_boot_reaches_prompt` memory has trace hints.

### 6) Real ZRE ROM 0x0000-layout rebuild (optional faithfulness, NOT a bug)

**The boot ROM works** — `zre.rom` boots CP/A fully. This point is only about physical
faithfulness: our `zre.rom` is framed with a 256-byte preamble + code from `0x0100`,
whereas the real A26 chip has code from `0x0000`. The current boot path was reverse-
engineered around the `+0x100` framing, so it is functionally correct but not a
byte-for-byte match of the real chip's address layout. Reframing the emulator to the
true `0x0000` layout is a standalone, optional task (blocker: drive-probe at `0x0040`,
`[0x03FC]==0x77`). Note: the committed `A5120_ZRE_rom.bin` is a corrupt/shifted dump —
do not use it as a boot ROM. Detail: memory `project_real_zre_rom_dump`,
`doc/analyse_zre_rom_boot.md`.

## Non-blocking / housekeeping

- **Pre-existing red tests** (independent of current work; confirm against baseline
  before treating as a regression): FormatParser CPA780 / K3526 / K7024, and 6 Z80CTC
  tests on `main`. See CLAUDE.md "Build & test".
- **Documentation coverage** (old open point #3): English API-level comments still
  incomplete across some headers; low priority.
