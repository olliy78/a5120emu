# K1520 Emulator - Open Points

Updated: 2026-07-06
Branch: `formating-disks`
Status: A5120 boots CP/A fully to the interactive prompt; keyboard, clock, disk
read **and** write, and FORMAT.COM disk formatting all work — including self-made
bootable disks (format → CPABCGEN → boot) for 5¼″-MFM and 8″-FM/mixed-density.
Remaining work is a short tail of exotic disk formats plus a few known limits — not
architecture blockers.

> Earlier "open frontier" items — full CP/A cold boot, the ZVE1↔ZVE2 DMA handshake,
> keyboard (K7637), the clock timing, disk write, GUI validation, and basic FORMAT.COM
> formatting — are all **resolved**. History lives in git and the analysis docs
> (`doc/analyse_zre_rom_boot.md`, `doc/analyse_bootloader.md`,
> `doc/K1520_architecture.md` §8.5/§14, `docs/format.md`).

## Remaining open points

### 1) Disk formatting — exotic-format tail

The formatting pipeline (`tools/format_all.py` + `tools/format_driver`) covers the
native K5601 §3 formats and the §3.4 single-sided / 40-track geometries as both `.hfe`
and `.img`, plus the four foreign drive types via combo-boot disks. Full status:
`docs/format.md` §8. What is left:

- **(a) "Sektorfolge 1,4,7" interleave formats — Format 7 ("ZIK-NK") and W:6
  ("BAP2001")** report `Fehler 'S' SPUR DEFEKT` in Verify, on **both** `.hfe` and
  `.img`. Black-box diagnosis shows the emulator writes provably-correct sectors
  (IDs 1–16 sequential) and behaves identically on passing and failing tracks — the
  `'S'` is a FORMAT.COM-internal data-track verdict, not a differential emulator bug.
  Definitive root cause needs **disassembly of FORMAT.COM's `'S'`-verify path**.
  Scope: 2 of ~30 formats.
  `docs/format.md` §8.4. Repro: `python3 tools/format_all.py 7 --type img --upto 5`.
- **(b) Double-step 40-track geometries T/U as `.img`** are skipped: the card only
  knows step pulses, so `cur_cyl_` = 2×logical and a logical-40-track `.img` would
  need a physical→logical mapping. Workaround: use `.hfe` (faithful bit-track model)
  for double-step disks. `docs/format.md` §8.3.
- **(c) 1024-B FM/SD read path (`mf3200_fmt1`)** — an 8″-SD/FM disk formatted with
  1024-B data sectors formats + CPABCGEN + boots, but a running-OS `DIR` fails with
  `Bdos Err On A: Bad Sector` reading the 1024-B FM data track. 256-B FM
  (`mf3200_fmt7`) works and shares the read-stream build, so the difference is the
  sector size on the FM read path. Rare format; preset kept for analysis, not a test.
  `docs/format.md` §8.6.1.

### 2) Fresh gap-blank `.hfe` format hang (known limit, workaround active)

Formatting a **freshly `create`d, gap-empty `.hfe`** directly hangs (ZVE2 read
co-routine `0x1D0F/0x1D21` pre-reading an unformatted data track on the first seek
past cyl 1; index-interrupt / dual-CPU coordination race). The "keep index
mask-independent" and "motor/index stops" hypotheses were both tested and disproved
(`docs/format.md` §8.2/§8.2.1). **Workaround in the pipeline:** copy B: from a valid
template, or use `.img` via `create` (0xE5 reads as valid). A real fix needs
cycle-level dual-CPU tracing of the retry loop's break condition across the cyl1→cyl2
seek.

### 3) Post-boot VRAM wipe after ~50–65M idle cycles

After reaching the prompt, VRAM is wiped after tens of millions of idle cycles —
suspected leftover clock/timing drift and/or spurious residual ZVE2 floppy activity.
Low priority (cosmetic, well past the reached-prompt milestone).
`project_os_boot_reaches_prompt` memory has trace hints.

## Known non-issues (do not re-investigate)

- **Native 8″ drive** — the K5122 is format-agnostic and drive type is pure BIOS
  software, so 8″ formats (MF3200 SD/FM, MF6400 DD/mixed-density) are testable and
  bootable via the combo-boot disks and the `mf3200_8_ss77` / `mf6400_8_ss77` drive
  profiles (`docs/format.md` §8.5/§8.6, §11). No dedicated 8″ card is needed.

## Non-blocking / housekeeping

- **Test suite is fully green on this branch** — 583/583 ctest + 58 legacy-harness
  tests pass, plus 5 slow `format_integration` boot-disk tests (excluded from the
  default `tools/dev.sh test` run via `-LE format_integration`; run them with
  `tools/dev.sh test-format`).
- **Documentation coverage**: essentially done. All non-generated `core/` headers
  carry file/class-level comments. Remaining low-priority nicety: fuller Doxygen on
  some Python helpers.
