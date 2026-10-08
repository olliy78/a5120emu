/**
 * @file platte.cpp
 * @brief Winchesterlaufwerk am P8000-WDC — s. platte.h (AP P13b).
 */
#include "core/peripherals/winchester/platte.h"

#include "core/logger.h"
#include "core/util/zustand.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

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
    // `<name>.<kuerzel>.img` — das vorletzte Stück vor der Endung `.img`, nur wenn es ein Typkürzel ist
    std::string n = std::filesystem::path(pfad).filename().string();
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

// ─── Datei ───────────────────────────────────────────────────────────────────

Platte::~Platte() { schliessen(); }

bool Platte::neu(const std::string& pfad, const Typ& t, std::string* fehler, bool mit_par)
{
    std::ofstream f(pfad, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (fehler) *fehler = "Abbild nicht anlegbar: " + pfad;
        return false;
    }
    auto par = parSektor(t);
    if (!mit_par) par.fill(0xE5);   // unformatiert: auch Z0/K0/S1 ohne Parametersatz
    f.write(reinterpret_cast<const char*>(par.data()), par.size());
    std::vector<char> block(1 << 20, static_cast<char>(0xE5));   // Füllwert nach Formatieren
    uint64_t rest = t.g.bytes() - par.size();
    while (rest > 0 && f) {
        const auto n = static_cast<std::streamsize>(std::min<uint64_t>(rest, block.size()));
        f.write(block.data(), n);
        rest -= static_cast<uint64_t>(n);
    }
    if (!f) {
        if (fehler) *fehler = "Abbild nicht vollständig geschrieben: " + pfad;
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
    const uint64_t groesse = std::filesystem::file_size(pfad, ec);
    if (ec) { fehler_ = "Abbild nicht gefunden: " + pfad; return false; }
    datei_.open(pfad, std::ios::in | std::ios::out | std::ios::binary);
    if (!datei_) { fehler_ = "Abbild nicht zu öffnen: " + pfad; return false; }
    pfad_ = pfad;

    std::array<uint8_t, SEKTOR> s0{};
    datei_.seekg(0);
    datei_.read(reinterpret_cast<char*>(s0.data()), SEKTOR);
    const bool s0_ok = static_cast<bool>(datei_);
    datei_.clear();
    const auto par = s0_ok ? parGeometrie(s0.data()) : std::nullopt;
    par_im_abbild_ = par;

    if (cfg.geometrie)      geo_ = *cfg.geometrie;
    else if (par)           geo_ = *par;
    else if (auto g = geometrieAusGroesse(groesse)) geo_ = *g;
    else {
        fehler_ = "Geometrie unbekannt (kein PAR-Sektor, Größe " + std::to_string(groesse) + " B)";
        datei_.close();
        return false;
    }
    if (geo_.bytes() != groesse) {
        fehler_ = "Dateigröße " + std::to_string(groesse) + " B passt nicht zu " +
                  std::to_string(geo_.zylinder) + "/" + std::to_string(geo_.koepfe) + "/" +
                  std::to_string(geo_.sektoren);
        datei_.close();
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
    return true;
}

void Platte::schliessen()
{
    if (!datei_.is_open()) return;
    flush();
    cache_.clear();
    datei_.close();
}

bool Platte::sektorLesen(int zyl, int kopf, int sektor, uint8_t* d)
{
    if (!offen() || zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe ||
        sektor < 1 || sektor > geo_.sektoren)
        return false;
    datei_.seekg(static_cast<std::streamoff>(offset(zyl, kopf, sektor)));
    datei_.read(reinterpret_cast<char*>(d), SEKTOR);
    const bool ok = static_cast<bool>(datei_);
    datei_.clear();
    return ok;
}

bool Platte::sektorSchreiben(int zyl, int kopf, int sektor, const uint8_t* d)
{
    if (!offen() || zyl < 0 || zyl >= geo_.zylinder || kopf < 0 || kopf >= geo_.koepfe ||
        sektor < 1 || sektor > geo_.sektoren)
        return false;
    datei_.seekp(static_cast<std::streamoff>(offset(zyl, kopf, sektor)));
    datei_.write(reinterpret_cast<const char*>(d), SEKTOR);
    datei_.flush();
    const bool ok = static_cast<bool>(datei_);
    datei_.clear();
    if (ok && zyl == 0 && kopf == 0 && sektor == 1) par_ueberlagert_ = false;
    return ok;
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
    for (int i = 0; i < n; ++i) {
        const int sek = SC_TAB[(i - kopf % n + n) % n];
        Schreiber w{s, i * SLOT};
        w.fuell(0xFF, 18);
        w.crc = 0xFFFF;
        w.marke(0xA1); w.marke(0xA1); w.marke(0xA1);
        w.daten(0xFE);
        w.daten(static_cast<uint8_t>(zyl)); w.daten(static_cast<uint8_t>(zyl >> 8));
        w.daten(static_cast<uint8_t>(kopf)); w.daten(static_cast<uint8_t>(sek));
        w.crcAus();
        w.fuell(0xFF, 10);
        w.fuell(0x00, 7);
        w.fuell(0xFF, 11);
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

void Platte::flush()
{
    for (auto& [kopf, s] : cache_)
        if (s.schmutzig) zurueckschreiben(zyl_, kopf, s);
}

bool Platte::schmutzig() const
{
    for (const auto& [kopf, s] : cache_)
        if (s.schmutzig) return true;
    return false;
}

bool Platte::autoFlush(uint64_t t)
{
    if (!schmutzig() || t < letzt_schreiben_ + cfg_.flush_pause) return false;
    flush();
    return true;
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
