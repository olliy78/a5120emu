<!-- Ausgelagert aus CLAUDE.md am 2026-08-19.  Diese Datei gilt WIE CLAUDE.md,
     sobald an diesem Teilsystem gearbeitet wird — sie ist nur nicht mehr in jeder
     Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# Boot-ROM-Fehlersuche und Ablaufverfolgung — Merkposten

The full CP/A cold boot works (boot ROM → SYL loader → secondary loader → CP/A boot system →
`@OS.COM` → running OS at the interactive prompt); the ZVE1↔ZVE2 DMA handshake is **solved**.
This section is the reusable debug/trace toolkit for the boot path (still the trickiest code to
poke at); `doc/analyse_zre_rom_boot.md` + `doc/K1520_architecture.md` §14 hold the analysis.

> **Start here: `tools/how_to_debug_and_trace.md`** is the task-oriented guide for the two
> debug/trace tools (which one when, with worked scenarios). Full references:
> `tools/k1520dbg.md` and `tools/boot_trace.md`. Two tools, complementary:
> - **`boot_trace`** — non-interactive: run a boot to a cycle limit / `--until <cond>`,
>   get a report (milestones, `[03F8]` done-flag, PC histograms, VRAM banner). **Locates**
>   *where* the DMA/boot hangs; also `--coverage`/`--diff`/`--csv` exports, `--fold`
>   (PC-period loop-collapse — crushes even register-varying hot loops), `--itrace`
>   (accepted-INT/NMI CSV), and a K5122 read-attempt log via `--log-level info|debug`.
> - **`k1520dbg`** — interactive gdb-style: breakpoints (incl. conditional / `bint`/`bnmi`/
>   `breti` and floppy `bbusrq`/`bxfer` event BPs), step into/over/out, `rs` reverse-step +
>   `rc` reverse-continue + `snap`/`snap diff`/`savestate`, watch mem/io, `logpoint`,
>   `itrace`, `x` examine, exact history `bt`, `where` (both CPUs at a glance), `hist`
>   (PC hotspots of both CPUs), `disk verify` (medium CRC health), `vars -f` (loadable
>   dashboard), `dev ctc/pio/sio` chip state, `help floppy`/`help dualcpu`. **Dissects** a
>   located problem. Full command list + the Dual-CPU/Floppy-read recipe:
>   `tools/how_to_debug_and_trace.md` §0b.
>
> **Run efficiently (this matters for the agent):**
> - **Invoke via `tools/dev.sh`** (rebuilds first → never a stale binary, see Build & test):
>   `tools/dev.sh trace <boot_trace-args>` and `tools/dev.sh tool k1520dbg <args>`. The bare
>   tool names in the examples below stand for these wrappers.
> - ✅ **Disk safety is now the default (Copy-on-Write).** Both tools copy the disk to a
>   temp file and mount that, so a committed fixture can't be corrupted — no more
>   `mktemp; cp DISK $D; … $D; rm $D` ritual; just pass the disk directly. Use `--rw` only
>   when a write must persist (e.g. FORMAT tests), then work on your own temp copy.
> - boot_trace: `-L /dev/null` discards the verbose emulator log; **`--quiet --json`** gives
>   exactly one machine-readable result line (instead of ~880) + a meaningful exit code
>   (`--until`: 0 met / 2 not met). Prefer **`--until <cond>`** over guessing cycle counts.
> - k1520dbg: drive it in one shot via a pipe (`printf 'b 0x0437\ng\nrj\nq\n' | k1520dbg $D`)
>   or `-x script.dbg`; `rj` prints registers as JSON. The interactive REPL (line editing via third_party/isocline) is for
>   humans — the agent uses batch mode.
> - **Boot once, resume often:** `--save-state`/`--load-state` (boot_trace) and
>   `savestate`/`loadstate` (k1520dbg) persist RAM+CPU+ROM-mapping to a file, so the ~2 s
>   boot is a one-time cost. Load `-l <bios.prn>` to see commented source instead of raw disasm.

Supporting tools (`tools/`):

