# 30 — Frühe A5120-Ausführungen: „5¼″ alt“, „KMBG“, „8″“ (Planung)

Stand: 2026-10-13 — **Planung, nichts umgesetzt; Entscheidungen des Anwenders vom 2026-10-13
eingearbeitet (§2).**  Kassette selbst: Entwurf 29.  Legende wie dort.

---

## 1. Ziel

Der Emulator kennt heute nur den **A5120 „5¼″ neu“** (ab 1985: K2526, K3526 64 KB, K7024
80×24, K8025, K5122, K7637) und den A5120.16.  Dazu kommen die älteren Ausführungen als
**Modelle desselben Programms** (`general.model`, wie V3/V2 beim K8915):

| Modell | Massenspeicher | RAM | Bild | Tastatur | Systeme |
|---|---|---|---|---|---|
| **A5120 5¼″ alt** | 3× K5600.10 (40 Sp., 1-seitig) an **K5120** — später oft K5122/K5601 | 4× K3525 (je 16 KB) bzw. 64-KB-Karte | **K7023.01** 16×64, Monitor **K7221** | **K7636** parallel an der K2526 | **CP/A** (BAB1, §3.1), **BCU 880**, SIOS |
| **A5120 KMBG** | 2× K5200 eingebaut an **K5020** (+ optional Floppy) | wie „alt“ | K7023 16×64 | K7636 | SIOS, **BCU 880** (Kassette = F:) |
| A5120 8″ | K5602 (+2 extern) | [?] | K7023 | K7636 | CP/A (8″-Laufwerkstypen), BCU (8″-Disketten [Web]) |
| A5120 5¼″ neu (vorhanden) | 3× K5601 / K5122 | K3526 | K7024 24×80 | K7637 | CP/A, SCP, UDOS, … |

Kartenreihe „5¼″ alt“ [Web]: K3525 · **K5120** · K8025 · K2526 · **K7023.01** · K3525 · K3525 · K3525.

**Gerät des Anwenders** (Kassettenausführung, erworben 2021, Fotos
`~/projects/robotron/sonst/A5120Kassette/Bilder/`): Monitor **K7221.11, Baujahr 1984**; Karten auf
den Fotos CIMG2916–2924 (Zuordnung vorläufig [?]): Bildschirmkarte mit zwei Zeichengenerator-EPROMs
(K7023), ZRE mit zwei U880 (K2526), RAM-Karte mit 32 dRAM-Bausteinen (062-8600…), Karte mit SIO/CTC
und vier Ausgangssteckern (K8025), Karte mit drei/vier PIO-großen Bausteinen (K5020 oder K5120).

## 2. Entscheidungen (Anwender, 2026-10-13)

| # | Frage | Entscheidung |
|---|---|---|
| E1 | Reihenfolge | **„5¼″ alt“ zuerst** (K7023.01, 4×K3525, K7636; Floppy zunächst K5122+K5600.10), dann **KMBG**, 8″ zuletzt |
| E2 | K5120 | später als eigene Karte (AP-A4); „5¼″ alt“ ist mit K5122 historisch belegt (Umrüstung [Web]) |
| E3 | Ausgegraut? | **nein** — es gibt passende Software (§3), die Modelle werden normal angeboten |

## 3. Software — selbst beantwortet

### 3.1 CP/A kann den kleinen Bildschirm schon [SRC]

Das CP/A-BIOS vom 25.09.89 (`doc/a5120_alt/cpa_quellen/CPA_890925_Quellen_A5120.zip`: `bios.mac`, `bioscrt.mac`
„Bildschirm-Ausgabe K7023, K7024, DSY5“) unterstützt **beide** Bildschirme **ohne Neuübersetzung**:

```
bsanf2 equ 0F800h  ; BAB2 24×80 (K7024)        bsanf1 equ bsanf2+400h ; BAB1 16×64 (K7023) ab FC00H
; Waehrend des Kaltstarts wird Port 0ah, Bit 6 getestet (Wickelbruecke auf ZE-Karte)
; und hierueber BAB1 und BAB2/3 unterschieden.
```

