# 32 — Umsetzungsplan: Kassette (29), frühe A5120 (30), MC80 (31) — Arbeitspakete für Agenten

Stand: 2026-10-13, Zweig **`kassette_mc80`** (von `origin/main` @ d39ef6e).  Fachlicher Inhalt
steht in den Entwürfen **29** (Kassette K5020/K5200), **30** (frühe A5120) und **31** (MC80 →
`mc80emu`); dieses Dokument schneidet die Umsetzung in **Arbeitspakete (AP)**, die je ein Agent
selbständig erledigen kann, und führt den **Stand**.

---

## 0. Spielregeln für jeden AP-Agenten

1. **Zuerst lesen:** `CLAUDE.md` (ganz), den zuständigen Entwurf (29/30/31) und die im AP genannten
   Unterlagen.  Vorbild für eine steckbare Option ist der Lochstreifen: `doc/design/23_lochstreifen.md`
   + `doc/merkposten/lochstreifen.md` (Festlegungen 1–13 gelten sinngemäß für die Kassette); Vorbild
   für eine neue Maschine ist der PC 1715 (`doc/design/21_pc1715.md`, `doc/merkposten/pc1715.md`).
2. **Bauen/Testen nur über `tools/dev.sh`** (nie Binaries direkt aus `build*/`).  Läufe > 60 s im
   Hintergrund.  Tests mit `k1520_add_test()` registrieren (`tests/README.md`), Ebene = Verzeichnis.
3. **Nur dieses AP.** Was dabei an Fremdem auffällt, kommt als Notiz in §3 („Funde“), nicht als
   Nebenbei-Änderung.
4. **Quellen nie verändern:** Abzüge in `doc/EPROMS/**`, `doc/original_docs/**`, `doc/kassette/fex/**`,
   `doc/a5120_alt/bcu/**`, `doc/a5120_alt/cpa_quellen/**` sind Belege.  Arbeitskopien für Tests nach
   `tests/fixtures/` (mit Herkunftsangabe in `tests/fixtures/README.md`); eingebettete ROMs wie bisher
   als erzeugte `*_data.h` (`tools/eprom_to_h.py`).
5. **Sprache:** Kommentare/Doku deutsch wie die Umgebung; keine Tastenkürzel ohne `Strg+Umschalt`;
   C-ABI nur additiv (`py_c_api` vergleicht Header ↔ Bibliothek ↔ ctypes).
6. **Bausteine vollständig testen** (Projektgrundsatz): jede Karte/jedes Signal mit eigenem GoogleTest.
7. **Abschluss je AP:** `tools/dev.sh test` grün (bzw. `-LE python` plus die eigene Python-Ebene),
   Stand-Tabelle §2 nachtragen (Datum, Commit, Wächter), **Commit in `kassette_mc80`**
   (Präfix `feat(kassette)`, `feat(a5120alt)`, `feat(mc80)`, `doc(…)`), **nicht pushen** — das macht
   die koordinierende Sitzung.
8. **Rückfragen** nur bei echtem Blocker (fehlende Information, die nicht in den Quellen steht);
   sonst begründet entscheiden und im AP-Ergebnis vermerken.

Agententyp-Empfehlung je AP in §2: **ana** = Analyse/Abschrift (Opus oder `boot-disasm-analyst`),
**cpp** = Kern-C++ (`cpp-coder` bei klarer Vorgabe, sonst Opus), **py** = Oberfläche/ctypes (Opus),
**int** = Integrationstest mit Gastsoftware (Opus — oft Fehlersuche im Kern).

## 1. Abhängigkeiten

```
Kassette:   K0 ─┬─ K1 ─┐
            K2 ─┘      ├─ K3 ── K4 ── K5 ─┬─ K6 ── K7 ─┬─ K9 ── K10
                       │                  │            └─ K8
                       │                  └─ (A8, M7)
A5120 alt:  A0 ──────────────────────────────────────┐
            A1a ── A1 ─┐                             │
            A2  ───────┼── A5 ── A6 ── A7 ── A8 (+K5) ── A9
            A3a ── A3 ─┘
            A4a ── A4 (später, unabhängig)
MC80:       M0 ─┬─ M2 ── M3 ── M4 ── M5 ── M6 ── M7 (+K4) ── M8 ── M9
            M1 ─┘
            M10 ── M11 ── M12          (MC80.2x, nach M6)
```

Parallel ohne Konflikt: alle reinen Analyse-APs (K0, K1, K2, A0, A1a, A3a, A4a, M0, M1, M10) —
sie schreiben nur unter `doc/`.  Bauende APs **nacheinander** (gemeinsames `build/`) oder je Agent
ein eigenes Worktree.

## 2. Stand

