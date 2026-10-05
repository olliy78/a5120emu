# 24 — K8915-Varianten: „Generation 2“ (Gerät des Anwenders) und „Generation 1“ (Planung)

Stand: 2026-10-05, Zweig `K8915Varianten` (von `main` @ 5190c9c). **Planungsdokument** —
noch kein Kerncode. **Abzüge der Gen-2-ZRE, der K7024 und der 2708-Karte liegen vor**
(`doc/EPROMS/K8915G2/`, §10 am Ende); Auswertung = AP-V3. Der Emulator bildet bisher nur die **V3** ab (`doc/design/16_k8915.md`,
ZRE 045-8762 mit 128 KB, `core/machines/k8915/`).

Legende wie in Entwurf 16: **[ROM]** aus einem Abzug, **[SLP]** Stromlaufplan, **[Web]**
Sekundärquelle, **[Gerät]** am Original des Anwenders, **[?]** Vermutung/offen.

---

## 1. Ziel und Abgrenzung

Zwei weitere K8915-Ausführungen als Varianten derselben Maschine (wie PRG 710 / 710-1):

- **„Generation 2“** — liegt als Gerät beim Anwender vor. Bestückung laut Anwender:
  ZRE **wie im PRG 710 (K2521-Familie), aber mit 3 EPROMs**, eine **zusätzliche RAM-Karte**
  und eine **Karte mit 16 × 2708 (je 1 KB)**. Die 2708-Karte hält der Anwender nicht für
  Standardbestückung. EPROM-Abzüge liefert der Anwender nach.
- **„Generation 1“** — kein Gerät, **kein Abzug**. **Angabe des Anwenders (2026-10-05):** Gen 1
  hat eine **parallele Tastatur K7634** und eine **Schnittstellenkarte K7028** (statt der
  045-8732 der Gen 2/V3); **der Rest entspricht scheinbar der Gen 2** (ZRE, RAM, K7024, K5122).
  Gen 1 ist damit **keine eigene Maschine, sondern Gen 2 mit anderer Tastatur und anderer
  Schnittstellenkarte** — der Aufwand sinkt erheblich (§6).

**Nicht Ziel:** Magnetkassetten-Version (KOKOS/KOBRA, „ausgestorben“ [Web]), Harddisk-Version.

---

## 2. Begriffsklärung — offene Zuordnung (Frage F1, blockiert die Namensgebung)

Entwurf 16 §2 und robotrontechnik.de kennen **sechs** Ausführungen, keine „Generationen“:

| robotrontechnik.de [Web] | ZRE / RAM | Tastatur | OS |
|---|---|---|---|
| Magnetkassette | — | — | KOKOS, KOBRA |
| 8″ | K2521, K3528 (64 KB), K5121 | K7634 (PIO) | SCPX 0/2, BIOS G3.2 |
| **5¼″ V1** | K2521, K3528 (64 KB), K5122 | K7634 (PIO) | SCPX 0/2, BIOS 4.x |
| **5¼″ V2** | K2521, K3528, 045-8778 (ABS 2K), K5122, 045-8732 | K7672 (flach) | SCPX 8915 5.x/8.x/9.x, CP/Z 2.2 |
| **5¼″ V3** (im Emulator) | 045-8762 (ZRE + 120/128 KB), 045-8732 | K7672 | SCPX 8915 |
| Harddisk | 045-8767, K5122, 045-8762, 045-8732, 045-8778 | K7672 | SCPX 8915 V0/2, BIOS 8.3 |

Die Bestückung des Anwendergeräts (K2521-artige ZRE + eigene RAM-Karte) passt zu **V1 oder
V2** der Tabelle, nicht zu V3. **[?] Zu klären:** Ist „Generation 2“ = „5¼″ V2“ und
„Generation 1“ = „5¼″ V1“ (oder die 8″-Version)? Dann gilt: Gen 1 = K7634/PIO-Tastatur,
Gen 2 = K7672. Der Anwender liest am Gerät ab: Tastaturtyp, Kartenbezeichnungen
(Platinen-Nr. 012-/045-…), Stecker-Belegung der Steckplätze, Laufwerke.

---

## 3. Quellenlage

### 3.1 Gefunden (alle bereits ausgewertet)

