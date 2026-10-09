/**
 * @file platte.cpp
 * @brief Winchesterlaufwerk am P8000-WDC — s. platte.h (AP P13b).
 */
#include "core/peripherals/winchester/platte.h"

#include "core/logger.h"
#include "core/util/zustand.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace k1520::winchester {

uint16_t crcCcitt(uint16_t crc, uint8_t byte)
{
    crc ^= static_cast<uint16_t>(byte << 8);
    for (int i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    return crc;
}

// ─── Typen und PAR-Sektor ────────────────────────────────────────────────────

const std::vector<Typ>& typen()
{
    // sa.format Z. 36–40 (WEGA 3.x); 1380/10/18 = WEGA-3.1-Abbild für den AVR-Emulator (§11)
    static const std::vector<Typ> t = {
        {"ROB K5504.50", {1024, 5, 18}, 1024, 1, 203, 209, 251, 253, 241, 243, "K5504.50", "k5504"},
        {"NEC D5126   ", {615, 4, 18}, 128, 12, 203, 209, 248, 250, 239, 241, "D5126", "d5126"},
        {"NEC D5146   ", {615, 8, 18}, 128, 12, 203, 209, 248, 250, 239, 241, "D5146", "d5146"},
        {"ROBOTRON VS ", {820, 6, 18}, 820, 1, 203, 209, 251, 253, 241, 243, "VS", "vs"},
        {"WDC-Emulator", {1380, 10, 18}, 1380, 1, 203, 209, 251, 253, 241, 243, "WEGA31-AVR", "avr"},
    };
    return t;
}

const Typ* typNachName(const std::string& name)
{
    for (const auto& t : typen())
        if (name == t.name) return &t;
    return nullptr;
}

const Typ* typNachKuerzel(const std::string& kuerzel)
{
    std::string k = kuerzel;
    for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& t : typen())
        if (k == t.kuerzel) return &t;
    return nullptr;
}

std::string kuerzelAusDateiname(const std::string& pfad)
{
    // `<name>.<kuerzel>.img[.gz]` — das vorletzte Stück vor der Endung `.img`, nur wenn es ein Typkürzel ist
    std::string n = std::filesystem::path(pfad).filename().string();
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n.size() > 3 && n.compare(n.size() - 3, 3, ".gz") == 0) n.erase(n.size() - 3);
    if (n.size() < 5 || n.compare(n.size() - 4, 4, ".img") != 0) return "";
    n.erase(n.size() - 4);
    const auto punkt = n.rfind('.');
    if (punkt == std::string::npos) return "";
    const std::string k = n.substr(punkt + 1);
    return typNachKuerzel(k) ? k : "";
}

std::array<uint8_t, 512> parSektor(const Typ& t)
{
    std::array<uint8_t, 512> s{};
    std::memcpy(s.data(), "DEFEKT", 6);          // 0: Kenntext BTT
    s[6] = 0; s[7] = 0;                          // 6: Anzahl BTT-Bytes (LSB zuerst)
    s[8] = s[9] = s[10] = 0xFF;                  // 8: Endekennung (leere BTT)
    std::memcpy(s.data() + 256, "PARMTR", 6);    // 256: Kenntext PAR
    std::memcpy(s.data() + 262, t.kennung, 12);  // 262: Laufwerksbezeichnung
    s[277] = static_cast<uint8_t>(t.g.zylinder); s[278] = static_cast<uint8_t>(t.g.zylinder >> 8);
    s[279] = t.g.koepfe;
    s[280] = t.g.sektoren;
    s[281] = static_cast<uint8_t>(t.vorkomp); s[282] = static_cast<uint8_t>(t.vorkomp >> 8);
    s[283] = t.ramp;
    s[290] = t.ztk40; s[291] = t.ztk41;
    s[292] = t.zmn40; s[293] = t.zmx40; s[294] = t.zmn41; s[295] = t.zmx41;
    return s;
}

