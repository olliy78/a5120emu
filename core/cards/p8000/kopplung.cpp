/**
 * @file kopplung.cpp
 * @brief Kopplung 8-Bit ↔ 16-Bit des P8000 — Leitungen K1–K15 (Entwurf 25 §10.3).
 * @see kopplung.h
 */

#include "core/cards/p8000/kopplung.h"
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/karte8.h"
#include "core/logger.h"
#include "core/util/zustand.h"

namespace {
constexpr uint8_t SAVE_VERSION = 1;
}

P8000Kopplung::P8000Kopplung(P8000Karte8& k8, P8000Karte16& k16, const Config& cfg)
    : k8_(k8), k16_(k16), cfg_(cfg)
{
    auto neu = [this] { rechne(); };
    k8_.setLatchRueckruf([this](int, uint8_t) { rechne(); });
    k8_.setzePioHaken([this](int pio) { if (pio == 0) rechne(); });
    k16_.setzePioHaken([this](int pio) { if (pio <= 1) rechne(); });
    k8_.pio0().setARdyCallback([neu](bool) { neu(); });
    k16_.pio0().setARdyCallback([neu](bool) { neu(); });
    k16_.pio1().setARdyCallback([neu](bool) { neu(); });
    // K12: NMI-Weiche der 8-Bit-Karte (B7eff = 0) ⇒ MANUALNMI als Tastenimpuls [K2].
    k8_.setNmiU8000Rueckruf([this] { k16_.nmiTaste(); });
    rechne();
}

P8000Kopplung::~P8000Kopplung()
{
    k8_.setLatchRueckruf(nullptr);
    k8_.setzePioHaken(nullptr);
    k16_.setzePioHaken(nullptr);
    k8_.pio0().setARdyCallback(nullptr);
    k16_.pio0().setARdyCallback(nullptr);
    k16_.pio1().setARdyCallback(nullptr);
    k8_.setNmiU8000Rueckruf(nullptr);
}

void P8000Kopplung::rechne()
{
    if (ruhig_) return;
    if (in_rechne_) { nochmal_ = true; return; }   // eine Senke hat eine Quelle bewegt
    in_rechne_ = true;
    int runden = 0;
    do {
        nochmal_ = false;
        einmal();
    } while (nochmal_ && ++runden < 16);
    if (nochmal_) LOG_WARN("P8000", "Kopplung kommt nicht zur Ruhe (16 Runden)");
    in_rechne_ = false;
}

void P8000Kopplung::einmal()
{
    Z80PIO& p8  = k8_.pio0();
    Z80PIO& p16 = k16_.pio0();   // 16 → 8 (Ausgabe, Modus 0)
    Z80PIO& q16 = k16_.pio1();   // 8 → 16 (Eingabe, Modus 1)

    Pegel n;
    n.d8_16   = k8_.latch1();                                  // K1
    n.l2      = k8_.latch2();                                  // K2–K4
    n.e0_8    = q16.ardy();                                    // K5
    n.d16_8   = p16.pinsA();                                   // K6 (offen ⇒ Pull-up)
    n.dd_8    = p16.ardy();                                    // K7
    n.rdy16_8 = p8.ardy();                                     // K8
    n.b16_8   = static_cast<uint8_t>(p16.pinsB() & 0x1F);      // K9/K10 (INT-8: Pull-up 1N1)
    n.reset16 = k8_.b7eff();                                   // K11
    n.run     = k16_.runLed();                                 // K14

    // ── Senken auf der 16-Bit-Seite ──
    k16_.setzePinTreiber(1, 0, n.d8_16, 0xFF);                                     // K1
    k16_.setzePinTreiber(1, 1, static_cast<uint8_t>(n.l2 & 0x7F), 0x7F);          // K2/K3 (B7 offen)
    if (cfg_.rueckfuehrung)                                                         // K15 [KP1]
        k16_.setzePinTreiber(0, 1, static_cast<uint8_t>((n.e0_8 ? 0x20 : 0) | (n.dd_8 ? 0x40 : 0)), 0x60);
    else
        k16_.setzePinTreiber(0, 1, 0x00, 0x00);
    q16.setStbA((n.l2 & 0x80) != 0);                                                // K4
    p16.setStbA(n.rdy16_8);                                                         // K8

    // ── Senken auf der 8-Bit-Seite (B7 bleibt offen: Pull-up 5R18) ──
    k8_.setzePinTreiber(0, 0, n.d16_8, 0xFF);                                       // K6
    k8_.setzePinTreiber(0, 1, static_cast<uint8_t>(n.b16_8 | (n.dd_8 ? 0x20 : 0) | (n.e0_8 ? 0x40 : 0)),
                        0x7F);                                                      // K9/K10/K7/K5
    p8.setStbA(n.dd_8);                                                             // K7

    if (!gerechnet_ || n.reset16 != pegel_.reset16) {                               // K11
        LOG_DEBUG("P8000", "Kopplung: RESET 16-Bit %s", n.reset16 ? "aktiv" : "frei");
        k16_.setResetEingang(n.reset16);
        n.run = k16_.runLed();
    }
    gerechnet_ = true;
    pegel_ = n;

    k8_.bus().markIntDirty();
    k16_.peripherieBus().markIntDirty();
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void P8000Kopplung::serialize(std::vector<uint8_t>& out) const
{
    out.push_back(SAVE_VERSION);
    auto a = k1520::ZAr::schreiber(out);
    Pegel n = pegel_;
    bool g = gerechnet_;
    a.num(n.d8_16); a.num(n.l2); a.flag(n.e0_8); a.num(n.d16_8); a.flag(n.dd_8);
    a.flag(n.rdy16_8); a.num(n.b16_8); a.flag(n.reset16); a.flag(n.run); a.flag(g);
}

bool P8000Kopplung::deserialize(const uint8_t*& p, const uint8_t* end)
{
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    ++p;
    auto a = k1520::ZAr::leser(p, end);
    Pegel n;
    bool g = false;
    a.num(n.d8_16); a.num(n.l2); a.flag(n.e0_8); a.num(n.d16_8); a.flag(n.dd_8);
    a.flag(n.rdy16_8); a.num(n.b16_8); a.flag(n.reset16); a.flag(n.run); a.flag(g);
    if (!a.ok) return false;
    p = a.p;
    pegel_ = n;
    gerechnet_ = g;
    return true;
}
