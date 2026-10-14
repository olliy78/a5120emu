/**
 * @file k5020.cpp
 * @brief AKB K5020 — siehe k5020.h.
 */

#include "core/cards/k5020/k5020.h"
#include "core/logger.h"

using kassette::KassettenLaufwerk;

namespace {
constexpr int kAbtastTakte = 64;          // Rückleitungen/Daten höchstens alle 26 µs abfragen
constexpr double kBmsHalteS = 108e-6;     // BD S.14: BMS = 1, solange mindestens alle 108 µs ein Flusswechsel
}  // namespace

K5020::K5020(const Config& cfg) : cfg_(cfg) {
    lw_[0] = std::make_unique<KassettenLaufwerk>(cfg.profil1, cfg.takt_hz);
    lw_[1] = std::make_unique<KassettenLaufwerk>(cfg.profil2, cfg.takt_hz);
    // Blindeingabe nötig, um BRDY zu aktivieren (BD S.13; ROM 0362); Steuer-PIO flankengetriggert.
    daten_.setModus1ReadyNachLesen(true);
    steuer_.setModus3Flanke(true);
}

void K5020::attachToBus(K1520Bus& bus) {
    bus.registerIO(&tor_d_, cfg_.basis, 4);
    bus.registerIO(&tor_s_, static_cast<uint8_t>(cfg_.basis + 4), 4);
}

void K5020::reset() {
    daten_.reset();
    steuer_.reset();
    rsi();
    rsi_ = false;
    rest_ = 0;
    ext_neu_ = true;
    leitungen();
    eingaenge();
}

double K5020::byteTakte() const {
    const KassettenLaufwerk& d = *lw_[sel_ == 1 ? 1 : 0];
    return d.profil().byteZeitS() * cfg_.takt_hz;
}

// ─── Busseite ────────────────────────────────────────────────────────────────

uint8_t K5020::Tor::ioRead(uint8_t port) {
    const uint8_t rel = relativ(port & 3);
    Z80PIO& pio = steuer ? karte.steuer_ : karte.daten_;
    if (steuer && (rel == 0 || rel == 2)) karte.eingaenge();   // Rückleitungen aktuell machen
    return pio.ioRead(rel);
}

void K5020::Tor::ioWrite(uint8_t port, uint8_t d) {
    const uint8_t rel = relativ(port & 3);
    Z80PIO& pio = steuer ? karte.steuer_ : karte.daten_;
    pio.ioWrite(rel, d);
    if (steuer) {
        karte.leitungen();
        karte.eingaenge();   // STA = AUF ∧ AVE sofort (treiber.md §3.2: BCU fragt ohne Bandlauf ab)
    } else if (rel == 0 && pio.debugState().port[0].mode == 0) {
        if (!karte.tx_busy_) karte.uebernehmen();   // Schieberegister leer: sofort übernehmen
    }
}

// ─── Steuer-PIO → Geräte ─────────────────────────────────────────────────────

void K5020::rsi() {
    // Internes Rücksetzen der Anschlusssteuerung (beide Auswahlbits 0): Ablauf und Merker löschen.
    tx_busy_ = false;
    zf_ = false;
    bms_pin_ = false;
    bms_halte_bis_ = 0;
}

void K5020::leitungen() {
    // Vor der Umschaltung auf Betriebsart 3 treibt die PIO nichts: Ruhezustand (inaktiv, kein AUF).
    const uint8_t a = steuer_.debugState().port[0].mode == 3 ? steuer_.pinsA() : 0x03;
    const uint8_t b = steuer_.debugState().port[1].mode == 3 ? steuer_.pinsB() : 0x00;

    const bool awa1 = (a & 8) && !(a & 4);
    const bool awa2 = (a & 4) && !(a & 8);
    const bool ist_rsi = !(a & 8) && !(a & 4);
    const bool trv = a & 0x10, trr = a & 0x20, hge = a & 0x40, auf = b & 0x02;
    if (ist_rsi && !rsi_) rsi();
    rsi_ = ist_rsi;
    sel_ = awa1 ? 0 : awa2 ? 1 : -1;

    for (int n = 0; n < 2; ++n) {
        const bool sel = (n == 0) ? awa1 : awa2;
        KassettenLaufwerk& d = *lw_[n];
        Leit l;
        l.res   = !(a & (n == 0 ? 2 : 1));
        l.awa   = sel;
        l.ums   = sel && trv && trr;
        l.vor   = sel && trv && !trr;
        l.rueck = sel && trr && !trv;
        l.auf   = sel && auf && !d.schreibsperre();   // AUF nur bei „Aufzeichnung erlaubt“ (BD S.10)
        l.hge   = sel && hge && !auf;                 // HGE nach AUF löst keinen Suchlauf aus …
        l.wid   = !(sel && auf && hge);               // … sondern sperrt die Wiedergabe (BD S.9)
        const Leit alt = leit_[n];
        if (l == alt) continue;
        // Erst fallende, dann steigende Flanken (kein kurzzeitiges VOR+RCK = Fehler am Gerät).
        if (!l.vor && alt.vor) d.vor(false);
        if (!l.rueck && alt.rueck) d.rueck(false);
        if (!l.auf && alt.auf) d.aufzeichnen(false);
        if (!l.hge && alt.hge) d.hge(false);
        if (l.wid != alt.wid) d.wiedergabe(l.wid);
        if (l.res != alt.res) d.reservieren(l.res);
        if (l.awa != alt.awa) d.anwaehlen(l.awa);
        if (l.hge && !alt.hge) d.hge(true);
        if (l.auf && !alt.auf) d.aufzeichnen(true);
        if (l.vor && !alt.vor) d.vor(true);
        if (l.rueck && !alt.rueck) d.rueck(true);
        // UMS ist im Gerät gespeichert; das ROM nimmt es eine Instruktion später wieder zurück (0320/0322).
        if (l.ums && !alt.ums) d.umspulen();
        leit_[n] = l;
    }
}

