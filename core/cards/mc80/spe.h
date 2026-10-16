/**
 * @file spe.h
 * @brief Speichereinheit SPE 2 (50300.3520) des MC80.3x und das Segmentfenster 4000H–BFFFH.
 *
 * Quellen: doc/mc80/hardware_3x.md §3.1/§3.2/§4 (AP M0), doc/mc80/egos.md §1 Punkte 3–5 (AP M1).
 *
 * **SPE 2** = 32 KB dRAM + bis 8 KB EPROM (8 Fassungen à 1 KB, 2708).  Auslieferung: ROM
 * 2000H–3FFFH, RAM 4000H–BFFFH.  Am MC80.30/.31 traegt die erste SPE 2 das ROM
 * `spe_1`/`spe_2` (2000H–27FFH: Sprungleiste, Segmentumschaltung, Relativlader,
 * Geraeteverwaltung).  Drei Festlegungen:
 *  - **Leere Fassungen lesen 00H** (2800H–3FFFH ohne Erweiterungs-EPROM).  Nur so laeuft die
 *    ROM-Kette von EGOS ueber 2048 leere Kennbyte-00-Objekte bis 4000H weiter und findet
 *    Programme im RAM; mit FFH endete sie bei 2800H (egos.md §1.4, wie MAME).  Am Geraet
 *    unbelegt [?].
 *  - **Der ROM-Block haengt NICHT am Segmentregister** (egos.md §1.5: Code aus dem SPE-2-ROM
 *    schaltet das Segment um und laeuft weiter); nur der RAM-Block 4000H–BFFFH ist das
 *    Segment.  RAM freigegeben ⇔ das Segmentbit der Karte (@ref Config::segment_bit) ist
 *    im Register (ZVE-PIO B, 85H) gesetzt.
 *  - **Nach /RESET ist alles freigegeben** (Auslieferung „ohne Anfangsinitialisierung“,
 *    egos.md §1.5).  Die 1-KB-Sperren ueber B0H (ROM sperren), B1H (ROM frei), B2H (RAM
 *    sperren), B3H (RAM frei) sind nachgebildet: A0 = 1 Freigabe, A10–A15 der
 *    E/A-Adresse = 1-KB-Block, Daten belanglos [SPE2 S.2–6].  EGOS benutzt sie nie.
 *    Gesperrter ROM-Block: die Karte treibt den Bus nicht (FFH).  Gesperrter RAM-Block
 *    je nach Wickel X5209 (@ref Config::sperrart): Schreibschutz (Vorgabe [?]) oder
 *    Zugriffssperre.
 *
 * Nicht nachgebildet: Wahl der Anfangsadressen per DIL (feste Auslieferungslage),
 * Vorrangsperre des ROM bei Ueberlappung, /MEMDI-Eingaenge vom Bus, SPE 1 und SPE 4.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <bitset>
#include <cstdint>
#include <vector>

/**
 * @brief Teilnehmer am Segmentfenster 4000H–BFFFH (RAM-Karte SPE 2 oder EPROM-Segment).
 *
 * Das Segmentfenster fragt jede Karte, ob sie bei Adresse @p a den Bus treibt; die erste,
 * die es tut, liefert das Byte.  Schreiben geht an alle (jede entscheidet selbst).
 */
class Mc80SegmentKarte {
public:
    virtual ~Mc80SegmentKarte() = default;
    virtual bool    ramLiest(uint16_t a) const = 0;
    virtual uint8_t ramLesen(uint16_t a) const = 0;
    virtual void    ramSchreiben(uint16_t a, uint8_t d) = 0;
    /// Segmentregister der ZVE (85H) hat sich geaendert.
    virtual void    setSegmentRegister(uint8_t maske) = 0;
};

class Mc80Spe2 : public BusDevice, public MemDevice, public Mc80SegmentKarte {
public:
    static constexpr uint16_t ROM_BASIS   = 0x2000;
    static constexpr uint16_t ROM_GROESSE = 0x2000;   ///< 8 Fassungen 2708
    static constexpr uint16_t RAM_BASIS   = 0x4000;
    static constexpr uint32_t RAM_GROESSE = 0x8000;   ///< 32 KB = ein Segment
    static constexpr uint8_t  LEER        = 0x00;     ///< Lesewert unbestueckter Fassungen

    enum class Sperrart : uint8_t { Schreibschutz, Zugriff };

    struct Config {
        /// ROM-Inhalt ab 2000H; nullptr = keine Fassung bestueckt (ROM-Block liest LEER).
        const uint8_t* rom = nullptr;
        uint16_t rom_len = 0;
        /// ROM-Block am Bus anmelden (nur die erste SPE 2 eines Geraets; Lage fest 2000H).
        bool rom_block = true;
        /// Bit im Segmentregister, das den RAM-Block freigibt (01H = Segment 1).
        uint8_t segment_bit = 0x01;
        /// Sperr-/Freigabetore (B0H–B3H); 0 = keine anmelden (zweite Karte: Wickel [?]).
        uint8_t io_base = 0xB0;
        Sperrart sperrart = Sperrart::Schreibschutz;

        /// Erste SPE 2 des MC80.30/.31: ROM spe_1/spe_2, Segment 1, Tore B0H–B3H.
        static Config mc8030();
        /// Weitere SPE 2 nur mit RAM (Segment @p bit), ohne ROM-Block und ohne Tore [?].
        static Config nurRam(uint8_t bit);
    };

