/**
 * @file eprommer590068.cpp
 * @brief EPROMmer der ATP 590068 mit virtuellem Sockel — siehe eprommer590068.h und
 *        doc/prg710/eprommer.md (Fundstellen in PROG V3.1 / PROG.COM V1.1).
 */

#include "core/cards/atp590068/eprommer590068.h"
#include "core/logger.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {
/// Anschlüsse eines PIO-Ports, wie die Karte sie sieht: Eingangsbits offen = 1 [?].
uint8_t anschluesse(const Z80PIO::DebugState::P& p)
{
    switch (p.mode) {
        case 0: case 2: return p.out;
        case 3:         return uint8_t((p.out & ~p.dir) | p.dir);
        default:        return 0xFF;
    }
}
std::string hex(unsigned v, int n)
{
    char b[16];
    std::snprintf(b, sizeof b, "%0*XH", n, v);
    return b;
}
std::string ms1(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, v < 10 ? "%.2f ms" : "%.1f ms", v);
    return b;
}
}  // namespace

Eprommer590068::Eprommer590068(const Config& cfg) : cfg_(cfg) {}

const char* Eprommer590068::name(Typ t)
{
    switch (t) {
        case Typ::U555:  return "U555";
        case Typ::U2716: return "U2716";
        default:         return "—";
    }
}

void Eprommer590068::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.basis, 5);   // PIO D0H–D3H + Steuerregister D4H (D5H–D7H [?])
}

void Eprommer590068::reset()
{
    std::lock_guard<std::mutex> lk(sperre_);
    if (d4_ & (BIT_VPP | BIT_VERS | BIT_VERS1)) log("/RESET: Steuerregister 00H, Spannungen aus [?]");
    if (brennen_aktiv_) brennphaseEnde();
    if (d4_ & BIT_VERS) lesephaseEnde();
    d4_ = 0;
    pio_.reset();
}

// ─── Hilfen (unter Sperre) ───────────────────────────────────────────────────

void Eprommer590068::log(const std::string& text)
{
    char b[32];
    std::snprintf(b, sizeof b, "[%9.3f s] ", jetzt() / double(cfg_.takt_hz));
    protokoll_.push_back(b + text);
    ++protokoll_nr_;
    if (protokoll_.size() > PROTOKOLL_MAX) protokoll_.pop_front();
    LOG_DEBUG("EPROMMER", "%s", text.c_str());
}

Eprommer590068::Typ Eprommer590068::eingestelltLocked() const
{
    // ZRE-PIO 84H Bit 0: 0 = 1 KB (U555), 1 = 2 KB (U2716) — PROG 60E7/60F4, Vorgabe 61B3.
    const uint8_t a = typwahl_ ? typwahl_() : 0xFF;
    return (a & 1) ? Typ::U2716 : Typ::U555;
}

void Eprommer590068::typwahlPruefen()
{
    const Typ t = eingestelltLocked();
    if (t == typ_gemeldet_) return;
    typ_gemeldet_ = t;
    log(std::string("Typ eingestellt: ") + name(t) + (t == Typ::U2716 ? " (2 KB, 84H Bit 0 = 1)"
                                                                      : " (1 KB, 84H Bit 0 = 0)"));
}

uint16_t Eprommer590068::adresseLocked() const
{
    const uint8_t a07 = anschluesse(pio_.debugState().port[1]);
    return uint16_t(a07 | (((d4_ >> 5) & 7) << 8));
}

uint8_t Eprommer590068::datenleitungen() const
{
    return anschluesse(pio_.debugState().port[0]);
}

// ─── Bus ─────────────────────────────────────────────────────────────────────

uint8_t Eprommer590068::ioRead(uint8_t port)
{
    std::lock_guard<std::mutex> lk(sperre_);
    const uint8_t rel = uint8_t(port - cfg_.basis);
    if (rel == 0) {
        typwahlPruefen();
        uint8_t v = 0xFF;
        if (d4_ & BIT_VERS) {
            const uint16_t a = adresseLocked();
            ++lese_n_;
            ++lese_gesamt_;
            if (a < lese_min_) lese_min_ = a;
            if (a > lese_max_) lese_max_ = a;
            if (typ_ != Typ::Keiner) {
                v = zellen_[a & (groesse(typ_) - 1)];
                if (typ_ != typ_gemeldet_ && !lese_typ_gewarnt_) {
                    lese_typ_gewarnt_ = true;
                    log(std::string("Lesen mit falschem Typ: eingestellt ") + name(typ_gemeldet_)
                        + ", gesteckt " + name(typ_) + " [?]");
                }
            }
        }
        pio_.portAWrite(v);   // Eingangsbits von Port A = Datenausgänge des PROM
        return pio_.ioRead(0);
    }
    if (rel < 4) return pio_.ioRead(pioPort(rel));
    return 0xFF;   // Steuerregister nur schreibbar [?]
}

