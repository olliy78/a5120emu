/**
 * @file mc80_2x.h
 * @brief MC80.21/.22 (VEB Elektronik Gera) — zweite MC80-Generation im Kern (AP M11).
 *
 * Massgeblich: doc/mc80/hardware_2x.md, doc/mc80/bsys_2x.prn (AP M10).  Der Rechner ist ein
 * K1520-Bus mit fuenf Karten; nichts davon gleicht dem MC80.3x ausser dem Takt, deshalb eine
 * eigene Klasse neben @ref Mc80Machine (gleicher Maschinentyp 6, `k1520_create_mc80(3|4, …)`).
 *
 * @code
 *   ZRE  K2521  U880 2,4576 MHz, ROM 0000–0BFF = S0.1–S0.3 (MAME-Satz „ver2“: s01+s02_v2+s03_v2),
 *               RAM 0C00–0FFF (BWS 0C00–0CFF, Systemzellen, Stapel), CTC 80H, PIO 84H (Anwender)
 *   PFS  K3820  ab 2000H: Sockel 0 = mo01 (S0.4 Bandmenue), 1 leer (2400H „frei“), 2 = mo03 (S2
 *               EPROM-Programmierung), 3–7 = mo04–mo08 (Editor), 8–15 = mo09–mo16 (EPAS/BASEX)
 *   OPS  K3525  16 KB dRAM ab C000H (Power-on FFH wie die Karte, A2)
 *   DPL         CTC F0H, PIO F4H: Bild 8 × 32 + Analogkurven, Tastenmatrix (core/cards/mc80/dpl.h)
 *   MBI         EFH + CTC F8H + PIO FCH, Laufwerk SK 1200 (core/cards/mc80/mbi.h)
 *   PPE         E0H–E8H nur Senke; 00H–0FH (RAM-Schreibsperre 01H/02H) Senke
 * @endcode
 *
 * **.21 und .22** unterscheiden sich in den vorhandenen Unterlagen nicht greifbar (Handbuch:
 * „.22 wie .21“, eine Tabellenspalte mit Fussnote „elektronisch gesteuertes Laufwerk“ ist in der
 * Textebene verstuemmelt [?]).  Beide bekommen hier denselben ROM-Satz ver2 und das SK 1200 an der
 * MBI (Register EFH = elektronisch gesteuert).  Die KMBG-Fassung des Original-Listings (Taste
 * „LAUF“, kein EFH) hat keinen Abzug.
 *
 * **Interruptkette** [HB III-16]: hoechste Prioritaet rechts (Platz 11) — DPL, dann MBI, dann die
 * ZRE (IEI vom Systembus) [? Reihenfolge DPL/MBI; hardware_2x.md §2 nennt beide „auf den beiden
 * hoechsten Plaetzen (11, 10)“].
 *
 * **Tastatur:** `keyPress` nimmt Qt-Codes bzw. ASCII; jeder Anschlag wird als Matrixtaste (mit
 * UC/CTR) 60 ms gehalten und 60 ms losgelassen [?], in Maschinenzeit — das Betriebssystem liest
 * die Matrix einmal je Bild (~17 ms) und erkennt den neuen Druck an der Flanke (TST).
 * Escape = OFF, Return = ENTER, Tab = TAB, Rueck/Entf = CL, Pfeile = Kursortasten, Pause = BK.
 *
 * Kein Save-State, keine Laufwerke.  RAF/K6022/K5020 als Zusatzkarten nach der Grundregel der
 * Basisklasse (K6022 scheitert an der PPE auf E0H).
 */

