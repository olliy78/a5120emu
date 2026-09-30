/**
 * @file k7672.cpp
 * @brief Tastatur K7672.03 — Protokollmodell (SCP-Modus, Befehlsfolgen, 9600 Bd).
 * @see k7672.h, doc/EPROMS/K7672/README.md
 */

#include "core/peripherals/k7672/k7672.h"
#include "core/logger.h"
#include <algorithm>
#include <array>

namespace {

constexpr uint8_t BEL = 0x07, DC1 = 0x11, DC3 = 0x13, ESC = 0x1B;
constexpr uint8_t LED_13 = 0x01, LED_FREI = 0x08;   // Firmware-Register 21H

// Befehlstabelle der Firmware bei 06F8H — die Zeichen NACH dem ESC.  Die Einträge
// hinter den Präfixen `ESC [?` und `ESC [2` sind hier ausgeschrieben.
constexpr std::array<const char*, 25> kBefehle = {
    "c", "[2;0y", "[2;1y", "[?2;1y", "[?2;2y", "[?2;4y", "[c", "Z", "[5n",
    "[?2l", "<", "[4h", "[3l",
    "[?11h", "[?11l", "[?12h", "[?12l", "[?13h", "[?13l", "[?18h", "[?18l",
    "[?19h", "[?20h", "[?21h", "[?22h",
};

// Qt::Key_*-Werte (ohne Qt-Kopfdateien), wie in K7637.
constexpr uint32_t QK_ESCAPE = 0x01000000, QK_TAB = 0x01000001, QK_BACKTAB = 0x01000002,
                   QK_BACKSPACE = 0x01000003, QK_RETURN = 0x01000004, QK_ENTER = 0x01000005,
                   QK_DELETE = 0x01000007;
constexpr uint32_t QK_RAW_BASE = 0x02000000;   // Rohbyte, wie K7637::QK_RAW_BASE
constexpr uint8_t  LED_GRAPH = 0x40, LED_CAPS = 0x80;   // 21H Bit 6 [?] / Bit 7

// ─── Tabellen der Firmware (EPROM D3, doc/EPROMS/K7672/7672.03-D3.bin) ───────
// Index = Matrixposition (Register 22H, 8 Zeilen × 16 Spalten).  Abgeschrieben mit
//   python3 -c "d=open('doc/EPROMS/K7672/7672.03-D3.bin','rb').read(); ..."
// (Offsets 0080H Scancode, 0100H Tastenart, 0400H/0480H SCP grund/umgeschaltet).

// D3 0080H: Scancode Satz 1.  Bit 7 = mit Vorsatz 2AH/1DH (0326H), FFH = keiner.
constexpr uint8_t kScan[128] = {
    0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0F, 0x4A, 0xCD, 0x44, 0xD1, 0x7A, 0xFF, 0xFF, 0x46, 0xFF,   // 00H
    0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x36, 0x4E, 0xD0, 0xCF, 0x7E, 0x48, 0xFF, 0xFF, 0x42, 0x43,   // 10H
    0x1E, 0x20, 0x22, 0x24, 0x26, 0x28, 0x3A, 0x7B, 0xCB, 0xFF, 0xFF, 0x4C, 0xFF, 0xFF, 0x3F, 0xFF,   // 20H
    0x2C, 0x2E, 0x30, 0x32, 0x34, 0xFF, 0x2A, 0x7C, 0x1C, 0xFF, 0xFF, 0x50, 0xFF, 0xFF, 0x3B, 0x3C,   // 30H
    0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x2B, 0xD3, 0x52, 0xC9, 0x79, 0x37, 0xFF, 0xFF, 0xB7, 0x45,   // 40H
    0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x01, 0x8F, 0x7D, 0xC7, 0x47, 0x49, 0xB8, 0xFF, 0x40, 0x41,   // 50H
    0x1F, 0x21, 0x23, 0x25, 0x27, 0x29, 0x36, 0x0E, 0x53, 0xFF, 0x4B, 0x4D, 0xFF, 0x38, 0x3D, 0x3E,   // 60H
    0x2D, 0x2F, 0x31, 0x33, 0x35, 0xFF, 0xFF, 0x39, 0xFF, 0xC8, 0x4F, 0x51, 0x1D, 0x00, 0xC5, 0xD2,   // 70H
};
// D3 0100H: Tastenart (Register 0DH).  Bit 6 wählt bei Bit 7 im Scancode den Vorsatz
// (1 = Umschalt 2AH, 0 = Strg 1DH); 0x90…0x93 & 3FH = 10H…13H sind Buchstaben/Zeichen.
constexpr uint8_t kArt[128] = {
    0x31, 0x33, 0x35, 0x37, 0x39, 0x7E, 0x09, 0x2D, 0xFF, 0xA7, 0xE9, 0x91, 0x00, 0x00, 0x07, 0x00,   // 00H
    0x90, 0x90, 0x90, 0x90, 0x90, 0x93, 0x06, 0x92, 0xEE, 0xF5, 0x09, 0x91, 0x00, 0x00, 0xAA, 0xAB,   // 10H
    0x90, 0x90, 0x90, 0x90, 0x90, 0x93, 0x05, 0x92, 0xF0, 0x00, 0x00, 0x92, 0x00, 0x00, 0xA3, 0x00,   // 20H
    0x90, 0x90, 0x90, 0x90, 0x91, 0x00, 0x06, 0x91, 0x91, 0x00, 0x00, 0x92, 0x00, 0x00, 0xA6, 0xA0,   // 30H
    0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0xD1, 0x92, 0xE8, 0x91, 0x91, 0x00, 0x00, 0x73, 0x32,   // 40H
    0x90, 0x90, 0x90, 0x90, 0x90, 0x91, 0x91, 0xF1, 0x92, 0xF6, 0x92, 0x91, 0x01, 0x00, 0xA4, 0xA5,   // 50H
    0x90, 0x90, 0x90, 0x90, 0x93, 0x91, 0x06, 0x91, 0x92, 0x00, 0x91, 0x92, 0x00, 0x04, 0xA1, 0xA2,   // 60H
    0x90, 0x90, 0x90, 0x91, 0x91, 0x00, 0x00, 0x90, 0x00, 0xED, 0x92, 0x92, 0x03, 0x02, 0x08, 0x4A,   // 70H
};
// D3 0400H: SCP-Zeichen ohne Umschaltung (FFH = keins — Funktionstasten senden ihre
// ESC-Folgen aus dem fehlenden Firmwareteil, README „Offene Punkte“ 1).
constexpr uint8_t kScpGrund[128] = {
    0x31, 0x33, 0x35, 0x37, 0x39, 0xE1, 0x09, 0x2D, 0xFF, 0xFF, 0xFF, 0x2F, 0xFF, 0xFF, 0xFF, 0xFF,   // 00H
    0x71, 0x65, 0x74, 0x75, 0x6F, 0x81, 0xFF, 0x2B, 0xFF, 0xFF, 0xFF, 0x38, 0xFF, 0xFF, 0xFF, 0xFF,   // 10H
    0x61, 0x64, 0x67, 0x6A, 0x6C, 0x84, 0xFF, 0x3D, 0xFF, 0xFF, 0xFF, 0x35, 0xFF, 0xFF, 0xFF, 0xFF,   // 20H
    0x79, 0x63, 0x62, 0x6D, 0x2E, 0xFF, 0xFF, 0x0D, 0x0D, 0xFF, 0xFF, 0x32, 0xFF, 0xFF, 0xFF, 0xFF,   // 30H
    0x32, 0x34, 0x36, 0x38, 0x30, 0x27, 0x3C, 0x7F, 0x30, 0xFF, 0x18, 0x2A, 0xFF, 0xFF, 0xFF, 0xFF,   // 40H
    0x77, 0x72, 0x7A, 0x69, 0x70, 0x2B, 0x1B, 0xFF, 0x2C, 0xFF, 0x37, 0x39, 0xFF, 0xFF, 0xFF, 0xFF,   // 50H
    0x73, 0x66, 0x68, 0x6B, 0x94, 0x23, 0xFF, 0x08, 0x2E, 0xFF, 0x34, 0x36, 0xFF, 0xFF, 0xFF, 0xFF,   // 60H
    0x78, 0x76, 0x6E, 0x2C, 0x2D, 0xFF, 0xFF, 0x20, 0xFF, 0xFF, 0x31, 0x33, 0xFF, 0xFF, 0xFF, 0xFF,   // 70H
};
// D3 0480H: SCP-Zeichen mit Umschaltung.
constexpr uint8_t kScpUmschalt[128] = {
    0x21, 0x40, 0x25, 0x2F, 0x29, 0x3F, 0x09, 0x2D, 0xFF, 0xFF, 0xFF, 0x2F, 0xFF, 0xFF, 0xFF, 0xFF,   // 00H
    0x51, 0x45, 0x54, 0x55, 0x4F, 0x9A, 0xFF, 0x2B, 0xFF, 0xFF, 0xFF, 0x38, 0xFF, 0xFF, 0xFF, 0xFF,   // 10H
    0x41, 0x44, 0x47, 0x4A, 0x4C, 0x8E, 0xFF, 0x3D, 0xFF, 0xFF, 0xFF, 0x35, 0xFF, 0xFF, 0xFF, 0xFF,   // 20H
    0x59, 0x43, 0x42, 0x4D, 0x3A, 0xFF, 0xFF, 0x0D, 0x0D, 0xFF, 0xFF, 0x32, 0xFF, 0xFF, 0xFF, 0xFF,   // 30H
    0x22, 0x24, 0x26, 0x28, 0x3D, 0x60, 0x3E, 0x7F, 0x30, 0xFF, 0x18, 0x2A, 0xFF, 0xFF, 0xFF, 0xFF,   // 40H
    0x57, 0x52, 0x5A, 0x49, 0x50, 0x2A, 0x1B, 0xFF, 0x2C, 0xFF, 0x37, 0x39, 0xFF, 0xFF, 0xFF, 0xFF,   // 50H
    0x53, 0x46, 0x48, 0x4B, 0x99, 0x5E, 0xFF, 0x08, 0x2E, 0xFF, 0x34, 0x36, 0xFF, 0xFF, 0xFF, 0xFF,   // 60H
    0x58, 0x56, 0x4E, 0x3B, 0x5F, 0xFF, 0xFF, 0x20, 0xFF, 0xFF, 0x31, 0x33, 0xFF, 0xFF, 0xFF, 0xFF,   // 70H
};

}  // namespace

