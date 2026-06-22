# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## CLI Tool Preferences
<tool_preferences>
ALWAYS check for and prioritize the following modern CLI tools over legacy ones:
- Use `rg` instead of `grep` for any code or file text searching (faster and respects .gitignore).
- Use `fd` (or `fdfind`) instead of `find` for finding files or directories.
- Use `sd` instead of `sed` for complex string replacements or in-place file modifications.
- Use `jq` for any JSON reading, querying, or manipulation tasks.
- Use `yq` for any YAML configuration file reading or manipulation.

CRITICAL: Do not use slow regex chains with grep/sed if rg/sd/jq/yq are available on the system.
</tool_preferences>


## Two emulators live in this repo — know which one you're touching

This is the single most important thing to understand before editing.

1. **Legacy emulator (`src/`)** — the original monolithic A5120 emulator. Emulates the Z80 plus a **CP/M BIOS HALT-trap** mechanism: BIOS jump-table targets are replaced with `HALT` opcodes, and the run loop dispatches C++ functions when the Z80 halts. Boots **only CPA/CP/M**. Builds the `a5120emu`, `cparun`, and `a5120emu_test` targets. `README.md` describes *this* emulator (its memory map, boot process, and disk format are CP/M-specific and do **not** apply to the new core).

2. **New modular K1520 core (`core/`)** — a hardware-accurate, transaction-level emulation of the K1520 bus and its plug-in cards. **No BIOS traps**: real Z80 code (boot ROM, BIOS, OS) runs natively, so any K1520 OS can boot. Builds `libk1520core.so` (a stable C-ABI) consumed by a **Python/PySide6 GUI** (`app/`). This is where active development happens. `doc/K1520_architecture.md` and `doc/design/*.md` are the authoritative design references.

The two share **no code** except the Z80 (legacy `src/z80.cpp`; the core has its own adapted `core/primitives/z80.cpp`). When a task mentions cards (K2526/K5122/...), the bus, ZVE1/ZVE2, the boot ROM, or the GUI, it is about the **new core**.

## Build & test

> **ALWAYS go through `tools/dev.sh` — never run a binary straight from `build*/`.**
> There are two build dirs with the SAME tool names (`build/` LOG_LEVEL=3,
> `build_trace/` LOG_LEVEL=5). Running a tool/test from a dir you forgot to rebuild
> tests **stale objects** and has repeatedly sent us down false trails (e.g. a
> "working" `dir` listing that was leftover from a reverted experiment). `dev.sh`
> rebuilds the right dir first (CMake's real dependency tracking — fast when nothing
> changed) and reports `aktuell` vs `NEU GEBAUT`, so you always test current source.
> There is no reliable read-only freshness check on CMake's Makefiles (`make -q` lies);
> "is it clean?" therefore means "run `cmake --build` and see if it had work to do".

```sh
tools/dev.sh test [ctest-args]   # build build/, then ctest + a5120emu_test (the default)
tools/dev.sh test -R K2526       #   one card's tests by name regex
tools/dev.sh build [trace]       # just build build/ (+ build_trace/ with 'trace')
tools/dev.sh trace <boot_trace-args>   # build build_trace/, then run boot_trace
tools/dev.sh tool <name> [args]  # build build/, then run build/<name> (floppy_diag, k1520dbg, kbd_test…)
tools/dev.sh check               # build both dirs + report freshness
tools/dev.sh rebuild             # rm -rf build build_trace, then build from scratch
```

After any experiment that touched build dirs (sanitizer builds, `-DLOG_LEVEL=…`,
interrupted builds), run `tools/dev.sh rebuild` to be certain. Raw commands still work
(`cmake -B build && cmake --build build -j`; `ctest --test-dir build`;
`./build/k1520_test_k2526 --gtest_filter='*ZVE2*'`) but only `dev.sh` guarantees no stale binary.

