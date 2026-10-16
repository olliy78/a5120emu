/**
 * @file k5020.h
 * @brief AKB K5020 (045-8028) — Anschlusssteuerung fuer zwei Kassettenmagnetbandgeraete K5200
 *        (IFKB) an zwei U855-PIOs (doc/design/29_kassette.md, AP K4; Abschrift der Unterlagen
 *        doc/kassette/k5020.md, Berichtigungen und Treiberbefunde doc/kassette/treiber.md).
 *
 * **Portplan** (`basis` + …, A5120 30H, K8915 D0H; AB0 = Tor, AB1 = Steuerwort, AB2 = Baustein):
 * @code
 *   +0 Daten A  +1 Daten B  +2 Steuer A  +3 Steuer B     Daten-PIO   (Baustein 1)
 *   +4 Daten A  +5 Daten B  +6 Steuer A  +7 Steuer B     Steuer-PIO  (Baustein 2)
 * @endcode
 * (Ports an der Karte wie bei der K6022: A0 = B/A, A1 = Steuerwort; die Karte setzt auf die
 * Relativports des `Z80PIO` um.)
 *
 * **Daten-PIO.**  Tor A Betriebsart 0 = Byte-Ausgabe aufs Band, Tor B Betriebsart 1 =
 * Byte-Eingabe vom Band, beide interruptgesteuert.  Die Karte bildet die Parallel-Seriell-
 * Wandlung nach: ein Byte belegt das Schieberegister eine Bytezeit (8 / Bitrate des
 * Laufwerks, 667 µs bei 12 kBit/s); wird sie frei, nimmt die Karte das naechste Byte aus
 * dem PIO-Ausgaberegister (ARDY) und quittiert mit ASTB (→ Interrupt).  Liegt keines an,
 * wird **ZF** gesetzt (bleibt bis zur naechsten Uebernahme).  Eingabe: ein vom Laufwerk
 * geliefertes Byte wird mit BSTB in die PIO uebernommen — sofern BRDY aktiv ist (Blindeingabe
 * nach dem Einschalten, `Z80PIO::setModus1ReadyNachLesen`); sonst geht es verloren
 * (@ref verloreneBytes), wie am Geraet, wenn die CPU nicht rechtzeitig liest.
 *
 * **Steuer-PIO** (Betriebsart 3): Pegel wie in k5020.md §3, **AEB nach treiber.md §3.1**
 * (Bit 7 = 1: Magnetband unter der Lichtschranke, 0: Vorspann/Marke).
 * @code
 *   Tor A  Bit0 /RES2  1 /RES1  2 /AWA2  3 /AWA1  4 TRV  5 TRR  6 HGE  7 AEB (Eingang)
 *   Tor B  Bit0 AZV (0 = 38 cm/s)  1 AUF (Ausgang)  2 KSB  3 /NIB2  4 /NIB1  5 BMS  6 STA  7 ZF
 * @endcode
 * Auswahl nach der Funktionstabelle k5020.md §3.1: Bits 3/2 = 1/0 waehlt Geraet 1, 0/1 Geraet 2,
 * 0/0 ist das interne Ruecksetzen RSI.  TRV und TRR wirken nur am angewaehlten Geraet; beide
 * zusammen sind UMS.  `HGE` zusammen mit `AUF` sperrt die Wiedergabe (kein Echo) und loest
 * keinen Suchlauf aus.  `AUF` erreicht das Geraet nur, wenn es nicht schreibgeschuetzt ist
 * (AVE); `STA` = AUF ∧ AVE.  `BMS` faellt erst 108 µs nach dem letzten Flusswechsel.
 * Die Steuer-PIO arbeitet flankengetriggert (`setModus3Flanke`, U855-Beschreibung §6.4).
 *
 * **Nicht nachgebildet** (ohne Wirkung fuer die Software, k5020.md §10): Stoerflusswechsel-Kontrolle
 * (`STA` aus FWF), Praeambelkontrolle (HGE-Puls), `/RDY`-Waits, die genaue Loeschbedingung von ZF.
 *
 * **Takt.**  Die Zeiten laufen in Maschinentakten (`Config::takt_hz`); die Bitrate kommt
 * aus dem Laufwerksprofil, nicht aus dem Bustakt (Bitzelle = Zeit, Fund K0/M0).
 * `Takt::ExternerCtc` (umgebaute K5020 am MC80.30, AP M7): der Schreibtakt kommt ueber den
 * Koppelbus von aussen (@ref setExternerTakt, MC80: ASP-CTC K2 als Zaehler ÷2).  Fehlt er,
 * uebernimmt das Schieberegister kein Byte — das Ausgaberegister bleibt voll, bis der Takt
 * anliegt.  Die Leseseite ist selbsttaktend (Richtungstaktschrift) und haengt nicht daran.
 * Welche Frequenz die Leitung fuehrt, ist nicht belegt [?]; die Bitrate bleibt die des Profils.
 *
 * **Reset** setzt beide PIOs zurueck und laesst alle Leitungen fallen; die Baender bleiben
 * liegen.  Kein Save-State.  **Faden:** alles aus dem Lauffaden (keine eigene Sperre).
 */

#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "core/bus/k1520_bus.h"
#include "core/peripherals/kassette/kassetten_laufwerk.h"
#include "core/primitives/z80_pio.h"

class K5020 {
public:
    enum class Takt { Bustakt, ExternerCtc };

