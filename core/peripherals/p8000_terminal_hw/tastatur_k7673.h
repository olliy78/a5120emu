/**
 * @file tastatur_k7673.h
 * @brief Flachtastatur K7673.09 (P8000-Terminal Typ 2) als Verhaltensmodell (AP P20b,
 *        Entwurf 28 §5, Befund doc/p8000/tastatur_k7673.md).
 *
 * @details
 * Die Codetabelle wird zur Laufzeit aus dem EPROM-Abzug gelesen (02E3H: 128 × [Merker, Code],
 * Index = Zeile·16 + Port·8 + Bit; Folgen ab (Merker & 7)·256 + Code, Länge Merker >> 4).
 * Der Ablauf bildet die Firmware 0048H–0285H nach — nicht den Prozessor:
 *  - Abtastung 8 Zeilen × 16 Spalten, zwischen den Zeilen 0–6 je ein Sendeversuch aus dem
 *    Puffer (ein ganzes Byte), Zeile 7 mit Auswertung;
 *  - 41 gleiche Abtastungen hintereinander = stabil; > 3 gedrückte Tasten ⇒ verworfen;
 *  - je Spaltenbyte (aufsteigend) erst Make, dann Break, Bits aufsteigend;
 *  - Sonderfälle 02EFH (1DH-Taste gemerkt), 02FFH (PAUSE: Break nichts, mit 1DH ⇒ 03EDH, keine
 *    Wiederholung), LED-Tasten 03DBH/032FH/03DFH (P2.4/5/6, einmal je Druck umschalten);
 *  - Wiederholung der zuletzt gedrückten Taste: Zähler 0DH bis 50, 0CH bis 10 im T1-Takt
 *    (40 · 250 · 4 interne Takte), geprüft am Rundenende;
 *  - Puffer 16 Byte + FF als Überlaufmarke; AA beim Einschalten.
 * Zeit in **internen Takten der Tastatur** (Quarz / 2).  Die Zeitkonstanten (Zeilenabstand,
 * Byte-Rahmen) sind an der Firmware auf dem Z8-Kern gemessen (Wächter
 * `TastaturK7673Diff.*`).  **Quarz unbekannt** [U, Beschaffung B3/B6]: Vorgabe 8 MHz
 * ⇒ Wiederholung nach 500 ms, dann alle 100 ms (am Rundenende, also etwas später).
 *
 * Leitung: Ruhe Takt 0 / Daten 1; je Byte Takt hoch, Startbit Daten 0, je Bit Daten = ¬Bit
 * (LSB zuerst), Takt low → high mit stabilen Daten; danach Ruhe.  Kein Rückkanal; die
 * „Sendefreigabe" P3.0 gilt als immer frei [U].
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace k1520 { struct ZAr; }

namespace k1520::p8000 {

struct TastaturK7673Config {
    const uint8_t* rom = nullptr;      ///< 2 KB (nullptr = K7673.09)
    uint32_t quarzHz = 8'000'000;      ///< [U]
};

/// Pegelwechsel an XB1 (Takt XB1/2, Daten XB1/4) zur Zeit @ref takt (interne Tastaturtakte).
struct TastaturFlanke {
    uint64_t takt = 0;
    bool     taktPegel = false;
    bool     daten = true;
};

class TastaturK7673 {
public:
    using Config = TastaturK7673Config;
    static constexpr int ZEILEN = 8, SPALTEN = 16;   ///< Spalte 0–7 = P0.0–7, 8–15 = P1.0–7
    // Zeitkonstanten der Firmware (interne Takte, gemessen auf dem Z8-Kern)
    static constexpr uint64_t INIT_TAKTE = 1137;      ///< Reset → erste Zeile
    static constexpr uint64_t ZEILE_TAKTE = 445;      ///< Zeile → Zeile (ohne Senden)
    static constexpr uint64_t RUNDE_REST = 449;       ///< Zeile 7 → Zeile 0 (Vergleich)
    static constexpr uint64_t KOPIE_EXTRA = 90;       ///< 38H = 0: Abtastung als Kandidat kopieren
    static constexpr uint64_t AUSWERT_EXTRA = 9782;   ///< 38H = 40: Tasten zählen, Unterschiede, Kopie
    static constexpr uint64_t BYTE_TAKTE = 5344;      ///< ein gesendetes Byte (Schleife 0240H)
    static constexpr uint64_t TICK_TAKTE = 40000;     ///< T1-Endwert (PRE1 = 40, T1 = 250, ×4)
    static constexpr int STABIL = 40;                 ///< 38H bis 28H
    static constexpr int VERZOEGERUNG = 50, ABSTAND = 10;   ///< 22H/23H

    explicit TastaturK7673(const Config& cfg = Config());

    void einschalten();
    void druecke(int zeile, int spalte);
    void loslassen(int zeile, int spalte);
    void allesLoslassen();
    bool gedrueckt(int zeile, int spalte) const;

    /// Bis @p takt (interne Tastaturtakte) laufen; erzeugte Flanken liegen danach bereit
    /// (sie können über @p takt hinausreichen — ein Byte wird als Ganzes geplant).
    void laufeBis(uint64_t takt);
    uint64_t takte() const { return jetzt_; }
    uint32_t taktHz() const { return cfg_.quarzHz / 2; }

    /// Flanken mit Zeit ≤ @p bis abholen (in Reihenfolge).
    std::vector<TastaturFlanke> holeFlanken(uint64_t bis);
    bool flankenAnstehend() const { return !flanken_.empty(); }
    /// Protokoll aller fertig geplanten Bytes (Prüfung).
    const std::vector<uint8_t>& gesendet() const { return log_; }
    void gesendetLeeren() { log_.clear(); }
    /// LEDs: Bit 0 ON/OFF (P2.4), Bit 1 CAPS LOCK (P2.5), Bit 2 MODE (P2.6); 1 = umgeschaltet.
    uint8_t leds() const { return uint8_t((p2_ >> 4) & 7); }
    size_t pufferBelegung() const { return puffer_.size(); }
    /// Prüfhilfe: Byte wie die Firmware-Routine 0209H einreihen (Überlauf ⇒ FF).
    void einreihen(uint8_t b);

    /// Tabelleneintrag (Merker, Code) einer Matrixposition.
    uint8_t merker(int zeile, int spalte) const;
    uint8_t code(int zeile, int spalte) const;
    /// Make-Folge, wie sie ohne Sonderzustand gesendet würde (Tabelle).
    std::vector<uint8_t> makeFolge(int zeile, int spalte) const;

    static constexpr uint8_t SAVE_VERSION = 1;
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    void visit(k1520::ZAr& a);
    void schritt();                       ///< eine Zeile (bzw. Sendeversuch) ausführen
    uint64_t runde();                     ///< Auswertung nach Zeile 7; Rückgabe = Zusatztakte
    void ausgeben(uint16_t adr, bool brk);  ///< Firmware 0146H
    void senden(uint8_t b);               ///< Flanken eines Bytes ab jetzt_
    uint16_t adresse(int index) const { return uint16_t(0x2E3 + 2 * index); }
    uint8_t  rom(uint16_t a) const { return rom_[a & 0x7FF]; }

    Config cfg_;
    std::vector<uint8_t> rom_;
    std::array<uint16_t, ZEILEN> matrix_{};   ///< gedrückt = 1, Bit = Spalte
    // Firmwarezustand
    std::array<uint8_t, 16> scan_{}, kandidat_{}, gueltig_{};
    int zaehler38_ = 0;
    int zeile_ = 0;                  ///< nächste abzutastende Zeile
    bool lTakt_ = true, lDaten_ = true;   ///< zuletzt geplanter Leitungszustand
    uint16_t letzte_ = 0;            ///< 20H/21H (Hi = 0 ⇒ keine Wiederholung)
    bool held1D_ = false;            ///< 3AH
    uint8_t ledMerker_ = 0;          ///< 39H
    uint8_t p2_ = 0x8F;
    int z0D_ = 0, z0C_ = 0;
    std::deque<uint8_t> puffer_;
    // Zeit
    uint64_t jetzt_ = 0, naechst_ = 0, tick_ = 0;
    std::deque<TastaturFlanke> flanken_;
    std::vector<uint8_t> log_;
};

}  // namespace k1520::p8000