`LOG_LEVEL` is the compile-time **ceiling** (0=off … 5=trace) baked in via `add_compile_definitions`; call sites above it are removed (zero overhead), so build with `-DLOG_LEVEL=5` (the `build_trace/` dir) to make every site available. **The actual output level is now runtime-controlled and gated** (`core/logger.{h,cpp}`): a runtime *base level* (`Logger::setBaseLevel`, default = ceiling), plus dynamic *gates* that raise the effective level only inside a PC range (`addPCGate`) or cycle window (`addCycleGate`), plus a RAII scoped boost (`K1520_LOG_BOOST(Level::TRACE)` at a function top). The emit macros check `Logger::shouldLog()` before formatting, so disabled logs cost ~one atomic load. The run loop (`a5120.cpp`) calls `Logger::update(cycle, zve1pc, zve2pc)` per instruction (cheap early-out when no gates). This replaces "build at level 5 → multi-GB log → grep" with "run quietly, boost only the window of interest". GoogleTest is fetched on first configure (network required). Some core tests (FormatParser CPA780 / K3526 / K7024 / CTC) are known-failing on the working branch independent of current work — confirm against the baseline before treating a red test as a regression.

## Python GUI

The GUI loads `libk1520core.so` through `ctypes` (`app/core_binding/k1520.py` searches `build/` first). It needs the shared lib built and on the library path:

```sh
python3 -m venv venv && source venv/bin/activate && pip install -r requirements.txt
bash run_gui.sh      # sets LD_LIBRARY_PATH=build and runs app/main.py
```

## K1520 core architecture (the part that needs multiple files to grasp)

Strict layering — each layer only knows the one below (see `doc/K1520_architecture.md` §5):

```
machines/a5120  →  wires cards onto the bus, drives the run loop, exposes the machine API
cards/          →  K2526 (ZRE/CPU), K3526 (RAM), K7024 (screen), K8025 (serial), K5122 / K5122v2 (floppy)
primitives/     →  Z80, Z80PIO, Z80CTC, Z80SIO, EPROM/RAM devices  (generic chips)
bus/            →  K1520Bus (memory/IO dispatch, INT daisy-chain, BUSRQ, NMI, MEMDI) + Koppelbus (signal router)
```

> **Floppy controller — single formatagnostic K5122 (2026-06-10).** Slot 2 (`core/cards/k5122/`)
> is a formatagnostic *read-head-over-rotating-track* controller on the `core/peripherals/floppy_drive/`
> stack (TrackImage / TrackCodec / BitCodec / DiskImage + RawSectorImage + HfeImage / FloppyDriveV2 /
> DriveProfile).  It **boots CP/A fully** (all `test_boot_integration` stages green, incl. boot from
> drives B:/C:) and reads/writes **HFE v1** in addition to raw `.img`.  *(History: this replaced an
> older monolithic on-the-fly-synthesis K5122, which was removed once this one booted; the change was
> developed as a parallel card "K5122v2" and then renamed to K5122.)*  The boot ROM/loader read the
> disk in a Robotron-specific layout (single A1, no IAM, mark on the A1, per-sector-size CRC); the
> controller reproduces it on-the-fly via `TrackCodec::buildRobotronTrack` (the generic IBM/HFE
> `buildTrack` layout is untouched).  Full model & boot fixes: `doc/refactoring_floppy_emulator.md`
> §15, `doc/K1520_architecture.md` §8.5.  The boot-DMA narrative below still describes the data path
> at the byte level (unchanged); the *card-internal* synthesis it mentions is now the TrackImage stack.

