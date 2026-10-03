/**
 * @file prg_boot.h
 * @brief Bootabbilder der PRG 710 / PRG 710-1 erkennen und zusammensetzen
 *        (doc/design/20_prg710.md AP-P6).
 *
 * Eine Systemdiskette des PRG kommt in zwei Bauarten (UDOS 4.3, SCPX 1526), je Gerät
 * in einer eigenen Fassung: Tastatur und MKE-Treiber unterscheiden sich (710: 8279 an
 * C8H/C9H, 710-1: K7672 an der K8025 5CH–5FH).  Ein Bootabbild der falschen Fassung
 * liest sich fehlerfrei und scheitert erst am Gerät — deshalb wird VOR dem Schreiben
 * erkannt, für welches Gerät ein Abbild gebaut ist.
 *
 * Erkannt wird am **Ladesektor**: Byte 0–4 = `18 03 "SYL"` (JR +3 über die Kennung) —
 * der A5120-Lader beginnt mit `"SYL"` selbst (UDOS) bzw. `"SYL" 95H` (SCPX).  Die Fassung
 * steht je System an einer anderen Stelle (Belege in der .cpp).  Das Abbild ist das
 * rohe Byteband von `DiskVolume::readBootImage`; UDOS trägt dort je Sektor 4 Byte
 * Kontrollblock — die Suche ist deshalb nicht an Sektorgrenzen gebunden.
 *
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prg_boot {

enum class System : uint8_t { Udos, Scpx };

enum class Variante : uint8_t { Unbekannt, Prg710, Prg710_1 };

/// @brief Ergebnis von @ref erkenne.
struct Kennung {
    bool     ist_prg  = false;               ///< Ladesektor des PRG (`18 03 "SYL"`)
    Variante variante = Variante::Unbekannt; ///< nur gesetzt, wenn eindeutig
};

/// @brief "PRG 710" / "PRG 710-1" / "unbekannt".
const char* name(Variante v);

/// @brief "710" / "710-1" (Schalter `--prg`) → Variante; sonst Unbekannt.
Variante ausName(const std::string& s);

/// @brief Beurteilt ein Bootabbild (Byteband der Systemspuren) des Systems @p sys.
Kennung erkenne(const std::vector<uint8_t>& img, System sys);

/**
 * @brief Urteil über ein Bootabbild gegen das Gerät, für das es gebaut sein soll.
 * @param img        das Abbild
 * @param sys        das System, das das Dateisystem der Zieldiskette erwarten lässt
 * @param erwartet   Unbekannt = kein Wunsch (nur die Strukturprüfung)
 * @param vorhanden  Fassung des Systems, das schon auf der Diskette steht (oder Unbekannt)
 * @return "" = in Ordnung, sonst die Begründung.  Ein Abbild OHNE PRG-Lader wird ohne
 *         Wunsch nie beanstandet (A5120/K8915 bleiben unberührt); mit Wunsch ist es ein Fehler.
 */
std::string problem(const std::vector<uint8_t>& img, System sys, Variante erwartet,
                    Variante vorhanden);

/**
 * @brief SCPX-Systemspuren aus den drei Modulen zusammensetzen (wie `SYSPRG`).
 *
 * Byte 0 = C600H: `SYL17.SYS` (256 B, Lader) + 256 × AAH + `CCPBD17.SYS` (ab C800H)
 * + BIOS (ab DE00H = Byte 6144), danach AAH bis zum vollen 256-B-Sektor.  Das ist
 * bytegleich das, was `SYSPRG` im Emulator schreibt (AP-P5e, Befund 2).
 * @param bios_name  Dateiname des BIOS (nur für die Meldung)
 * @param ziel       Gerät; Unbekannt = aus dem BIOS ablesen
 * @param[out] erkannt  die am BIOS erkannte Fassung
 */
bool scpxBand(const std::vector<uint8_t>& syl, const std::vector<uint8_t>& ccp,
              const std::vector<uint8_t>& bios, Variante ziel,
              std::vector<uint8_t>& out, Variante& erkannt, std::string& err);

}  // namespace prg_boot