void Eprommer590068::ioWrite(uint8_t port, uint8_t data)
{
    std::lock_guard<std::mutex> lk(sperre_);
    const uint8_t rel = uint8_t(port - cfg_.basis);
    if (rel < 4) { pio_.ioWrite(pioPort(rel), data); return; }
    if (rel == 4) steuerregisterSchreiben(data);
}

void Eprommer590068::steuerregisterSchreiben(uint8_t neu)
{
    const uint8_t alt = d4_;
    typwahlPruefen();
    // Impuls: steigende Flanke merkt die Zeit, fallende brennt (Bedingungen von VORHER).
    if (!(alt & BIT_IMPULS) && (neu & BIT_IMPULS)) impuls_ab_ = jetzt();
    if ((alt & BIT_IMPULS) && !(neu & BIT_IMPULS)) impulsEnde();
    d4_ = neu;

    const uint8_t wechsel = uint8_t((alt ^ neu) & (BIT_VPP | BIT_VERS1 | BIT_VERS));
    if (!wechsel) return;
    std::string t = "D4H " + hex(neu, 2) + ":";
    auto an = [&](uint8_t b) { return (neu & b) != 0; };
    if (wechsel & BIT_VERS1) t += an(BIT_VERS1) ? " Bit 3 ein" : " Bit 3 aus";
    if (wechsel & BIT_VERS)  t += an(BIT_VERS) ? " Versorgung ein (Bit 4)" : " Versorgung aus (Bit 4)";
    if (wechsel & BIT_VPP) {
        const bool beide = (neu & BIT_VPP) == BIT_VPP;
        if (beide)                  t += " Programmierspannung ein (Bit 0+1)";
        else if (!(neu & BIT_VPP))  t += " Programmierspannung aus";
        else                        t += " nur Bit " + std::to_string((neu & 1) ? 0 : 1) + " [?]";
    }
    log(t);

    const bool vpp_alt = (alt & BIT_VPP) == BIT_VPP, vpp_neu = (neu & BIT_VPP) == BIT_VPP;
    if (!vpp_alt && vpp_neu) {
        brennen_aktiv_ = true;
        brenn_n_ = brenn_wirksam_ = brenn_max_ = 0;
        brenn_min_ = ~0ull;
        vorher_ = zellen_;
        impulse_je_adresse_.assign(groesse(typ_), 0);
        brenn_gewarnt_ = breite_gewarnt_ = false;
    }
    if (vpp_alt && !vpp_neu && brennen_aktiv_) brennphaseEnde();
    if (!(alt & BIT_VERS) && (neu & BIT_VERS)) {
        lese_n_ = 0;
        lese_min_ = 0xFFFF;
        lese_max_ = 0;
        lese_typ_gewarnt_ = false;
    }
    if ((alt & BIT_VERS) && !(neu & BIT_VERS)) lesephaseEnde();
}

