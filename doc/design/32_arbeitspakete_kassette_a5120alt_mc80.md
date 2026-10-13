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
| K1 | Abgleich der vier K5020-Treiber, Bandformat | ana | K0 | L | offen |
| K2 | K5200: Laufwerkszeiten und Signale | ana | — | S | **fertig 2026-10-14** |
| K3 | `peripherals/kassette`: Medium, `.k52`, Laufwerk | cpp | K1, K2 | M | offen |
| K4 | `cards/k5020` | cpp | K3 | M | offen |
| K5 | Stecken in allen Maschinen, C-ABI, Python, Werkzeuge | cpp/py | K4 | M | offen |
| K6 | CLI `k1520tape` (+ Bootband) | cpp | K3 | S | offen |
| K7 | A5120 bootet vom Band | int | K5, K6 | M | offen |
| K8 | SCPX + `KMBG.REL`: Block schreiben/lesen | int | K5 | M | offen |
| K9 | Oberfläche „Kassette“ | py | K5 | M | offen |
| K10 | Merkposten Kassette, CLAUDE.md, Handbuch | doc | K7–K9 | S | offen |
| K11 | optional `.wav` (ISO 3407) | cpp | K3 + Mitschnitt | M | zurückgestellt |
| A0 | Lade-ROM-Fassungen V0.6/V0.9 vergleichen | ana | — | S | **fertig 2026-10-14** |
| A1a | K7023: Abschrift BD/SLP | ana | — | S | **fertig 2026-10-14** |
| A1 | `cards/k7023` | cpp | A1a | M | offen |
| A2 | K3525 (16 KB) | cpp | — | S | offen |
| A3a | K7636 + Anschluss an K2526: Abschrift | ana | — | M | **fertig 2026-10-14** |
| A3 | K7636-Modell + K2526-Anschluss | cpp | A3a | M | offen |
| A4a | K5120: Analyse (BD, 2 ROMs, CP/A-Treiber) | ana | — | L | offen |
| A4 | `cards/k5120` | cpp | A4a | L | zurückgestellt |
| A5 | `A5120Machine::Config::ausfuehrung`, C-ABI, Profil-Modelle | cpp/py | A1, A2, A3 | M | offen |
| A6 | CP/A auf „5¼″ alt“ bis Prompt | int | A5 | M | offen |
| A7 | BCU 880 bis Prompt | int | A5, A0 | L | offen |
| A8 | Modell KMBG: BCU mit Kassette F: | int | A7, K5 | M | offen |
| A9 | Oberfläche/Handbuch/Merkposten A5120 alt | py/doc | A6 | S | offen |
| M0 | MC80.3x: Hardware-Abschrift | ana | — | M | **fertig 2026-10-14** |
| M1 | EGOS-Disassemblat (`.prn`) | ana | — | L | offen |
| M2 | `Mc80Machine` Gerüst: ZVE, SPE, Segmente | cpp | M0 | M | offen |
| M3 | VIS2a + ASP + Tastatur → EGOS meldet sich | cpp/int | M2, M1 | L | offen |
| M4 | MC80.31E-Programmsegmente 10H/20H | int | M3 | S | offen |
| M5 | Speicherabzug laden + Spiele-Wächter | cpp/int | M3 | S | offen |
| M6 | Programm `mc80emu` + Paket | py | M3 | L | offen |
| M7 | Kassette an MC80.31/.30/.33 | cpp/int | M3, K4 | L | offen |
| M8 | `boot_trace`/`k1520dbg --machine mc80` | cpp | M3 | M | offen |
| M9 | Merkposten MC80, CLAUDE.md, Handbuch | doc | M6 | S | offen |
| M10 | MC80.2x: Hardware aus Handbuch + ROM-Disassemblat | ana | — | L | offen |
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