- `tools/z80_disasm2.py` — the canonical generic Z80 disassembler (configurable `--org`, repeatable `--entry`/`--label`). The other two disassemblers are format.com-specific.
- **`k1520dbg`** (`tools/k1520dbg.md`) — the interactive debugger; expression-conditioned breakpoints, reverse-step, save-state, and `.prn`/symbol annotation make hand-disassembling RAM dumps mostly unnecessary. Delegate heavy log/trace reads to the `log-trace-analyzer` subagent.
- **`.prn`-Listing-Annotation (`-l`, both `k1520dbg` and `boot_trace`)** — instead of hand-disassembling RAM dumps, load the commented MACRO-80 source listing of the running code (e.g. `-l ~/projects/CPA_Workbench/build/bios.prn`) and every disassembly/trace line + PC-histogram entry whose address is in the listing gets the **original label+mnemonic+comment** appended. Repeatable (multiple listings cover different ranges); `@OFFSET` (signed, `0x..`/`..h`/dec) relocates a listing's addresses to the runtime load address. Only absolute addresses — a BIOS listing covers ~`0xD200+` (and BIOS pieces mapped low, e.g. the CONIN keyboard poll at `0x041C–0x042B`). Parser: header-only `tools/prn_listing.h` (tests `tests/debugtools/test_prn_listing.cpp`, gtest suite `PrnListing`). See `tools/k1520dbg.md` §6 / `tools/boot_trace.md` §4.
- **Fremdquellen `.MAC`/`.ASM` (`-l quelle.mac[@auto]`)** — für Fremd-OS (UDOS, SCPX …) gibt es kein `.prn`, nur reinen Quelltext ohne Adressspalte. `tools/mac_listing.h` **assembliert** ihn (Opcode-Tabelle wird zur Laufzeit aus `z80dis_min.h` *rückwärts* erzeugt; `ORG`/`EQU`/`DB`/`DW`/`DS`, `Mxxxx`-Adressanker mit Selbstkorrektur) und liefert dieselbe Adresse→Quellzeile-Tabelle. **`@auto`** bestimmt den Ladeversatz selbst (Objektbytes im RAM suchen, alle Kandidaten bewerten) und urteilt über die Passung (*identischer Build* … *anderer Build*). Ergänzend `verify <datei> @<adr>` (Datei↔RAM-Abgleich) und `dump <adr> <len> <datei>`. Tests `tests/debugtools/test_mac_listing.cpp` (`MacListing`). Doku: `tools/k1520dbg.md` §6.1, `tools/how_to_debug_and_trace.md` §0d.
- **break-before-execute (Debugger-Halt)** — `Z80::abortBeforeExecute` (nur ausgewertet, wenn ein `traceCallback` installiert ist → im Produktivlauf gratis): fordert der Trace-Callback einen Halt an, kehrt `step()` mit **0 Takten** zurück und die Instruktion läuft NICHT. `A5120Machine::run` behandelt `used==0` als Laufende. Damit zeigen Haltezeile und jede Folgeabfrage (`r`/`rj`/`where`/`snap`/`savestate`) denselben Zustand — vorher lief die Instruktion noch zu Ende (Haltezeile 0135, `r` 0136). `k1520dbg` überspringt beim Fortsetzen einmalig die Halteprüfung auf der aktuellen Adresse (sonst hielte `g` sofort wieder), und `s` zählt so, dass N Instruktionen ausgeführt werden und VOR der (N+1)-ten gehalten wird (gdb-Semantik). Guards: `Z80Test.AbortBeforeExecute_*`, `MachineRunControl.*`, `cli_dbg_stop_is_before_instruction`, `cli_dbg_resume_past_breakpoint`, `cli_dbg_step_shows_next_instruction`.
- **Debugger-Regressionsnetz** — `ctest -R cli_dbg_` sichert `k1520dbg` ab: `cli_dbg_all_commands_smoke` fährt über `tests/cli/scripts/all_commands_smoke.dbg` **jedes** Kommando einmal an (schlägt fehl, sobald eines aus der Dispatch-Kette fällt), dazu ~30 gezielte Tests auf den Meldungs-Wortlaut. Neue Kommandos gehören in beide. `MacListing.RoundTripsEveryDecodableInstruction` prüft Assembler↔Disassembler über den ganzen Befehlssatz.
- **Interrupt-Diagnose** — `k1520dbg ivt` zeigt die IM-2-Vektortabelle (Vektor → Tabelleneintrag → Gerät → Status; findet die scharfe Quelle ohne Tabelleneintrag), `dev pio [all|bs|k5122ctrl|k5122data]` jetzt inkl. **beider K5122-PIOs**, `bint`/`--itrace` melden Vektor **und Quellbaustein** und unterscheiden `SPURIOUS` (kein Gerät hat quittiert) vom Vektor 0xFF. **Lauf-Budgets (`g N`) zählen die Maschinenuhr (beide CPUs)** — `clock zve1` schaltet zurück; Ctrl-C bricht einen langen Lauf ab.
- `tools/disasm_difftest.py` — cross-checks the disassembler against the `z80dis` pip package (in `venv`); run it before changing the disassembler engine.
- `tools/boot_trace.cpp` (`boot_trace` target) — traces **both** ZVE1 and ZVE2 per instruction and reports where the DMA freezes. Use `-L <file>` to divert the emulator log so the summary stays readable. A separate `build_trace/` build dir is conventionally configured with `-DLOG_LEVEL=5` (the compile ceiling). **Default base level is now ERROR — the run is quiet & fast.** Raise it with `--log-level <off|error|warn|info|debug|trace>`, or far better, boost only where it matters: `--log-pc LO:HI[:level]` (effective level while either CPU PC is in the range) and `--log-cycle FROM:TO[:level]` (while the cycle counter is in the window). **Gotcha:** a `--log-pc` gate on a *spin-loop* address fires for as long as the CPU parks there (can be tens of millions of cycles → multi-GB log) — pair it with a tight `--log-cycle`, or just use a cycle window. Reference: `boot_trace --log-level info …` (≈11 KB / 8 s for a full @OS.COM run) gives the K5122 `>>> READ` summaries; add a `--log-cycle` window for full TRACE only there.

