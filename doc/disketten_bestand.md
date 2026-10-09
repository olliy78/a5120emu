# Disketten-Bestand: was es gibt, was ausgeliefert wird, was die Tests brauchen

Stand 2026-10-09 (Abschnitt 6: die Konsolidierung ist umgesetzt). Ziel (Anwender): **je Maschine × Betriebssystem eine Diskette in der
Auslieferung**; zusätzliche Disketten nur als Testdaten im Repo, nicht im Installer.

Drei Orte, nicht verwechseln:

| Ort | Rolle | Im Installer? |
|-----|-------|---------------|
| `disks/` | Arbeitsverzeichnis, Quelle der Auslieferung | nur die 17 in `DISKS_DEFAULT` (`packaging/build_payload.sh`), nur `.hfe` |
| `tests/fixtures/disks/` | unveränderliche Testdaten | nein |
| `tests/fixtures/cpm/`, `…/raf/` | Prüfprogramme, die der Test auf eine Temp-Kopie schreibt | nein |

`disks/` und `tests/fixtures/disks/` sind **getrennte Dateien**, auch bei gleichem Namen:
die `disks/`-Fassungen der CP/A-Disketten tragen zusätzlich die Beigaben (SERTEST, ROMREAD,
EM256*, RAF*, LBREAD/LBPUNCH — 32 statt 24 Dateien, `tools/disketten_beigaben.py`). Die
`@OS.COM` darauf ist **byte-gleich** (geprüft per md5). Die Tests laufen also nie gegen
die ausgelieferte Fassung, nur gegen deren Betriebssystem.

## 1. CP/A-Versionen im Einsatz

**A5120 (alle `cpa_cpa780_*`): genau eine Fassung.** BIOS `CP/A, Version 25.09.89`
(`verst/versm/versj` = 25/09/89), CCP `11.01.89`, Bootsystem `05.04.88`
(`bootsec_cpa780.bin` ist auf allen Disketten byte-gleich), CPU-Takt 2,5 MHz. Die
Unterschiede der Disketten stecken **nur in der Übersetzung des BIOS** (`@OS.COM`):

| Variante | `@OS.COM` | md5 (8) | Laufwerke A/B/C | Uhr | Besonderheit |
|----------|-----------|---------|-----------------|-----|--------------|
| `clock` | 14 464 B | 5e0a8857 | K5601 ×3 | ja (`uhrvar=1`, fragt beim Start) | — |
| `noclock` | 14 208 B | bb8c2e0d | K5601 ×3 | nein | — |
| `combo5zoll` | 14 464 B | 0591720d | K5601 / K5600.10 (`10540`) / K5600.20 (`10580`) | nein | — |
| `combo8zoll` | 14 464 B | 7a30fbbb | K5601 / MF3200 (`00877`) / K5602.10·MF6400 (`10877`) | nein | — |
| `noclock-raf` | 15 616 B | feaf37cd | K5601 ×3 | nein | `raf=1`, `rafpar=1`: M: auf der RAF (88H); **ohne Karte kein M:** |
| `noclock-em256` | 15 232 B | da8fa9e0 | K5601 ×3 | nein | `em256=1` (A5120.16); nur als Fixture |

Die Listings (`tests/fixtures/disks/cpa_cpa780_*.prn`) existieren für die ersten fünf; `-em256` hat keins.
Eine Uhr-Variante der Fremdlaufwerke gibt es nicht — die Kombination Laufwerke × Uhr ist
nicht lückenlos gebaut (kein `combo5zoll_clock`).

**Andere CP/A-Abkömmlinge** (eigene Maschine, eigene Fassung — nicht Teil der Konsolidierung
oben): `CP/A 1715` auf `pc1715_cpa1715_boot_4lw` (BIOS 24.05.88, CCP 06.03.87); die
Workbench-Fassung `pc1715_cpa1715_workbench` (nur Fixture) trägt Zeichenfolgen beider
Stände (24.05.88 und 25.09.89).

## 2. `disks/` — Bestand und Auslieferung