std::optional<Geometrie> parGeometrie(const uint8_t* s)
{
    // Firmware `t_par` (Abzug 4.2 wie Quelle Z. 842–959)
    if (std::memcmp(s + 256, "PARMTR", 6) != 0) return std::nullopt;
    for (int i = 262; i < 274; ++i)
        if (s[i] < 0x20 || s[i] >= 0x7B) return std::nullopt;
    const unsigned zyl = s[277] | (s[278] << 8);
    if (zyl < 100 || zyl >= 3000) return std::nullopt;
    if (s[279] < 2 || s[279] > 16) return std::nullopt;
    if (s[280] < 17 || s[280] > 18) return std::nullopt;
    const unsigned vork = s[281] | (s[282] << 8);
    if (vork > zyl) return std::nullopt;
    if (s[283] < 1 || s[283] > 21) return std::nullopt;
    if (s[290] < 0xB0 || s[290] >= s[291] || s[290] > 0xF0) return std::nullopt;
    if (s[293] < s[292] || s[295] < s[294] || s[294] >= s[292]) return std::nullopt;
    return Geometrie{static_cast<uint16_t>(zyl), s[279], s[280]};
}

// ─── Schreibfaden ────────────────────────────────────────────────────────────

/// Ein Arbeitsfaden je Platte, höchstens ein Auftrag zugleich (die Platte stößt keinen neuen an,
/// solange einer läuft).  Er kennt nur seine Kopie und die Datei — nie den Zustand der Platte.
class Schreibfaden {
public:
    using Auftrag = std::function<bool(std::string& fehler)>;
    struct Ergebnis { bool ok; std::string fehler; };

    ~Schreibfaden()
    {
        {
            std::lock_guard<std::mutex> l(m_);
            ende_ = true;
        }
        cv_.notify_all();
        if (t_.joinable()) t_.join();
    }
    void uebergeben(Auftrag a)
    {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return !auftrag_ && !aktiv_; });
        auftrag_ = std::move(a);
        if (!t_.joinable()) t_ = std::thread([this] { lauf(); });
        cv_.notify_all();
    }
    void warten()
    {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return !auftrag_ && !aktiv_; });
    }
    bool beschaeftigt() const
    {
        std::lock_guard<std::mutex> l(m_);
        return auftrag_ || aktiv_;
    }
    /// Ergebnis des zuletzt abgeschlossenen Auftrags, einmal abholbar.
    std::optional<Ergebnis> abholen()
    {
        std::lock_guard<std::mutex> l(m_);
        auto e = std::move(ergebnis_);
        ergebnis_.reset();
        return e;
    }

private:
    void lauf()
    {
        std::unique_lock<std::mutex> l(m_);
        for (;;) {
            cv_.wait(l, [&] { return ende_ || auftrag_; });
            if (!auftrag_) return;   // ende_ und nichts mehr zu tun
            Auftrag a = std::move(auftrag_);
            auftrag_ = nullptr;
            aktiv_ = true;
            l.unlock();
            std::string f;
            const bool ok = a(f);
            l.lock();
            ergebnis_ = Ergebnis{ok, std::move(f)};
            aktiv_ = false;
            cv_.notify_all();
        }
    }

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread t_;
    Auftrag auftrag_;
    bool aktiv_ = false, ende_ = false;
    std::optional<Ergebnis> ergebnis_;
};

// ─── Datei ───────────────────────────────────────────────────────────────────

Platte::Platte() = default;
Platte::~Platte() { schliessen(); }

bool Platte::neu(const std::string& pfad, const Typ& t, std::string* fehler, bool mit_par)
{
    std::vector<uint8_t> d(size_t(t.g.bytes()), 0xE5);   // Füllwert nach Formatieren
    if (mit_par) {                                         // unformatiert: auch Z0/K0/S1 ohne Parametersatz
        const auto par = parSektor(t);
        std::memcpy(d.data(), par.data(), par.size());
    }
    std::string f;
    if (!gzip::speichern(pfad, d.data(), d.size(), gzip::artNachEndung(pfad), gzip::STUFE_VORGABE, &f)) {
        if (fehler) *fehler = "Abbild nicht anlegbar: " + f;
        return false;
    }
    return true;
}

std::optional<Geometrie> Platte::geometrieAusGroesse(uint64_t bytes)
{
    std::optional<Geometrie> g;
    for (const auto& t : typen()) {
        if (t.g.bytes() != bytes) continue;
        if (g && !(*g == t.g)) return std::nullopt;   // mehrdeutig (D5146 ≡ VS)
        g = t.g;
    }
    return g;
}

bool Platte::oeffnen(const std::string& pfad) { return oeffnen(pfad, Config{}); }

