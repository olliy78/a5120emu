/**
 * @file udos_bitmap.h
 * @brief UDOS-Belegungskarte — Spur 23 (17H), Sektoren 1–3.
 *
 * Drei physisch aufeinanderfolgende 128-B-Sektoren = 384 Byte
 * (`doc/udos_diskettenformat.md` §4):
 *
 * @code
 *   +000 … +023   Datentraegername, 24 B, mit 0x0D aufgefuellt
 *   +024 … +335   78 × 4 B Belegung, ein Eintrag je Spur 0…77
 *   +336 … +374   konstanter Nachlauf  11×0x33 · 0xF7 · 27×0x77
 *   +375, +376    16 Bit LE „belegt"-Zaehler   ⚠ unbrauchbar, s. unten
 *   +378          Sektoren je Spur
 *   +379          Anzahl Spuren
 *   +380, +381    16 Bit LE freie Sektoren
 * @endcode
 *
 * Ein Spureintrag ist 32 Bit **MSB zuerst**: Bit 31 = Sektor-ID 1 … Bit 6 = Sektor-ID 26,
 * gesetzt = belegt; die 6 ueberzaehligen Bits sind immer gesetzt.
 *
 * > **Den Zaehlern nicht trauen — die Karte ist die Wahrheit** (§4.2).  Der Zaehler bei
 * > +375 ergibt zusammen mit dem Freizaehler stets die Konstante 2464, die als Festwert
 * > in `FORMATPC.MAC` steht und nicht zur Kapazitaet von 77×26 = 2002 Sektoren passt.
 * > @ref UdosBitmap::countFree zaehlt deshalb die Bits aus.
 *
 * @see doc/udos_diskettenformat.md §4
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "core/filesystem/sector_space.h"

#include <cstdint>
#include <string>
#include <vector>

/// @brief Groesse der Karte: drei 128-B-Sektoren.
inline constexpr size_t kUdosBitmapBytes = 384;
/// @brief Offset des ersten Spureintrags.
inline constexpr size_t kUdosBitmapFirstTrack = 24;
/// @brief Konstante aus FORMATPC.MAC, mit der UDOS den „belegt"-Zaehler bildet.
inline constexpr uint16_t kUdosCounterConstant = 2464;

/**
 * @class UdosBitmap
 * @brief Belegungskarte einer UDOS-Seite (eine Seite = ein Datentraeger).
 */
class UdosBitmap {
public:
    /// @brief Karte von Spur @p bitmap_track lesen.  @p head waehlt die Seite.
    static bool load(const SectorSpace& space, uint8_t head, uint8_t bitmap_track,
                     UdosBitmap& out, std::string& err);

    /// @brief Karte zurueckschreiben (drei Sektoren).
    bool store(SectorSpace& space, uint8_t head, uint8_t bitmap_track,
               std::string& err) const;

    /// @brief Datentraegername (0x0D-Fuellung entfernt).
    std::string label() const;
    void        setLabel(const std::string& name);

    uint8_t  sectorsPerTrack() const { return raw_[378]; }
    uint8_t  trackCount()      const { return raw_[379]; }
    /// @brief Gespeicherter Freizaehler (+380) — zur Gegenprobe, nicht als Wahrheit.
    uint16_t storedFree()      const;
    /// @brief Gespeicherter „belegt"-Zaehler (+375) — s. Klassenkommentar.
    uint16_t storedUsed()      const;

    /// @brief Ist der Sektor belegt?  @p sector_id ist 1-basiert.
    bool used(uint8_t track, uint8_t sector_id) const;
    void setUsed(uint8_t track, uint8_t sector_id, bool belegt);

    /// @brief Freie Sektoren, **aus den Bits gezaehlt** (nur die nutzbaren Spuren).
    int countFree() const;
    /// @brief Belegte Sektoren, aus den Bits gezaehlt.
    int countUsed() const;

    /// @brief Beide Zaehler konsistent nachfuehren (so haelt es der Originalformatierer).
    void refreshCounters();

    /// @brief Sieht das nach einer echten UDOS-Karte aus? (Positivprobe der Erkennung)
    static bool looksValid(const std::vector<uint8_t>& raw, uint8_t expect_secs,
                           uint8_t expect_tracks, std::string* why);
    bool looksValid(uint8_t expect_secs, uint8_t expect_tracks, std::string* why) const {
        return looksValid(raw_, expect_secs, expect_tracks, why);
    }

    const std::vector<uint8_t>& raw() const { return raw_; }
    std::vector<uint8_t>&       raw()       { return raw_; }

    /// @brief Frisch angelegte Karte (alles frei, Systemspuren noch nicht gesperrt).
    static UdosBitmap makeEmpty(uint8_t sectors_per_track, uint8_t tracks,
                                const std::string& label);

private:
    std::vector<uint8_t> raw_ = std::vector<uint8_t>(kUdosBitmapBytes, 0);
};