void K7672::connect(Z80SIO& sio, int kanal)
{
    sio_   = &sio;
    kanal_ = kanal;
}

void K7672::powerOn()
{
    // Firmware 000CH: CLR 60H, dann derselbe Weg wie ESC c — Selbsttest, aber
    // ohne DC1 (60H Bit4 ist gelöscht).
    unterwegs_.clear();
    leitung_frei_ = 0;
    esc_aktiv_ = false;
    esc_folge_.clear();
    gesperrt_ = false;
    modus_    = Modus::Scp;
    leds_     = 0;
    ++selbsttests_;
}

void K7672::sende(uint8_t b, uint64_t ab)
{
    const uint64_t start = std::max({jetzt_, leitung_frei_, ab});
    leitung_frei_ = start + ZEICHEN_TAKTE;
    unterwegs_.push_back({leitung_frei_, b});
}

bool K7672::service(uint64_t now)
{
    jetzt_ = now;
    bool geaendert = false;
    if (sio_) {
        Z80SIO::Channel& ch = kanal_ ? sio_->channelB() : sio_->channelA();
        // Der Rechner sendet: jedes Byte sofort auswerten.  Die Übertragungszeit
        // zur Tastatur ist hier vernachlässigt — das ROM sendet mit Pausen.
        while (ch.txAvailable()) {
            empfangeVomRechner(ch.txGet());
            geaendert = true;
        }
        while (!unterwegs_.empty() && unterwegs_.front().faellig <= now) {
            ch.rxByte(unterwegs_.front().wert);
            unterwegs_.pop_front();
            geaendert = true;
        }
    }
    return geaendert;
}

