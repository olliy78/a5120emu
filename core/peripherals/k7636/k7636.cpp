#include "core/peripherals/k7636/k7636.h"
#include "core/peripherals/k7636/k7636_tabelle.h"
#include <cstring>

namespace {
// Qt::Key_* (ohne Qt-Header, wie K7637/K7609)
constexpr uint32_t QT_ESCAPE = 0x01000000, QT_TAB = 0x01000001, QT_BACKTAB = 0x01000002,
                   QT_BACKSPACE = 0x01000003, QT_RETURN = 0x01000004, QT_ENTER = 0x01000005,
                   QT_DELETE = 0x01000007, QT_HOME = 0x01000010, QT_LEFT = 0x01000012,
                   QT_UP = 0x01000013, QT_RIGHT = 0x01000014, QT_DOWN = 0x01000015,
                   QT_F1 = 0x01000030, QT_F10 = 0x01000039;

constexpr uint8_t DAUER_CODES[] = {0x07, 0x04, 0x05, 0x20, 0x06, 0x5F};   // BD 4.1, bei .01

/// Ziffernblock: Spalte >= 50 im Positionsnamen (D51…A53, D50 CE, S-Tasten …)
bool imBlock(const char* pos) { return std::strlen(pos) == 3 && pos[1] >= '5'; }

/// Rechenadresse der Position @p pos (Tabellensuche), -1 = nicht vorhanden.
int adresseVon(const char* pos)
{
    for (int i = 0; i < 128; ++i)
        if (std::strcmp(K7636_TABELLE[i].pos, pos) == 0) return i;
    return -1;
}
}  // namespace

uint8_t K7636::code(uint8_t adr, bool umschalt)
{
    const auto& e = K7636_TABELLE[adr & 0x7F];
    return umschalt ? e.b : e.a;
}

const char* K7636::position(uint8_t adr) { return K7636_TABELLE[adr & 0x7F].pos; }

bool K7636::istDauerCode(uint8_t c)
{
    for (uint8_t d : DAUER_CODES) if (d == c) return true;
    return false;
}

bool K7636::findeZeichen(uint8_t z, uint8_t& adr, bool& umschalt)
{
    // Erst der Hauptblock, dann der Ziffernblock; erst Stufe a, dann b.  Modifier/Sonderleitungen
    // (SHIFT/LOCK/CTRL/SL, Adresse >= 77H) tragen nie ein druckbares Zeichen.
    for (int block = 0; block < 2; ++block)
        for (int stufe = 0; stufe < 2; ++stufe)
            for (int i = 0; i < 0x77; ++i) {
                const auto& e = K7636_TABELLE[i];
                if (!e.pos[0] || imBlock(e.pos) != (block == 1)) continue;
                if ((stufe ? e.b : e.a) == z) {
                    adr = static_cast<uint8_t>(i);
                    umschalt = stufe == 1;
                    return true;
                }
            }
    return false;
}

// ── Anschluss ───────────────────────────────────────────────────────────────

bool K7636::ubusRead(uint8_t port, uint8_t& wert)
{
    switch (port) {
        case 0x01: wert = out_valid_ ? STATUS_BEREIT : STATUS_RUHE; return true;
        case 0x06:
            wert = out_code_;
            if (out_valid_) {            // Lesen quittiert (/UINT zurueck) [?]
                out_valid_ = false;
                liefere();               // naechster Code, wenn der Abstand schon verstrichen ist
            }
            return true;
        default: return false;           // uebrige Ports wie ohne Geraet
    }
}

void K7636::ubusWrite(uint8_t port, uint8_t data)
{
    if (port == 0x03) kommando(data);    // 05H: /UCS3 wird von der K7636 nicht benutzt
}

void K7636::reset(bool kommando)
{
    puffer_.clear();
    out_valid_ = false;
    lock_ = false;
    kommando_shift_ = false;
    led_ = 0;
    fehler_ = false;
    signal_bis_ = 0;
    repeat_aktiv_ = false;
    halte_taste_ = 0;
    naechster_code_ = 0;
    einreihen(TYP, cur_cycle_ + (kommando ? ANTWORT_ZEIT : 0), true);    // Einschalten/Reset: erst das Typzeichen (SL1–SL6: X2 offen)
    liefere();
}

void K7636::kommando(uint8_t d)
{
    if (d == 0x00) {                                       // 1 Reset
        reset(true);
        return;
    }
    if (d & 0x80) {                                        // 2 Steuern: 1 S F E A B C D
        if (!(d & 0x40)) { signal_bis_ = cur_cycle_ + SIGNAL_DAUER; ++signale_; }
        fehler_ = !(d & 0x20);
        led_ = static_cast<uint8_t>(((d & 0x10) ? 0 : 0x10) | (~d & 0x0F));
    } else if ((d & 0x9E) == 0x02) {                       // 3: 0 F S 0 0 0 1 X (laut BD, Reihenfolge F/S [?])
        fehler_ = !(d & 0x40);
        if (!(d & 0x20)) { signal_bis_ = cur_cycle_ + SIGNAL_DAUER; ++signale_; }
        kommando_shift_ = !(d & 0x01);
    } else if (d == 0x20) {                                // 4 Spezial: Signal
        signal_bis_ = cur_cycle_ + SIGNAL_DAUER;
        ++signale_;
    }
    // Jedes Kommando wird mit dem Typzeichen beantwortet (BD 4.5).
    einreihen(TYP, cur_cycle_ + ANTWORT_ZEIT, true);
    liefere();
}

