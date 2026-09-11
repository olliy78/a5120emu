# Feinentwurf: K7637 – Serielle Tastatur

**Modul:** `core/peripherals/k7637/`  
**Dateien:** `k7637.h`, `k7637.cpp`, `keytable.h`  
**Dokumentation:** `doc/trascripted/Serielle Tastatur K 7637.XX.md`

---

## 1. Aufgabe

Die K7637 ist kein K1520-Bus-Gerät, sondern ein **externes Peripheriegerät** mit eigenem Z80-Prozessor (U880), das über eine IFSS-Stromschleifen-Verbindung mit dem K8025 kommuniziert.

Im Emulator ersetzt die `K7637`-Klasse die gesamte Tastaturelektronik. Sie:
- übersetzt PC-Tastendrücke → K7637-Tastencodes
- kommuniziert mit dem K8025 SIO via simuliertem IFSS-Byte-Strom
- empfängt Kommandos (LED-Steuerung, Akustik) vom K8025

---

## 2. Kommunikationsprotokoll

### 2.1 IFSS-Schnittstelle (9600 Baud, 1+8+1)

- **Baudrate:** 9600 Bit/s
- **Format:** 1 Startbit + 8 Datenbits + 1 Stoppbit (kein Paritätsbit)
- **Physik:** Stromschleife (IFSS), galvanisch getrennt
- **Im Emulator:** Direktes Byte-Interface zum SIO-Kanal (kein Bit-Timing)

### 2.2 Tastencode-Übertragung (K7637 → K8025)

```
Tastendruck → Matrixscan (intern K7637) → 1 Byte Tastencode senden
```

- 1 Byte pro Taste (kein Breakcode)
- Wiederholfunktion: 500ms Verzögerung, dann alle 100ms
- Prellunterdrückung: 2 aufeinanderfolgende Matrixscans (~10.9ms)

### 2.3 Kommandos (K8025 → K7637) — erkannt an der **Flankenzahl**