| AP | Titel | Art | hängt ab von | Umfang | Stand |
|---|---|---|---|---|---|
| K0 | K5020: Abschrift Programmierung + Signale | ana | — | M | **fertig 2026-10-14** |
| K1 | Abgleich der vier K5020-Treiber, Bandformat | ana | K0 | L | **fertig 2026-10-14** (957c329, `doc/kassette/treiber.md`) |
| K2 | K5200: Laufwerkszeiten und Signale | ana | — | S | **fertig 2026-10-14** |
| K3 | `peripherals/kassette`: Medium, `.k52`, Laufwerk | cpp | K1, K2 | M | **fertig 2026-10-14** (bbcd4a3; `KassettenMedium.*`, `K52Codec.*`, `KassettenLaufwerk.*`) |
| K4 | `cards/k5020` | cpp | K3 | M | **fertig 2026-10-14** (113367c; `K5020Test.*`, `K5020Rom.*`) |
| K5 | Stecken in allen Maschinen, C-ABI, Python, Werkzeuge | cpp/py | K4 | M | **fertig 2026-10-14** (f24c155; `K5020Maschine.*`, `py_tape_api`, `cli_dbg_tape*`, `cli_bt_tape`) |
| K6 | CLI `k1520tape` (+ Bootband) | cpp | K3 | S | **fertig 2026-10-14** (96b72f9; `cli_k1520tape_*`) |
| K7 | A5120 bootet vom Band | int | K5, K6 | M | **fertig 2026-10-14** (04575b5; `KassetteBoot.*`, `cli_bandtest_*`) |
| K8 | SCPX + `KMBG.REL`: Block schreiben/lesen | int | K5 | M | **fertig 2026-10-14** (7ff31f9; `ScpxKmbg.SchreibtUndLiestEinenBlock`, `cli_kmbgtest_com_passt_zur_quelle`) |
| K9 | Oberfläche „Kassette“ | py | K5 | M | **fertig 2026-10-14** (c28ebbc; `py_kassette_gui`) |
| K10 | Merkposten Kassette, CLAUDE.md, Handbuch | doc | K7–K9 | S | **fertig 2026-10-14** (6913361; `py_help`, `py_kassette_gui`) |
| K11 | optional `.wav` (ISO 3407) | cpp | K3 + Mitschnitt | M | zurückgestellt |
| A0 | Lade-ROM-Fassungen V0.6/V0.9 vergleichen | ana | — | S | **fertig 2026-10-14** |
| A1a | K7023: Abschrift BD/SLP | ana | — | S | **fertig 2026-10-14** |
| A1 | `cards/k7023` | cpp | A1a | M | **fertig 2026-10-14** (7dd94df; `K7023.*`) |
| A2 | K3525 (16 KB) | cpp | — | S | **fertig 2026-10-14** (94e2360; `K3525.*`) |
| A3a | K7636 + Anschluss an K2526: Abschrift | ana | — | M | **fertig 2026-10-14** |
| A3 | K7636-Modell + K2526-Anschluss | cpp | A3a | M | **fertig 2026-10-14** (9ab3e1f; `K7636.*`, `K7636K2526.*`, `py_keyboard_k7636`) |
| A4a | K5120: Analyse (BD, 2 ROMs, CP/A-Treiber) | ana | — | L | **fertig 2026-10-14** (`doc/a5120_alt/k5120.md`; K5120 ≈ K5122, A4 nur als Variante, Anwenderwunsch) |
| A4 | `cards/k5120` | cpp | A4a | L | zurückgestellt |
| A5 | `A5120Machine::Config::ausfuehrung`, C-ABI, Profil-Modelle | cpp/py | A1, A2, A3 | M | **fertig 2026-10-15** (514cf6a; `A5120Ausfuehrung.*`, `py_a5120_modelle`, `py_c_api`) |
| A6 | CP/A auf „5¼″ alt“ bis Prompt | int | A5 | M | **fertig 2026-10-15** (4f21185; `BootIntegrationAlt.*`, `cli_bt_ausfuehrung_alt`, `cli_dbg_ausfuehrung_*`) |
| A7 | BCU 880 bis Prompt | int | A5, A0 | L | **teilweise fertig 2026-10-15** (45c4adc; V1.2 bootet bis `A>`; V2.1 mit keinem vorhandenen Lade-ROM startbar — Befund; `BootIntegrationBcu.*`) |
| A7b | Experiment: V2.1-System auf SYL-Bootaufbau (Eigenbau, kein Beleg) | int | A7 | S | **fertig 2026-10-15** (a38f612; `BootIntegrationBcu.V21EigenbauBootet`; EIGENBAU, kein Beleg) |
| A8 | Modell KMBG: BCU mit Kassette F: | int | A7, K5 | M | **fertig 2026-10-15** (0c18b03; `BcuKassette.FormatiertSchreibtLiest`, `Z80Test.EiSchatten_*`, `RetiChain.PioRetiVonB*`) |
| A8b | Vormerkung: EI-Schatten/RETI-Lesart in K8915, PRG 710, PC 1715, CTC/SIO/Bus nachziehen | cpp/int | A8 | M | vorgemerkt (riskant: alle Gast-Boots laufen neu) |
| A9 | Oberfläche/Handbuch/Merkposten A5120 alt | py/doc | A6 | S | **fertig 2026-10-15** (e0c3231; `py_help`, `py_a5120_modelle`, `py_kassette_gui`) |
| M0 | MC80.3x: Hardware-Abschrift | ana | — | M | **fertig 2026-10-14** |
| M1 | EGOS-Disassemblat (`.prn`) | ana | — | L | **fertig 2026-10-15** (b990db5/ee73ea9; `doc/mc80/egos.prn` + `egos.md`, Wächter `cli_mc80_egos_prn_passt_zur_quelle`) |
| M2 | `Mc80Machine` Gerüst: ZVE, SPE, Segmente | cpp | M0 | M | **fertig 2026-10-15** (0e0a27e; `Mc80ZveTest.*`, `Mc80Spe*`, `Mc80Maschine.*`, `py_c_api`) |
| M3 | VIS2a + ASP + Tastatur → EGOS meldet sich | cpp/int | M2, M1 | L | **fertig 2026-10-15** (abf94cc; `Mc80Boot.*`, `Mc80Vis2a.*`, `Mc80Asp.*`, `Mc80Tastatur.*`, `Z80CTC.IntAusVerwirftAnforderungNurMitSchalter`) |
| M4 | MC80.31E-Programmsegmente 10H/20H | int | M3 | S | **teilweise fertig 2026-10-15** (23d7e8f; EPROM-Segmente 10H/20H, TE und HASI laufen aus dem EPROM; Sondertasten→Editor/BASIC nicht belegbar; `Mc80EpromSegmentTest.*`, `Mc80Segmente.*`) |
| M5 | Speicherabzug laden + Spiele-Wächter | cpp/int | M3 | S | **fertig 2026-10-15** (d3fdc25; `Mc80Spiele.*`, `py_c_api`) |
| M6 | Programm `mc80emu` + Paket | py | M3 | L | **fertig 2026-10-15** (8efbc89; `py_mc80emu_gui`, `py_packaging`, `py_programme`, `py_gui_smoke`) |
| M7 | Kassette an MC80.31/.30/.33 | cpp/int | M3, K4 | L | **fertig für .30/.31 2026-10-15** (1c39e26; `Mc80Kassette.*`, `Mc80K5020.*`; .33 gesperrt, `doc/mc80/pk3.md`) |
| M8 | `boot_trace`/`k1520dbg --machine mc80` | cpp | M3 | M | **fertig 2026-10-15** (83fef80; `cli_bt_mc80_*`, `cli_dbg_mc80_*`) |
| M9 | Merkposten MC80, CLAUDE.md, Handbuch | doc | M6 | S | **fertig 2026-10-15** (a5c37cd; `doc/merkposten/mc80.md`) |
| M10 | MC80.2x: Hardware aus Handbuch + ROM-Disassemblat | ana | — | L | **fertig 2026-10-15** (0287094; `hardware_2x.md`, `bsys_2x.prn`, `cli_mc8020_bsys_prn_passt_zur_quelle`) |
| M11 | MC80.21/.22 im Kern (DPL/BSA/MBI) | cpp/int | M10, M2 | L | offen |
| M12 | MC80.2x in `mc80emu` | py | M11, M6 | M | offen |

## 3. Funde während der Umsetzung

(von den Agenten fortzuschreiben: Datum, AP, Fund, Folge)