void K7672::empfangeVomRechner(uint8_t b)
{
    // Einzelzeichen wertet der Empfangs-Interrupt (0602H) sofort aus, auch mitten
    // in einer Folge.
    switch (b) {
        case BEL: ++summer_;        return;
        case DC3: gesperrt_ = true;  leds_ &= static_cast<uint8_t>(~LED_FREI); return;   // LED 21H Bit3 aus
        case DC1: gesperrt_ = false; leds_ |=  LED_FREI; return;   // LED 21H Bit3 an
        case ESC:
            esc_aktiv_ = true;
            esc_folge_.clear();
            return;
        default: break;
    }
    if (!esc_aktiv_) return;          // sonstige Zeichen ohne ESC: ohne Wirkung
    esc_folge_.push_back(static_cast<char>(b));
    bool praefix = false;
    for (const char* f : kBefehle) {
        const std::string s(f);
        if (s == esc_folge_) {
            esc_aktiv_ = false;
            befehl(s);
            esc_folge_.clear();
            return;
        }
        if (s.compare(0, esc_folge_.size(), esc_folge_) == 0) praefix = true;
    }
    if (!praefix) {
        LOG_DEBUG("K7672", "unbekannte Folge ESC %s verworfen", esc_folge_.c_str());
        esc_aktiv_ = false;
        esc_folge_.clear();
    }
}

