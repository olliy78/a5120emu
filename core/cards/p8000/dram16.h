/**
 * @file dram16.h
 * @brief P8000 16-Bit-Teil: Hauptspeicher am Speicherbus X9–X13 — bis zu vier DRAM-Karten
 *        (256 KB Index 0/1/3-Var. 2, 1 MB Index 3-Var. 1, RAM-Karte 16 MB von 2009 mit 2/4/8/16 MB),
 *        Moduladresse, Byte-/Wortzugriff, Parität mit PE−, Refresh als Zähler.
 *
 * Quelle (Rang 2, ein Kartenschaltplan liegt NICHT vor — schaltplan_16bit.md §3, Frage 8):
 * Handbuch §8/§9 = doc/p8000/hw_16bit.md §6; AP P10a (doc/design/25_p8000.md §10.4, §10.11).
 *
 * @code
 *   256-KB-Karte: Moduladresse n = A23…A18 (0…63) ⇒ n·40000H … n·40000H + 3FFFFH
 *   1-MB-Karte:   Moduladresse n = A23…A20 (0…15) ⇒ n·100000H … n·100000H + FFFFFH
 *   RAM-Karte 16 MB (robotrontechnik.de 2009, Schaltplan + GAL-Gleichungen, ram_konfiguration.md):
 *                 KEINE Moduladresse — die Karte liegt immer ab 0 (GAL U9: 2 MB = A21…A23 = 0,
 *                 4 MB = A22/A23 = 0, 8 MB = A23 = 0, 16 MB = alles).  Bei 8/16 MB sperrt U16 (7430,
 *                 A16…A22 = 1) Segment 7FH = 7F0000…7FFFFF — Umgehung eines Firmwarefehlers (MON16
 *                 Testschritt 70 käme bei 128 vorhandenen Segmenten nicht zum Ende).  Keine
 *                 Paritätsbits (SIMMs ×8, PE−/CLR-PAR− am Stecker frei).
 *   Wort: A0 = 0, beide Bänke.  Byte schreiben: A0 = 1 ⇒ D0–7 (ungerade), A0 = 0 ⇒ D8–15.
 *   Byte lesen: beide Bänke auf den Bus, die CPU wählt (Handbuch §3.1).
 * @endcode
 *
 * Benannte Annahmen (Lücken der Quellen):
 *  [D1] Überlappende Moduladressen sind ein Konfigurationsfehler (`pruefe()`), keine
 *       Mehrfachselektion — am Gerät stritten zwei Karten um den Datenbus.  Lücken sind erlaubt
 *       (Handbuch: „zusammenhängend", aber ohne Sperre); MON16 findet MAXSEG am ersten Loch.
 *  [D2] Parität gerade/ungerade ist unbelegt und für den Emulator gleichgültig: ein Fehler entsteht
 *       nur als eingepflanztes Fehlerbild (`paritaetsfehlerSetzen`).  Schreiben erzeugt die
 *       Paritätsbits neu (Read-Modify-Write der Karte) und löscht das Fehlerbild DES Bytes.
 *  [D3] Lesen prüft BEIDE Bytes des Wortes, auch beim Bytezugriff (beide Bänke liegen auf dem
 *       Bus, §3.1).  PE− ist ein Pegel NUR während des fehlerhaften Lesezyklus; das Speichern macht
 *       die 16-Bit-Karte (Paritäts-FF, SCR Bit 3).  Die rote LED der Karte hält bis RES− oder
 *       CL_PAR− (`ledFehler()`).
 *  [D4] Kein Inhaltsverlust ohne Refresh (Entwurf §10.2: nachzubilden wäre ein Fehlerbild, kein
 *       Nutzen).  Refreshzyklen werden nur gezählt.
 *  [D5] Inhalt nach Netz-Ein = `Config::fuellwert` (real unbestimmt).
 *
 * Save-State: serialize()/deserialize(), Version 1 (Inhalt aller Karten, Fehlerbilder, LED).
 *
 * @license MIT
 */
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

class P8000Dram16 {
public:
    struct Karte {
        /// K256/M1: Robotron-Karten mit Moduladresse.  R2…R16: RAM-Karte 16 MB, bestückt mit
        /// 2 / 4 / 8 / 16 MB (Brücken J2 = 1/2 Bänke, J3 = 1/4-MB-SIMMs), Moduladresse immer 0.
        /// Werte sind Save-State-Format (P8KS-Fingerabdruck, Dram16 v1) — nur anhängen.
        enum class Typ : uint8_t { K256, M1, R2, R4, R8, R16 };
        Typ     typ = Typ::M1;
        uint8_t modul = 0;      ///< Wickelbrücken: 0…63 (256 K) bzw. 0…15 (1 M); RAM-Karte: 0
        bool ramKarte() const { return typ >= Typ::R2; }
        uint32_t basis() const {
            if (ramKarte()) return 0;
            return typ == Typ::M1 ? uint32_t(modul) << 20 : uint32_t(modul) << 18;
        }
        uint32_t groesse() const {
            switch (typ) {
                case Typ::K256: return 0x40000u;
                case Typ::M1:   return 0x100000u;
                case Typ::R2:   return 0x200000u;
                case Typ::R4:   return 0x400000u;
                case Typ::R8:   return 0x800000u;
                case Typ::R16:  return 0x1000000u;
            }
            return 0;
        }
        /// MEMSEL der Karte für die Busadresse @p adr (A0–A23) — Bereich, bei der RAM-Karte
        /// ab 8 MB Bestückung ohne Segment 7FH (U16).
        bool waehlt(uint32_t adr) const {
            adr &= 0xFFFFFF;
            if (adr - basis() >= groesse()) return false;
            if ((typ == Typ::R8 || typ == Typ::R16) && (adr >> 16) == 0x7F) return false;
            return true;
        }
        /// Paritätsbits (nur die Robotron-Karten).
        bool paritaet() const { return !ramKarte(); }
        /// Klartext, z. B. „256 KB @ Modul 3 (0C0000H)" / „RAM-Karte 16 MB".
        std::string text() const;
    };
    static constexpr int MAX_KARTEN = 4;   ///< Speicherbus X10–X13 (X9 = Testbus/Karte selbst)

