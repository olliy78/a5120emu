/**
 * @file mbi.h
 * @brief Magnetbandinterface MBI des MC80.21/.22 (AP M11): Laufwerksregister EFH, CTC F8H–FBH,
 *        PIO FCH–FFH und das eingebaute Kassettenlaufwerk SK 1200.
 *
 * Quellen: doc/mc80/hardware_2x.md §7 (AP M10), doc/mc80/bsys_2x.prn 0600H–0BFFH und 2000H–2137H
 * (Bandprogramm „Programmversion 12.01.84“).  Ein Schaltplan der MBI fehlt (hardware_2x.md §11);
 * die Verdrahtung ist aus dem Code erschlossen, Annahmen sind mit [?] markiert.
 *
 * **Kein K5020-Modell** (hardware_2x.md §12): das Byte geht als Ganzes ueber ein Schieberegister
 * an PIO A; wiederverwendet wird nur Band und Laufwerk (`core/peripherals/kassette`, Profil
 * SK1200: 4,76 cm/s, 32 Bit/mm).  Das Bandformat ist reine Software (`AA`·BLN hi,lo·128
 * Daten·CRC lo,hi·`AA`, Bandmarke `AA 00 00 AA`); das Medium speichert die Bytes roh.
 *
 * **Tore:**
 *   - EFH (nur schreiben) Laufwerksregister [?: Deutung der Werte, hardware_2x.md §7.2]:
 *     `13H` Lauf (mit Capstan, PIO B Bit 1) 4,76 cm/s vorwaerts, `0AH` schneller Ruecklauf,
 *     `06H` schneller Vorlauf; jeder andere Wert (`00H` aus, `11H` Andruck vorbereiten, `02H`
 *     Endabschaltung) = Band steht.  Lesen liefert FFH [?].
 *   - F8H–FBH CTC 0–3, FCH/FDH PIO A Daten/Steuer, FEH/FFH PIO B Daten/Steuer.
 *   - PIO B Ausgaenge (Bit 0–3): Bit 0 = 1 Lesen / 0 Schreiben, Bit 1 Capstan, Bit 3 Daten (0 =
 *     Pausenpegel) [HB IV-15]; Eingaenge (Bit 4–7): Bit 7 = Kassette eingelegt, Bit 6 = 0 am
 *     Bandende (Vorspann unter der Lichtschranke) [?], Bit 5 = Schreibsperre, Bit 4 =
 *     Pausenpegel/Phase beim Blockanfang — hier **immer 0** (8 Bit bis zum ersten Byte) [?].
 *
 * **Schreiben** (PIO B Bit 0 = 0): CTC 2 (Zeitgeber, 3072 Hz bei `07H/32H`) → ÷2 [?] =
 * Bittakt 1536 Hz an CLK/TRG0 → CTC 0 (Zaehler, ZK 8) → ZC/TO0 = Bytegrenze an CLK/TRG3 → CTC 3
 * (ISB/IVL/IPA je Byte).  An jeder Bytegrenze uebernimmt das Schieberegister den Inhalt von PIO A
 * und schreibt ihn — bei PIO B Bit 3 = 1 — aufs Band, sonst Pausenpegel (Luecke).  Der Kopf loescht,
 * was er im Schreibbetrieb mit laufendem Capstan ueberfaehrt (`aufzeichnen`).
 *
 * **Lesen** (Bit 0 = 1): CLK/TRG3 = Synchronisations-/Blockendeerkennung [HB (55)]: ein Impuls,
 * wenn ein Block unter den Kopf kommt, und einer zwei Bitzeiten [?] nach seinem letzten Byte
 * (ISY schaerft CTC 0, BLE0 prueft beim zweiten die Bytezahl).  Jedes vom Band gelesene Byte kommt
 * ueber das Schieberegister (Strobe an PIO A, Betriebsart 1) mit acht Bittakten an CLK/TRG0 —
 * nach dem achten fordert CTC 0 den ILB-Interrupt.  CTC 1/2 (`47H/4BH` beim Lesen) haben keinen
 * erkennbaren Zweck und bekommen keinen Takt [?].
 *
 * **Laufwerk:** belegt (`reservieren`), solange EFH ≠ 0 oder der Capstan laeuft; danach (AUS) laesst
 * sich die Kassette auswerfen/wenden.  Neue Kassetten: `Geometrie::kompaktkassette()` [?: ohne
 * BOT-/EOT-Marke, 80 mm Vorspann] — eine Marke hinter dem Vorspann schaltete das Band ueber den
 * PIO-B-Interrupt (IPB, Bit 6) mitten in der Vorluecke ab.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/peripherals/kassette/kassetten_laufwerk.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include <cstdint>

class Mc80Mbi : public BusDevice, public InterruptSlave {
public:
    struct Config {
        uint8_t reg_port = 0xEF;                          ///< Laufwerksregister
        uint8_t io_base  = 0xF8;                          ///< CTC F8H–FBH, PIO FCH–FFH
        double  takt_hz  = 2'457'600.0;
        kassette::ProfilId profil = kassette::ProfilId::SK1200;
    };

    Mc80Mbi();
    explicit Mc80Mbi(const Config& cfg);

    void attachToBus(K1520Bus& bus);
    /// /RESET: CTC/PIO zurueck, Register EFH = 0 (Band steht); die Kassette bleibt liegen.
    void reset();
    /// @p ticks Systemtakte.  @return true, wenn sich der Interruptzustand aendern konnte.
    bool clockTick(int ticks);
    /// Gebundene `.k52` nach der Schreibpause zurueckschreiben (einmal je run()-Scheibe).
    void autoFlush() { lw_.autoFlush(); }

    // ─── BusDevice ───────────────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "MC80.2x MBI"; }

    // ─── InterruptSlave: IEI → CTC → PIO → IEO [? Reihenfolge auf der Karte] ──
    void        setIEI(bool iei) override;
    bool        getIEO() const override { return pio_.getIEO(); }
    bool        hasInterrupt() const override { return ctc_.hasInterrupt() || pio_.hasInterrupt(); }
    uint8_t     getVector() const override;
    void        onRETI() override;
    /**
     * @brief RETI nach dem Datenblatt: ist ein Kanal/Port der Karte in Bedienung, nimmt die Karte
     *        das RETI an (hoechster Kanal in Bedienung, auch wenn ein hoeherer gerade anfordert)
     *        und meldet true.  Die Maschine gibt das RETI so nur dem obersten Geraet in Bedienung
     *        (Z80CTC::setRetiNachIus, AP M11).
     */
    bool        retiAnnehmen();
    const char* intDeviceName() const override;

    kassette::KassettenLaufwerk&       laufwerk()       { return lw_; }
    const kassette::KassettenLaufwerk& laufwerk() const { return lw_; }
    Z80CTC& ctc() { return ctc_; }
    Z80PIO& pio() { return pio_; }
    uint8_t register_efh() const { return efh_; }
    uint8_t ausgabeB() const { return fe_; }
    /// Eingaenge von PIO B (Bit 7–4) so, wie die Karte sie gerade anlegt.
    uint8_t eingaengeB() const;
    /// Bytes, die das Schieberegister seit Netz ein aufs Band gegeben bzw. vom Band geholt hat.
    uint64_t geschriebeneBytes() const { return n_geschrieben_; }
    uint64_t geleseneBytes() const { return n_gelesen_; }

private:
    void updateInternalChain();
    void laufwerkSteuern();
    void pinsB();
    void zcto(int kanal);
    void puls(int kanal);
    bool lesen() const { return (fe_ & 0x01) != 0; }

    Config cfg_;
    Z80CTC ctc_{"MBI-CTC"};
    Z80PIO pio_{"MBI-PIO"};
    kassette::KassettenLaufwerk lw_;
    bool   iei_in_ = true;

    uint8_t efh_ = 0;
    uint8_t fe_  = 0x01;          ///< PIO-B-Ausgang (Vorgabe: Lesen, alles aus)
    uint8_t a_   = 0;             ///< PIO-A-Ausgang (Schreib-Schieberegister)
    uint8_t pins_b_ = 0xFF;
    bool    teiler_ = false;      ///< ÷2 zwischen CTC-2-Ausgang und Bittakt [?]
    bool    bms_ = false;         ///< Block unter dem Kopf (Lesen)
    int64_t ende_in_ = -1;        ///< Takte bis zum Blockende-Impuls (−1 = keiner faellig)
    int     lw_rest_ = 0;         ///< noch nicht ans Laufwerk gegebene Takte
    uint64_t n_geschrieben_ = 0, n_gelesen_ = 0;
};