und erkennt die **Tastatur K7604/06/34/36/37 beim Kaltstart** (PIO-Tastatur an der ZE: Zeichen 06H,
Status 01H, Lampen 03H/05H).  Auch **K5120** ist als `fdc`-Variante enthalten (`biosdskt.mac`,
`biosdskp.mac`) und Laufwerkstyp **10540 = K5600.10** (`diskA` …).  Unser K2526 bildet die
Wickelbrücken B5–B7 von Port 0AH bereits ab (`K2526::A5120Config::config_bridges`,
`buildPortBInputs`) — für „alt“ wird nur Bit 6 umgestellt.

**Folge:** Die vorhandene CP/A-Systemdiskette läuft voraussichtlich unverändert auf dem alten Modell
(Prüfung AP-A6, erster Schritt).  Falls nicht (andere BIOS-Fassung auf der Diskette): CP/A aus den
Quellen mit `crt`/`fdc`/`diskA` neu generieren (`make_@OS.cmd` im Archiv, M80/LINKMT) und
als Fixture ablegen.

**Ergebnis AP A6 (2026-10-15):** bestätigt — Weg 2 war nicht nötig.  Die vorhandenen Disketten
tragen das BIOS 25.09.89; es liest die Brücke (BAB1), meldet „Tastatur: K7636“ und kommt ohne
Kernänderung bis `A>`, `dir` über die K7636 geht.  Geprüft (a) mit
`cpa_cpa780_k5601_noclock.hfe` an auf K5601 umgerüsteten Laufwerken und (b) an der Vorgabe
3× K5600.10 mit einer 40-Spur-Systemdiskette aus `make_bootdisk --preset k5600_10_fmt1`
(`@OS.COM` = `combo5zoll`, meldet A: als K5601 — die automatische Formaterkennung liest die
einseitige 40-Spur-Diskette trotzdem).  Ein BIOS mit `diskA = 10540` (Banner „A:5"(40,DD,SS)“)
gibt es damit noch nicht; es bliebe Weg 2.  Wächter `BootIntegrationAlt.*`,
`cli_bt_ausfuehrung_alt`, `cli_dbg_ausfuehrung_*`; Werkzeuge `boot_trace`/`k1520dbg
--ausfuehrung neu|alt|kmbg [--laufwerke P0,…]`.

### 3.2 BCU 880 — Originalsystem der Frühzeit, liegt vor [Disk]

`doc/a5120_alt/bcu/` (Herkunft VEB Halbleiterwerk Frankfurt/Oder):
BCU 880 V1.2 und 2.1, „EPROM BCU 880 VERSION 2.2“, ASSEMBLER FZW/BL V5.1, PIP, WordStar 3.0; Format
26×128×40×1 (K5600.10), Fassungen „ohne SYL“ und „SYL auf Spur 1“; **mit KMBG-Treiber**.  Laut
[Web] brauchte BCU „einen besonderen Lade-EPROM“ — Kandidaten liegen vor: Lade-ROM **V0.6** und
**V0.9_1** (`doc/EPROMS/K2526/laderom_v06.bin`, `laderom_v09_1.bin` = MAME `a5120_v2`; Analyse des
Anwenders `doc/EPROMS/K2526/laderom_v06.mac`, `laderom_orirom.mac`).  BCU ist damit **die** Abnahme für KMBG und K7023.

### 3.3 Tastatur K7636 — Beschriftung und Codes liegen vor [SLP]

`doc/original_docs/K7634-36-Serviceschaltplaene.pdf` (2. Auflage 1987, Verzeichnis S. 2):
**S. 8 = Belegungsplan „Tastatur robotron K 7636.01“** (1.49.001458.5/00) mit **jeder
Tastenbeschriftung und Matrixposition** (Reihen E/D/C/B/A, Spalten 00–59, Funktionsreihe
F51–F59: CI, M, Ein/Aus, LEDs „rt“/„gn“); Folgeseiten je Variante (.04 lat./kyr., .05 deutsch
ESS, .07 Jugoslawien, .03 Ungarn) mit **Codetabelle**: je Tastenposition die ROM-Adresse (`LOC`,
1F0H–2FFH, CTAB1/CTAB2) und der Code ungeschaltet/geschaltet, „•“ = Dauerfunktion.  Die
Codetabelle ist damit der Tabellenteil des Tastatur-ROMs — ein ROM-Abzug ist **nicht** nötig.
Achtung: die Tabelle auf S. 9 ist mit „K 7636.03“ unterschrieben; welche Tabelle zur .01 gehört,
klärt AP-A3a (Seitenzuordnung prüfen).

Fotos zur Gegenprobe (Farben, Tastenformen): Wikimedia Commons „Eastern German Robotron
(3495380310).jpg“ (3888×2592, K7636 am A5120; Buchstaben, Ziffernblock, S1–S9 lesbar, rote/beige
Funktionstasten nicht), „VCFe 8.0 (476448034).jpg“, „Vcfe2007 img 5132.jpg“.  Die
Commons-Serie „Robotron A 5120 Bild 0x“ zeigt dagegen eine **K7637** (Funktionstastenreihe).

### 3.4 SIOS

Nicht vorhanden (Forumsgesuch 13657 offen; die mit „SIOS“ beschrifteten Disketten im BCU-Satz
enthalten BCU).  Bleibt auf der Wunschliste, blockiert nichts.

## 4. Bausteine

| Baustein | Unterlagen | Abzüge | Aufwand |
|---|---|---|---|
| **K7023 / .01** (ABS 16×64, BAB1, Bildspeicher FC00H–FFFFH) | tiffe.de `K1520/K7023/` BD, SLP, Techn. Beschreibung | **Zeichengenerator BAB1-46-2 / BAB1-47-1** (`doc/EPROMS/K7023/`, = MAME `bab46/47_lat`) | mittel — Ableitung von `K7024` (VRAM-Basis/-Größe, Zeilen/Spalten, Helligkeitsbit .01) |
| **K3525** (OPS 16 KB) | tiffe.de `K1520/K3820/…K3525…` BD + SLP | — | klein |
| **K7636** an K2526 | `doc/original_docs/Tastaturen_K7632_34_35_36_Betriebsdokumentation.pdf` (Funktion, Zeiten), **`K7634-36-Serviceschaltplaene.pdf` S. 8 Belegungsplan K7636.01 + Codetabellen** (§3.3); Auswertung K7634 `doc/k8915g2/k7634.md` | ROM nicht nötig — **Verhaltensmodell** mit der Codetabelle | mittel |
| **K5120** (AMF) | `doc/original_docs/AMF_K5120_Betriebsdokumentation.pdf` | **Marken-ROM + Schreib-ROM** (`doc/EPROMS/K5120/`); Referenztreiber CP/A `biosdskt/p.mac` | groß (später) |
| K5600.10 | — | — | vorhandenes `DriveProfile` |
| K5020 + K5200 | Entwurf 29 | — | Entwurf 29 |

## 5. Arbeitspakete

Maßgeblich ist **`doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md`** §5: **A0** Lade-ROM-Fassungen ·
**A1a/A1** K7023 · **A2** K3525 · **A3a/A3** K7636 · **A4a/A4** K5120 (A4 zurückgestellt) ·
**A5** Ausführungen in Kern/C-ABI/Profil · **A6** CP/A auf „alt“ · **A7** BCU 880 · **A8** KMBG mit
Kassette · **A9** Oberfläche/Handbuch/Merkposten.

Reihenfolge: A0 → A1/A2 → A3 → A5 → A6 → A7; A8 nach K5; A4 zuletzt.

## 6. Rest-Wünsche (nicht blockierend)

- SIOS1526 (Diskette oder Kassette).
- Fotos/Etiketten der Karten im eigenen KMBG-Gerät zur sicheren Zuordnung, Stellung der
  Wickelbrücke Port 0AH Bit 6 [Gerät].