void K7672::befehl(const std::string& f)
{
    if (f == "c")          { neustart(true);  return; }
    if (f == "[2;0y")      { neustart(false); return; }
    if (f == "[2;1y")      { selbsttest();    return; }
    if (f == "[?22h") {
        // 043DH: 29H Bit0 — Scancodes statt Zeichen; zurück nur über ESC c.
        modus_ = Modus::Dcp;
        umschalt_unten_ = strg_unten_ = false;
        gedrueckt_.clear();
        matrix_gedrueckt_.clear();
        LOG_INFO("K7672", "DCP-Modus eingeschaltet (Scancodes Satz 1)");
        return;
    }
    // 04B1H / 04A6H: LED 21H Bit0 an / aus [?] — welche Lampe, sagt die Firmware nicht.
    if (f == "[?13h") { leds_ |= LED_13;  return; }
    if (f == "[?13l") { leds_ &= static_cast<uint8_t>(~LED_13); return; }
    // 0467H / 0470H: Modus 23H Bit 6 (Feststellung) samt LED 21H Bit 7 = CAPS.
    if (f == "[?11h") { leds_ |= LED_CAPS;  return; }
    if (f == "[?11l") { leds_ &= static_cast<uint8_t>(~LED_CAPS); return; }
    // 048BH / 0483H: 60H Bit 5 (zweite Zeichentabelle) samt LED 21H Bit 6 — GRAPH [?].
    if (f == "[?18h") { leds_ |= LED_GRAPH;  return; }
    if (f == "[?18l") { leds_ &= static_cast<uint8_t>(~LED_GRAPH); return; }
    // Kennung, Status, VT52/ANSI, LED-Modi: die Antworttexte fehlen in den Dumps
    // (README), für den Selbsttest nicht nötig — erkannt und verworfen.
    LOG_DEBUG("K7672", "Befehl ESC %s ohne Nachbildung", f.c_str());
}

void K7672::neustart(bool mit_test)
{
    // 000EH: AND 60H,#18H — DC3 (Bit2) und alle Modi fallen, Bit3/Bit4 bleiben.
    // Der Speicherlöscher 7FH…04H setzt auch den DCP-Modus (29H) zurück.
    gesperrt_ = false;
    modus_    = Modus::Scp;
    leds_     = 0;                    // 21H liegt im Speicherlöscher 04H…7FH
    if (mit_test) {
        selbsttest();
    } else {
        sende(DC1);                   // 004FH: sofort DC1
    }
}

