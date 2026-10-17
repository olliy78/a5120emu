/**
 * @file cqm_codec.h
 * @brief CqmCodec – CopyQM-Abbild (`.cqm`) ⇄ @ref DiskMedium (Lesen und Schreiben).
 *
 * CopyQM (Sydex/Central Point) legt ein DOS-Diskettenabbild als **Sektorstrom** ab:
 * 133-Byte-Kopf, Kommentar, danach die Nutzdaten aller Sektoren in der Reihenfolge
 * Zylinder → Kopf → Sektor (ID `secbase+1 …`), RLE-gepackt.  Wie bei `.img` fehlen Gaps,
 * Marken und CRCs; sie werden beim Laden ueber @ref TrackCodec::buildTrack aus der
 * Kopf-Geometrie synthetisiert (MFM — CopyQM kennt kein FM).
 *
 * Kopf (alle Zahlen little-endian; Befund an 142 echten Abbildern + libdsk `drvqm.c`):
 * @code
 *   00 'C' 'Q'          Kennung
 *   02 u8   Version (0x14)
 *   03 u16  Sektorgroesse          10 u16 Sektoren je Spur      12 u16 Seiten
 *   1C char[60] Bezeichnung ("720K Double-Sided"), NUL-terminiert
 *   58 u8   Blind-Flag (0 DOS, 1 blind, 2 HFS)      59 u8 Dichte (0 DD, 1 HD, 2 ED)
 *   5A u8   gespeicherte Zylinder   5B u8 Zylinder insgesamt
 *   5C u32  Pruefwert der entpackten Nutzdaten (0 = nicht gesetzt, alte Versionen)
 *   60 char[11] Etikett ("** NONE **")   6B u16 DOS-Zeit   6D u16 DOS-Datum
 *   6F u16  Kommentarlaenge         71 u8 erste Sektor-ID - 1
 *   74 u8   Interleave   75 u8 Skew   76 u8 Quellaufwerk
 *   84 u8   Kopfpruefbyte: Summe der 133 Kopfbytes == 0 (mod 256)
 *   85      Kommentar (Laenge von 6F), danach der Datenstrom
 * @endcode
 *
 * Datenstrom: Folge von Bloecken `i16 n`; `n < 0` → das EINE folgende Byte `-n`-mal,
 * `n > 0` → `n` Rohbytes, `n = 0` ohne Wirkung.  **Bloecke laufen ueber Sektor- und
 * Spurgrenzen hinweg** (kein Neubeginn je Spur).
 *
 * Pruefwert: CRC-32 (Polynom 0xEDB88320, reflektiert, Startwert 0, kein Endxor), aber mit
 * CopyQMs Tabellenfehler — der Tabellenindex ist `(byte ^ crc) & 0x3F` statt `& 0xFF`
 * (die obersten zwei Bits gehen in einem 8-Bit-Register verloren; libdsk `drvqm.c`).
 *
 * Interleave/Skew des Kopfs werden gelesen und gemeldet, aber **nicht** auf die Spur
 * angewandt: der Datenstrom liegt in logischer Sektorfolge (an den .tar-Gegenstuecken
 * von 68 Abbildern gegengeprueft), die Spur wird mit Interleave 1 gebaut — wie ein
 * `.img` gleicher Geometrie.
 *
 * Schreiben (@ref save, @ref encode): nur fuer Medien, die sich als Sektorstrom darstellen
 * lassen — @ref DiskMedium::rawCompatible UND einheitliche MFM-Geometrie (je Spur gleich
 * viele gleich grosse Sektoren, IDs lueckenlos `base…base+n-1`, beide Seiten bzw. nur
 * Seite 0, nur am Ende unformatierte Zylinder).  Alles andere wird mit Begruendung
 * abgelehnt, ohne die Zieldatei anzufassen.  Geschrieben wird in eine Nachbardatei und
 * dann umbenannt (nie ein Teilzustand).  Ist eine @ref SourceInfo der Quelle da, bleiben
 * Kommentar, Bezeichnung, Etikett, Zeit/Datum, Interleave/Skew und alle uebrigen
 * Kopfbytes erhalten, solange die Geometrie unveraendert ist.  Der RLE-Schnitt ist NICHT
 * der des Originals (Datei1 → Medium → Datei2 ist nicht byteidentisch, wohl aber
 * inhaltsgleich).
 *
 * @see doc/design/33_cqm.md, doc/merkposten/cqm.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_medium.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @namespace CqmCodec
 * @brief Laden von CopyQM-Abbildern.
 */
