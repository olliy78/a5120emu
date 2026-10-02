/**
 * @file k7609.cpp
 * @brief Tastatur K7609 — Host-Taste → 8279-Code, siehe k7609.h.
 * @see doc/prg710/k7609_codes.csv (Quelle der Tabellen), doc/prg710/resident.md §5
 */

#include "core/peripherals/k7609/k7609.h"

namespace {

// Qt::Key_*-Werte (ohne Qt-Kopfdateien), wie in K7637/K7672.
constexpr uint32_t QK_ESCAPE = 0x01000000, QK_TAB = 0x01000001, QK_BACKTAB = 0x01000002,
                   QK_BACKSPACE = 0x01000003, QK_RETURN = 0x01000004, QK_ENTER = 0x01000005,
                   QK_LEFT = 0x01000012, QK_UP = 0x01000013, QK_RIGHT = 0x01000014,
                   QK_DOWN = 0x01000015;

// Aus doc/prg710/k7609_codes.csv (Spalten `zeichen`/`shift_zeichen`, UDOS-Tabelle des
// Residenten 067BH); 0 = Taste ohne druckbares Zeichen (Sonder-/Cursor-/Merkertaste).
// Index = 8279-Code 00H–3FH (ohne Umschaltbit).
constexpr uint8_t kGrund[64] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x00, 0x24, 0x2D, 0x30, 0x39, 0x3E, 0x5D, 0x5B,   // 00H
    0x00, 0x25, 0x2C, 0x50, 0x4F, 0x40, 0x00, 0x00, 0x00, 0x00, 0x23, 0x4C, 0x4B, 0x00, 0x00, 0x00,   // 10H
    0x51, 0x57, 0x45, 0x52, 0x54, 0x5A, 0x55, 0x49, 0x00, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A,   // 20H
    0x59, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 30H
};
constexpr uint8_t kUmschalt[64] = {
    0x3A, 0x2F, 0x2A, 0x28, 0x29, 0x3D, 0x2E, 0x2B, 0x00, 0x26, 0x21, 0x27, 0x3F, 0x3C, 0x7D, 0x7B,   // 00H
    0x00, 0x5F, 0x3B, 0x70, 0x6F, 0x60, 0x00, 0x00, 0x00, 0x5E, 0x22, 0x6C, 0x6B, 0x00, 0x00, 0x00,   // 10H
    0x71, 0x77, 0x65, 0x72, 0x74, 0x7A, 0x75, 0x69, 0x00, 0x61, 0x73, 0x64, 0x66, 0x67, 0x68, 0x6A,   // 20H
    0x79, 0x78, 0x63, 0x76, 0x62, 0x6E, 0x6D, 0x00, 0x00, 0x7E, 0x5C, 0x7C, 0x00, 0x00, 0x00, 0x00,   // 30H
};

/// Taste für ein druckbares Zeichen; -1 = keine.  Bit 6 = Zeichen steht auf der Umschaltstufe.
int tasteFuerZeichen(uint8_t ch)
{
    for (int c = 0; c < 64; ++c)
        if (kGrund[c] == ch) return c;
    for (int c = 0; c < 64; ++c)
        if (kUmschalt[c] == ch) return c | K7609::UMSCHALT_BIT;
    return -1;
}

}  // namespace

uint8_t K7609::zeichen(uint8_t code, bool umschalt)
{
    return (umschalt ? kUmschalt : kGrund)[code & 0x3F];
}

int K7609::codesFuer(uint32_t k, bool shift, bool ctrl, uint8_t out[2])
{
    int code = -1;
    if ((k & ~0x7Fu) == QK_TASTE_BASE) {
        code = static_cast<int>(k & 0x3F) | (((k & 0x40) || shift) ? UMSCHALT_BIT : 0);
    } else {
        switch (k) {
            case QK_RETURN:
            case QK_ENTER:     code = CODE_ET1;    break;
            case QK_ESCAPE:    code = CODE_ET2;    break;
            case QK_TAB:
            case QK_BACKTAB:   code = CODE_TAB;    break;
            case QK_BACKSPACE: code = CODE_BS;     break;
            case QK_UP:        code = CODE_HOCH;   break;
            case QK_RIGHT:     code = CODE_RECHTS; break;
            case QK_LEFT:      code = CODE_LINKS;  break;
            case QK_DOWN:      code = CODE_RUNTER; break;
            default: break;
        }
        if (code < 0 && k >= 0x20 && k <= 0x7E) {
            if (k == 0x20) {
                code = CODE_LEERTASTE;
            } else if ((k | 0x20) >= 'a' && (k | 0x20) <= 'z') {
                // Buchstabe = physische Taste, Stufe aus `shift` (die Tabelle führt den
                // Grossbuchstaben als unverschoben, das OS dreht ihn nach seinem Merker).
                code = tasteFuerZeichen(static_cast<uint8_t>(k & ~0x20u)) & 0x3F;
                if (shift) code |= UMSCHALT_BIT;
            } else {
                code = tasteFuerZeichen(static_cast<uint8_t>(k));
            }
        }
    }
    if (code < 0) return 0;
    int n = 0;
    if (ctrl) out[n++] = CODE_STRG1;
    out[n++] = static_cast<uint8_t>(code);
    return n;
}

void K7609::keyPress(uint32_t k, bool shift, bool ctrl)
{
    uint8_t c[2];
    const int n = codesFuer(k, shift, ctrl, c);
    for (int i = 0; i < n; ++i) wartend_.push_back(c[i]);
}

bool K7609::service()
{
    bool gesendet = false;
    while (kbc_ && !wartend_.empty() && !kbc_->full()) {
        kbc_->pushKey(wartend_.front());
        wartend_.pop_front();
        gesendet = true;
    }
    return gesendet;
}