    Mc80Spe2() : Mc80Spe2(Config{}) {}
    explicit Mc80Spe2(const Config& cfg);

    // ─── BusDevice: B0H–B3H ──────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t) override { return 0xFF; }
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80 SPE 2"; }
    /// Volle E/A-Adresse fuer die Blockwahl (A10–A15); ohne Bus: Port mit B = 0.
    void setBus(K1520Bus* bus) { bus_ = bus; }

    // ─── MemDevice: nur der ROM-Block 2000H–3FFFH ────────────────────────────
    uint8_t memRead(uint16_t a) override;
    void    memWrite(uint16_t, uint8_t) override {}

    // ─── RAM-Block (ueber das Segmentfenster) ─────────────────────────────────
    /// Treibt die Karte bei einem Lesezugriff auf @p a im RAM-Block den Bus?
    bool    ramLiest(uint16_t a) const override;
    uint8_t ramLesen(uint16_t a) const override { return ram_[(a - RAM_BASIS) & (RAM_GROESSE - 1)]; }
    /// Schreibzugriff auf @p a (wirkungslos bei abgeschaltetem Segment bzw. Sperre).
    void    ramSchreiben(uint16_t a, uint8_t d) override;

    void attachToBus(K1520Bus& bus);
    void reset();                        ///< alle Bloecke frei; RAM bleibt
    void powerOn(uint8_t fill = 0x00);   ///< RAM vorbelegen, dann reset()

    /// Segmentregister der ZVE (85H): RAM-Block freigegeben ⇔ segment_bit gesetzt.
    void setSegmentRegister(uint8_t maske) override { ram_frei_ = (maske & cfg_.segment_bit) != 0; }
    bool segmentAktiv() const { return ram_frei_; }

    bool romBlockGesperrt(int block) const { return rom_sperre_.test(block & 63); }
    bool ramBlockGesperrt(int block) const { return ram_sperre_.test(block & 63); }
    const Config& config() const { return cfg_; }
    uint8_t ramPeek(uint16_t a) const { return ramLesen(a); }
    void    ramPoke(uint16_t a, uint8_t v) { ram_[(a - RAM_BASIS) & (RAM_GROESSE - 1)] = v; }

private:
    Config cfg_;
    K1520Bus* bus_ = nullptr;
    std::vector<uint8_t> ram_;
    std::bitset<64> rom_sperre_;   ///< je 1-KB-Block der CPU-Adresse (A10–A15)
    std::bitset<64> ram_sperre_;
    bool ram_frei_ = false;
};

/**
 * @brief EPROM-Segment (MC80.31E: Segment 10H und 20H): nur lesbar, 4-KB-Abzuege im Fenster.
 *
 * Quelle: doc/EPROMS/MC80/README.md (`segmente/`), doc/design/31_mc80.md §4.  Das Segment ist
 * freigegeben ⇔ sein Bit im Segmentregister (85H) gesetzt ist.  Schreibzugriffe sind
 * wirkungslos (EPROM).  Was kein Abzug abdeckt, treibt den Bus nicht (Fenster liefert FFH).
 * Die Lage der Abzuege im Fenster steht beim Anlegen (@ref abzug), nicht im Baustein.
 */
class Mc80EpromSegment : public Mc80SegmentKarte {
public:
    explicit Mc80EpromSegment(uint8_t segment_bit) : bit_(segment_bit) {}
    /// Abzug @p daten (@p laenge Byte) ab Fensteradresse @p adresse einsetzen (4000H–BFFFH).
    void abzug(uint16_t adresse, const uint8_t* daten, uint16_t laenge);

    bool    ramLiest(uint16_t a) const override;
    uint8_t ramLesen(uint16_t a) const override;
    void    ramSchreiben(uint16_t, uint8_t) override {}
    void    setSegmentRegister(uint8_t maske) override { frei_ = (maske & bit_) != 0; }
    uint8_t segmentBit() const { return bit_; }
    bool    segmentAktiv() const { return frei_; }

private:
    struct Abzug { uint16_t adresse; const uint8_t* daten; uint16_t laenge; };
    const Abzug* suche(uint16_t a) const;
    uint8_t bit_;
    bool frei_ = false;
    std::vector<Abzug> abzuege_;
};

/**
 * @brief Das Fenster 4000H–BFFFH: fragt die RAM-Bloecke aller SPE-Karten.
 *
 * Lesen: die erste Karte, die den Bus treibt; keine → FFH (offener Bus, wie MAME
 * `unmap_value_high`).  Schreiben: an jede Karte (jede entscheidet selbst).  Ein eigener
 * Baustein, weil der Bus je Adresse nur EINEN Lesenden kennt, das Segmentregister aber
 * zur Laufzeit waehlt.
 */
class Mc80Segmentfenster : public MemDevice {
public:
    void anmelden(Mc80SegmentKarte* karte) { karten_.push_back(karte); }
    void attachToBus(K1520Bus& bus) { bus.registerMem(this, Mc80Spe2::RAM_BASIS, Mc80Spe2::RAM_GROESSE); }
    uint8_t memRead(uint16_t a) override;
    void    memWrite(uint16_t a, uint8_t d) override;

private:
    std::vector<Mc80SegmentKarte*> karten_;
};