bool Platte::oeffnen(const std::string& pfad, const Config& cfg)
{
    schliessen();
    fehler_.clear();
    cfg_ = cfg;
    std::error_code ec;
    if (!std::filesystem::exists(pfad, ec)) { fehler_ = "Abbild nicht gefunden: " + pfad; return false; }
    {   // geschrieben wird später im Schreibfaden — schreibgeschützt fällt HIER auf, nicht dort
        std::fstream probe(pfad, std::ios::in | std::ios::out | std::ios::binary);
        if (!probe) { fehler_ = "Abbild nicht zu öffnen: " + pfad; return false; }
    }
    std::string lf;
    if (!gzip::laden(pfad, abbild_, &art_, &lf)) {
        fehler_ = "Abbild nicht lesbar: " + lf;
        abbild_.clear();
        return false;
    }
    const uint64_t groesse = abbild_.size();
    pfad_ = pfad;

    const auto par = groesse >= uint64_t(SEKTOR) ? parGeometrie(abbild_.data()) : std::nullopt;
    par_im_abbild_ = par;

    if (cfg.geometrie)      geo_ = *cfg.geometrie;
    else if (par)           geo_ = *par;
    else if (auto g = geometrieAusGroesse(groesse)) geo_ = *g;
    else {
        fehler_ = "Geometrie unbekannt (kein PAR-Sektor, Größe " + std::to_string(groesse) + " B)";
        abbild_.clear();
        return false;
    }
    if (geo_.bytes() != groesse) {
        fehler_ = "Dateigröße " + std::to_string(groesse) + " B passt nicht zu " +
                  std::to_string(geo_.zylinder) + "/" + std::to_string(geo_.koepfe) + "/" +
                  std::to_string(geo_.sektoren);
        abbild_.clear();
        return false;
    }
    par_ueberlagert_ = false;
    if (!par && cfg.par_ergaenzen) {    // [P5]
        Typ t{"P8000-Platte", geo_, geo_.zylinder, 1, 203, 209, 251, 253, 241, 243, ""};
        for (const auto& k : typen())
            if (k.g == geo_) t = k;
        par_ = parSektor(t);
        par_ueberlagert_ = true;
        LOG_INFO("WDC", "Platte %s: kein PAR-Sektor auf Z0/K0/S1 — ergänzt (%u/%u/%u)", pfad.c_str(),
                 geo_.zylinder, geo_.koepfe, geo_.sektoren);
    }
    unformatiert_.assign(size_t(geo_.zylinder) * geo_.koepfe, 0);
    cache_.clear();
    zyl_ = 0;
    seek_ende_ = 0;
    bereit_ab_ = cfg.startzeit_takte;
    letzt_schreiben_ = 0;
    sektor_neu_.assign(size_t(geo_.zylinder) * geo_.koepfe * geo_.sektoren, 0);
    abbild_neu_ = false;
    in_arbeit_.clear();
    je_geschrieben_ = false;
    schreibfehler_.clear();
    offen_ = true;
    if (gepackt()) LOG_INFO("WDC", "Platte %s: gzip-gepackt, %llu B entpackt", pfad.c_str(),
                            static_cast<unsigned long long>(groesse));
    return true;
}

void Platte::schliessen()
{
    if (!offen_) return;
    if (!flush())
        LOG_ERROR("WDC", "Platte %s: beim Schließen NICHT gesichert (%s)", pfad_.c_str(), schreibfehler_.c_str());
    faden_.reset();   // wartet auf den Faden und beendet ihn
    cache_.clear();
    abbild_.clear();
    abbild_.shrink_to_fit();
    sektor_neu_.clear();
    offen_ = false;
}

bool Platte::sektorLesen(int zyl, int kopf, int sektor, uint8_t* d)
{
    if (!offen() || zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe ||
        sektor < 1 || sektor > geo_.sektoren)
        return false;
    std::memcpy(d, abbild_.data() + offset(zyl, kopf, sektor), SEKTOR);
    return true;
}

bool Platte::sektorSchreiben(int zyl, int kopf, int sektor, const uint8_t* d)
{
    if (!offen() || zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe ||
        sektor < 1 || sektor > geo_.sektoren)
        return false;
    std::memcpy(abbild_.data() + offset(zyl, kopf, sektor), d, SEKTOR);
    sektor_neu_[sektorIndex(zyl, kopf, sektor)] = 1;
    abbild_neu_ = true;
    if (zyl == 0 && kopf == 0 && sektor == 1) par_ueberlagert_ = false;
    return true;
}

