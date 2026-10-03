/**
 * @file ass590069.cpp
 * @brief ASS 590069 — Fernschreiber F1100/F1200 an SIO-Kanal B (C5H/C7H), siehe ass590069.h.
 */

#include "core/cards/ass590069/ass590069.h"
#include "core/serial/sio_format.h"

using k1520::serial::SerialFormat;

namespace {
/// Umsetztabelle des BIOS `B17172FS`/`B17272FS` bei E366H (64 Byte, in beiden gleich):
/// Index = Lage·20H + ITA2-Code, FFH = keins, FEH = Ziffernumschaltung.
constexpr uint8_t kIta2[64] = {
    0xFF, 0x45, 0x0A, 0x41, 0x20, 0x53, 0x49, 0x55, 0x0D, 0x44, 0x52, 0x4A, 0x4E, 0x46, 0x43, 0x4B,
    0x54, 0x5A, 0x4C, 0x57, 0x48, 0x59, 0x50, 0x51, 0x4F, 0x42, 0x47, 0xFE, 0x4D, 0x58, 0x56, 0xFF,
    0xFF, 0x33, 0x0A, 0x2D, 0x20, 0x27, 0x38, 0x37, 0x0D, 0xFF, 0x34, 0x07, 0x2C, 0x22, 0x3A, 0x28,
    0x35, 0x2B, 0x29, 0x32, 0x3B, 0x36, 0x30, 0x31, 0x39, 0x3F, 0x21, 0xFF, 0x2E, 0x2F, 0x3D, 0xFF,
};
constexpr uint8_t ITA2_ZIFFERN    = 0x1B;   // „Zi“ (BIOS E31EH/E340H)
constexpr uint8_t ITA2_BUCHSTABEN = 0x1F;   // „Bu“ (BIOS E318H/E34DH)
}  // namespace

uint8_t Ass590069::ita2NachAscii(uint8_t code, bool ziffern) {
    const uint8_t a = kIta2[(ziffern ? 0x20 : 0) | (code & 0x1F)];
    return (a == 0xFF || a == 0xFE) ? 0 : a;
}

int Ass590069::asciiNachIta2(uint8_t ascii) {
    if (ascii >= 0x60) ascii = static_cast<uint8_t>(ascii - 0x20);   // wie das BIOS (E30EH)
    for (int i = 0; i < 64; ++i)
        if (kIta2[i] == ascii) return i & 0x1F;
    return -1;
}

// ─── Anschluss „Fernschreiber“ ───────────────────────────────────────────────

class Ass590069::Anschluss : public k1520::serial::SerialAnschluss {
public:
    explicit Anschluss(Ass590069& k) : k_(k) {}
    const char* name() const override { return "Fernschreiber"; }
    const char* stecker() const override { return "590069"; }   // Steckerbezeichnung [?]
    bool v24() const override { return false; }

    SerialFormat format() const override {
        SerialFormat f = gastFormat();
        // Hinaus geht Text: 8 Bit, Zeichentakt der Gastleitung (s. ass590069.h).
        f.daten = 8;
        f.paritaet = 0;
        f.stopp_halbe = 2;
        return f;
    }
    bool senderHatZeichen() const override {
        return kanal().senderHatZeichen() && !k_.stummesZeichen();
    }
    uint8_t senderNimm() override {
        const uint8_t code = static_cast<uint8_t>(kanal().txGet() & 0x1F);
        return Ass590069::ita2NachAscii(code, k_.ziffern_);
    }
    bool empfaengerFrei() const override { return kanal().empfaengerFrei(); }
    void empfange(uint8_t b) override {
        const int c = Ass590069::asciiNachIta2(b);
        if (c >= 0) kanal().rxByte(static_cast<uint8_t>(c));
    }

    SerialFormat gastFormat() const {
        return k1520::serial::serialFormatAusSio(kanal(), k_.ctc_, k_.cfg_.takt_kanal);
    }

private:
    Z80SIO::Channel& kanal() const { return k_.sio_.channelB(); }
    Ass590069& k_;
};

Ass590069::Ass590069(const Config& cfg)
    : cfg_(cfg), ctc_("590069 CTC"), anschluss_(std::make_unique<Anschluss>(*this)) {}

Ass590069::~Ass590069() = default;

k1520::serial::SerialAnschluss& Ass590069::anschluss() { return *anschluss_; }

void Ass590069::attachToBus(K1520Bus& bus) {
    bus.registerIO(&ports_, cfg_.sio_basis, 4);
    bus.registerIO(&ctc_, cfg_.ctc_basis, 4);
}

void Ass590069::reset() {
    sio_.reset();
    ctc_.reset();
    stumm_laeuft_ = false;
    ziffern_ = false;
}

bool Ass590069::stummesZeichen() const {
    const auto& tx = const_cast<Z80SIO&>(sio_).channelB().tx_buf;   // channelB() hat keine const-Fassung
    if (!tx) return false;
    const uint8_t code = static_cast<uint8_t>(*tx & 0x1F);
    return code == ITA2_ZIFFERN || code == ITA2_BUCHSTABEN || ita2NachAscii(code, ziffern_) == 0;
}

bool Ass590069::clockTick(int takte) {
    zeit_ += static_cast<uint64_t>(takte);
    bool geaendert = ctc_.clockTick(takte);
    // Ein Umschaltzeichen druckt nichts, braucht aber seine Zeichenzeit auf der Leitung —
    // die Karte nimmt es selbst ab (der Wandler sieht es nicht, s. Anschluss).
    if (!sio_.channelB().tx_buf) return geaendert;
    if (!stumm_laeuft_) {
        if (!stummesZeichen()) return geaendert;
        const SerialFormat f = anschluss_->gastFormat();
        const uint64_t zt = (f.gueltig && f.zeichen_takte) ? f.zeichen_takte
                                                           : k1520::serial::ersatzFormat().zeichen_takte;
        stumm_laeuft_ = true;
        stumm_bis_ = zeit_ + zt;
        return geaendert;
    }
    if (zeit_ < stumm_bis_) return geaendert;
    stumm_laeuft_ = false;
    const uint8_t code = static_cast<uint8_t>(sio_.channelB().txGet() & 0x1F);
    if (code == ITA2_ZIFFERN) ziffern_ = true;
    else if (code == ITA2_BUCHSTABEN) ziffern_ = false;
    return true;   // Sender leer
}
