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

/// @brief Groesse der Karte: 384 Byte — bei ZDOS drei 128-B-, bei NDOS zwei
///        256-B-Sektoren (dort bleibt die zweite Haelfte des zweiten ungenutzt).
inline constexpr size_t kUdosBitmapBytes = 384;
/// @brief Offset des ersten Spureintrags.
inline constexpr size_t kUdosBitmapFirstTrack = 24;
/// @brief Konstante aus FORMATPC.MAC, mit der ZDOS den „belegt"-Zaehler bildet.
inline constexpr uint16_t kUdosCounterConstant = 2464;

/**
 * @enum UdosMapSitte
 * @brief Welche der beiden Karten-Sitten gilt.
 *
 * Die **Feldoffsets sind in beiden identisch** (Name 0…23, Eintraege ab 24, Zaehler bei
 * 375/378/379/380).  Unterschiedlich sind nur vier Dinge; getrennt werden die Karten bei
 * der Erkennung an den ZDOS-Kennzeichen `33H`/`F7H` (die in einer 80-Spur-Karte im
 * Belegungsplan der Spuren 78/79 laegen) und am Zaehlerabgleich — **nicht** an der
 * Fuellung dahinter, die beim P8000 wie bei ZDOS `77H` ist:
 *
 * | | @ref Zdos (A5120) | @ref Ndos1715 (PC 1715 · P8000) |
 * |---|---|---|
 * | Spureintraege | 78 (bis Offset 335) | **80** (bis Offset 343) |
 * | Fuellung dahinter | `11×33H · F7H · 27×77H` | **`00`** (P8000: `77H`) |
 * | „belegt"-Zaehler | `2464 − frei` (Festwert aus FORMATPC.MAC) | **die wirkliche Zahl** |
 * | abgelegt in | 3 Sektoren à 128 B, IDs 1–3 | **2 Sektoren à 256 B, IDs 1–2** |
 *
 * @see doc/udos_diskettenformat.md §4 · doc/udos1715_diskettenformat.md §3
 */
enum class UdosMapSitte : uint8_t {
    Zdos,      ///< UDOS 1526 / 4.x auf dem A5120
    Ndos1715   ///< UDOS1715/NDOS — PC 1715 und Robotron P8000 (UDOS 2.2)
};

/// @brief Anzahl der Spureintraege, die die Karte in dieser Sitte fuehrt.
inline constexpr uint8_t udosMapTrackSlots(UdosMapSitte s) {
    return s == UdosMapSitte::Ndos1715 ? 80 : 78;
}

/**
 * @class UdosBitmap
 * @brief Belegungskarte eines UDOS-Datentraegers (bei ZDOS: einer Seite).
 */
class UdosBitmap {
public:
    /// @brief Karte von Spur @p bitmap_track lesen.  @p head waehlt die Seite.
    static bool load(const SectorSpace& space, uint8_t head, uint8_t bitmap_track,
                     UdosBitmap& out, std::string& err,
                     UdosMapSitte sitte = UdosMapSitte::Zdos);

    /// @brief Karte zurueckschreiben (drei bzw. zwei Sektoren, s. @ref UdosMapSitte).
    bool store(SectorSpace& space, uint8_t head, uint8_t bitmap_track,
               std::string& err) const;

    UdosMapSitte sitte() const { return sitte_; }

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
                           uint8_t expect_tracks, std::string* why,
                           UdosMapSitte sitte = UdosMapSitte::Zdos);
    bool looksValid(uint8_t expect_secs, uint8_t expect_tracks, std::string* why) const {
        return looksValid(raw_, expect_secs, expect_tracks, why, sitte_);
    }

    const std::vector<uint8_t>& raw() const { return raw_; }
    std::vector<uint8_t>&       raw()       { return raw_; }

    /// @brief Frisch angelegte Karte (alles frei, Systemspuren noch nicht gesperrt).
    static UdosBitmap makeEmpty(uint8_t sectors_per_track, uint8_t tracks,
                                const std::string& label,
                                UdosMapSitte sitte = UdosMapSitte::Zdos);

private:
    std::vector<uint8_t> raw_ = std::vector<uint8_t>(kUdosBitmapBytes, 0);
    UdosMapSitte         sitte_ = UdosMapSitte::Zdos;
};
