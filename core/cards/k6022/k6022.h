/**
 * @file k6022.h
 * @brief ADA K6022 (012-7090) — zwei SIF1000-Anschlüsse: Lochbandstanzer und
 *        Lochbandleser, je eine PIO (doc/design/20_prg710.md §3.8, AP-P8b;
 *        Befunde doc/prg710/sif1000_fernschreiber.md).
 *
 * Belegung [Disk] (`DRUCK.DOK` §4, `PTAPE.6022` F115H–F158H):
 * @code
 *   E0H Daten A   E1H Daten B   E2H Steuer A   E3H Steuer B    PIO „Stanzer“ (ST)
 *   E4H Daten A   E5H Daten B   E6H Steuer A   E7H Steuer B    PIO „Leser“   (LE)
 * @endcode
 * Die PIOs liegen also mit A0 = B/A und A1 = C/D am Bus (nicht in der Reihenfolge
 * Daten A, Steuer A, … der `Z80PIO`-Relativports) — die Karte setzt um.
 *
 * SIF1000 an der PIO (Treiber `PTAPE.6022`, Protokoll robotrontechnik.de):
 * - **Stanzer**: Tor A Betriebsart 0 (Ausgabe), Vektor EEH, Interrupt ein.  DAT = Tor A,
 *   **RUF = ARDY** (Daten geschrieben), **END = /ASTB** (Stanzer fertig → Interrupt).
 *   Tor B Bitbetrieb: D0–D3 Ausgänge (KOM, `PTAPE` schreibt 01H, `SD1156` 00/03/05H),
 *   D4–D7 Eingänge (STA; `PTAPE` wertet D5|D6 ≠ 0 als Fehler „C2“ aus).
 * - **Leser**: Tor A Betriebsart 1 (Eingabe), Vektor ECH, Interrupt ein.  **RUF = ARDY**
 *   (die CPU hat das Datenregister gelesen), **END = /ASTB** (Byte liegt an → Interrupt).
 *   Tor B Bitbetrieb: D0–D3 Ausgänge (KOM, 03H), D4–D7 Eingänge; **D6 = Bandende /
 *   kein Band** (F191H: vor Datenbeginn → Fehler „C2“, danach zählt es wie Nullbytes).
 *
 * Modell der Geräte (daro 1210 / daro 1215):
 * - Der **Leser** liefert das eingelegte Band (eine Datei, Bytes wie gestanzt) Byte für
 *   Byte, jeweils `Config::leser_zeichen_s` nach RUF.  Davor und dahinter liegt ein Vor-
 *   bzw. Nachlauf aus Nullbytes (ein gestanztes Band hat beides; `PTAPE.6022` verwirft
 *   das zuerst gelesene Byte und erkennt das Bandende an 100 Nullbytes in Folge).  Erst
 *   wenn die CPU das letzte Byte abgeholt hat, meldet STA D6 „Bandende“.
 * - Der **Stanzer** nimmt jedes Byte, das die CPU in Betriebsart 0 nach Tor A schreibt,
 *   nach `Config::stanzer_zeichen_s` ins Stanzband und quittiert mit END.  STA = 0.
 *   Ohne eingeschalteten Stanzer kommt kein END — der Treiber läuft in seine Frist
 *   (≈ 0,9 s, „C2“), wie am Gerät mit ausgeschaltetem Stanzer.
 *
 * **Dateien** (doc/design/23_lochstreifen.md §4/§5, AP-L2) in einem der Bandformate
 * (`core/peripherals/lochstreifen/band_format.h`: Roh, Intel HEX, ASCII-Art):
 * - Der **Leser** liest die Datei einmal ganz und legt ihren Inhalt als Band ein.
 * - Der **Stanzer** wird an eine Datei **gebunden** wie ein Laufwerk an ein Abbild: eine
 *   vorhandene Datei ist der Anfang des Bandes (weiterstanzen hängt an), eine fehlende
 *   wird leer angelegt.  Zurückgeschrieben wird **verzögert** nach einer Stanzpause von
 *   @ref kStanzpauseS Maschinenzeit, sofort beim Lösen, beim Wechsel der Datei, bei
 *   „Neues Band“ und im Destruktor.  Ohne Bindung sammelt der Stanzer im Speicher.
 *
 * **Faden:** `clockTick`, `autoFlush`, die Busseite und die PIO-Interrupts nur aus dem
 * Lauffaden.  Alles andere aus jedem Faden (Sperre m_; Datei-E/A zusätzlich unter
 * datei_m_, damit sich zwei Schreiber nicht mit veraltetem Stand überholen).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/peripherals/lochstreifen/band_format.h"
#include "core/primitives/z80_pio.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class K6022 {
public:
    struct Config {
        uint8_t  basis = 0xE0;              ///< Stanzer E0H–E3H, Leser E4H–E7H
        /// Lesegeschwindigkeit daro 1210 [?] (SIF1000 = „1000 Zeichen je Sekunde“).
        uint32_t leser_zeichen_s = 1000;
        /// Stanzgeschwindigkeit daro 1215 [?].
        uint32_t stanzer_zeichen_s = 150;
        uint32_t vorlauf  = 16;             ///< Nullbytes vor dem Bandinhalt
        uint32_t nachlauf = 128;            ///< Nullbytes dahinter (> 100, s. o.)
    };

    /// STA-Bits an Tor B (Eingänge D4–D7).
    static constexpr uint8_t STA_BANDENDE = 0x40;   ///< Leser D6 [Disk: PTAPE F191H]

    /// Bandformat einer Datei (stabile Kennung Roh = 0, IntelHex = 1, AsciiArt = 2).
    using Format = lochstreifen::Format;
    /// Stanzpause, nach der ein gebundener Stanzer seine Datei zurückschreibt.
    static constexpr double kStanzpauseS = 0.5;

    explicit K6022(uint32_t cpu_hz) : K6022(Config{}, cpu_hz) {}
    K6022(const Config& cfg, uint32_t cpu_hz);
    /// Schreibt ein gebundenes Stanzband zurück (Fehler nur ins Protokoll).
    ~K6022();
    K6022(const K6022&) = delete;
    K6022& operator=(const K6022&) = delete;

    void attachToBus(K1520Bus& bus);
    /// /RESET: beide PIOs in den Einschaltzustand, laufende Übertragung verworfen.
    /// Band im Leser (samt Stellung) und Stanzband bleiben — sie gehören den Geräten.
    void reset();

    /// Maschinentakte fortschreiben; true, wenn sich ein Interruptzustand geändert haben kann.
    /// Stellt auch fest, ob die Stanzpause abgelaufen ist — geschrieben wird erst in
    /// @ref autoFlush (Datei-E/A gehört nicht in den Pfad je Instruktion).
    bool clockTick(int takte);
    /**
     * @brief Laufweg, einmal je run()-Scheibe: fällige Stanzdatei zurückschreiben.
     *
     * Wie `Laufwerke::autoFlush` für die Disketten im Lauffaden — ein Lochstreifen ist
     * höchstens einige KiB groß, und das Schreiben fällt nur nach einer Stanzpause an.
     * @return true, wenn geschrieben wurde.
     */
    bool autoFlush();

    // ── Interruptkette: die Maschine hängt beide PIOs ein (Stanzer vor Leser [?]) ──
    Z80PIO& pioStanzer() { return pio_st_; }
    Z80PIO& pioLeser()   { return pio_le_; }

    // ── Leser (jeder Faden) ──────────────────────────────────────────────────
    /// Band einlegen (ersetzt ein eingelegtes, Stellung auf Bandanfang).
    void bandEinlegen(std::vector<uint8_t> inhalt);
    /// Band aus einer Datei im Format @p fmt einlegen; false (+ @p fehler, beim
    /// Inhalt mit Zeilennummer), wenn sie nicht lesbar ist oder nicht zum Format passt.
    bool bandEinlegenDatei(const std::string& pfad, Format fmt, std::string& fehler);
    /// Dasselbe im Format Roh (alte C-ABI `k1520_ptape_load`).
    bool bandEinlegenDatei(const std::string& pfad, std::string& fehler) {
        return bandEinlegenDatei(pfad, Format::Roh, fehler);
    }
    void bandEntnehmen();
    /// Datei des eingelegten Bandes ("" = keins oder aus dem Speicher eingelegt).
    std::string leserDatei() const;
    Format leserFormat() const;
    struct LeserStand {
        bool     eingelegt = false;
        uint64_t laenge    = 0;     ///< Bytes der Datei (ohne Vor-/Nachlauf)
        uint64_t gelesen   = 0;     ///< davon schon abgeholt (0 … laenge)
        bool     bandende  = false; ///< ganz durchgelaufen (STA D6)
    };
    LeserStand leserStand() const;

    // ── Stanzer (jeder Faden) ────────────────────────────────────────────────
    std::vector<uint8_t> stanzband() const;
    uint64_t stanzbandLaenge() const;
    /// Wie @ref neuesBand (ist der Stanzer gebunden, wird auch die Datei geleert).
    void stanzbandLeeren() { neuesBand(); }
    /// Stanzband in eine Datei schreiben (überschreibt, ohne zu binden); false + @p fehler.
    bool stanzbandSpeichern(const std::string& pfad, std::string& fehler,
                            Format fmt = Format::Roh) const;

    /**
     * @brief Stanzer an eine Datei im Format @p fmt binden (§5).
     *
     * Eine bestehende Bindung wird vorher zurückgeschrieben.  Eine vorhandene Datei wird
     * gelesen und ersetzt das Stanzband (weiterstanzen hängt an); eine fehlende wird mit
     * leerem Band angelegt.  false + @p fehler (alte Bindung bleibt dann bestehen), wenn
     * die Datei nicht lesbar, nicht im Format oder nicht schreibbar ist.
     */
    bool stanzerBinden(const std::string& pfad, Format fmt, std::string& fehler);
    /// Bindung lösen: zuerst zurückschreiben; das Band bleibt im Speicher.  false, wenn
    /// das Zurückschreiben scheiterte (Grund in @ref stanzerFehler; gelöst wird trotzdem).
    bool stanzerLoesen();
    /// „Neues Band“: Stanzband leeren und eine gebundene Datei SOFORT leer schreiben.
    bool neuesBand();
    /// Zurückschreiben, falls gebunden und geändert (aus jedem Faden).
    bool stanzerZurueckschreiben();
    std::string stanzerDatei() const;     ///< "" = nicht gebunden
    Format stanzerFormat() const;
    /// Letzter Schreibfehler der Bindung ("" = keiner seit dem letzten Erfolg).
    std::string stanzerFehler() const;
    /// Stanzer ein/aus (Vorgabe ein).  Aus: kein END, der Treiber meldet nach seiner Frist C2.
    void setStanzerEin(bool ein);
    bool stanzerEin() const;