„Ausl." = in `DISKS_DEFAULT`. „Tests" = nutzt ein Test **diese Datei in `disks/`** (nein: die
Tests nehmen ihre Kopie unter `tests/fixtures/disks/`, s. §3).

| Datei | Maschine · System | Ausl. | Bemerkung |
|-------|-------------------|:----:|-----------|
| `cpa_cpa780_k5601_clock` `.hfe/.img/.prn` | A5120 · CP/A mit Uhr | ✔ (`.hfe`) | |
| `cpa_cpa780_k5601_noclock` | A5120 · CP/A | ✔ | |
| `cpa_cpa780_combo5zoll_noclock` | A5120 · CP/A, B:/C: 5¼″ SS | ✔ | Matrix-Fixture (§3) |
| `cpa_cpa780_combo8zoll_noclock` | A5120 · CP/A, B:/C: 8″ | ✔ | Matrix-Fixture (§3) |
| `cpa_cpa780_k5601_noclock-raf` | A5120 · CP/A + RAF-Treiber | — | nur im Arbeitsverzeichnis; Handbuch/CLAUDE.md verweisen darauf |
| `scpx17_cpa780_k5601.hfe` | A5120 · SCPX 1.7 (16×256) | ✔ | |
| `a5120_udos43_k5601_entwickler.hfe` | A5120 · UDOS 4.3 | ✔ | |
| `scpx17_5x1024_k5601_hardy.hfe` | A5120 · SCPX 1.7 (5×1024) mit HARDY | — | |
| `k8915scpx_boot1.hfe` | K8915 · SCPX 8915 | ✔ | |
| `prg710_udos43_k5601_system.hfe` · `prg710-1_udos43_k5601_v43_189.hfe` | PRG 710 / 710-1 · UDOS 4.3 | ✔ ✔ | |
| `prg710_scpx15_cpa640_sysprg.hfe` · `prg710-1_scpx17_cpa640_boot.hfe` | PRG 710 / 710-1 · SCPX | ✔ ✔ | |
| `pc1715_scp1715_v0006_boot` · `…_v0007_cpa640_boot` | PC 1715 · SCP 1715 (zwei Fassungen) | ✔ ✔ | |
| `pc1715_cpa1715_boot_4lw` · `pc1715_cpz22_boot` | PC 1715 · CP/A 1715 · CP/Z 2.2 | ✔ ✔ | |
| `udos1715_640k_pc1715_system.hfe` | PC 1715 · UDOS1715/NDOS | ✔ | |
| `pc1715w_scp30_system.hfe` | PC 1715W · SCP 3.0 | ✔ | |
| `boot_*.bin`, `bootsec_cpa780.bin` | Systemspuren zum Wiedereinspielen | — | `boot_*` Werkzeugvorrat; `bootsec_cpa780.bin` zusätzlich Testvergleichswert |

Ausgeliefert sind damit **17 Disketten**; davon vier für CP/A am A5120 — die einzige Stelle,
an der es mehr als eine Diskette je Maschine × System gibt (SCP 1715 am PC 1715 hat zwei
*Fassungen* V0006/V0007, die sich im Format unterscheiden; das ist eine andere Frage).

## 3. `tests/fixtures/disks/` und die Format-Matrix

Die **Format-Matrix** (`format_matrix`, 88 Tests, `tools/dev.sh test-matrix`) benutzt
genau **drei** Disketten, jeweils die `.img` aus `tests/fixtures/disks/`
(`tests/system/drivers/format_all.py::BOOT_DISKS`):

| Boot-Diskette | Matrix-Fälle | Zweck |
|---------------|-------------:|-------|
| `cpa_cpa780_k5601_clock` | 47 | FORMAT.COM-Menü des K5601 (§3) und die Geometrien S/T/U/V/W (§3.4), Ziel B: |
| `cpa_cpa780_combo5zoll_noclock` | 24 | K5600.10 (B:) 16 Formate, K5600.20 (C:) 8 |
| `cpa_cpa780_combo8zoll_noclock` | 17 | MF3200 (B:) 9, MF6400 (C:) 8 |

