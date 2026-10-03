/**
 * @file ass590069.h
 * @brief ASS 590069 (Numerik) — „Anschlusssteuereinheit seriell“, hier: der Anschluss
 *        des **Fernschreibers F1100/F1200** (doc/design/20_prg710.md §3.5, AP-P8c;
 *        Befunde doc/prg710/sif1000_fernschreiber.md §3).
 *
 * Belegt aus dem SCPX-BIOS `B17172FS`/`B17272FS` [Disk] (LIST E2DAH, Initialisierung
 * E2F4H, Ausgabe E35BH — beide Fassungen gleich):
 * - **SIO C4H–C7H** in der Ordnung Daten A, Daten B, Steuer A, Steuer B (A0 = B/A,
 *   A1 = C/D): Kanal **B** = Fernschreiber (`OUT (C5H)` Daten, `IN (C7H)` RR0 D2).
 *   Programmiert: WR4 = F8H (×64, 1½ Stopp, ohne Parität), WR3 = 01H (5 Bit), WR5 = 88H
 *   (DTR, Sender ein, **5 Bit**), keine Interrupts.  Kanal A belegt kein Programm [?].
 * - **CTC CCH–CFH** [?] (nur `OUT (CDH)` = Kanal 1 belegt): 07H, 18H = Zeitgeber,
 *   Vorteiler 16, Zeitkonstante 24 → ZC/TO1 = φ/384 = 6400 Hz = Sende-/Empfangstakt von
 *   SIO-Kanal B [?] → bei ×64 **100 Bd**.
 * - Zeichen: **ITA2 (CCITT Nr. 2)**, 5 Bit, Buchstaben-/Ziffernumschaltung 1FH/1BH; die
 *   Umsetztabelle steht im BIOS bei E366H (64 Einträge, Index = Ziffernlage·20H + Code).
 *
 * Nach außen (SerialHub, ein Anschluss „Fernschreiber“): die Karte bildet den
 * **Fernschreiber selbst** nach — sie nimmt die ITA2-Zeichen ab, führt die Umschaltung
 * und gibt **Text (ASCII)** weiter: Telnet zeigt ihn, „Datei“ ist ein lesbares
 * Fernschreibprotokoll.  Umschaltzeichen (und ITA2 00H) verbrauchen ihre Zeichenzeit,
 * erscheinen aber nicht.  Der Wandler taktet im Zeichentakt der Gastleitung (100 Bd ≈
 * 75 ms je Zeichen); gemeldet wird deshalb dieses Tempo mit **8 Bit** (es geht Text
 * hinaus, keine 5-Bit-Zeichen).  Empfangsrichtung (Tastatur des Fernschreibers): ein
 * ankommendes ASCII-Zeichen wird als ITA2-Code in den Empfänger gelegt (ohne Umschaltung);
 * kein bekanntes Programm liest sie (READER des BIOS liefert 1AH).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_sio.h"
#include "core/serial/anschluss.h"
#include <cstdint>
#include <memory>
#include <optional>

class Ass590069 {
public:
    struct Config {
        uint8_t sio_basis = 0xC4;   ///< [Disk] B17172FS
        uint8_t ctc_basis = 0xCC;   ///< [?] nur Kanal 1 (CDH) belegt
        int     takt_kanal = 1;     ///< CTC-Kanal, der SIO-Kanal B taktet [?]
    };

    Ass590069() : Ass590069(Config{}) {}
    explicit Ass590069(const Config& cfg);
    ~Ass590069();

    void attachToBus(K1520Bus& bus);
    void reset();
    /// Maschinentakte fortschreiben (CTC, Umschaltzeichen); true = Interruptzustand evtl. geändert.
    bool clockTick(int takte);

    Z80SIO& sio() { return sio_; }
    Z80CTC& ctc() { return ctc_; }
    /// Der Anschluss „Fernschreiber“ für den SerialHub.
    k1520::serial::SerialAnschluss& anschluss();

    /// ITA2 → ASCII nach der BIOS-Tabelle (E366H); 0 = kein druckbares Zeichen.
    static uint8_t ita2NachAscii(uint8_t code, bool ziffern);
    /// ASCII → ITA2 (Buchstabenlage zuerst); -1 = nicht darstellbar.
    static int asciiNachIta2(uint8_t ascii);

private:
    class Anschluss;
    friend class Anschluss;

    /// Liegt im Sender ein Zeichen, das nichts druckt (Umschaltung, 00H)?
    bool stummesZeichen() const;

    Config  cfg_;
    Z80SIO  sio_{"590069 SIO"};
    Z80CTC  ctc_;
    struct Ports : BusDevice {   // C4/C5/C6/C7 → Z80SIO 0=A-D, 1=A-S, 2=B-D, 3=B-S
        explicit Ports(Z80SIO& s) : sio(s) {}
        static uint8_t rel(uint8_t p) { return static_cast<uint8_t>(((p & 1) << 1) | ((p >> 1) & 1)); }
        uint8_t ioRead(uint8_t p) override { return sio.ioRead(rel(p & 3)); }
        void ioWrite(uint8_t p, uint8_t d) override { sio.ioWrite(rel(p & 3), d); }
        const char* deviceName() const override { return "ASS 590069 SIO"; }
        Z80SIO& sio;
    } ports_{sio_};

    uint64_t zeit_ = 0;
    uint64_t stumm_bis_ = 0;          ///< Umschaltzeichen „im Schieberegister“ bis dahin
    bool     stumm_laeuft_ = false;
    bool     ziffern_ = false;        ///< Lage des nachgebildeten Fernschreibers
    std::unique_ptr<Anschluss> anschluss_;
};
