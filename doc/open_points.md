# K1520 Emulator - Open Points

Updated: 2026-07-22
Branch: `formating-disks` (baseline) / `scpx_boot` (SCPX 1526, items §4/§5 + disabled-tests)
Status: A5120 boots CP/A fully to the interactive prompt; keyboard, clock, disk
read **and** write, and FORMAT.COM disk formatting all work — including self-made
bootable disks (format → CPABCGEN → boot) for 5¼″-MFM and 8″-FM/mixed-density.
On `scpx_boot`, SCPX 1526 also boots to `A>` with keyboard, `DIR`, `.COM` loading and
runtime writes working; the former `PIP`/`REN` same-name rename hang is now resolved
(§4) — only a regression guard test is still to be added.
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

### 4) SCPX runtime `PIP`/`REN` rename hang (branch `scpx_boot`) — ✅ RESOLVED 2026-07-12, needs a guard test

> Branch `scpx_boot` (SCPX 1526 V1.7). SCPX boots to `A>`; keyboard, `DIR`, `.COM`
> loading and runtime disk writes all work. Full analysis: `doc/analyse_scpx_pip_rename.md`.

**Was hanging (never returned to `A>`):**
- `PIP B:=A:STAT.COM` — cross-drive copy with **identical source AND destination name**.
- `REN B:STAT.COM=B:STAT2.COM` — rename onto a name with a **deleted** directory entry.

**Now fixed** — the fix was **not** a dedicated change but a **side effect of the K5122
rotation-coupled, encoding-dependent byte-timing rework** (commit `1d547d0` **plus the ongoing
working-tree refinements** to `k5122.cpp/.h` — `consumeByteSlot()`/`currentBytePeriod()`, the
byte-slot spacing — that were uncommitted when this was verified; verify against the committed
state once those land). This matches the diagnosed root cause exactly: the hang lived in
the K5122 read-stream **byte pacing / `resyncToNextMark`** for the `E671` read that ZVE1 drives
unpaced (see `doc/analyse_scpx_pip_rename.md` §4d/§4e — `[EC0D]=0xE295` was the *constant*
data-CRC seed, never stale; the real issue was `head_pos_` pinning under the old flat
`kBytePeriodCycles=150` timing). The new rotation-coupled timing unpins it.

**Verified 2026-07-12** (k1520dbg, fresh boot + keystrokes): both `PIP B:=A:STAT.COM` and
`REN B:STAT.COM=B:STAT2.COM` complete, `DIR B:` shows `STAT COM`, and the `A>` prompt returns.

**Remaining action (small):** there is **no automated regression guard** for this path. Add one
to `ScpxIntegration` (`tests/cpp/test_boot_integration.cpp`): boot → `ERA B:STAT.COM` →
`PIP B:=A:STAT.COM` → assert return-to-`A>` + `STAT COM` present on B: (and/or the `REN` variant).
This locks in the fix so a future timing change can't silently re-break it. Repro script in
`doc/analyse_scpx_pip_rename.md` §3.

### 5) SCPX `INIT.COM` disk formatting — verify fails on half the tracks (branch `scpx_boot`)

> **→ Selbstständiges Handoff für neue Sessions: `doc/analyse_scpx_init_verify_handoff.md`**
> (das Verify-Problem mit dem entscheidenden nächsten Experiment + allem, was bereits
> ausgeschlossen ist).
>
> `INIT.COM` is SCPX's FORMAT.COM equivalent (dialog-driven formatter that programs the
> K5122 **directly**, no BIOS call). Full analysis: `doc/analyse_scpx_init_format.md`;
> memory `project_scpx_init_format`.

**Done / working:** the drive-speed gate is solved (commit `feaae01`, rotation-coupled
byte-spacing → `[12A6]≈6282` in window 6248–6373); INIT formats all 80 cylinders. The K5122
read-path/head-select model is HW-faithful: head/side (`bit2`/`/FR`) is latched **only** at the
path/read control word (`(data&0xF9)==0x81`) and the `/STR`-format-write edge (`K5122::setHead`),
so INIT's head-1 verify read **streams correctly** (`>>> READ … 16 Sekt, 0 CRC`) instead of the
`0xFF` PIO-fallback. Guard tests: `K5122Test.HeadLatch_*`, `K2526ZVE2FloppyChain.ZVE2ReadsHead1FieldViaBus`.