private:
    /// Bus-Seite einer PIO: E0/E1/E2/E3 → Daten A, Daten B, Steuer A, Steuer B.
    struct Tor : BusDevice {
        Tor(K6022& k, bool leser) : karte(k), leser(leser) {}
        uint8_t ioRead(uint8_t port) override;
        void ioWrite(uint8_t port, uint8_t d) override;
        const char* deviceName() const override {
            return leser ? "K6022 Leser (E4H)" : "K6022 Stanzer (E0H)";
        }
        K6022& karte;
        bool   leser;
    };
    static uint8_t relativ(uint8_t port) {   // A0 = B/A, A1 = C/D → Z80PIO 0=A-D,1=A-S,2=B-D,3=B-S
        return static_cast<uint8_t>(((port & 1) << 1) | ((port >> 1) & 1));
    }
    void leserRuf();                 ///< CPU hat Tor A des Lesers gelesen (Betriebsart 1)
    void stanzerRuf(uint8_t byte);   ///< CPU hat Tor A des Stanzers beschrieben (Betriebsart 0)
    void leserStatusSetzen();        ///< STA an Tor B beider PIOs (unter m_)
    uint64_t bandGesamt() const { return cfg_.vorlauf + band_.size() + cfg_.nachlauf; }
    /// Schnappschuss der Bindung nehmen (optional dabei lösen) und schreiben; unter datei_m_.
    bool zurueckschreibenGesperrt(bool loesen);

    const Config   cfg_;
    const uint64_t leser_takte_;
    const uint64_t stanzer_takte_;
    const uint64_t pause_takte_;     ///< @ref kStanzpauseS in Maschinentakten
    Z80PIO pio_st_{"K6022 Stanzer"};
    Z80PIO pio_le_{"K6022 Leser"};
    Tor    tor_st_{*this, false};
    Tor    tor_le_{*this, true};

    uint64_t zeit_ = 0;              ///< Maschinentakte seit Netz-Ein (nur Lauffaden)
    uint64_t flush_ab_ = 0;          ///< Ende der Stanzpause (0 = nichts; nur Lauffaden)
    /// Stanzpause abgelaufen — @ref autoFlush schreibt.
    std::atomic<bool> flush_faellig_{false};
    std::mutex datei_m_;             ///< reiht Dateischreiber; immer VOR m_ nehmen
    /// Etwas unterwegs oder von außen geändert — sonst kehrt clockTick ohne Sperre zurück.
    std::atomic<bool> arbeit_{true};

    mutable std::mutex m_;
    // ── unter m_ ──
    bool     band_drin_ = false;
    std::vector<uint8_t> band_;
    uint64_t pos_ = 0;               ///< nächstes Byte im Gesamtband (Vorlauf + Band + Nachlauf)
    bool     bandende_ = false;
    bool     leser_ruf_ = false;     ///< RUF steht, Byte unterwegs
    uint64_t leser_faellig_ = 0;
    uint8_t  leser_sta_ = 0xFF;      ///< zuletzt an Tor B gelegt (0xFF = noch nie)
    uint8_t  stanz_sta_ = 0xFF;      ///< dasselbe am Stanzer
    std::vector<uint8_t> stanz_;
    bool     stanzer_ein_ = true;
    bool     stanz_ruf_ = false;
    uint8_t  stanz_byte_ = 0;
    uint64_t stanz_faellig_ = 0;
    std::string leser_pfad_;
    Format      leser_format_ = Format::Roh;
    std::string stanz_pfad_;         ///< gebundene Datei ("" = keine)
    Format      stanz_format_ = Format::Roh;
    bool        stanz_dirty_ = false;   ///< Stanzband weicht von der Datei ab
    std::string stanz_fehler_;
};