| Quelle | Inhalt | Nutzen |
|---|---|---|
| [tiffe.de K1520/K2521/](https://www.tiffe.de/Robotron/K1520/K2521/) `K2521_Beschreibung.pdf` | Kartenbeschreibung: **3 × U555 (2708) bei 0000–0BFFH, 1 KB RAM 0C00–0FFFH**, CTC 80H–83H, PIO 84H–87H, /MEMDI-Brücken X8–X9, Takt 9,8304 MHz : 4 | Hardware der Gen-2-ZRE; bereits im Kern (`core/cards/k2521/`, PRG 710) |
| [tiffe.de K1520/K3528/](https://www.tiffe.de/Robotron/K1520/K3528/) `OPS_K3528-1/-2.pdf` | **Scans 168/154 MB** der 64-KB-RAM-Karte | Adressierung/Brücken der RAM-Karte — **noch nicht gelesen** (AP-V1) |
| [tiffe.de K1520/K7634_36/](https://www.tiffe.de/Robotron/K1520/K7634_36/) `K7634-36.pdf` | Parallele Tastatur K7634/36 (5,1 MB) | Tastatur Gen 1 (Anwenderangabe: K7634 + K7028) — **noch nicht gelesen** (AP-V1b) |
| [tiffe.de CPM-KRZ/](https://www.tiffe.de/Robotron/CPM-KRZ/) | CP/M-3-Quellen (BIOS2710, FL, KM, LFLO…), Lader, `ccp.bin` | **Nur für K2521 + K5122 + K3528/K3526 + K7634**, also Gen-1-nahe Hardware; Quelle für Speicherumschaltung und Boot [?]; Teile (KMB/Kassette) irrelevant |
| Forum [CP/M auf K2521/K5122](https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=12101) | System aus K2521, K3526 64K, K3525 16K, K7028, K5122, K7024; Speicherumschaltung über **PIO Port B → /MEMDI** | Bestätigt, dass die K2521 die Bankumschaltung per PIO macht |
| Forum [SCP auf K2521 + AMF im Wait-Betrieb](https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=5713) | ROM „V.09“ (1 KB) lädt Bootspuren nach 0400H; /MEMDI1+2 aus PIO in 8 Bitmustern; genannte Ports: PIO **08–0FH**, CTC 80–83H, ATS E0–FFH, AMF 10–2FH | **Widerspruch zur Kartenbeschreibung (PIO 84H–87H)** — vor Verwendung klären (AP-V1/V3) |

### 3.2 Nicht gefunden

- **Kein EPROM-Abzug einer Gen-1-/V1-/V2-ZRE.** Auf tiffe.de liegt unter `K8915/Eprominhalte/`
  nur `k8915.bin` = `k8915-ZVE_2732.bin` = **V3-Boot-ROM** (beide MD5 `19e301bd…`, identisch mit
  `doc/EPROMS/K8915/k8915_boot_2732.bin`) und `c10_char.bin` (Zeichensatz, 8 KB, MD5
  `723eaf3f…`), dazu `EPROMs_ABS/`. Keine Treffer in Suchmaschinen, Foren, classic-computing.
- **Keine Systemdiskette SCPX 0/2 BIOS 4.x oder G3.2.** Forum erwähnt nur private Angebote
  („Rolly2 bietet SCP-Bootdisketten an“; „Rüdiger bot SCP1526/710 mit Boot-ROM an“) — **nicht
  verifiziert, nicht heruntergeladen**.
- **Folgerung:** Gen 1 ist ohne Fremdhilfe **nicht bootfähig nachbildbar**. Das PRG-710-ROM
  (`doc/EPROMS/PRG710/`, 1 KB-Lader, ebenfalls K2521) ist ein Nachbar, aber **kein** K8915-ROM.
  Nächster Schritt dafür: Anfrage an die Foren (robotrontechnik.de, VzEkC) und an das
  Rechenwerk Halle, wo laut Web eine V1 läuft [Web].

---

## 4. Was der Kern schon hat (Wiederverwendung)

| Baustein | Stand | Gen 2 / Gen 1 |
|---|---|---|
| `K2521` (`core/cards/k2521/`) | U880, ROM bis 3 × 1 KB, 1 KB RAM, CTC 80H, PIO 84H, Brücken X10/X14; Speicher über **externen Speicherweg** (kein `registerMem`) | **Kernstück beider Varianten**; Speicherverwaltung fehlt (PRG: `prg710_speicher`, E8H–EBH) |
| `K7024`, `K5122` (`/WAIT`-Zweig), `Laufwerke` | wie V3 | unverändert; K5122 ist im PRG 710 mit K2521 schon im Betrieb |
| `K7028` (ATS, 2×SIO+2×CTC) | V3 = 045-8732 | V2 hat sie auch (045-8732); V1 nennt „K7028“ (K7028.10/.20?) [?] |
| `k7672` (Tastatur) | V3 | V2 ja; **V1: K7634 (PIO)** — neu |
| ZRE 045-8762 (`zre8762`) | V3 | **nicht** Teil von Gen 1/2 |
| `prg710_speicher` | Seitenregister E8H–EBH | Vorlage für die RAM-Karte, **nicht** übertragbar (andere Platine) [?] |

Maschinenform: wie PRG 710 eine Klasse mit `Variante`-Wahl (`Prg710Machine`) **oder** Erweiterung
von `K8915Machine` um `Config::generation` — Entscheidung in AP-V2 (Empfehlung: **eine Klasse
`K8915Machine`, `Config::generation {V3, Gen2, Gen1}`**, damit Oberfläche, Debugger, Paket und
Starter `k8915emu` unverändert bleiben und `Config::variante` das Modell wählt, wie beim A5120
die `modellwahl`).

---

## 5. Gen 2 — Bestand und Unbekannte

### 5.1 Gesichert (K2521-Beschreibung)

ROM 3 × U555 0000–0BFFH (steckbar), RAM 0C00–0FFFH, CTC 80–83H, PIO 84–87H, Interruptkette
CTC vor PIO, Koppelbus-Brücken X10/X11, IEI-Wahl X14/X15. Das ist die schon modellierte Karte.

### 5.2 Unbekannt (alles [?], bis Abzüge/Gerätebefund da sind)

1. **Inhalt der drei EPROMs** — liegt vor (§10): Urlader mit Selbsttest und Floppy-Boot, **kein**
   CCP. Offen bleibt die genaue Hardware-Zuordnung der Ports.
2. **Die RAM-Karte:** K3528 (64 KB) oder K3526 + K3525? Adressierung, /MEMDI-Nutzung,
   Bankumschaltung. Der Anwender nennt eine einzelne „extra RAM-Karte“; bei 64 KB ergäben
   ZRE-RAM (1 KB) + Karte kein vollständiges 64-KB-System ohne Umschaltung der ZRE-ROM.
3. **Die 2708-Karte (16 × 1 KB = 16 KB EPROM):** ist die **PFS K3820** (Platine 012-7040/-7041,
   Handbuch `Speichersteckeinheiten_K3520…K3820_Betriebsdokumentation.pdf` §6, Chips Abb. 7,
   Startadresse über X8/X9 in 4-KB-Schritten, MEMDI über X6/X7). Inhalt: **nur 2 von 16 Chips
   programmiert** (§10) — eine abweichende Fassung des Urladers. Der Anwender hält sie nicht
   für Standard — deshalb **optional** modellieren (wie RAF/K6022: steckbare Karte, Vorgabe aus),
   und nur, wenn AP-V3 zeigt, dass der Gast sie benutzt.
4. **Speicherumschaltung** der ZRE per PIO-Port B (→ /MEMDI, /MEMDI1, /MEMDI2) und welche
   Bitmuster welche Karte freigeben (Forum nennt 8 Muster [Web]); die Wickelbrücken-Stellung
   X8–X9 am Gerät ablesen.
5. **Tastatur und Bildschirmkarte** der Gen 2 (K7634 oder K7672; K7024 oder 045-8778 ABS 2K).
6. **Laufwerke/Controller:** K5122 im `/WAIT`-Betrieb wie V3 [?]; Drive-Typen.

### 5.3 Auslesen der 2708 — Hinweise für den Anwender

U555/2708 brauchen **+12 V, +5 V, −5 V** — viele Programmer (TL866) lesen sie **nicht**
(der eigene Dump der V3 war ein 2732). Der Weg läuft deshalb über den **EPROMmer des PRG 710**
(`doc/prg710/eprommer.md`) oder einen Adapter mit −5 V/+12 V. Je Chip: zweimal lesen, MD5
vergleichen, Beschriftung/Bestückungsplatz (D-Nummer) festhalten. **Vorschlag für die
Ablage:** `doc/EPROMS/K8915G2/<karte>_<platz>_<aufschrift>.bin` + `README.md` (Muster:
`doc/EPROMS/PC1715/README.md`), und die ZRE-Bausteine **einzeln** nach Platz 0000/0400/0800.

---

## 6. Gen 1 — Bestand und Unbekannte

Belegt: **Anwender (2026-10-05):** Gen 1 = Gen 2, aber **parallele Tastatur K7634** und
**Schnittstellenkarte K7028**; [Web] zusätzlich: K2521 + K3528 (64 KB) + K7024 + K5122, SCPX 0/2
BIOS 4.x, zwei 5¼″-Laufwerke extern. Daraus folgt:

**Was Gen 1 von Gen 2 unterscheidet (und nur das):**
1. **Tastatur K7634** (parallel, an eine PIO) statt K7672 (seriell an SIO der K7028.30/045-8732).
   Neu im Kern: `core/peripherals/` K7634 (Vorbild `k7672`, `k7637`), AP-V1b liefert die Spezifikation.
2. **K7028 statt 045-8732** *(AP-V1b 2026-10-05: Fassung **.10, Ports `E0H–FFH`**, Tastatur an `E0H–E2H`, siehe `doc/k8915g2/k7634.md` §4)*: welche K7028-Fassung (.10/.20 mit 2 × SIO + 2 × CTC + PIO?) und
   was sich gegenüber der im Kern vorhandenen `K7028` (= 045-8732, K7028.30) ändert — Ports,
   PIO-Anschluss für die Tastatur, Bestückung, Baudraten-Taktung. **Noch nicht belegt [?]**:
   Unterlagen im Forum erwähnt (K7028-Servicedokumentation, siehe §3.1), nicht beschafft.
3. **Das ROM** ist die offene Größe: Wenn das Gen-2-ROM (175–177) beide Tastaturen beherrscht
   (Selbsttest „KEY“ → V3b prüft, **welche** Tastatur er abfragt), läuft Gen 1 mit **demselben
   ROM** und braucht keinen eigenen Abzug. Sonst fehlt ein Gen-1-ROM weiterhin (§3.2).
4. **System/Diskette:** SCPX 0/2 BIOS 4.x ist **nicht** vorhanden. Das Gen-2-System
   (`Disk on A: ready`-Urlader) lädt wohl dasselbe Bootformat — V3b prüft das; eine Gen-1-Diskette
   braucht es für den Beweis des Betriebs trotzdem.

**Empfehlung (geändert):** Gen 1 **nach** Gen 2 als **Konfigurationsvariante** (`generation`
= Gen1: Tastatur K7634 + K7028-Fassung) einbauen, **wenn** V1b die Tastatur und V3b die ROM-Frage
beantworten. Ohne ROM-/Diskettenbeleg bleibt der Betriebsnachweis offen, **die Maschine selbst
ist aber lauffähig (Selbsttest, Tastaturtest, Meldung)**. Kein ROM nachzuerfinden.

---

## 7. Arbeitspakete

Jedes AP ist für die Umsetzung durch **einen Agenten** geschnitten (Vorgabe des Anwenders,
Merkposten `feedback_ap_koordination_agenten`): Ziel, Eingaben, Ergebnis, Wächter,
Abhängigkeiten, Modell. **[Anwender]** = Schritt am Gerät, nicht delegierbar.

**Nachtrag 2026-10-05 (AP-V2):** V4, V6a, V6b, V7a, V7b sind jetzt vollständige Aufträge
(Abschnitt „AP-V2 — Ergebnis“); V5 entfällt, V8 gestrichen, V9 zurückgestellt. Der folgende
Absatz beschreibt den Stand davor.

**Ehrlicher Stand der Schärfe:** Nur **V1a, V1b, V3a, V3b** sind heute vollständig
spezifiziert — sie hängen an vorhandenen Unterlagen. **V2 und alle Kern-APs (V4–V9)** hängen am
Ergebnis von V3b (Portbelegung, Speicherumschaltung der Gen-2-ZRE) und an Anwenderantworten
(F1–F4); ihre Zeilen unten sind ein **Rahmen**, den V2 zu vollständigen Aufträgen schärft. Wer
V4 ff. vorher startet, rät.

### Übersicht

| AP | Inhalt | hängt ab von | Agent / Modell | Baut? |
|----|--------|--------------|----------------|-------|
| V0 | ✔ Planungsdokument, Quellensuche, Abzüge abgelegt | — | — | — |
| **V1a** | Unterlagen RAM-Karte K3528 + K2521-Schaltplan + K3820 lesen → `doc/k8915g2/karten.md` | — | general-purpose / Sonnet (Bilder lesen) | nein |
| **V1b** | Unterlagen Tastatur K7634/36 lesen → `doc/k8915g2/k7634.md` | — | general-purpose / Sonnet | nein |
| **V3a** | Kommentierte Listings der fünf Abzüge + Wächter | — | boot-disasm-analyst / Sonnet | nur `dev.sh test` |
| **V3b** | Analyse: Portkarte, Speicherumschaltung, Bootablauf, Hardware-Liste → `doc/k8915g2/zre_rom.md` | V3a | boot-disasm-analyst / **Opus** | nein |
| **V2** | ✔ Entwurf Maschinenform + Auftragsschärfung V4–V9 („AP-V2 — Ergebnis“) | V1a, V1b, V3b | Plan / Opus | nein |
| **V4** | Karte `K3528` + `K2521::Config::k8915g2()` + ROM 175–177 + `forK8915Gen2()` | V2 | cpp-coder / Sonnet | ja |
| V5 | **entfällt** — Beigabe, durch V3a erledigt | — | — | — |
| **V6a** | `K8915Machine::Config::generation`, Gen 2 bis Kaltstartmeldung + Diskette 901 | V4 | **Opus** | ja |
| **V6b** | `boot_trace`/`k1520dbg --machine k8915-g2` | V6a | cpp-coder / Sonnet | ja |
| **V7a** | C-ABI `k1520_create_k8915`, Python `machine="k8915-g2"` | V6a | cpp-coder / Sonnet | ja |
| **V7b** | Programmprofil (Modellwahl), Handbuch | V7a | Sonnet | ja |
| V8 | **gestrichen** (Ladekopf/Format wie V3); wieder öffnen nur bei F4 | — | — | — |
| V9 | **zurückgestellt**: Gen 1 ohne 0400-Lader nicht startfähig (F11) | F11 / F5 / F8 | — | — |
| V10 | Merkposten, `CLAUDE.md`, Verweis in 16 | alle | Sonnet | nein |
| VT | Alle vier Lanes vor dem Merge | V10 | test-runner / Haiku | ja |

**Parallel:** V1a, V1b, V3a laufen gefahrlos nebeneinander (nur lesen/Python); V3b nach V3a.
Alles mit „Baut? ja" **nacheinander** oder je Agent mit `isolation: "worktree"` (CLAUDE.md,
Bau-Kollision `build/`).

### Gemeinsame Regeln für alle APs (jeder Agent liest das zuerst)

- **Vorher lesen:** `CLAUDE.md` (Bauen/Testen nur über `tools/dev.sh`, knappe Testausgabe,
  Läufe > 60 s im Hintergrund), dieses Dokument komplett, `doc/EPROMS/K8915G2/README.md`,
  `doc/merkposten/k8915.md` und `doc/merkposten/prg710.md` (K2521 + `/WAIT`-K5122 gelten
  sinngemäß), `doc/design/16_k8915.md` §3–§4 (V3-Hardware als Vergleich).
- **Nicht anfassen:** A5120-, V3-K8915-, PRG-710- und PC-1715-Pfade. Änderungen an gemeinsamen
  Bausteinen (`K5122`, `K7024`, `K7028`, `K2521`, `K1520Bus`) nur über **neue Konfiguration mit
  unveränderter Vorgabe**; die acht Boot-Invarianten und alle bestehenden Wächter bleiben grün.
  Die Vorgabe von `K8915Machine` bleibt **V3**.
- **Fertig heißt:** `tools/dev.sh test` grün (voll, nicht nur neue Fälle; nie ein Binary aus
  `build*/` von Hand); neue Wächter über `k1520_add_test()`; **dieses Dokument nachgetragen**
  (Zeile im AP-Abschnitt: „erledigt JJJJ-MM-TT“, Befunde, Abweichungen vom Plan, neue offene
  Fragen in §9); **ein Commit** auf `K8915Varianten` (nicht pushen) mit dem Attributions-Trailer.
  Dateien des Anwenders im Arbeitsbaum (`app/ui/serial_widget.py`, `disks/a5120_cpm.hfe`,
  `doc/PRG710/`) **nicht** committen: nur die eigenen Dateien mit `git add <Pfad>`.
- **Befunde sind Belege:** jede Port-/Bit-/Adressdeutung mit Fundstelle (Datei + Adresse im
  Abzug, Seite in der Unterlage). Was Hypothese ist, trägt **[?]**, was aus dem Abzug folgt
  **[ROM]**, aus dem Schaltplan **[SLP]**, aus einer Fremdquelle **[Web]**.
- **Quellen:** Abzüge nur lesend (`doc/EPROMS/K8915G2/`, MD5SUMS prüfen). Unterlagen von
  tiffe.de dürfen heruntergeladen werden, **nur in das Scratchpad**, nicht ins Repo (die
  K3528-Scans haben 150+ MB). Nichts in Foren posten, keine Anfragen an Dritte.
- Kommentare, Logtexte, Doku **deutsch**, Stil wie `core/cards/zre8762/`, `core/cards/k2521/`.
- **Stoppregel:** Fehlt eine Anwenderangabe (F1–F4), arbeitet der Agent mit der im AP genannten
  Annahme weiter, kennzeichnet sie **[?]** und trägt die Frage in §9 ein — er hält nicht an, es
  sei denn, das AP nennt es als Blocker.

### AP-V1a — Karten lesen: K3528 (RAM), K2521-Schaltplan, K3820

**Ziel:** Wissen über die Karten, die V2/V4 brauchen, ohne Raten.
**Agent:** general-purpose (Sonnet). **Abhängig:** —. **Baut:** nein.

Eingaben (alles Scans ohne Textebene, kein OCR installiert: Seiten mit `pdftoppm -r 110 -png`
rendern und als Bild lesen; große PDFs seitenweise, Inhaltsverzeichnis zuerst):
- `https://www.tiffe.de/Robotron/K1520/K3528/OPS_K3528-1.pdf` und `-2.pdf` (168/154 MB).
- `https://www.tiffe.de/Robotron/K1520/K2521/` — `K2521_sch/Schaltplan_1..3.jpg`,
  `K2521_Schaltplan_1..3.jpg`, `BSPlan_CPU.jpg` (Belegungsplan, Brücken X6–X15).
- `https://www.tiffe.de/Robotron/K1520/K3820/Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf`
  — §6 (PFS K3820) **Seiten 22–26 sind gelesen** (siehe unten), der Rest (§3 K3520, §5 K3525,
  Programmierfelder, Wait-Bildung) ist für die RAM-Frage lesenswert.
- Bereits gesichert: K3820 = 16 × 1 KB (Q260), Startadresse über `X8:1–4/X9:1–4` in 4-KB-Schritten,
  MEMDI über `X6/X7` (MEMDI = X1:B09, MEMDI1 = X2:A21, MEMDI2 = X2:B21), WAIT-Bildung über
  `X10–X11` (offen = WAIT im M1-Zyklus), Chip-Raster Abb. 7. Das **nicht** erneut lesen,
  nur in `karten.md` übernehmen.

Ergebnis `doc/k8915g2/karten.md` mit je Karte: Größe, Organisation, Adressdekodierung, Brücken
(mit Tabelle der Stellungen), /MEMDI-Verhalten, WAIT, Speicherumschaltung falls vorhanden, und
die **Antwort auf: welche dieser Karten kann zusammen mit einer ZRE, die per A8H umschaltet,
ein 64-KB-System bilden?** Außerdem: **Widerspruch PIO 84H–87H (K2521-Beschreibung) ↔ 08H–0FH
(Forum 5713)** entscheiden anhand des Schaltplans der K2521 (Adressdekoder).
**Wächter:** keiner (Doku). **Fertig, wenn:** jede Aussage eine Seite/Blatt-Fundstelle hat und
offene Punkte als Fragen in §9 stehen.

**Erledigt 2026-10-05** → `doc/k8915g2/karten.md`. Befunde:
- **PIO der K2521 = 84H–87H, CTC 80H–83H** (Text S.6/S.10, Dekoder A34 = 8205 im Schaltplan Blatt 2:
  AB7 = 1, AB6/AB5 = 0, AB4:2 wählt; nur Ausgang 00/01 belegt, 02–07 offen). **08H–0FH aus dem Forum
  ist auf der K2521 nicht erreichbar.** Die Gen-2-ROMs sprechen weder 84H–87H noch 08H–0FH
  unmittelbar an (statisch, V3b prüft).
- **Die K2521 schaltet nicht selbst um** (kein Register); ROM/RAM 0000–0FFF werden über /MEMDI/1/2
  (X8/X9) von außen gesperrt.
- **Die K3528 ist die Umschaltkarte**: 64/48/32 KB dynamisches RAM (32 Chips „Q281“, 4 Banken je
  16 KB nach AB14/AB15), mit eigenem **8212-Register (OUT)**, das **/MEMDI, /MEMDI1, /MEMDI2, /MEMDI3**
  treibt und eine programmierbare 1-KB-Ausblendadresse hat. Register-Adresse über zwei 8205-Dekoder
  (AB2–4, AB5–7, /IORQ, /IODI) per Wickelfeld X3 wählbar: im Plan handverdrahtet auf **88H–8BH**;
  **A8H** (Wert des ROM) ist derselbe Dekoder mit einem Draht weiter (D2:02-Ausgang 05 statt 04)
  [?, F20]. Welches Registerbit welche /MEMDI-Leitung treibt: im Plan nicht eindeutig [?].
- **64-KB-Antwort:** nur **K2521 + K3528 (64 KB) (+ K3820)** bildet das A8H-umgeschaltete 64-KB-System;
  K3520/K3525/K3521/K3820 haben kein Register (Raster 4 KB, nur über X6/X7 von fremd erzeugtem MEMDI1/2
  gesperrt).
- K3820: Daten wie gesichert; Abb. 7 der Doku ist in der vierten Spalte verdruckt (gemeint 3C00/2C00/1C00/0C00),
  WAIT X10–X11 offen = M1-WAIT bestätigt; Hypothese „Start C000“ (Chip 3C00 = FC00) passt zur Brückentabelle.
- Lücken: `K3520_K3820_Heft2.pdf` ist keine PDF (218 B); K3820-Serviceschaltpläne nicht gelesen; Leiterbahn
  A34 → CS von CTC/PIO nicht verfolgt; Brücken-Polarität der Ausblendadresse unklar. Zu K3528 gibt es **keinen
  Text**, nur den (handschriftlich ergänzten) Plan.

### AP-V1b — Tastatur K7634/36

**Ziel:** Anschluss und Protokoll der parallelen Tastatur K7634 (Gen 1; **Anwenderangabe:
Gen 1 = K7634 + K7028**) **und** der K7028-Schnittstellenkarte, an der sie hängt.
**Agent:** general-purpose (Sonnet). **Abhängig:** —. **Baut:** nein.

Eingabe: `https://www.tiffe.de/Robotron/K1520/K7634_36/K7634-36.pdf` (5,1 MB, Scan), dazu
`doc/design/08_k7637_keyboard.md` und `core/peripherals/k7672/` als Vorbild des Zuschnitts.
Ergebnis `doc/k8915g2/k7634.md`: Steckerbelegung, Datenformat (Bitbelegung, Strobe/Ack),
Tastenmatrix → Code (Tabelle), Sondertasten, Anschluss an PIO der K7028/K2521 (welcher Port,
welche Handshake-Leitungen), Taktung. **Zusätzlich:** welche Fassung der **K7028** (.10/.20/.30
bzw. 045-8732) die K7634 trägt — Port-Adressen, PIO-Beschaltung, Unterschiede zur vorhandenen
Karte `core/cards/k7028/` (Tabelle). Unterlagen dazu suchen: tiffe.de hat **kein** K7028-
Verzeichnis; das Forum erwähnt K7028-Servicedokumente (classic-computing Thread 22043), in
`k8915schaltung.pdf` ist 045-8732 = K7028.30. **Fertig, wenn:** die Tabelle Taste → Code
vollständig ist oder die Lücken benannt sind; Hinweis, ob der `KEY`-Test der Gen-2-ROMs (V3b)
diese Tastatur abfragt.

> **AP-V1b erledigt 2026-10-05** — Ergebnis `doc/k8915g2/k7634.md`. Befunde:
> - **K7634 = intelligente Tastatur** (eigener U880 + 2708-ROM + 8212), parallele Schnittstelle X1
>   (`UB0–7`, `/UCS1` Daten, `/UCS2` Gültigkeit, `/UCS4` Kommando, `/UINT`), Codetabelle **je
>   Variante** im Tastatur-ROM. Neben den Serviceschaltplänen gibt es auf tiffe.de die
>   **Betriebsdokumentation** (`K1520/Tastaturen/Tastatur_K7634_K7636_Betriebsdokumentation.pdf`,
>   33 Seiten Scan, bisher nicht im Entwurf verzeichnet) mit Protokoll, Kommandos und Steckerbelegung.
> - Tabelle Taste → Code der **K7634.04 vollständig** (16 Matrixgruppen × 8, a/b, Dauerfunktion,
>   TYP `80H`); Varianten .05/.10 teilweise, .06/.13 nur identifiziert. Großschreibung =
>   Grundstellung, SHIFT liefert Kleinbuchstaben.
> - **An der K7028.10 (Ports `E0H–FFH`) belegt die Tastatur `E0H` (Daten), `E1H` (Status, Bit 3 =
>   Gültigkeit), `E2H` (Kommando)**, Interrupt `UINT` → ZRE-CTC Kanal 3; SIO `F0–F7H`, CTC
>   `F8–FFH`. Belege: 2708-Karten-ROM „3C00" und das CP/M-BIOS von Krzikalla (tiffe `CPM-KRZ`).
>   Die **K7028.30 (im Kern) trägt die K7634 nicht** (dort K7672 an SIO2-B) — Gen 1 braucht ein
>   anderes Portbild, nicht nur eine Tastatur.
> - **`KEY`-Test der Gen-2-ROMs 175–177 fragt die K7634 NICHT ab** (Ports `52H/53H` = SIO2-B,
>   K7672; Portbild der K7028.30). Der Chip „3C00" der 2708-Karte fragt dagegen `E0H/E1H` ab und
>   wertet `10H` (OFF → „ZYKL.") und `9DH` (ENTER → „LADER") aus. Folge für V3b/V9: Gen 1 =
>   **anderer Urlader** (der Kartenchip), nicht ROM 175.
> - **Abweichung vom Plan:** Die Fremdquellen erwarten TYP `A0H`, ENTER=`9DH`, RESET=`1FH`,
>   PF1=`91H` — keine der gedruckten Tabellen enthält diese Fassung. **Lücken:** kein Schaltplan
>   K7028.10/.20 (tiffe 404, felgentreu 403); `E3H`/`E4H`, Frontplatte der Gen 1, Bitbilder der
>   Kommandos `1xxYYYY` vs `1YYxxxx`, Druckfehler `E02` b=`33H` (K7634.04) — alles **[?]**.

### AP-V3a — Kommentierte Listings der Gen-2-Abzüge

**Erledigt 2026-10-05** (Befunde unten am Ende des Abschnitts).

**Ziel:** Lesbare, wächtergesicherte Listings als Grundlage für V3b und den Debugger.
**Agent:** boot-disasm-analyst (Sonnet). **Abhängig:** —. **Baut:** nur `dev.sh test`.

Eingaben: `doc/EPROMS/K8915G2/k8915g2_zre_0000-0BFF.bin` (3 KB, Platzfolge 175/176/177 **aus dem
Inhalt erschlossen [?]**), `k8915g2_pfs3820_3C00.bin` und `…_3000.bin`, Disassembler
`tools/z80_disasm2.py` (Einsprünge per `--entry`; Werkzeugwahl `tools/README.md`).
**Muster (nachbauen, nicht neu erfinden):** `tools/gen_prg710_zre_prn.py`,
`doc/EPROMS/PRG710/prg710_zre.prn`, Wächter `cli_prg710_zre_prn_passt_zur_quelle`
(`tests/cli/CMakeLists.txt` Z. 95–105).

Auftrag:
1. **Reihenfolge der drei ZRE-Bausteine prüfen** (Sprungziele, Tabellen, Meldungstexte,
   relativ zueinander) und das Ergebnis mit Beleg im Listing-Kopf festhalten. Abweichung von
   175/176/177 = 0000/0400/0800 **melden und im README korrigieren**.
2. Die ZRE ist **nicht** flach zu lesen: `175` kopiert 1 KB nach **FC00** und läuft dort
   (`LD SP,FC00 / LD DE,FC00 / LD HL,0 / LD BC,0400 / LDIR`), `177` läuft nach der Kopie bei
   **04xx** (Sprungziele 03xx–09xx). Das Listing zeigt deshalb **beide Sichten**: Lade-Adresse
   (ROM) und Lauf-Adresse (nach Kopie), mit Umrechnung je Abschnitt.
3. Ausgabe: `doc/EPROMS/K8915G2/k8915g2_zre.prn` (3 KB, Lade- und Lauf-Adresse) und
   `k8915g2_pfs3820.prn` (die zwei belegten Karten-Chips; die 14 leeren knapp vermerken),
   Handkommentare nur wo belegbar; Generator **`tools/gen_k8915g2_prn.py`** mit `--check`.
4. **Gegenüberstellung** der drei Paare (ZRE 175 ↔ Karte 3C00, ZRE 177 ↔ Karte 3000, jeweils
   Byte-Diff) als Anhang im Listing: welche Bytes unterscheiden sich, welche davon sind
   Adress-Operanden (F7xx ↔ 0Cxx), welche Port-/Textänderungen.
**Wächter:** `cli_k8915g2_prn_passt_zur_quelle` (Muster oben): bytegleich zum eingecheckten
`.prn`, jedes ROM-Byte genau einmal. **Fertig, wenn:** `tools/dev.sh test` grün und der Wächter
rot wird, sobald man einen Abzug ändert.

**Ergebnis V3a (erledigt 2026-10-05):** `tools/gen_k8915g2_prn.py` (+ `--check`), Listings
`doc/EPROMS/K8915G2/k8915g2_zre.prn` (3 KB, Lade- und Lauf-Adresse je Zeile, Anhang mit
der Gegenüberstellung) und `k8915g2_pfs3820.prn` (die zwei belegten Chips; die 14 leeren
knapp vermerkt), Wächter `cli_k8915g2_prn_passt_zur_quelle` (`tests/cli/CMakeLists.txt`):
prüft MD5 aller Abzüge samt Verkettungen, jedes ROM-Byte genau einmal, die Prüfsummen, jede
benannte Routine als Label, Byte-Gleichheit zum `.prn`. Gegenversuch: Abzug 176 in einer
Kopie (`K8915G2_DIR=…`) um ein Bit verändert → Wächter rot (MD5), Abzug 177 mit angepasster
MD5SUMS → rot (Verkettung); Abzüge im Repo unverändert (`md5sum -c MD5SUMS` ok).
Befunde:
1. **Reihenfolge 175/176/177 = 0000/0400/0800 bestätigt, keine README-Korrektur der Reihenfolge
   nötig** (nur Ergänzung der Belege): 175 `JP 0400H` ↔ Sprungtabelle `C3 1A 04 / C3 2B 04` in
   176; 176 `CALL 0907H` ×6 und Kopie `098FH → FFE0H` ↔ Textroutine/Haeppchen in 177; 177
   `JP 03F3H` ↔ Vektor `C3 00 04` in 175; der Selbsttest „MROM“ summiert IX = 0000/0400/0800.
2. **Korrektur zu Punkt 2 des Auftrags:** nur **175** wird nach FC00H kopiert (Lauf = Lade +
   FC00H ab 0021H); **176 und 177 laufen unverschoben aus dem ROM** (bei 0400H/0800H, ihre
   absoluten Sprungziele sind ihre Ladeadressen). „03xx–09xx“ = `JP 03F3H` (Vektor in 175) bis
   09xx (Text in 177). Zweite Lauf-Sicht nur noch beim 23-Byte-Haeppchen `098FH → FFE0H`
   (A8H = 8FH, prüft `[0000]`/`[0005]` = `C3`, A8H = 0EH, `JP 0428H`).
3. **Prüfsummen:** letzte 3 Bytes jedes 1-KB-Bausteins = 24-Bit-Summe der ersten 3FDH Byte
   (so rechnet der Selbsttest). **177 stimmt nicht** (berechnet 00A680H, gespeichert 00A67CH);
   Byte 0A33H = 04H im Füllbereich erklärt die Differenz genau → Lesefehler oder Bitfehler im
   2708 [?]. Neue Frage **F9** (§9). 175, 176 und beide Karten-Chips stimmen.
4. **Karten-Chips:** „3C00“ = Fassung des 175 (A8H = 0EH statt 06H, Ports E0H–E4H/B1H/B3H, prüft
   `F3 ED 5E` bei D001H–D003H, eigener Kopierer 003FH, Selbsttest „I/O“); „3000“ = Fassung des
   177 (Zellen 0Cxx, Port E4H, **Taste über `IN E1H`/`IN E0H`**, kein SIO-Init). Passt zu
   einer anderen E/A-Karte als die ZRE-Fassung (Gen 1 = K7634 + K7028 [?], für V3b/V9).
   Deckt sich mit F7/V1b: das Chip-„3000“ wertet `1FH` (RESET → `JP 03F3H`) und `9DH` (ENTER) aus, also
   die K7634-Codes; die ZRE-Fassung 177 fragt dagegen `45H`/`1BH 63H`/`0DH`/`7CH`/`1CH` an der SIO ab.
5. Die ZRE-Listings enthalten gesicherte Strukturen, die V3b übernehmen kann: SIO-Tore
   5AH/53H/52H (Tastatur/Konsole), 45H/47H/55H, Bildspeicher 1000H–177FH, Rahmen-/Muster-
   tabellen bei FF77H.., Vektorwort `FFF6H → FF3DH`, Inline-Text-Konvention (Bit 7 = Textende,
   Aufruf `CALL 0907H`).

### AP-V3b — Analyse der Gen-2-ROMs (Kopf der Planung)

**Ziel:** Alles, was V2/V4/V6 an Hardware-Wissen brauchen, **belegt**.
**Agent:** boot-disasm-analyst (**Opus**: Speicherumschaltung und Boot sind subtil).
**Abhängig:** V3a (Listings), V1a (Karten, wenn schon fertig; sonst mit Annahme **[?]**).
**Baut:** nein; Läufe im Emulator **nicht möglich** (es gibt noch keine Gen-2-Maschine) — nur
statische Analyse, ggf. kurze Z80-Probeläufe mit `tools/` ohne Maschine.

Ergebnis `doc/k8915g2/zre_rom.md`:
1. **Speicherkarte nach der Kopie:** welcher Teil liegt wo (ROM, RAM, FC00-Kopie, Arbeitszellen
   F7xx/F9xx), welche Speicherbereiche werden sichtbar/unsichtbar.
2. **`OUT (A8H)`:** Bitbedeutung des Registers (Werte `06H`, `0EH`, … aus den Fundstellen
   ableiten), Vergleich mit dem V3-Register (`doc/merkposten/k8915.md`, A8H-Brückenfeld,
   `core/cards/zre8762/`). Entscheidung: **gleiches Register, anderes Register, oder anderer
   Mechanismus?** mit Belegen.
3. **Portkarte:** jede in den ROMs angesprochene Adresse (80H–83H CTC, 52H, 61H, 10H–19H K5122,
   E0H–E4H ATS, B1H/B3H, FBH–FEH, … — vollständig, nicht nur die Beispiele) → Baustein, mit
   Fundstelle; Abgleich mit der V3-Portkarte (`doc/design/16_k8915.md`). Unbekannte Adressen
   als Fragen.
4. **Selbsttest** („MROM RAM SIO KEY CTC DIAGNOSTIC“): je Test, was er prüft, **welche Hardware
   vorhanden sein muss**, Fehlermeldung bei Fehlen, Abbruchverhalten (damit V6 weiß, was zum
   Start nötig ist; vgl. `Config::pruefstecker` bei V3).
5. **Bootablauf:** Reihenfolge, Laufwerksauswahl, Lade-Adresse/-Länge der Bootspuren, Übergabe
   an das System (Sprungziel, Registerzustand), Meldungen mit Bedingung („No system disk“ usw.).
   Prüfen, ob das Verfahren zu den vorhandenen Disketten passt (`disks/k8915scpx_boot1.hfe`,
   Entwurf 16): Ladekopf-Format, K5122-Betriebsart (`/WAIT` oder ZVE2-Weg).
6. **Interruptmodus** (IM 2, I-Register, Tabelle), CTC-/SIO-Initialisierung (Baudraten →
   Takt 2,4576 MHz?).
7. **Die Karte (3C00/3000) vs. ZRE:** was ist anders, **läuft die Karte als Ersatz oder als
   Ergänzung**, an welcher Adresse wird sie benutzt; welche Hardware-Annahme steckt hinter 0Cxx
   gegenüber F7xx (1-KB-RAM der K2521 bei 0C00–0FFF?).
8. **Anforderungsliste für V4/V6** („die Maschine braucht: …“) und **offene Fragen** an den
   Anwender, nummeriert F7ff. in §9.
**Wächter:** keiner (Doku), aber jede Zeile der Portkarte mit Fundstelle. **Fertig, wenn:** die
Anforderungsliste so vollständig ist, dass V2 daraus ohne Rückfrage einen Maschinenentwurf schreibt.

> **AP-V3b erledigt 2026-10-05** — Ergebnis `doc/k8915g2/zre_rom.md` (statisch, Byte-Vergleich mit dem
> V3-ROM, Probelauf `tools/k8915_sysload.py`). Befunde:
> - **Der Lader ist der V3-Lader:** 0400–0906H sind mit `k8915_boot_2732.bin` **byteidentisch** bis auf
>   041BH (Stub-Quelle 098FH) und 042AH (`JP NZ,03FAH`). Ladekopf, `/WAIT`-K5122, Software-CRC,
>   Warmstart-Einsprung 0406H gelten unverändert; `disks/k8915scpx_boot1.hfe` wird gelesen wie am V3
>   (12 272 B nach C000–EFEFH, Einsprung D600H). Eigen sind nur 175 (Selbsttest) und die
>   Meldungsroutine (`E`/`ESC c` ⇒ Neubeginn) samt Stub-Werten.
> - **A8H = gleiches Register, bitkompatibel für Bit 0/1/2/7, anderer Träger (K3528 [?]).** Gen-2-Werte
>   nur 06H/0EH/87H/8FH — Bit0 und Bit7 immer gleich; der V3-Resetwert 8EH fehlt (würde bei `/MEMDI`
>   an Bit7 die steckbare K2521 abschalten [?]). RAM-Test bei 87H über 0068–FDC7H ⇒ 64 KB RAM durchgehend;
>   keine Bank 2, Bit3 ohne beobachtbare Wirkung.
> - **Portkarte = V3** (gleiche ATS 045-8732, K5122 10H–18H, Latch 61H, K2521-CTC 80H/83H); PIO
>   84H–87H unbenutzt. Selbsttest **ROM → KEY → CTC → SIO → RAM** (SIO drei Runden, Rückschleife auf
>   drei Kanälen wie V3); erster Fehler ⇒ ERROR, 16 × BEL, `CR` ⇒ Kaltstartmeldung. NMI = `RETN`.
> - **Abzug 177 (F9):** der Emulator bliebe mit dem Abzug bei „ROM“ Kennbuchstabe `C` stehen, KEY…RAM
>   ungeprüft; `CR` führt trotzdem zum Lader — für V6 prüfbar ohne Diskette.
> - **Kartenchips = Gen-1-Urlader (Ersatz, keine Ergänzung):** 3C00 für Platz 0000H, 3000 für 0800H;
>   sie passen **nicht** zum ZRE-176 (Zellen 0Cxx statt F7xx, Stub 095BH statt 098FH) — das passende
>   176-Gegenstück fehlt (F11). Im Gen-2-Ablauf nie angesprochen, neben 64 KB RAM nicht betreibbar ⇒
>   **V5 = Beigabe**. Die K7634 fragt nur die Karte ab (E0H/E1H), nicht 175–177.
> - **Berichtigung zu Entwurf 16 §4.4:** Ladekopf-Byte 7–9 werden benutzt — vom Warmstart 0406H
>   (F710H = 1), auch am V3.
> - **Abweichung vom Plan:** keine Z80-Probeläufe nötig; die Selbsttestdauer (≈ 17 Mio. Takte) ist nur
>   gerechnet. Anforderungsliste für V4/V6: `zre_rom.md` §8.

### AP-V2 — Entwurf und Schärfung der Kern-APs

**Agent:** Plan (Opus). **Abhängig:** V1a, V1b, V3b, Antworten F1–F3. **Baut:** nein.
Ergebnis in diesem Dokument: (1) **Entscheidung eine Klasse** (`K8915Machine::Config::generation`)
**oder** eigene Klasse — Empfehlung bleibt eine Klasse, bei Abweichung begründen; (2) Steckplatz-
und Kartenliste je Variante; (3) Port-/Speicherkarte; (4) **V4–V9 als vollständige Aufträge** im
Muster von V3a (Ziel, Eingaben mit Dateipfaden, Ergebnis, Wächtern mit Namen, Fertig-Kriterium),
V5 entschieden (Beigabe oder Karte); (5) Namen in Oberfläche/Konfiguration (F6). **Fertig,
wenn** ein Agent V4 ohne Rückfrage beginnen kann.

> **AP-V2 erledigt 2026-10-05** — Ergebnis im folgenden Abschnitt. Ohne Antworten auf F1–F4
> gearbeitet; jede davon abhängige Festlegung trägt **[?]** und nennt die Frage.

### AP-V2 — Ergebnis

Grundlage: `doc/k8915g2/zre_rom.md` (V3b, insb. §8 Anforderungsliste), `karten.md` (V1a),
`k7634.md` (V1b), der Kern (`core/machines/k8915/`, `core/cards/zre8762/`, `core/cards/k2521/`,
Vorbild `core/machines/prg710/` + `core/cards/prg710_speicher/`), `app/profil.py`.

#### R1. Klassenentscheidung: **eine Klasse `K8915Machine`, `Config::generation`**

**Entscheidung:** `K8915Machine::Config` bekommt
`enum class Generation : uint8_t { V3, Gen2 }` und `Generation generation = Generation::V3`.
**Gen 1 kommt NICHT in die Aufzählung**, solange V9 zurückgestellt ist (R4) — kein toter Pfad.

Begründung:
1. Die Gen 2 unterscheidet sich vom V3 **nur in CPU-Karte und Speicher** (V3b §0/§3.1: Lader
   0400–0906H byteidentisch, Portmenge identisch). ATS K7028.30 + K7672, K5122 im `/WAIT`-Zweig,
   `Laufwerke`, `SerialHub`, Anzeigelatch 61H, NMI-Weg, RAF/K6022-Haken bleiben **dieselben
   Objekte mit derselben Verdrahtung**. Eine zweite Klasse kopierte ~400 Zeilen
   (`k8915.{h,cpp}`) samt Laufschleife und Hub-Anmeldung.
2. Maschinentyp 2, `k8915emu`, `k8915emu.yaml`, Starter, Paket, `dbgm::DbgMachine` und alle
   Werkzeuge bleiben eine Maschine — genau wie `Prg710Machine::Config::Variante`.
3. Risiko (V3-Regression durch den Umbau) ist beherrschbar: Vorgabe bleibt V3, alle
   `K8915*`-Wächter laufen unverändert mit.

**Umbau-Muster (für V6a verbindlich):** die CPU-Karte wird austauschbar, alles andere bleibt Wert-Member.
- `K8915Zre zre_` → `std::unique_ptr<K8915Zre> zre8762_` (nur V3); neu
  `std::unique_ptr<K2521> k2521_` und `std::unique_ptr<K3528> ops_` (nur Gen 2). Die übrigen
  Member (`bus_`, `ats_`, `screen_`, `afs_`, `lw_`, `kbd_`, `hub_`) bleiben unverändert in
  derselben Reihenfolge (Zerstörungsreihenfolge des Hubs!).
- Private Weichen statt verstreuter `if`: `Z80& cpuRef()`, `bool zreTakt(int)` (CTC der
  jeweiligen ZRE), `InterruptSlave& zreInt()`, `uint8_t memCpu(uint16_t)`/`memCpuW(…)`.
- Öffentlich: `generation()`; `zre()` bleibt `K8915Zre&` und ist **nur am V3** gültig
  (Vorbedingung per `assert` + Kommentar; alle heutigen Aufrufer sind V3); neu `k2521()`,
  `ops()` (nur Gen 2); `cpuPC()`, `setCpuTraceCallback`, `setBusTrace`, `memReadDebug`
  dispatchen über die Weichen.
- `K2521::Config::k8915g2()` + `K7024::A5120Config::forK8915Gen2()` als neue Fabriken; die
  Vorgaben von `K2521`, `K7024`, `K7028`, `K5122` bleiben unberührt.

#### R2. Karten und Steckplätze je Variante

| Funktion | V3 (Vorgabe, im Kern) | **Gen 2** (V4/V6) | Gen 1 (zurückgestellt, V9) |
|---|---|---|---|
| ZRE | 045-8762 (`K8915Zre`): U880, CTC 80H, 4-KB-ROM, 2 × 64 KB, A8H | **K2521** (`K2521`, `Config::k8915g2()`): U880 2,4576 MHz, ROM 175/176/177 = 3 KB 0000–0BFFH, 1 KB RAM 0C00–0FFFH, CTC 80H, PIO 84H (unbenutzt) | K2521 + Gen-1-Urlader (Kartenchips „3C00“/„3000“ + **fehlender 0400-Baustein**, F11) |
| Speicher | auf der ZRE | **K3528** [?, F2/F20] (`K3528`, neu): 64 KB DRAM, 8212-Register A8H–ABH → /MEMDI, /MEMDI1 | wie Gen 2 [Web] |
| ATS | 045-8732 = K7028.30 (`K7028`) | **unverändert** | K7028.10 (E0H–FFH), neu [?, F8] |
| Tastatur | K7672 an SIO2-B | **unverändert** | K7634 parallel an E0H–E2H, neu |
| Bild | K7024 012-6820, ZG y411/y412 | **K7024 mit A5120-ZG `v171`/`v172`** (`forK8915Gen2()`) | [?] |
| Floppy | K5122 `/WAIT`, 2 × K5601 | **unverändert** [?, F4: Laufwerke] | unverändert [Web] |
| PFS K3820 | — | **nicht modelliert** (V5 = Beigabe) | Träger des Gen-1-Urladers [?] |
| Optionen | RAF 88H, K6022 E0H | RAF 88H, K6022 E0H (beide frei) | K6022 **kollidiert** mit E0H–E7H |

**Steckplätze:** V3 nach Gerät (3 = K5122, 4 = ZRE, 6 = ATS, 7 = K7024). Gen 2: **unbekannt
[?, F2]**; als Ersatz gilt die **Interruptkette des V3**: K5122 → K2521 (CTC → PIO) → ATS.
Dafür steht die K2521 auf `IeiQuelle::System` (in der Kette, nicht an der Spitze) — damit
verhält sich die Gen 2 bei Interrupts **wie der erprobte V3** mit demselben BIOS. Die
„Normalfall“-Stellung der K2521-Beschreibung (X14:1 = höchste Priorität) wäre ebenso
denkbar [?, F22]; das ROM pollt die K5122, der CTC-Test braucht nur, dass alle drei CTCs
überhaupt quittiert werden. CTC-Kaskade X10/X11 der K2521: **alle offen** (wie die
045-8762, die keine hat; K3 läuft im ROM und BIOS als Zeitgeber) [?, F22].

#### R3. Port- und Speicherkarte

**Ports** (Gen 2 = V3 bis auf die Träger; Belege `zre_rom.md` §3.1):

| Port | V3 | Gen 2 | Bemerkung |
|---|---|---|---|
| 10H–18H | K5122 `/WAIT` | gleich | |
| 40H–5FH | ATS K7028.30 (SIO1 44H–47H, CTC1 48H–4BH, SIO2 52H–55H, CTC2 58H–5BH, Spiegel) | gleich | K7672 an 52H/53H |
| 60H–67H | Anzeigelatch 61H (ATS) | gleich | |
| 80H–83H | CTC der 045-8762 | **CTC der K2521** | IM-2-Vektor F0H |
| 84H–87H | (PIO D35 nicht nachgebildet) | **PIO der K2521**, vorhanden, unbenutzt | |
| 88H–89H | RAF (Option) | RAF (Option) | K3528 **nicht** auf 88H (Handeintrag im Plan, F21) |
| A8H–ABH | Register der 045-8762 | **Register der K3528** [?, F20], nur schreibbar | Lesen: nicht dekodiert → Bus (FFH) [?] |
| E0H–E7H | K6022 (Option) | K6022 (Option) | |

**Speicherbild Gen 2** je A8H-Wert (4-KB-Schritte; „Bus“ = nicht von K2521/K3528 bedient,
geht an den Systembus, dort antwortet nur die K7024 bei 1000–17FFH, sonst FFH):

| A8H | 0000–0BFF | 0C00–0FFF | 1000–17FF | 1800–3FFF | 4000–7FFF | 8000–FFFF |
|---|---|---|---|---|---|---|
| 00H (Reset) | K2521-ROM | K2521-RAM | Bus (K7024) | Bus | Bus | Bus |
| 06H / 0EH | K2521-ROM | K2521-RAM | Bus (K7024) | Bus | K3528 | K3528 |
| 87H / 8FH | K3528 | K3528 | K3528 | K3528 | K3528 | K3528 |

Regeln dahinter (Vorgabe „V3-kompatibel“, Konfigurationsstruktur `K3528::Belegung`):
Seite 0 (0000–3FFF) = Bit 0, Seite 1 (4000–7FFF) = Bit 1, Seiten 2 + 3 (8000–FFFF) = Bit 2;
`/MEMDI` = Bit 7 (aktiv bei 1) sperrt die K2521 (ROM + RAM); `/MEMDI1` = Bit 3 (aktiv bei 0),
**ohne Verbraucher**; Bit 4–6 wirkungslos (keine Bank 2). Ein gewählter K3528-Zugriff geht
**nicht** an den Systembus (Annahme wie V3, `zre8762.h` Kopf). Vorrang: K3528-Seite gewählt →
K3528; sonst 0000–0FFF und kein /MEMDI → K2521; sonst Bus. Für alle bekannten Werte (06H,
0EH, 87H, 8FH, BIOS 87H/06H) ist Bit 0 = Bit 7; die Alternative „/MEMDI an Bit 0“ (Handdraht
X3:37→38, `karten.md` §2.2) ergibt dasselbe Bild und ist als Konfiguration bewacht (V4).
1-KB-Ausblendadresse der K3528: **nicht modelliert** (kein ROM- oder BIOS-Zugriff, Polarität
unklar). Ausbau .10/.20 (48/32 KB): nicht modelliert.

#### R4. Was ohne Anwenderantwort sinnvoll ist — und was nicht

| AP | Entscheidung | Grund |
|---|---|---|
| **V4** | **jetzt** | Verhalten aus dem ROM vollständig belegt (V3b §1/§2); die offenen Brücken (F20/F23) sind Konfiguration mit V3-kompatibler Vorgabe |
| V5 | **entfällt — durch V3a erledigt** | Kartenchips sind Gen-1-Urlader, im Gen-2-Ablauf nie angesprochen, neben 64 KB RAM nicht betreibbar (V3b §7). Abzüge + MD5 + Listing liegen im Repo, Wächter `cli_k8915g2_prn_passt_zur_quelle` existiert. Keine Karte K3820 |
| **V6a** | **jetzt** (nach V4) | Netz-Ein → Selbsttest → „ROM C“ → `CR` → Kaltstartmeldung ist ohne Diskette prüfbar; Laden der V3-Systemdiskette 901 ist mit byteidentischem Lader belegt |
| **V6b** | **jetzt** (nach V6a) | Werkzeuge; reine Fleißarbeit an `--machine` |
| **V7a/V7b** | **jetzt** (nach V6a) | Bedienbarkeit; Namen mit Annahme [?] (R5), nur Anzeigetexte hängen an F1/F6 |
| V8 | **gestrichen** | Ladekopf und Format wie V3 (V3b §5); das DiskTool kann schon bootfähige K8915-Disketten (AP-E5c). Wieder öffnen nur, wenn eine Gen-2-Diskette (F4) davon abweicht |
| V9 | **zurückgestellt** (ganz) | Gen 1 ohne den Lader-Baustein für 0400H **nicht startfähig** (F11), kein Nachbau aus 176. Eine K7634/K7028.10 ohne Gast, der sie anspricht, hätte keinen belastbaren Wächter. Wiedervorlage bei Antwort auf F11 oder F5/F8 |
| V10, VT | nach V7b | wie geplant |

Reihenfolge (alle bauen, also **nacheinander**): V4 → V6a → V6b → V7a → V7b → V10 → VT.

#### R5. Namen in Kern, Schnittstelle, Oberfläche und Konfiguration

Annahme **[?, F1]**: „Generation 2“ ≈ robotrontechnik „5¼″ V2“ (K2521, K3528, 045-8732, K7672
stimmen; nur dort genannte 045-8778 ABS 2K ≠ K7024 des Geräts). Weil F1 offen ist, tragen
**Schlüssel** nur technische, nicht umzubenennende Namen; nur die **Anzeigetexte** folgen F1/F6
und dürfen später ohne Konfigurationsumzug geändert werden.

| Ebene | V3 | Gen 2 |
|---|---|---|
| Kern | `Generation::V3` (Vorgabe) | `Generation::Gen2` |
| C-ABI | `k1520_create(K1520_MACHINE_K8915)` / `k1520_create_k8915(0, …)` | `k1520_create_k8915(1, d0..d3)`; 2 (= Gen 1) → NULL mit Grund „Gen 1: kein startfähiger Urlader (F11)“ |
| Abfrage | `k1520_k8915_generation(h)` = 0 | = 1; andere Maschinen −1 |
| `k1520_machine_type` | 2 | 2 |
| Python | `K1520Emulator(machine="k8915")` | `machine="k8915-g2"` (wie `"prg710-1"`, `"pc1715w"`) |
| Werkzeuge | `--machine k8915` | `--machine k8915-g2` |
| Konfiguration (`k8915emu.yaml`) | `general.model` fehlt oder `k8915` | `general.model: k8915-g2` |
| Anzeige (Modellwahl) | „K8915 V3 (ZRE 045-8762, 128 KB)“ | „K8915 Gen 2 (ZRE K2521, 64 KB)“ **[?, F6]** |
| Gesperrt angezeigt | — | „K8915 Gen 1 (Tastatur K7634)“, Grund „kein Urlader-Baustein für 0400H (F11)“ über `gesperrte_modelle` |

**Abweichung vom Rahmen:** nicht `K1520Emulator(…, generation=…)`, sondern ein Maschinenname
— so verlangt es die Modelltabelle von `app/profil.py` (Spalte „Kern-Maschine“), und PRG 710-1
und PC 1715W machen es ebenso.

#### AP-V4 — Karte K3528 und K2521-Fassung der Gen 2

**Ziel:** Speicher der Gen 2 als Karte, gegen die ROM-Belege bewacht; ROM 175–177 im Kern.
**Agent:** cpp-coder (Sonnet; Speicherlogik klein, Muster vorhanden). **Abhängig:** —.
**Baut:** ja (`tools/dev.sh test`).

Eingaben: R1–R3 oben; `doc/k8915g2/zre_rom.md` §1, §2, §8 Punkt 1–2; `doc/k8915g2/karten.md`
§2; Muster `core/cards/zre8762/zre8762.{h,cpp}` (A8H-Abbildung, `Brueckenfeld`, `setMemTrace`,
kein `registerMem`) und `core/cards/prg710_speicher/prg710_speicher.{h,cpp}` (Zugriffswege
`setZreWeg`, `ortVon`); `core/cards/k2521/k2521.{h,cpp}` + `rom_prg710.h` (Fabrik + ROM-Kopf);
Abzug `doc/EPROMS/K8915G2/k8915g2_zre_0000-0BFF.bin` (MD5 aus `MD5SUMS` prüfen), Generator
`tools/eprom_to_h.py`; Tests `tests/unit/cards/test_zre8762.cpp`, `test_k2521.cpp`;
Anmeldung `tests/unit/CMakeLists.txt` Z. 40–46, Bibliotheken `CMakeLists.txt` Z. 302–315.

Auftrag:
1. **`core/cards/k3528/k3528.{h,cpp}`**, Klasse `K3528 : public BusDevice`, Bibliothek
   `k1520_k3528`. Kopf: Herkunft (`karten.md` §2), **Hypothese [?] F2/F20** (welche Karte das
   Gerät trägt). Inhalt:
   - `struct Belegung { uint8_t seite0 = 0, seite1 = 1, seite23 = 2; uint8_t memdi_bit = 7; bool memdi_aktiv_h = true; uint8_t memdi1_bit = 3; bool memdi1_aktiv_h = false; static Belegung v3kompatibel(); static Belegung memdiAnBit0(); }`
     (Bitnummern; 0xFF = „nicht verdrahtet“).
   - `struct Config { uint8_t reg_base = 0xA8; Belegung belegung{}; }` — belegt 4 Ports
     (AB0/AB1 nicht dekodiert).
   - `ioWrite` setzt das Register und baut die Abbildung neu (16 × 4-KB-Slots wie `K8915Zre`);
     `ioRead` → `0xFF` (Ausgaberegister, nicht lesbar [?]).
   - Zugriffswege: `setZreWeg(LeseFn, SchreibFn)` (K2521 0000–0FFF); alles nicht Gewählte über
     `K1520Bus::memRead/memWrite`. `memRead/memWrite` = CPU-Sicht mit Vorrang aus R3.
   - `enum class Quelle { Ram, Zre, Bus }`, `ortVon(addr)`, `reg()`, `setReg()`, `memdi()`,
     `memdi1()`, `reset()` (Register 00H, RAM bleibt), `powerOn(fill)`, `ramPeek/ramPoke`,
     `setMemTrace(K1520Bus::BusTrace)` (feuert für RAM- und ZRE-Zugriffe, nicht für Bus).
   - `attachToBus(bus)`: nur `registerIO(this, reg_base, 4)`, **kein** `registerMem`.
2. **ROM:** `core/cards/k2521/rom_k8915g2.h` (`K8915G2_ZRE_ROM[3072]`, Kopfzeile „Generated
   from: doc/EPROMS/K8915G2/k8915g2_zre_0000-0BFF.bin“, mit `tools/eprom_to_h.py`), **Abzug
   unverändert** (Byte 0A33H = 04H bleibt, F9). `K2521::Config::k8915g2()`: ROM 0x0C00,
   CTC 80H, PIO 84H, Kaskaden alle `false`, `iei_quelle = System` (R2).
3. **K7024:** `K7024::A5120Config::forK8915Gen2()` = `forK8915()` mit `chargen_* = nullptr`
   (eingebauter A5120-Satz v171/v172; Abzüge 171/172 sind byteidentisch, §10).
4. Tests `tests/unit/cards/test_k3528.cpp` (`k1520_add_test(k3528 …)`), Erweiterung
   `test_k2521.cpp`, ein Fall in einem bestehenden K7024-Test.

**Wächter:**
- `K3528.SpeicherbildJeRegisterwert` — 00H/06H/0EH/87H/8FH, jeder 4-KB-Slot gegen die Tabelle R3.
- `K3528.ResetLoeschtRegisterRamBleibt`, `K3528.RegisterHatVierPorts` (A8H–ABH gleichwertig),
  `K3528.RegisterIstNichtLesbar`.
- `K3528.GewaehlterSpeicherErscheintNichtAmBus` (Bus-Gerät an 1000H sieht bei 87H keinen Schreibzyklus).
- `K3528.MemdiSperrtDieZre`, `K3528.Memdi1OhneWirkungAufDasBild`, `K3528.Bits4Bis6Wirkungslos`.
- `K3528Config.MemdiAnBit0ErgibtDasselbeBildFuerAlleBekanntenWerte`, `K3528Config.BelegungIstKonfiguration`.
- `K2521Rom.K8915Gen2AbzugUnveraendert` — `rom_len` = 0x0C00; 24-Bit-Summe der ersten 3FDH Byte
  je Baustein: 175/176 = gespeicherte Summe, **177 = 00A680H ≠ 00A67CH**, Byte 0A33H = 04H.
- `K7024.Gen2HatDenA5120Zeichensatz`.

**Fertig, wenn:** `tools/dev.sh test` grün (voll), V3-, PRG-710- und K7024-Wächter unverändert,
Abschnitt „AP-V4 erledigt“ hier nachgetragen, ein Commit (Regeln oben).

**AP-V4 erledigt 2026-10-05.** `core/cards/k3528/` (Bibliothek `k1520_k3528`, nur `registerIO`,
Speicherpfad wie `K8915Zre`), `core/cards/k2521/rom_k8915g2.h` + `K2521::Config::k8915g2()`,
`K7024::A5120Config::forK8915Gen2()`; Wächter wie benannt (`test_k3528.cpp`, je ein Fall in
`test_k2521.cpp`/`test_k7024.cpp`) und voller `tools/dev.sh test` grün (2163/2163).
Befunde/Abweichungen:
- Die 24-Bit-Prüfsumme steht **hoch..tief** (Byte 3FDH = Bits 23..16): 175 = 01AE2BH, 176 = 018EF9H,
  177 gespeichert 00A67CH, errechnet 00A680H — wie im Plan.
- `K3528::ioWrite/ioRead` bekommen wie bei der 045-8762 die **absolute** Portnummer (Bus reicht
  `port` durch, obwohl `registerIO` „relativ“ dokumentiert); dem 4-Port-Register ist das gleich.
- `K3528::Quelle` hat `Ram/Zre/Bus`; die ZRE-Seite ist ein Rückruf (`setZreWeg`), die Karte kennt
  `K2521` nicht (Schichtung). `memRead` auf einen Slot `Zre` ohne gesetzten Weg liefert FFH.
- Der Wächter `K2521Rom.K8915Gen2AbzugUnveraendert` vergleicht den ROM-**Inhalt** (das
  `static constexpr`-Array hat je Übersetzungseinheit eine eigene Adresse, Zeigervergleich scheitert).
- Keine neuen offenen Fragen; F2/F20/F22/F23 bleiben als [?] im Kopf von `k3528.h`/`k2521.cpp`.

#### AP-V6a — `K8915Machine` Gen 2: Verdrahtung und Boot

**Ziel:** Die Gen 2 läuft vom Netz-Ein bis zur Kaltstartmeldung und lädt die V3-Systemdiskette.
**Agent:** **Opus** (Umbau der laufenden V3-Maschine, Interruptkette, Laufschleife).
**Abhängig:** V4. **Baut:** ja.

Eingaben: R1–R3; `zre_rom.md` §4–§6, §8 Punkte 3–13; `core/machines/k8915/k8915.{h,cpp}`;
Vorbild Speicherweg `core/machines/prg710/prg710.cpp` Z. 55–70 (`setSpeicherweg` +
`mem_trace_`); Wächter-Muster `tests/integration/test_k8915_boot.cpp`, `test_k8915_scpx.cpp`,
`tests/support/` (`TempDisk`, `vramText`, Losgröße 5 000 bei Tastatur), Fixture
`tests/fixtures/disks/k8915scpx_boot1.hfe` (Diskette 901, **nur über `TempDisk`**);
`doc/merkposten/k8915.md` (alle Festlegungen gelten für Gen 2 sinngemäß).

Auftrag:
1. Umbau nach R1 („Umbau-Muster“). Gen-2-Verdrahtung im Konstruktor: `K2521(bus_,
   cfg.gen2_rom ? … : K2521::Config::k8915g2())`, `K3528`, `k2521_->setSpeicherweg(→ ops_)`,
   `ops_->setZreWeg(→ k2521_->memRead/memWrite)`, `K7024` mit `forK8915Gen2()`, ATS/K7672/
   K5122/Hub/Prüfstecker **wie V3**, Interruptkette `{&afs_, k2521_.get(), &ats_}`
   (RAF/K6022 hängen sich wie heute an).
2. `Config::gen2_rom` (`const uint8_t*`, 0x0C00 Byte, Vorgabe `nullptr` = Abzug) — **nur für
   Tests**: der volle Selbsttest braucht ein ROM mit stimmender Summe (F9); der Test legt eine
   Kopie an und setzt Byte 0A33H := 00H. Nie im Repo-Abzug flicken.
3. `powerOn()`: K2521-RAM und K3528-RAM mit 00H (sonst „System im RAM“ zufällig, wie V3),
   dann /RESET; `resetHardware()`: `k2521_->reset()`, `ops_->reset()` (A8H := 00H).
4. Laufschleife: unverändert bis auf die Weichen (`cpuRef()`, `zreTakt()`).
5. Doku: Kopf von `k8915.h` (Gen 2, Steckplätze [?]), Absatz in `doc/merkposten/k8915.md`
   (Gen 2: Umbau-Muster, `zre()` nur V3, ROM C bis F9).

**Wächter** (`tests/integration/test_k8915g2_boot.cpp`, `k1520_add_test(k8915g2_boot …)`):
- `K8915Gen2Boot.RomFehlerCDannCrZurKaltstartmeldung` — Abzug wie geliefert: Bild zeigt
  „ROM“ + Kennbuchstabe `C` (1770H/1776H), 61H = 7FH, `bellCount` ≥ 16; `CR` ⇒
  „\* Coldstart \*“ + „Disk on A: ready“. **An F9 gebunden:** liefert der Anwender einen
  korrigierten 177, wird dieser Wächter durch den vollen Selbsttest ersetzt.
- `K8915Gen2Boot.GeflickteSummeSelbsttestFehlerfreiBisColdstart` — `gen2_rom` mit 0A33H = 00H:
  ROM → KEY → CTC → SIO → RAM ohne Fehler, ohne `CR` zur Kaltstartmeldung, A8H am Ende 06H.
- `K8915Gen2Boot.OhnePruefsteckerScheitertSio` (Kennbuchstabe notieren), `.OhneTastaturScheitertKeyMitA`.
- `K8915Gen2Boot.SystemImRamFuehrtZumLader` — `C3` bei 0000H/0005H im K3528-RAM, Reset ⇒
  Kaltstartmeldung **ohne** Selbsttest, A8H = 0EH.
- `K8915Gen2Boot.EOderEscCStartetNeu` — `E` an der Kaltstartmeldung ⇒ Selbsttest erscheint wieder.
- `K8915Gen2Boot.NmiImRomWirkungslos` — NMI während des Selbsttests ändert nichts (`RETN`).
- `K8915Gen2Boot.RamTestSiehtUnterRomUndBild` — nach bestandenem RAM-Test liegt `ED 45` bei 0066H im K3528-RAM.
- `K8915Gen2Scpx.LaedtDieV3SystemdisketteBisZumPrompt` — 901 über `TempDisk`, `CR`, stabiles
  `A>`, danach `dir` zeigt Dateien. **[?]** Der Autostart `rade` darf scheitern (F13, keine
  Bank 2) — der Wächter prüft den Prompt nach dem Autostart, nicht RADE. Scheitert der Boot
  an etwas anderem, ist das ein Befund für §9, kein Grund, das ROM zu biegen.
- **Alle** `K8915Boot.*`, `K8915Scpx.*`, `K8915Physical.*`, `RafK8915.*`, `K6022Maschine.K8915`
  unverändert grün (V3-Vorgabe).

**Fertig, wenn:** `tools/dev.sh test` grün, `tools/dev.sh test-format` grün (wegen
`K8915Format.*`, im Hintergrund), Nachtrag hier, ein Commit.

**AP-V6a erledigt 2026-10-05.** `K8915Machine::Config::generation` (`V3` Vorgabe | `Gen2`) +
`Config::gen2_rom` (nur Tests); Umbau nach R1 (`zre8762_`/`k2521_`/`ops_` als `unique_ptr`,
Weichen `cpuRef`/`zreTakt`/`zreInt`/`memCpu(W)`, `zre()` mit `assert` nur V3, neu `generation()`,
`k2521()`, `ops()`); `k1520_k8915` linkt `k1520_k2521`/`k1520_k3528`. Wächter
`tests/integration/test_k8915g2_boot.cpp` (`k1520_test_k8915g2_boot`, fast, 9 Fälle ≈ 1,5 s)
wie benannt, alle grün **ohne** Änderung an K2521/K3528/K7024/K5122/K7028; V3-Wächter
unverändert grün; voller `tools/dev.sh test` grün, `test-format` grün.
Befunde/Abweichungen:
- **Die Gen 2 läuft mit dem Abzug auf Anhieb**: ROM-Fehler `C` → 16 × BEL, 61H = 7FH → `CR` →
  Kaltstartmeldung → `CR` → der V3-Lader liest 901 → SCPX 8915 V5.3 meldet sich → `A>`, `dir`
  listet die Diskette, BIOS bei A8H = 87H. Mit geflickter Summe: ROM → KEY → CTC → SIO → RAM
  fehlerfrei ≈ 17 Mio. Takte (wie V3b §4 gerechnet), A8H am Ende 06H, `ED 45` bei 0066H im
  K3528-RAM. Ohne Prüfstecker `G` unter SIO, ohne Tastatur `A` unter KEY (wie V3).
- **Abweichung vom Plan (Wächter `RomFehlerC…`):** nach dem ROM-Fehler und `CR` steht A8H auf
  **0EH**, nicht 06H — der Stub (0400H) setzt 0EH vor dem Kopierer, 06H kommt erst nach dem
  RAM-Test, und den überspringt der erste Fehler. Folge von zre_rom.md §4/§5, kein
  Emulatorbefund; der Wächter prüft 0EH. Mit dem Abzug übergibt der Lader also mit 0EH, mit
  korrigiertem 177 mit 06H (beides vom BIOS vertragen, es schaltet selbst auf 87H).
- **F13 beobachtet:** der Autostart `rade` meldet „no RAM-device configurated or fatal
  RAM-error !!!“ (keine Bank 2), danach steht der Prompt stabil. Der Wächter prüft das bewusst
  nicht (F13 bleibt offen: was zeigt das Gerät?).
- `NmiImRomWirkungslos`: NMI unter „KEY“ (ROM bei 0000H) läuft einmal durch 0066H (`RETN`),
  Lampen und Selbsttest bleiben unberührt.
- `test-format`: keine Gen-2-Fälle darin; gelaufen wegen `K8915Format.*` (V3-Regression).

#### AP-V6b — Werkzeuge: `boot_trace`/`k1520dbg --machine k8915-g2`

**Ziel:** Gen 2 im Debugger und im Trace wie der V3. **Agent:** cpp-coder (Sonnet).
**Abhängig:** V6a. **Baut:** ja.

Eingaben: `tools/dbg_machine.h` (Z. 43–50 Maschinenwahl, Z. 104 `speicherbild(const K8915Zre&)`,
Z. 149 Erzeugung), `tools/k1520dbg.cpp` (alle `zre()`-Stellen: Z. 1224–1529, 2803–2810,
3118–3156, 3316), `tools/boot_trace_k8915.cpp`, `tools/boot_trace.cpp` Z. 321–329,
Referenzen `tools/k1520dbg.md` §11, `tools/boot_trace.md` §7; Wächter-Muster
`tests/cli/CMakeLists.txt` (`cli_dbg_k8915_*`, `cli_bt_k8915_*`), Listing
`doc/EPROMS/K8915G2/k8915g2_zre.prn`.

Auftrag: `--machine k8915-g2` in beiden Werkzeugen; `speicherbild(const K3528&)`; `map` zeigt A8H,
Bild, /MEMDI/MEMDI1; `bank` meldet „nicht vorhanden (Gen 2 ohne Bank 2)“; `ctc` beschriftet
„ZRE K2521, 80H-83H“; jede `zre()`-Stelle auf die Generation prüfen (sonst trifft die
`assert`-Vorbedingung aus V6a). `boot_trace --machine k8915-g2` tippt `CR` bei
Selbsttestfehler (61H = 7FH, Meldung „Selbsttestfehler <Test> <Buchstabe> → CR“) **und** nach
„\* Coldstart \*“ (`--no-cr` schaltet beides ab). Listing-Annotation:
`k8915g2_zre.prn@0xFC00:0021-03FF` (Kopie von 175) und `…:0400-0BFF` (unverschoben),
Beispiele in die beiden `.md`.

**Wächter:** `cli_dbg_k8915g2_all_commands_smoke` (Dispatch über alle Kommandos, Muster
`tests/cli/scripts/all_commands_smoke_k8915.dbg` + `tests/cli/cases/dbg_k8915_all_commands_smoke.cli`), `cli_bt_k8915g2_coldstart` (ohne Diskette: Meldung „ROM C“,
dann Kaltstartmeldung), `cli_bt_k8915g2_prompt` (901 bis stabiles `A>`), alle `cli_*_k8915_*` grün.
**Fertig, wenn:** `tools/dev.sh test` grün, Referenzen nachgetragen, Nachtrag hier, ein Commit.

#### AP-V7a — C-ABI und Python-Bindung

**Ziel:** Gen 2 aus Python erzeugbar. **Agent:** cpp-coder (Sonnet). **Abhängig:** V6a.
**Baut:** ja.

Eingaben: `core/api/k1520_api.{h,cpp}` (Muster `k1520_create_prg710` Z. 58–69 bzw.
`k1520_prg710_variant` Z. 759), `core/api/k1520_export.h` (`K1520_API`!),
`app/core_binding/k1520.py` (Z. 295–310 ctypes, Z. 519–528 `MACHINE_TYPES`/`PRG_VARIANTEN`,
Z. 770–830 Konstruktor), `tests/python/test_c_api.py` (gleicht Header ↔ Bibliothek ↔ ctypes
mechanisch ab), `tests/python/test_k8915_smoke.py`.

Auftrag: `k1520_create_k8915(int generation, d0..d3)` (0 = V3, 1 = Gen 2, sonst NULL + Grund
in `k1520_last_init_error`, Wert 2 mit dem Gen-1-Text aus R5); `k1520_k8915_generation(h)`;
`k1520_create(K1520_MACHINE_K8915)` bleibt V3. Python: `MACHINE_TYPES["k8915-g2"] = 2`,
`K8915_GENERATIONEN = {"k8915": 0, "k8915-g2": 1}`, Konstruktorzweig,
`K1520Emulator.k8915_generation()`. Danach in `app/` nach `== "k8915"` suchen: jede Stelle
muss `"k8915-g2"` mitnehmen oder über `machine_type() == 2` gehen.

**Wächter:** `py_c_api` (neue Funktionen in allen drei Schichten), neuer Fall in
`test_k8915_smoke.py`: `test_gen2_reaches_the_coldstart_message_after_rom_error_c`
(`K1520Emulator(machine="k8915-g2")`, `CR` aus zweitem Faden, Kaltstartmeldung über
`k1520_screen_char`, **nie** `mem_read`); `test_generation_two_of_the_k8915_is_refused_with_a_reason`
(Wert 2 → `ValueError`/NULL mit Text). **Fertig, wenn:** `tools/dev.sh test` grün (inkl.
Python-Ebene), Nachtrag hier, ein Commit.

#### AP-V7b — Programmprofil, Oberfläche, Handbuch

**Ziel:** Modellwahl im `k8915emu`. **Agent:** Sonnet (general-purpose; Python/Qt).
**Abhängig:** V7a. **Baut:** ja (Python-Ebene).

Eingaben: `app/profil.py` (K8915-Profil Z. 219–238, Vorbild PRG710 Z. 241–265, Felder
`modellwahl`/`modelle`/`modell_tipp`/`gesperrte_modelle` Z. 53–70), `app/ui/settings_widget.py`
Z. 221, `data/default_config_k8915.yaml`, `app/help/handbuch.md` (K8915-Teil),
`tests/python/test_k8915emu_gui.py`, `doc/design/18_k8915emu_oberflaeche.md`.

Auftrag: K8915-Profil `modellwahl=True`, `modelle` und `gesperrte_modelle` nach R5 (Tastatur
beider Modelle `k7672`), `modell_tipp` („Ein Wechsel erzeugt die Maschine neu (wie ein
Kaltstart)“). `default_config_k8915.yaml` bleibt **ohne** `general.model` (fehlend = V3).
Handbuch: Abschnitt Modellwahl K8915 (Gen 2: Selbsttest endet mit „ROM C“, weiter mit ⏎ —
Hinweis auf den Abzug, F9). **Kein Tastenkürzel.**
**Achtung, bestehender Wächter ändert seine Aussage:** `test_only_the_a5120_offers_the_a5120_16_model`
prüft heute `not w.profil.modellwahl` am K8915. Neu: der K8915 hat eine Modellwahl, aber
`a5120.16` in `k8915emu.yaml` wird zu `k8915`, `em_variant() == ""`, gespeichert wird
`model: k8915` — die Festlegung „EM nur am A5120“ bleibt, der Test wird entsprechend
umgeschrieben (Name bleibt).

**Wächter:** `test_the_k8915_offers_v3_and_gen2_and_shows_gen1_locked`,
`test_switching_the_k8915_model_rebuilds_the_machine` (`k1520_k8915_generation` 0 → 1),
`test_an_unknown_k8915_model_falls_back_to_v3`, angepasster
`test_only_the_a5120_offers_the_a5120_16_model`, Kürzel-Wächter unverändert grün.
**Fertig, wenn:** `tools/dev.sh test` grün, Nachtrag hier, ein Commit.

**V10/VT** wie unten; V10 nimmt zusätzlich die Festlegungen von R1/R3/R5 in
`doc/merkposten/k8915_varianten.md` auf (Wächter je Festlegung).

### V4–V9 (Rahmen — **ersetzt durch „AP-V2 — Ergebnis“**, nur noch zur Nachverfolgung)

| AP | Rahmen | Vorläufiges Fertig-Kriterium (Wächter-Familie) |
|---|---|---|
| V4 | Karte(n) für Speicher/RAM der Gen 2 unter `core/cards/` (Vorbild `prg710_speicher`, `zre8762`); Konfiguration als Config-Struct, **nicht** zur Laufzeit; Unit-Tests | `K8915Gen2Speicher.*` — Umschaltung A8H gegen die Belege aus V3b |
| V5 | Entweder Abzug bleibt Beigabe (ROM-Abbilder im Repo, Wächter auf MD5), oder steckbare Karte PFS K3820 (`installXxx` nach dem Anlegen, Vorgabe aus, wie RAF/K6022) | Entscheidung V2 |
| V6 | Maschinenzweig Gen 2 in `core/machines/k8915/`; Verdrahtung nach V3b; Boot der ROMs 175–177 bis zur Meldung („Coldstart“ / „No system disk, change disk“, **ohne Diskette prüfbar**) und danach mit Diskette bis zum Prompt, sobald F4 beantwortet ist; `boot_trace`/`k1520dbg --machine k8915 --generation 2` | `K8915Gen2Boot.*` (Integration), V3-Wächter bleiben grün |
| V7 | `k1520_create…` für die Variante, Python `K1520Emulator(machine="k8915", generation=…)`, Profil `k8915emu` (Modellwahl wie `general.model`, `modellwahl` in `app/profil.py`), Konfigurationsdatei, Handbuch; **`test_c_api.py` gleicht Header, Bibliothek und ctypes mechanisch ab** | `py_c_api`, `py_k8915emu_gui` |
| V8 | DiskTool: nur wenn die Gen-2-Diskette von V3-Profilen abweicht; sonst gestrichen | `DisktoolK8915Gen2.*` |
| V9 | Gen 1 = Gen 2 + **Tastatur K7634** (neu, `core/peripherals/`, nach V1b) + **K7028-Fassung** (V1b; Konfiguration an `core/cards/k7028/` bei unveränderter Vorgabe) ; ROM: **dasselbe Gen-2-ROM, wenn V3b zeigt, dass es die K7634 abfragt**, sonst bleibt der Betrieb ohne Gen-1-ROM offen | `K7634.*` (Unit), `K8915Gen1Tastatur.*` (Integration: Taste → Code im Gast, soweit das ROM sie liest) |

### AP-V10 / VT

**V10 (Sonnet):** `doc/merkposten/k8915_varianten.md` (Festlegungen mit Wächter, Muster
`prg710.md`), Absatz in `CLAUDE.md`, Zeile in `doc/merkposten/README.md`, Verweis aus Entwurf 16.
**VT (test-runner/Haiku):** vor dem Merge **alle vier Läufe** — `tools/dev.sh test`,
`test-format`, `test-matrix`, `win` (die langen im Hintergrund, nicht pollen); Ergebnis mit
bekannten Zeitüberläufen abgrenzen (`doc/merkposten`/Erfahrung: Last-Timeouts), roter Fall →
volle Ausgabe.

### Was nur der Anwender liefern kann (Blocker je AP)

| Blockiert | Braucht | Frage |
|---|---|---|
| V2 (Namen, Klasse) | Zuordnung Gen 1/2 ↔ V1/V2 | F1 |
| V4 (Speicherkarte) | Typ der RAM-Karte, Brücken, Brückenstand X8/X9 der Karte | F2, F3 |
| V6 (Boot mit Diskette), V8 | Systemdiskette der Gen 2 (Image) | F4 |
| V9 (Betriebsnachweis) | Gen-1-ROM, falls das Gen-2-ROM die K7634 nicht abfragt; SCPX-0/2-4.x-Diskette; Angabe der K7028-Fassung am Gerät (Platinennummer), falls Unterlagen fehlen | F5 |

---

## 8. Risiken

- **Gen 1 ohne eigenes ROM:** läuft das Gen-2-ROM die K7634 nicht an, bleibt der Betriebsnachweis
  offen (§3.2, §6). Kein ROM aus der Phantasie.
- **K2521 ≠ PRG-710-Speicherverwaltung:** die K2521 selbst kennt keine Seitenregister (E8H–EBH
  sind **PRG-spezifisch**, Karte `prg710_speicher`); Gen 2 braucht eine eigene Lösung.
- **Zwei Quellen, zwei PIO-Adressen:** K2521-Beschreibung 84H–87H, Forum 08H–0FH (§3.1) —
  die ROM-Abzüge entscheiden.
- **2708-Auslesen:** −5 V/+12 V nötig; Fehlablesungen sehen wie Emulatorfehler aus. Doppelte
  Abzüge mit MD5 sind Pflicht.
- **Namen:** „Generation“ ist kein Begriff der Quellen (F1). Ein falsch gewählter Name landet
  in Oberfläche, Konfiguration und Handbuch.

---

## 9. Fragen an den Anwender

| # | Frage |
|---|---|
| **F1** | Entspricht „Generation 2“ der „5¼″ V2“ (K7672, K2521, K3528, 045-8778?) und „Generation 1“ der „5¼″ V1“ (K7634)? Oder sind es andere Bezeichnungen (z. B. Aufschrift am Gerät)? |
| **F2** | Kartenliste des Gen-2-Geräts mit **Platinennummern** (012-/045-…), Steckplätzen und Tastaturtyp (K7634 PIO oder K7672 flach); Fotos beider Seiten der RAM- und der 2708-Karte. |
| **F3** | Wickelbrücken-Stand der ZRE (X6–X15) und der RAM-Karte; Aufschriften der drei ZRE-EPROMs und ihre Plätze. |
| **F4** | Laufwerke und Controller (K5122 oder anderer); eine Systemdiskette (Image) falls vorhanden. |
| **F5** | Gen 1 = Gen 2 + K7634 + K7028 (Ihre Angabe 2026-10-05): **Wissen Sie, welche K7028-Fassung** (.10/.20/.30, Platinennummer) und welches ROM dort steckt — gleiche Aufschriften 175–177? Darf der Betriebsnachweis bis zur Beschaffung einer Diskette/eines Abzugs warten (Empfehlung: ja)? Soll ich in den Foren (robotrontechnik.de, VzEkC) und beim Rechenwerk Halle anfragen? **Das ginge nur mit Ihrer Freigabe — ich schreibe nichts ohne Rückfrage in ein Forum.** |
| **F7** | Welche **K7634-Fassung** hängt am Gerät (Aufdruck `K7634.xx`, ROM-Nummer `Y708-I…` auf der Tastaturplatine)? Die Fremdquellen (BIOS Krzikalla, Kartenchip „3C00") erwarten TYP `A0H`, ENTER `9DH`, RESET `1FH`, PF1 `91H`; die gedruckten Tabellen (K7634.04/.05/.10/.13) haben das nicht. Steckt ein Bedienelement (BES, Sonderleitungen SL) an der Tastatur? (ohne: nur TYP, kein Einfluss auf den Emulator) |
| **F8** | Gibt es einen **Schaltplan der K7028.10/.20** (Portbereich `E0H–FFH`, Bedeutung von `E3H`/`E4H`, Frontplatte)? Steckt in der ZRE der Gen 1 der **Kartenchip „3C00"** (Urlader `MROM RAM I/O KEY CTC`, ENTER: LADER / OFF: ZYKL.) oder die Bausteine 175–177? Welche Platinennummer trägt die K7028 des Geräts? |
| **F6** | Der Name der Variante in der Oberfläche (z. B. „K8915 (Gen 2)“) und ob `k8915emu` die Modellwahl wie der A5120 erhält. *(AP-V2 arbeitet mit der Annahme [?]: Modellwahl ja, Anzeige „K8915 V3 (ZRE 045-8762, 128 KB)“ / „K8915 Gen 2 (ZRE K2521, 64 KB)“, Schlüssel `k8915` / `k8915-g2` — eine Umbenennung ändert nur Anzeigetexte, R5.)* |
| **F9** | Der Abzug **177** hat eine falsche 24-Bit-Summe (Byte 0A33H = 04H statt 00H, s. V3a). Bitte den Baustein 177 am Gerät **ein zweites Mal lesen** (MD5 vergleichen) und notieren, ob der „MROM“-Selbsttest des Geräts einen Fehler meldet. |
| **F20** | (V1a) Aufdruck/Platinen-Nr. der „zusätzlichen RAM-Karte“ (K3528? 32 Chips, 8212 „D8“?), Stand des Wickelfeldes **X3** (insb. D2:01/D2:02 → Register-Adresse A8H; Bank-Brücken X3:64–71). |
| **F21** | (V1a) Die handschriftliche „88“ im K3528-Plan: gilt sie für Ihr Gerät oder ein anderes (ROM schreibt A8H)? |
| **F22** | (V1a) Brückenstand am Gerät: ZRE X6–X9, X14/X15; PFS K3820 X6/X7 (welches MEMDI), X8/X9 (Startadresse), X10–X11 (WAIT). |
| **F23** | (V1a) Wie ist das Feld X3:23–X3:45 (Registerbit → /MEMDI…/MEMDI3) gebrückt? Foto der RAM-Karte genügt. |
| **F24** | (V1a) Woher stammt „PIO 08–0FH“ (Forum 5713)? Gibt es ein Gerät mit anderer K2521-Dekodierung? |
| **F10** | (V3b) Kartenchip „3C00“ spricht **B1H/B3H** (`0FH`, `40H`) und **E3H Bit 2** (RAM-Test 64 K oder nur 8000H ff.) an und prüft D001–D003H auf `F3 ED 5E` (sonst `76H` nach D000H). Welche Karten/Brücken der Gen 1 sind das? |
| **F11** | (V3b) Zu den Kartenchips „3C00“/„3000“ gehört ein **zweiter Lader-Baustein für 0400H** mit Arbeitszellen bei 0C00H und Stub bei 095BH (Platz 0400 der Karte ist leer, das ZRE-176 passt nicht). Gibt es diesen Baustein (z. B. in einem Gen-1-Gerät)? |
| **F12** | (V3b) Braucht der Selbsttest der Gen 2 am Gerät einen **Prüfstecker** an den drei Schnittstellen (Test „SIO“ wie beim V3, Entwurf 16 §6.10), und in welcher Reihenfolge zeigt das Gerät die Tests (erwartet ROM, KEY, CTC, SIO, RAM)? |
| **F13** | (V3b) Läuft auf dem Gen-2-Gerät `RADE` (RAM-Disk)? Ohne Bank 2 erwartet: nein; RADE erkennt die Maschine an 0C00H, dort liegt in der Gen 2 das K2521-RAM. *Emulator (V6a): „no RAM-device configurated or fatal RAM-error !!!“, danach `A>` — stimmt das mit dem Gerät überein?* |

---

## 10. Abzüge der Gen-2-Hardware (2026-10-05, AP-V3 vorbereitet)

Ablage, Prüfsummen und Einzelbefunde: **`doc/EPROMS/K8915G2/README.md`**. Kurzfassung:

- **ZRE: drei Bausteine 175/176/177 (0000/0400/0800, Reihenfolge aus dem Inhalt [?])** =
  Urlader: Einstieg DI/IM 2, `OUT (A8H)`, **kopiert sich nach FC00** und läuft dort; Selbsttest
  „MROM RAM SIO KEY CTC“; Floppy-Boot über Ports 10H–19H; Meldungen „Coldstart * Disk on A: ready“,
  „No system disk“. **Nicht** das V3-Boot-ROM (1007 von 1024 Bytes verschieden) und **nicht** die
  PRG-710-Lader — aber im Aufbau verwandt (A8H, 61H, Meldungen). Folgerung: die Gen-2-ZRE ist
  keine K2521 pur, sondern hat mindestens Speicherumschaltung über **A8H** wie die V3-ZRE
  [?, Port-/Bankbelegung in AP-V3 klären].
- **K7024: 171/172 sind byteidentisch mit `v171`/`v172`** (A5120-Zeichensatz) — nichts Neues,
  der vorhandene Zeichengenerator genügt.
- **2708-Karte PFS K3820: 14 von 16 Chips leer (FFH)**, zwei programmiert: „3C00“ = andere
  Fassung des Urladers (Selbsttest „I/O“, Code bei FC00), „3000“ = Lader-Meldungen wie ZRE 177,
  aber Arbeitszellen bei 0Cxx. Vermutung: eine **ältere/andere Urladerfassung** auf der Karte;
  Lage vermutlich ab C000 (Chip „3C00“ = FC00–FFFF) — **vom Brückenstand X8/X9 abhängig, am Gerät
  abzulesen** (Frage F3).
- Folge für den Plan: **V5 (2708-Karte)** schrumpft auf „Abzug als Beigabe, keine eigene
  Karte“, solange kein Gast sie anspricht; V3 bekommt als erste Aufgabe, die drei ZRE-Bausteine
  und die zwei Kartenchips zu vergleichen (Disassembly mit `tools/`, Muster
  `doc/EPROMS/PRG710/*.prn`).