void K7672::selbsttest()
{
    // 06B6H: Test, dann DC1 — nur auf Befehl (60H Bit4), deshalb nicht beim
    // Einschalten.  Das DC1 gilt auch bei gesetztem DC3 (die Sperre betrifft Tasten
    // und Antworttexte).
    ++selbsttests_;
    sende(DC1, jetzt_ + SELBSTTEST_TAKTE);
}

uint8_t K7672::zeichenFuer(uint32_t k, bool /*shift*/, bool ctrl)
{
    if ((k & ~0xFFu) == QK_RAW_BASE) return static_cast<uint8_t>(k & 0xFF);
    switch (k) {
        case QK_RETURN:
        case QK_ENTER:     return 0x0D;
        case QK_ESCAPE:    return ESC;
        case QK_TAB:
        case QK_BACKTAB:   return 0x09;
        case QK_BACKSPACE: return 0x08;
        case QK_DELETE:    return 0x7F;
        default: break;
    }
    if (k >= 0x20 && k <= 0x7E) {
        if (ctrl && ((k >= 0x40 && k <= 0x5F) || (k >= 0x60 && k <= 0x7E)))
            return static_cast<uint8_t>(k & 0x1F);
        return static_cast<uint8_t>(k);   // Umschaltung steckt schon im Code
    }
    return 0;
}

void K7672::keyPress(uint32_t k, bool shift, bool ctrl)
{
    if ((k & ~0xFFu) == QK_TASTE_BASE) { tasteMatrix(static_cast<uint8_t>(k & 0x7F), true, shift, ctrl); return; }
    if (modus_ == Modus::Dcp) { tasteDcp(k, true, shift, ctrl); return; }
    const uint8_t z = zeichenFuer(k, shift, ctrl);
    if (z) sendeZeichen(z);
}

void K7672::keyRelease(uint32_t k)
{
    if ((k & ~0xFFu) == QK_TASTE_BASE) { tasteMatrix(static_cast<uint8_t>(k & 0x7F), false, false, false); return; }
    if (modus_ == Modus::Dcp) tasteDcp(k, false, false, false);
    // SCP-Modus: Loslassen erzeugt nichts.
}

// ─── DCP-Modus: Scancodes Satz 1 ────────────────────────────────────────────

namespace {

constexpr uint8_t SC_STRG = 0x1D, SC_UMSCHALT = 0x2A, SC_FESTSTELL = 0x3A, SC_ALT = 0x38;
constexpr uint32_t QK_SHIFT = 0x01000020, QK_CONTROL = 0x01000021, QK_ALT = 0x01000023,
                   QK_CAPSLOCK = 0x01000024, QK_F1 = 0x01000030,
                   QK_LEFT = 0x01000012, QK_UP = 0x01000013, QK_RIGHT = 0x01000014,
                   QK_DOWN = 0x01000015, QK_PGUP = 0x01000016, QK_PGDN = 0x01000017;

// Grundbelegung des BIOS SCPX 8915 V5.3 (Tabelle DC1CH, Scancode → Zeichen), hier
// umgekehrt: Zeichen → Scancode.  DIN-Belegung (Z/Y getauscht, [\] für Ä/Ö, ~ auf ß).
struct Paar { uint8_t zeichen; uint8_t code; };
constexpr Paar kGrund[] = {
    {0x1B,0x01},{'1',0x02},{'2',0x03},{'3',0x04},{'4',0x05},{'5',0x06},{'6',0x07},
    {'7',0x08},{'8',0x09},{'9',0x0A},{'0',0x0B},{'~',0x0C},{'\'',0x0D},{0x7F,0x0E},
    {0x09,0x0F},{'q',0x10},{'w',0x11},{'e',0x12},{'r',0x13},{'t',0x14},{'z',0x15},
    {'u',0x16},{'i',0x17},{'o',0x18},{'p',0x19},{']',0x1A},{'+',0x1B},{0x0D,0x1C},
    {'a',0x1E},{'s',0x1F},{'d',0x20},{'f',0x21},{'g',0x22},{'h',0x23},{'j',0x24},
    {'k',0x25},{'l',0x26},{'\\',0x27},{'[',0x28},{'#',0x29},{'<',0x2B},{'y',0x2C},
    {'x',0x2D},{'c',0x2E},{'v',0x2F},{'b',0x30},{'n',0x31},{'m',0x32},{',',0x33},
    {'.',0x34},{'-',0x35},{' ',0x39},{'*',0x37},{'/',0x7A},
};
// Umschaltung (Tabelle DCA5H, nach Zeichen geschlüsselt, und die Ziffernregel DAB3H:
// '1'…'9' außer 3/7 ⇒ AND EFH): Zielzeichen ← Grundzeichen.
constexpr Paar kUmschalt[] = {
    {'>','<'},{'=','0'},{'?','~'},{'`','\''},{'*','+'},{'^','#'},{'_','-'},{':','.'},
    {';',','},{'}',']'},{'{','['},{'|','\\'},{'@','3'},{'/','7'},
    {'!','1'},{'"','2'},{'$','4'},{'%','5'},{'&','6'},{'(','8'},{')','9'},
};

bool grundCode(uint8_t ch, uint8_t& code) {
    for (const Paar& p : kGrund)
        if (p.zeichen == ch) { code = p.code; return true; }
    return false;
}

}  // namespace