- 2026-10-14 K0: Entwurf 29 §4.1 ordnet die Init-Worte falsch zu (`CFH,80H,17H,7FH`/`CFH,FDH,17H,DFH` gehören zur Steuer-PIO; Daten-PIO nur `0FH`/`4FH`); die Steuer-PIO braucht Interrupt bei High (FEX, `37H`) UND bei Low (KMBG, `17H`). Blocklänge laut BD 2–256. `zre.prn:437`-Kommentar „Bits 1,0 löschen“ falsch (AND F3H löst internes Rücksetzen aus). Folge: K1/K4 beachten.
- 2026-10-14 K2: K5020 kennt UMS/WID/KNG/AVE nicht (Schreibschutz für die CPU nur indirekt). 12 000/6 000 Bit/s nominal, Bitzelle 83,33 µs. CRC/Präambel stehen nicht in der K5200-Unterlage → K1. Bei 19 cm/s keine Zeiten in der BD.
- 2026-10-14 K0/M0: Bitzelle als ZEIT modellieren, nicht als Teiler (Teilerverhältnis nicht lesbar); externer Takt X3B06/X4B06 beim MC80.30 offen → M7.
- 2026-10-14 A0: V0.6 ist ein anderes Programm (liest Zylinder 1, 6×128 B, Einsprung 042EH) → BCU „SYL auf Spur 1“; V0.9_2 bleibt Vorgabe. Der Kern kann kein anderes Lade-ROM laden → A5/A7 brauchen eine Konfigurationsgröße dafür. „BCU ohne SYL“ braucht vermutlich unbekanntes „EPROM BCU 880 V2.2“ (A7 offen). k8924-Abzug handgepatcht (CRC passt nicht).
- 2026-10-14 A1a: K7023 hat KEINE Lesesperre; K7023.01 hat Helligkeit je Zelle (Marken 04H–0FH) — K7024-Kern ignoriert Marken. Für „alt“ muss K2526 `config_bridges` Bit 1 (Port 0AH Bit 6 = BAB1) gesetzt werden (A5). VRAM beim Start 0xFF vorschlagsweise.
- 2026-10-14 A3a: K7636 Statusbit 3 = /UINT (0 = Code bereit), Ruhewert 0CH muss das K7636-Modell selbst vorgeben (K2526-Standard unverändert lassen); Typzeichen 80H nach Reset/Kommando. CSV hat Druckfehler-Stellen mit [?] (A15 DA→im Modell 0A).
- 2026-10-14 M0: **Entwurf 31 §3.1 ist falsch: Segmentumschaltung über Port 85H = ZVE-PIO Kanal B** (A = Einsernummer). Takt 2,4576 MHz (MAME stimmt). SPE-2-Sperrports B0H–B3H, MAME bildet SPE nicht ab. Offen: Schreibschutz 8CH, SPE-2-Zustand nach Reset (→ M1), VIS2a-Lesepfad.
- 2026-10-14 K1: Blockformat = reine Software: `AA | 1…256 Daten | CRC-16/ARC (low zuerst, Start 0000, nur Daten) | AA`; Bandmarke = Block ohne Daten `AA 00 00 AA` (rückwärts `AA FF FF AA`). **Berichtigung K0:** Steuer-PIO A Bit 7 = 1 heißt Band unter der Lichtschranke, 0 = AEB aktiv. Bootband: 4×128 B nach 0400H, `SYL` in 0400–0402, Einsprung 0434H (darf nicht zurückkehren). STA = AUF ∧ AVE. FEX-Formate nicht belegbar (K6: nur Rohblöcke + Bootband). Details `doc/kassette/treiber.md` §8.
- 2026-10-14 K3: Bitrate ergibt sich aus Dichte (31,5 Bit/mm) × Geschwindigkeit; Zeiten LW1200/SK1200/PK3/K5200-19 sind Annahmen `[?]`. K4 muss: PIO-Polarität, AWA-Gating, BMS-Verzögerung 108 µs, „HGE nach AUF“ → `wiedergabe(false)`, TRV+TRR → `umspulen()`, `autoFlush()` je run()-Scheibe (K5).
- 2026-10-14 K4: Lade-ROM bootet vollständig von einem K3-Band (Test `K5020Rom.LiestEinBootbandMitVierBloeckenUndPruefSyl`). Steuer-PIO braucht Flankentrigger (`setModus3Flanke`), Daten-PIO `modus1ReadyNachLesen`. UMS wirkt auf steigende Flanke. K5: `clockTick` mit Instruktionstakten, bei true `markIntDirty()`+`updateInterruptChain()`, `autoFlush()` je run(); Chain `{pioDaten, pioSteuer}`; `takt_hz` je Maschine (A5120 2 457 600). `k5020.md` §3.1 (AEB-Polarität) ist noch zu berichtigen.
- 2026-10-14 K5: PC 1715 steckt die K5020 auf D0H (30H–37H belegt durch LT107/BWS), PRG 710 bleibt bei 30H (D0H–D4H = EPROMmer); nur mechanisch am PC 1715. Gast-ISRs: kein HALT-Warten auf die ISR (Wettlauf), ISR A > 0x20 B — siehe `doc/merkposten/kassette.md`. GUI (K9) muss `tape_flush` je Laufwerk beim Beenden rufen. Nach Kern-/Werkzeug-Änderungen einmal `tools/dev.sh build` vor dem Volllauf (7 CLI-Fälle waren nur veraltete k1520dbg-Binary). Python-Ebene im Worktree braucht `venv/` (Symlink aufs Haupt-venv).
- 2026-10-14 K6: Block mit 128 Daten = 132 B auf dem Band (nicht 131, das ist die ROM-Leselänge). `k1520tape bootband prog.bin band.k52` verlangt 'SYL' am Anfang; Einsprung bei 34H liegt im Anwenderprogramm. Lücke zwischen Blöcken 10 mm = Annahme [?]. CLI-Runner kennt nur Textdateien.
- 2026-10-14 K7: **A5120 bootet ohne Diskette vom (selbstgebauten) Band** — `tools/kassette/bandtest/`, Fixture `tests/fixtures/kassette/bandtest_boot.k52`. Kernfehler in K5020 behoben: `Z80PIO::setExtern` übernimmt Pegel nur im Modus 3 und holt sie beim Moduswechsel nicht nach → nach jedem Steuerwort Rückleitungen neu anlegen (`ext_neu_`). Ob `Z80PIO` selbst beim Moduswechsel neu einlesen sollte (P8000-Karten nutzen `setExtern`): offen [?]. Boot dauert ~27 Mio. Takte. `boot_trace --tape` bindet und schreibt zurück — nie Fixture direkt angeben.
- 2026-10-14 K9: GUI-Kassette fertig (Profilfeld `tape_wahl`, kein Kästchen am P8000; `tape_flush` beim Beenden/Neuaufbau/Auswerfen; `tape_load` legt fehlende .k52 leer an — Abweichung vom Lochstreifen). Kassette nicht im Save-State. Festlegungen für K10: Kasten nur mit Karte, `kassette:`-Abschnitt, kein Kürzel, Wächter `py_kassette_gui`.
- 2026-10-14 K8: `KMBG.REL` läuft unter SCPX 1.7 am A5120 (schreibt/liest Block, alle Status 00); kein Kernfehler. **Offen [?]:** KMBG wartet nur ~2,45 s (≈0,9 m bei 381 mm/s) auf Magnetband; der 1-m-Vorspann des Modells ist von Position 0 aus nicht zu durchqueren → Status 04 bei frisch eingelegter Kassette; erst nach Rückspulen (UMS) geht es. Vorspannlänge/Einlegeposition klären (K10/Handbuch oder Modell). `tools/cpm_bau.py` hat jetzt `module=` für Fremd-REL. `k1520dbg`: `--tape` vor dem Diskpfad.
- 2026-10-14 A2: `K3525` (16 KB, Basis im 4-KB-Raster, Power-on 0xFF, Bus-/MEMDI oder MEMDI1/2); vier Karten 0000/4000/8000/C000 = 64 KB; Lade-ROM der K2526 NACH den Karten registrieren. Refresh nicht modelliert. `K1520Bus::registerMem`: Überlappung wirft NICHT (Lesen: letzte Karte gewinnt, Schreiben an alle). BD der K3525 ist Scan ohne Textebene; Brücken X6/X7 [?].
- 2026-10-14 A1: `K7023` (.00/.01) fertig: FC00H, 64×16, Zelle 8×16, Framebuffer 512×256 (nicht 640×288 — GUI beachten), Kursor Bit 7 als Balken Linien 12–15, `.01` Marken 04H–0FH. GCC: Konfig-Struct mit Defaultwerten im Klassenkörper + `explicit K(bus, const Config& = Config{})` kompiliert nicht → zwei Konstruktoren (gilt für K7636/weitere Karten). Beim Einbau (A5): `config_bridges` Bit 1 setzen; Karte nach den RAM-Karten registrieren; `k1520_screen_char` über `vramRead`. Zwei SerielleKopplung-64KiB-Tests liefen bei paralleler Rechnerlast in den Zeitüberlauf (vermutlich Last).
- 2026-10-14 A3: K7636-Modell (`core/peripherals/k7636/`) + `UBusGeraet`-Schnittstelle an der K2526 (`setUBusGeraet` vor `powerOn()`, `service(zve1_cycles)` je Befehl; ohne Gerät unverändert K7637-Verdrahtung); Bildschirmtastatur `keyboard_k7636.py` (Matrix `0x03000000|Adresse`, noch nicht im Profil). Tabellenkorrekturen: A15 → 0AH, C02-b → 53H [?]. Annahmen [?]: 2 ms Reaktionszeit, Strg=ET2-Vorsatz. A5 muss anlegen: C-ABI für Lampenmaske (Bit 0–3 Wahl, 4 INS MODE, 5 Fehler), Kern bietet `wahlLampe()/insModeLampe()/fehlerLampe()`. Volllauf nach Merge noch nicht gefahren (A5 holt ihn nach). Nicht im Savestate.
- 2026-10-15 A5: `Config::ausfuehrung` Neu/Alt/Kmbg (Acht lehnt der Konstruktor ab, EM nur bei Neu); `Config::fuerAusfuehrung(a)` = 3× K5600.10; C-ABI `k1520_create_a5120_ausfuehrung`/`k1520_a5120_ausfuehrung`, Lampen der K7636 über `k1520_keyboard_leds` (Bit 0–3 Wahl, 4 INS, 5 Fehler, 6 SHIFT/LOCK, 7 Signal); Python `ausfuehrung="alt"|"kmbg"`; Profil-Modelle „A5120 (64×16, K7636)“, „A5120 Kassette“ (+ „A5120 8″“ ausgegraut). Offen: `boot_trace`/`k1520dbg` ohne `--ausfuehrung` (→ A6 zuerst); Konsolenmodus nur K7024; anderes Lade-ROM braucht Config-Feld (A7); GUI-Modellwechsel setzt Laufwerke nicht auf die Vorgabe der Ausführung (A9-Frage); K7637 am Alt-Modell nicht an SIO A32.
- 2026-10-15 A6: CP/A 25.09.89 der vorhandenen Disketten läuft auf „alt“ unverändert bis `A>` (BAB1, „Tastatur: K7636“, `dir`); kein Kernfehler, Weg 2 unnötig. `boot_trace`/`k1520dbg` haben `--ausfuehrung neu|alt|kmbg` und `--laufwerke`. Neue Fixture 40-Spur-K5600.10-Bootdiskette (noch combo5zoll-BIOS, Banner „A:5″(80,DD,DS)“; ein BIOS mit `diskA=10540` fehlt, optional). Tastendruck im CP/A-Autostart → `*break*` (Gastverhalten). Platte war voll: Worktrees/`build*` regelmäßig aufräumen.
- 2026-10-15 A7: BCU 880 **V1.2 bootet bis `A>`** (`dir b:`); **V2.1 startet mit keinem der vier Lade-ROMs** (Start-EPROM unbekannt; „EPROM BCU 880 VERSION 2.2“ ist nur die Kennung des Programms `EPROM.COM`). Beide „SYL auf Spur 1“-Sätze sind V1.2 (DT-INFO „Bootfähig: Nein“ falsch): Satz 2 → K7023/`Alt`, Satz 1 → K7024 + K7636 (`Neu`+`tastatur=K7636`). Neue Config-Felder `laderom` (V09_2 Vorgabe|V09_1|V06) und `tastatur`; Werkzeuge `--laderom`, `--tastatur`, `--format`. BCU lässt nach Reset 80H in der K7636 liegen (erstes Zeichen der ersten Zeile verloren). SYL-Disketten taugen nicht als Dateidisketten (OFF=2). C-ABI für laderom/tastatur fehlt noch (A9, falls GUI-Modell BCU). `KMBG.REL` liegt auf `bcu12_ss40-26x128_k5600-10_ohnesyl-sys1.img` (A8).
- 2026-10-15 A8: BCU V1.2 formatiert/schreibt/liest die Kassette in F: (Modell `Kmbg`, Lade-ROM V0.6). **Zwei Kernfehler behoben:** (1) `Z80::ei_schatten` + `interruptAnnehmbar()` — Interrupt wurde direkt nach `EI` angenommen (nur im A5120-Laufweg genutzt; K8915/PRG 710/PC 1715 prüfen weiter nur IFF1, vermutlich gleicher Fehler [?] → A8b); (2) `Z80PIO::onRETI` ließ Port B in Bedienung, wenn A nur anforderte (gleiche Lesart steckt noch in `Z80CTC`/`Z80SIO::onRETI` und `K1520Bus::updateInterruptChain` [?]). BCU-Bedienung: `OPEN B:KMBG`, F: = Gerät 2, Diskette „ohne SYL“ nach A:, INIT verlassen mit ^S/^C (Handbuch A9); BCU wartet im Vorspann ohne Frist (frische Kassette geht, anders als SCPX/KMBG). READ SEQ liefert 17-B-Abschlussblock als 24. Satz.
- 2026-10-15 A7b: **Eigenbau** (V1.2-SYL-Lader/Kaltstart + V2.1-System + V1.2-Plattentreiber, 11 Adressen umgebogen) bootet bis `A>`, `dir b:` geht; `tools/bcu_v21_eigenbau.py`, Fixture als „EIGENBAU, kein Original“. V2.1 trägt keinen residenten Plattentreiber ab F100H → stammt vermutlich aus dem unbekannten Start-EPROM [?]. Nicht erprobt: WRITE, WBOOT. `boot_trace` bricht nach ~4,8 Mio. Takten ab (längere Läufe `k1520dbg g N`).
- 2026-10-15 M1 (Details `doc/mc80/egos.md`): EGOS-Einsprünge aus §8 alle gefunden (VERA bei 22A8H statt 2290H; SEGC = FBFBH). **ROM-Kettensuche:** jede Suche läuft über eine RAM-Kette ab 0AFEH bis 2800H; liest der Leerbereich 2800H–3FFFH **00H** (wie MAME), erreicht die Kette 4000H (RAM-Programme/Spiele werden gefunden), bei FFH endet sie dort → M2: Leerbereich = 00H. **SPE-2 nach Reset: frei** (ROM-Block nicht segmentabhängig, nur RAM-Block hängt an 85H). **Schreibschutz 8CH/8DH: nach Reset aus**, EGOS fasst ihn nur in der Reset-Schleife an. Y=0 ist unten (erste Textzeile Y=F8H). VIS2a-Lesepfad: `LD C,0C3H/0CBH; OUT (C),L ×2; IN A,(C)`, Bildpunkt Bit 0 (Polarität [?]). Tastatur 9600 Bd 7O2; Präfixe 1CH/1DH/1EH(Sondertaste Bitmaske)/1FH; ENTER = 11H; Rechner→Tastatur 07H Piep, 12H/14H. Erwartete Einschaltmeldung „mc 80.30. microcomputer“/„Version 3.0/1,2“, „OK“ Zeile 23, „>“ Zeile 24. **Magnetband** (30E/31): K5020 an 30H–37H, nur Laufwerk 1, kein Block vor/zurück; Blockformat wie K1 (AA|20H…100H Daten|CRC|AA, CRC-16/ARC); **erstes gelesenes Byte muss ABH sein**; CTC-ASP K1 = Zeitgeber (10 ms/26,7 ms), K2 ÷2. Fassungen: 30E-Teile nur paarweise (0000v2+0800v2); `mc80.33-1000.bin` nur 1000H–17FFH (PK-3-Treiber ab 17E0H, zweite Hälfte fehlt → Kassette am .33 ohne ROM); `3s`/`31e-2400` = Kundenfassung CCD 1990; `mc80.31e-3800` defekt (Bytes doppelt). Offen: 2800H am Gerät FFH oder 00H, Wirkung einzelner 8CH-Ausgaben, VIS2a-Polarität, Tastencodes je Taste, Interruptkettenreihenfolge, Dateikopf auf Kassette.
- 2026-10-15 M2: `Mc80Machine` (.30/.31; .33/.21/.22 → `invalid_argument`), `cards/mc80/zve3x` + `spe`, `Mc80Segmentfenster`; C-ABI `K1520_MACHINE_MC80 = 6`, `k1520_create_mc80(variante, "spe=1|2")`. Ohne Tastatur durchläuft EGOS die RESET-Kette in ~3,67 Mio. Takten (≈1,5 s) bis zur Kommandoschleife 07D4H, dann Wartet in 02D7H (Tastatur-Interrupt); Bildkommandos kosten je ~43 ms Wartezeit. **Berichtigung `hardware_3x.md` §2.2:** ADT-Vorbelegung `LDIR 0C71H→FC00H` gehört zum Kommando INIT, nicht zum RESET. Annahmen [?]: Segmentregister nach Reset 00H; RAM-Sperre = Schreibschutz; ZVE-Interruptreihenfolge CTC vor PIO. M3: ASP (SIO D0H, CTC D4H, PIO D8H) + VIS2a einhängen; Test in eigene Datei `test_mc80_boot.cpp`. M4: EPROM-Segmente 10H/20H als weiterer Teilnehmer am `Mc80Segmentfenster` (`ramLiest/ramLesen/ramSchreiben`). M6: `MACHINE_TYPES["mc80"]` + `K1520Emulator` fehlen; K6022 auf E0H kollidiert mit EPR E0H–EFH. M8: `--machine mc80` fehlt. Tore der 2. SPE nicht angemeldet.
- 2026-10-15 M3: **EGOS meldet sich im Emulator** („mc 80.30. microcomputer“ doppelt hoch, „Version 3.0/1,2“, OK Zeile 23, „>“ Zeile 24); `INIT`/`CATA`/`CAT` über die Tastatur; `core/cards/mc80/{vis2a,asp,egos_text}`, `peripherals/mc80_tastatur`. Bitmap über vorhandenes `k1520_framebuffer` (512×256, Zeile 0 oben), `k1520_screen_char` 25 Zeilen (Spalte = Zeichenindex, Schrift proportional). **Z80CTC-Befund:** Steuerwort mit D7=0 (ASP K3: `91H`, dann `01H`) muss anstehende Anforderung verwerfen, sonst läuft die Tasten-ISR doppelt → Schalter `Z80CTC::setIntAusVerwirftAnforderung` (Vorgabe aus, nur ASP an); echtes Bausteinverhalten [?], Kandidat für A8b. VIS2a-Lesepolarität: Funktion 3 = Bildpunkt, 2 = negiert [?] (Segment 4C40H „Bild invertieren“). Tastatur: Präfix 1CH vor jedem Anschlag [Annahme], Mindestabstand 20 ms, INIT ~2,5 Mio. Takte (Tasten davor gehen verloren). M6: Tastenzuordnung `Mc80Tastatur::qtTaste` (Return=11H, Esc=13H, F1–F8 = Sondertasten 0–7, Kursor unbekannt). Interruptkette {ZVE, ASP}-Reihenfolge [?]. SIO B ohne Gegenstelle/SerialHub.
- 2026-10-15 M4: EPROM-Segmente 10H/20H (nur .31) als `Mc80EpromSegment` am `Mc80Segmentfenster` (neue Schnittstelle `Mc80SegmentKarte`); **Entwurf 31 §3 „FT2–FT5 laden BASIC/OCED“ ist nicht belegt → [?]**; Sondertasten-Belegung (FED0H+3n) fehlt in allen Abzügen; Editor `ED` liegt im ROM (`31e-3000/3400`), nicht im Segment. EGOS-Kette führt nicht von selbst in 10H/20H (Tests legen Kettenglied `FE 00 40 10` selbst ab). Lage der 4-KB-Abzüge teils abgeleitet (d=4000H/10H, f=4000H, g=5000H/20H, c=6000H belegt; b=5000H, h=6000H aus Sprungzielen; a=7000H Annahme [?]); Segment 20H „e“ fehlt. BASIC `ba` startet, endet nach ~13 Befehlen mit `ERROR %C7` (BASIC-Arbeitsbereich 'F'/'T'/Systemkassette fehlt?). Tetris: Sondertasten nach dem Start bringen es aus dem Tritt (nicht untersucht). .31 nutzt aktuell dieselben ROMs wie .30; ob die Kundenfassung `31e-2400…3800` (mit `ED`) eingebunden werden soll: offen.
- 2026-10-15 M5: `k1520_mem_load(h, addr, path)` (-1 Argumente, -2 Datei, -3 passt nicht unter 10000H) + `K1520Emulator.mem_load`; Debugger-`load` gab es schon. **Alle vier Spiele (Pharao, Wall, Tetris, Hasi) laufen auf .30** (Wächter `Mc80Spiele.*`, Bild ändert sich, keine ROM-Warteschleife). Voraussetzung: vorher `INIT` (sonst gibt es die RAM-Kette nicht). Pharao/Wall beginnen selbst mit Kettengliedern; `RKET 01,4000,adr` = leeres Glied `01 <adr-4003H> FF` an 4000H (Annahme, nicht gegen echtes RKET geprüft [?]).
- 2026-10-15 M6: Programm `mc80emu` (Modelle `mc80`=.30, `mc80-31`; .33/.21/.22 ausgegraut), neues Profilfeld `laufwerke=False` (keine Laufwerke/RAF/Lochstreifen am MC80), Bildschirmtastatur `keyboard_mc80.py` (BDA Bild A5; Kursor/CL/CRLF/÷/00 senden nichts [?]), *Maschine ▸ Speicherabzug laden…* (`mem_load`, Adresse aus Dateinamen vorgeschlagen), Paket/Inno/Starter/Rauchtest (6, „MC80“), Handbuchkapitel. Nach `INIT` ist Zeile 24 nur `>` (Echo verschwindet; Tests prüfen ADT + Prompt). M7: `tape_wahl=True` im Profil setzen. M9: CLAUDE.md „Vier Programme, eine Oberfläche“ + Merkposten um `mc80emu`/`laufwerke`/`ladeadresse_aus_namen` ergänzen. M12: gesperrte Zeilen nach `modelle`, `MC80_VARIANTEN` um 3/4. Spiele liegen nicht im Paket (Entscheidung Anwender). `test-format`/`test-matrix`/`win` nicht gefahren.
- 2026-10-15 M7: EGOS formatiert (`KINIT 1`), schreibt (`WRITE 1,TST,01,4000,40FF`) und liest (`READ 1,TST,R`) auf .30 und .31; Programm danach als Kommando startbar. **Kernfehler behoben:** `Mc80Machine::powerOn()` im Konstruktor schloss die Bestückung ab → `installK5020()` scheiterte immer; jetzt `resetHardware()` wie K8915. **Berichtigung egos.md:** EGOS nimmt als erstes Byte AAH **oder** ABH. Postambelprobe je gelesenem AAH (1C3A) ~1,97 ms → bei 12 kBit/s verliert der Treiber Bytes; .31 läuft daher mit K5200 bei 19 cm/s (6 kBit/s) [?]; zwei AAH hintereinander gehen trotzdem verloren (Treibergrenze, Testdaten haben nur einzelne AAH). .30: ASP-CTC K2 (Zähler) als Schreibtakt der umgebauten K5020 modelliert [?]. Neue MC80-Bänder = `Geometrie::kompaktkassette()` (80 mm Vorspann, keine Marken [?]) wegen EGOS-Zeiten (Rückspulen fährt 2 s vorwärts dann UMS → Vorspann < ~95 mm); gilt auch für `k1520_tape_new`/`tape_load`. Neue virtuelle `K1520Machine::k5020Konfigurieren`. INIT legt `00 F5 7A FF` an 4000H an (eigene Programme müssen den Abstand übernehmen). WRITE schreibt 5 zusätzliche 256-B-Blöcke [?]. .33: Treiber ruft die fehlende zweite ROM-Hälfte (1A12H) → gesperrt. Offen: externer Takt X3B06, K5200-Geschwindigkeit am .31, echte Kassettenmaße, Arbeitsbereiche 13H/1CH (`doc/mc80/kassette.md` §6). `test-format`/`test-matrix`/`win` nicht gefahren.
- 2026-10-15 M8: `boot_trace`/`k1520dbg --machine mc80|mc80-30|mc80-31 [--spe 1|2]`; boot_trace Exit 0 = Zeile 24 ist `>` (Prompt nach ~12 Mio. Takten, Stillstand 8 Mio. wegen 3,7 Mio. RESET-Kette ohne Bildänderung); `CATA` läuft lange ohne Bildänderung → `--stall` anheben. `map` zeigt 85H/Schutz/SPE-2-/EPROM-Segmente; `dev`, `ivt`, `keys`, `screen` 25×80. Offen: `map` zeigt B0H–B3H-Sperren nicht; `-33/-21/-22` nicht in `parseMachine`. **Build-Falle:** `dev.sh build/tool` meldete „aktuell“ trotz älterem `build/k1520dbg` (Zeitstempel-Kollision bei Quelländerung während eines Baus) → nach Änderung während Bau `touch` + neu bauen.
- 2026-10-15 M10 (Details `doc/mc80/hardware_2x.md`): **MAME `mc8020.cpp` und Entwurf 31 §3.2 sind falsch:** CTC F0–F3 und PIO F4–F7 gehören zur Displaylogik DPL, die ZRE-CTC/-PIO liegen auf 80H–87H (Anwender). MBI-Ports bestätigt (EFH Laufwerksregister, F8H–FBH CTC, FCH/FDH PIO A Byte-Schieberegister, FEH/FFH PIO B: Bit 7 Kassette eingelegt, 6 Bandende, 5 Schreibsperre, 4 Phase). Takt 2,4576 MHz belegt. **Bandformat 2x:** `AA`·Blocknummer (high zuerst)·128 Daten·CRC-16/ARC über Blocknummer+Daten (low zuerst)·`AA` = 86H Byte; Bandmarke `AA 00 00 AA`; Kennzeichnungsblock Blocknummer FFFFH; Bittakt vermutlich 1536 Bit/s [?]. **Original-Listing gilt nur bis 05B5H**, danach andere Fassung (KMBG-Menü, Taste „LAUF“); nur MAME-Satz **ver2** (s01+s02_v2+s03_v2+mo01) passt zum Handbuch. K5020-Modell für die MBI nicht verwendbar (nur Bandmedium wiederverwendbar). Zu schätzen [?]: DPL-Rücklaufzeiten, MBI-CTC-Verkettung, EFH-Werte, Zeichensatz U402 (kein Abzug), Programmiereinheit E0–E8. Nur im 43-MB-Original lesbar (nicht im Repo): Geräteadresstabelle, Magnetband-Adressen, Speichertabelle, Bilder. Fehlt: Schaltpläne DPL/MBI/Programmiereinheit, Abzug U402, ROM der KMBG-Fassung. Der volle Lauf vor dem Merge hatte einen Zeitüberlauf (`DiskVolume.JedesKatalogformatHatNormspurenMitEinerUmdrehung`, vermutlich Last).

