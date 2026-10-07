/**
 * @file wdc.cpp
 * @brief WDC des P8000 — s. wdc.h (AP P13c).
 */
#include "core/cards/p8000/wdc.h"

#include "core/cards/p8000/rom_wdc.h"
#include "core/logger.h"
#include "core/util/zustand.h"

#include <algorithm>
#include <cstring>
#include <numeric>

using k1520::winchester::crcCcitt;

namespace {
constexpr uint8_t PORT_DSKEA = 0x0, PORT_CNTST = 0x1, PORT_BM_T = 0x2, PORT_BM_D = 0x3,
                  PORT_DSKC1 = 0x4, PORT_DSKC2 = 0x5, PORT_IMPAUS = 0x6, PORT_IMPATV = 0x7,
                  PORT_ST_PRT = 0x8;
constexpr int IO_VERSATZ  = 9;   // E/A-Zugriff ≈ Takt 9 des Befehls (OUT (n),A = 11 + 1 Wartetakt)
constexpr int MEM_VERSATZ = 6;   // Schreibzugriff von LD (HL),A ≈ Takt 6
constexpr int SCHREIBVERZUG = 2; // [W11] RAM-Byte → Kopf: Puffer + Schieberegister
}  // namespace

P8000Wdc::P8000Wdc(const Config& cfg) : cfg_(cfg)
{
    rom_.fill(0xFF);
    const uint8_t* r = cfg.rom;
    size_t n = cfg.rom_groesse;
    if (!r) {
        switch (cfg.firmware) {
            case Config::Firmware::V4_2:    r = P8K_WDC_4_2;    n = sizeof P8K_WDC_4_2; break;
            case Config::Firmware::V4_0_05: r = P8K_WDC_4_0_05; n = sizeof P8K_WDC_4_0_05; break;
            case Config::Firmware::V3_4_05: r = P8K_WDC_3_4_05; n = sizeof P8K_WDC_3_4_05; break;   // EPROM 2 unbestückt
        }
    }
    std::memcpy(rom_.data(), r, std::min(n, rom_.size()));

    // Plattenbytes je Takt = 625 000 / takt_hz (5 Mbit/s), gekürzt
    const uint64_t g = std::gcd<uint64_t>(625'000, cfg.takt_hz);
    rate_z_ = 625'000 / g;
    rate_n_ = cfg.takt_hz / g;

    cpu_.readByte     = [this](uint16_t a)            { return lesen(a); };
    cpu_.writeByte    = [this](uint16_t a, uint8_t v) { schreiben(a, v); };
    cpu_.readPort     = [this](uint16_t p)            { return ioLesen(static_cast<uint8_t>(p)); };
    cpu_.writePort    = [this](uint16_t p, uint8_t v) { ioSchreiben(static_cast<uint8_t>(p), v); };
    cpu_.retiCallback = [this]()                      { kette_.signalRETI(); };
    kette_.setInterruptChain({&ctc_});
    // [H1] Nulldurchgang K2 beendet die Hostübertragung, wenn HEN schon zurückgenommen ist
    ctc_.setZCTOCallback([this](int ch, bool pegel) {
        if (ch == 2 && !pegel && !(cntst_ & 0x08)) host_aktiv_ = false;
    });
    powerOn();
}

void P8000Wdc::anschliessen(int lw, Platte* p)
{
    if (lw >= 0 && lw < LAUFWERKE) platten_[size_t(lw)] = p;
}

// ─── Lebenslauf ──────────────────────────────────────────────────────────────

void P8000Wdc::hardwareReset()
{
    cpu_.reset();
    ctc_.reset();
    cntst_ = 0;          // 8212 CLR
    dskc2_ = 0;          // 8212 CLR [H4]
    host_aktiv_ = false;
    synchron_ = marke_scharf_ = letzte_marke_ = crc_fehler_ = false;
    marken_ = marken_rest_ = crc_rest_ = 0;
    ei_sperre_ = false;
    kette_.markIntDirty();
    statusMelden();
}

void P8000Wdc::statusMelden()
{
    const uint8_t s = static_cast<uint8_t>(cntst_ & 0x27);   // ST2–ST0, TR
    if (s == status_gemeldet_) return;
    const uint8_t alt = status_gemeldet_;
    status_gemeldet_ = s;
    if (statusBeobachter) statusBeobachter(alt, s);
    if (status_cb_) status_cb_();
}

void P8000Wdc::powerOn()
{
    ram_.fill(cfg_.ram_fuellwert);
    dskea_ = bm_t_ = bm_d_ = dskc1_ = 0;
    dz_ = hz_ = 0;
    hardwareReset();
}

void P8000Wdc::setzeRst(bool high)
{
    if (high == rst_) return;
    rst_ = high;
    if (high) hardwareReset();
    else hostHandshake();
}

int P8000Wdc::m1Zyklen() const
{
    if (cpu_.halted) return 1;
    const uint8_t op = lesen(cpu_.PC);
    return (op == 0xCB || op == 0xED || op == 0xDD || op == 0xFD) ? 2 : 1;
}

int P8000Wdc::schritt()
{
    if (rst_) return 0;
    diskBis(tw_);
    kette_.markIntDirty();
    kette_.updateInterruptChain();
    const uint64_t c0 = cpu_.cycles;
    int m1 = 1;
    if (!ei_sperre_ && cpu_.IFF1 && kette_.isINT()) {
        cpu_.interrupt(kette_.interruptAcknowledge());
    } else {
        const bool halt = cpu_.halted;
        const bool ei = !halt && lesen(cpu_.PC) == 0xFB;
        m1 = m1Zyklen();
        if (cpu_.step() == 0) return 0;
        ei_sperre_ = ei;
    }
    const int w = m1 * cfg_.wartetakte_m1;
    cpu_.cycles += static_cast<uint64_t>(w);
    const int c = static_cast<int>(cpu_.cycles - c0);
    tw_ += static_cast<uint64_t>(c);
    if (ctc_.clockTick(c)) kette_.markIntDirty();
    return c;
}

void P8000Wdc::laufeBis(uint64_t t)
{
    while (tw_ < t) {
        if (rst_) {
            diskBis(t);
            tw_ = t;
            break;
        }
        if (schritt() == 0) break;
    }
}

// ─── Speicher ────────────────────────────────────────────────────────────────

uint8_t P8000Wdc::lesen(uint16_t addr) const
{
    const uint16_t a = addr & 0x3FFF;   // [W1]
    if (a < 0x2000) return rom_[a];
    if (a < 0x3800) return ram_[size_t(a - 0x2000)];
    return 0xFF;                        // Adresszählerbereich: keine Zelle
}

void P8000Wdc::schreiben(uint16_t addr, uint8_t v)
{
    const uint16_t a = addr & 0x3FFF;
    if (a < 0x2000) return;
    if (a < 0x3800) { ram_[size_t(a - 0x2000)] = v; return; }
    // A13–A11 = 111: Adresszähler laden (b_daz/p_h_1), Datenbyte ohne Bedeutung
    diskBis(tw_ + MEM_VERSATZ);
    const uint16_t wert = static_cast<uint16_t>((((addr >> 14) & 3) << 10) | (addr & 0x03FF));
    if (addr & 0x0400) dz_ = wert;
    else               hz_ = wert;
}

uint8_t P8000Wdc::ramLesen(uint16_t adr) const
{
    return (adr >= 0x2000 && adr < 0x3800) ? ram_[size_t(adr - 0x2000)] : 0xFF;
}

void P8000Wdc::ramSchreiben(uint16_t adr, uint8_t v)
{
    if (adr >= 0x2000 && adr < 0x3800) ram_[size_t(adr - 0x2000)] = v;
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

P8000Wdc::Platte* P8000Wdc::gewaehlt() const
{
    const int lw = (dskc2_ >> 4) & 3;   // 01/10/11 = LW 0/1/2, 00 = keins
    return lw ? platten_[size_t(lw - 1)] : nullptr;
}

uint8_t P8000Wdc::ioLesen(uint8_t port)
{
    diskBis(tw_ + IO_VERSATZ);
    if ((port & 0xFC) == 0x70) return ctc_.ioRead(port & 3);
    if ((port & 0x08) && (port >> 4) == PORT_ST_PRT) {
        const uint64_t t = tw_ + IO_VERSATZ;
        uint8_t v = 0xFF;                                     // [W10] Bit 0, 1, 4; /WRITE FAULT inaktiv
        if (crc_fehler_) v &= static_cast<uint8_t>(~0x80);
        if (const Platte* p = gewaehlt()) {
            if (p->bereit(t))     v &= static_cast<uint8_t>(~0x04);
            if (p->seekFertig(t)) v &= static_cast<uint8_t>(~0x08);
            if (p->spur0())       v &= static_cast<uint8_t>(~0x40);
        }
        crc_fehler_ = false;                                  // Lesen löscht das CRC-Fehler-FF
        return v;
    }
    return 0xFF;   // Ausgabetore lesen den offenen Bus
}

void P8000Wdc::ioSchreiben(uint8_t port, uint8_t v)
{
    const uint64_t t = tw_ + IO_VERSATZ;
    diskBis(t);
    if ((port & 0xFC) == 0x70) {
        ctc_.ioWrite(port & 3, v);
        kette_.markIntDirty();
        return;
    }
    if (!(port & 0x08)) return;
    switch (port >> 4) {
        case PORT_DSKEA: dskea_ = v; break;
        case PORT_CNTST:
            cntst_ = v;
            if (v & 0x08) host_aktiv_ = true;   // [H1]
            hostHandshake();
            statusMelden();
            break;
        case PORT_BM_T: bm_t_ = v; break;
        case PORT_BM_D: bm_d_ = v; break;
        case PORT_DSKC1: {
            const uint8_t alt = dskc1_;
            dskc1_ = v;
            if (!(alt & 0x01) && (v & 0x01))   // STEP-Impuls ein
                if (Platte* p = gewaehlt()) p->schritt((v & 0x08) != 0, t);
            break;
        }
        case PORT_DSKC2:
            dskc2_ = v;
            if ((cntst_ & 0x80) && !(v & 0x02)) marke_scharf_ = true;   // [W5]
            break;
        case PORT_IMPAUS:                                               // [W7]
            synchron_ = marke_scharf_ = letzte_marke_ = false;
            marken_ = marken_rest_ = crc_rest_ = 0;
            break;
        case PORT_IMPATV: break;
        default: break;
    }
}

// ─── Disk-Schnittstelle ──────────────────────────────────────────────────────

void P8000Wdc::impuls(int kanal)
{
    ctc_.clkTrg(kanal, false);
    ctc_.clkTrg(kanal, true);
    kette_.markIntDirty();
}

void P8000Wdc::diskBis(uint64_t t)
{
    const uint64_t ziel = t * rate_z_ / rate_n_;
    constexpr uint64_t N = Platte::BYTES_JE_SPUR;
    while (bytes_ < ziel) {
        Platte* p = gewaehlt();
        const uint64_t bt = bytes_ * rate_n_ / rate_z_;      // Zeitpunkt des Bytes
        const bool bereit = p && p->bereit(bt);
        if (!dssLaeuft() || rst_) {                          // nur der Index zählt
            const uint64_t naechster = (bytes_ / N + 1) * N;
            if (naechster > ziel) { bytes_ = ziel; break; }
            bytes_ = naechster;
            if (bereit && !rst_) impuls(3);
            continue;
        }
        ++bytes_;
        const int pos = static_cast<int>(bytes_ % N);
        if (pos == 0 && bereit) impuls(3);
        byteVerarbeiten(bereit ? p : nullptr, pos, bt);
    }
}

void P8000Wdc::ablegen(uint16_t w, bool schreiben)
{
    if (schreiben) ramSchreiben(ringAdresse(dz_, cntst_ & 0x40), static_cast<uint8_t>(w));
    const bool dend = (dz_ & 0xFF) == dskea_;
    dz_ = (dz_ + 1) & 0x0FFF;
    if (dend) {
        if ((dskc2_ & 0x04) && crc_ != 0) crc_fehler_ = true;   // [W4]
        impuls(1);
    }
}

void P8000Wdc::byteVerarbeiten(Platte* p, int pos, uint64_t t)
{
    const int kopf = dskc1_ >> 4;
    if (cntst_ & 0x80) {                                  // RAM → Platte
        if ((dz_ & 0xFF) == dskea_) {                     // [W4]
            if (marke_scharf_) {
                marken_rest_ = (dskea_ & 7) + 1;
                marke_scharf_ = false;
            } else if ((dskc2_ & 0x04) && crc_rest_ == 0) {
                crc_rest_ = 2;
                crc_aus_ = crc_;
            }
            impuls(1);
        }
        uint16_t w;
        if (crc_rest_ > 0) {
            w = (crc_rest_ == 2) ? static_cast<uint8_t>(crc_aus_ >> 8) : static_cast<uint8_t>(crc_aus_);
            --crc_rest_;
            letzte_marke_ = false;
        } else {
            const uint8_t b = ramLesen(ringAdresse(dz_, cntst_ & 0x40));
            if (marken_rest_ > 0) {
                --marken_rest_;
                if (!letzte_marke_) crc_ = 0xFFFF;        // [W6]
                letzte_marke_ = true;
                w = static_cast<uint16_t>(b | Platte::MARKE);
            } else {
                letzte_marke_ = false;
                w = b;
            }
            crc_ = crcCcitt(crc_, b);
        }
        if ((dskc2_ & 0x08) && p)                                     // [W8], [W11]
            p->schreibe(kopf, (pos + SCHREIBVERZUG) % Platte::BYTES_JE_SPUR, w, t);
        dz_ = (dz_ + 1) & 0x0FFF;
        return;
    }
    // Platte → RAM
    const uint16_t w = p ? p->lies(kopf, pos) : 0x00;
    const uint8_t  b = static_cast<uint8_t>(w);
    const bool marke = (w & Platte::MARKE) && b == bm_d_;
    if (!synchron_) {                                     // [W3]
        if (!(dskc2_ & 0x02) && marke) {
            if (marken_ == 0) crc_ = 0xFFFF;
            crc_ = crcCcitt(crc_, b);
            if (++marken_ >= (dskea_ & 7) + 1) {
                synchron_ = true;
                marken_ = 0;
                impuls(0);                                // MAERK
                ablegen(w, false);                        // [W3] Marke belegt die Adresse, ohne Schreiben
            }
        } else {
            marken_ = 0;
        }
        return;
    }
    crc_ = crcCcitt(crc_, b);
    ablegen(w, true);
}

// ─── Hostschnittstelle ───────────────────────────────────────────────────────

void P8000Wdc::setzeTe(bool aktiv)
{
    te_ = aktiv;
    hostHandshake();
}

void P8000Wdc::setzeArdy(bool aktiv)
{
    if (aktiv && !ardy_) ardy_verbraucht_ = false;   // neue Aktivierung = neues Byte
    ardy_ = aktiv;
    hostHandshake();
}

void P8000Wdc::hostHandshake()
{
    if (im_handshake_) { nochmal_ = true; return; }
    im_handshake_ = true;
    do {
        nochmal_ = false;
        while (!rst_ && host_aktiv_ && ardy_ && !ardy_verbraucht_) {
            const bool zum_host = (cntst_ & 0x20) != 0;
            if (zum_host != te_) break;                    // [H3]
            const uint16_t adr = ringAdresse(hz_, cntst_ & 0x10);
            if (zum_host) zum_host_ = ramLesen(adr);
            else          ramSchreiben(adr, hostbus_);
            ardy_verbraucht_ = true;
            hz_ = (hz_ + 1) & 0x0FFF;
            ctc_.clkTrg(2, (hz_ & 1) != 0);                // HA0-Flanke
            kette_.markIntDirty();
            if (astb_cb_) astb_cb_();
        }
    } while (nochmal_);
    im_handshake_ = false;
}

// ─── Save-State ──────────────────────────────────────────────────────────────

namespace {
void visitZ80(k1520::ZAr& a, Z80& z)
{
    a.num(z.AF); a.num(z.BC); a.num(z.DE); a.num(z.HL);
    a.num(z.AF_); a.num(z.BC_); a.num(z.DE_); a.num(z.HL_);
    a.num(z.IX); a.num(z.IY); a.num(z.PC); a.num(z.SP);
    a.num(z.I); a.num(z.R); a.flag(z.IFF1); a.flag(z.IFF2); a.num(z.IM); a.flag(z.halted);
    a.num(z.cycles);
}
}  // namespace

void P8000Wdc::serialize(std::vector<uint8_t>& out) const
{
    auto* s = const_cast<P8000Wdc*>(this);
    auto a = k1520::ZAr::schreiber(out);
    uint8_t stand = 1;
    a.num(stand);
    visitZ80(a, s->cpu_);
    a.raw(s->ram_.data(), ram_.size());
    a.num(s->tw_); a.num(s->bytes_); a.flag(s->ei_sperre_);
    a.num(s->cntst_); a.num(s->dskea_); a.num(s->bm_t_); a.num(s->bm_d_); a.num(s->dskc1_); a.num(s->dskc2_);
    a.num(s->dz_); a.num(s->hz_);
    a.flag(s->synchron_); a.flag(s->marke_scharf_); a.flag(s->letzte_marke_); a.flag(s->crc_fehler_);
    a.num(s->marken_); a.num(s->marken_rest_); a.num(s->crc_rest_); a.num(s->crc_); a.num(s->crc_aus_);
    a.flag(s->rst_); a.flag(s->te_); a.flag(s->ardy_); a.flag(s->ardy_verbraucht_); a.flag(s->host_aktiv_);
    a.num(s->hostbus_); a.num(s->zum_host_);
    ctc_.serialize(out);
    ctc_.serializeTakt(out);
    for (const Platte* p : platten_) {
        out.push_back(p ? 1 : 0);
        if (p) p->serialize(out);
    }
}

bool P8000Wdc::deserialize(const uint8_t*& p, const uint8_t* end)
{
    auto a = k1520::ZAr::leser(p, end);
    uint8_t stand = 0;
    a.num(stand);
    if (!a.ok || stand != 1) return false;
    visitZ80(a, cpu_);
    a.raw(ram_.data(), ram_.size());
    a.num(tw_); a.num(bytes_); a.flag(ei_sperre_);
    a.num(cntst_); a.num(dskea_); a.num(bm_t_); a.num(bm_d_); a.num(dskc1_); a.num(dskc2_);
    a.num(dz_); a.num(hz_);
    a.flag(synchron_); a.flag(marke_scharf_); a.flag(letzte_marke_); a.flag(crc_fehler_);
    a.num(marken_); a.num(marken_rest_); a.num(crc_rest_); a.num(crc_); a.num(crc_aus_);
    a.flag(rst_); a.flag(te_); a.flag(ardy_); a.flag(ardy_verbraucht_); a.flag(host_aktiv_);
    a.num(hostbus_); a.num(zum_host_);
    if (!a.ok) return false;
    status_gemeldet_ = static_cast<uint8_t>(cntst_ & 0x27);
    const uint8_t* q = a.p;
    if (!ctc_.deserialize(q, end) || !ctc_.deserializeTakt(q, end)) return false;
    for (Platte* pl : platten_) {
        if (q >= end) return false;
        const bool da = *q++ != 0;
        if (da != (pl != nullptr)) return false;
        if (pl && !pl->deserialize(q, end)) return false;
    }
    p = q;
    kette_.markIntDirty();
    return true;
}