#pragma once
#include "core/machines/machine.h"
#include "core/bus/k1520_bus.h"
#include "core/cards/k2521/k2521.h"
#include "core/cards/k3820/k3820.h"
#include "core/cards/k3525/k3525.h"
#include "core/cards/mc80/dpl.h"
#include "core/cards/mc80/mbi.h"
#include "core/cards/mc80/senke.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Mc80_2xMachine : public K1520Machine {
public:
    struct Config {
        enum class Variante { Mc8021, Mc8022 };
        Variante variante = Variante::Mc8022;
        /// Vorbelegung des 1-KB-RAM der ZRE (das OPS beginnt wie die K3525 mit FFH).
        uint8_t ram_fill = 0x00;
    };

    static constexpr uint32_t CPU_HZ = 2'457'600;
    /// Zeitkonstanten der Tastatursteuerung (Maschinentakte) [?].
    static constexpr int TASTE_HALTEN = CPU_HZ / 1000 * 60;
    static constexpr int TASTE_PAUSE  = CPU_HZ / 1000 * 60;

    Mc80_2xMachine() : Mc80_2xMachine(Config{}) {}
    explicit Mc80_2xMachine(const Config& cfg);
    ~Mc80_2xMachine() override;

    static const char* variantenName(Config::Variante v);

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    void powerOn() override;
    void reset() override;
    int  run(int max_cycles) override;
    void stop() override { stop_.store(true); }
    void nmi() override { nmi_taster_.store(true, std::memory_order_relaxed); }

    // ─── Bild (DPL) ──────────────────────────────────────────────────────────
    const uint8_t* framebuffer() const override { return dpl_.framebuffer(); }
    int  fbWidth()  const override { return Mc80Dpl::BREITE; }
    int  fbHeight() const override { return Mc80Dpl::HOEHE; }
    bool fbDirty()  const override { return dpl_.dirty(); }
    void fbClearDirty() override { dpl_.clearDirty(); }
    void setConsoleMode(bool) override {}
    bool consolePoll(int&, int&, char&) override { return false; }
    /// ASCII-Zeichen der Bildzelle (Zeile 0…7, Spalte 0…31) — wie die DPL sie gezeichnet hat
    /// (nicht aus dem BWS); ' ' bei nie gezeichneten Zellen und Analogzeilen, 0 ausserhalb.
    uint8_t screenChar(int col, int row) const override;
    std::string textZeile(int row) const;

    // ─── Tastatur (Matrix an der DPL) ────────────────────────────────────────
    void keyPress(uint32_t keycode, bool shift, bool ctrl) override;
    void keyRelease(uint32_t) override {}
    void setKeyRepeatRealtime(bool) override {}
    /// Matrixtaste @p nr (0…37H) einreihen, wahlweise mit UC/CTR (gleichzeitig gedrueckt).
    void tasteEinreihen(int nr, bool uc = false, bool ctr = false);
    /// Zeichen @p c als Anschlag einreihen; false, wenn es keine Taste dafuer gibt.
    bool zeichenEinreihen(char c);
    /// Matrixnummer + UC fuer ASCII @p c (−1 = keine Taste).  Kleinbuchstaben = Grossbuchstaben.
    static int tasteFuer(char c, bool& uc);
    bool tastenLeer() const;

    int machineType() const override { return 6; }   // K1520_MACHINE_MC80
    Config::Variante variante() const { return cfg_.variante; }

    // ─── Keine Laufwerke ─────────────────────────────────────────────────────
    bool mountDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool mountDiskImage(int, std::unique_ptr<DiskImage>, bool) override { return false; }
    bool createDisk(int, const std::string&, const std::string&, bool) override { return false; }
    bool saveDiskAs(int, const std::string&, const std::string&) override { return false; }
    bool unmountDisk(int) override { return false; }
    bool flushDisks() override { return false; }
    bool isDiskRawCompatible(int) const override { return false; }
    std::string diskPath(int) const override { return ""; }
    std::string diskContainer(int) const override { return ""; }
    std::string diskNotice(int) const override { return ""; }
    std::string detectedFormatName(int) const override { return ""; }
    std::string defaultFormatName(int) const override { return ""; }
    std::vector<std::string> compatibleFormats(int) const override { return {}; }
    std::string formatDescription(const std::string&) const override { return ""; }
    const FormatCatalog& formatCatalog() const override;
    bool isDiskActive(int) const override { return false; }
    bool isDiskWriteProtected(int) const override { return false; }
    bool isDiskLedOn(int) const override { return false; }
    bool isMotorOn(int) const override { return false; }
    bool isHeadLoaded() const override { return false; }
    void setDiskWriteProtect(int, bool) override {}

    void setDFUECallback(SerialCb) override {}
    void dfueSend(uint8_t) override {}

    // ─── Kassette: Laufwerk 0 = SK 1200 an der MBI ───────────────────────────
    kassette::KassettenLaufwerk* kassettenLaufwerk(int drive) override
    { return drive == 0 ? &mbi_.laufwerk() : nullptr; }

    // ─── Diagnose ────────────────────────────────────────────────────────────
    uint8_t memReadDebug(uint16_t a) override { return bus_.memRead(a); }
    void    memWriteDebug(uint16_t a, uint8_t d) override { bus_.memWrite(a, d); }
    uint8_t ioReadDebug(uint8_t port) override { return bus_.ioRead(port); }
    std::string lastError() const override { return ""; }

    K2521&    zre() { return zre_; }
    K3820&    pfs() { return pfs_; }
    K3525&    ops() { return ops_; }
    Mc80Dpl&  dpl() { return dpl_; }
    const Mc80Dpl& dpl() const { return dpl_; }
    Mc80Mbi&  mbi() { return mbi_; }
    Mc80Senke& ppe() { return ppe_; }
    Mc80Senke& systemtore() { return sys_; }
    K1520Bus& bus() { return bus_; }
    uint64_t  totalCycles() const { return total_cycles_; }
    void      clearStop() { stop_.store(false); }
    uint16_t  cpuPC() const { return zre_.cpu().PC; }
    void setCpuTraceCallback(std::function<void(const Z80&)> cb) { zre_.cpu().traceCallback = std::move(cb); }
    void setBusTrace(K1520Bus::BusTrace cb) { bus_.setTraceCallback(std::move(cb)); }

protected:
    K1520Bus& systemBus() override { return bus_; }
    uint32_t  k6022TaktHz() const override { return CPU_HZ; }

private:
    /// 0000–0FFF der K2521 am Bus (die Karte meldet ihren Speicher nicht selbst an).
    class ZreSpeicher : public MemDevice {
    public:
        explicit ZreSpeicher(K2521& z) : z_(z) {}
        uint8_t memRead(uint16_t a) override { return z_.memRead(a); }
        void    memWrite(uint16_t a, uint8_t d) override { z_.memWrite(a, d); }
        bool    isWritable() const override { return true; }
    private:
        K2521& z_;
    };
    struct Anschlag { int nr; bool uc; bool ctr; };

    static K2521::Config zreConfig();
    void resetHardware();
    bool tastenTakt(int takte);

    const Config cfg_;
    K1520Bus  bus_;
    K2521     zre_;
    ZreSpeicher zre_speicher_;
    K3820     pfs_;
    K3525     ops_;
    Mc80Dpl   dpl_;
    Mc80Mbi   mbi_;
    Mc80Senke sys_{"MC80.2x intern (00H-0FH)", 0x00, 0x10};
    Mc80Senke ppe_{"MC80.2x PPE (E0H-E8H)", 0xE0, 0x09};

    mutable std::mutex tasten_mtx_;
    std::deque<Anschlag> tasten_;
    bool    taste_unten_ = false;
    int64_t taste_rest_ = 0;
    Anschlag taste_akt_{-1, false, false};

    std::atomic<bool> stop_{false};
    std::atomic<bool> nmi_taster_{false};
    uint64_t total_cycles_ = 0;
};