bool K7672::tasteFuer(uint8_t ch, DcpTaste& t)
{
    t = DcpTaste{};
    // Grossbuchstaben = Umschalt + Buchstabe (BIOS DAC8H; ohne Feststell).
    if (ch >= 'A' && ch <= 'Z' && grundCode(static_cast<uint8_t>(ch + 0x20), t.code)) {
        t.umschalt = true;
        return true;
    }
    if (grundCode(ch, t.code)) return true;
    for (const Paar& p : kUmschalt)
        if (p.zeichen == ch && grundCode(p.code, t.code)) { t.umschalt = true; return true; }
    // Steuerzeichen: Strg + Buchstabe (DB17H: AND 9FH) bzw. Tabelle DC0FH.
    if (ch >= 0x01 && ch <= 0x1A && grundCode(static_cast<uint8_t>(ch + 0x60), t.code)) {
        t.strg = true;
        return true;
    }
    switch (ch) {
        case 0x1C: t.code = 0x27; t.strg = true; return true;
        case 0x1D: t.code = 0x1A; t.strg = true; return true;
        case 0x1E: t.code = 0x29; t.strg = true; return true;
        case 0x1F: t.code = 0x35; t.strg = true; return true;
        default: break;
    }
    return false;
}

void K7672::sendeCode(uint8_t b)
{
    if (gesperrt_) return;            // 60H Bit2: die Tastatur sendet nichts
    sende(b);
}

void K7672::dcpDruecken(uint32_t schluessel, const DcpTaste& t)
{
    Gedrueckt g{schluessel, t.code, 0, 0};
    if (t.strg && !strg_unten_) { sendeCode(SC_STRG); strg_unten_ = true; g.strg = +1; }
    if (t.umschalt != umschalt_unten_) {
        sendeCode(t.umschalt ? SC_UMSCHALT : static_cast<uint8_t>(SC_UMSCHALT | 0x80));
        umschalt_unten_ = t.umschalt;
        g.umschalt = t.umschalt ? +1 : -1;
    }
    sendeCode(t.code);
    gedrueckt_.push_back(g);
}

