/**
 * @file gzip_datei.h
 * @brief Abbilddateien roh oder gzip-gepackt laden und atomar speichern (miniz, third_party/miniz).
 *
 * Ein gepacktes Abbild ist eine gewöhnliche `.gz`-Datei (RFC 1952): `gunzip`, 7-Zip usw. lesen sie,
 * und was diese Werkzeuge schreiben, liest `laden()` — auch mehrteilige Dateien (`cat a.gz b.gz`)
 * und Köpfe mit Namen/Kommentar/Extrafeld.  Erkannt wird an den **Magic Bytes** `1F 8B`, nicht an
 * der Endung: ein umbenanntes Abbild bleibt lesbar, und `speichern()` schreibt in der Art zurück,
 * in der geladen wurde.
 *
 * Mitten in eine gepackte Datei kann man nicht schreiben.  Wer ein gepacktes Abbild ändert, hält
 * es deshalb ganz im Speicher und schreibt es ganz neu — **atomar**: erst `<pfad>.tmp`, dann
 * `rename` über das Original.  Ein Absturz beim Schreiben lässt das Original heil.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace k1520::gzip {

enum class Art { Roh, Gzip };

/// Packstufe beim Schreiben: 1 = schnell.  Mehr bringt bei Plattenabbildern wenig (leere Bereiche
/// schrumpfen ohnehin auf fast nichts) und kostet auf einer vollen Platte Sekunden.
constexpr int STUFE_VORGABE = 1;

/// Beginnt @p d mit der gzip-Kennung `1F 8B`?
bool istGzip(const uint8_t* d, size_t n);
/// Art einer vorhandenen Datei (liest 2 Byte); nullopt, wenn sie nicht lesbar ist.
std::optional<Art> artVon(const std::string& pfad);
/// Art, die eine NEUE Datei an @p pfad bekommt: `.gz` am Ende → Gzip, sonst Roh.
Art artNachEndung(const std::string& pfad);

/// Rohes Deflate in eine gzip-Hülle (ein Teil, ohne Namen).
bool packen(const uint8_t* d, size_t n, std::vector<uint8_t>& aus, int stufe = STUFE_VORGABE);
/// Alle Teile einer gzip-Datei entpacken; prüft CRC-32 und Länge jedes Teils.
bool entpacken(const uint8_t* d, size_t n, std::vector<uint8_t>& aus, std::string* fehler = nullptr);

/// Ganze Datei laden, gzip entpackt.  @p art (optional) erhält die gefundene Art.
bool laden(const std::string& pfad, std::vector<uint8_t>& daten, Art* art = nullptr,
           std::string* fehler = nullptr);
/// Erste @p n Byte des INHALTS (bei gzip entpackt; liest dafür die ganze Datei).
bool ladenAnfang(const std::string& pfad, size_t n, std::vector<uint8_t>& daten, std::string* fehler = nullptr);
/// Inhaltsgröße: roh = Dateigröße, gzip = entpackte Größe (entpackt dafür die ganze Datei).
std::optional<uint64_t> inhaltsGroesse(const std::string& pfad);

/// Ganze Datei schreiben, atomar über `<pfad>.tmp` + `rename`.
bool speichern(const std::string& pfad, const uint8_t* d, size_t n, Art art,
               int stufe = STUFE_VORGABE, std::string* fehler = nullptr);

}  // namespace k1520::gzip