**Still failing:** `INIT` reports `BAD TRACKS = {cyl 0} ∪ {all odd cylinders}` (even cyls
2–78 good). Per-`(cyl,head)` retry counts show **only (even cyl, head 0) verifies pass**;
head 1 (all cyls) and head 0 on odd cyls fail (5 retries → bad). **The read bytes are not the
blocker** — with the head-select model in place, the K5122 serves every failing `(cyl,head)`
byte-perfectly: all IDAM `FE` / DATA `FB` at the correct offsets, the ID reads back the correct
`cyl/head/sec/size`, data = `0xE5`, 0 CRC errors. Yet INIT still rejects the track. This has
**two robust, read-byte-independent factors** — head-1-always-fails AND head-0-fails-on-odd-
cylinders — that survive every read-path/head-select variant tried, so the cause is **inside
INIT's verify pipeline past the first ID mark**: its **compare logic** (DAM check `0x1172 CP L`
with `L=0xFB`; data-CRC `0x1186 SBC HL,DE` vs expected `DE=0x7827`; ID compare) and/or a
**dual-CPU pacing-phase / seek** factor. The even/odd + head parity smells like a per-track
rotational/pacing phase or a seek interaction (INIT double-step vs. our single-step?) that only
aligns for even-cyl-head-0. INIT's per-track engine is a self-modifying coroutine dispatcher via
`[12A4]` (states `0x0E57`/`0x0F14`/`0x0FD1`/`0x0F2D`); the sector-verify inner loop is `0x1150–0x1195`.

**Next step (next session):** cycle-accurate trace of INIT's per-track *decision*, side by side for
an even-cyl-head-0 (pass) vs even-cyl-head-1 (fail) verify — same cylinder, so it isolates the head
factor from the seek factor. Break ZVE2 at the bad-branch (`b2 0x1197`/`0x119C`) and the success
(`0x11A8`), and dump *everything INIT compares*: the ID scratch buffer `0x13E4` (cyl/head/sec/size),
the data-CRC `HL` vs expected `DE` at `0x1186`, the physical cylinder (`dev` → `cur_cyl_`), and the
`/BUSRQ` phase. Then repeat for even-cyl-head-0 (pass) vs odd-cyl-head-0 (fail) to isolate the seek
factor. Live recipe: `K1520DBG_LOGLEVEL=info ./build/k1520dbg disks/scpx_boot.hfe -x <script>`
(`gu 0xE079` → `keys INIT\r`/`g …`/`keys A\r`/…/`keys Y\r`). No regression guard exists yet.

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
- **Review the disabled/skipped tests** — go through the `DISABLED_`/skipped tests and
  decide re-enable vs. delete vs. keep-as-documentation:
  - `KeyboardIntegration.DISABLED_TypeCommandAtCcpEchoesAndProcesses`
    (`tests/cpp/test_boot_integration.cpp:484`) — CP/A "type a command at the CCP,
    expect echo + processing" check. Disabled because of a harness clock / timer-ISR
    timing peculiarity (the CCP drops the command while time-entry input works). The
    serial-latency mechanism itself is regression-guarded by the K7637 unit tests, so
    this is a *harness* gap, not a product bug. Re-enable once the harness clock issue
    is understood; note that on `scpx_boot` the interactive CCP input path
    (`ScpxIntegration`) *is* exercised, so check whether that already covers the intent.
  - **Stale comment to clean up**: `tests/cpp/test_boot_integration.cpp:282` still
    references "`DISABLED_Stage3_FullyLoadsAndJumpsToOs`", but that test is now
    **enabled and passing** (`BootIntegration.Stage3_FullyLoadsAndJumpsToOs`, line 316).
    Update the comment.
  - Sweep for any other deactivation forms while here (`GTEST_SKIP`, `#if 0`,
    commented-out `TEST(...)`), and confirm the `format_integration`-labelled slow
    tests are *excluded-by-label*, not broken (run `tools/dev.sh test-format`).