    struct Config {
        std::vector<Karte> karten = {Karte{Karte::Typ::M1, 0}};   ///< Vorgabe 1 MB, Modul 0
        uint8_t fuellwert = 0x00;                                 ///< [D5]
    };

    /// Leerer Text = gültig; sonst der Grund (Anzahl, Modulbereich, Überlappung [D1],
    /// RAM-Karte nicht auf Modul 0 oder doppelt).
    static std::string pruefe(const Config& cfg);

    /// Konfigurationstext → Karten (leerer Rückgabetext = gültig, Grund sonst; prüft auch pruefe()).
    /// Lang: Karten mit '+' getrennt, `1M@n` (n 0–15), `256K@n` (0–63), `2M@0`/`4M@0`/`8M@0`/`16M@0`
    /// (RAM-Karte).  Kurz: `4x256K` (= 256K@0+…+256K@3), `NxT` mit N 1–4 und T 256K|1M ab Modul 0,
    /// `2M`/`4M`/`8M`/`16M` (RAM-Karte allein).  Groß-/Kleinschreibung egal, `×` statt `x` erlaubt.
    static std::string parse(const std::string& text, std::vector<Karte>& karten);
    /// Langform zu @p karten (Umkehrung von parse, für Fingerabdruck/Anzeige).
    static std::string format(const std::vector<Karte>& karten);
    /// Höchstes Segment, das MON16 (Testschritt 70) findet: vom Segment 0 an aufwärts bis zum ersten,
    /// in dem weder Offset 8000H noch FFFEH antwortet (MMU aus: phys. = SN·64K + Offset, A23 = 0).
    /// -1 = Segment 0 fehlt (FATAL).  Bei 128 Segmenten ohne Loch käme MON16 nicht zum Ende —
    /// geliefert wird dann 0x7F (s. ram_konfiguration.md §4).
    static int maxSegment(const std::vector<Karte>& karten);

    P8000Dram16();
    /// Ungültige Karten (s. pruefe) werden NICHT gesteckt; der Grund steht in fehler().
    explicit P8000Dram16(const Config& cfg);

    const std::string& fehler() const { return fehler_; }
    const Config&      config() const { return cfg_; }
    /// Gesteckte Karten (nach pruefe), Summe in Byte.
    const std::vector<Karte>& karten() const { return karten_; }
    uint32_t gesamt() const;

    // ── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn();                 ///< Inhalt := Füllwert, Fehlerbilder und LED weg [D5]
    void reset();                   ///< RES− (BUSMRESET−): LED aus, Inhalt bleibt

    // ── Bus (A0–A23, nur bei SYSDS — die Karte16 sperrt unterdrückte Zyklen selbst) ──
    /// Wort an @p adr & ~1.  false = keine Karte gewählt (MEMSEL inaktiv), @p wort unverändert.
    bool lesen(uint32_t adr, uint16_t& wort);
    /// @p wort = true: beide Bytes; sonst das Byte nach A0 aus der passenden Bushälfte.
    bool schreiben(uint32_t adr, bool wort, uint16_t daten);
    /// PE− im letzten Lesezyklus (Pegel nur während dieses Zyklus) [D3].
    bool pe() const { return pe_; }
    /// Refreshzyklus (Status 0001): gezählt, ohne Wirkung [D4].
    void refresh() { ++refreshs_; }
    /// CL_PAR− (SCR Bit 3 = 0 ist aktiv): löscht die LED.
    void setzeClrParitaet(bool aktiv) { if (aktiv) led_ = false; }
    bool ledFehler() const { return led_; }

    // ── Fehlerbild, Debugger (ohne Nebenwirkung auf PE/LED) ─────────────────
    /// Das Paritätsbit des Bytes @p adr ist falsch, bis das Byte neu geschrieben wird [D2].
    bool paritaetsfehlerSetzen(uint32_t adr);
    bool gewaehlt(uint32_t adr) const { return const_cast<P8000Dram16*>(this)->zelle(adr) != nullptr; }
    uint8_t peek(uint32_t adr) const;
    bool    poke(uint32_t adr, uint8_t wert);
    uint64_t refreshZyklen() const { return refreshs_; }

    // ── Save-State ──────────────────────────────────────────────────────────
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    uint8_t* zelle(uint32_t adr);

    Config cfg_;
    std::string fehler_;
    std::vector<Karte> karten_;
    std::vector<std::vector<uint8_t>> inhalt_;   ///< je gesteckte Karte
    std::set<uint32_t> paritaetFalsch_;          ///< Busadressen mit falschem Paritätsbit [D2]
    bool pe_ = false, led_ = false;
    uint64_t refreshs_ = 0;
};
