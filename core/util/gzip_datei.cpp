/**
 * @file gzip_datei.cpp
 * @brief s. gzip_datei.h.  Die gzip-Hülle (RFC 1952) steht hier, miniz liefert rohes Deflate.
 */
#include "core/util/gzip_datei.h"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace k1520::gzip {

namespace {

constexpr uint8_t ID1 = 0x1F, ID2 = 0x8B, CM_DEFLATE = 8;
constexpr uint8_t FHCRC = 0x02, FEXTRA = 0x04, FNAME = 0x08, FCOMMENT = 0x10, FRESERVIERT = 0xE0;

uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void le32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void setze(std::string* fehler, const std::string& s) { if (fehler) *fehler = s; }

/// Länge des Kopfs ab @p d; 0 = kein gültiger gzip-Kopf.
size_t kopfLaenge(const uint8_t* d, size_t n)
{
    if (n < 10 || d[0] != ID1 || d[1] != ID2 || d[2] != CM_DEFLATE || (d[3] & FRESERVIERT)) return 0;
    const uint8_t flg = d[3];
    size_t p = 10;
    if (flg & FEXTRA) {
        if (p + 2 > n) return 0;
        p += 2 + (size_t(d[p]) | size_t(d[p + 1]) << 8);
    }
    for (uint8_t bit : {FNAME, FCOMMENT}) {
        if (!(flg & bit)) continue;
        while (p < n && d[p] != 0) ++p;   // nullterminierte Zeichenkette
        ++p;
    }
    if (flg & FHCRC) p += 2;
    return p <= n ? p : 0;
}

mz_bool deflatePut(const void* buf, int len, void* user)
{
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    const auto* b = static_cast<const uint8_t*>(buf);
    v->insert(v->end(), b, b + len);
    return MZ_TRUE;
}

}  // namespace

bool istGzip(const uint8_t* d, size_t n) { return n >= 2 && d[0] == ID1 && d[1] == ID2; }

std::optional<Art> artVon(const std::string& pfad)
{
    std::ifstream f(pfad, std::ios::binary);
    if (!f) return std::nullopt;
    uint8_t k[2] = {};
    f.read(reinterpret_cast<char*>(k), 2);
    return istGzip(k, size_t(f.gcount())) ? Art::Gzip : Art::Roh;
}

Art artNachEndung(const std::string& pfad)
{
    std::string n = std::filesystem::path(pfad).filename().string();
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n.size() > 3 && n.compare(n.size() - 3, 3, ".gz") == 0 ? Art::Gzip : Art::Roh;
}

bool packen(const uint8_t* d, size_t n, std::vector<uint8_t>& aus, int stufe)
{
    aus.clear();
    aus.reserve(n / 8 + 1024);
    // Kopf: keine Flags, keine Zeit (Abbilder sollen bei gleichem Inhalt gleich sein), OS = unbekannt
    aus.insert(aus.end(), {ID1, ID2, CM_DEFLATE, 0, 0, 0, 0, 0, static_cast<uint8_t>(stufe <= 1 ? 4 : 0), 0xFF});
    // negative Fensterbreite = rohes Deflate ohne zlib-Hülle
    const int flags = static_cast<int>(
        tdefl_create_comp_flags_from_zip_params(std::clamp(stufe, 1, 9), -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
    if (!tdefl_compress_mem_to_output(d, n, deflatePut, &aus, flags)) return false;
    le32(aus, static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, d, n)));
    le32(aus, static_cast<uint32_t>(n));   // ISIZE = Länge mod 2^32
    return true;
}

