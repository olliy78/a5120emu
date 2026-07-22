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
> (Teil A = isolierter Read-Pfad-`/WE`=0-Bug, fixbar standalone; Teil B = das Verify-Problem
> mit dem entscheidenden nächsten Experiment + allem, was bereits ausgeschlossen ist).
>
> `INIT.COM` is SCPX's FORMAT.COM equivalent (dialog-driven formatter that programs the
> K5122 **directly**, no BIOS call). Full analysis: `doc/analyse_scpx_init_format.md`;
> memory `project_scpx_init_format`.

**Done / working:** the drive-speed gate is solved (commit `feaae01`, rotation-coupled
byte-spacing → `[12A6]≈6282` in window 6248–6373); INIT formats all 80 cylinders. Three
HW-faithful K5122 read-path/head-model corrections (commit `939dda5`) then got INIT's own
write-then-verify read to work for **(even cylinder, head 0)** tracks — the first ID mark
is now found on **all** tracks (previously `IN(16H)` returned the `0xFF` PIO-fallback and
the verify failed immediately). Those three fixes: (1) `/FR` head-select = **held bit2
level on every Port-A write** (not a `/STR`-edge latch); (2) a path/read control word
(`0x81/83/85/87`) **arms the streaming read directly** (INIT verifies without a `/STR`
read strobe); (3) `startReadTransfer` positions `head_pos_` at the **first sync mark**
(HW sync-detector model), so INIT's tight "skip-A1-then-expect-FE" resync doesn't choke on
leading `0x00` sync bytes.

**Still failing:** `INIT` reports `BAD TRACKS = {cyl 0} ∪ {all odd cylinders}` (even cyls
2–78 good). Per-`(cyl,head)` retry counts show **only (even cyl, head 0) verifies pass**;
head 1 (all cyls) and head 0 on odd cyls fail (5 retries → bad). Ground truth gathered:
the verify read reads the **correct (cyl,head)** in most failing cases (head-select is
fixed) yet still fails, so the remaining cause is **inside INIT's verify pipeline past the
first ID mark** — the ID→DATA resync (`0x1170/0x1173`: after an MK1 resync, expects DAM
`0xFB` in `L`), the data checksum (`0x1186 SBC HL,DE`), or the sector-loop / format-verify
ordering (INIT's per-track engine is a self-modifying coroutine dispatcher via `[12A4]`,
states `0x0E57`/`0x0F14`/`0x0FD1`/`0x0F2D`). The even/odd-cylinder + head-1 parity is the
key clue and is **robust across all four head/read-model variants tried** — it points at a
seek/head **pipeline** interaction (INIT may format/verify heads or cylinders in an order
that our step/head timing mis-aligns for odd cylinders and head 1), not at the sector data
(IDs 1..16 + `0xE5` fill read back byte-identical with 0 CRC errors).

**RE session findings (2026-07-21, deeper dive — the read path is NOT the culprit):**
Disassembled INIT's per-sector verify inner loop and traced the K5122 stream byte-by-byte.
- INIT's inner loop (`0x1150`–`0x1195`): `INIR` reads the 5-byte ID (`cyl,head,sec,size,+`)
  into `0x13E4`; MK1-strobe resync ID→DATA; `0x1170/0x1172 CP L`(`L=0xFB`) = **DAM check**;
  then read data **while `==0xFA`… no, while `== H` (`H=0xE5`)** (`L1175` loop), capture the
  first two non-`0xE5` bytes (the data-CRC) into `HL`, `0x1186 SBC HL,DE` vs the expected
  data-CRC `DE` (=`0x7827` for a passing sector), `JR NZ 0x119C` = **bad**. Success falls
  through to `0x11A8` (`[12A4]:=0x0F14`).
- **For an even-cyl-head-1 verify, the K5122 serves ALL 16 sectors byte-perfectly**: every
  IDAM `FE` and DATA `FB` at the correct stream offsets (16/66, 349/399, 682/732, … 5011/
  5061), the ID reads back `cyl=0,head=1,sec=1,size=1` (head byte correct!), data = `0xE5`.
  The head-select fix works; the resync landings (`romReadResyncTarget` → first `A1`, mark
  at +4) are all correct. **So the failure is NOT wrong bytes from the card.**
- Yet INIT still rejects and retries. Two failure sites seen: some tracks reach `0x1186`
  with `HL≠DE` (data-CRC mismatch); others (e.g. `C=2/C=3 H1`) fail earlier at `0x1173`
  (DAM≠`0xFB`) — i.e. the failure point itself varies per attempt.
**RE session 2 (2026-07-22): byte-steal FALSIFIED — TRUE root cause found & verified.**
Instrumented `IN(16H)` (port 0x16) with the issuing CPU (`bus_master_zve2_`): **all verify
reads are ZVE2, ZERO ZVE1 reads** → no byte-steal. The real mechanism, traced end-to-end:

- **INIT's head-1 sector-verify read loop issues control words with `/WE`=0** (`0xB0`/`0xB2`,
  = the density-table entries `0xB4`/`0xB6` with the head-bit2 cleared; head-0 uses
  `0x85`/`0xB5` with `/WE`=1). Our K5122's **synthetic full-track FORMAT-write path** (the
  "alter /STR-Schreibpfad", `handleCtrlPortAWrite` ~line 616) treats **any /STR-falling-edge
  with bit0(`/WE`)=0 as a full-track format** → `write_mode_=true, transferring_=false`.