// ── Ausgaberegister ─────────────────────────────────────────────────────────

void K7636::einreihen(uint8_t c, uint64_t frueh, bool antwort)
{
    if (!antwort) {
        int n = 0;
        for (const auto& e : puffer_) n += !e.antwort;
        if (n >= PUFFER_TIEFE) { ++verloren_; return; }
    }
    puffer_.push_back({frueh, c, antwort});
}

bool K7636::liefere()
{
    if (out_valid_ || puffer_.empty()) return false;
    const Eintrag& e = puffer_.front();
    if (cur_cycle_ < e.frueh) return false;
    if (!e.antwort && cur_cycle_ < naechster_code_) return false;
    out_code_ = e.code;
    out_valid_ = true;
    if (!e.antwort) naechster_code_ = cur_cycle_ + ABSTAND_CODES;
    puffer_.pop_front();
    return true;
}

bool K7636::service(uint64_t now)
{
    if (now > cur_cycle_) cur_cycle_ = now;
    if (repeat_aktiv_) {
        repeat_rest_ -= static_cast<int64_t>(repeat_uhr_.schritt(now));
        while (repeat_rest_ <= 0) {
            // Bei vollem Puffer faellt der Wiederholcode weg (kein Zaehlen als „verloren“).
            int n = 0;
            for (const auto& e : puffer_) n += !e.antwort;
            if (n < PUFFER_TIEFE) puffer_.push_back({cur_cycle_, halte_code_, false});
            repeat_rest_ += static_cast<int64_t>(DAUER_PERIODE);
        }
    }
    return liefere();
}

// ── Bedienung ───────────────────────────────────────────────────────────────

bool K7636::aufloesen(uint32_t k, bool shift, uint8_t& c) const
{
    c = 0;
    int adr = -1;
    bool stufe = shift || shift_phys_ || lock_;
    if ((k & 0xFF000000u) == QK_TASTE_BASE) {
        adr = static_cast<int>(k & 0x7F);
    } else if (k >= 0x20 && k <= 0x7E) {
        uint8_t a; bool b;
        if (!findeZeichen(static_cast<uint8_t>(k), a, b)) return false;
        adr = a; stufe = b;                               // das Zeichen bestimmt die Stufe
    } else {
        const char* pos = nullptr;
        switch (k) {
            case QT_RETURN: case QT_ENTER: pos = "A10"; break;   // ET1 FFH
            case QT_ESCAPE:    pos = "D17"; break;               // DEL LINE 13H = ESC
            case QT_TAB:       pos = "E13"; break;               // 0FH
            case QT_BACKTAB:   pos = "C15"; break;               // 0BH
            case QT_BACKSPACE: case QT_DELETE: pos = "E17"; break;  // DEL 1BH
            case QT_UP:        pos = "C16"; break;
            case QT_DOWN:      pos = "A16"; break;
            case QT_LEFT:      pos = "B15"; break;
            case QT_RIGHT:     pos = "B17"; break;
            case QT_HOME:      pos = "B16"; break;
            default:
                if (k >= QT_F1 && k <= QT_F10) {                  // F1…F9 = S1…S9, F10 = S [?]
                    static const char* const S[] = {"C54", "A56", "C56", "D54", "D56", "D57",
                                                    "C57", "B57", "A57", "A54"};
                    pos = S[k - QT_F1];
                }
        }
        if (!pos) return false;
        adr = adresseVon(pos);
        if (adr < 0) return false;
    }
    if (adr == ADR_SHIFT || adr == ADR_LOCK || adr == ADR_CTRL || adr >= 0x78) return false;
    // Netzschalter F59 und Steuertaste F57 sind auf der .01 nicht bestueckt bzw. senden nichts (k7636.md §2).
    if (adr == 0x58 || adr == 0x59) return false;
    c = code(static_cast<uint8_t>(adr), stufe);
    return c != 0;
}

void K7636::keyPress(uint32_t k, bool shift, bool ctrl)
{
    if ((k & 0xFF000000u) == QK_TASTE_BASE) {
        const uint32_t adr = k & 0x7F;
        if (adr == ADR_SHIFT) { shift_phys_ = true; return; }
        if (adr == ADR_LOCK)  { lock_ = !lock_; return; }
    }
    uint8_t c;
    if (!aufloesen(k, shift, c)) return;
    if (ctrl) einreihen(0xFE, cur_cycle_, false);        // ET2 = Control-Ersatz unter CP/A [?]
    einreihen(c, cur_cycle_, false);
    liefere();
    halte_taste_ = k;
    halte_code_ = c;
    repeat_aktiv_ = istDauerCode(c);
    if (repeat_aktiv_) {
        repeat_rest_ = static_cast<int64_t>(DAUER_VERZOEGERUNG);
        repeat_uhr_.start(cur_cycle_);
    }
}

void K7636::keyRelease(uint32_t k)
{
    if ((k & 0xFF000000u) == QK_TASTE_BASE && (k & 0x7F) == ADR_SHIFT) {
        shift_phys_ = false;
        return;
    }
    if (k == halte_taste_) { repeat_aktiv_ = false; halte_taste_ = 0; }
}