---

## 4. Arbeitspaket-Karten — Kassette (Entwurf 29)

### K0 — K5020: Abschrift Programmierung und Signale
- **Ziel:** `doc/kassette/k5020.md` — vollständige, prüfbare Beschreibung der Karte.
- **Lesen:** `doc/original_docs/AKB_K5020_Betriebsdokumentation.pdf` (Scan; §II.2 Anschlussverzeichnis,
  §3 E/A-Schnittstellen, §4 Anschlusslogik, §5–8 Programmierung/Datenaus-/-eingabe/Steuerfunktionen),
  `…_Serviceschaltplaene.pdf`; Gegenprobe `doc/kassette/fex/LesenOhneKMBG.mac`, `kmbrev.mac`,
  `PIO-Initialisierung.txt`, `fexmain.mac`.
- **Liefern:** Portplan (zwei PIOs, A5120 30H–37H, K8915 D0H–D7H), jede Leitung mit Richtung und
  Bedeutung (Tabelle aus Entwurf 29 §4.1 bestätigen/berichtigen), Interruptquellen und -reihenfolge,
  wie der Bittakt entsteht (Teilerkette aus Bustakt, Umschaltung X3B06/X4B06 für den MC80.30),
  Präambel-/Bitraum-/Zeitüberwachung, Bedeutung von `BMS`, `AEB`, `STA`, `ZF`; Zeitwerte in µs.