void K7672::dcpLoslassen(uint32_t schluessel)
{
    for (auto it = gedrueckt_.begin(); it != gedrueckt_.end(); ++it) {
        if (it->schluessel != schluessel) continue;
        sendeCode(static_cast<uint8_t>(it->code | 0x80));
        // Was das Drücken an Umschalt/Strg verstellt hat, zurücknehmen.
        if (it->umschalt == +1) { sendeCode(SC_UMSCHALT | 0x80); umschalt_unten_ = false; }
        if (it->umschalt == -1) { sendeCode(SC_UMSCHALT);        umschalt_unten_ = true;  }
        if (it->strg == +1)     { sendeCode(SC_STRG | 0x80);     strg_unten_ = false;     }
        gedrueckt_.erase(it);
        return;
    }
}

void K7672::tasteDcp(uint32_t k, bool gedrueckt, bool /*shift*/, bool ctrl)
{
    // Umschalttasten selbst: eigene Codes, Zustand mitführen.
    auto modifikator = [&](uint8_t code, bool* zustand) {
        sendeCode(gedrueckt ? code : static_cast<uint8_t>(code | 0x80));
        if (zustand) *zustand = gedrueckt;
    };
    switch (k) {
        case QK_SHIFT:    modifikator(SC_UMSCHALT, &umschalt_unten_); return;
        case QK_CONTROL:  modifikator(SC_STRG, &strg_unten_);         return;
        case QK_ALT:      modifikator(SC_ALT, nullptr);               return;
        case QK_CAPSLOCK:
            // Firmware 0303H: die Feststelltaste schaltet LED 21H Bit 7 (CAPS) beim Drücken.
            if (gedrueckt) leds_ ^= LED_CAPS;
            modifikator(SC_FESTSTELL, nullptr);
            return;
        default: break;
    }
    if (!gedrueckt) { dcpLoslassen(k); return; }

    DcpTaste t;
    if ((k & ~0xFFu) == QK_RAW_BASE) {
        t.code = static_cast<uint8_t>(k & 0x7F);   // Rohcode: Taste so, wie sie ist
    } else if (k >= QK_F1 && k < QK_F1 + 10) {
        t.code = static_cast<uint8_t>(0x3B + (k - QK_F1));   // F1…F10 → 3BH…44H
    } else {
        switch (k) {
            // Cursor = Umschalt + Ziffernblock (Tabelle DCC2H: ^H ^X ^D ^E, 9BH/9CH).
            case QK_LEFT:  t = {0x4B, true, false}; break;
            case QK_DOWN:  t = {0x50, true, false}; break;
            case QK_RIGHT: t = {0x4D, true, false}; break;
            case QK_UP:    t = {0x48, true, false}; break;
            case QK_PGUP:  t = {0x49, true, false}; break;
            case QK_PGDN:  t = {0x51, true, false}; break;
            case QK_BACKSPACE:
            case QK_DELETE: t.code = 0x0E; break;   // BIOS: 0EH → 7FH
            default: {
                const uint8_t z = zeichenFuer(k, false, false);
                if (!z || !tasteFuer(z, t)) {
                    LOG_DEBUG("K7672", "DCP: keine Taste für Host-Code %08X", k);
                    return;
                }
                break;
            }
        }
    }
    if (ctrl) t.strg = true;
    dcpDruecken(k, t);
}

void K7672::sendeZeichen(uint8_t ch)
{
    if (modus_ == Modus::Scp) {
        if (gesperrt_) return;        // 60H Bit2: die Tastatur sendet nichts
        sende(ch);
        return;
    }
    // DCP-Modus: das Zeichen als Tastendruck tippen (Drücken, Loslassen).
    DcpTaste t;
    if (!tasteFuer(ch, t)) {
        LOG_WARN("K7672", "DCP: Zeichen %02X hat keine Taste", ch);
        return;
    }
    constexpr uint32_t kTipp = 0xFFFFFFFFu;   // eigener Schlüssel, kein Host-Code
    dcpDruecken(kTipp, t);
    dcpLoslassen(kTipp);
}

// ─── Physische Tasten (Bildschirmtastatur, AP-UI1) ─────────────────────────

uint8_t K7672::scancode(uint8_t m)        { return kScan[m & 0x7F]; }
bool    K7672::vorsatzUmschalt(uint8_t m) { return (kArt[m & 0x7F] & 0x40) != 0; }
uint8_t K7672::scpZeichen(uint8_t m, bool umschalt)
{
    return (umschalt ? kScpUmschalt : kScpGrund)[m & 0x7F];
}