### Boot chain — SOLVED (don't regress these invariants)

The full chained boot (ROM `0x01DD` → SYL loader `0x0437` → secondary loader `0x062E` → CP/A
boot system `0x1800` → `@OS.COM`) runs end-to-end into the running OS. The read path was
refactored to the format-agnostic TrackImage stack (§ "K1520 core architecture" and
`doc/K1520_architecture.md` §8.5/§14.5); the load-bearing boot invariants a future editor
must **not** break:

1. **Concurrent ZVE1/ZVE2 stepping during DMA** (`A5120Machine::run`): while `/BUSRQ` is held
   and ZVE2 active, step ZVE2 **and fall through to also step ZVE1** (parallel on real HW). ZVE1
   must finish `CALL 0194` (tail writes `[0x03F8]=0`) and reach its poll loop `0x0168` before ZVE2
   writes `[0x03F8]=3`, else the late `=0` clobbers the `=3` and boot hangs.
2. **Transition-based completion watch** on `[0x03F8]` (0=running / 1=timeout / 3=done): the run
   loop arms only after seeing `[0x03F8]!=3` (ZVE1 cleared it) and *then* treats `→3` as completion
   — level detection fires on the stale `3` left from the previous round.
3. **ZVE2 start-from-reset** (`K2526::zve2StartFromReset`): the 3rd stage poises ZVE2 via
   `[0x0000]=JP 0x1F7D` + `OUT(04)=0x00` and restores `[0x0000]` immediately (no explicit bit0=1
   start), so `run()` starts ZVE2 from PC=0 when `/BUSRQ` asserts while ZVE2 is in reset. `OUT(04H)`
   bit0=1 also restarts ZVE2 from PC=0 every DMA round (reloads its IDAM regs).
4. **Faithful read stream + MK/MK1 resync** (`K5122`): `startReadTransfer()` streams
   `buildFaithfulReadTrack` (4×A1 sync — serves boot ROM *and* SYL loader); MK (ctrl Port A bit1) and
   **MK1 (bit4)** re-sync edges call `resyncToNextMark` (IDAM→DATA→next IDAM). The **MK1 resync was
   the final `@OS.COM` fix** — without it a data `0xA1` was mistaken for the A1 address-mark sync.
   Standard IBM-CCITT CRC throughout.
