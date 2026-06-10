/**
 * @file bit_codec.h
 * @brief BitCodec – Bitzellen ⇆ Bytes je Aufzeichnungsverfahren (MFM und FM) für HFE.
 *
 * Nur das HFE-Backend braucht die Bitebene; sie ist hier gekapselt, damit der Rest des
 * Floppy-Stacks byteorientiert bleibt (@ref TrackImage).  @ref decode wandelt den rohen
 * Bitzellen-Strom einer Spurseite (wie in HFE gespeichert) in eine decodierte
 * @ref TrackImage (Datenbytes + Markenflags); @ref encode den umgekehrten Weg.
 *
 * **Bit-Reihenfolge (HFE-Konvention, Greaseweazle-treu):** In der HFE-Datei wird jedes
 * Byte **LSB zuerst** als Zellfolge ausgegeben.  Intern arbeitet der Codec MSB-first
 * (Sync-Suche auf das Zellwort `0x4489`); jedes Datei-Byte wird daher an der Byte-Grenze
 * **bit-gespiegelt** (bytereverse) — beim Lesen wie beim Schreiben.
 *
 * **Zellmodell:** Ein Datenbyte belegt 16 Zellen (`c0 d0 c1 d1 … c7 d7`, MSB-first).
 *   MFM: Daten-Zelle = `d_i`, Clock-Zelle `c_i = NOT(d_{i-1} OR d_i)`; A1-Sync = Zellwort
 *        `0x4489` (→ Datenbyte 0xA1, fehlendes Clock-Bit), C2-Sync (IAM) = `0x5224`.
 *   FM : Clock-Zelle immer 1; Marken-Bytes mit Sondertakt (FE/FB/F8 Clock C7, FC Clock D7).
 *
 * @see doc/refactoring_floppy_emulator.md §5
 * @see https://github.com/keirf/greaseweazle  (image/hfe.py, codec/ibm/ibm.py)
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "track_image.h"        // Encoding, TrackImage, MarkType
#include <cstdint>
#include <vector>

/**
 * @namespace BitCodec
 * @brief MFM-/FM-Bitzellen-Decodierung/-Encodierung für eine Spurseite.
 */
namespace BitCodec {

/**
 * @brief Decodiert einen Bitzellen-Strom (eine Spurseite) in eine @ref TrackImage.
 *
 * @p cells sind die Spurseiten-Bytes, **wie in HFE gespeichert** (LSB-first je Byte).
 * Es werden die ersten @p bitcell_count Zellen ausgewertet.  Das Ergebnis trägt @p enc
 * als @ref TrackImage::encoding und setzt @ref TrackImage::marks auf den Mark-Bytes
 * (FE → Id, FB/F8 → Data, FC → Index); die 0xA1/0xC2-Sync-Bytes erscheinen als reguläre
 * Bytes mit Marke None — analog zur Ausgabe von @ref TrackCodec::buildTrack, damit
 * @ref TrackCodec::parseTrack auf beiden Quellen identisch arbeitet.
 *
 * @param cells          Spurseiten-Bytes (LSB-first Zellstrom)
 * @param bitcell_count  Zahl gültiger Bitzellen (≤ cells.size()*8)
 * @param enc            erwartetes Verfahren (MFM/FM)
 * @return decodierte Spur (mit encoding == @p enc und bitcells == @p bitcell_count)
 */
TrackImage decode(const std::vector<uint8_t>& cells, uint32_t bitcell_count, Encoding enc);

/**
 * @brief Encodiert eine @ref TrackImage zurück in einen Bitzellen-Strom (eine Spurseite).
 *
 * Verfahren aus @p track.encoding.  Marken (`marks[i] != None`) werden mit dem
 * verfahrenstypischen Sonder-Clock kodiert (MFM: A1 → `0x4489`, C2 → `0x5224`;
 * FM: Sondertakt C7/D7), übrige Bytes regulär.  Das Ergebnis ist LSB-first (HFE-fähig)
 * und wird auf @p target_bitcells (auf volle Bytes aufgerundet) mit Gap aufgefüllt/getrimmt,
 * damit die ursprüngliche Spurlänge (HFE `track_len`) erhalten bleibt.
 *
 * @param track           zu kodierende Spur (mit marks[] und encoding)
 * @param target_bitcells Ziel-Bitzellenzahl (typ. @ref TrackImage::bitcells der Quelle)
 * @return Spurseiten-Bytes (LSB-first), Länge = ceil(target_bitcells/8)
 */
std::vector<uint8_t> encode(const TrackImage& track, uint32_t target_bitcells);

}  // namespace BitCodec