    struct Config {
        uint8_t  basis = 0x30;             ///< A5120 30H–37H, K8915 D0H–D7H
        uint32_t takt_hz = 2'457'600;      ///< Maschinentakt fuer clockTick
        Takt     takt = Takt::Bustakt;
        kassette::ProfilId profil1 = kassette::ProfilId::K5200_38;   ///< Geraet 1 (X3)
        kassette::ProfilId profil2 = kassette::ProfilId::K5200_38;   ///< Geraet 2 (X4)
        /// Bandmasse neu angelegter Kassetten an beiden Geraeten (MC80: Kompaktkassette ohne Marken).
        kassette::Geometrie neue_kassette{};
    };

    K5020() : K5020(Config{}) {}
    explicit K5020(const Config& cfg);
    K5020(const K5020&) = delete;
    K5020& operator=(const K5020&) = delete;

    void attachToBus(K1520Bus& bus);
    /// /RESET: PIOs und Ablauf zurueck, alle Leitungen zu den Geraeten fallen; Baender bleiben.
    void reset();

    /// Maschinentakte fortschreiben (auch die beiden Laufwerke); true, wenn sich ein
    /// Interruptzustand geaendert haben kann.
    bool clockTick(int takte);
    /// Faellige Baender zurueckschreiben (einmal je run()-Scheibe).  true, wenn geschrieben wurde.
    bool autoFlush();

    /// Beide PIOs fuer die Interruptkette: Daten-PIO vor Steuer-PIO (k5020.md §5.2).
    Z80PIO& pioDaten()  { return daten_; }
    Z80PIO& pioSteuer() { return steuer_; }

    /// Geraet @p n (0 = KMBG 1 / X3, 1 = KMBG 2 / X4).
    kassette::KassettenLaufwerk& laufwerk(int n) { return *lw_[n & 1]; }
    const kassette::KassettenLaufwerk& laufwerk(int n) const { return *lw_[n & 1]; }
    /// Nur bei `Takt::ExternerCtc`: liefert die Koppelbusleitung gerade einen Bittakt?
    /// Ohne Rueckruf gilt der Takt als vorhanden.
    void setExternerTakt(std::function<bool()> f) { ext_takt_ = std::move(f); }
    /// Liegt der Schreibtakt an (Bustakt: immer)?
    bool taktLiegtAn() const { return cfg_.takt == Takt::Bustakt || !ext_takt_ || ext_takt_(); }
    /// Angewaehltes Geraet (0/1) oder -1.
    int angewaehlt() const { return sel_; }

    // ── Zustand fuer Oberflaeche/Tests ──
    bool zeitfehler() const { return zf_; }
    uint64_t verloreneBytes() const { return verloren_; }
    const Config& config() const { return cfg_; }

private:
    struct Tor : BusDevice {
        Tor(K5020& k, bool steuer) : karte(k), steuer(steuer) {}
        uint8_t ioRead(uint8_t port) override;
        void ioWrite(uint8_t port, uint8_t d) override;
        const char* deviceName() const override { return steuer ? "K5020 Steuer-PIO" : "K5020 Daten-PIO"; }
        K5020& karte;
        bool steuer;
    };
    static uint8_t relativ(uint8_t port) { return static_cast<uint8_t>(((port & 1) << 1) | ((port >> 1) & 1)); }

    /// Leitungen eines Geraets, wie die Karte sie aus der Steuer-PIO ableitet.
    struct Leit {
        bool res = false, awa = false, vor = false, rueck = false, hge = false, auf = false, wid = true, ums = false;
        bool operator==(const Leit& o) const {
            return res == o.res && awa == o.awa && vor == o.vor && rueck == o.rueck && hge == o.hge &&
                   auf == o.auf && wid == o.wid && ums == o.ums;
        }
    };
    void leitungen();                 ///< Steuer-PIO-Ausgaenge → Leitungen zu den Geraeten
    bool eingaenge();                 ///< Rueckleitungen → Steuer-PIO-Eingaenge; true bei Aenderung
    void uebernehmen();               ///< Byte aus dem Ausgaberegister ins Schieberegister
    void rsi();
    double byteTakte() const;

    const Config cfg_;
    Z80PIO daten_{"K5020 Daten-PIO"};
    Z80PIO steuer_{"K5020 Steuer-PIO"};
    Tor tor_d_{*this, false};
    Tor tor_s_{*this, true};
    std::unique_ptr<kassette::KassettenLaufwerk> lw_[2];

    Leit leit_[2];
    int sel_ = -1;
    bool rsi_ = false;
    uint64_t zeit_ = 0;
    int rest_ = 0;                    ///< noch nicht an die Laufwerke weitergegebene Takte
    // Ausgabe
    bool tx_busy_ = false;
    double tx_ende_ = 0;              ///< Maschinentakt, an dem das Schieberegister frei wird
    bool zf_ = false;
    bool tx_wartet_ = false;          ///< Byte im Ausgaberegister wartet auf den externen Takt
    std::function<bool()> ext_takt_;
    // Eingabe
    uint64_t verloren_ = 0;
    uint64_t bms_halte_bis_ = 0;
    bool bms_pin_ = false;
    uint8_t a_ext_ = 0xFF, b_ext_ = 0xFF;   ///< zuletzt an die Eingaenge gelegt
    bool ext_neu_ = true;
};
