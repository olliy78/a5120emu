/**
 * @file kassetten_medium.cpp
 * @brief Kassettenband: Blockfolge je Seite in Bandlaengen, siehe kassetten_medium.h.
 */

#include "core/peripherals/kassette/kassetten_medium.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kassette {

// ─── Hilfsfunktionen fuer Bloecke ──────────────────────────────────────────────

uint16_t crc16_arc(const uint8_t* daten, size_t n, uint16_t start) {
    uint16_t crc = start;
    for (size_t i = 0; i < n; ++i) {
        crc ^= daten[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
    }
    return crc;
}

std::vector<uint8_t> blockBauen(const std::vector<uint8_t>& daten, uint8_t praeambel, uint8_t postambel) {
    std::vector<uint8_t> b;
    b.reserve(daten.size() + 4);
    b.push_back(praeambel);
    b.insert(b.end(), daten.begin(), daten.end());
    const uint16_t crc = crc16_arc(daten);
    b.push_back(static_cast<uint8_t>(crc & 0xFF));  // niedriges Byte zuerst (treiber.md §2.2)
    b.push_back(static_cast<uint8_t>(crc >> 8));
    b.push_back(postambel);
    return b;
}

std::vector<uint8_t> bandmarkeBytes() { return blockBauen({}); }

bool istBandmarke(const std::vector<uint8_t>& bytes) {
    return bytes.size() == 4 && bytes[1] == 0x00 && bytes[2] == 0x00;
}

bool crcStimmt(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return false;
    const size_t n = bytes.size() - 3;  // Daten = bytes[1 .. size-3)
    const uint16_t crc = crc16_arc(bytes.data() + 1, n - 1);
    return bytes[bytes.size() - 3] == (crc & 0xFF) && bytes[bytes.size() - 2] == (crc >> 8);
}

// ─── Masse ────────────────────────────────────────────────────────────────────

KassettenMedium::KassettenMedium(Geometrie g, uint32_t dichte_milli, uint32_t aufnahme_mm_s)
    : geo_(g), dichte_milli_(dichte_milli ? dichte_milli : kDichteVorgabe), aufnahme_mm_s_(aufnahme_mm_s) {}

int64_t KassettenMedium::laengeVonBytes(size_t n) const {
    // n Bytes · 8 Bit / (Dichte/1000 Bit/mm) mm = n · 8e6 / Dichte_milli µm
    return static_cast<int64_t>(std::llround(static_cast<double>(n) * 8.0e6 / dichte_milli_));
}

int64_t KassettenMedium::botAnfang() const { return int64_t(geo_.vorspann_um) + geo_.marken_abstand_um; }
int64_t KassettenMedium::botEnde() const { return botAnfang() + geo_.marke_um; }
int64_t KassettenMedium::eotEnde() const { return magnetbandEnde() - geo_.marken_abstand_um; }
int64_t KassettenMedium::eotAnfang() const { return eotEnde() - geo_.marke_um; }
int64_t KassettenMedium::magnetbandEnde() const { return int64_t(geo_.laenge_um) - geo_.vorspann_um; }

bool KassettenMedium::aebAktiv(int64_t s) const {
    if (s < geo_.vorspann_um || s >= magnetbandEnde()) return true;
    return (s >= botAnfang() && s < botEnde()) || (s >= eotAnfang() && s < eotEnde());
}

// ─── Seite, Position ─────────────────────────────────────────────────────────

void KassettenMedium::wenden() {
    seite_ = andere(seite_);
    pos_um_ = int64_t(geo_.laenge_um) - pos_um_;
}

void KassettenMedium::setPosition(int64_t um) { pos_um_ = std::clamp<int64_t>(um, 0, geo_.laenge_um); }

// ─── Blockfolge ──────────────────────────────────────────────────────────────

const std::vector<int64_t>& KassettenMedium::starts(Seite s) const {
    const SeitenInhalt& si = seiten_[idx(s)];
    if (!si.cache_gueltig) {
        si.starts.clear();
        si.starts.reserve(si.bloecke.size());
        int64_t ende = botEnde();
        for (const Block& b : si.bloecke) {
            const int64_t st = ende + b.luecke_vor_um;
            si.starts.push_back(st);
            ende = st + laengeVonBytes(b.bytes.size());
        }
        si.cache_gueltig = true;
    }
    return si.starts;
}

void KassettenMedium::aenderung(Seite s) {
    seiten_[idx(s)].cache_gueltig = false;
    ++revision_;
}

int64_t KassettenMedium::blockStart(Seite s, size_t i) const { return starts(s).at(i); }
int64_t KassettenMedium::blockEnde(Seite s, size_t i) const {
    return starts(s).at(i) + laengeVonBytes(seiten_[idx(s)].bloecke[i].bytes.size());
}

int KassettenMedium::blockBei(Seite s, int64_t pos) const {
    const auto& st = starts(s);
    auto it = std::upper_bound(st.begin(), st.end(), pos);
    if (it == st.begin()) return -1;
    const size_t i = static_cast<size_t>(it - st.begin()) - 1;
    return pos < blockEnde(s, i) ? static_cast<int>(i) : -1;
}

size_t KassettenMedium::ersterBlockAb(Seite s, int64_t pos) const {
    const auto& st = starts(s);
    return static_cast<size_t>(std::lower_bound(st.begin(), st.end(), pos) - st.begin());
}

int KassettenMedium::letzterBlockBis(Seite s, int64_t pos) const {
    // Enden wachsen mit dem Index: binaere Suche ueber blockEnde().
    int lo = 0, hi = static_cast<int>(anzahlBloecke(s)) - 1, erg = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (blockEnde(s, static_cast<size_t>(mid)) <= pos) {
            erg = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return erg;
}

int64_t KassettenMedium::datenEnde(Seite s) const {
    const size_t n = anzahlBloecke(s);
    return n ? blockEnde(s, n - 1) : botEnde();
}

namespace {
struct Platziert {
    int64_t start;
    std::vector<uint8_t> bytes;
};
}  // namespace

bool KassettenMedium::schreibeBlock(Seite s, int64_t start, const std::vector<uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > kMaxBlockBytes) return false;
    const int64_t ende = start + laengeVonBytes(bytes.size());
    if (start < botEnde() || ende > magnetbandEnde()) return false;

    SeitenInhalt& si = seiten_[idx(s)];
    std::vector<Platziert> neu;
    neu.reserve(si.bloecke.size() + 1);
    for (size_t i = 0; i < si.bloecke.size(); ++i) {
        const int64_t bs = blockStart(s, i), be = blockEnde(s, i);
        if (bs < ende && be > start) continue;  // ueberschrieben: zerstoert
        neu.push_back({bs, si.bloecke[i].bytes});
    }
    neu.push_back({start, bytes});
    std::sort(neu.begin(), neu.end(), [](const Platziert& a, const Platziert& b) { return a.start < b.start; });

    std::vector<Block> folge;
    folge.reserve(neu.size());
    int64_t vorher = botEnde();
    for (Platziert& p : neu) {
        folge.push_back({static_cast<uint32_t>(p.start - vorher), std::move(p.bytes)});
        vorher = p.start + laengeVonBytes(folge.back().bytes.size());
    }
    si.bloecke = std::move(folge);
    aenderung(s);
    return true;
}

bool KassettenMedium::haengeAn(Seite s, uint32_t luecke, const std::vector<uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > kMaxBlockBytes) return false;
    const int64_t start = datenEnde(s) + luecke;
    if (start + laengeVonBytes(bytes.size()) > magnetbandEnde()) return false;
    seiten_[idx(s)].bloecke.push_back({luecke, bytes});
    aenderung(s);
    return true;
}