void Eprommer590068::impulsEnde()
{
    const uint64_t dauer = jetzt() - impuls_ab_;
    ++impulse_gesamt_;
    const bool spannung = (d4_ & BIT_VPP) == BIT_VPP && (d4_ & BIT_VERS);
    if (!spannung) {
        if (!brenn_gewarnt_) {
            brenn_gewarnt_ = true;
            log("Impuls ohne Versorgung/Programmierspannung — ohne Wirkung");
        }
        return;
    }
    ++brenn_n_;
    if (dauer < brenn_min_) brenn_min_ = dauer;
    if (dauer > brenn_max_) brenn_max_ = dauer;
    if (typ_ == Typ::Keiner) {
        if (!brenn_gewarnt_) { brenn_gewarnt_ = true; log("Impuls bei leerem Sockel"); }
        return;
    }
    const Typ eingestellt = eingestelltLocked();
    if (eingestellt != typ_) {
        if (!brenn_gewarnt_) {
            brenn_gewarnt_ = true;
            log(std::string("Brennen mit falschem Typ: eingestellt ") + name(eingestellt)
                + ", gesteckt " + name(typ_) + " — ohne Wirkung [?]");
        }
        return;
    }
    // Datenblattwerte nur als Hinweis (Wirkung eines falschen Impulses ist nicht belegt).
    const double d = ms(dauer);
    const bool passt = typ_ == Typ::U555 ? (d >= 0.1 && d <= 1.0) : (d >= 45.0 && d <= 55.0);
    if (!passt && !breite_gewarnt_) {
        breite_gewarnt_ = true;
        log("Hinweis: Impulsbreite " + ms1(d) + " ausserhalb des Datenblatts ("
            + (typ_ == Typ::U555 ? "0,1–1 ms" : "45–55 ms") + "), gebrannt wird trotzdem");
    }
    const uint16_t a = uint16_t(adresseLocked() & (groesse(typ_) - 1));
    if (a < impulse_je_adresse_.size() && impulse_je_adresse_[a] < 0xFFFF) ++impulse_je_adresse_[a];
    const uint8_t neu = uint8_t(zellen_[a] & datenleitungen());   // nur 1 → 0
    if (neu != zellen_[a]) { zellen_[a] = neu; geaendert_ = true; }
    ++brenn_wirksam_;
}

void Eprommer590068::brennphaseEnde()
{
    brennen_aktiv_ = false;
    if (brenn_n_ == 0) { log("Programmierphase ohne Impuls"); return; }
    size_t adressen = 0, geaendert = 0;
    unsigned jmin = 0xFFFF, jmax = 0;
    for (size_t i = 0; i < impulse_je_adresse_.size(); ++i) {
        const unsigned n = impulse_je_adresse_[i];
        if (!n) continue;
        ++adressen;
        if (n < jmin) jmin = n;
        if (n > jmax) jmax = n;
    }
    if (vorher_.size() == zellen_.size())
        for (size_t i = 0; i < zellen_.size(); ++i) geaendert += vorher_[i] != zellen_[i];
    std::string t = "Programmierphase: " + std::to_string(brenn_n_) + " Impulse ("
                  + std::to_string(brenn_wirksam_) + " wirksam) an " + std::to_string(adressen)
                  + " Adressen";
    if (adressen) t += ", je Adresse " + std::to_string(jmin) + (jmin == jmax ? "" : "–" + std::to_string(jmax));
    t += ", Impuls " + ms1(ms(brenn_min_));
    if (brenn_max_ != brenn_min_) t += "–" + ms1(ms(brenn_max_));
    t += ", " + std::to_string(geaendert) + " Byte geändert";
    log(t);
}

void Eprommer590068::lesephaseEnde()
{
    if (lese_n_ == 0) return;
    log("Gelesen: " + std::to_string(lese_n_) + " Zugriffe, Adressen " + hex(lese_min_, 3) + "–"
        + hex(lese_max_, 3));
    lese_n_ = 0;
}

// ─── Sockel ──────────────────────────────────────────────────────────────────

bool Eprommer590068::einlegen(const std::vector<uint8_t>& daten, Typ typ, const std::string& datei,
                              std::string* fehler)
{
    if (typ == Typ::Keiner) typ = daten.size() <= 1024 ? Typ::U555 : Typ::U2716;
    if (daten.size() > groesse(typ)) {
        if (fehler)
            *fehler = "Abbild ist " + std::to_string(daten.size()) + " Byte gross, ein "
                    + name(typ) + " fasst " + std::to_string(groesse(typ));
        return false;
    }
    std::lock_guard<std::mutex> lk(sperre_);
    typ_ = typ;
    zellen_.assign(groesse(typ), 0xFF);
    std::copy(daten.begin(), daten.end(), zellen_.begin());
    datei_ = datei;
    geaendert_ = false;
    if (brennen_aktiv_) {
        impulse_je_adresse_.assign(groesse(typ), 0);
        vorher_ = zellen_;
    }
    log(std::string("Sockel: ") + name(typ) + " eingelegt"
        + (datei.empty() ? std::string(" (leer)") : " (" + datei + ")")
        + (d4_ & (BIT_VERS | BIT_VPP) ? " — bei anliegender Spannung" : ""));
    return true;
}

