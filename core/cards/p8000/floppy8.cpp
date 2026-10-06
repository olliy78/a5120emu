/**
 * @file floppy8.cpp
 * @brief Floppy-Seite der 8-Bit-Karte des P8000 — U8272, UA858, PIO2-Glue, Laufwerke.
 * @see floppy8.h, doc/p8000/schaltplan_8bit.md §6
 */

#include "core/cards/p8000/floppy8.h"
#include "core/logger.h"
#include "core/util/zustand.h"

namespace {
std::array<DriveProfile, 4> profile(const P8000Floppy8::Config& cfg) {
    return { builtinDriveProfile(cfg.laufwerke[0]), builtinDriveProfile(cfg.laufwerke[1]),
             builtinDriveProfile(cfg.laufwerke[2]), builtinDriveProfile(cfg.laufwerke[3]) };
}
constexpr uint32_t PHI_HZ = 4'000'000;
}  // namespace

P8000Floppy8::P8000Floppy8(P8000Karte8& karte, const Config& cfg)
    : karte_(karte), cfg_(cfg), afs_(karte.bus(), profile(cfg), PHI_HZ), lw_(afs_, profile(cfg)),
      fdc_(Upd765::Config{PHI_HZ, 250, 0})
{
    // FDC-Tore 20H–23H (CPU am Bus; 22H/23H spiegeln)
    karte_.fdcLesen = [this](uint8_t reg) { pinsAuswerten(); return fdc_.read((reg & 1) != 0); };
    karte_.fdcSchreiben = [this](uint8_t reg, uint8_t d) { pinsAuswerten(); fdc_.write((reg & 1) != 0, d); };
    // PIO2-Ausgänge wirken sofort (Leitungen, kein Takt); Moduswechsel fängt die Auswertung beim
    // FDC-Zugriff und in takt() auf
    karte_.pio2().setPortAOutputCallback([this](uint8_t) { pinsAuswerten(); });
    karte_.pio2().setPortBOutputCallback([this](uint8_t) { pinsAuswerten(); });
    // DMA-Steuerport 3CH–3FH
    karte_.dmaLesen = [this]() { return dma_.ioRead(0); };
    karte_.dmaSchreiben = [this](uint8_t d) { dma_.ioWrite(0, d); karte_.bus().markIntDirty(); };
    dma_.setZaehlerLiestBlocklaenge(true);   // MON8-Hardwaretest 26 (UA858: Zähler = N nach Blockende)
    dma_.setIEI(true);   // IEI fest High (Schaltplan §6.3): vorderstes Glied der Kette
    karte_.setzeDmaKettenglied(&dma_);
    karte_.setzeBusmaster([this] { return dma_.busRequest(); }, [this] { return dma_.step(); },
                          [this] { dma_.cpuZyklus(); });
    karte_.setzeResetHaken([this] { reset(); });

    // UA858-Zyklen über den Bus; DACK = Portadresse 20H–23H (der Dekoder läuft im DMA-Zyklus mit)
    K1520Bus& bus = karte_.bus();
    dma_.memRead  = [&bus](uint16_t a) { return bus.memRead(a); };
    dma_.memWrite = [&bus](uint16_t a, uint8_t d) { bus.memWrite(a, d); };
    dma_.portRead = [this, &bus](uint16_t a) -> uint8_t {
        const uint8_t p = static_cast<uint8_t>(a);
        if (uint8_t(p - 0x20) < 4) return fdc_.dmaRead();
        return bus.ioRead(p);
    };
    dma_.portWrite = [this, &bus](uint16_t a, uint8_t d) {
        const uint8_t p = static_cast<uint8_t>(a);
        if (uint8_t(p - 0x20) < 4) { fdc_.dmaWrite(d); return; }
        bus.ioWrite(p, d);
    };
    // TC = DMA-Blockende ∨ PIO2-A6 — das Blockende kommt NACH dem letzten Byte-Transfer
    dma_.blockEnde = [this](bool l) { dma_ende_ = l; fdc_.setTC(tcPegel()); };

    // DRQ → ein Takt synchronisiert (7474) → RDY (aktiv high); INT → PIO2-B4 (Pegel)
    fdc_.onDrq = [this](bool d) { drq_pin_ = d; };
    fdc_.onIrq = [this](bool i) {
        karte_.setzePinTreiber(2, 1, i ? 0x10 : 0x00, 0x10);
        karte_.bus().markIntDirty();
    };
    for (int u = 0; u < 4; ++u) fdc_.setDrive(u, &afs_.drive(u));
    // US0/US1 unbeschaltet: angesprochen wird allein das per PB0–3 gewählte Laufwerk
    fdc_.unitAuswahl = [this](int) { return gewaehlt(); };
    fdc_.ready = [this](int) {
        const int d = gewaehlt();
        return d >= 0 && afs_.drive(d).isMounted() && motorAn(d);
    };
    reset();
}

