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
| **V2** | Entwurf Maschinenform + Auftragsschärfung V4–V9 | V1a, V1b, V3b, F1–F3 | Plan / Opus | nein |
| V4 | Karten: Speicherverwaltung/RAM der Gen 2 | V2 | cpp-coder / Sonnet, Opus für die Umschaltung | ja |
| V5 | 2708-Karte als Beigabe (nur Abzug) oder optionale Karte | V3b | Entscheidung in V2 | ja |
| V6 | `K8915Machine`-Zweig Gen 2, Boot bis Meldung | V4 | Opus | ja |
| V7 | C-ABI, Python, Programmprofil, Handbuch | V6 | cpp-coder + Sonnet | ja |
| V8 | DiskTool: Diskettenformat Gen 2 | V6, F4 (Diskette) | cpp-coder / Sonnet | ja |
| V9 | Gen 1 als Konfigurationsvariante: K7634 + K7028-Fassung auf Gen 2 | V6, V1b, V3b (ROM-Frage) | cpp-coder / Sonnet | ja |
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

### AP-V2 — Entwurf und Schärfung der Kern-APs

**Agent:** Plan (Opus). **Abhängig:** V1a, V1b, V3b, Antworten F1–F3. **Baut:** nein.
Ergebnis in diesem Dokument: (1) **Entscheidung eine Klasse** (`K8915Machine::Config::generation`)
**oder** eigene Klasse — Empfehlung bleibt eine Klasse, bei Abweichung begründen; (2) Steckplatz-
und Kartenliste je Variante; (3) Port-/Speicherkarte; (4) **V4–V9 als vollständige Aufträge** im
Muster von V3a (Ziel, Eingaben mit Dateipfaden, Ergebnis, Wächtern mit Namen, Fertig-Kriterium),
V5 entschieden (Beigabe oder Karte); (5) Namen in Oberfläche/Konfiguration (F6). **Fertig,
wenn** ein Agent V4 ohne Rückfrage beginnen kann.

### V4–V9 (Rahmen — wird in V2 geschärft)

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
| **F6** | Der Name der Variante in der Oberfläche (z. B. „K8915 (Gen 2)“) und ob `k8915emu` die Modellwahl wie der A5120 erhält. |
| **F20** | (V1a) Aufdruck/Platinen-Nr. der „zusätzlichen RAM-Karte“ (K3528? 32 Chips, 8212 „D8“?), Stand des Wickelfeldes **X3** (insb. D2:01/D2:02 → Register-Adresse A8H; Bank-Brücken X3:64–71). |
| **F21** | (V1a) Die handschriftliche „88“ im K3528-Plan: gilt sie für Ihr Gerät oder ein anderes (ROM schreibt A8H)? |
| **F22** | (V1a) Brückenstand am Gerät: ZRE X6–X9, X14/X15; PFS K3820 X6/X7 (welches MEMDI), X8/X9 (Startadresse), X10–X11 (WAIT). |
| **F23** | (V1a) Wie ist das Feld X3:23–X3:45 (Registerbit → /MEMDI…/MEMDI3) gebrückt? Foto der RAM-Karte genügt. |
| **F24** | (V1a) Woher stammt „PIO 08–0FH“ (Forum 5713)? Gibt es ein Gerät mit anderer K2521-Dekodierung? |

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
