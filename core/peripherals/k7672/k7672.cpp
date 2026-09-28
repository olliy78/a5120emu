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
        case DC3: gesperrt_ = true;  return;
        case DC1: gesperrt_ = false; return;
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
        LOG_INFO("K7672", "DCP-Modus eingeschaltet (Scancodes, Etappe 3)");
        return;
    }
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

void K7672::sendeZeichen(uint8_t ch)
{
    if (gesperrt_) return;            // 60H Bit2: die Tastatur sendet nichts
    sende(ch);
}

void K7672::keyPress(uint32_t k, bool shift, bool ctrl)
{
    if (modus_ == Modus::Dcp) { tasteDcp(k, true); return; }
    const uint8_t z = zeichenFuer(k, shift, ctrl);
    if (z) sendeZeichen(z);
}

void K7672::keyRelease(uint32_t k)
{
    if (modus_ == Modus::Dcp) tasteDcp(k, false);
    // SCP-Modus: Loslassen erzeugt nichts.
}

void K7672::tasteDcp(uint32_t, bool)
{
    // Etappe 3: PC/XT-Scancode Satz 1 (Drücken = Code, Loslassen = Code | 80H,
    // Umschalt/Strg einzeln als 2AH/1DH), Firmware 0320H–035FH.
    LOG_WARN("K7672", "DCP-Modus: Scancodes noch nicht nachgebildet, Taste verworfen");
}