- **Abnahme:** jede Zeile mit Quelle (Seite der BD bzw. Datei:Zeile); offene Punkte mit [?].

### K1 — Abgleich der vier K5020-Treiber und Bandformat
- **Ziel:** `doc/kassette/treiber.md` — was die Software auf das Band schreibt und wie sie liest.
- **Lesen:** K0; Lade-ROM `doc/EPROMS/zre.prn` 027E–03AF; `doc/kassette/fex/KMBG.REL` (Robotron,
  disassemblieren — `.REL` mit M80-Werkzeugen lesbar machen oder Bytes ab Code-Segment); BCU-Treiber
  auf `doc/a5120_alt/bcu/satz2_OhneSYL.1/disk.img` (Suchwort „kmbg“, „KASSETTE“); MC80-EGOS
  `doc/EPROMS/MC80/mc8030_mame/zve_3.rom`/`zve_4.rom` (Treiber über `OUT (C)`); K5261-Firmware
  `doc/EPROMS/K5261/` (dort F8H–FFH); FEX-Formate `doc/kassette/fex/Formate.txt`, `befehl.txt`;
  gelesene Blöcke `doc/kassette/fex/dirk2014/images/` (Statusbytes + Daten).
- **Liefern:** je Funktion (reservieren, rückspulen, Block vor/zurück, Bandmarke vor/zurück, Block
  lesen/schreiben, Bandmarke schreiben, löschen, entriegeln) die Signalfolge an Steuer-PIO A/B;
  Blockaufbau auf dem Band (Präambel, Daten, **von der CPU gerechnete CRC** — Polynom/Startwert,
  Postambel), Längen 4–255, Lückenlängen; Format „Basis-/Kompakt-/Ausbaustufe, FLX K1600“ als
  Blockfolgen; Bootband-Aufbau, den das Lade-ROM erwartet (Ladeadresse 0400H, Blockzahl, Kennungen).