size_t KassettenMedium::loesche(Seite s, int64_t von, int64_t bis) {
    if (bis <= von) return 0;
    SeitenInhalt& si = seiten_[idx(s)];
    std::vector<Platziert> neu;
    size_t weg = 0;
    for (size_t i = 0; i < si.bloecke.size(); ++i) {
        const int64_t bs = blockStart(s, i), be = blockEnde(s, i);
        if (bs < bis && be > von) {
            ++weg;
            continue;
        }
        neu.push_back({bs, si.bloecke[i].bytes});
    }
    if (!weg) return 0;
    std::vector<Block> folge;
    folge.reserve(neu.size());
    int64_t vorher = botEnde();
    for (Platziert& p : neu) {
        folge.push_back({static_cast<uint32_t>(p.start - vorher), std::move(p.bytes)});
        vorher = p.start + laengeVonBytes(folge.back().bytes.size());
    }
    si.bloecke = std::move(folge);
    aenderung(s);
    return weg;
}

void KassettenMedium::leereSeite(Seite s) {
    if (seiten_[idx(s)].bloecke.empty()) return;
    seiten_[idx(s)].bloecke.clear();
    aenderung(s);
}

void KassettenMedium::setSchreibschutz(Seite s, bool an) {
    if (seiten_[idx(s)].schreibschutz == an) return;
    seiten_[idx(s)].schreibschutz = an;
    ++revision_;
}

bool KassettenMedium::gleichInhalt(const KassettenMedium& o) const {
    if (!(geo_ == o.geo_) || dichte_milli_ != o.dichte_milli_ || aufnahme_mm_s_ != o.aufnahme_mm_s_) return false;
    for (int i = 0; i < 2; ++i)
        if (seiten_[i].schreibschutz != o.seiten_[i].schreibschutz || seiten_[i].bloecke != o.seiten_[i].bloecke) return false;
    return true;
}

}  // namespace kassette
