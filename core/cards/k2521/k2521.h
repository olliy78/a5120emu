/**
 * @file k2521.h
 * @brief ZRE K2521 (012-7105) — CPU-Karte des PRG 710 / PRG 710-1: U880 2,4576 MHz,
 *        ROM bis 3 × U555, 1 KB RAM, CTC 80H–83H, PIO 84H–87H.
 *
 * Quelle: K2521_Beschreibung.pdf, doc/design/20_prg710.md §3.1, §4, §4a, §7.2.
 *
 * @code
 *   CPU   U880 (Z80), 2,4576 MHz (Quarz 9,8304 MHz : 4); EINE CPU, keine ZVE2
 *   ROM   0000H–0BFFH, bis zu 3 × U555 (je 1 KB); im PRG 710 nur EIN Baustein (0000–03FF)
 *   RAM   0C00H–0FFFH, 8 × U202 (1 KB)
 *   CTC   80H–83H, PIO 84H–87H; Interruptkette auf der Karte: CTC vor PIO
 * @endcode
 *
 * **Die Karte entscheidet nicht selbst, ob sie antwortet.**  Auf der echten Karte
 * antwortet der Speicher bei AB12–AB15 = 0, /RFSH high und /MEMDI high (Wickelbrücke
 * X8–X9); im PRG 710 erzeugt die Speicherverwaltung E8H–EBH die Seitenumschaltung
 * (§4b, AP-P1b).  Deshalb meldet die Karte ihren Speicher NICHT über
 * K1520Bus::registerMem an, sondern bietet @ref belegt / @ref memRead / @ref memWrite
 * für 0000H–0FFFH an; wer sie einblendet, entscheidet die Speicherverwaltung.
 * Die CPU der Karte greift über einen von außen gesetzten Speicherweg
 * (@ref setSpeicherweg) zu, Vorgabe = Systembus.
 *
 * **E/A mit voller 16-Bit-Adresse** (`bus.ioRead(p)`, nicht `p & 0xFF` wie am K8915):
 * die Speicherverwaltung braucht AB12–AB15 (K1520Bus::ioAddress()).
 *
 * **Brückenfelder** als Config-Struct: X10–X11 (CTC-Kaskade), X14–X15 (IEI).
 * X6–X7 (Takt TAKTO auf den Koppelbus trennbar) und X8–X9 (welches /MEMDI) wirken
 * nur im Zusammenspiel mit der Maschine bzw. der Speicherverwaltung und stehen hier
 * nur als Kommentar.
 *
 * **PIO-Adressierung [?]:** die Kartenbeschreibung nennt „A = 84H, B = 85H, Steuer
 * 86H/87H" (B/A = AB0, C/D = AB1).  Der Baustein @ref Z80PIO ist wie am K2526 mit
 * 0 = A-Daten, 1 = A-Steuer, 2 = B-Daten, 3 = B-Steuer nummeriert; die Karte
 * übersetzt die Busadresse entsprechend (Config::pio_ab_an_a0).  Das ROM spricht die
 * PIO nicht an; **belegt durch `PROG`** (EPROMmer, AP-P7a): Steuerwort `FFH, 00H` an
 * 86H, danach `IN/OUT (84H)` Bit 0 = PROM-Typ (doc/prg710/eprommer.md §1).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <array>
#include <cstdint>
#include <functional>

class K2521 : public BusDevice, public InterruptSlave {
public:
    /** @brief Woher der IEI der Karte kommt (Brücken X14–X15). */
    enum class IeiQuelle : uint8_t {
        HoechstePrioritaet,   ///< /IODI: Karte steht an der Spitze der Kette (PRG: so)
        System,               ///< IEI vom Systembus (Kette von der vorigen Karte)
        // Sonderfall MRES (dritte Stellung) wird wie System behandelt [?]
    };

    /** @brief Brückenstellungen der Karte (Vorgabe = PRG 710). */
    struct Config {
        const uint8_t* rom     = nullptr;   ///< Inhalt der U555 ab 0000H (nullptr = keine bestückt)
        size_t         rom_len = 0;         ///< Länge in Byte, höchstens 0x0C00
        uint8_t        ctc_base = 0x80;     ///< CTC 80H–83H
        uint8_t        pio_base = 0x84;     ///< PIO 84H–87H
        bool kaskade_to0_clk1 = false;      ///< X10–X11: ZC/TO0 → CLK/TRG1
        bool kaskade_to1_clk2 = false;      ///< X10–X11: ZC/TO1 → CLK/TRG2
        bool kaskade_to2_clk3 = true;       ///< X10–X11: ZC/TO2 → CLK/TRG3 (PRG: gesteckt [ROM])
        IeiQuelle iei_quelle  = IeiQuelle::HoechstePrioritaet;
        bool pio_ab_an_a0     = true;       ///< PIO: B/A = AB0, C/D = AB1 (s. Kopf; belegt durch PROG)

        /// ROM PRG 710 (1 KB, 0000–03FF), Kaskade TO2→CLK3, IEI höchste Priorität.
        static Config prg710();
        /// ROM PRG 710-1 (1 KB); Brücken wie PRG 710.
        static Config prg710_1();
    };

    explicit K2521(K1520Bus& bus);
    K2521(K1520Bus& bus, const Config& cfg);

    // ─── BusDevice (CTC 80H–83H, PIO 84H–87H) ────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "ZRE K2521"; }

    // ─── InterruptSlave: IEI → CTC → PIO → IEO ───────────────────────────────
    void        setIEI(bool iei) override;
    bool        getIEO() const override        { return ctc_.getIEO() && pio_.getIEO(); }
    bool        hasInterrupt() const override  { return ctc_.hasInterrupt() || pio_.hasInterrupt(); }
    uint8_t     getVector() const override;
    void        onRETI() override;
    const char* intDeviceName() const override;

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief I/O-Ports anmelden (CTC, PIO).  Speicher wird NICHT angemeldet (s. o.). */
    void attachToBus(K1520Bus& bus);
    /** @brief /RESET: CTC, PIO und CPU zurück; RAM bleibt. */
    void reset();
    /** @brief Netz-Ein: RAM mit @p fill vorbelegen, dann reset(). */
    void powerOn(uint8_t fill = 0x00);
    /** @brief CTC um @p ticks Systemtakte weiterzählen; true bei ZC/TO-Flanke. */
    bool clockTick(int ticks) { return ctc_.clockTick(ticks); }

    // ─── CPU ─────────────────────────────────────────────────────────────────
    Z80&       cpu()       { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    Z80CTC&    ctc()       { return ctc_; }
    Z80PIO&    pio()       { return pio_; }

    /** @brief Speicherweg der CPU (z. B. über die Speicherverwaltung).  Leere Funktion = Systembus. */
    using LeseFn   = std::function<uint8_t(uint16_t)>;
    using SchreibFn = std::function<void(uint16_t, uint8_t)>;
    void setSpeicherweg(LeseFn lesen, SchreibFn schreiben);

    /**
     * @brief ZC/TO-Ausgänge 0–2 nach außen (Koppelbus, X2); die Kaskade X10–X11 wird
     *        zusätzlich intern bedient.
     */
    void setZCTOCallback(Z80CTC::ZCTOCallback cb) { zcto_ext_ = std::move(cb); }

    // ─── Speicher der Karte (0000H–0FFFH) ────────────────────────────────────
    /** @brief Liegt @p a im Adressbereich der Karte (0000H–0FFFH)?  Nicht: ob sie antwortet. */
    bool    belegt(uint16_t a) const { return a < 0x1000; }
    /** @brief ROM / leere Fassung (FFH) / RAM; außerhalb der Karte FFH. */
    uint8_t memRead(uint16_t a) const;
    /** @brief Nur das RAM 0C00–0FFF; ROM und leere Fassungen schlucken den Schreibzyklus. */
    void    memWrite(uint16_t a, uint8_t d);

    /** @brief Direkter Blick in das 1-KB-RAM (Offset 0–3FFH = Adresse 0C00H–0FFFH). */
    uint8_t ramPeek(uint16_t off) const { return ram_[off & 0x3FF]; }
    void    ramPoke(uint16_t off, uint8_t v) { ram_[off & 0x3FF] = v; }

    const Config& config() const { return cfg_; }

private:
    uint8_t pioPort(uint8_t rel) const;

    K1520Bus& bus_;
    Config    cfg_;

    Z80    cpu_;
    Z80CTC ctc_{"K2521-CTC"};
    Z80PIO pio_{"K2521-PIO"};

    std::array<uint8_t, 0x400> ram_{};

    LeseFn    lesen_;
    SchreibFn schreiben_;
    Z80CTC::ZCTOCallback zcto_ext_;
};