bool entpacken(const uint8_t* d, size_t n, std::vector<uint8_t>& aus, std::string* fehler)
{
    aus.clear();
    if (!istGzip(d, n)) { setze(fehler, "keine gzip-Datei"); return false; }
    // Vorab-Größe aus der Länge des letzten Teils — bei einer einteiligen Datei genau richtig
    if (n >= 18) aus.reserve(le32(d + n - 4));
    size_t pos = 0;
    while (pos < n) {
        const size_t kopf = kopfLaenge(d + pos, n - pos);
        if (kopf == 0) {
            // Nach dem ersten Teil: Nullbytes (Auffüllung von Bandarchiven) sind erlaubt, sonst Müll
            if (pos > 0 && std::all_of(d + pos, d + n, [](uint8_t b) { return b == 0; })) break;
            setze(fehler, pos == 0 ? "gzip-Kopf ungültig" : "unerwartete Daten hinter dem gzip-Ende");
            return false;
        }
        pos += kopf;
        const size_t anfang = aus.size();
        size_t laenge = anfang;
        if (aus.size() < anfang + (1u << 16)) aus.resize(std::max(aus.capacity(), anfang + (size_t(1) << 16)));
        tinfl_decompressor dek;
        tinfl_init(&dek);
        for (;;) {
            size_t ein = n - pos, raus = aus.size() - laenge;
            const tinfl_status st = tinfl_decompress(&dek, d + pos, &ein, aus.data(), aus.data() + laenge, &raus,
                                                     TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            pos += ein;
            laenge += raus;
            if (st == TINFL_STATUS_DONE) break;
            if (st == TINFL_STATUS_HAS_MORE_OUTPUT) { aus.resize(aus.size() * 2); continue; }
            aus.clear();
            setze(fehler, st == TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS || st == TINFL_STATUS_NEEDS_MORE_INPUT
                              ? "gzip-Datei abgeschnitten" : "gzip-Daten beschädigt");
            return false;
        }
        aus.resize(laenge);
        if (pos + 8 > n) { aus.clear(); setze(fehler, "gzip-Datei abgeschnitten (Prüfsumme fehlt)"); return false; }
        const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, aus.data() + anfang, laenge - anfang));
        if (crc != le32(d + pos) || static_cast<uint32_t>(laenge - anfang) != le32(d + pos + 4)) {
            aus.clear();
            setze(fehler, "gzip-Prüfsumme falsch — Datei beschädigt");
            return false;
        }
        pos += 8;
    }
    return true;
}

bool laden(const std::string& pfad, std::vector<uint8_t>& daten, Art* art, std::string* fehler)
{
    std::ifstream f(pfad, std::ios::binary);
    if (!f) { setze(fehler, "nicht lesbar: " + pfad); return false; }
    std::vector<uint8_t> roh((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad()) { setze(fehler, "Lesefehler: " + pfad); return false; }
    if (!istGzip(roh.data(), roh.size())) {
        if (art) *art = Art::Roh;
        daten = std::move(roh);
        return true;
    }
    if (art) *art = Art::Gzip;
    std::string f2;
    if (!entpacken(roh.data(), roh.size(), daten, &f2)) { setze(fehler, f2 + ": " + pfad); return false; }
    return true;
}

bool ladenAnfang(const std::string& pfad, size_t n, std::vector<uint8_t>& daten, std::string* fehler)
{
    const auto art = artVon(pfad);
    if (!art) { setze(fehler, "nicht lesbar: " + pfad); return false; }
    if (*art == Art::Gzip) {
        if (!laden(pfad, daten, nullptr, fehler)) return false;
        if (daten.size() > n) daten.resize(n);
        return true;
    }
    std::ifstream f(pfad, std::ios::binary);
    daten.assign(n, 0);
    f.read(reinterpret_cast<char*>(daten.data()), static_cast<std::streamsize>(n));
    daten.resize(size_t(f.gcount()));
    return true;
}

std::optional<uint64_t> inhaltsGroesse(const std::string& pfad)
{
    const auto art = artVon(pfad);
    if (!art) return std::nullopt;
    if (*art == Art::Roh) {
        std::error_code ec;
        const auto g = std::filesystem::file_size(pfad, ec);
        return ec ? std::nullopt : std::optional<uint64_t>(g);
    }
    std::vector<uint8_t> d;
    if (!laden(pfad, d)) return std::nullopt;
    return d.size();
}

bool speichern(const std::string& pfad, const uint8_t* d, size_t n, Art art, int stufe, std::string* fehler)
{
    std::vector<uint8_t> gepackt;
    if (art == Art::Gzip) {
        if (!packen(d, n, gepackt, stufe)) { setze(fehler, "Packen gescheitert: " + pfad); return false; }
        d = gepackt.data();
        n = gepackt.size();
    }
    const std::string tmp = pfad + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { setze(fehler, "nicht anlegbar: " + tmp); return false; }
        f.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
        f.close();
        if (!f) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            setze(fehler, "nicht vollständig geschrieben: " + tmp);
            return false;
        }
    }
    // Ersetzt das Original atomar.  Unter Windows scheitert das, solange ein anderer die Datei offen
    // hält (Virenscanner, Dateibetrachter, ein ifstream im Test) — erst kurz wiederholen, dann als
    // letzter Ausweg direkt überschreiben (nicht atomar, aber so, wie es vor gzip immer war).
    std::error_code ec;
    for (int versuch = 0; versuch < 5; ++versuch) {
        std::filesystem::rename(tmp, pfad, ec);
        if (!ec) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    const std::string grund = ec.message();
    {
        std::ofstream f(pfad, std::ios::binary | std::ios::trunc);
        if (f) f.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
        if (f) f.close();
        std::filesystem::remove(tmp, ec);
        if (f) return true;
    }
    setze(fehler, "nicht ersetzbar: " + pfad + " (" + grund + ")");
    return false;
}

}  // namespace k1520::gzip
