/**
 * @file kassetten_laufwerk.cpp
 * @brief Kassettenlaufwerk, siehe kassetten_laufwerk.h.
 */

#include "core/peripherals/kassette/kassetten_laufwerk.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "core/peripherals/kassette/k52_codec.h"

namespace kassette {
namespace {

// Zahlen: k5200.md §6 (K5200), hardware_3x.md/pk-3 (PK3), MC80.2x-Handbuch §3.1.8 (SK 1200); [?] = Annahme.
const LaufwerksProfil kProfile[] = {
    {ProfilId::K5200_38, "K5200-38", 381.0, 1500.0, 12000.0, 0.300, 0.018, 4.0, 0.2, 3.81, 14.0, true},
    // 19 cm/s: Zeiten ausser Anlauf nicht angegeben [?] — Startzeit verdoppelt, uebrige gleich.
    {ProfilId::K5200_19, "K5200-19", 190.5, 1500.0, 6000.0, 0.300, 0.036, 4.0, 0.2, 3.81, 14.0, false},
    // LW1200 = SK 1200 (MC80): 4,76 cm/s, 32 Bit/mm → 1523 Bit/s; Anlauf/Bremsweg/Suchlauf/Spalt nicht belegt [?].
    {ProfilId::LW1200, "LW1200", 47.6, 476.0, 1523.2, 0.500, 0.050, 2.0, 0.3, 3.81, 14.0, false},
    // PK-3 (MC80.33): 0,254 m/s, Suchlauf 1 m/s, max. 8000 Bit/s (pk-3-doku); Zeiten [?].
    {ProfilId::PK3, "PK3", 254.0, 1000.0, 8000.0, 0.500, 0.030, 4.0, 0.3, 3.81, 14.0, false},
    {ProfilId::SK1200, "SK1200", 47.6, 476.0, 1523.2, 0.500, 0.050, 2.0, 0.3, 3.81, 14.0, false},
};

constexpr double kSchrittMax = 0.002;  ///< groesste Teilschritt der Bandmechanik (s)
constexpr double kFlushPause = 0.5;    ///< Schreibpause bis zum Zurueckschreiben (s Maschinenzeit)
constexpr size_t kRxMax = 1u << 16;

inline int64_t um(double x) { return static_cast<int64_t>(std::llround(x)); }

uint8_t bitrev(uint8_t b) {
    b = static_cast<uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = static_cast<uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = static_cast<uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

std::string klein(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool dateiLesen(const std::string& pfad, std::vector<uint8_t>& d, std::string& fehler) {
    std::ifstream f(pfad, std::ios::binary);
    if (!f) {
        fehler = "Kassettendatei nicht lesbar: " + pfad;
        return false;
    }
    d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

/// Ueber eine Nebendatei schreiben, damit ein Abbruch die alte Datei nicht halbiert.
bool dateiSchreibenAtomar(const std::string& pfad, const std::vector<uint8_t>& d, std::string& fehler) {
    const std::string tmp = pfad + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            fehler = "Kassettendatei nicht schreibbar: " + pfad;
            return false;
        }
        f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
        if (!f) {
            fehler = "Schreiben der Kassettendatei fehlgeschlagen: " + pfad;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, pfad, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        fehler = "Kassettendatei nicht ersetzbar: " + pfad;
        return false;
    }
    return true;
}

}  // namespace

const LaufwerksProfil& profil(ProfilId id) {
    for (const LaufwerksProfil& p : kProfile)
        if (p.id == id) return p;
    return kProfile[0];
}

const LaufwerksProfil* profilNachName(const std::string& name) {
    const std::string n = klein(name);
    for (const LaufwerksProfil& p : kProfile)
        if (klein(p.name) == n) return &p;
    return nullptr;
}

std::vector<ProfilId> alleProfile() {
    return {ProfilId::K5200_38, ProfilId::K5200_19, ProfilId::LW1200, ProfilId::PK3, ProfilId::SK1200};
}

KassettenLaufwerk::KassettenLaufwerk(ProfilId p, double takt_hz) : profil_(&kassette::profil(p)), takt_hz_(takt_hz) {}

// ─── Kassette ────────────────────────────────────────────────────────────────

bool KassettenLaufwerk::kassetteEinlegen(std::shared_ptr<KassettenMedium> m) {
    if (!m || medium_) return false;
    medium_ = std::move(m);
    pos_um_ = static_cast<double>(medium_->positionUm());
    v_mm_s_ = 0;
    rev_gespeichert_ = rev_gesehen_ = medium_->revision();
    pause_s_ = 0;
    leseNeu();
    return true;
}

bool KassettenLaufwerk::kassetteAuswerfen(bool erzwingen) {
    if (!medium_ || reserviert_ || ums_ || v_mm_s_ != 0.0) return false;
    schreibblockAbschliessen();
    if (gebunden()) {
        std::string f;
        if (!speichern(f) && !erzwingen) return false;
    }
    medium_->setPosition(um(pos_um_));
    medium_.reset();
    pfad_.clear();
    rx_.clear();
    echo_.clear();
    leseNeu();
    t_motor_ = 0;
    fehler_ = false;
    flush_faellig_ = false;
    return true;
}

bool KassettenLaufwerk::positionSetzen(int64_t um_) {
    if (!medium_ || v_mm_s_ != 0.0) return false;
    schreibblockAbschliessen();
    medium_->setPosition(um_);
    pos_um_ = static_cast<double>(medium_->positionUm());
    leseNeu();
    return true;
}

bool KassettenLaufwerk::wenden() {
    if (!medium_ || reserviert_ || ums_ || v_mm_s_ != 0.0) return false;
    schreibblockAbschliessen();
    medium_->wenden();
    pos_um_ = static_cast<double>(medium_->positionUm());
    leseNeu();
    return true;
}

// ─── Datei ───────────────────────────────────────────────────────────────────

bool KassettenLaufwerk::dateiSchreiben(std::string& fehler) {
    if (!dateiSchreibenAtomar(pfad_, k52::kodieren(*medium_), fehler)) return false;
    rev_gespeichert_ = medium_->revision();
    return true;
}

bool KassettenLaufwerk::dateiBinden(const std::string& pfad, std::string& fehler) {
    if (medium_) {
        fehler = "Es liegt schon eine Kassette im Laufwerk";
        return false;
    }
    std::shared_ptr<KassettenMedium> m;
    std::error_code ec;
    if (std::filesystem::exists(pfad, ec)) {
        std::vector<uint8_t> d;
        if (!dateiLesen(pfad, d, fehler)) return false;
        auto k = k52::dekodieren(d, fehler);
        if (!k) {
            fehler = pfad + ": " + fehler;
            return false;
        }
        m = std::make_shared<KassettenMedium>(std::move(*k));
        // Schreibbar?  Anhaengend oeffnen aendert nichts am Inhalt.
        std::ofstream probe(pfad, std::ios::binary | std::ios::app);
        if (!probe) {
            fehler = "Kassettendatei nicht schreibbar: " + pfad;
            return false;
        }
    } else {
        m = std::make_shared<KassettenMedium>(neue_kassette_);
        if (!dateiSchreibenAtomar(pfad, k52::kodieren(*m), fehler)) return false;
    }
    kassetteEinlegen(std::move(m));
    pfad_ = pfad;
    datei_fehler_.clear();
    return true;
}

bool KassettenLaufwerk::dateiLoesen(std::string& fehler) {
    if (!speichern(fehler)) return false;
    pfad_.clear();
    flush_faellig_ = false;
    return true;
}

bool KassettenLaufwerk::speichern(std::string& fehler) {
    if (!gebunden() || !medium_) return true;
    schreibblockAbschliessen();
    if (rev_gespeichert_ == medium_->revision()) return true;
    if (!dateiSchreiben(fehler)) {
        datei_fehler_ = fehler;
        return false;
    }
    datei_fehler_.clear();
    flush_faellig_ = false;
    return true;
}

bool KassettenLaufwerk::autoFlush() {
    if (!flush_faellig_) return false;
    flush_faellig_ = false;
    std::string f;
    return speichern(f);
}

void KassettenLaufwerk::pauseUndFlush(double dt) {
    if (!gebunden() || !medium_) return;
    const uint64_t rev = medium_->revision();
    if (rev == rev_gespeichert_) {
        pause_s_ = 0;
        rev_gesehen_ = rev;
        return;
    }
    if (rev != rev_gesehen_) {
        rev_gesehen_ = rev;  // noch geaendert: Pause beginnt von vorn
        pause_s_ = 0;
    } else {
        pause_s_ += dt;
        // Ein Block, der gerade geschrieben wird, ist noch nicht im Medium; erst danach zaehlt die Pause.
        if (pause_s_ >= kFlushPause && !wr_.aktiv) flush_faellig_ = true;
    }
}

// ─── Leitungen ───────────────────────────────────────────────────────────────

void KassettenLaufwerk::reservieren(bool an) {
    reserviert_ = an;
    if (!an) {
        fehler_ = false;  // Grundzustand
        if (!ums_) t_motor_ = 0;
    }
}
void KassettenLaufwerk::anwaehlen(bool an) { angewaehlt_ = an; }
void KassettenLaufwerk::vor(bool an) {
    vor_ = an;
    if (vor_ && rueck_) fehler_ = true;
}
void KassettenLaufwerk::rueck(bool an) {
    rueck_ = an;
    if (vor_ && rueck_) fehler_ = true;
}
void KassettenLaufwerk::hge(bool an) { hge_ = an; }
void KassettenLaufwerk::aufzeichnen(bool an) { auf_ = an; }
void KassettenLaufwerk::wiedergabe(bool an) { wid_ = an; }
void KassettenLaufwerk::umspulen() {
    if (reserviert_ && angewaehlt_ && bereit()) {
        ums_ = true;
        leseNeu();
    }
}

bool KassettenLaufwerk::bereit() const {
    return medium_ && reserviert_ && !fehler_ && !ums_ && t_motor_ >= profil_->t_anlauf_s;
}
bool KassettenLaufwerk::kng() const { return !(medium_ && reserviert_ && !fehler_); }
bool KassettenLaufwerk::schreibsperre() const {
    return !medium_ || medium_->schreibgeschuetzt(medium_->aktiveSeite());
}
bool KassettenLaufwerk::seiteB() const { return medium_ && medium_->aktiveSeite() == Seite::B; }

bool KassettenLaufwerk::aebAktiv() const {
    if (!medium_ || ums_) return false;
    return medium_->aebAktiv(um(pos_um_) + um(profil_->aeb_vorlauf_mm * 1000.0));
}

bool KassettenLaufwerk::bms() const {
    if (!medium_ || v_mm_s_ == 0.0 || auf_) return false;
    return medium_->blockBei(medium_->aktiveSeite(), um(pos_um_)) >= 0;
}

size_t KassettenLaufwerk::blockNummer() const {
    if (!medium_) return 0;
    return static_cast<size_t>(medium_->letzterBlockBis(medium_->aktiveSeite(), um(pos_um_)) + 1);
}

// ─── Zeit ────────────────────────────────────────────────────────────────────

void KassettenLaufwerk::clockTick(int takte) { advance(static_cast<double>(takte) / takt_hz_); }

void KassettenLaufwerk::advance(double sekunden) {
    double rest = sekunden;
    while (rest > 0) {
        const double c = std::min(rest, kSchrittMax);
        schritt(c);
        rest -= c;
    }
    pauseUndFlush(sekunden);
}

double KassettenLaufwerk::rampe(double v, double ziel) const {
    const double va = profil_->v_arbeit_mm_s;
    const double hoch = std::max(std::fabs(v), std::fabs(ziel));
    if (hoch > va * 1.0001) return profil_->v_such_mm_s / profil_->t_hge_s;
    if (std::fabs(ziel) < std::fabs(v)) return va * va / (2.0 * profil_->s_stopp_mm);  // Bremsen: s = v²/2a
    return va / profil_->t_start_s;
}

/// Geschwindigkeit in Rampen auf @p ziel bringen und die Strecke aufsummieren.  Ein Vorzeichenwechsel
/// laeuft ueber den Stillstand.
void KassettenLaufwerk::integriere(double ziel, double dt) {
    double rest = dt;
    int schutz = 0;
    while (rest > 1e-12 && schutz++ < 8) {
        const double zt = (v_mm_s_ * ziel < 0) ? 0.0 : ziel;
        const double dv = zt - v_mm_s_;
        if (dv == 0.0) {
            pos_um_ += v_mm_s_ * rest * 1000.0;
            return;
        }
        const double a = rampe(v_mm_s_, zt);
        const double tr = std::fabs(dv) / a;
        const double t = std::min(rest, tr);
        const double v1 = (t >= tr) ? zt : v_mm_s_ + std::copysign(a * t, dv);
        pos_um_ += (v_mm_s_ + v1) / 2.0 * t * 1000.0;
        v_mm_s_ = v1;
        rest -= t;
    }
}

void KassettenLaufwerk::schritt(double dt) {
    if (!medium_) {
        v_mm_s_ = 0;
        return;
    }
    const bool motor = (reserviert_ && !fehler_) || ums_;
    t_motor_ = motor ? t_motor_ + dt : 0.0;

    double ziel = 0.0;
    if (ums_) {
        ziel = -profil_->v_such_mm_s;
    } else if (bereit() && angewaehlt_) {
        const double v = hge_ ? profil_->v_such_mm_s : profil_->v_arbeit_mm_s;
        if (vor_ && !rueck_) ziel = v;
        else if (rueck_ && !vor_) ziel = -v;
    }

    const int64_t p0 = um(pos_um_);
    integriere(ziel, dt);
    const double ende = static_cast<double>(medium_->geometrie().laenge_um);
    if (pos_um_ <= 0.0) {
        pos_um_ = 0.0;
        v_mm_s_ = 0.0;
    } else if (pos_um_ >= ende) {
        pos_um_ = ende;
        v_mm_s_ = 0.0;
    }
    const int64_t p1 = um(pos_um_);
    // Umspulen endet, sobald die Lichtschranke den Anfangsvorspann erreicht (AEB dabei ausgeblendet).
    if (ums_ && p1 + um(profil_->aeb_vorlauf_mm * 1000.0) <= int64_t(medium_->geometrie().vorspann_um)) ums_ = false;
    medium_->setPosition(p1);
    datenSchritt(p0, p1);
}

// ─── Daten ───────────────────────────────────────────────────────────────────

void KassettenLaufwerk::rxPush(uint8_t b) {
    if (rx_.size() >= kRxMax) rx_.pop_front();
    rx_.push_back(b);
}

std::optional<uint8_t> KassettenLaufwerk::holeByte() {
    if (rx_.empty()) return std::nullopt;
    const uint8_t b = rx_.front();
    rx_.pop_front();
    return b;
}

void KassettenLaufwerk::leseNeu() {
    rd_ = Lesen{};
}

bool KassettenLaufwerk::schreibeErlaubt() const {
    if (!medium_ || !reserviert_ || !angewaehlt_ || !auf_ || hge_ || !bereit() || ums_) return false;
    if (medium_->schreibgeschuetzt(medium_->aktiveSeite())) return false;
    const double va = profil_->v_arbeit_mm_s;
    return v_mm_s_ > 0.5 * va && v_mm_s_ <= 1.05 * va;
}

void KassettenLaufwerk::schreibblockAbschliessen() {
    if (!wr_.aktiv) return;
    if (medium_) medium_->schreibeBlock(medium_->aktiveSeite(), wr_.start, wr_.bytes);
    wr_.aktiv = false;
    wr_.bytes.clear();
}

bool KassettenLaufwerk::schreibeByte(uint8_t b) {
    if (!schreibeErlaubt()) return false;
    const int64_t pos = um(pos_um_);
    if (wr_.aktiv) {
        const int64_t ende = wr_.start + medium_->laengeVonBytes(wr_.bytes.size());
        // Laenger als zwei Bytelaengen kein Byte gekommen: das war der Block, dies ist ein neuer.
        if (pos - ende > 2 * medium_->laengeVonBytes(1) || wr_.bytes.size() >= kMaxBlockBytes) schreibblockAbschliessen();
    }
    if (!wr_.aktiv) {
        wr_.aktiv = true;
        wr_.start = pos;
        wr_.bytes.clear();
    }
    wr_.bytes.push_back(b);
    if (wid_) echo_.emplace_back(wr_.start + medium_->laengeVonBytes(wr_.bytes.size()) + um(profil_->spalt_mm * 1000.0), b);
    return true;
}

void KassettenLaufwerk::datenSchritt(int64_t p0, int64_t p1) {
    const Seite s = medium_->aktiveSeite();
    const double va = profil_->v_arbeit_mm_s;

    // ── Schreiben: der Kopf loescht, was er ueberfaehrt; der Block wird beim Abschluss abgelegt ──
    if (schreibeErlaubt() && p1 > p0) medium_->loesche(s, p0, p1);
    if (wr_.aktiv) {
        const int64_t ende = wr_.start + medium_->laengeVonBytes(wr_.bytes.size());
        if (!schreibeErlaubt() || p1 - ende > 2 * medium_->laengeVonBytes(1)) schreibblockAbschliessen();
    }

    // ── Echo (Lese-nach-Schreiben): Byte kommt, wenn es den Lesespalt erreicht hat ──
    if (v_mm_s_ <= 0.0 || !wid_) {
        if (v_mm_s_ <= 0.0) echo_.clear();
    } else {
        while (!echo_.empty() && echo_.front().first <= p1) {
            rxPush(echo_.front().second);
            echo_.pop_front();
        }
    }

    // ── Lesen vom Band ──
    const bool lese_ok = angewaehlt_ && wid_ && !auf_ && !hge_ && !ums_ && bereit() && v_mm_s_ != 0.0 &&
                         std::fabs(v_mm_s_) <= va * 1.05;
    if (!lese_ok) {
        // Bei Stillstand oder anderer Betriebsart beginnt das Lesen spaeter von vorn.
        if (rd_.aktiv || rd_.grenze_vor != INT64_MIN || rd_.grenze_rueck != INT64_MAX) leseNeu();
        return;
    }
    const int richtung = v_mm_s_ > 0 ? 1 : -1;
    if (rd_.aktiv && rd_.richtung != richtung) leseNeu();
    if (p1 == p0) return;

    for (int umlauf = 0; umlauf < 4096; ++umlauf) {
        if (rd_.aktiv) {
            // Bytes, deren Bandstueck den Kopf schon passiert hat.
            while (rd_.n < rd_.folge.size()) {
                const int64_t bis = medium_->laengeVonBytes(rd_.n + 1);
                const bool fertig = richtung > 0 ? (rd_.anker + bis <= p1) : (rd_.anker - bis >= p1);
                if (!fertig) break;
                rxPush(rd_.folge[rd_.n++]);
            }
            if (rd_.n < rd_.folge.size()) return;  // Block reicht ueber den Schritt hinaus
            rd_.aktiv = false;
        }
        // Naechsten Block suchen, dessen Anfang (rueckwaerts: Ende) der Kopf in diesem Schritt ueberfaehrt.
        if (richtung > 0) {
            const int64_t ab = std::max(p0, rd_.grenze_vor);
            const size_t i = medium_->ersterBlockAb(s, ab);
            if (i >= medium_->anzahlBloecke(s) || medium_->blockStart(s, i) > p1) return;
            rd_ = Lesen{};
            rd_.aktiv = true;
            rd_.richtung = 1;
            rd_.anker = medium_->blockStart(s, i);
            rd_.folge = medium_->block(s, i).bytes;
            rd_.grenze_vor = rd_.anker + 1;
        } else {
            const int64_t bis = std::min(p0, rd_.grenze_rueck);
            const int i = medium_->letzterBlockBis(s, bis);
            if (i < 0 || medium_->blockEnde(s, static_cast<size_t>(i)) < p1) return;
            const std::vector<uint8_t>& b = medium_->block(s, static_cast<size_t>(i)).bytes;
            rd_ = Lesen{};
            rd_.aktiv = true;
            rd_.richtung = -1;
            rd_.anker = medium_->blockEnde(s, static_cast<size_t>(i));
            rd_.folge.reserve(b.size());
            // Rueckwaerts kommt jedes Byte invertiert und mit umgekehrter Bitfolge, das letzte zuerst (treiber.md §2.4).
            for (size_t k = b.size(); k-- > 0;) rd_.folge.push_back(static_cast<uint8_t>(~bitrev(b[k])));
            rd_.grenze_rueck = rd_.anker - 1;
        }
    }
}

}  // namespace kassette