- **Registration model**: cards register memory ranges and I/O port ranges on `K1520Bus`; the CPU's read/write/port callbacks route through the bus, which dispatches to the owning device. Interrupt priority is a daisy chain set via `bus.setInterruptChain(...)`; the Koppelbus models the A5120 backplane's hand-wired signal links (CTC clock cascades, second IEI/IEO chain).
- **Dual Z80 on the K2526 (`core/cards/k2526/`)** — non-obvious and central to the boot path:
  - **ZVE1** is the main CPU. Its memory accesses pass through the **Q240 protection logic** (`MemIOProtect`); a violation raises NMI.
  - **ZVE2** is a second Z80 acting as the **DMA processor** for loading boot sectors. It shares the same bus (no Q240 filtering). The run loop in `a5120.cpp` steps **ZVE2 only while `/BUSRQ` is asserted**, otherwise it steps ZVE1; both CPUs coordinate purely through shared RAM variables. ZVE2 is held in reset (port `04H`) and stalled by `/WAIT-ZVE2` (BS-PIO B3) until ZVE1 releases it.
  - The **boot ROM** is mapped at `0x0000–0x03FF` at power-on and unmapped by writing BS-PIO Port B bit0 (`/LD-ROM`); after that the low addresses are plain RAM shared by both CPUs.
- **C-API boundary** (`core/api/k1520_api.{h,cpp}`): the only surface the Python side sees; keep it `extern "C"` and ABI-stable. `A5120Machine` (`core/machines/a5120/a5120.{h,cpp}`) is the integration point exposing `run()`, disk mounting, framebuffer, keyboard, and debug accessors.
- **EPROM/charset data** are committed as generated C arrays (`*_data.h`, `charset_*.h`) produced from binaries by `tools/eprom_to_h.py`; they are not loaded at runtime.

## Boot-ROM debugging workflow

The ZRE boot ROM and the ZVE1↔ZVE2 DMA handshake are the current hard problem; `doc/analyse_zre_rom_boot.md` is the running analysis.

> **Start here: `tools/how_to_debug_and_trace.md`** is the task-oriented guide for the two
> debug/trace tools (which one when, with worked scenarios). Full references:
> `tools/k1520dbg.md` and `tools/boot_trace.md`. Two tools, complementary:
> - **`boot_trace`** — non-interactive: run a boot to a cycle limit / `--until <cond>`,
>   get a report (milestones, `[03F8]` done-flag, PC histograms, VRAM banner). **Locates**
>   *where* the DMA/boot hangs; also `--coverage`/`--diff`/`--csv` exports.
> - **`k1520dbg`** — interactive gdb-style: breakpoints (incl. conditional / `bint`/`bnmi`/
>   `breti` event BPs), step into/over/out, `rs` reverse-step + `snap`/`savestate`, watch
>   mem/io, `logpoint`, `x` examine, exact history `bt`, `dev ctc/pio/sio` chip state.
>   **Dissects** a located problem.
>
> **Run efficiently (this matters for the agent):**
> - **Invoke via `tools/dev.sh`** (rebuilds first → never a stale binary, see Build & test):
>   `tools/dev.sh trace <boot_trace-args>` and `tools/dev.sh tool k1520dbg <args>`. The bare
>   tool names in the examples below stand for these wrappers.
> - ⚠️ **Both tools mount the disk read/write — a run can CORRUPT the image.** Always run
>   against a temp copy, never a committed fixture: `D=$(mktemp --suffix=.img); cp DISK $D; … $D; rm -f $D`.
> - boot_trace: `-L /dev/null` discards the verbose emulator log; **`--quiet --json`** gives
>   exactly one machine-readable result line (instead of ~880) + a meaningful exit code
>   (`--until`: 0 met / 2 not met). Prefer **`--until <cond>`** over guessing cycle counts.
> - k1520dbg: drive it in one shot via a pipe (`printf 'b 0x0437\ng\nrj\nq\n' | k1520dbg $D`)
>   or `-x script.dbg`; `rj` prints registers as JSON. The interactive REPL/readline is for
>   humans — the agent uses batch mode.
> - **Boot once, resume often:** `--save-state`/`--load-state` (boot_trace) and
>   `savestate`/`loadstate` (k1520dbg) persist RAM+CPU+ROM-mapping to a file, so the ~2 s
>   boot is a one-time cost. Load `-l <bios.prn>` to see commented source instead of raw disasm.