// ─── Geräte → Steuer-PIO ─────────────────────────────────────────────────────

bool K5020::eingaenge() {
    const KassettenLaufwerk* s = sel_ >= 0 ? lw_[sel_].get() : nullptr;
    const bool auf = steuer_.debugState().port[1].mode == 3 && (steuer_.pinsB() & 0x02);
    uint8_t a = 0, b = 0;
    if (!s || !s->aebAktiv()) a |= 0x80;             // 1 = Magnetband, 0 = AEB aktiv (treiber.md §3.1)
    if (s && !s->azv38()) b |= 0x01;                 // AZV: 0 = 38 cm/s
    if (s && s->seiteB()) b |= 0x04;
    if (lw_[1]->bereit()) b |= 0x08;                 // /NIB2
    if (lw_[0]->bereit()) b |= 0x10;                 // /NIB1
    if (bms_pin_) b |= 0x20;
    if (s && auf && s->schreibsperre()) b |= 0x40;   // STA = AUF ∧ AVE
    if (zf_) b |= 0x80;
    if (!ext_neu_ && a == a_ext_ && b == b_ext_) return false;
    ext_neu_ = false;
    a_ext_ = a;
    b_ext_ = b;
    steuer_.setExternA(a, 0x80);
    steuer_.setExternB(b, 0xFD);   // Bit 1 (AUF) treibt die Karte selbst
    return true;
}

// ─── Daten ───────────────────────────────────────────────────────────────────

void K5020::uebernehmen() {
    const uint8_t byte = daten_.portARead();
    if (sel_ >= 0) lw_[sel_]->schreibeByte(byte);
    tx_busy_ = true;
    tx_ende_ = static_cast<double>(zeit_) + byteTakte();
    zf_ = false;
    daten_.setStbA(false);   // ASTB: Byte übernommen → ARDY aus, Interrupt
    daten_.setStbA(true);
}

bool K5020::clockTick(int takte) {
    zeit_ += static_cast<uint64_t>(takte);
    rest_ += takte;
    if (rest_ < kAbtastTakte) return false;
    const int t = rest_;
    rest_ = 0;
    lw_[0]->clockTick(t);
    lw_[1]->clockTick(t);
    bool geaendert = false;

    // BMS mit Haltezeit (Abfall erst 108 µs nach dem letzten Flusswechsel)
    if (sel_ >= 0 && lw_[sel_]->bms()) {
        bms_pin_ = true;
        bms_halte_bis_ = zeit_ + static_cast<uint64_t>(kBmsHalteS * cfg_.takt_hz);
    } else if (bms_pin_ && zeit_ >= bms_halte_bis_) {
        bms_pin_ = false;
    }

    // Eingabe: Bytes vom Gerät in Daten-PIO B (nur das angewählte Gerät; die Bytes des anderen verfallen)
    for (int n = 0; n < 2; ++n) {
        while (auto b = lw_[n]->holeByte()) {
            if (n != sel_) continue;
            if (daten_.brdy()) {
                daten_.setExternB(*b, 0xFF);
                daten_.setStbB(false);
                daten_.setStbB(true);
                geaendert = true;
            } else {
                ++verloren_;
            }
        }
    }

    // Ausgabe: Schieberegister frei?
    if (tx_busy_ && static_cast<double>(zeit_) >= tx_ende_) {
        tx_busy_ = false;
        if (daten_.ardy()) {
            const double ende = tx_ende_;
            uebernehmen();
            tx_ende_ = ende + byteTakte();   // lückenlos anschließen (kein Drift)
            geaendert = true;
        } else {
            zf_ = true;                      // kein neues Zeichen rechtzeitig
        }
    }
    if (eingaenge()) geaendert = true;
    return geaendert;
}

bool K5020::autoFlush() {
    const bool a = lw_[0]->autoFlush();
    const bool b = lw_[1]->autoFlush();
    return a || b;
}