bool Platte::sektorSicht(int zyl, int kopf, int sektor, uint8_t* d)
{
    if (par_ueberlagert_ && zyl == 0 && kopf == 0 && sektor == 1) {
        std::memcpy(d, par_.data(), SEKTOR);
        return true;
    }
    return sektorLesen(zyl, kopf, sektor, d);
}

// ─── Mechanik ────────────────────────────────────────────────────────────────

void Platte::schritt(bool nach_innen, uint64_t t)
{
    const int neu = std::clamp(zyl_ + (nach_innen ? 1 : -1), 0, std::max(0, geo_.zylinder - 1));
    if (neu != zyl_) {
        zylinderVerlassen();
        zyl_ = neu;
    }
    seek_ende_ = t + cfg_.seek_takte;
}

// ─── Spursynthese ────────────────────────────────────────────────────────────

namespace {
// sc_tab der Firmware (Interleave 2:1), Kopf h beginnt mit sc_tab[(i − h) mod n] (ft_trk)
constexpr int SLOT_3X = 570;   // Abstand der Kennfelder, die die 3.x beim Formatieren schreibt
constexpr uint8_t SC_TAB[18] = {1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17, 9, 18};

struct Schreiber {
    std::vector<uint16_t>& s;
    int pos;
    uint16_t crc = 0xFFFF;
    void roh(uint16_t w) { s[size_t(pos++ % Platte::BYTES_JE_SPUR)] = w; }
    void fuell(uint8_t b, int n) { for (int i = 0; i < n; ++i) roh(b); }
    void marke(uint8_t b) { crc = crcCcitt(crc, b); roh(static_cast<uint16_t>(b | Platte::MARKE)); }
    void daten(uint8_t b) { crc = crcCcitt(crc, b); roh(b); }
    void crcAus() { const uint16_t c = crc; roh(static_cast<uint8_t>(c >> 8)); roh(static_cast<uint8_t>(c)); }
};
}  // namespace

std::vector<uint16_t> Platte::synthetisiere(int zyl, int kopf)
{
    std::vector<uint16_t> s(BYTES_JE_SPUR, 0x00);
    if (!offen() || zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe) return s;
    if (unformatiert_[spurIndex(zyl, kopf)]) return s;   // [P2]
    const int n = geo_.sektoren;
    std::array<uint8_t, SEKTOR> d{};
    const bool v3 = cfg_.spurformat == Spurformat::V3x;
    // 3.x: ganze Spur FF, erste Kennfeldmarke bei Byte 36, Abstand 570, Sektoren der Reihe nach
    // (kein Interleave, kein Kopfversatz), Lücke hinter der Kennfeld-CRC FF×2 · 00×18 · FF×8
    if (v3) std::fill(s.begin(), s.end(), uint16_t(0xFF));
    for (int i = 0; i < n; ++i) {
        const int sek = v3 ? (i + 1) : SC_TAB[(i - kopf % n + n) % n];
        Schreiber w{s, v3 ? 36 + i * SLOT_3X : i * SLOT};
        if (!v3) w.fuell(0xFF, 18);
        w.crc = 0xFFFF;
        w.marke(0xA1); w.marke(0xA1); w.marke(0xA1);
        w.daten(0xFE);
        w.daten(static_cast<uint8_t>(zyl)); w.daten(static_cast<uint8_t>(zyl >> 8));
        w.daten(static_cast<uint8_t>(kopf)); w.daten(static_cast<uint8_t>(sek));
        w.crcAus();
        if (v3) {
            w.fuell(0xFF, 2);
            w.fuell(0x00, 18);
            w.fuell(0xFF, 8);
        } else {
            w.fuell(0xFF, 10);
            w.fuell(0x00, 7);
            w.fuell(0xFF, 11);
        }
        if (!sektorSicht(zyl, kopf, sek, d.data())) d.fill(0xE5);
        w.crc = 0xFFFF;
        w.marke(0xA1);
        w.daten(0xFB);
        for (uint8_t b : d) w.daten(b);
        w.crcAus();
        w.fuell(0xFF, 6);
    }
    return s;
}

Platte::Spur& Platte::spurRef(int kopf)
{
    auto it = cache_.find(kopf);
    if (it != cache_.end()) return it->second;
    Spur& s = cache_[kopf];
    s.strom = synthetisiere(zyl_, kopf);
    return s;
}

uint16_t Platte::lies(int kopf, int pos)
{
    if (!offen() || kopf < 0 || kopf >= geo_.koepfe) return 0x00;
    return spurRef(kopf).strom[size_t(pos) % BYTES_JE_SPUR];
}