`make_bootdisk.py` (Kette Leerdiskette → FORMAT → CPABCGEN → Kaltstart, `test-format`) nimmt
dieselben drei. `format_all.py` kennt außerdem `noclk` (`noclock.img`); die Matrix nutzt es
nicht. Entscheidend für die Matrix ist **nicht die Diskette, sondern die `@OS.COM`**: sie legt
über `diskB`/`diskC` fest, welche Laufwerkstypen FORMAT.COM anbietet.

Übrige Fixtures und ihre Nutzer (Auszug der wichtigsten; die vollständige Zuordnung mit
Begründung steht in `tests/fixtures/README.md`):

| Fixture | Nutzer |
|---------|--------|
| `cpa_cpa780_k5601_clock` `.img/.hfe` | `test_boot_integration` (Hauptfixture), ~80 Dateien (CLI-, Debugger-, DiskTool-Fälle), Matrix, `make_bootdisk` |
| `cpa_cpa780_k5601_noclock` `.img/.hfe` | Boot von B:/C:, `.img` vs. `.hfe`, EM256-/RAF-Prüfprogramme (`Em256Adr`, `Em16Abl`, `RafZwg`), DiskTool-Rundläufe, `test_physical_boot`, ~40 CLI-Fälle |
| `cpa_cpa780_k5601_noclock-em256.img` | `Em256RamFloppy`, `test_raf_a5120`, `test_kbd_nach_reset`, `bt_em` |
| `cpa_cpa780_k5601_noclock-raf.img` | `RafCpaBios*` |
| `cpa_mini` | synthetisch, Codec-/Geometrietests |
| `scpx17_cpa780_k5601`, `scpx17_5x1024_k5601_hardy_norm` | `ScpxIntegration`, `ScpxInit`, `test_hardy`, DiskTool |
| `udos_boot_scp`, `udos_ds77_k5601_fremdsync`, `mixed_udos_ss40_over_cpa800` | UDOS-/Codec-/Fremdsync-Tests |
| `k8915scpx_*` (3) | `K8915Scpx.*`, `test_k8915_format` |
| `prg710*` (7), `pc1715*` (8), `pc1715w_scp30_system`, `udos1715_*`, `udosP8000_*`, `scp1700_*` | je Maschine/Dateisystem, s. README |

## 4. Mehrere `@OS.COM` auf einer CP/A-Diskette — geprüft

Idee: eine ausgelieferte CP/A-Diskette, die als K5601 ×3 bootet (`@OS.COM`), und daneben
weitere BIOS-Fassungen als gewöhnliche Dateien, die der Anwender vom `A>` aus startet.

**Es funktioniert.** Versuch 2026-10-08 (`k1520dbg`, Kopie von `noclock.img`, die
`@OS.COM` der anderen Varianten unter anderem Namen daraufgelegt):

| Start von | Befehl | Ergebnis |
|-----------|--------|----------|
| `noclock` | `OS588` (= `combo5zoll`) | Neustart, Banner `A:5"(80,DD,DS)/B:5"(40,DD,SS)/C:5"(80,DD,SS)`, `A>` |
| `noclock` | `OSC` (= `clock`) | Neustart, fragt `Bitte Uhrzeit eingeben!`, Uhr läuft (12:00:44 …) |
| `clock` (gerade gestartet) | `OS588` | Neustart auf `combo5zoll` |
| `clock` | `^C` (Warmstart) | Variante bleibt, Uhr läuft weiter |

Gründe, warum es trägt: Die `@OS.COM` ist eine normale `.COM`, die ihr BIOS komplett neu
aufbaut (Neustart mit RAM-Test und Laufwerkserkennung); und `wbootv = 0` in **allen**
Fassungen — die CCP-Kopie steckt im BIOS, ein Warmstart liest nichts von der Diskette
nach und kippt die gewählte Variante nicht zurück.

Was dabei zu beachten ist:

