/**
 * @file terminal_hw.h
 * @brief P8000-Terminalrechner Typ 2 mit ORIGINAL-Firmware (AP P20a, Entwurf 28 §4):
 *        Z8 UB 8840 M (7,3728 MHz) + EPROM 2732 + 2 KB Bildwiederholspeicher + 8275 +
 *        Zeichengenerator 2 × 2716 + Strobes (ZG2/ZG1/Klingel/TRES) + Watchdog +
 *        Tastatur-Schieberegister + serielle Leitung.
 *
 * @details
 * Hardwarebefund: doc/p8000/terminal_typ2.md.  Das Bild entsteht aus Firmware + `I8275`:
 * der 8275 fordert je Zeichenzeile eine Zeile an (DRQ → P31 = IRQ2), die Firmware lädt
 * mit einem LDE die DMA-Zähler und schaltet P35 = 0 (DMA ein) — dann gehen die Zeichen
 * der Zeile in den Zeilenpuffer.  Am Bildende (VRTC → P32 = IRQ0) baut `I8275::frame()`
 * die Zellen; daraus wird das Pixelbild 8 × Rasterzeilen je Zelle gerastert.
 *
 * Zeit: alles in internen Z8-Takten (3 686 400 Hz).  Der 8275-Zeichentakt (17,998 MHz / N)
 * wird mit Zähler + Rest fortgeschrieben (driftfrei); die Bildgeometrie stammt aus den
 * Reset-Parametern, die die Firmware dem 8275 gibt.
 *
 * **Benannte Annahmen** [A]/[U] (Tests halten sie fest, Entwurf 28):
 * - DMA-Zähler laden bei jedem /DM-Lesezyklus im BWS (die Adresse liegt am Bus) [A].
 * - DRQ Zeile 0 am Anfang der letzten VRTC-Zeichenzeile, DRQ Zeile r ≥ 1 am Anfang der
 *   Zeichenzeile r−1 (Datenblatt); P31 als Low-Impuls.  P32 = VRTC invertiert [U, Befund §2.1].
 * - Nicht bediente Zeile = leer (F1) [A].  Zeichensatz gilt für das ganze Bild (RS-FF) [G].
 * - Watchdog 100 ms ohne steigende P36-Flanke ⇒ Reset des Z8 (8275 nicht) [U: R/C unbekannt].
 * - Tastatur: steigende Taktflanke schiebt ¬Daten ein; nach Startbit + 8 Bit ist das Byte
 *   voll (P33 low), weitere Takte bis TRES verworfen [A].
 * - Videoregel: Punkt = ZG ∧ ¬VSP, Unterstrich/Unterstrich-Cursor in `underlineLine()`,
 *   danach XOR RVV; Highlight = Stufe 2 (VIDEO2 [U]).
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/peripherals/p8000_terminal/terminal.h"   // TerminalZelle, ATTR_*
#include "core/primitives/i8275.h"
#include "core/primitives/z8.h"

namespace k1520::p8000 {

struct P8000TerminalHwConfig {
    const uint8_t* firmware = nullptr;   ///< 4 KB (nullptr = P8T_1_5.0)
    size_t firmwareGroesse = 0;
    const uint8_t* zg1 = nullptr;        ///< Zeichensatz 1 (Strobe 4000H), 2 KB; nullptr = P8TEZS
    const uint8_t* zg2 = nullptr;        ///< Zeichensatz 2 (Strobe 2000H), 2 KB; nullptr = P8TDZS
    uint32_t punkttaktHz = 17'998'000;   ///< Quarz Q2
    int      zeichentaktTeiler = 8;      ///< N (7 oder 8) [U, Beschaffung B4]
    uint32_t watchdogMs = 100;           ///< 0 = Watchdog aus
};

/// Ein Ereignis der Leitung Terminal → Rechner.
struct TerminalSendung {
    uint8_t  byte = 0;
    bool     brk = false;      ///< mit halbierter Baudrate gesendetes 00H (Taste BREAK)
    uint64_t takt = 0;         ///< Ende des Rahmens (Z8-Takte)
    uint64_t dauer = 0;        ///< Rahmenlänge (Z8-Takte)
};

class P8000TerminalHw {
public:
    using Config = P8000TerminalHwConfig;
    static constexpr uint64_t Z8_HZ = 3'686'400;       ///< interner Takt (7,3728 MHz / 2)
    static constexpr uint64_t BIT_TAKTE = 384;         ///< 9600 Bd
    static constexpr int BWS = 0x1000, ZTAB = 0x1780;

    explicit P8000TerminalHw(const Config& cfg = Config());

    /// Netz ein: RAM 00H, 8275 zurück, Z8-Reset, Leitungen in Ruhe.
    void einschalten();

    /// Bis zur Zeit @p takt (Z8-Takte) laufen lassen (Video, Watchdog, DMA inklusive).
    void laufeBis(uint64_t takt);
    uint64_t takte() const { return z8_.takte; }

    // ── serielle Leitung ────────────────────────────────────────────────────
    /// Rechner → Terminal: ein Zeichen als Rahmen (Start, 8 Daten LSB zuerst, Stopp) auf P30,
    /// frühestens jetzt, nach dem letzten Rahmen.  Rahmenabstand @p bits Bitzeiten.
    void hostByte(uint8_t b, int bits = 10);
    /// Mitschrift: alle vom Rechner EMPFANGENEN Zeichen (Rohbytes, in Empfangsreihenfolge) werden
    /// gesammelt, solange sie an ist; die Oberfläche holt sie ab (fremder Faden, daher gesperrt).
    /// Aus ⇒ nichts wird gesammelt und ein Rest verworfen.  Nicht im Save-State.
    void setMitschrift(bool an);
    std::string holeMitschrift(size_t hoechstens = MITSCHRIFT_MAX);
    /// Rahmen, die noch nicht ganz auf der Leitung waren.
    size_t hostWartend() const;
    /// Zeit, zu der die Leitung frei wird (Ende des letzten Rahmens).
    uint64_t hostLeitungFrei() const { return rxFrei_; }
    /// Terminal → Rechner (in Sendereihenfolge).
    bool hatAusgabe() const { return !aus_.empty(); }
    TerminalSendung holeAusgabe() { auto s = aus_.front(); aus_.pop_front(); return s; }
    const std::deque<TerminalSendung>& ausgabe() const { return aus_; }

    // ── Tastaturschnittstelle XB1 ───────────────────────────────────────────
    /// Pegel an XB1/2 (Takt) und XB1/4 (Daten) zur aktuellen Zeit.
    void tastaturLeitung(bool takt, bool daten);
    /// Prüfhilfe ohne Tastaturmodell: ein ganzes Byte steht im Schieberegister.
    void tastaturByte(uint8_t b);
    bool tastaturRegisterVoll() const { return srVoll_; }

    // ── Bild ────────────────────────────────────────────────────────────────
    int zeilen() const { return 24; }
    int spalten() const { return 80; }
    /// Anfangsadresse der logischen Zeile @p z (aus der Zeilentabelle 1780H).
    uint16_t zeilenAdresse(int z) const;
    uint8_t  bwsByte(int z, int s) const;
    /// Zeile als Text: Zeichen 00–7FH roh, Feldattribut (80H–BFH) als Leerzeichen.
    std::string text(int z) const;
    TerminalZelle zelle(int z, int s) const;
    /// Das an (z, s) wirksame Feldattribut (ATTR_*-Bits, Feldregel bis Zeilenende).
    uint8_t wirksamesAttribut(int z, int s) const;
    int cursorZeile() const { return crt_.cursorRow(); }
    int cursorSpalte() const { return crt_.cursorCol(); }
    bool zeichensatz2() const { return zg2_; }
    unsigned klingel() const { return klingel_; }
    /// Firmware 5.0 steht in ihrer Warteschleife MAIN0 (0285H–0293H), ohne Zeichen in Tastatur-
    /// und Empfangspuffer (STAT0 = R04H, Bit 4/6) und ohne Rahmen auf der Leitung.  Prüfhilfe:
    /// ein Löschen des Bildes dauert ≈ 65 ms (jedes Zeichen wartet auf den Zeilen-DMA).
    bool ruht() const;
    unsigned watchdogResets() const { return wdResets_; }
    uint64_t bilder() const { return bilder_; }

    /// Zelle des 8275 nach dem letzten Bild (Feldattribute, Cursor, Blinkphase).
    const I8275::Cell& bildZelle(int z, int s) const { return crt_.cells(z, s); }
    /// Pixelbild des letzten Bildes: Breite × Höhe Byte, 0 dunkel / 1 normal / 2 hell.
    const std::vector<uint8_t>& pixel() const { return pix_; }
    int pixelBreite() const { return pixB_; }
    int pixelHoehe() const { return pixH_; }

    // ── Zugriff für Debugger/Tests ──────────────────────────────────────────
    Z8& z8() { return z8_; }
    const Z8& z8() const { return z8_; }
    I8275& crt() { return crt_; }
    const I8275& crt() const { return crt_; }
    std::array<uint8_t, 0x800>& ram() { return ram_; }
    const std::array<uint8_t, 0x800>& ram() const { return ram_; }
    uint8_t programm(uint16_t a) const { return a < fw_.size() ? fw_[a] : 0xFF; }
    /// Protokoll der 8275-Zugriffe (Adresse 0/1, Wert, lesend) — für die Abdeckungsprüfung.
    struct CrtZugriff { bool a0; uint8_t wert; bool lesen; };
    std::vector<CrtZugriff> crtProtokoll;
    bool crtProtokollieren = false;
    /// Zeilen-DMA: angeforderte / bediente Zeilen (Zähler seit dem Einschalten).
    uint64_t dmaAnforderungen() const { return drqZahl_; }
    uint64_t dmaBedient() const { return dmaZahl_; }

    // ── Save-State ──────────────────────────────────────────────────────────
    static constexpr uint8_t SAVE_VERSION = 1;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    struct Rahmen { uint64_t start; uint8_t byte; };

    void verdrahten();
    void visit(k1520::ZAr& a);
    // Bus
    uint8_t busLesen(const Z8BusZyklus& c);
    void    busSchreiben(const Z8BusZyklus& c, uint8_t v);
    void    portAusgang(int port, uint8_t pegel);
    bool    p30(uint64_t t);
    void    gesendet(uint8_t b);
    // Video
    struct Geometrie { int spalten, zeilen, linien, hrtc, vrtc; uint64_t zeile, bild; };
    Geometrie geometrie() const;
    void     bildBeginnen();                 ///< neues Bild: Geometrie, Ereigniszeiten
    uint64_t zeitVon(uint64_t zeichentakte) const;   ///< Bildanfang + Zeichentakte → Z8-Takte
    void     ereignis(int nr);
    void     bildEnde();                     ///< VRTC: frame() + Raster
    void     rastern();
    void     watchdogPruefen();

    Config cfg_;
    std::vector<uint8_t> fw_, zg1_, zg2v_;
    Z8 z8_;
    I8275 crt_;
    std::array<uint8_t, 0x800> ram_{};

    // Zeitführung Video
    uint64_t bildStart_ = 0;        ///< Z8-Takt des Bildanfangs
    uint64_t bildRest_ = 0;         ///< Rest (Nenner = punkttaktHz)
    uint64_t bildZt_ = 0;           ///< Bildlänge in Zeichentakten (dieses Bild)
    uint64_t zeileZt_ = 0;          ///< Zeichenzeile in Zeichentakten
    int      zeilenImBild_ = 24, vrtcZeilen_ = 3;
    int      naechstes_ = 0;        ///< Index des nächsten Ereignisses im Bild
    int      p31Ende_ = 0;          ///< 1 = P31 wieder high zu `p31Zeit_`
    uint64_t p31Zeit_ = 0;
    // DMA
    uint16_t dmaAdresse_ = 0x1000;
    int      drqZeile_ = -1;        ///< angeforderte, noch nicht bediente Zeile
    bool     dmaLauf_ = false;      ///< DMA seit dem Bildanfang nach START DISPLAY
    std::array<std::array<uint8_t, 128>, 64> zpuf_{};
    std::array<bool, 64> zbedient_{};
    uint64_t drqZahl_ = 0, dmaZahl_ = 0;
    int      lesePos_ = 0;          ///< frame(): Byte im Zeilenpuffer
    // Pins / Strobes
    uint8_t  p3Alt_ = 0xFF;
    bool     zg2_ = false;
    unsigned klingel_ = 0;
    // Watchdog
    uint64_t wdLetzt_ = 0;
    unsigned wdResets_ = 0;
    // Tastatur-Schieberegister
    uint16_t sr_ = 0;
    bool     srVoll_ = false, taktAlt_ = false;
    // serielle Leitung
    std::deque<Rahmen> rx_;
    std::mutex mitschriftSperre_;
    std::string mitschrift_;
    bool mitschriftAn_ = false;
    static constexpr size_t MITSCHRIFT_MAX = 1u << 20;   ///< ungeholt: mehr als 1 MiB verfällt
    uint64_t rxFrei_ = 0;
    std::deque<TerminalSendung> aus_;
    // Bild
    uint64_t bilder_ = 0;
    std::vector<uint8_t> pix_;
    int pixB_ = 640, pixH_ = 312;
};

}  // namespace k1520::p8000