void Platte::schreibe(int kopf, int pos, uint16_t wort, uint64_t t)
{
    if (!offen() || kopf < 0 || kopf >= geo_.koepfe) return;
    Spur& s = spurRef(kopf);
    uint16_t& z = s.strom[size_t(pos) % BYTES_JE_SPUR];
    if (z != wort) {
        z = wort;
        s.eigen = s.schmutzig = true;
    }
    letzt_schreiben_ = t;
}

const std::vector<uint16_t>& Platte::spur(int zyl, int kopf)
{
    if (zyl == zyl_ && kopf >= 0 && kopf < geo_.koepfe) return spurRef(kopf).strom;
    fremd_ = synthetisiere(zyl, kopf);
    return fremd_;
}

bool Platte::formatiert(int zyl, int kopf) const
{
    if (zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe) return false;
    return !unformatiert_[spurIndex(zyl, kopf)];
}

void Platte::setzeFormatiert(int zyl, int kopf, bool f)
{
    if (zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe) return;
    unformatiert_[spurIndex(zyl, kopf)] = f ? 0 : 1;
    if (zyl == zyl_) cache_.erase(kopf);
}

// ─── Zerlegung und Zurückschreiben ───────────────────────────────────────────

Platte::Zerlegung Platte::zerlege(int zyl, int kopf, const std::vector<uint16_t>& st) const
{
    Zerlegung z;
    const int N = static_cast<int>(st.size());
    if (N == 0) return z;
    auto at = [&](int i) { return st[size_t(((i % N) + N) % N)]; };
    struct Feld { int pos; int marken; };
    std::vector<std::pair<int, int>> ids;    // Position des FE, Sektor
    std::vector<Feld> dfs;                   // Position des FB, Anfang der Marken
    for (int p = 0; p < N; ++p) {
        if (!(at(p) & MARKE) || (at(p - 1) & MARKE)) continue;   // Anfang einer Markenfolge
        int q = p;
        while ((at(q + 1) & MARKE) && q - p < 8) ++q;
        const uint16_t k = at(q + 1);
        if (k & MARKE) continue;
        if (k == 0xFE) {
            uint16_t c = 0xFFFF;
            for (int i = p; i <= q + 7; ++i) c = crcCcitt(c, static_cast<uint8_t>(at(i)));
            if (c != 0) continue;
            const int cyl = (at(q + 2) & 0xFF) | ((at(q + 3) & 0xFF) << 8);
            const int hd = at(q + 4) & 0xFF, sc = at(q + 5) & 0xFF;
            if (cyl != zyl || hd != kopf || sc < 1 || sc > geo_.sektoren) continue;
            ids.push_back({q + 1, sc});
        } else if (k == 0xFB) {
            dfs.push_back({q + 1, p});
        }
    }
    std::vector<bool> gesehen(size_t(geo_.sektoren) + 1, false);
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto [pid, sc] = ids[i];
        if (gesehen[size_t(sc)]) continue;     // Doppel: das erste gilt
        gesehen[size_t(sc)] = true;
        ++z.kennfelder;
        // nächstes Kennfeld (zyklisch) begrenzt die Suche nach dem Datenfeld
        int grenze = N;
        for (const auto& [p2, s2] : ids) {
            (void)s2;
            const int d = ((p2 - pid) % N + N) % N;
            if (d > 0 && d < grenze) grenze = d;
        }
        int best = -1, best_d = N;
        for (const auto& f : dfs) {
            const int d = ((f.pos - pid) % N + N) % N;
            if (d > 6 && d < grenze && d < best_d) { best_d = d; best = static_cast<int>(&f - dfs.data()); }
        }
        if (best < 0) continue;
        const Feld& f = dfs[size_t(best)];
        uint16_t c = 0xFFFF;
        for (int j = f.marken; j != f.pos + 1 + SEKTOR + 2; ++j) c = crcCcitt(c, static_cast<uint8_t>(at(j)));
        if (c != 0) continue;
        std::vector<uint8_t> d(SEKTOR);
        for (int j = 0; j < SEKTOR; ++j) d[size_t(j)] = static_cast<uint8_t>(at(f.pos + 1 + j));
        z.daten[sc] = std::move(d);
    }
    z.formatiert = z.kennfelder == geo_.sektoren;
    return z;
}