Supporting tools (`tools/`):

- `tools/z80_disasm2.py` — the canonical generic Z80 disassembler (configurable `--org`, repeatable `--entry`/`--label`). The other two disassemblers are format.com-specific.
- **`k1520dbg`** (`tools/k1520dbg.md`) — the interactive debugger; expression-conditioned breakpoints, reverse-step, save-state, and `.prn`/symbol annotation make hand-disassembling RAM dumps mostly unnecessary. Delegate heavy log/trace reads to the `log-trace-analyzer` subagent.
- **`.prn`-Listing-Annotation (`-l`, both `k1520dbg` and `boot_trace`)** — instead of hand-disassembling RAM dumps, load the commented MACRO-80 source listing of the running code (e.g. `-l ~/projects/CPA_Workbench/build/bios.prn`) and every disassembly/trace line + PC-histogram entry whose address is in the listing gets the **original label+mnemonic+comment** appended. Repeatable (multiple listings cover different ranges); `@OFFSET` (signed, `0x..`/`..h`/dec) relocates a listing's addresses to the runtime load address. Only absolute addresses — a BIOS listing covers ~`0xD200+` (and BIOS pieces mapped low, e.g. the CONIN keyboard poll at `0x041C–0x042B`). Parser: header-only `tools/prn_listing.h` (tests `tests/cpp/test_prn_listing.cpp`, gtest suite `PrnListing`). See `tools/k1520dbg.md` §6 / `tools/boot_trace.md` §4.
- `tools/disasm_difftest.py` — cross-checks the disassembler against the `z80dis` pip package (in `venv`); run it before changing the disassembler engine.
- `tools/boot_trace.cpp` (`boot_trace` target) — traces **both** ZVE1 and ZVE2 per instruction and reports where the DMA freezes. Use `-L <file>` to divert the emulator log so the summary stays readable. A separate `build_trace/` build dir is conventionally configured with `-DLOG_LEVEL=5` (the compile ceiling). **Default base level is now ERROR — the run is quiet & fast.** Raise it with `--log-level <off|error|warn|info|debug|trace>`, or far better, boost only where it matters: `--log-pc LO:HI[:level]` (effective level while either CPU PC is in the range) and `--log-cycle FROM:TO[:level]` (while the cycle counter is in the window). **Gotcha:** a `--log-pc` gate on a *spin-loop* address fires for as long as the CPU parks there (can be tens of millions of cycles → multi-GB log) — pair it with a tight `--log-cycle`, or just use a cycle window. Reference: `boot_trace --log-level info …` (≈11 KB / 8 s for a full @OS.COM run) gives the K5122 `>>> READ` summaries; add a `--log-cycle` window for full TRACE only there.

### Boot DMA model (fixed — `disks/cpadisk.img` boots to loaded code)

`boot_trace -L /tmp/emu.log disks/cpadisk.img` reports **Boot reached: YES** — ZVE2
copies all boot sectors, signals completion, and ZVE1 jumps into the loaded code at
`0x0437`. Three coupled fixes made this work; keep all three intact:

1. **Continuous rotating-track stream** (`K5122::doReadSector`). On a read `/STR`,
   the whole `(cyl,head)` track is built as a byte stream of **138-byte** sector
   blocks: `[sync 00][IDAM FE][cyl][head][sec][size][IDAM-CRC×2][128 data][data-CRC×2]`.
   The 138 count is exact — it is how many port-0x16 reads the boot ROM does per
   sector (6 header + 2 IDAM-CRC at 0x022F/0x0239 + 128 data + 2 data-CRC at the
   trailing INIs 0x0245/0x0247). A short block drifts the stream and the IDAM
   search fails on sector 2+. `ioRead(0x16)` streams this buffer and **wraps** at
   the end (the disk keeps spinning); it no longer releases BUSRQ on drain.