- **Abnahme:** die vier Treiber widersprechen sich nirgends (oder der Widerspruch ist benannt).

### K2 — K5200: Laufwerkszeiten und Signale
- **Ziel:** `doc/kassette/k5200.md`.
- **Lesen:** `doc/original_docs/Kassettenmagnetbandgeraet_K5200_Betriebsdokumentation.pdf`,
  `…_Serviceschaltplaene.pdf`.
- **Liefern:** IFKB-Signale (Zuordnung zu den K5020-Leitungen), Anlauf-/Bremszeiten, Geschwindigkeiten
  (19/38 cm/s, Suchlauf), Bandlänge/Kapazität je Seite, Lage der Bandanfangs-/-endemarke, Verhalten
  bei Schreibschutz und ohne Kassette, Lese-nach-Schreiben.
- **Abnahme:** Zahlenwerte mit Seitenangabe; daraus die Konstanten für K3.

### K3 — `core/peripherals/kassette/`: Medium, Codec, Laufwerk
- **Ziel:** kartenunabhängiges Bandmodell (Entwurf 29 §5).
- **Liefern:** `KassettenMedium` (zwei Seiten; Folge aus Lücke/Block/Bandmarke; Position als
  Maschinenzeit-Bandlänge; Schreiben überschreibt ab Position, Löschen), `K52Codec` (`.k52` lesen/
  schreiben, verlustfrei, versioniert, Kopf mit Geschwindigkeit), `KassettenLaufwerk` mit
  `DriveProfile`-artigen Profilen **K5200-38**, **K5200-19**, **LW1200**, **PK3**, **SK1200**
  (Werte aus K2/M0/M10), Signale reserviert/ausgewählt/bereit/Seite/Schreibschutz/AEB/BMS, Byteakt
  im Bittakt; Bindung an eine Datei mit verzögertem Zurückschreiben (Vorbild
  `K6022::autoFlush`, 0,5 s Maschinenzeit).
- **Abnahme:** `tests/unit/peripherals/test_kassette_medium.cpp`, `test_k52_codec.cpp`,
  `test_kassetten_laufwerk.cpp` (Positionieren, Suchlauf mit BMS, AEB an beiden Enden, Seite wenden,
  Schreibschutz, Hin-und-zurück `.k52` bitgleich, Zurückschreiben nach Pause).

### K4 — `core/cards/k5020/`
- **Ziel:** die Karte nach K0/K1 auf zwei `Z80PIO`-Primitiven + Ablaufsteuerung.
- **Liefern:** `K5020` mit `Config{basis, takt = Bustakt|ExternerCtc}`; zwei Laufwerksanschlüsse;
  Interrupts in der Kette; `reset()` lässt Bänder liegen; kein Save-State.
- **Abnahme:** `tests/unit/cards/test_k5020.cpp` — **je Leitung aus K0 ein Fall** (RES/AWA/TRV/TRR/HGE/
  AUF/AEB/AZV/KSB/NIB/BMS/STA/ZF), Byteeingabe im Takt (6 und 12 kBit/s), Zeitfehler bei zu langsamem
  Schreiben, Daisy-Chain-Fälle.  Ein Testfall fährt die **Init-Folge des Lade-ROMs** (027E ff.) als
  Gastcode auf einer Minimalmaschine.

### K5 — Stecken in allen Maschinen, C-ABI, Python, Werkzeuge
- **Ziel:** wie `installK6022` (Merkposten Lochstreifen 1–5).
- **Liefern:** `K1520Machine::installK5020()` + `k5020Takt/Reset/AutoFlush` in A5120 (inkl. .16),
  K8915 (D0H), PRG 710 (30H, prüfen ob frei), PC 1715 (nur mechanisch, wie RAF/K6022), MC80 folgt in
  M7; C-ABI `k1520_tape_install/_installed/_load/_eject/_new/_side/_protect/_position/_error`
  (additiv, `K1520_API`), `app/core_binding/k1520.py`, `K1520Emulator(tape=True)`; `boot_trace`/
  `k1520dbg --tape <datei>`; Debugger-Befehl `tape` (Status, Position, Blockliste).
- **Abnahme:** `tests/integration/test_k5020_maschinen.cpp` (zu spät/doppelt/Tor belegt, Gastcode
  schreibt Block und liest ihn zurück je Maschine), `py_c_api`, `tests/python/test_tape_api.py`,
  Debugger-Smoke `tests/dbg/all_commands_smoke.dbg` ergänzt.

### K6 — CLI `k1520tape`
- **Ziel:** `tools/k1520tape.cpp` (+ `tools/k1520tape.md`): `.k52` anlegen, Blöcke/Bandmarken listen,
  Dateien als Blockfolge ein-/auslesen (Rohformat und FEX-Formate aus K1), **`bootband`**: erzeugt das
  vom Lade-ROM erwartete Band aus einer Binärdatei (Ladeadresse 0400H).
- **Abnahme:** `tests/cli/` Fall `cli_k1520tape` (Hin-und-zurück, Bootband-Aufbau gegen K1).

### K7 — A5120 bootet vom Band
- **Ziel:** ohne Diskette fällt das Lade-ROM auf die Kassette durch und startet ein Bandprogramm.
- **Liefern:** Testprogramm (`tools/kassette/bandtest/`, Quelle + eingechecktes `.bin`, Bau über
  `tools/cpm_bau.py`-Muster), das „KASSETTE OK“ in den Bildspeicher schreibt; Bootband per K6.
- **Abnahme:** `tests/integration/test_kassette_boot.cpp` `KassetteBoot.LadeRomLaedtVonBand` (A5120,
  keine Diskette, beide Laufwerke: LW1 leer → LW2).  Boot-Invarianten (CLAUDE.md) bleiben grün.

### K8 — SCPX mit `KMBG.REL`
- **Ziel:** Robotron-Kassettenbibliothek unter SCPX schreibt und liest einen Block im Emulator.
- **Liefern:** kleines Prüfprogramm aus `doc/kassette/fex/kasstest.mac` + `KMBG.REL`, gebaut mit
  M80/LINK (Muster `tools/cpm_bau.py`), auf eine SCPX-Systemdiskette (Kopie über `TempDisk`);
  Ablauf über die Tastatur (`typeString`).