- So INIT's head-1 verify `IN(16H)` then hits the **`0xFF` PIO fallback** (`ioRead`, port 0x16
  requires `transferring_ && !write_mode_`) → `head_pos_` never advances → the MK1 ID→DATA
  resync (`resyncToNextMark`→`romReadResyncTarget`) stays on the **IDAM** (`from=12→t=12`) →
  INIT reads `FE` where it expects the DAM `FB` (`0x1172 CP L`) → `JR NZ 0x1197` → bad → 5
  retries → track "bad". Head-0 works because its `/WE`=1 read-strobes go through the normal
  read arm. The head-1 read is **never armed as a read** (no `0x81/0x85` path byte, no bit0=1
  strobe). Confirmed: `WRITE-STR data=0xB0` fires for head-1 verifies, **zero** for head-0.
- Note on real HW: `/WE`=0 (manual A0) enables the write clock; INIT's verify loop pulses
  `/WE`=0 and re-writes the just-read bytes (`IN(16H); OUT(14H),A`) = a **read-verify-by-
  rewrite** that leaves data unchanged. Our synthetic format path can't model that and instead
  latches full write mode, destroying the read.

**Fix attempts that did NOT work (learnings for next time):**
1. Gate the format path on `!transferring_` → fails: `transferring_`=0 at the `/STR`-write
   edge (the head-1 read is never armed).
2. Defer `write_mode_` until the first `OUT(14H)` format byte → fails: INIT writes ONE setup
   byte to `OUT(14H)` *before* its `IN(16H)` (`0x113D OUT(14H),A; 0x113F IN A,(16H)`), which
   engages `write_mode_` prematurely.
3. Defer + speculatively arm a read at the `/STR`-write edge + "2nd consecutive `OUT(14H)`
   without an `IN(16H)` = real format" discriminator → fails: the speculative read is **torn
   down by the `/STR`=1 read-end detection** (`update()` ~line 390: `str_inactive_cycles_ >=
   strEndSampleCycles()` → `transferring_=false`) because INIT raises `/STR` again (`0xB0→0xB9`)
   between the arm and the read.

**RE session 3 (2026-07-22): the read path is NOT the (sole) blocker — 6+ fixes fail identically.**
Tried, all with the **exact same** `BAD TRACKS` result and **identical** per-`(cyl,head)` retry
counts (only `(even cyl, head 0)`=1 pass; everything else=5=fail): (1) `!transferring_` gate;
(2) deferred `write_mode_` on 1st `OUT(14H)`; (3) deferred + speculative read-arm + "2nd `OUT` =
format"; (4) `ioRead` switch-to-read when `write_mode_`; (5) a `str_write_pending_` flag surviving
the write-idle clear, switched by `IN(16H)`; (6) **head-select from bit2 only on the path byte +
`/STR`-write edge** (not every write — because INIT's head-1 verify alternates `0x81`(path,head1) /
`0xB5`(resync strobe, bit2=1) and the committed bit2-level model flips the head to 0 on every
`0xB5`). Key result: with fix (6) the head-1 verify **read now streams correctly** — `>>> READ …
16 Sekt, 0 CRC-Fehler`, 18 streaming reads vs 11 fallbacks — **yet INIT still rejects the track**.
`buildTrack` preserves physical sector order (no interleave loss). So: **the read decode is not the
blocker; the bytes INIT reads are correct.**

**Redirected conclusion:** the failure has **two robust, read-byte-independent factors** —
head-1-always-fails AND head-0-fails-on-odd-cylinders (only even-cyl-head-0 passes) — that survive
every read-path/head-select change. This points at INIT's **verify comparison logic** (what it
checks beyond the sector bytes) and/or a **dual-CPU pacing-phase / seek** factor, NOT the read
decode. (The `/WE`=0 `0xFF`-fallback traced in RE session 2 is a real bug but fixing it does not
change the outcome.) The even/odd-cylinder + head parity smells like a per-track **rotational /
pacing phase** or a **seek** interaction that only aligns for even-cyl-head-0.

**Next step (next session):** cycle-accurate trace of INIT's per-track *decision*, side by side for
an even-cyl-head-0 (pass) vs even-cyl-head-1 (fail) verify — same cylinder, so it isolates the head
factor from the seek factor. Break ZVE2 at the bad-branch (`b2 0x1197`/`0x119C`) and the success
(`0x11A8`), and dump *everything INIT compares*: the ID scratch buffer `0x13E4` (cyl/head/sec/size),
the data-CRC `HL` vs expected `DE` at `0x1186`, the physical cylinder (`dev`), and the `/BUSRQ`
phase. Determine what differs between the passing and failing verify when the read bytes are
identical. Only after that is understood should the `/WE`=0 read-arm fix (design above) be
revisited. Live recipe: `K1520DBG_LOGLEVEL=info ./build/k1520dbg disks/scpx_boot.hfe -x <script>`
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