2. **BUSRQ released on completion, not on buffer-drain.** ZVE2 finishes by writing
   the shared flag `[0x03F8]=3` (ROM `0x026B`). `A5120Machine::run()` watches that
   RAM byte and calls `K5122::endDmaTransfer()` to release the bus.
3. **Concurrent ZVE1/ZVE2 stepping during DMA.** While `/BUSRQ` is held and ZVE2 is
   active, the run loop steps ZVE2 **and falls through to also step ZVE1** (the two
   CPUs run in parallel on real hardware). This ordering is essential: ZVE1 must
   finish `CALL 0194` — whose tail writes `[0x03F8]=0` at `0x01B3` — and reach its
   poll loop at `0x0168` *before* ZVE2 writes `=3`, otherwise ZVE1's late `=0`
   clobbers ZVE2's `=3` and the boot hangs.

ZVE2 reads sectors until `sector_id == [0x07F2]` (=4 → sectors 1–4); its inner loop
returns to `0x025A` (not `0x01E9`), emitting only one `/STR` edge per session, which
is why the stream must contain the *whole* track up front.

**Handshake RAM variables** (low memory is plain RAM here — ROM unmapped — and
`RAM[0x0000..2] = C3 DD 01 = JP 0x01DD` is ZVE2's entry): `[0x03F8]` done-flag
(0=running, 1=ISR timeout, 3=done), `[0x03F7]` index counter, `[0x03FD]` path byte
(`0x87`=correct), `[0x07F2]` target sector count, `[0x03F0]` load address (`0x0400`).
Boot sector 0 starts with the signature bytes `53 59 4C` ("SYL") that ZVE1 checks at
`0x01B6`. Key addresses: ZVE1 wait `0x0168`, drive-init/ZVE2-start `0x0194`, ZVE2
entry `0x01DD`, index-pulse ISR `0x01C7`, ZVE2 completion `0x0267`, loaded code `0x0437`.

Earlier fixes also in place: index-pulse period (`kIndexPeriodCycles=490000`), ZVE2
reset/wait clearing on port-04 release, 1-based IDAM `sector_id`, `/STR` read-refresh.
Full narrative and annotated disassembly: `doc/analyse_zre_rom_boot.md`; canonical
`z80_disasm2.py` invocation for `zre.rom`: `tools/README.md`.

### Multi-round chained bootloader (current frontier — reaches screen banner)

The loaded code at `0x0437` is **stage-1 of a chained loader**: it re-uses the boot
ROM's DMA machinery for further rounds instead of containing its own. It patches the
operand of the ROM's `CALL [0x0175]` (at ROM `0x0174`) to point at the next stage,
then re-enters the ROM DMA path via `JP 0x0165`. Each round reloads `[0x03F3]`=cyl /
`[0x03F5]`=sector / `[0x03F0]`=load-addr and runs another ZVE2 transfer. Round 2+ reads
**cyl 1, head 1** and several cylinders into `0x0600+`. With the three fixes below,
`boot_trace -p 9000000 disks/cpadisk01.img` now completes **2+ DMA rounds**, does
**~2079 VRAM writes**, and the screen shows **`Bootloader, Version 24.02.87`**.

Three round-2+ fixes (all required, beyond the round-1 three above):

1. **OUT(04H) bit0=1 always restarts ZVE2 from PC=0** (`K2526` port-04 handler). The
   ROM's `CALL 0194` issues `OUT(04H)` with bit0=1 before *every* DMA round; ZVE2 must
   `reset()` to PC=0 each round to reload its IDAM registers for the new cyl/sector —
   not only on the first reset→run edge.
2. **Step direction = bit5 of ctrl-port-A** (`K5122::handleCtrlPortAWrite`):
   `step_dir_in_ = (data & 0x20) != 0` (bit5=1 → inward/toward higher cyl, bit5=0 →
   outward/toward track 0). ROM track-0 seek writes `0x09` (bit5=0, outward); loaded
   code writes `0x29` (bit5=1, inward to cyl 1). Getting this backwards hangs round 2.
3. **Transition-based DMA-completion detection** (`A5120Machine::run`). `[0x03F8]`
   still holds `3` from the prior round when the next begins, so the run loop arms
   `dma_saw_progress_` only after seeing `[0x03F8]!=3` (ZVE1 cleared it) and *then*
   treats a `→3` write as completion. Level-based detection fires on the stale 3.

**Post-banner spin at `0x052A–0x0538` — DIAGNOSED & largely fixed (2026-06-07).** The
loop is the loader's interrupt/handshake wait for the next sector. It was NOT an
interrupt-system bug (IM2/daisy-chain/RETI/index-int all verified working; vec 0x62 IS
delivered). Root cause was a **K5122 sector-format/access mismatch**: the loaded
bootloader's own ZVE2 routine (`0x062E`, installed via `[0x0000]=JP 0x062E`) reads sectors
with TWO `/STR` strobes (1st → IDAM `0xFE`, 2nd mid-sector → data mark `0xFB`), whereas the
boot ROM uses ONE strobe and reads IDAM+data continuously. Two fixes landed (all 40
K5122/Boot tests green): (1) **head-latch** — `current_head_` is now latched from the start
control word's MR/SD bit *before* `doReadSector` (was stale → IDAM head mismatch); (2)
**MK-strobe field model** (`K5122::buildField()`/`advanceField()`) + **CRC-16**
(`loaderCrc16()`). Each address-mark field advance is driven by an MK rising edge (ctrl Port A
bit1, `0xB5`→`0x87`) — same for ROM and loader; `ioRead(0x16)` streams the current field
(IDAM `A1 FE …` / DATA `A1 FB <128> CRC`) and pads over-reads with gap `0x4E`, so per-field
byte counts don't matter. The loader CRC-verifies every sector (`CALL 0x0407`), so the synthesised
DATA field carries the real CRC-16 (`loaderCrc16` = byte-exact `sub_0407`, start `BF84H`). Sync
byte `0x00`→`0xA1`. A fourth fix (**track-end /BUSRQ release**: on `OUT(13H),03H` during a read, K5122 releases
`/BUSRQ` so ZVE1 takes over before ZVE2's idle loop `L0696` corrupts `[07F8..07FC]`) completed
the secondary bootloader. **Result (all 40 K5122/Boot tests green, banner no-regression):** the
full secondary loader runs — ZVE2 reads whole tracks across cyl 0/1/2, ZVE1 CRC-verifies every
sector, `[07FC]` counts to 0, and the loader jumps to `0x0880` (`JP 0x1800`) into the **third
stage (CP/A boot system)**. The screen shows `CP/A-Bootsystem, Version 05.04.88 laedt @OS.COM`.

### Third stage — @OS.COM read from the 1024B data area (current frontier, 2026-06-09)

The third stage reads `@OS.COM` from the **1024B data area**. Four fixes got it reading and
loading `@OS.COM` (all K5122/Boot/K2526 tests green, 111/111):

1. **ZVE2-from-reset** (`K2526::zve2StartFromReset`, committed 9af4912). The stage poises ZVE2 by
   `[0x0000]=JP 0x1F7D` + `OUT(04)=0x00` (reset) then immediately restores `[0x0000]`, never doing
   an explicit bit0=1 start. `A5120Machine::run()` now starts ZVE2 from PC=0 when `/BUSRQ` asserts
   while ZVE2 is reset (the `/STR`'s `/BUSRQ`), so it fetches the live `[0x0000]=JP 0x1F7D`. Gotcha:
   fire on `isZVE2InReset()` ALONE (ZVE2 is both reset AND `/WAIT`-held in this path).
2. **Disk geometry** (`format_parser.cpp`, **asymmetric mixed geometry**): sides interleave (cyl0/A,
   cyl0/B, cyl1/A, cyl1/B, …); the system area is **three** 128B sides (cyl 0 both sides + cyl 1 side A)
   and the **1024B data area begins at cyl 1 side B** (`0x2700`). `3×3328 + 5120 = 0x3B00`, so the CP/M
   directory (`"@OS     COM"` + CPABCGEN/FORMAT/…) lands exactly on the **cyl 2 side A** boundary, where
   `0x1F7D` reads (IDAM cyl=2, size_code=3). Format = `{0,0,0,1,26,128}` + `{1,1,0,0,26,128}` +
   `{1,1,1,1,5,1024}` + `{2,79,0,1,5,1024}` (findTrack/sectorOffset honor the head range). Modelling
   cyl 1 side B as 128B would shift the data area 0x700 early and misalign @OS.COM's allocation blocks.
3. **Continuous-stream field model + CRC seed** (`k5122.cpp`, gated `sector_data_len_ != 128`). The 3rd
   stage reads IDAM+DATA **continuously** (INIR, no per-byte strobe), re-syncs via **MK1 (ctrl Port A
   bit4**, `0xB5↔0x85`), not MK/bit1 — so the discrete field model never advanced IDAM→DATA and served
   gap (`0x4E`). Now `buildField()` emits one continuous full-sector block for 1024B and `ioRead(0x16)`
   auto-steps to the next sector on over-read. Its CRC routine `sub_1E44` is **byte-identical to
   `loaderCrc16`** but inits **`0xCDB4`** (not 0xBF84) over `[data-mark]+data`, so the DATA field CRC is
   `loaderCrc16([0xFB]+data, 0xCDB4)`. (Verified `loaderCrc16([4E]+4E×1024, 0xCDB4)=0x87B3`.)

4. **K5122 side-select = ctrl Port A bit2 (/FR), NOT bit5 (committed `ab59ee8`).** bit5 (MR/SD) is
   step DIRECTION only and toggles with the MK/MK1 re-sync strobes; using it as side-select flipped the
   head mid-transfer so head-1 reads served head-0 data. Now `current_head_ = (data & 0x04) ? 0 : 1;`
   latched ONLY at the `/STR` edge. This removed the `RU;…=020101` (cyl2 head1) timeout. See
   `doc/design/07_k5122_afs.md §4` (signal table corrected) and `tests/cpp/test_k5122.cpp`
   (`ReadStrobe_LatchesHeadFromBit2`, `Continuous1024_Head1_*`) + `test_k2526.cpp`
   (`K2526ZVE2FloppyChain` — real ZVE2 reads K5122 field via the bus, head0 & head1).

**Result (2026-06-09):** with the bit2 fix the 3rd stage reads cyl 2 (both heads) + start of cyl 3 —
ZVE2 reads **9 sectors** successfully. **Current frontier: `RS;T,Si,Se=030002` ('S', NOT a CRC error).**
DIAGNOSED (ZVE2 side): after the 9th sector ZVE2 spins **unbounded** in its IDAM sync-search
`0x1FA9–0x1FBF` and never finds the `A1 FE` of the 10th wanted sector — that path decrements no counter
(`[0x1ED7]` only counts on a *found-but-mismatched* IDAM at `0x2023`; `[0x1ED5]` CRC-retry is never
touched → confirms not CRC). ZVE1's outer retry `[0x1ED6]` (init 5) then exhausts → 'S'. The routine
`0x1F7D` reads ONE sector/call to staging buffer `0x21AE`, wanted sector `L'` (start 2), head `B'`,
cyl `C'`. **OPEN (ZVE1 side — next concrete step):** how does ZVE1 sequence cyl/head/sector across the
@OS.COM sectors (read-setup `0x1F36`, retry `0x1D3C–0x1DDE`, sources of `C'/B'/L'`), and **what does the
10th read request** — does that sector exist (IDs 1–5, both heads) or does sector/head/cyl run out of
range / is a seek or head-switch missing between read 9 and 10? **Tried & reverted (don't repeat):** a
"no-rewind on same-track `/STR` refresh" guard in `doReadSector` — never fired (no `/STR` re-strobes in
the failure window, only MK1 toggles) and broke 3 K5122 tests.
Key 3rd-stage addrs: read/verify `0x1F7D`/`0x2038`, IDAM-mismatch `0x2023` (`[0x1ED7]`), data read
`0x2038–0x2061` (INIR×4=1024B), loop-tail `0x2068` (`INC L`), CRC `sub_1E44` (seed 0xCDB4), CRC-compare
`sub_1E20`, retry `0x1D3C–0x1DDE` (`[0x1ED6]`@`0x1DE9`), error display `sub_1BF0` ('C'=CRC@`0x1DE1`,
'S'@`0x1E00`, 'U'=timeout@`0x1E04`). Repro: `boot_trace -L /dev/null -c 40000000 -p 38000000
disks/cpadisk01.img` (big `-c` AND `-p`); ZVE2 instr trace: `-z 0x1F7D:0x2076 -W <n>`; RAM dump of the
loaded stage: `-d 0x1C00:0x2080 /tmp/loader.bin` then `tools/z80_disasm2.py --org 0x1C00 --entry 0x1F7D`.
Full model: `doc/K1520_architecture.md` §14.5/§14.5b/§14.6/§14.6a/§14.6b; `doc/analyse_bootloader.md` §7.

`boot_trace` post-boot tracing: `-p <cycles>` continues past `0x0437`; the summary then
adds an I/O-port read/write histogram, VRAM write count + range, a loaded-code PC
histogram, and an 80-col text dump of VRAM (`0xF800`) so the screen banner is visible.

## Subagenten / Delegation an günstigere Modelle

Projektspezifische Subagenten liegen in `.claude/agents/`. **Delegiere abgrenzbare Teilaufgaben
an diese Agenten, statt sie selbst zu erledigen** — das spart Kosten (Haiku/Sonnet statt Opus)
und hält den Hauptkontext frei. Faustregel: kontextarme, gut umrissene Arbeit auslagern; eng mit
dem laufenden Arbeitsstand verwobene Arbeit selbst behalten.

| Agent | Modell | Wofür |
|-------|--------|-------|
| `log-trace-analyzer` | haiku | Auswertung großer `boot_trace`-/Emulator-Logs, Trace-/ctest-Ausgaben, VRAM-/Port-Histogramme — liefert nur die Schlussfolgerung. |
| `code-explorer`      | haiku | Read-only Code-/Symbol-/Fundstellen-Suche über `src/` + `core/` + `tests/` + `tools/`. |
| `test-runner`        | haiku | Bauen + `a5120emu_test` / `ctest` ausführen, Pass/Fail knapp berichten, gegen bekannte pre-existing Failures abgrenzen. |
| `cpp-coder`          | sonnet | Umrissene C++-Implementierungen in `core/`/`src/` + zugehörige GoogleTests, im Stil der Umgebung. |
| `boot-disasm-analyst`| sonnet | Z80-Disassembly + ZRE-Boot-ROM/ZVE1↔ZVE2-DMA-Analyse mit den `tools/`-Werkzeugen. |

Konkret heißt das u.a.: breite Suchen → `code-explorer`; Log-/Trace-Auswertung → `log-trace-analyzer`;
Build-&-Test-Durchläufe → `test-runner`. Opus bleibt für Orchestrierung, Entwurf und Entscheidungen.

## Conventions

- Code comments and many log strings are in German; match the surrounding language of the file you edit.
- Card classes encode DIP switches / backplane bridges as compile-time config structs (e.g. `K2526::A5120Config`), not runtime settings.
- `cparun/` is an independent sub-project (own `CMakeLists.txt`) and is kept unchanged.
