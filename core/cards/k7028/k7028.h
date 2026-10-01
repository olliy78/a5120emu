/**
 * @file k7028.h
 * @brief ATS K7028.30 (045-8732) — Anschlusssteuerung Tastatur/IFSS/V.24 des K8915.
 *
 * Stromlaufplan 1.45.518732.4/04 (doc/design/16_k8915.md §3.2).  Bestückung:
 * 2 × UA856 (SIO), 2 × UB857 (CTC), IFSS-Analogteil.  Der Adressdekoder D3:02
 * (DS8205) legt AB3–AB5 an A0–A2 und AB6/AB7 über Brücken an die Freigaben —
 * **A2 wird nicht dekodiert**, jede Vierergruppe erscheint doppelt:
 *
 * @code
 *   40H–43H (= 44H–47H)  SIO 1   A = V.24 (X4), B = Drucker/IFSS1 (X3, Drucker des BIOS)
 *   48H–4BH (= 4CH–4FH)  CTC 1   K0/K1 → SIO1-A (Multiplexer D13), K2 → SIO1-B, K3 Zeitgeber
 *   50H–53H (= 54H–57H)  SIO 2   A = DFÜ/IFSS2 (X5, IFSS), B = Tastatur K7672
 *   58H–5BH (= 5CH–5FH)  CTC 2   K0 Baudtakt SIO 2A, K2 Baudtakt Tastatur, K3 Zeitgeber
 *   60H–67H [?]          Anzeigelatch (ROM/BIOS schreiben nur 61H; §6.2)
 * @endcode
 *
 * SIO-Adressierung C/D = A0, B/A = A1 — dieselbe Reihenfolge wie `Z80SIO`
 * (A-Daten, A-Steuer, B-Daten, B-Steuer).
 *
 * **Serielle Ebene (AP-S5, Entwurf 19 §3.2).**  `Z80SIO` arbeitet byteweise; nach
 * außen geht jede der drei Schnittstellen über einen `SerialAnschluss`
 * (@ref anschluss), den die Maschine im `SerialHub` anmeldet.  Der Wandler taktet
 * jedes Zeichen nach dem Format, das der Gast in SIO und CTC programmiert hat.
 * Belegung laut Stromlaufplan 1.45.518732 Blatt 1/2 (`k8915schaltung.pdf` S. 11/16)
 * und BIOS (Befund AP-S5); Namen und Stecker nach der Beschriftung am Gerät
 * (Anwender, AP-S12 — der Plan zählt die Kartenstecker X3/X4 umgekehrt, s. k7028.cpp
 * und §3.2 des K8915-Entwurfs).  Reihenfolge = Stecker = C-ABI-Index:
 *
 * @code
 *   Drucker/IFSS1  SIO1-B  X3  nur TxD/RxD — BIOS-Drucker LIST (XON/XOFF)            CTC1 K2
 *   V.24           SIO1-A  X4  V.24 mit Steuerleitungen (Treiber D14, Empfänger D17)  CTC1 K0
 *                              /CTSA = V107 ∧ (¬RTSA ∨ V106), /DCDA = V109 (§14.5)
 *   DFÜ/IFSS2      SIO2-A  X5  IFSS-Stromschleife (Blatt 2) bzw. TxD/RxD              CTC2 K0
 *   —              SIO2-B  —   Tastatur K7672 (fest, nicht einstellbar; Karten-X6)   CTC2 K2
 * @endcode
 *
 * **Prüfstecker.**  Der Selbsttest des ROMs sendet AAH/55H an SIO1-A, SIO1-B und
 * SIO2-A und verlangt dasselbe Byte zurück (§4.3).  Die frühere Rückschleife der
 * Karte (`Config::rueckschleife`, `mitPruefstecker`, `service()`) ist seit AP-S5 die
 * Einstellung **Rx/Tx-Loop** des Wandlers (Entwurf 19 §6.5); die Maschinenvorgabe
 * `K8915Machine::Config::pruefstecker` setzt sie für alle drei.
 *
 * @see doc/design/16_k8915.md §3.2, §4.3, §6.10, §8a AP-E2
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

class K7028 : public BusDevice, public InterruptSlave {
public:
    /// Serielle Kanäle, die nach außen gehen (SIO2-B gehört der Tastatur).  Der Wert
    /// ist zugleich die Reihenfolge der Anmeldung im `SerialHub` (= Index der C-ABI)
    /// und folgt den Steckern am Gerät: X3 (SIO1-B), X4 (SIO1-A), X5 (SIO2-A).
    enum Kanal : int { Sio1B = 0, Sio1A = 1, Sio2A = 2, KanalAnzahl = 3 };

    /** @brief Brückenstellungen. */
    struct Config {
        uint8_t io_base    = 0x40;   ///< Brücken X24/X25: Block 40H–5FH
        uint8_t latch_base = 0x60;   ///< Anzeigelatch 60H–67H [?] (§6.2)
    };

    /// Fester Name der Tastaturschnittstelle (nicht einstellbar, Leitsatz 7).
    static constexpr const char* TASTATUR_NAME = "Tastatur K7672";

    K7028();
    explicit K7028(const Config& cfg);
    ~K7028() override;

    /** @brief Ports anmelden: 32 ab io_base, 8 ab latch_base. */
    void attachToBus(K1520Bus& bus);

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "ATS K7028.30"; }

    // ─── InterruptSlave: SIO 1 → SIO 2 → CTC 1 → CTC 2 [?] ──────────────────
    void        setIEI(bool iei) override;
    bool        getIEO() const override;
    bool        hasInterrupt() const override;
    uint8_t     getVector() const override;
    void        onRETI() override;
    const char* intDeviceName() const override;

    // ─── Lebenslauf / Takt ───────────────────────────────────────────────────
    /** @brief /RESET: SIOs, CTCs, Latch. */
    void reset();
    /** @brief Beide CTCs um @p ticks Systemtakte (CLK/TRG der Zeitgeberkanäle = Φ). */
    bool clockTick(int ticks);

    // ─── Schnittstellen nach außen (Entwurf 19 §3.2, AP-S5) ─────────────────
    /// Anschluss je Kanal (lebt so lange wie die Karte).
    k1520::serial::SerialAnschluss& anschluss(Kanal k);
    /// Ein Anschluss hat SIO-Zustand geändert — Interruptkette neu bewerten.
    /// Liest und löscht.
    bool nimmSeriellGeaendert() { const bool g = seriell_geaendert_; seriell_geaendert_ = false; return g; }

    /// Alter Unterbau (Tests, `K1520Machine::setPrinterCallback`/`setDFUECallback`):
    /// Abnehmer der Bytes, die der Gast an Kanal @p k SENDET — in ihrer Zeichenzeit,
    /// aber nur, solange weder Transport noch Loop den Stecker belegen (Entwurf 19 §8).
    using Abnehmer = std::function<void(uint8_t)>;
    void setAbnehmer(Kanal k, Abnehmer cb);
    /// Ein Byte von außen in den Empfänger eines Kanals (alter Unterbau); belegt ein
    /// Transport/Loop den Stecker, geht es ins Leere.
    void empfange(Kanal k, uint8_t byte);

    // ─── Bausteine / Zustand ─────────────────────────────────────────────────
    Z80SIO& sio1() { return sio1_; }
    Z80SIO& sio2() { return sio2_; }
    Z80CTC& ctc1() { return ctc1_; }
    Z80CTC& ctc2() { return ctc2_; }
    /** @brief Zuletzt ins Anzeigelatch geschriebener Wert (61H; aktiv L, §4.4). */
    uint8_t anzeige() const { return latch_; }
    const Config& config() const { return cfg_; }

private:
    class Anschluss;   // k7028.cpp
    friend class Anschluss;

    void updateInternalChain();
    Z80SIO::Channel& kanal(Kanal k);
    /// /CTSA = V107 ∧ (¬RTSA ∨ V106) (Stromlaufplan D3:02/D9, Entwurf 19 §14.5).
    void bildeCtsA();

    Config cfg_;
    Z80SIO sio1_{"ATS-SIO1"};
    Z80SIO sio2_{"ATS-SIO2"};
    Z80CTC ctc1_{"ATS-CTC1"};
    Z80CTC ctc2_{"ATS-CTC2"};
    uint8_t latch_  = 0xFF;
    bool    iei_in_ = false;
    bool    seriell_geaendert_ = false;
    bool    v106_ = false;   ///< V.24-Eingang am Stecker (SIO1-A): Sendebereitschaft
    bool    v107_ = false;   ///< V.24-Eingang am Stecker (SIO1-A): Betriebsbereitschaft (DSR)
    std::array<std::unique_ptr<Anschluss>, KanalAnzahl> anschluesse_;
};