namespace CqmCodec {

/// @brief Laenge des festen Kopfes.
inline constexpr size_t kHeaderSize = 133;

/**
 * @struct SourceInfo
 * @brief Metadaten der geladenen Datei (Kopf + Befund des Ladens).
 */
struct SourceInfo {
    uint16_t sector_size    = 0;
    uint16_t secs_per_track = 0;
    uint16_t heads          = 0;
    uint8_t  used_cyls      = 0;    ///< im Datenstrom gespeicherte Zylinder
    uint8_t  total_cyls     = 0;    ///< Zylinder des Datentraegers insgesamt
    uint8_t  blind          = 0;    ///< 0 DOS, 1 blind, 2 HFS
    uint8_t  density        = 0;    ///< 0 DD, 1 HD, 2 ED
    uint8_t  first_sector_id = 1;   ///< Kopfbyte 0x71 + 1
    uint8_t  interleave     = 0;    ///< nur gemeldet
    uint8_t  skew           = 0;    ///< nur gemeldet
    uint16_t dos_time       = 0;    ///< Zeitstempel des Abbilds (DOS-Kodierung)
    uint16_t dos_date       = 0;
    std::string description;        ///< Bezeichnung ("720K Double-Sided")
    std::string label;              ///< Etikett; leer, wenn "** NONE **"
    std::string comment;            ///< Kommentartext des Abbilds (leer = keiner)
    std::vector<uint8_t> raw_header;   ///< die 133 Kopfbytes wie gelesen (Vorlage fuer @ref save)
    std::vector<uint8_t> comment_raw;  ///< Kommentarbytes wie gelesen (ohne Kuerzung)
    uint32_t crc_stored     = 0;
    uint32_t crc_computed   = 0;
    bool     crc_checked    = false;  ///< false: Datei traegt keinen Pruefwert (0)
    size_t   trailing_bytes = 0;      ///< Bytes hinter dem Datenstrom (toleriert)
};

/// @brief CopyQM-Pruefwert (CRC-32 mit `& 0x3F`-Tabellenfehler) ueber @p n Bytes.
uint32_t crc(const uint8_t* data, size_t n);

/**
 * @brief Sieht @p path wie ein CopyQM-Abbild aus? (Kennung `CQ` + Kopfpruefbyte)
 */
bool isCqm(const std::string& path);

/// @brief Wie @ref isCqm, fuer einen Speicherpuffer (die ersten @ref kHeaderSize Bytes genuegen).
bool isCqm(const uint8_t* data, size_t n);

/// @brief `.cqm` vollstaendig in @p out laden.
bool load(const std::string& path, DiskMedium& out, SourceInfo* info, std::string& err);

/// @brief Wie @ref load, aus einem Speicherpuffer (Tests, Archivinhalte).
bool loadBuffer(const std::vector<uint8_t>& datei, DiskMedium& out, SourceInfo* info,
                std::string& err);

/**
 * @brief Entpackte Nutzdaten (Zylinder → Kopf → Sektor) statt eines Mediums.
 * @param raw  Ausgabe: `used_cyls * heads * secs * sector_size` Bytes
 */
bool decodeBuffer(const std::vector<uint8_t>& datei, std::vector<uint8_t>& raw,
                  SourceInfo* info, std::string& err);

/// @brief Ein Feld der Metadaten als Text (description, label, comment, interleave, skew,
///        cylinders, crc); unbekannter Schluessel = "".  Gemeinsam fuer DiskTool und Emulator.
std::string infoField(const SourceInfo& si, const std::string& key);

/**
 * @brief Medium als CopyQM-Datei im Speicher aufbauen (nichts wird geschrieben).
 * @param vorlage     Kopf der Quelldatei (Kommentar & Co. bleiben erhalten); darf nullptr sein
 * @param out         Ausgabe: die vollstaendige Datei
 * @param geschrieben Ausgabe (optional): Metadaten der erzeugten Datei, wie sie ein Leser sieht
 * @return false mit Begruendung in @p err, wenn das Medium nicht darstellbar ist
 */
bool encode(const DiskMedium& in, const SourceInfo* vorlage, std::vector<uint8_t>& out,
            SourceInfo* geschrieben, std::string& err);

/// @brief RLE-Packer des Datenstroms (Wiederholung negativ, Literal positiv, Bloecke
///        hoechstens 32767 Byte, ueber Sektor-/Spurgrenzen hinweg).
std::vector<uint8_t> packRle(const std::vector<uint8_t>& raw);

/// @brief Medium als @p path schreiben (erst Nachbardatei, dann umbenennen); siehe @ref encode.
bool save(const std::string& path, const DiskMedium& in, const SourceInfo* vorlage,
          SourceInfo* geschrieben, std::string& err);

/// @brief Wie oben ohne Vorlage (Standardkopf).
bool save(const std::string& path, const DiskMedium& in, std::string& err);

}  // namespace CqmCodec
