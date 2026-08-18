/**
 * @file udos_bitmap.cpp
 * @brief Umsetzung von @ref UdosBitmap.
 *
 * @see doc/udos_diskettenformat.md §4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#include "core/filesystem/udos/udos_bitmap.h"

#include <algorithm>

namespace {

/// @brief Bitposition eines Sektors im 4-B-Spureintrag: MSB zuerst, ID 1 = Bit 31.
bool bitOf(const std::vector<uint8_t>& raw, uint8_t track, uint8_t sector_id) {
    const size_t base = kUdosBitmapFirstTrack + static_cast<size_t>(track) * 4;
    if (base + 3 >= raw.size() || sector_id == 0) return true;   // ausserhalb = „belegt"
    const int idx = sector_id - 1;
    if (idx >= 32) return true;
    return (raw[base + idx / 8] >> (7 - (idx % 8))) & 1;
}

}  // namespace

// ─── Laden / Speichern ───────────────────────────────────────────────────────

namespace {

/// @brief Sektorgroesse, in der die Karte abgelegt ist (ZDOS 128, NDOS 256).
size_t mapSectorSize(UdosMapSitte s) { return s == UdosMapSitte::Ndos1715 ? 256 : 128; }
/// @brief Wie viele Sektoren die 384 Byte belegen (3 bzw. 2 — der letzte nur halb).
uint8_t mapSectorCount(UdosMapSitte s) { return s == UdosMapSitte::Ndos1715 ? 2 : 3; }

}  // namespace

bool UdosBitmap::load(const SectorSpace& space, uint8_t head, uint8_t bitmap_track,
                      UdosBitmap& out, std::string& err, UdosMapSitte sitte) {
    out.raw_.assign(kUdosBitmapBytes, 0);
    out.sitte_ = sitte;
    const size_t  ss = mapSectorSize(sitte);
    const uint8_t n  = mapSectorCount(sitte);
    for (uint8_t s = 1; s <= n; ++s) {
        SectorData sec;
        if (!space.readSector(bitmap_track, head, s, sec)) {
            err = "Belegungskarte: " + space.lastError();
            return false;
        }
        if (sec.data.size() < ss) {
            err = "Belegungskarte: Sektor " + std::to_string(s) + " auf Spur "
                + std::to_string(bitmap_track) + " ist kuerzer als "
                + std::to_string(ss) + " B";
            return false;
        }
        // Der letzte Sektor traegt bei NDOS nur noch 128 der 256 Byte zur Karte bei.
        const size_t ab    = (s - 1) * ss;
        const size_t menge = std::min(ss, kUdosBitmapBytes - ab);
        std::copy(sec.data.begin(), sec.data.begin() + static_cast<long>(menge),
                  out.raw_.begin() + static_cast<long>(ab));
    }
    return true;
}

bool UdosBitmap::store(SectorSpace& space, uint8_t head, uint8_t bitmap_track,
                       std::string& err) const {
    const size_t  ss = mapSectorSize(sitte_);
    const uint8_t n  = mapSectorCount(sitte_);
    for (uint8_t s = 1; s <= n; ++s) {
        const size_t ab    = (s - 1) * ss;
        const size_t menge = std::min(ss, kUdosBitmapBytes - ab);
        // Der Rest des letzten Sektors ist Altbestand und geht uns nichts an — deshalb
        // Lesen-Aendern-Schreiben statt eines Sektors voller Nullen.
        std::vector<uint8_t> teil;
        if (menge < ss) {
            SectorData sec;
            if (!space.readSector(bitmap_track, head, s, sec) || sec.data.size() < ss) {
                err = "Belegungskarte schreiben: " + space.lastError();
                return false;
            }
            teil = sec.data;
        } else {
            teil.assign(ss, 0);
        }
        std::copy(raw_.begin() + static_cast<long>(ab),
                  raw_.begin() + static_cast<long>(ab + menge), teil.begin());
        if (!space.writeSector(bitmap_track, head, s, teil)) {
            err = "Belegungskarte schreiben: " + space.lastError();
            return false;
        }
    }
    return true;
}

// ─── Felder ──────────────────────────────────────────────────────────────────

std::string UdosBitmap::label() const {
    std::string s(raw_.begin(), raw_.begin() + 24);
    // UDOS fuellt mit 0x0D auf; Leerzeichen am Rand sind ebenfalls Fuellung.
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\0'))
        s.pop_back();
    return s;
}

void UdosBitmap::setLabel(const std::string& name) {
    for (size_t i = 0; i < 24; ++i)
        raw_[i] = i < name.size() ? static_cast<uint8_t>(name[i]) : 0x0D;
}

uint16_t UdosBitmap::storedFree() const {
    return static_cast<uint16_t>(raw_[380] | (raw_[381] << 8));
}

uint16_t UdosBitmap::storedUsed() const {
    return static_cast<uint16_t>(raw_[375] | (raw_[376] << 8));
}

bool UdosBitmap::used(uint8_t track, uint8_t sector_id) const {
    return bitOf(raw_, track, sector_id);
}

void UdosBitmap::setUsed(uint8_t track, uint8_t sector_id, bool belegt) {
    const size_t base = kUdosBitmapFirstTrack + static_cast<size_t>(track) * 4;
    if (base + 3 >= raw_.size() || sector_id == 0 || sector_id > 32) return;
    const int idx = sector_id - 1;
    const uint8_t maske = static_cast<uint8_t>(1u << (7 - (idx % 8)));
    if (belegt) raw_[base + idx / 8] |=  maske;
    else        raw_[base + idx / 8] &= static_cast<uint8_t>(~maske);
}

int UdosBitmap::countFree() const {
    int frei = 0;
    const uint8_t n = sectorsPerTrack();
    for (uint8_t t = 0; t < trackCount(); ++t)
        for (uint8_t s = 1; s <= n; ++s)
            if (!used(t, s)) ++frei;
    return frei;
}

int UdosBitmap::countUsed() const {
    return static_cast<int>(trackCount()) * sectorsPerTrack() - countFree();
}

void UdosBitmap::refreshCounters() {
    const uint16_t frei = static_cast<uint16_t>(countFree());
    raw_[380] = static_cast<uint8_t>(frei & 0xFF);
    raw_[381] = static_cast<uint8_t>(frei >> 8);
    // Der „belegt"-Zaehler ist bei ZDOS 2464 − frei (Festwert aus FORMATPC.MAC) und
    // hat mit der wirklichen Kapazitaet nichts zu tun — wir fuehren ihn genauso nach,
    // damit das laufende UDOS nichts Ungewohntes vorfindet.  NDOS traegt dort die
    // WIRKLICHE Zahl ein (nachgemessen: 1673 + 887 = 80·32).
    const uint16_t belegt = sitte_ == UdosMapSitte::Ndos1715
                          ? static_cast<uint16_t>(countUsed())
                          : static_cast<uint16_t>(kUdosCounterConstant - frei);
    raw_[375] = static_cast<uint8_t>(belegt & 0xFF);
    raw_[376] = static_cast<uint8_t>(belegt >> 8);
}

// ─── Plausibilitaet (Positivprobe der Erkennung) ─────────────────────────────

bool UdosBitmap::looksValid(const std::vector<uint8_t>& raw, uint8_t expect_secs,
                            uint8_t expect_tracks, std::string* why, UdosMapSitte sitte) {
    auto sag = [&](const std::string& m) { if (why) *why = m; return false; };
    if (raw.size() < kUdosBitmapBytes) return sag("Karte ist zu kurz");

    // Sektoren/Spur und Spurzahl muessen zur gemessenen Geometrie passen.
    if (raw[378] != expect_secs)
        return sag("Karte nennt " + std::to_string(raw[378]) + " Sektoren je Spur, gemessen "
                   + std::to_string(expect_secs));
    if (expect_tracks && raw[379] > expect_tracks)
        return sag("Karte nennt " + std::to_string(raw[379]) + " Spuren, das Medium hat "
                   + std::to_string(expect_tracks));
    if (raw[379] == 0) return sag("Karte nennt 0 Spuren");

    if (sitte == UdosMapSitte::Ndos1715) {
        // NDOS fuehrt 80 Spureintraege (bis 343); dahinter bleibt bis zu den Zaehlern
        // Platz, den die Formatierer verschieden fuellen — der PC 1715 mit 00, der
        // **P8000** mit dem 0x77-Rest der ZDOS-Sitte (dessen 0x33/0xF7 liegen bei ZDOS
        // vor 344 und damit hier mitten im Belegungsplan; sie bleiben das
        // Unterscheidungsmerkmal, s. u.).  Geprueft wird deshalb nur, dass dort NICHTS
        // ANDERES steht — die Trennung zu ZDOS traegt der Zaehlerabgleich.
        for (size_t i = 344; i < 375; ++i)
            if (raw[i] != 0x00 && raw[i] != 0x77)
                return sag("Bereich hinter dem Belegungsplan ist weder 0 noch Nachlauf");
        // Belegt + frei muss die Kapazitaet ergeben — bei NDOS sind beide Zaehler echt.
        const uint32_t kapazitaet = static_cast<uint32_t>(raw[378]) * raw[379];
        const uint32_t belegt = static_cast<uint32_t>(raw[375] | (raw[376] << 8));
        const uint32_t frei   = static_cast<uint32_t>(raw[380] | (raw[381] << 8));
        if (belegt + frei != kapazitaet)
            return sag("Zaehler passen nicht zur Kapazitaet (" + std::to_string(belegt)
                       + " + " + std::to_string(frei) + " != " + std::to_string(kapazitaet)
                       + ")");
    } else {
        // Der konstante Nachlauf ist das trennschaerfste Merkmal: 11×0x33, 0xF7, 27×0x77.
        for (size_t i = 336; i < 347; ++i) if (raw[i] != 0x33) return sag("Nachlauf 0x33 fehlt");
        if (raw[347] != 0xF7)                                   return sag("Nachlauf 0xF7 fehlt");
        for (size_t i = 348; i < 375; ++i) if (raw[i] != 0x77) return sag("Nachlauf 0x77 fehlt");
    }

    // Datentraegername: druckbar oder 0x0D-Fuellung.
    for (size_t i = 0; i < 24; ++i) {
        const uint8_t c = raw[i];
        if (c == 0x0D || (c >= 0x20 && c < 0x7F)) continue;
        return sag("Datentraegername enthaelt ein Sonderzeichen");
    }
    return true;
}

UdosBitmap UdosBitmap::makeEmpty(uint8_t sectors_per_track, uint8_t tracks,
                                 const std::string& label, UdosMapSitte sitte) {
    UdosBitmap b;
    b.raw_.assign(kUdosBitmapBytes, 0);
    b.sitte_ = sitte;
    b.setLabel(label);

    // Alle Spuren frei; ueberzaehlige Bits sind bei UDOS immer gesetzt.
    for (uint8_t t = 0; t < udosMapTrackSlots(sitte); ++t) {
        const size_t base = kUdosBitmapFirstTrack + static_cast<size_t>(t) * 4;
        if (t < tracks) {
            b.raw_[base] = b.raw_[base + 1] = b.raw_[base + 2] = b.raw_[base + 3] = 0x00;
            // Bits fuer Sektoren oberhalb sectors_per_track sind belegt/gesperrt — bei
            // ZDOS (26) nur die letzten sechs, bei einer einseitigen NDOS-Diskette (16)
            // die ganze zweite Haelfte des Eintrags.
            for (int idx = sectors_per_track; idx < 32; ++idx)
                b.raw_[base + idx / 8] |= static_cast<uint8_t>(1u << (7 - (idx % 8)));
        } else {
            // Spuren jenseits der Kapazitaet traegt der Formatierer als gesperrt ein.
            b.raw_[base] = b.raw_[base + 1] = b.raw_[base + 2] = b.raw_[base + 3] = 0xFF;
        }
    }

    if (sitte == UdosMapSitte::Zdos) {
        for (size_t i = 336; i < 347; ++i) b.raw_[i] = 0x33;
        b.raw_[347] = 0xF7;
        for (size_t i = 348; i < 375; ++i) b.raw_[i] = 0x77;
    }   // NDOS laesst den Bereich hinter dem Belegungsplan auf 00.

    b.raw_[377] = 0x00;
    b.raw_[378] = sectors_per_track;
    b.raw_[379] = tracks;
    b.raw_[382] = b.raw_[383] = 0x00;
    b.refreshCounters();
    return b;
}