P8000Floppy8::~P8000Floppy8()
{
    // Rückrufe der Karte lösen (die Karte kann länger leben)
    karte_.pio2().setPortAOutputCallback(nullptr);
    karte_.pio2().setPortBOutputCallback(nullptr);
    karte_.fdcLesen = nullptr;  karte_.fdcSchreiben = nullptr;
    karte_.dmaLesen = nullptr;  karte_.dmaSchreiben = nullptr;
    karte_.setzeBusmaster(nullptr, nullptr);
    karte_.setzeResetHaken(nullptr);
}

int P8000Floppy8::gewaehlt() const
{
    const uint8_t se = karte_.pio2().pinsB() & 0x0F;   // /SE0..3, 0 = gewählt
    switch (se) {
        case 0x0E: return 0;
        case 0x0D: return 1;
        case 0x0B: return 2;
        case 0x07: return 3;
        default:   return -1;   // keins oder mehrere
    }
}

bool P8000Floppy8::motorAn(int d) const
{
    if (d < 0 || d > 3) return false;
    if (!(karte_.pio2().pinsA() & (1u << d))) return true;   // /MOd = 0
    return d < 2 && gewaehlt() == d;                          // interne Laufwerke: Jumper ML
}

void P8000Floppy8::reset()
{
    dma_.reset();
    dma_ende_ = false;
    drq_pin_ = rdy_ = false;
    dma_.setReady(false);
    fdc_.setTC(false);
    // PIO2 steht nach /RES in Modus 1 (Eingang): alle Pins offen = Pull-up = 1
    pinsAuswerten();
    karte_.setzePinTreiber(2, 1, fdc_.irq() ? 0x10 : 0x00, 0x10);
}

void P8000Floppy8::pinsAuswerten()
{
    const uint8_t a = karte_.pio2().pinsA(), b = karte_.pio2().pinsB();
    fdc_.setDatenrate((a & 0x10) ? 250 : 500);                       // PA4: 1 = 4 MHz / 0 = 8 MHz
    const bool res = (b & 0x80) != 0;                                // PB7
    const bool aktiv = cfg_.fdc_reset_high_aktiv ? res : !res;
    if (aktiv != fdc_resetet_) {
        fdc_resetet_ = aktiv;
        fdc_.setReset(aktiv);
    }
    fdc_.setTC(tcPegel());
}

void P8000Floppy8::takt(int n)
{
    if (rdy_ != drq_pin_) { rdy_ = drq_pin_; dma_.setReady(rdy_); }  // ein Takt Verzug
    pinsAuswerten();
    if (n > 0) fdc_.tick(static_cast<uint32_t>(n));
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void P8000Floppy8::serialize(std::vector<uint8_t>& out) const
{
    fdc_.serialize(out);
    dma_.serialize(out);
    out.push_back(dma_ende_ ? 1 : 0);
    out.push_back(drq_pin_ ? 1 : 0);
    out.push_back(rdy_ ? 1 : 0);
    out.push_back(fdc_resetet_ ? 1 : 0);
    auto& afs = const_cast<K5122&>(afs_);
    for (int i = 0; i < 4; ++i) {
        const bool m = afs.drive(i).isMounted();
        out.push_back(m ? 1 : 0);
        out.push_back(m ? afs.drive(i).currentCylinder() : 0);
    }
}

bool P8000Floppy8::deserialize(const uint8_t*& p, const uint8_t* end)
{
    if (!fdc_.deserialize(p, end) || !dma_.deserialize(p, end)) return false;
    if (end - p < 4 + 8) return false;
    dma_ende_ = *p++ != 0;
    drq_pin_ = *p++ != 0;
    rdy_ = *p++ != 0;
    fdc_resetet_ = *p++ != 0;
    for (int i = 0; i < 4; ++i) {
        const bool m = *p++ != 0;
        const uint8_t cyl = *p++;
        // Nur wenn auch jetzt ein Abbild steckt (das Abbild mountet der Aufrufer neu).
        if (m && afs_.drive(i).isMounted()) afs_.drive(i).restoreHeadPosition(cyl);
    }
    karte_.bus().markIntDirty();
    return true;
}
