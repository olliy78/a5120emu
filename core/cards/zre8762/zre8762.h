/**
 * @file zre8762.h
 * @brief ZRE 045-8762 „ZRE für K8G" — CPU-Karte des K8915 V3 (U880, CTC, 4-KB-Boot-EPROM,
 *        2 × 64 KB DRAM, Speichersteuerregister A8H).
 *
 * Stromlaufplan 1.45.518762.1/04, drei Blätter (doc/design/16_k8915.md §3.1, §4.2a):
 *
 * @code
 *   CPU      U880 (D34), 2,4576 MHz, EINE CPU (keine ZVE2 wie beim K2526)
 *   CTC      D36, Ports 80H–83H (E/A-Dekoder D41, Ø0)
 *   PIO      D35 — weder ROM noch BIOS sprechen sie an, NICHT nachgebildet (§6.3)
 *   EPROM    2732 (D40), 0000H–0FFFH, sofern die A8H-Logik es wählt
 *   DRAM     Bank 1 (/CAS1) identisch abgebildet, Bank 2 (/CAS2) nur als Fenster 4000H–7FFFH
 *   A8H      D20 (DS8212), /RESET ⇒ 00H; A0/A1 nicht dekodiert ⇒ A8H–ABH
 * @endcode
 *
 * **Speicherpfad.**  Die Karte meldet ihren Speicher NICHT über
 * K1520Bus::registerMem an: bei A8H = 87H überdeckt ihr RAM die K7024 bei 1000H,
 * und auf der Karte ist das auch keine Überlagerung, sondern eine Umschaltung —
 * D12 („Speicher auf der ZRE gewählt") schaltet den Datentreiber D28 zwischen
 * ZRE-Speicher und Systembus.  Deshalb fragen die Speicher-Rückrufe der CPU
 * **zuerst** die A8H-Abbildung und geben nur nicht gewählte Adressen an den Bus
 * weiter (wie beim K2526 der Weg über MemIOProtect).  Nachgeschlagen wird in
 * 4-KB-Schritten (16 Einträge), neu berechnet bei jedem `OUT (A8H)`.
 *
 * **Annahme:** ein Zugriff, den die ZRE selbst bedient, erscheint NICHT auf dem
 * Systembus (auch kein Schreibzyklus).  Sonst schriebe ein Programm, das bei 87H
 * im TPA 1000H–17FFH arbeitet, zugleich ins Bild der K7024 — die wertet `/MEMDI`
 * nicht aus (§4.2a „Folgen" 2).
 *
 * **Brückenfeld X8–X27** zwischen D20 und der Logik ist die Konfigurationsstruktur
 * @ref K8915Zre::Brueckenfeld; Vorgabe = das am Gerät des Anwenders abgelesene Feld.
 *
 * @see doc/design/16_k8915.md §3.1, §4.2a
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include <array>
#include <cstdint>

class K8915Zre : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Brückenfeld X8–X27 (Blatt 3): welcher Registerausgang welchen Logikeingang treibt.
     *
     * Die linke Klemmenreihe (ungerade Nummern) trägt die Ausgänge von D20, die
     * rechte (gerade) führt in die Logik.  Jedes Feld unten ist ein Logikeingang
     * und enthält die Nummer der linken Klemme, von der sein Draht kommt, oder 0
     * für „offen".  Linke Klemmen (§4.2a):
     *
     * @code
     *   X9  /DO8 = /Bit7   X11 /DO1 = /Bit0   X13 Bit7   X15 Bit0   X17 Bit6
     *   X19 Bit1           X21 Bit5           X23 Bit2   X25 Bit4   X27 Bit3
     * @endcode
     *
     * Ein offener Logikeingang liest wie ein offener TTL-Eingang als H.
     */
    struct Brueckenfeld {
        uint8_t x8_memdi        = 9;   ///< /MEMDI (X1:B21), aktiv L
        uint8_t x10_rom         = 11;  ///< ROM-Freigabe (D9), aktiv H
        uint8_t x12_memdi1      = 27;  ///< /MEMDI1 (X2:A11), aktiv L — am Gerät gekreuzt von X27
        uint8_t x14_seite0      = 15;  ///< Seite 0 aus Bank 1 (57), aktiv H
        uint8_t x16_bank2       = 17;  ///< Bank 2 ins Fenster 4000H (63), aktiv H
        uint8_t x18_seite1      = 19;  ///< Seite 1 aus Bank 1 (58), aktiv H
        uint8_t x20_bank2_a15   = 21;  ///< Bank-2-Adresse A15 (62)
        uint8_t x22_seite2      = 23;  ///< Seite 2 aus Bank 1 (59), aktiv H
        uint8_t x24_bank2_a14   = 25;  ///< Bank-2-Adresse A14 (61)
        uint8_t x26_seite3      = 23;  ///< Seite 3 aus Bank 1 (60) — am Gerät Doppeldraht von X23

        /// Das Feld am Gerät des Anwenders (abgelesen 2026-09-28, §4.2a).
        static Brueckenfeld geraetAnwender() { return Brueckenfeld{}; }
    };

    /** @brief Brückenstellungen der Karte (Vorgabe = Gerät des Anwenders). */
    struct Config {
        uint8_t      ctc_base  = 0x80;  ///< CTC D36 (Ø0 des D41, Brücke X70/X71)
        uint8_t      reg_base  = 0xA8;  ///< Register D20 (Brücken X31–X38/X47–X54), belegt 4 Ports
        Brueckenfeld feld{};
    };

    /** @brief Was an einer Adresse im Speicherbild der CPU liegt (für Diagnose und Wächter). */
    enum class Quelle : uint8_t { Rom, Bank1, Bank2, Bus };
    struct Ort {
        Quelle   quelle;
        uint32_t offset;   ///< Adresse innerhalb von ROM/Bank (bei Bus: die Busadresse)
    };

    explicit K8915Zre(K1520Bus& bus);
    K8915Zre(K1520Bus& bus, const Config& cfg);

    // ─── BusDevice (CTC 80H–83H, Register A8H–ABH) ───────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "ZRE 045-8762"; }

    // ─── InterruptSlave (die CTC ist der einzige Interruptbaustein der Karte) ─
    void        setIEI(bool iei) override      { ctc_.setIEI(iei); }
    bool        getIEO() const override        { return ctc_.getIEO(); }
    bool        hasInterrupt() const override  { return ctc_.hasInterrupt(); }
    uint8_t     getVector() const override     { return ctc_.getVector(); }
    void        onRETI() override              { ctc_.onRETI(); }
    const char* intDeviceName() const override { return "ZRE-CTC"; }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief I/O-Ports anmelden (CTC, Register).  Speicher wird NICHT angemeldet (s. o.). */
    void attachToBus(K1520Bus& bus);
    /** @brief /RESET: A8H := 00H (CLR des DS8212), CTC zurück, CPU nach 0000H. RAM bleibt. */
    void reset();
    /** @brief Netz-Ein: DRAM mit @p fill vorbelegen, dann reset(). */
    void powerOn(uint8_t fill = 0x00);
    /** @brief CTC um @p ticks Systemtakte weiterzählen; true bei ZC/TO-Flanke. */
    bool clockTick(int ticks) { return ctc_.clockTick(ticks); }

    // ─── CPU ─────────────────────────────────────────────────────────────────
    Z80&       cpu()       { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    Z80CTC&    ctc()       { return ctc_; }

    // ─── Register A8H und Speicherbild ───────────────────────────────────────
    uint8_t reg() const { return reg_; }
    /** @brief Register setzen wie ein `OUT (A8H)` (Abbildung + /MEMDI werden nachgeführt). */
    void    setReg(uint8_t v);
    /** @brief /MEMDI (X1:B21) aktiv? */
    bool    memdi()  const { return memdi_; }
    /** @brief /MEMDI1 (X2:A11) aktiv?  Keine Karte der V3 wertet es aus (§4.2a). */
    bool    memdi1() const { return memdi1_; }

    /** @brief Was liegt unter @p addr im aktuellen Speicherbild? */
    Ort     ortVon(uint16_t addr) const;

    /** @brief Speicherzugriff aus Sicht der CPU (ZRE-Abbildung, sonst Bus). */
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);

    /** @brief Direkter Blick in die DRAM-Bänke (bank 0 = Bank 1, 1 = Bank 2). */
    uint8_t bankPeek(int bank, uint16_t addr) const { return ram_[bank & 1][addr]; }
    void    bankPoke(int bank, uint16_t addr, uint8_t v) { ram_[bank & 1][addr] = v; }

    /**
     * @brief Beobachter für Speicherzugriffe, die die ZRE SELBST bedient (ROM, Bank 1,
     *        Bank 2) — Signatur wie K1520Bus::BusTrace, `isIO` ist immer false.
     *
     * Der Busbeobachter (K1520Bus::setTraceCallback) sieht diese Zugriffe nicht, weil
     * sie nicht auf den Systembus gehen (s. o. „Speicherpfad“).  Werkzeuge
     * (k1520dbg-Watchpoints, boot_trace `--watch`) hängen sich deshalb an beide;
     * K8915Machine::setBusTrace erledigt das.  Leer = aus (Vorgabe, kostet einen Test
     * je Zugriff).  §8a AP-E4d.
     */
    void setMemTrace(K1520Bus::BusTrace cb) { mem_trace_ = std::move(cb); }

private:
    /// Pegel einer linken Klemme für den Registerwert @p r (offen ⇒ H).
    static bool klemme(uint8_t nr, uint8_t r);
    void rebuildMap();

    K1520Bus& bus_;
    Config    cfg_;

    Z80    cpu_;
    Z80CTC ctc_{"ZRE-8762-CTC"};

    std::array<std::array<uint8_t, 0x10000>, 2> ram_{};   ///< [0] = Bank 1, [1] = Bank 2

    uint8_t reg_    = 0x00;
    bool    memdi_  = false;
    bool    memdi1_ = false;

    /// Abbildung in 4-KB-Schritten: Quelle + Basis innerhalb der Quelle.
    struct Slot { Quelle quelle; uint32_t basis; };
    std::array<Slot, 16> map_{};

    K1520Bus::BusTrace mem_trace_;   ///< s. setMemTrace()
};
