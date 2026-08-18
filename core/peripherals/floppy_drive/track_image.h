/**
 * @file track_image.h
 * @brief TrackImage – zentrale formatagnostische Track-Bytestrom-Abstraktion.
 *
 * Eine @ref TrackImage ist der *decodierte* Spurinhalt einer (Zylinder, Kopf)-Spur:
 * genau die Bytefolge, die der Datenseparator hinter dem Lesekopf liefert — inklusive
 * Gaps, Sync-/Adressmarken, ID-Feldern, Datenfeldern und **echten CRCs**.  Sie ist die
 * einzige Repräsentation, die der neue Floppy-Controller (@ref K5122) sieht; er kennt
 * keine Sektorgrößen, Sektoranzahl, CRC-Verfahren oder Boot-Stadien mehr.
 *
 * Diese Schicht ersetzt die mit der Lese-Routine verflochtene On-the-fly-Synthese der
 * alten K5122 (siehe doc/design/07_k5122_afs.md).  Sie arbeitet auf
 * **decodierten Bytes**, weil der K5122 hinter dem Datenseparator ebenfalls byteweise
 * arbeitet (Ports 0x14/0x16).  Die Bitebene (MFM/FM) lebt ausschließlich in den
 * Bit-Codecs des HFE-Backends.
 *
 * @see doc/design/07_k5122_afs.md
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @enum Encoding
 * @brief Aufzeichnungsverfahren einer Spur. Bestimmt NUR die Codec-Schicht, nicht
 *        den Controller.  Der Controller (@ref K5122) ist verfahrensneutral.
 */
enum class Encoding : uint8_t {
    FM,   ///< Frequenzmodulation (Single Density, 8″-Laufwerke)
    MFM   ///< Modified FM (Double Density, 5,25″/8″)
};

/**
 * @enum MarkType
 * @brief Typ einer Adressmarke an einer Zellposition (aus dem fehlenden Clock-Bit
 *        abgeleitet).  Gilt für BEIDE Verfahren; nur das Sync-/Clock-Muster
 *        unterscheidet sich.
 *
 * @code
 *   MFM: Sync A1A1A1 + Mark-Byte (FE/FB/F8/FC) bzw. C2C2C2+FC für Index
 *   FM : Mark-Byte allein mit Sonder-Clock (FE/C7, FB/C7, F8/C7, FC/D7), KEIN A1-Sync
 * @endcode
 */
enum class MarkType : uint8_t {
    None = 0,   ///< normales Daten-/Gap-Byte
    Index,      ///< IAM  (MFM C2C2C2 FC / FM FC)
    Id,         ///< IDAM (MFM A1A1A1 FE / FM FE) — ID-Feld folgt
    Data        ///< DAM  (MFM A1A1A1 FB|F8 / FM FB|F8) — Datenfeld folgt (F8 = gelöscht)
};

/**
 * @struct TrackImage
 * @brief Eine decodierte Spur als Byte-Strom mit parallelen Markenflags.
 *
 * Die Bytes sind GENAU das, was der Lesekopf während einer Umdrehung sieht (inkl.
 * Gaps/Sync/CRC) — keine Synthese im Controller, keine Sonderfälle.  @ref marks ist
 * parallel zu @ref bytes und markiert das *Mark-Byte* einer Adressmarke (bei MFM das
 * Mark-Byte FE/FB/FC hinter dem A1/C2-Sync).
 *
 * **Warum @ref marks statt Suche nach 0xA1/0xFE:** In echtem MFM unterscheidet sich
 * das Sync-Byte A1 von einem Daten-A1 durch ein fehlendes Clock-Bit.  Datenbytes
 * dürfen also 0xA1/0xFE/0xFB enthalten, ohne als Marke missverstanden zu werden.
 * Die Markenpositionen werden vom Backend gesetzt (Raw: beim Bauen; HFE: beim
 * Decodieren aus dem Clock-Muster) und machen die Re-Sync-Logik des Controllers
 * robust und formatagnostisch.
 */
struct TrackImage {
    std::vector<uint8_t>  bytes;            ///< decodierte Spur-Bytes (eine Umdrehung)
    std::vector<MarkType> marks;            ///< parallel zu @ref bytes; markiert das Mark-Byte
    Encoding              encoding = Encoding::MFM;  ///< Verfahren der Spur (Re-Encode + Index-/HF-Logik)
    uint32_t              bitcells = 0;     ///< urspr. Bitzellen-Länge (0 = unbekannt/Raw); für HFE-Rückschreiben

    /**
     * @brief Wie viele **nominale** Bitzellen eine Modellzelle dieser Spur belegt.
     *
     * 1 = volle Datenrate (250 kbit/s, der Regelfall — MFM ebenso wie das 8″-FM der
     * MF3200/MF6400).  2 = **halbe** Datenrate: die Spur ist mit 125 kbit/s
     * aufgezeichnet, jede Zelle dauert doppelt so lang.
     *
     * Der Fall ist nicht theoretisch — die Bootspur (c0h0) der SCP1700-Disketten des
     * A7100 ist genau so aufgezeichnet, alle übrigen Spuren derselben Diskette mit
     * voller Rate (`data/formats.yaml`, `scp1700_640`).  Das Modell rechnet immer in
     * nominalen Zellen; der Faktor sagt nur, wie der Zellstrom beim **Laden**
     * heruntergerechnet und beim **Zurückschreiben** wieder gestreckt werden muss
     * (@ref BitCodec::downsampleCells / @ref BitCodec::upsampleCells).  Ohne ihn ginge
     * eine solche Spur mit doppelter Rate zurück auf die Diskette.
     */
    uint8_t               cell_factor = 1;

    /// @brief Leer, wenn die Spur keine Bytes enthält (z. B. nicht existierende Spur).
    bool   empty() const { return bytes.empty(); }
    /// @brief Anzahl Bytes der Spur (eine Umdrehung).
    size_t size()  const { return bytes.size(); }

    /**
     * @brief Index der nächsten Marke ab Position @p pos (zyklisch).
     *
     * Sucht ab @p pos vorwärts (mit Umlauf über das Spurende) die nächste Position,
     * deren @ref marks -Eintrag passt.  Beginnt die Suche bei @p pos selbst.
     *
     * @param pos  Startposition (Byte-Index); muss < @ref size() sein.
     * @param type Gesuchter Markentyp; @ref MarkType::None passt auf jede Marke.
     * @return Byte-Index der gefundenen Marke, oder SIZE_MAX wenn keine existiert.
     */
    size_t nextMark(size_t pos, MarkType type = MarkType::None) const;
};
