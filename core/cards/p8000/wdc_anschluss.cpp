/**
 * @file wdc_anschluss.cpp
 * @brief WDC ↔ 16-Bit-PIO2 des P8000 — Leitungen nach Bl. 13 (AP P13d).
 * @see wdc_anschluss.h
 */

#include "core/cards/p8000/wdc_anschluss.h"
#include "core/cards/p8000/karte16.h"
#include "core/cards/p8000/wdc.h"
#include "core/logger.h"

P8000WdcAnschluss::P8000WdcAnschluss(P8000Karte16& k16, P8000Wdc& wdc) : k16_(k16), wdc_(wdc)
{
    k16_.setzeWdcHaken([this] { rechne(); });
    k16_.pio2().setARdyCallback([this](bool) { rechne(); });
    wdc_.setzeStatusRueckruf([this] { rechne(); });
    wdc_.setzeAstbRueckruf([this] { astb(); });
    rechne();
}

P8000WdcAnschluss::~P8000WdcAnschluss()
{
    k16_.setzeWdcHaken(nullptr);
    k16_.pio2().setARdyCallback(nullptr);
    wdc_.setzeStatusRueckruf(nullptr);
    wdc_.setzeAstbRueckruf(nullptr);
}

void P8000WdcAnschluss::rechne()
{
    if (ruhig_) return;
    if (in_rechne_) { nochmal_ = true; return; }
    in_rechne_ = true;
    int runden = 0;
    do {
        nochmal_ = false;
        einmal();
    } while (nochmal_ && ++runden < 16);
    if (nochmal_) LOG_WARN("P8000", "WDC-Anschluss kommt nicht zur Ruhe (16 Runden)");
    in_rechne_ = false;
}

void P8000WdcAnschluss::einmal()
{
    Z80PIO& p = k16_.pio2();
    // B0–B2 = NOT ST0–2, B7 = NOT TR− (DL540); B5/B6 treibt die PIO (Eingang ⇒ Pull-up)
    const uint8_t b = static_cast<uint8_t>((~wdc_.status() & 0x07) | (wdc_.tr() ? 0x80 : 0x00));
    const uint8_t pb = p.pinsB(static_cast<uint8_t>(0xFF));
    const bool rst = (pb & 0x20) != 0;
    const bool te  = (pb & 0x40) != 0;   // [A1]
    k16_.setzePinTreiber(2, 1, b, 0x87);
    // Datentreiber: B6 = 1 ⇒ WDC → PIO-A-Pins, sonst PIO → WDC [A2]
    if (te) k16_.setzePinTreiber(2, 0, wdc_.datenZumHost(), 0xFF);
    else    k16_.setzePinTreiber(2, 0, 0x00, 0x00);
    wdc_.setzeHostbus(te ? 0xFF : p.pinsA(static_cast<uint8_t>(0xFF)));
    wdc_.setzeRst(rst);
    wdc_.setzeTe(te);
    wdc_.setzeArdy(p.ardy());
    k16_.peripherieBus().markIntDirty();
}

void P8000WdcAnschluss::astb()
{
    // Das Byte liegt an (WDC → Host) bzw. ist übernommen (Host → WDC): /ASTB-Impuls an PIO2-A.
    Z80PIO& p = k16_.pio2();
    if (p.pinsB(static_cast<uint8_t>(0xFF)) & 0x40) k16_.setzePinTreiber(2, 0, wdc_.datenZumHost(), 0xFF);
    p.setStbA(false);
    p.setStbA(true);
    k16_.peripherieBus().markIntDirty();
}