- **Abnahme:** `tests/system/` `ScpxKmbg.SchreibtUndLiestEinenBlock` (langsam → `format_integration`).

### K9 — Oberfläche „Kassette“
- **Ziel:** wie Lochstreifen (Merkposten 12/13): *Einstellungen ▸ Allgemein ▸ Kassette (K5221, AKB
  K5020)*; Karteikarte „Kassette“ nur mit Karte, je Laufwerk: Kassette einlegen (`.k52`)/neu/auswerfen,
  Seite A/B, Schreibschutz, Position, Lampe; Konfiguration `general.tape`, Abschnitt `kassette:`;
  Handbuchkapitel; kein Tastenkürzel.
- **Abnahme:** `tests/python/test_kassette_gui.py` (Vorgabe aus in allen Programmen, Kasten folgt der
  Karte, Wiederherstellen aus Konfiguration, keine Kürzel), Kürzel-Wächter grün.

### K10 — Merkposten, CLAUDE.md, Handbuch
- **Ziel:** `doc/merkposten/kassette.md` (Festlegungen mit Wächtern, Format wie Lochstreifen),
  CLAUDE.md-Absatz „Kassette K5020/K5200“, Entwurf 29 auf „abgeschlossen“, README-Zeile.

### K11 — optional `.wav` (zurückgestellt)
- Erst wenn ein echter Bandmitschnitt vorliegt (Wunschliste).  Richtungstaktschrift ISO 3407 /
  KROS 5109, auch für die MC80.2x-MBI nutzbar.

## 5. Arbeitspaket-Karten — frühe A5120 (Entwurf 30)

### A0 — Lade-ROM-Fassungen
- **Lesen:** `doc/EPROMS/K2526/laderom_v06.bin` (+ Analyse des Anwenders `laderom_v06.mac`,
  `laderom_orirom.mac`), `laderom_v09_1.bin` (= MAME `a5120_v2`), `doc/EPROMS/zre.rom` (= V0.9_2 =
  MAME `a5120_v1`), `k8924_laderom_v0.9a.bin`; Werkzeuge `doc/merkposten/boot_debugging.md`.
- **Liefern:** `doc/a5120_alt/laderom.md`: Unterschiede je Fassung (Adresse, Bedeutung), welche
  Fassung BCU braucht („SYL auf Spur 1“ vs. „ohne SYL“), Empfehlung je Modell.
- **Abnahme:** Unterschiede vollständig (byteweiser Vergleich + Disassemblat).

### A1a / A1 — K7023
- **Lesen:** `doc/original_docs/ASS_K7023_Betriebsdokumentation.pdf`, `ASS_K7023_ServiceSchaltplaene.pdf`,
  `K7023_Serviceschaltung.pdf`, `ABS_K7023.pdf`; CP/A `bioscrt.mac` (in
  `doc/a5120_alt/cpa_quellen/CPA_890925_Quellen_A5120.zip`); Zeichengenerator `doc/EPROMS/K7023/`;
  vorhandene `core/cards/k7024/` als Vorlage.
- **A1a liefern:** `doc/a5120_alt/k7023.md` (Bildspeicheradresse/-größe — CP/A sagt BAB1 FC00H, 16×64,
  Cursor/Invers über Bit 7, Helligkeit .01, Lesesperre, Zeichengeneratoraufteilung BAB1-46/47).
- **A1 liefern:** `core/cards/k7023/` (Varianten `.00`/`.01`), Framebuffer/`screenChar` für C-ABI.
- **Abnahme:** `tests/unit/cards/test_k7023.cpp` (Adressierung, Bit 7, Helligkeit, Zeichensatz-Goldbild
  mit `tools/fb_ocr.py`-Ansatz).

### A2 — K3525
- **Lesen:** `doc/original_docs/Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf`,
  `core/cards/k3526/`.
- **Liefern:** 16-KB-dRAM-Karte mit Basisadresse (Brücken), vier gesteckt = 64 KB.
- **Abnahme:** `tests/unit/cards/test_k3525.cpp`.

### A3a / A3 — K7636 an der K2526
- **Lesen:** `doc/original_docs/K7634-36-Serviceschaltplaene.pdf` — **S. 2 Verzeichnis, S. 8 Belegungsplan
  K7636.01 (alle Beschriftungen + Matrixpositionen), Folgeseiten Codetabellen je Variante** (LOC =
  ROM-Adresse, a/b = ungeschaltet/geschaltet, • = Dauerfunktion; S. 9 ist mit „K 7636.03“
  unterschrieben — Zuordnung prüfen); `Tastaturen_K7632_34_35_36_Betriebsdokumentation.pdf`
  (Funktion, Wiederholzeiten ROM 154H/159H, Sl1–Sl6); Entwurf 30 §3.3;
  `doc/k8915g2/k7634.md` (K7634-Auswertung, gleiche Familie); CP/A `bioskbd.mac`/`bioskbdc.mac`
  (PIO-Tastatur: 06H Zeichen, 01H Status, 03H/05H Lampen, Typcode 80H); `core/cards/k2526/`
  (BS-PIO, Port 0AH).
- **A3a liefern:** `doc/a5120_alt/k7636.md` (Anschluss an die ZRE-PIO, Handschlag, Typcode) und
  `doc/a5120_alt/k7636_01_codes.csv` (Position, Beschriftung, Code a/b, Dauerfunktion — abgeschrieben aus
  Belegungsplan + Codetabelle, Vorbild `doc/prg710/k7609_codes.csv`).
- **A3 liefern:** `core/peripherals/k7636/` Verhaltensmodell + Anschluss an die K2526;
  Bildschirmtastatur auf Basis `app/ui/keyboard_k7634.py`.
- **Abnahme:** `tests/unit/peripherals/test_k7636.cpp`; CP/A-Typtest erkennt „K7636“ (in A6).

### A4a / A4 — K5120 (A4 zurückgestellt)
- **Lesen:** `doc/original_docs/AMF_K5120_Betriebsdokumentation.pdf`, `doc/EPROMS/K5120/`
  (Marken-ROM, Schreib-ROM), CP/A `biosdskt.mac`/`biosdskp.mac` (`fdc equ k5120`).
- **A4a liefern:** `doc/a5120_alt/k5120.md` mit Entscheidung „eigene Karte“ und Aufwandsschätzung.

### A5 — Ausführungen im Kern und in der Oberfläche
- **Liefern:** `A5120Machine::Config::ausfuehrung` = `Neu` (Vorgabe, bitgleich zu heute) | `Alt`
  (K7023.01, 4×K3525, K7636, K2526-Brücke Bit 6 = BAB1, K5122 mit 3× K5600.10) | `Kmbg` (wie Alt +
  K5020 fest, Laufwerke K5200) | `Acht` (K7023, K5602); C-ABI additiv
  (`k1520_create_a5120_ausfuehrung` o. ä.), Python `K1520Emulator(machine="a5120", ausfuehrung=…)`,
  Programmprofil `a5120emu`: `modelle` um „A5120 (64×16, K7636)“, „A5120 Kassette“ ergänzen;
  Bildschirmtastatur je Modell; Konfigurationsmigration (fehlend = Neu).
- **Abnahme:** `tests/integration/test_a5120_ausfuehrungen.cpp`, `py_c_api`,
  `tests/python/test_a5120_modelle.py`, alle bisherigen A5120-Tests unverändert grün.

### A6 — CP/A auf „5¼″ alt“
- **Ziel:** CP/A startet mit K7023 (16×64) und K7636 bis `A>`.
- **Wege:** (1) vorhandene CP/A-Diskette aus `disks/` unverändert (BIOS 25.09.89 schaltet über
  Port 0AH Bit 6); (2) falls die Diskette eine ältere BIOS-Fassung trägt: aus
  `doc/a5120_alt/cpa_quellen/` mit `crt`/`fdc`/`diskA = 10540` generieren (M80/LINKMT unter
  `cpm.exe`-Ersatz bzw. `zxcc`; Bauweg dokumentieren), als Fixture ablegen.
- **Abnahme:** `BootIntegrationAlt.CpaBab1BisPrompt` (Bildspeicher FC00H zeigt Banner + `A>`,
  `dir` über K7636).

### A7 — BCU 880
- **Lesen:** `doc/a5120_alt/bcu/*/DT-INFO.TXT` (bootfähig sind die „SYL auf Spur 1“-Disketten),
  A0, Entwurf 30 §3.2.