void Platte::zurueckschreiben(int zyl, int kopf, Spur& s)
{
    const Zerlegung z = zerlege(zyl, kopf, s.strom);
    s.schmutzig = false;
    if (!z.formatiert) {
        unformatiert_[spurIndex(zyl, kopf)] = 1;
        if (z.kennfelder > 0)
            LOG_WARN("WDC", "Platte: Spur %d/%d mit nur %d von %d Kennfeldern — gilt als unformatiert",
                     zyl, kopf, z.kennfelder, geo_.sektoren);
        return;
    }
    unformatiert_[spurIndex(zyl, kopf)] = 0;
    std::array<uint8_t, SEKTOR> alt{};
    for (const auto& [sc, d] : z.daten) {
        if (sektorSicht(zyl, kopf, sc, alt.data()) && std::memcmp(alt.data(), d.data(), SEKTOR) == 0) continue;
        if (!sektorSchreiben(zyl, kopf, sc, d.data()))
            LOG_WARN("WDC", "Platte: Sektor %d/%d/%d nicht geschrieben (%s)", zyl, kopf, sc, pfad_.c_str());
    }
    if (static_cast<int>(z.daten.size()) < geo_.sektoren)
        LOG_WARN("WDC", "Platte: Spur %d/%d: %d Sektoren ohne gültiges Datenfeld — Abbild behält den alten Inhalt",
                 zyl, kopf, geo_.sektoren - static_cast<int>(z.daten.size()));
}

void Platte::zylinderVerlassen()
{
    for (auto& [kopf, s] : cache_)
        if (s.schmutzig) zurueckschreiben(zyl_, kopf, s);
    cache_.clear();
}

bool Platte::flush()
{
    if (!offen_) return true;
    for (auto& [kopf, s] : cache_)
        if (s.schmutzig) zurueckschreiben(zyl_, kopf, s);
    return dateiSchreiben(/*warten=*/true);
}

bool Platte::schmutzig() const
{
    if (abbild_neu_) return true;
    for (const auto& [kopf, s] : cache_)
        if (s.schmutzig) return true;
    return false;
}

bool Platte::schreibtGerade() const { return faden_ && faden_->beschaeftigt(); }

void Platte::warteAufSchreiben()
{
    if (faden_) faden_->warten();
    ergebnisAbholen();
}

bool Platte::autoFlush(uint64_t t)
{
    ergebnisAbholen();
    if (!offen_ || !schmutzig() || t < letzt_schreiben_ + cfg_.flush_pause) return false;
    // Zweite Schranke in WIRTSzeit: nicht öfter als alle `min_abstand_ms` in die Datei, und nie,
    // solange der Faden noch am vorigen Auftrag sitzt — die Änderung wartet im Speicher.
    if (schreibtGerade()) return false;
    if (je_geschrieben_ &&
        std::chrono::steady_clock::now() - letzt_datei_ < std::chrono::milliseconds(cfg_.min_abstand_ms))
        return false;
    for (auto& [kopf, s] : cache_)
        if (s.schmutzig) zurueckschreiben(zyl_, kopf, s);
    dateiSchreiben(/*warten=*/false);
    return true;
}

bool Platte::dateiSchreiben(bool warten)
{
    if (faden_) faden_->warten();   // ein laufender Auftrag zuerst; sein Ergebnis zählt mit
    ergebnisAbholen();
    if (abbild_neu_) {
        Schreibfaden::Auftrag a;
        if (gepackt()) {
            // Ganze Kopie (47 MB ≈ 10 ms) — gepackt wird im Faden, nicht hier.
            auto kopie = std::make_shared<std::vector<uint8_t>>(abbild_);
            a = [pfad = pfad_, kopie, stufe = cfg_.gzip_stufe](std::string& f) {
                return gzip::speichern(pfad, kopie->data(), kopie->size(), gzip::Art::Gzip, stufe, &f);
            };
        } else {
            // Roh: nur die geänderten Sektoren, an Ort und Stelle
            std::vector<std::pair<uint64_t, std::array<uint8_t, SEKTOR>>> sek;
            for (size_t i = 0; i < sektor_neu_.size(); ++i) {
                if (!sektor_neu_[i]) continue;
                std::array<uint8_t, SEKTOR> d;
                std::memcpy(d.data(), abbild_.data() + i * SEKTOR, SEKTOR);
                sek.emplace_back(uint64_t(i) * SEKTOR, d);
                in_arbeit_.push_back(i);
            }
            auto liste = std::make_shared<decltype(sek)>(std::move(sek));
            a = [pfad = pfad_, liste](std::string& f) {
                std::fstream datei(pfad, std::ios::in | std::ios::out | std::ios::binary);
                if (!datei) { f = "nicht zu öffnen: " + pfad; return false; }
                for (const auto& [off, d] : *liste) {
                    datei.seekp(static_cast<std::streamoff>(off));
                    datei.write(reinterpret_cast<const char*>(d.data()), SEKTOR);
                }
                datei.flush();
                if (!datei) { f = "Schreibfehler: " + pfad; return false; }
                return true;
            };
        }
        std::fill(sektor_neu_.begin(), sektor_neu_.end(), uint8_t(0));
        abbild_neu_ = false;
        if (!faden_) faden_ = std::make_unique<Schreibfaden>();
        faden_->uebergeben(std::move(a));
        letzt_datei_ = std::chrono::steady_clock::now();
        je_geschrieben_ = true;
    }
    if (!warten) return true;
    if (faden_) faden_->warten();
    ergebnisAbholen();
    return !abbild_neu_;
}