Die Tastatur wertet **nicht den Bytewert** aus. Die Impulse der empfangenen
Bytes zählen einen auf 15 voreingestellten Binärzähler (D7:1) herunter; sein
Stand *ist* das Kommando (Handbuch §2.2.3: „lediglich die Anzahl der
Einzelimpulse … ist entscheidend"). Maßgeblich sind die **fallenden Flanken**
des seriellen Rahmens — Ruhepegel 1, Startbit 0, acht Datenbits LSB zuerst,
Stoppbit 1:

| Zählerstand | Kanonisches Byte | Wirkung |
|---|---|---|
| 14 | `00H` | Software-RESET → Grundzustand, alle Funktionsanzeigen aus |
| 13 | `20H` | Fehleranzeige G53 **blinken** an/aus (umschaltend); beim Einschalten ≈1 s Ton |
| 12 | `44H` | akustisches Signal ≈1 s |
| 11 | `52H` | Funktionsanzeige **G00** umschalten |
| 10 | `55H` | **Vorkommando** — das nächste Byte zählt weiter mit |
| 9 | `55H 00H` | Grundzustand herstellen |
| 8 | `55H 20H` | **G01** umschalten |
| 7 | `55H 44H` | **G02** umschalten |
| 6 | `55H 52H` | **G03** umschalten |
| 5 | `55H 55H` | **G04** umschalten |

> **Der Bytewert ist nur eine bequeme Schreibweise.** Jedes Byte mit derselben
> Flankenzahl löst dasselbe Kommando aus; die Tabelle nennt je Zählerstand nur
> das Byte, das die Systemsoftware üblicherweise schickt. Ein Modell, das
> `switch (byte)` rechnet, trifft die Hardware also nur zufällig. Wächter:
> `K7637.CommandDecoding_CountsFallingEdges` (rechnet alle neun Zählerstände der
> Handbuchtabelle nach) und `K7637.CommandDecoding_IgnoresTheByteValue`.

**Was CP/A daraus macht** (BIOS-Routine `kbdmd2`, im Listing ausdrücklich
„Routine fuer K7637"): Es schickt **je geändertem Bit** seines Lampenpuffers
(`lampbf`, 0x0040) ein Kommando — passend dazu, dass die Kommandos *umschalten*.
Die Zweibyte-Kommandos gibt es dabei als 55H **ohne** Warten auf die Quittung,
dann das zweite Byte mit Warten (`lmpout`) — genau die Ausnahme, die das
Handbuch für das Vorkommando nennt.

| `lampbf`-Bit | Bedeutung im CP/A | gesendet | Anzeige |
|---|---|---|---|
| 0 | Selektor 0 | `52H` | G00 |
| 1 | Selektor 1 | `55H 20H` | G01 |
| 2 | Selektor 2 | `55H 44H` | G02 |
| 3 | Selektor 3 | `55H 52H` | G03 |
| 7 | Hardcopy / INS-Modus | `55H 55H` | G04 |
| 6 | Fehlerlampe | `20H` | G53 (blinkt) |

Die Bits 4/5 (Zweibahndrucker) kennt die K7637-Routine nicht. Am laufenden CP/A
sind die fünf Funktionsanzeigen also **die vier Selektorlampen über den Tasten
`0 1 2 3` plus die INS-Modus-Lampe**.

> **Die andere Routine ist eine Falle.** `lmp34` (Adresse 0619) sendet den
> invertierten Lampenpuffer über einen Parallelport und gehört zu den Tastaturen
> K7604/06/34/36; `coi84l` („Typcode 80h") hängt an `km8454`, einer K7634-artigen
> Tastatur mit Typcode 80H — **nicht** an der K7637. Deren Eintrag `km37` trägt
> für die Lampen `coidum`, also *keine* Modifikation: es bleibt bei `kbdmd2`.

**Quittung.** Ein gültiges Kommando quittiert die Tastatur mit dem Zeichen TYP
(0x80). Das Modell quittiert **jedes** empfangene Byte — daran hängen die
Tastaturerkennung (`coityp`: Reset senden, Typcode erwarten) und der
LED-Handschlag (`lmpout` wartet nach jedem Byte).

### 2.4 Die acht Anzeigen

| Anzeige | Ort | Wer schaltet sie |
|---|---|---|
| **G00…G04** | die fünf über den Selektortasten | der Rechner, mit den fünf LED-Kommandos (**umschaltend**, nicht setzend) |
| **G53** | rechts in derselben Leiste | der Rechner, Kommando `20H` — sie **blinkt**, solange eingeschaltet |
| **E54** | neben dem Blindplatz der Einschalttaste | die Betriebsspannung: leuchtet, solange die Tastatur versorgt ist, und bleibt es auch im Grundzustand |
| **C99** | links neben dem Umschaltfeststeller | die Tastatur selbst — sie folgt dem LOCK-Zustand, kein Kommando |

Die Zuordnung G00…G04/G53 zu den Kommandos steht im ROM (Adressen 4A0H…4A4H)
und ist je Tastaturvariante anders; hier gilt die Standardbelegung aus dem
Handbuch. **Welcher Leuchtpunkt am Gerät welche Position trägt**, sagt das
Handbuch nicht; die Zuordnung oben ist erschlossen: G00…G04 sitzen als Gruppe
über den Selektortasten, E54 liegt laut Handbuch neben der Einschalttaste
(Tastenposition E53,5 — am Auftischgerät der Blindplatz rechts in der
Ziffernreihe), C99 ist als LOCK-Anzeige benannt (Feststeller = C00).

---

## 3. Tastencodes (Code-Tabellen)

Die K7637 hat 3 Code-Tabellen (CTAB0, CTAB1, CTAB2) mit je Normal-/Shift-Belegung.

**Auswahl der Code-Tabelle:** Via Brücken E2:1 und E2:2 im K7637-EPROM.

**Standard A5120 (CTAB0):** Deutsche Tastenbelegung.

Da das K7637-EPROM (2KB) nicht vorliegt, werden die Tastencodes aus der Dokumentation und dem A5120-BIOS rekonstruiert. Die Tabellen werden in `keytable.h` hart kodiert:

```cpp
// keytable.h – Rekonstruierte K7637 Code-Tabelle
// Quelle: K7637-Dokumentation + CPA-BIOS Analyse (z80_disasm3.py)

// CTAB0a: Normal (kein Shift)
static constexpr uint8_t CTAB0_NORMAL[128] = {
    // Zeile 0 (Tasten 0x00-0x0F)
    0x00, 0x1B, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '+', 0x08, 0x00, 0x00,
    // ... weitere Zeilen
};

// CTAB0b: Shift
static constexpr uint8_t CTAB0_SHIFT[128] = {
    // ...
};

// PC-Keycode → K7637-Scancode Mapping (für Emulation)
struct PCKeyMap {
    int     qt_key;        // Qt::Key_xxx
    uint8_t normal_code;   // K7637-Code ohne Shift
    uint8_t shift_code;    // K7637-Code mit Shift
};
static constexpr PCKeyMap KEY_MAP[] = {
    { Qt::Key_Escape, 0x1B, 0x1B },
    { Qt::Key_1,      '1',  '!' },
    { Qt::Key_A,      'a',  'A' },
    // ...
    { Qt::Key_F1,     0xF1, 0xF1 },  // Funktionstasten
    // ...
};
```

---

## 4. Klassen-Interface

```cpp
class K7637 {
public:
    // Verbindung mit K8025 SIO-Kanal
    void connect(Z80SIO::Channel& sio_channel);

    // ─── PC-Tastatureingabe → K7637 ──────────────────────────────

    // Tastendruck (Qt-Keycode)
    void keyPress(int qt_keycode, bool shift, bool ctrl);
    void keyRelease(int qt_keycode);

    // CLI-Modus: Zeichen direkt injizieren (ASCII → Tastencode)
    void consoleChar(char c);

    // ─── Kommandos K8025 → K7637 ─────────────────────────────────
    // (werden automatisch durch SIO-Empfang verarbeitet)

    // Aktueller LED-Zustand (für GUI-Darstellung)
    struct LEDState {
        bool lock;      // LOCK-LED (C99)
        bool error;     // Fehler-LED (G53, blinkend)
        bool sel[5];    // Selektor-LEDs G00-G04
    };
    LEDState getLEDState() const;

    // Akustik-Status (für GUI: kann Ton abspielen)
    bool isBeeeping() const;

    // ─── Takt (für Wiederholrate und Blinken) ────────────────────
    // Muss regelmäßig aufgerufen werden (z.B. alle 10ms)
    void tick(int ms_elapsed);

private:
    Z80SIO::Channel* sio_ = nullptr;

    // Aktueller Zustand
    bool     shift_down_  = false;
    bool     ctrl_down_   = false;
    bool     lock_on_     = false;
    int      repeat_key_  = -1;      // Aktuell gehaltene Taste
    int      repeat_timer_ = 0;
    int      repeat_delay_ = 500;    // ms Verzögerung
    int      repeat_interval_ = 100; // ms Wiederholung
    LEDState leds_{};
    bool     beeping_    = false;
    int      beep_timer_ = 0;

    // Kommando-Dekodierung K8025 → K7637
    bool     expect_second_byte_ = false; // Nach 55H
    uint8_t  first_cmd_byte_ = 0;

    // Hilfsfunktionen
    uint8_t  translateKey(int qt_keycode, bool shift);
    void     sendCode(uint8_t code);
    void     processCommand(uint8_t byte);
};
```

---

## 5. PC-Tastatur → K7637-Tastencode Übersetzung

### 5.1 Sondertasten (A5120-spezifisch)

Die K7637 sendet den **physischen** Tastencode aus ihrer ROM-Codetabelle; der
A5120-BIOS-Treiber recodiert die hohen Codes (≥0x80 sowie 0xFF/0xFE) über die
Tabelle **`cp37`** (`CPA_Workbench/.../bioskbdc.mac`) auf seine virtuellen Codes.
Druckbares ASCII (0x20–0x7E) steht **nicht** in `cp37` und wird unverändert
durchgereicht. `translateKey()` liefert daher den physischen Code — nicht einen
vor-übersetzten ASCII-Wert, sonst fallen physisch verschiedene Tasten zusammen.

| PC-Taste | physischer K7637-Code | cp37 → BIOS-Code |
|----------|-----------------------|------------------|
| **Return (Haupttaste = ET1)** | **0xFF** | 0x0D (CR) |
| **Enter (Ziffernblock)** | **0xC0** | pf0c (≠ CR!) |
| Escape | 0x1B | — (ASCII, durchgereicht) |
| Tab (\|<-\|) | 0x9F | 0x09 (TAB) |
| Delete (DELCH) | 0xBB | spcdel |
| Backspace | **0xBB** (DEL CH) | spcdel |
| Cursor ↑ / ↓ / ← / → | 0x94 / 0x95 / 0x96 / 0x97 | kcurup/kcurdw/kcurlf/kcurri |
| F1 … F8 | 0xC1 … 0xC8 | pf1c … pf8c |
| Ctrl+\<Taste\> | \<Taste\> & 0x1F | — (Steuercode <0x20, durchgereicht) |

> **ET1 ≠ Enter.** Die Haupt-Return-Taste (ET1, physisch **0xFF**) und die
> Ziffernblock-Enter-Taste (physisch **0xC0**) sind auf der echten K7637 zwei
> verschiedene Tasten: ET1 wird zu CR recodiert, Enter zur Funktion pf0c.
>
> **Esc und Rückschritt liegen auf den Tasten, die im Gast auch das tun** —
> am laufenden CP/A nachgemessen (0xBB löscht ein Zeichen rückwärts, die
> ESC-Taste schickt 0x1B). Früher ging Escape auf 0xB3 (DEL L), was cp37
> ebenfalls zu ESC macht; auf der Bildschirmtastatur leuchtete dann aber die
> falsche Taste auf, und ein anderes Betriebssystem kodiert 0xB3 anders.
> **Ein Code unter 0x20 muss als Rohcode kommen**: `translateKey` reicht nur
> 0x20…0x7E durch, alles darunter fällt sonst still unter den Tisch (daran war
> die ESC-Taste der Nachbildung wirkungslos). Wächter:
> `test_every_code_survives_the_core_translation`.
>
> **Tab gehört dem Gast, nicht dem Fokuswechsel**: Qt fragt
> `focusNextPrevChild()` VOR `keyPressEvent` und verschluckt die Taste sonst —
> Bildschirm und Bildschirmtastatur antworten dort mit `False`.

> **CTRL** ist real die ET2-Taste (physisch **0xFE** beim *Loslassen*, setzt ein
> Einmal-Flag für die nächste Taste). Das Modell nimmt die Abkürzung `Code & 0x1F`.

### 5.2 Rohcodes — der Weg für Tasten, die der PC nicht hat

Die Qt-Abbildung oben reicht nur so weit, wie eine PC-Tastatur reicht. Die
Bildschirmtastatur (`app/ui/keyboard.py`) bildet aber die **ganze** K7637 nach,
und die hat Tasten, für die es keine Qt-Taste gibt: CE, SEL 0…3, PA 1…3, CLEAR,
REC, FM, DUP, EREOF, ERINP, PF 9…PF 12, MON, RESET, `00`, die vier zusätzlichen
Kursortasten. Dafür gibt es den **Rohcode-Fluchtweg**:

```
K7637::QK_RAW_BASE | <Byte>     (QK_RAW_BASE = 0x0200_0000)
```

`translateKey()` erkennt den Bereich als erstes und sendet das Byte unverändert
— insbesondere **ohne** die `& 0x1F`-Rechnung, die nur für druckbares ASCII
gilt (sonst käme PF 1 = 0xC1 mit gedrücktem CTRL als 0x01 an). Der Bereich liegt
über den `Qt::Key_*`-Werten (0x0100_0000), kollidiert also mit nichts.

Wächter: `K7637.RawCode_IsSentVerbatim`, `K7637.RawCode_IgnoresCtrl`; auf der
Python-Seite vergleicht `test_keyboard_layout.py::test_raw_base_matches_the_core`
die Konstante mechanisch mit dem Kern-Header.

---

## 6. IFSS-Verbindung im Emulator

Da im Emulator keine analoge Stromschleife existiert, wird die Verbindung über
die SIO-API simuliert. **Wichtig: die 9600-Baud-Laufzeit wird modelliert** — ein
Byte (Tastencode *oder* Typcode-Quittung) erscheint **nicht** im selben Befehl im
SIO-RX, sondern erst nach einer Byte-Zeit (≈2604 ZVE1-Takte bei 2,5 MHz).
`service(now_cycles)` (pro Instruktion aus der Run-Loop aufgerufen) gibt fällige
Bytes frei; `sendByte()` reiht sie nur mit Freigabe-Zeitpunkt ein.

> **Warum die Laufzeit nötig ist (Bug 2026-06):** Ohne Latenz erschien die
> Typcode-Quittung (0x80) auf ein LED-Kommando sofort. Dann konkurrieren der
> Tastatur-Scan in der Timer-ISR und der Vordergrund-LED-Handshake um dasselbe
> SIO-RX-Byte; der Verlierer liest einen leeren FIFO (0xFF → vom BIOS zu CR
> recodiert) und flutet den Tastaturpuffer (0xF6D9, 20 B), bis er überläuft und
> echte Tasten verwirft → am CCP kam **keine Eingabe** an. Mit der Byte-Latenz
> holt der aktiv pollende Handshake seine Quittung ab, die ISR sieht das Byte
> noch „in Übertragung" und liest es nicht — kein Race, sauberer Puffer.

```cpp
void K7637::sendByte(uint8_t code) {
    // Serialisieren: Byte wird erst nach einer 9600-Baud-Byte-Zeit zugestellt,
    // Bytes hintereinander.  service() gibt sie zum Freigabe-Zeitpunkt frei.
    uint64_t start   = std::max(cur_cycle_, next_tx_cycle_);
    tx_queue_.push_back({start + SERIAL_BYTE_CYCLES, code});
    next_tx_cycle_   = start + SERIAL_BYTE_CYCLES;
}

void K7637::tick(int ms_elapsed) {
    // Wiederholrate verwalten
    if (repeat_key_ >= 0) {
        repeat_timer_ += ms_elapsed;
        if (repeat_timer_ >= repeat_delay_) {
            repeat_timer_ = repeat_delay_ - repeat_interval_;
            repeat_delay_ = repeat_interval_;
            sendCode(translateKey(repeat_key_, shift_down_));
        }
    }
    // Befehle von SIO lesen (LED-Steuerung)
    while (sio_ && sio_->txAvailable()) {
        processCommand(sio_->txGet());
    }
    // Beep-Timer
    if (beeping_) {
        beep_timer_ -= ms_elapsed;
        if (beep_timer_ <= 0) beeping_ = false;
    }
}
```

---

## 7. Bildschirmtastatur (`app/ui/keyboard.py`)

Die GUI zeigt die Tastatur als **maßstäbliche Nachbildung** der K7637.50
(Standard-Latein, US-Anordnung) — die Vorlage ist ein Foto der echten Tastatur,
das Rastermaß daran abgemessen. Nicht aus Knöpfen zusammengesetzt, sondern
gezeichnet (`paintEvent`): runde Kappen im quadratischen Schacht, Ovale für
SHIFT/ET1/ET2/Leertaste, der dreireihige ENTER-Balken und die Doppelbeschriftung
(oben Umschaltebene, unten Grundebene) sind mit Widgets nicht sinnvoll
nachzubauen. Farben schwarz/weiß/rot wie am Original; der Codierstecker am
rechten Rand fehlt (er wirkt nur unter SIOS).

Ein Klick sendet `keyPressed(keycode, shift, ctrl)`, das Loslassen
`keyReleased` — gedrückt gehalten läuft also die Tastenwiederholung des
emulierten K7637 an. SHIFT und CTRL/ET2 wirken auf genau die nächste Taste,
LOCK bleibt gesetzt und wird (wie am Original) mit SHIFT aufgehoben.

### 7.1 Die Tastencodes des Tastenfelds

Alle Sondercodes stammen aus der BIOS-Umkodiertabelle **`cp37`** (Listing
`disks/cpa_cpa780_*.prn`) — sie ist die einzige vorliegende Quelle für die
physischen Codes, das Tastatur-EPROM fehlt.

| Taste(n) | Grundebene | Umschaltebene |
|----------|-----------|---------------|
| `0` `1` `2` `3` (links oben) | SEL 0…3 = 0xA0…0xA3 | — |
| INS MD / INS L | 0xA8 | 0x93 |
| DEL CH / DEL L | 0xBB | 0xB3 |
| PF 1…PF 3 / PA 1…PA 3 | 0xC1…0xC3 | 0xFA / 0xF9 / 0xF8 |
| PF 4 | 0xC4 | — |
| PF 5…PF 8 / CLEAR, REC, FM, DUP | 0xC5…0xC8 | 0xFC, 0xFD, 0xBE, 0xBC |
| PF 9 (POWER) | 0xC9 | — (am Auftischgerät ohne Funktion) |
| PF 10, PF 11 / EREOF, ERINP | 0xCA, 0xCB | 0x98, 0x99 |
| PF 12 | 0xCC | — |
| RESET, M (MON) | 0xAF, 0xB0 | — |
| Kursor ↑ ↓ ← → | 0x94, 0x95, 0x96, 0x97 | — |
| `|←|` (Tab), `|←`, `→|`, `↰`, `↵` | 0x9F, 0x9B, 0x91, 0x9C, 0x9A | — |
| CE | 0xB9 | — |
| ET1 / ENTER (Ziffernblock) | 0xFF / 0xC0 | — |
| `00` | 0xB1 | — |
| Zeichentasten, Ziffernblock | ASCII | ASCII |

> **Drei Tasten sind bewusst unbelegt bzw. behelfsmäßig belegt.** **PRINT** und
> **HLT** stehen in keiner vorliegenden Codetabelle — sie werden gezeichnet,
> senden aber nichts (ein erfundener Code löste im Gast Unsinn aus). Die rote
> **`−`** des Ziffernblocks sendet ASCII `-`; ihr echter Code ist ebenfalls
> unbekannt, `cp37` führt INS MD ausdrücklich als „Ersatz num. Minus". Die weiße
> **ESC**-Taste sendet 0x1B (ASCII, wird durchgereicht) — im CP/A ist ESC sonst
> über DELL erreichbar („DELL als Ersatz ESC").

### 7.2 Beschriftung der Ziffernreihe

Die Umschaltebene ist **bitgepaart** (Shift löscht Bit 0x10): `1`→`!`, `2`→`"`,
… `9`→`)`, und ebenso `-`→`=`, `;`→`+`, `:`→`*`, `,`→`<`, `.`→`>`, `/`→`?`. Zwei
Beschriftungen sind auf dem Foto nur als Balken lesbar und wurden über die
Vollständigkeit des ASCII-Vorrats erschlossen: `0` trägt oben `_` (0x5F), `^`
trägt oben `‾` (0x7E, im A5120-Zeichensatz als Überstrich gezeichnet); `¤` auf
der `4` ist die Darstellung von 0x24. Wächter ist
`test_keyboard_layout.py::test_ascii_set_is_complete`: jedes druckbare
ASCII-Zeichen muss auf genau einer Taste erreichbar sein.

### 7.3 Aufbau: die Tastatur ist modular

Das Tastenfeld besteht aus Modulen im Rastermaß, und nicht jedes trägt eine
Taste. **Der Versatz der Reihen gegeneinander entsteht durch diese Module, nicht
durch breitere Tasten** — genau daran war die erste Fassung falsch: CTRL und
`→|` sind je *eine* Taste breit, und vor `→|` sitzt ein halbes Blindmodul. So
steht die `1` der Ziffernreihe genau unter der `1` der Funktionsreihe und PF 4
über der `9`, und zwischen PF 12 und RESET klafft keine Lücke. Die Buchstaben
laufen wie auf jeder Schreibmaschine schräg nach rechts unten (1 → Q → A → Z);
rechts fangen die etwas breitere **CE**-Taste und die breiten
**Umschalttasten** den Versatz auf, damit der Ziffernblock rechteckig bleibt.
Die linke Umschalttaste ist aus Symmetriegründen ebenso breit wie die rechte und
ragt dadurch links aus dem Raster heraus.

**Ein Ausschnitt in Form des Tastenblocks.** Das Blech hat **ein** Loch über
*alle* Reihen — auch zwischen Funktions- und Ziffernreihe liegt keines, dort ist
derselbe schmale Spalt wie zwischen allen anderen Reihen. Der Ausschnitt ist
**kein Rechteck**, sondern folgt dem Umriss des Tastenblocks (die linke
Umschalttaste ragt heraus, der Ziffernblock endet eine halbe Taste vor der
Betriebsanzeige): es gibt keine großen schwarzen Flächen, nur den schmalen
schwarzen Spalt ringsum. Gezeichnet wird er als **Vereinigung aller Zellen**
(`QPainterPath.simplified()`), deren Ecken ein Strich mit rundem Gehrungsstoß
rundet — die Zellen werden dafür vorher um denselben Betrag geschrumpft, damit
der Umriss sie genau abdeckt. In den Spalten sieht man den schwarzen Grund
(≈1,5 mm breit).

**Der Aufbau einer Taste** ist zweistufig und je nach Farbe verschieden:

| Tastenart | tieferliegende Ebene | Kappe |
|---|---|---|
| schwarz | graue Fassung, rechteckig mit runden Ecken | runde schwarze Kappe |
| rot | Fassung **rot mit Grauschleier** (das Teil ist aus rotem Kunststoff) | runde rote Kappe |
| hell | **keine** — ringsum gleich der schwarze Grund | abgerundetes Rechteck |
| Blindmodul | nur die Fassung | keine | Kursor- und Ziffernblock schließen
direkt an den Buchstabenblock an: die Buchstabenreihen enden bei Raster 13.5,
dort beginnt der Kursorblock, und bei 15.5 der rechteckige Ziffernblock (vier
Spalten bis 19.5 — eine halbe Tastenbreite links der Betriebsanzeige, die bis
20 reicht).

Die Nachbildung führt die Blindelemente mit:

- **halbbreite Module mit Anzeige**: links neben dem Umschaltfeststeller (C99)
  und am rechten Ende der Ziffernreihe (E54, dort wo die Einbauvariante ihre
  Einschalttaste hat — Tastenposition E53,5),
- **halbbreite Blindmodule** ohne alles: links und rechts der Leertaste und
  rechts neben ET1 (dieses schließt mit der rechten Umschalttaste und der
  `]`-Taste ab),
- eine **volle Blindtaste** links unten und eine rechts neben ESC, dort mit
  einem **halben** Blindmodul davor,
- ein **Viertelmodul** vor der `→|`-Taste, das den Versatz der QWERTY-Reihe
  macht.

Die fünf Funktionsanzeigen sitzen frei in der Wanne, **mittig über SEL 0…3 und
INS MD** — genau über den Tasten, deren Lampen CP/A dort schaltet (§2.3); die
Fehleranzeige **über der RESET-Taste**. Gezeichnet werden die Dioden
zuletzt, sonst verdeckte sie ihr eigenes Modul. Es sind 5-mm-Dioden mit hellem,
milchigem Gehäuse: aus hellgrau, an rot — auf dem Foto ist keine von ihnen rot
eingefärbt.

### 7.4 Anzeigen

`k1520_keyboard_leds` liefert die Bitmaske (Bit 0…4 = G00…G04, Bit 5 =
Fehleranzeige, Bit 7 = Ton läuft); `MainWindow._run_emulator` holt sie je Bild
ab und gibt sie an `KeyboardWidget.set_leds()`, das nur bei echter Änderung neu
zeichnet. Das **Blinken** der Fehleranzeige macht die Oberfläche (Zeitgeber,
500 ms) — der Kern kennt keine Wanduhr, er meldet nur „blinkt". Die
Betriebsanzeige hängt am Netzschalter des Fensters (`set_powered`), die
LOCK-Anzeige am Feststeller der Bildschirmtastatur selbst.

### 7.5 Die echte Tastatur wird mitgezeigt

Jede Host-Taste geht durch die Nachbildung: `MainWindow` setzt
`ScreenWidget.key_sink = keyboard_widget`, und `ScreenWidget._map_key` reicht
jedes Tastenereignis an `host_key_press`/`host_key_release` weiter — auch die
reinen Modifikatoren, die der Kern nie sieht (sonst ließe sich ein gehaltenes
Strg nicht darstellen). Drei Dinge passieren dort:

- **Hervorheben.** `_keys_for_host_event` sucht die Taste der Nachbildung, die
  der Kern tatsächlich anspricht — über den Code, nicht über die Beschriftung.
  Die Tabelle `_HOST_SPECIAL` muss deshalb `K7637::translateKey` entsprechen
  (Return → ET1 0xFF, Enter → 0xC0, Tab → 0x9F, Esc → DEL L 0xB3, …), sonst
  leuchtet etwas anderes auf, als gesendet wird. Ziffern gibt es zweimal; den
  Ausschlag gibt `Qt::KeypadModifier`.
- **Feststeller lesen.** Qt meldet den Zustand der Feststelltaste nicht. Er
  lässt sich aber an jedem Buchstaben ablesen: Großbuchstabe *ohne*
  Umschalttaste heißt festgestellt (`_note_host_caps`, korrigiert sich bei jedem
  weiteren Buchstaben selbst); die Feststelltaste selbst kippt ihn sofort mit.
- **Zwei Feststeller auseinanderhalten.** Der Feststeller der *Nachbildung*
  (`_lock`) schaltet sie auf die **Umschaltebene** — dieselbe Taste schickt dann
  DEL L statt DEL CH, PA 1 statt PF 1, wie am Original. Der erkannte Feststeller
  der *echten* Tastatur (`_host_caps`) tut das **nicht**: er macht dort auch nur
  aus Buchstaben Großbuchstaben, die Rücktaste bleibt die Rücktaste. Wer beides
  vermischt, bekommt zwei Eingabewege, die für dieselbe Taste verschiedene Codes
  schicken (Wächter `test_host_key_and_click_agree`).
- **Feststeller wirken lassen.** Die Gegenrichtung — den Feststeller der echten
  Tastatur *einschalten* — kann ein Programm nicht: kein Betriebssystem gibt
  diesen Zustand für die ganze Maschine frei (unter Wayland gar nicht, sonst nur
  über systemweite Eingriffe, die jedes andere Fenster mitbeträfen). Sie ist
  auch nicht nötig: `map_host_key` setzt den Buchstaben selbst um, sobald der
  Feststeller der Nachbildung gesetzt ist — der Gast bekommt Großbuchstaben,
  genau das war der Zweck. Ebenso wirken angeklicktes SHIFT/CTRL auf die nächste
  Taste der echten Tastatur, und Umschalt+F1 schickt PA 1 statt PF 1.

Bleibt eine Taste hängen (Fokuswechsel, während sie gedrückt ist), räumt
`ScreenWidget.focusOutEvent` → `clear_host_keys()` auf.

### 7.6 Größe im Dock

Das Widget ist maßstabstreu und kennt sein Seitenverhältnis
(`heightForWidth`); `MainWindow._shrink_keyboard` setzt die Dock-Höhe danach,
damit die Tastatur die Breite der linken Spalte genau ausfüllt.

---

## 8. Wächter

| Was | Wo |
|-----|----|
| Tastencodes, Ctrl, ET1≠ENTER, Kursor, Wiederholung, LED-Kommandos, Byte-Laufzeit | `tests/unit/peripherals/test_k7637.cpp` |
| Rohcode-Weg (§5.2) | `K7637.RawCode_IsSentVerbatim`, `K7637.RawCode_IgnoresCtrl` |
| Kommandodekodierung über Flankenzählung (§2.3) | `K7637.CommandDecoding_CountsFallingEdges`, `K7637.CommandDecoding_IgnoresTheByteValue`, `K7637.PreCommand_NeedsTheSecondByte` |
| Anzeigen und Ton (§2.4) | `K7637.LedCommands_ToggleTheirDisplay`, `K7637.ErrorDisplay_TogglesAndBeepsWhenSwitchedOn`, `K7637.BeepCommand_RunsForAboutOneSecond`, `K7637.ResetCommand_ClearsAllDisplays`, `K7637.EveryCommandByteIsAcknowledged` |
| Host-Taste → Kern-Keycode | `tests/python/test_keyboard_map.py` |
| Tastenfeld der Bildschirmtastatur (Codes, Umschaltebene, ASCII-Vollständigkeit, keine überlappenden Tasten) | `tests/python/test_keyboard_layout.py` |
| Mitzeigen der echten Tastatur (§7.5): Hervorhebung, Sondertasten, gehaltene Modifikatoren, Ziffernblock, Feststeller in beide Richtungen | `tests/python/test_keyboard_layout.py` (`test_host_*`, `test_onscreen_lock_uppercases_host_keys`) |
| Tastatur am laufenden System | `tests/python/test_boot_smoke.py::test_keyboard_input_reaches_the_machine` |