5. **Head-select = ctrl Port A bit2 (/FR)**, latched only at the `/STR` edge (bit5 is step DIRECTION
   only, toggles with MK/MK1). **Track-end `/BUSRQ` release** on `OUT(13H),03H` during a 128-B read
   (ZVE1 takes over before ZVE2's idle loop `L0696` corrupts `[07F8..07FC]`).
6. **Asymmetric mixed geometry** — now declared in **`data/formats.yaml`** (`cpa780`:
   `{0,0,0,1,26,128}` + `{1,1,0,0,26,128}` + `{1,1,1,1,5,1024}` + `{2,79,0,1,5,1024}`, all MFM);
   index period `≈490000` cycles. Guarded by `test_format_catalog`
   (`BootKritischeGeometrien_Unveraendert`). **Do not declare the 128-B system tracks as
   `encoding: fm`** — the disks are plain IBM-MFM and the ROM's FM→MFM trial-and-error depends on
   it (§14.5).
7. **/WR (BS-PIO Port A, A5) ist ein Strobe, kein Dauerpegel** (`K2526::pulseWriteStrobe`, pro
   ZVE1-Schreibzyklus gepulst; A0 `/M1` und A6 `/RDY` bleiben dauernd aktiv). Die
   Speicher-Ausbaumessung des Lade-ROMs (`0040H–005AH`) schärft Port A mit Maske `9FH`
   (A5 AND A6, aktiv-LOW), gibt `EI` und will den Interrupt **durch** das Testschreiben —
   ihre ISR (`007AH`) prüft, ob das Byte ankam. Dauerpegel ⇒ Interrupt schon beim `EI` ⇒
   die ISR sieht den ALTEN Speicherinhalt: bei frischem DRAM (0xFF) unauffällig, bei
   **Reset/Power-Cycle aus dem laufenden Betrieb** meldet sie „kein Speicher" und der
   Neustart entgleist. Dazu gehört, dass ein Interruptsteuerwort mit IE=0 eine anstehende
   Anforderung **verwirft** (`Z80PIO::writeCtrl`) — sonst bleibt die vom Stack-Push der
   Interruptannahme neu gesetzte Anforderung liegen. Guards: `test_k2526`
   (`K2526WriteStrobe.*`), `test_hardy` (MEMDI-RDY-Test nutzt dieselbe Maske).
8. **Reset ist ein SYSTEMWEITER /RESET, nicht nur die CPU** (`A5120Machine::resetHardware()`,
   von `reset()` **und** `powerOn()` benutzt). Die /RESET-Leitung des Backplane räumt alle
   Bausteine ab: `Z80CTC::reset` / `Z80PIO::reset` / `Z80SIO::reset` (neu),
   `K2526::powerOn` (Q302-CTC + BS-PIO), `K5122::reset` (Transfer abbrechen, /BUSRQ frei,
   PIOs; Disketten/Kopfposition bleiben), `K8025::reset`, `K7637::reset`, dazu
   NMI/INT/WAIT lösen + `markIntDirty()`. **Ohne das** zählte nach einem Reset aus dem
   laufenden Betrieb der System-CTC mit der IM2-Vektorbasis des alten OS (`vecBase=F8`,
   INT frei) weiter → der erste Timer-Interrupt nach dem `EI` des Lade-ROMs landet auf
   einem Fantasie-Vektor aus der ROM-Seite 0 → Boot-Kette entgleist (genau der Fall
   „nach der Uhrzeit-Eingabe am `A>` geht weder Reset noch Power ON"). `powerOn()` löscht
   zusätzlich das DRAM (`ops_.fill(0xFF)`) — Netz-Aus verliert den Inhalt, `reset()` nicht.
   Guards: `test_boot_integration` (`RestartFromInteractivePromptRebootsFromRom`,
   `ResetFromRunningSystemRebootsFromRom`, `PowerCycleFromRunningOsRebootsFromRom`).

Handshake RAM: `[0x03F8]` done-flag, `[0x03F7]` index counter, `[0x03FD]` path byte (`0x87`),
`[0x07F2]` target sector count, `[0x03F0]` load address. Key addresses: ZVE1 wait `0x0168`,
ZVE2-start `0x0194`, ZVE2 entry `0x01DD`, index ISR `0x01C7`, SYL sig check `0x01B6`, loaded code
`0x0437`, secondary loader `0x062E`, 3rd stage `0x1800`/read `0x1F7D`. Guard tests:
`test_boot_integration` (`Stage3_FullyLoadsAndJumpsToOs`), `test_k5122`
(`Continuous1024_MK1ResyncJumpsToNextAddressMark`), `test_k2526` (`K2526ZVE2FloppyChain`). Full
analysis: `doc/analyse_zre_rom_boot.md`, `doc/analyse_bootloader.md`, `doc/K1520_architecture.md`
§14.5/§14.5b/§14.5c.

`boot_trace` post-boot tracing: `-p <cycles>` continues past `0x0437`; the summary then
adds an I/O-port read/write histogram, VRAM write count + range, a loaded-code PC
histogram, and an 80-col text dump of VRAM (`0xF800`) so the screen banner is visible.
