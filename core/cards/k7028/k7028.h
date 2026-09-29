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
 *   40H–43H (= 44H–47H)  SIO 1   A = IFSS [?], B = Drucker/V.24 des BIOS
 *   48H–4BH (= 4CH–4FH)  CTC 1   K0–K2 Baudtakte SIO 1, K3 Zeitgeber
 *   50H–53H (= 54H–57H)  SIO 2   A = IFSS [?], B = Tastatur K7672
 *   58H–5BH (= 5CH–5FH)  CTC 2   K0 Baudtakt SIO 2A, K2 Baudtakt Tastatur, K3 Zeitgeber
 *   60H–67H [?]          Anzeigelatch (ROM/BIOS schreiben nur 61H; §6.2)
 * @endcode
 *
 * SIO-Adressierung C/D = A0, B/A = A1 — dieselbe Reihenfolge wie `Z80SIO`
 * (A-Daten, A-Steuer, B-Daten, B-Steuer).
 *
 * **Serielle Ebene.**  `Z80SIO` arbeitet byteweise (kein Bitstrom); die Baudtakte
 * aus den CTCs werden deshalb nicht an die SIO geführt, sondern nur gezählt
 * (sie müssen trotzdem laufen: der CTC-Test und spätere Programme lesen sie).
 * Ein gesendetes Byte holt der Empfänger mit `txGet()` ab: die Tastatur (SIO2-B,
 * @ref K7672), eine Rückschleife (@ref Config::rueckschleife) oder ein
 * Abnehmer nach außen (@ref setAbnehmer) — beide Wege bekommen das Byte erst nach
 * einer Zeichenzeit (@ref ZEICHEN_TAKTE), nicht sofort mit `txGet()` (AP-E4c). Ein
 * gesetzter Abnehmer **ersetzt** die Rückschleife nur auf seinem eigenen Kanal
 * (Drucker/DFÜ angeschlossen ⇒ kein Prüfstecker-Echo mehr auf diesem Kanal); die
 * beiden anderen Kanäle bleiben für den ROM-Selbsttest zurückgeschleift, solange
 * `Config::rueckschleife` es für sie vorsieht.
 *
 * **Rückschleife/Prüfstecker.**  Der Selbsttest des ROMs sendet AAH/55H an
 * SIO1-A, SIO1-B und SIO2-A und verlangt dasselbe Byte zurück (§4.3).  Ob das am
 * Gerät ein gesteckter Prüfstecker leistet oder die ATS die IFSS-Schleifen
 * selbst schließt, ist offen (§6.10).  Deshalb ist die Rückschleife je Kanal
 * eine Option; das zurückgeschleifte Byte erscheint nach einer Zeichenzeit.
 *
 * @see doc/design/16_k8915.md §3.2, §4.3, §6.10, §8a AP-E2
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>

class K7028 : public BusDevice, public InterruptSlave {
public:
    /// Serielle Kanäle, die nach außen gehen (SIO2-B gehört der Tastatur).
    enum Kanal : int { Sio1A = 0, Sio1B = 1, Sio2A = 2, KanalAnzahl = 3 };

    /** @brief Brückenstellungen und Außenbeschaltung. */
    struct Config {
        uint8_t io_base    = 0x40;   ///< Brücken X24/X25: Block 40H–5FH
        uint8_t latch_base = 0x60;   ///< Anzeigelatch 60H–67H [?] (§6.2)
        /// Rückschleife je Kanal (Sio1A, Sio1B, Sio2A): gesendete Bytes kommen am
        /// selben Kanal wieder an.  Vorgabe AUS; @ref mitPruefstecker schaltet alle drei.
        std::array<bool, KanalAnzahl> rueckschleife{};

        /// Alle drei Kanäle zurückgeschleift — so besteht der SIO-Test des ROMs.
        static Config mitPruefstecker() {
            Config c;
            c.rueckschleife = {true, true, true};
            return c;
        }
    };

    /**
     * @brief Dauer eines zurückgeschleiften Zeichens in Systemtakten.
     *
     * 9600 Bd bei 2,4576 MHz = 256 Takte je Bit; das ROM stellt 1 Start-, 8 Daten-,
     * 1 Paritäts- und 1 Stoppbit ein (WR4 = 45H) ⇒ 11 Bit.  Fest angenommen — die
     * Karte rechnet die Baudrate nicht aus den CTCs nach.
     */
    static constexpr uint64_t ZEICHEN_TAKTE = 11 * 256;

    K7028();
    explicit K7028(const Config& cfg);

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
    /** @brief /RESET: SIOs, CTCs, Latch; laufende Rückschleifen-Zeichen verworfen. */
    void reset();
    /** @brief Beide CTCs um @p ticks Systemtakte (CLK/TRG der Zeitgeberkanäle = Φ). */
    bool clockTick(int ticks);
    /**
     * @brief Serielle Außenwelt bedienen (je Befehl aus der Laufschleife):
     *        Sendebytes der Kanäle Sio1A/Sio1B/Sio2A abholen und zurückschleifen
     *        bzw. an den Abnehmer geben; fällige Rückschleifen-Zeichen zustellen.
     * @return true, wenn sich der Zustand einer SIO geändert hat (Kette neu bewerten).
     */
    bool service(uint64_t now_cycles);

    /** @brief Abnehmer für gesendete Bytes eines nicht zurückgeschleiften Kanals. */
    using Abnehmer = std::function<void(uint8_t)>;
    void setAbnehmer(Kanal k, Abnehmer cb) { abnehmer_[k] = std::move(cb); }
    /** @brief Ein Byte von außen in den Empfänger eines Kanals legen. */
    void empfange(Kanal k, uint8_t byte);

    // ─── Bausteine / Zustand ─────────────────────────────────────────────────
    Z80SIO& sio1() { return sio1_; }
    Z80SIO& sio2() { return sio2_; }
    Z80CTC& ctc1() { return ctc1_; }
    Z80CTC& ctc2() { return ctc2_; }
    /** @brief Zuletzt ins Anzeigelatch geschriebener Wert (61H; aktiv L, §4.4). */
    uint8_t anzeige() const { return latch_; }
    const Config& config() const { return cfg_; }
    void setRueckschleife(Kanal k, bool an) { cfg_.rueckschleife[k] = an; }

private:
    void updateInternalChain();
    Z80SIO::Channel& kanal(Kanal k);

    Config cfg_;
    Z80SIO sio1_{"ATS-SIO1"};
    Z80SIO sio2_{"ATS-SIO2"};
    Z80CTC ctc1_{"ATS-CTC1"};
    Z80CTC ctc2_{"ATS-CTC2"};
    uint8_t latch_  = 0xFF;
    bool    iei_in_ = false;

    /// @p nachAussen: an den Abnehmer (setAbnehmer) statt in den eigenen Empfänger
    /// zurückgeschleift — beide teilen sich dieselbe Zeichenzeit-Pacing (Leitung).
    struct Unterwegs { uint64_t faellig; uint8_t byte; bool nachAussen; };
    std::array<std::deque<Unterwegs>, KanalAnzahl> schleife_{};
    std::array<uint64_t, KanalAnzahl>              frei_ab_{};   ///< Leitung frei ab Takt
    std::array<Abnehmer, KanalAnzahl>              abnehmer_{};
};