- **Liefern:** Fixtures (`.img` 26×128×40×1 → passendes Katalogformat in `data/formats.yaml`, ggf.
  neuer Eintrag mit Begründung; `.td0` nur Beleg), Modell „Alt“ mit gewähltem Lade-ROM; bei
  Fehlern Analyse wie in `doc/merkposten/boot_debugging.md`.
- **Abnahme:** `BootIntegrationBcu.V12BisPrompt` und `…V21BisPrompt`.

### A8 — Modell KMBG mit Kassette
- **Ziel:** BCU 880 formatiert die Kassette in Laufwerk F:, schreibt eine Datei, liest sie zurück.
- **Abnahme:** `tests/system/` `BcuKassette.FormatiertSchreibtLiest`.

### A9 — Oberfläche/Handbuch/Merkposten
- Handbuchkapitel „Frühe A5120“, `doc/merkposten/a5120_alt.md`, CLAUDE.md-Absatz, Entwurf 30
  abschließen.

## 6. Arbeitspaket-Karten — MC80 (Entwurf 31)

### M0 — MC80.3x: Hardware-Abschrift
- **Lesen:** `doc/original_docs/mc80/` (`mc80.3x-egos.pdf` §1–3 Speicher/Ports/Vektoren,
  `mc80.3x-bda.pdf` Bedienung, `kdt_hardware_mc80.30_1988.pdf` + `kdt_mc80.30_1989.pdf` Karten,
  `spe2-*.pdf`, `zve_bst_3o.pdf`, `pk-3-*.pdf`), MAME `mc8030.cpp` (im Entwurf 31 §3.1 ausgewertet).
- **Liefern:** `doc/mc80/hardware_3x.md`: Speicherkarte inkl. Segmentregister (ZVE-PIO A — Bitbelegung),
  Schreibschutz 8CH, VIS2a-Register C0H–CFH (Bildspeicherorganisation 512×256, Schreib-/Leseweg,
  Kaskade), ASP (SIO/CTC/PIO-Belegung, Tastaturprotokoll, Summer), EPROM-Programmierer E0H–EFH,
  Interruptkette, Takt (2,5 vs. 2,4576 MHz klären), AKB/AKM-Anschluss.
- **Abnahme:** jede Angabe mit Quelle; Widersprüche zu MAME benannt.

### M1 — EGOS-Disassemblat
- **Lesen:** `doc/EPROMS/MC80/mc8030_mame/zve_1…4.rom`, `spe_1/2.rom`; Alternativfassungen
  `doc/EPROMS/MC80/mc80.30e-*.bin`; HEX-Listing `doc/original_docs/mc80/mc8.3x-bsys-3.0_hex-0000-277f.pdf`,
  `mc80.3x-sw-0aff.pdf`; Werkzeuge `doc/merkposten/boot_debugging.md` (Disassembler, `.prn`-Annotation).
- **Liefern:** `doc/mc80/egos.prn` (0000H–27FFH, kommentiert: Reset, Init, VIS-Treiber,
  Tastatur, Kommandointerpreter, Magnetband 1000H–1FFFH, Segmentumschaltung), Fassungsvergleich
  MAME ↔ ycdt.
- **Abnahme:** Einsprungpunkte aus EGOS §8 (Softwareschnittstellen) wiedergefunden.

### M2 — `Mc80Machine`-Gerüst
- **Liefern:** `core/machines/mc80/` (`Variante` .30/.31/.33/.21/.22), `core/cards/mc80/zve3x`,
  `spe` (SPE1/SPE2, Segmentfenster 4000H–BFFFH, EPROM-Segmente), Schreibschutz; C-ABI
  `K1520_MACHINE_MC80 = 6` + `k1520_create_mc80(variante, …)`; ROMs als `*_data.h`.
- **Abnahme:** `tests/unit/cards/test_mc80_zve.cpp`, `test_mc80_spe.cpp` (Segmentumschaltung,
  Schreibschutz), `py_c_api`.

### M3 — VIS2a, ASP, Tastatur → EGOS meldet sich
- **Liefern:** `core/cards/mc80/vis2a` (Bitmap 512×256, Ebene; Zeichen über die EGOS-Treiber),
  `asp` (SIO/CTC/PIO), `core/peripherals/mc80_tastatur/` (ASCII über SIO, Summer); C-ABI für ein
  **Bitmap-Bild** (neu: `k1520_screen_bitmap`-artig, additiv) und `k1520_screen_char` über eine
  Textrückgewinnung, falls möglich.
- **Abnahme:** `tests/integration/test_mc80_boot.cpp` `Mc80Boot.EgosMeldetSich` (Text im Bild per
  Mustervergleich), Kommandos `CATA`/`INIT` über die Tastatur.

### M4 — Programmsegmente 10H/20H
- **Lesen:** `doc/EPROMS/MC80/segmente/seg_*.bin`, Entwurf 31 §4 (MC80.31E: FT2–FT5).
- **Abnahme:** `Mc80Segmente.SondertastenLadenEditorUndBasic`.

### M5 — Speicherabzug laden, Spiele
- **Liefern:** C-ABI `k1520_mem_load(h, adr, datei)` (allgemein, additiv) + Debugger `load`;
  Wächter mit `doc/mc80/spiele/` (Startbefehle in Entwurf 31 §4).
- **Abnahme:** `Mc80Spiele.*` (je Spiel: geladen, gestartet, Bild ändert sich, keine Endlosschleife
  im ROM).

### M6 — Programm `mc80emu`
- **Liefern:** Programmprofil in `app/profil.py` (Titel „MC80 Emulator“, `mc80emu.yaml`,
  `data/default_config_mc80.yaml`, Modelle .30/.31/.33, .21/.22 gesperrt bis M12), Bildfläche
  512×256 über den CRT-Shader, Bildschirmtastatur (Layout aus `mc80.3x-bda.pdf`), Starter
  `run_mc80emu.sh`, Paket (`packaging/`: `MASCHINEN`, Inno `{#Programm6}`, `.desktop`), Werkzeugmenü,
  Handbuchkapitel; Merkposten `doc/merkposten/paketierung.md` beachten.
- **Abnahme:** `tests/python/test_mc80emu_gui.py`, `py_packaging`, `py_programme`, Kürzel-Wächter.

### M7 — Kassette am MC80
- **Liefern:** K5020 fest 30H am .31 (Bustakt) und .30 (Takt aus ASP-CTC K2 über Koppelbus, Profil
  LW1200); AKM + PK3 am .33 (Analyse aus `pk-3-*.pdf` + `mc80.33-1000.bin` als Teil des AP).
- **Abnahme:** `Mc80Kassette.SpeichernUndLaden` je Modell (EGOS-Kommando, RAM-Kettung mit Kennbyte).

### M8 — Werkzeuge
- `boot_trace`/`k1520dbg --machine mc80[-30|-31|-33]` (Segmentanzeige in `map`, VIS-Bild als Text
  wo möglich); `tools/k1520dbg.md` §, `tools/boot_trace.md` §.

### M9 — Merkposten MC80, CLAUDE.md, Handbuch
- `doc/merkposten/mc80.md`, CLAUDE.md-Abschnitt „Sechste Maschine: MC80“, README.

### M10 — MC80.2x: Hardware und ROMs
- **Lesen:** MC80.2x-Handbuch — Textebene (OCR, fehlerhaft) im Repo `doc/mc80/mc80_2x_handbuch_ocr.txt`,
  Bilder/Schaltungen nur im Original (43 MB, tiffe.de `Robotron/MC80.2X/MC80.2x-Handbuch.pdf`), `doc/original_docs/mc80/
  mc80_22-bsys-doku.pdf`, `mc80_22-up-doku.pdf`; ROMs `doc/EPROMS/MC80/mc8020/` (s02/s03 greifen auf
  F8H–FFH und EFH zu — vermutlich MBI), MAME `mc8020.cpp`.
- **Liefern:** `doc/mc80/hardware_2x.md` (DPL/BSA-Bild 8×32 + Analogkurve, Tastaturmatrix, MBI-Ports
  und Ablauf, PPE E0H–E8H), `doc/mc80/bsys_2x.prn`.

### M11 / M12 — MC80.21/.22 im Kern und in `mc80emu`
- Kern mit `core/cards/k2521` + `k3820` + K3525 (A2) + DPL/BSA/MBI; Bild „Oszilloskop“ (eigene
  Darstellung), Kassette über `peripherals/kassette` (Profil SK1200/LW1200).  Wächter
  `Mc80_2x.BsysMeldetSich`, `Mc80_2x.KassetteSpeichernLaden`; Modelle in `mc80emu` freischalten.