1. **Der Unterstrich geht nicht.** `OS_588` wurde mit `OS_588?` abgewiesen (getippt über
   die emulierte Tastatur; ob das CCP das Zeichen ablehnt oder die Tastenzuordnung, ist
   nicht getrennt untersucht). Namen ohne `_`, z. B. `OS588`, `OS555C`. `@` im Namen wäre
   dagegen kein Problem (`@OS.COM` selbst), ist aber verwirrend neben der Bootdatei.
2. **`@OS.COM` bleibt die erste Datei und ist die Bootvariante.** Sie ist die einzige, die der
   Lader findet (`@OS     COM`, Block 3). Vorschlag: dort die Variante, die am wenigsten
   fragt und am meisten kann — `noclock-raf` (ohne Karte ohne Wirkung, belegt aber 1,4 KB
   mehr; **nicht getestet**, ob jede Anwenderhardware damit zurechtkommt).
3. **Die Variante ändert nur B:/C:/Uhr/RAF.** A: bleibt K5601 — das ist Voraussetzung
   dafür, dass die Diskette überhaupt bootet. Der Anwender braucht also ein K5601 als A:,
   wie vorgeschlagen; beliebige Rechner mit anderen A:-Laufwerken deckt das nicht ab.
4. **Systemdiskette „auf Basis einer Variante" erzeugen** ist **nicht geprüft.** `CPABCGEN`
   nimmt laut `tools/bootsec/README.md` §500 ein Quellsystem als zweiten Parameter
   (`CPABCGEN B:  OS588.COM`), das spricht dafür. Aber die Zieldiskette wird im Laufwerk
   der **gestarteten** Variante formatiert: unter `OS588` ist B: ein 5¼″-SS-40-Laufwerk, unter
   einer 8″-Variante B: ein MF3200 — eine K5601-Systemdiskette entsteht dann nur in A:
   (oder in einem B:, das selbst K5601 ist). Das ist ein Gastverhalten, kein Emulatorproblem;
   es sollte im Handbuch stehen.
5. **Die Matrix bleibt unberührt.** Ihre Fixtures sind eigene Dateien; die ausgelieferte Diskette
   kann beliebig umgebaut werden. Neu wäre allenfalls ein Test, der die ausgelieferte
   Diskette bootet und die Varianten nachstartet (Banner prüfen) — das ist das, was der
   Versuch oben von Hand gemacht hat.
6. **Speicher ist kein Thema:** sechs Varianten ≈ 90 KB, die Diskette hat 480 KB frei.

## 5. Vorschlag für die Auslieferung

Vier CP/A-Disketten → **eine** (`cpa_cpa780_k5601_system.hfe`), mit

- `@OS.COM` = Bootvariante (siehe 4.2),
- `OS555`, `OS555C` (drei 5¼″, ohne/mit Uhr), `OS588`, `OS588C` (K5601 + zwei 8″), `OSC`
  (K5601 ×3 mit Uhr) — Benennung nach Ihrem Schema, nur ohne `_`; die Ziffer = Zoll von A/B/C,
  `C` = Uhr,
- Beigaben wie bisher, plus eine `LIESMICH.TXT`, die die Varianten erklärt.

**Das braucht neue Übersetzungen:** `clock` gibt es nur mit K5601 ×3, die Combo-Disketten
nur ohne Uhr. `OS555C`/`OS588C` müssen also aus den BIOS-Quellen neu gebaut werden
(Schalter `uhrvar` und `diskB`/`diskC`; Quelle `~/projects/CPA_Workbench/src/bc_a5120/bios_org.mac`, Bau wie in
`tests/fixtures/README.md` bei `-raf`/`-em256` beschrieben). Ohne diese Neubauten kann man die vier vorhandenen Varianten
(`clock`, `noclock`, `combo5zoll`, `combo8zoll`) sofort bündeln.

Nebenbei gefunden (inzwischen entfernt: `leer.hfe`, `leer_scp.*`, `unbekannt_daten_b.img`); `-raf` in `disks/` ist die einzige nicht
ausgelieferte Diskette, die das Handbuch/CLAUDE.md als Anwenderfunktion beschreibt (→
würde mit 4.2 entfallen); `pc1715_cpa1715_workbench` und `…_v0006_pctest` sind
Fixtures ohne Gegenstück in `disks/`.