bool Eprommer590068::einlegenDatei(const std::string& pfad, Typ typ, std::string* fehler)
{
    std::ifstream f(pfad, std::ios::binary);
    if (!f) {
        if (fehler) *fehler = "Datei nicht lesbar: " + pfad;
        return false;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() > 2048) {
        if (fehler) *fehler = "Abbild ist " + std::to_string(d.size()) + " Byte gross (höchstens 2048)";
        return false;
    }
    return einlegen(d, typ, pfad, fehler);
}

void Eprommer590068::einlegenLeer(Typ typ)
{
    if (typ == Typ::Keiner) typ = Typ::U2716;
    einlegen({}, typ, "");
}

void Eprommer590068::entnehmen()
{
    std::lock_guard<std::mutex> lk(sperre_);
    if (typ_ == Typ::Keiner) return;
    log(std::string("Sockel: ") + name(typ_) + " entnommen" + (geaendert_ ? " (ungespeichert)" : ""));
    typ_ = Typ::Keiner;
    zellen_.clear();
    datei_.clear();
    geaendert_ = false;
}

bool Eprommer590068::speichern(const std::string& pfad, std::string* fehler)
{
    std::lock_guard<std::mutex> lk(sperre_);
    const std::string ziel = pfad.empty() ? datei_ : pfad;
    if (typ_ == Typ::Keiner || ziel.empty()) {
        if (fehler) *fehler = typ_ == Typ::Keiner ? "Sockel ist leer" : "keine Datei angegeben";
        return false;
    }
    std::ofstream f(ziel, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(zellen_.data()), std::streamsize(zellen_.size()));
    if (!f) {
        if (fehler) *fehler = "Datei nicht schreibbar: " + ziel;
        return false;
    }
    datei_ = ziel;
    geaendert_ = false;
    log("Sockel: gespeichert nach " + ziel);
    return true;
}

void Eprommer590068::uvLoeschen()
{
    std::lock_guard<std::mutex> lk(sperre_);
    if (typ_ == Typ::Keiner) return;
    for (auto& z : zellen_) {
        if (z != 0xFF) geaendert_ = true;
        z = 0xFF;
    }
    log(std::string("Sockel: ") + name(typ_) + " UV-gelöscht");
}

// ─── Zustand ─────────────────────────────────────────────────────────────────

bool Eprommer590068::steckt() const { std::lock_guard<std::mutex> lk(sperre_); return typ_ != Typ::Keiner; }
Eprommer590068::Typ Eprommer590068::typ() const { std::lock_guard<std::mutex> lk(sperre_); return typ_; }
std::vector<uint8_t> Eprommer590068::inhalt() const { std::lock_guard<std::mutex> lk(sperre_); return zellen_; }
std::string Eprommer590068::datei() const { std::lock_guard<std::mutex> lk(sperre_); return datei_; }
bool Eprommer590068::geaendert() const { std::lock_guard<std::mutex> lk(sperre_); return geaendert_; }
uint8_t Eprommer590068::steuerregister() const { std::lock_guard<std::mutex> lk(sperre_); return d4_; }
Eprommer590068::Typ Eprommer590068::eingestellterTyp() const { std::lock_guard<std::mutex> lk(sperre_); return eingestelltLocked(); }
uint16_t Eprommer590068::adresse() const { std::lock_guard<std::mutex> lk(sperre_); return adresseLocked(); }
uint64_t Eprommer590068::leseZugriffe() const { std::lock_guard<std::mutex> lk(sperre_); return lese_gesamt_; }
uint64_t Eprommer590068::brennImpulse() const { std::lock_guard<std::mutex> lk(sperre_); return impulse_gesamt_; }

std::vector<std::string> Eprommer590068::protokoll() const
{
    std::lock_guard<std::mutex> lk(sperre_);
    return {protokoll_.begin(), protokoll_.end()};
}

std::vector<std::string> Eprommer590068::protokollNeu()
{
    std::lock_guard<std::mutex> lk(sperre_);
    uint64_t neu = protokoll_nr_ - abgeholt_;
    if (neu > protokoll_.size()) neu = protokoll_.size();
    abgeholt_ = protokoll_nr_;
    return {protokoll_.end() - std::ptrdiff_t(neu), protokoll_.end()};
}
