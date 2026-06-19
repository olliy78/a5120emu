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

### 2.3 Kommandos (K8025 → K7637)

| Byte | Funktion |
|------|---------|
| 00H | Software-RESET |
| 20H | Fehler-LED blinken an/aus |
| 44H | Akustisches Signal (~1s) |
| 52H | LED-Anzeigen ein/aus |
| 55H + xxH | Erweiterte LED-Steuerung |

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
| Escape (DELL) | 0xB3 | 0x1B (ESC) |
| Tab (\|<-\|) | 0x9F | 0x09 (TAB) |
| Delete (DELCH) | 0xBB | spcdel |
| Backspace | 0x08 | — (ASCII, durchgereicht) |
| Cursor ↑ / ↓ / ← / → | 0x94 / 0x95 / 0x96 / 0x97 | kcurup/kcurdw/kcurlf/kcurri |
| F1 … F8 | 0xC1 … 0xC8 | pf1c … pf8c |
| Ctrl+\<Taste\> | \<Taste\> & 0x1F | — (Steuercode <0x20, durchgereicht) |

> **ET1 ≠ Enter.** Die Haupt-Return-Taste (ET1, physisch **0xFF**) und die
> Ziffernblock-Enter-Taste (physisch **0xC0**) sind auf der echten K7637 zwei
> verschiedene Tasten: ET1 wird zu CR recodiert, Enter zur Funktion pf0c.
>
> **CTRL** ist real die ET2-Taste (physisch **0xFE** beim *Loslassen*, setzt ein
> Einmal-Flag für die nächste Taste). Das Modell nimmt die Abkürzung `Code & 0x1F`.

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

## 7. GUI-Tastatur-Widget (Python)

Die Python-GUI kann eine stilisierte Darstellung der K7637-Tastatur anzeigen:

```python
class KeyboardWidget(QWidget):
    """Visuelle Darstellung der K7637-Tastatur"""

    keyPressed  = Signal(int, bool, bool)  # qt_key, shift, ctrl
    keyReleased = Signal(int)

    def __init__(self):
        super().__init__()
        # Tastaturlayout aus Dokumentation (107 Tasten)
        self._buildLayout()

    def updateLEDs(self, led_state: dict):
        """Aktualisiert LED-Anzeigen (LOCK, Fehler, Selektoren)"""

    def _buildLayout(self):
        """Baut die Tastatur-Schaltflächen nach K7637-Layout"""
```

---

## 8. Testbarkeit

```python
# tests/python/test_k7637.py
def test_key_A_generates_code(k7637_lib, mock_sio):
    kb = k7637_lib.k7637_create()
    k7637_lib.k7637_connect(kb, mock_sio)
    k7637_lib.k7637_key_press(kb, Qt.Key_A, False, False)
    assert mock_sio.last_rx_byte == ord('a')  # Kleinbuchstabe ohne Shift

def test_key_A_shift_generates_uppercase(k7637_lib, mock_sio):
    kb = k7637_lib.k7637_create()
    k7637_lib.k7637_connect(kb, mock_sio)
    k7637_lib.k7637_key_press(kb, Qt.Key_A, True, False)  # Shift
    assert mock_sio.last_rx_byte == ord('A')

def test_repeat_after_delay(k7637_lib, mock_sio):
    kb = k7637_lib.k7637_create()
    k7637_lib.k7637_connect(kb, mock_sio)
    k7637_lib.k7637_key_press(kb, Qt.Key_A, False, False)
    count_before = mock_sio.rx_count
    k7637_lib.k7637_tick(kb, 600)  # 600ms gehalten
    assert mock_sio.rx_count > count_before + 1  # Wiederholung

def test_led_command(k7637_lib, mock_sio):
    kb = k7637_lib.k7637_create()
    k7637_lib.k7637_connect(kb, mock_sio)
    mock_sio.inject_tx(0x52)  # LED-Kommando
    k7637_lib.k7637_tick(kb, 10)
    leds = k7637_lib.k7637_get_leds(kb)
    # Nach 0x52: LED-Zustand geändert
```