## 6. Umgesetzt (2026-10-09): `cpa_cpa780_k5601_system.hfe`

Gebaut von `tools/cpa_a5120/build.py` (Wächter `cli_cpa_a5120`), im Installer **statt** der vier
CP/A-Einzeldisketten (`DISKS_DEFAULT`). Die Einzeldisketten bleiben als Arbeitsstand in `disks/`.

- `@OS.COM` = mit Uhr, 3 × K5601. Daneben `OSNOCLK`, `OSCOMB5`, `OSCOMB8`, `OSRAF`, `OSEM256`
  (Namen ohne Unterstrich, s. §4.1; beschrieben in `LIESMICH.TXT` auf der Diskette).
- Systemprogramme wie bisher plus `STAT.COM` (aus der SCPX-1.7-Diskette; unter CP/A geprüft:
  `STAT`, `STAT *.COM`, `STAT DSK:` laufen).
- Eigene Prüfprogramme: SERTEST, ROMREAD, EM256ADR/EM16ABL/EM256FUL (+ EM256TST), RAFCPM/RAF512,
  **neu** RAFTEST (Kartenadresse auf 88H/89H gepatcht) und RAFQUICK, LBREAD/LBPUNCH.
- **Erweitert (2026-10-09, zweiter Schritt):** WordMaster mit **deutscher** `WM.HLP` (Original russisch in
  KOI-7-Umschrift; unter `tools/cpa_a5120/quellen/`), Textprozessor **TP 3.0** (CP/A-Fassung) mit
  `TPOVLY1/TPHT/TPDRUCK.OVR` und Installer `TPINSCPA`, `BASIC.COM`, Turbo Pascal (`PASCAL.COM` + `PASINST`).
  Herkunft und Gründe für das Weggelassene: `tools/cpa_a5120/README.md`.
- **52 Dateien, 194 KB frei** (von 780 KB).

Nachgewiesen im Emulator: Kaltstart mit Uhrabfrage; `OSCOMB8` vom `A>` startet das 8″-BIOS
(Banner `B:8"(77,SD,SS)/C:8"(77,DD,SS)`); **`CPABCGEN B: OSCOMB5.COM`** legt auf einer Leerdiskette
eine Systemdiskette an (`@OS.COM` byte-gleich `OSCOMB5`), die allein in A: bootet und das
5¼″-Banner `B:5"(40,DD,SS)/C:5"(80,DD,SS)` zeigt. Damit ist §4.4 (offen) geklärt.

Nicht umgesetzt: Varianten mit Uhr und Fremdlaufwerken (`OS555C`, `OS588C`) — brauchen
BIOS-Neuübersetzungen. Nicht verändert: die Test-Fixtures, die Matrix und `tools/disketten_beigaben.py`
(pflegt weiter die Einzeldisketten).

**Aufgeräumt (2026-10-09):** Die Einzeldisketten `cpa_cpa780_{k5601_clock,k5601_noclock,k5601_noclock-raf,combo5zoll_noclock,combo8zoll_noclock}.{hfe,img}`
sind aus `disks/` entfernt; es bleibt nur `cpa_cpa780_k5601_system.hfe` (die `.prn`-Listings liegen seit dem Folgecommit bei den Fixtures). Die Tests brauchten sie nicht
(eigene Kopien unter `tests/fixtures/disks/`). Handbeispiele in den Dokumenten zeigen jetzt auf die Fixtures.

`leer.hfe`, `leer_scp.hfe/.dmk` und `unbekannt_daten_b.img` sind ebenfalls aus `disks/` entfernt (von keinem Test aus `disks/` gelesen; die Tests legen `leer.hfe` selbst in ein Temp-Verzeichnis). `doc/analyse_format_leerspur.md` beschreibt `leer_scp.*` als Messquelle von damals.