void K7672::tasteMatrix(uint8_t m, bool gedrueckt, bool shift, bool ctrl)
{
    m &= 0x7F;
    if (modus_ == Modus::Scp) {
        if (!gedrueckt) return;                       // SCP: Loslassen erzeugt nichts
        // 0462H / 047EH [?]: Feststell- und GRAPH-Taste schalten Modus und Lampe um.
        if (m == MATRIX_FESTSTELL) { leds_ ^= LED_CAPS;  return; }
        if (m == MATRIX_GRAPH)     { leds_ ^= LED_GRAPH; return; }
        // 0223H: Umschalt ⇒ zweite Tabelle; sonst Feststellung, aber nur für
        // Buchstaben (Tastenart & 3FH = 10H).
        const bool buchstabe = (kArt[m] & 0x3F) == 0x10;
        const bool um = shift || ((leds_ & LED_CAPS) && buchstabe);
        uint8_t z = scpZeichen(m, um);
        if (z == 0xFF) return;                        // keine belegte Taste: nichts
        if (ctrl && z >= 0x40 && z <= 0x7F) z &= 0x1F;
        sendeZeichen(z);
        return;
    }

    // ─── DCP-Modus: Scancodes Satz 1 (0303H–035FH, 05C2H) ───────────────────
    if (!gedrueckt) {
        for (auto it = matrix_gedrueckt_.begin(); it != matrix_gedrueckt_.end(); ++it) {
            if (it->m != m) continue;
            for (char b : it->loslassen) sendeCode(static_cast<uint8_t>(b));
            if (it->umschalt_taste) umschalt_unten_ = false;
            if (it->strg_taste)     strg_unten_ = false;
            matrix_gedrueckt_.erase(it);
            return;
        }
        return;
    }
    if (m == MATRIX_KLICK) return;                    // 030BH: Tastenklick um, kein Code
    const uint8_t sc = kScan[m];
    if (sc == 0xFF) return;                           // unbelegt: federt zurück
    if (m == MATRIX_FESTSTELL) leds_ ^= LED_CAPS;     // 0303H

    std::string drueck, los;
    // Umschalt/Strg der Nachbildung wirken wie gehaltene Tasten (1DH/2AH davor, danach los).
    if (ctrl && !strg_unten_)       { drueck += char(SC_STRG);     los.insert(0, 1, char(SC_STRG | 0x80)); }
    if (shift && !umschalt_unten_)  { drueck += char(SC_UMSCHALT); los.insert(0, 1, char(SC_UMSCHALT | 0x80)); }
    const uint8_t code = sc & 0x7F;
    if (sc & 0x80) {
        if (vorsatzUmschalt(m)) {                     // 2AH, Code — Code|80H, AAH
            drueck += char(SC_UMSCHALT); drueck += char(code);
            los.insert(0, std::string{char(code | 0x80), char(SC_UMSCHALT | 0x80)});
        } else {                                      // 1DH, Code — 9DH, Code|80H
            drueck += char(SC_STRG); drueck += char(code);
            los.insert(0, std::string{char(SC_STRG | 0x80), char(code | 0x80)});
        }
    } else {
        drueck += char(code);
        los.insert(0, 1, char(code | 0x80));
    }
    // Umschalt-/Strg-Tasten selbst: den Zustand mitführen, den der Rechner kennt.
    const bool ist_umschalt = !(sc & 0x80) && (code == SC_UMSCHALT || code == 0x36);
    const bool ist_strg     = !(sc & 0x80) && code == SC_STRG;
    if (ist_umschalt) umschalt_unten_ = true;
    if (ist_strg)     strg_unten_ = true;
    for (char b : drueck) sendeCode(static_cast<uint8_t>(b));
    matrix_gedrueckt_.push_back({m, los, ist_umschalt, ist_strg});
}