void Platte::ergebnisAbholen()
{
    if (!faden_) return;
    const auto e = faden_->abholen();
    if (!e) return;
    if (e->ok) {
        in_arbeit_.clear();
        return;
    }
    // Gescheitert: wieder als ungesichert führen — der nächste Versuch schreibt den AKTUELLEN Inhalt
    schreibfehler_ = e->fehler;
    LOG_WARN("WDC", "Platte: Datei nicht geschrieben (%s) — bleibt ungesichert", e->fehler.c_str());
    for (size_t i : in_arbeit_) sektor_neu_[i] = 1;
    in_arbeit_.clear();
    abbild_neu_ = true;
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void Platte::serialize(std::vector<uint8_t>& out) const
{
    auto a = ZAr::schreiber(out);
    auto* self = const_cast<Platte*>(this);
    a.num(self->geo_.zylinder); a.num(self->geo_.koepfe); a.num(self->geo_.sektoren);
    a.num(self->zyl_); a.num(self->seek_ende_); a.num(self->bereit_ab_); a.num(self->letzt_schreiben_);
    a.flag(self->par_ueberlagert_);
    a.raw(self->par_.data(), par_.size());
    uint32_t n = static_cast<uint32_t>(unformatiert_.size());
    a.num(n);
    a.raw(self->unformatiert_.data(), n);
    uint32_t eigene = 0;
    for (const auto& [k, s] : cache_) if (s.eigen) ++eigene;
    a.num(eigene);
    for (auto& [k, s] : self->cache_) {
        if (!s.eigen) continue;
        int32_t kopf = k;
        a.num(kopf); a.flag(s.schmutzig);
        for (auto& w : s.strom) a.num(w);
    }
}

bool Platte::deserialize(const uint8_t*& p, const uint8_t* end)
{
    auto a = ZAr::leser(p, end);
    Geometrie g;
    a.num(g.zylinder); a.num(g.koepfe); a.num(g.sektoren);
    if (!a.ok || !(g == geo_)) return false;
    int zyl = 0; uint64_t se = 0, ba = 0, ls = 0; bool pu = false;
    a.num(zyl); a.num(se); a.num(ba); a.num(ls); a.flag(pu);
    std::array<uint8_t, SEKTOR> par{};
    a.raw(par.data(), par.size());
    uint32_t n = 0;
    a.num(n);
    if (!a.ok || n != unformatiert_.size()) return false;
    std::vector<uint8_t> unf(n);
    a.raw(unf.data(), n);
    uint32_t eigene = 0;
    a.num(eigene);
    std::map<int, Spur> cache;
    for (uint32_t i = 0; i < eigene && a.ok; ++i) {
        int32_t kopf = 0; Spur s; s.eigen = true;
        a.num(kopf); a.flag(s.schmutzig);
        s.strom.resize(BYTES_JE_SPUR);
        for (auto& w : s.strom) a.num(w);
        cache[kopf] = std::move(s);
    }
    if (!a.ok) return false;
    zyl_ = zyl; seek_ende_ = se; bereit_ab_ = ba; letzt_schreiben_ = ls;
    par_ueberlagert_ = pu; par_ = par;
    unformatiert_ = std::move(unf);
    cache_ = std::move(cache);
    p = a.p;
    return true;
}

}  // namespace k1520::winchester
