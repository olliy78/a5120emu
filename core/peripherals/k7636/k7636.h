/**
 * @file k7636.h
 * @brief Parallele Tastatur K7636 (K7636.01, lateinisch) an der U-Bus-Schnittstelle der K2526.
 *
 * Verhaltensmodell nach `doc/a5120_alt/k7636.md` (AP A3a) und der Codetabelle
 * `doc/a5120_alt/k7636_01_codes.csv`; Entwurf 30 §3.3.  Die Tastatur hat am echten Gerät einen
 * eigenen U880 (Matrix 16 × 8, ROM Y708); nachgebildet ist ihr Verhalten an Stecker X1, nicht
 * der Prozessor.  Sie steckt am U-Bus der K2526 (E/A 00H–07H) und wird dort als
 * @ref UBusGeraet angeschlossen (`K2526::setUBusGeraet`) — **nur zuschaltbar**: ohne Anschluss
 * bleibt die K2526 (und damit die K7637-Verdrahtung des A5120) unverändert.
 *
 * Ports (Richtung aus Sicht der CPU):
 * - `06H` IN  (/UCS1) Tastencode bzw. Typzeichen; **Lesen quittiert** (/UINT zurück) [?, k7636.md §4]
 * - `01H` IN  (/UCS2) Status: Bit 3 = /UINT (**0 = Code liegt bereit**, Ruhewert 1), Bit 2 =
 *   „CTRL gedrückt“ (0 = gedrückt; die K7636 treibt es nicht → 1).  Ruhewert `0CH`.
 * - `03H` OUT (/UCS4) Kommando (Reset, Steuern, Fehlerlampe/Signal/SHIFT, Spezial).  **Jedes
 *   Kommando wird mit dem Typzeichen `80H` beantwortet** (CP/A wartet darauf, `lmp36w`).
 * - `05H` OUT (/UCS3) von der K7636 nicht benutzt.
 *
 * **Host-Tastencodes** (`keyPress(k, shift, ctrl)`):
 * - `QK_TASTE_BASE | Rechenadresse` — **physische Taste** der Matrix (00H–7FH): ergibt den Code
 *   der Grundstellung a bzw. (mit `shift`, gehaltener SHIFT-Taste 7FH oder LOCK) der
 *   Umschaltstellung b.  `7FH` = SHIFT (gehalten bis @ref keyRelease), `77H` = LOCK (rastet,
 *   jeder Druck schaltet um); beide liefern keinen Code.  Positionen ohne Taste/Code (00H in
 *   der Tabelle) schweigen.
 * - Druckbares ASCII (20H–7EH): die Taste der Tabelle, auf der das Zeichen steht (Stufe ergibt
 *   sich aus dem Zeichen; Ziffernblock wird nur benutzt, wenn der Hauptblock es nicht hat).
 * - Qt-Sondertasten: Return = ET1 (FFH), Escape = DEL LINE (13H, ESC unter CP/A),
 *   Tab = 0FH, Backtab = 0BH, Backspace/Delete = DEL (1BH), Cursor 04H/05H/06H/07H, Home 0CH,
 *   F1–F9 = S1–S9 (C1H–C9H), F10 = S (C0H) [?].
 * - `ctrl`: die K7636.01 hat keine CTRL-Taste; CP/A nimmt ET2 (FEH) als Control-Ersatz — das
 *   Modell stellt ET2 vor den Code [?, ob als Vorsatz oder gehalten].
 *
 * **Zeitverhalten** (BD 4.1; in Maschinentakten zu 2,5 MHz, `service(now_cycles)`): zwei Codes
 * im Ausgaberegister gepuffert, Mindestabstand zweier Codes 21 ms (3 τM), Antwort auf ein
 * Kommando nach 2 ms [?], Dauerfunktion der sechs Listencodes `07 04 05 20 06 5F` nach 500 ms,
 * dann alle 100 ms.  Das „Verschlucken“ bei sehr schneller Eingabe ist die Folge des Puffers
 * (weitere Tasten bei vollem Puffer gehen verloren).
 */
#pragma once
#include "core/cards/k2526/ubus_geraet.h"
#include "core/peripherals/tasten_uhr.h"
#include <cstdint>
#include <deque>

class K7636 : public UBusGeraet {
public:
    /// Kennung einer physischen Taste, wie K7609/K7672 (Rechenadresse in Bit 6…0).
    static constexpr uint32_t QK_TASTE_BASE = 0x03000000;
    static constexpr uint8_t  ADR_SHIFT = 0x7F, ADR_LOCK = 0x77, ADR_CTRL = 0x7E;
    static constexpr uint8_t  TYP = 0x80;           ///< Typzeichen der K7636-Familie (CP/A: `cp 80h`)
    static constexpr uint8_t  STATUS_RUHE = 0x0C;   ///< Port 01H ohne Code: Bit 3 (/UINT) und Bit 2 (kein CTRL) = 1
    static constexpr uint8_t  STATUS_BEREIT = 0x04; ///< Port 01H mit Code: /UINT = 0

    static constexpr uint64_t ZYKLEN_PRO_MS = 2500;
    static constexpr uint64_t ABSTAND_CODES    = 21 * ZYKLEN_PRO_MS;
    static constexpr uint64_t ANTWORT_ZEIT     = 2 * ZYKLEN_PRO_MS;
    static constexpr uint64_t DAUER_VERZOEGERUNG = 500 * ZYKLEN_PRO_MS;
    static constexpr uint64_t DAUER_PERIODE      = 100 * ZYKLEN_PRO_MS;
    static constexpr uint64_t SIGNAL_DAUER       = 1250 * ZYKLEN_PRO_MS;
    static constexpr int      PUFFER_TIEFE = 2;

    K7636() = default;

    // ── UBusGeraet ───────────────────────────────────────────────────────────
    bool ubusRead(uint8_t port, uint8_t& wert) override;
    void ubusWrite(uint8_t port, uint8_t data) override;
    void ubusReset() override { reset(); }

    /// Einschalten / Kommando „Reset“: Grundzustand, danach das Typzeichen.
    void reset(bool kommando = false);

    // ── Bedienseite ──────────────────────────────────────────────────────────
    void keyPress(uint32_t k, bool shift, bool ctrl);
    void keyRelease(uint32_t k);

    /// Zeit fortschreiben, Codes aus dem Puffer ins Ausgaberegister geben, Dauerfunktion.
    /// @return true, wenn ein neuer Code im Register bereitsteht.
    bool service(uint64_t now_cycles);

    /// Zeitbasis der Dauerfunktion (wie K7637): Echtzeit für die Oberfläche.
    void setRepeatRealtime(bool an) { repeat_uhr_.setEchtzeit(an); }
    void setRepeatClockSource(TastenUhr::Quelle q) { repeat_uhr_.setQuelle(q); }

    // ── Anzeigen (für die Bildschirmtastatur und Tests) ──────────────────────
    bool codeBereit() const       { return out_valid_; }
    bool shiftLampe() const       { return lock_ || kommando_shift_; }   ///< LED C99
    bool lockAktiv() const        { return lock_; }
    /// Wahllampe n (0…3 = D58, C58, B58, A58).
    bool wahlLampe(int n) const   { return (led_ >> (n & 3)) & 1; }
    bool insModeLampe() const     { return (led_ & 0x10) != 0; }          ///< LED E15
    bool fehlerLampe() const      { return fehler_; }
    bool signalAktiv() const      { return cur_cycle_ < signal_bis_; }
    unsigned signalZaehler() const { return signale_; }   ///< Anzahl ausgelöster Töne
    unsigned verloren() const     { return verloren_; }   ///< bei vollem Puffer verworfene Tasten

    // ── Tabelle ──────────────────────────────────────────────────────────────
    /// Code der Rechenadresse @p adr in Grund- (false) bzw. Umschaltstellung (true); 0 = keiner.
    static uint8_t code(uint8_t adr, bool umschalt);
    /// Position (z. B. "C01") der Rechenadresse; "" = unbestückt.
    static const char* position(uint8_t adr);
    /// Ist @p code ein Dauerfunktionscode (Liste 07 04 05 20 06 5F)?
    static bool istDauerCode(uint8_t code);
    /// Taste für ein ASCII-Zeichen suchen; @p umschalt = Zeichen steht in Stufe b.
    static bool findeZeichen(uint8_t zeichen, uint8_t& adr, bool& umschalt);

private:
    struct Eintrag { uint64_t frueh; uint8_t code; bool antwort; };

    void einreihen(uint8_t code, uint64_t frueh, bool antwort);
    bool liefere();
    void kommando(uint8_t data);
    bool aufloesen(uint32_t k, bool shift, uint8_t& code) const;

    std::deque<Eintrag> puffer_;
    bool     out_valid_ = false;
    uint8_t  out_code_  = 0xFF;
    uint64_t cur_cycle_ = 0;
    uint64_t naechster_code_ = 0;   ///< frühester Zeitpunkt für den nächsten Tastencode (21 ms)

    bool shift_phys_ = false;       ///< SHIFT-Taste (7FH) gehalten
    bool lock_ = false;             ///< LOCK (77H) eingerastet
    bool kommando_shift_ = false;   ///< Kommando 3: C99 an
    uint8_t led_ = 0;               ///< Bit 4 = E15, Bit 3…0 = Wahllampen A58…D58 (1 = an)
    bool fehler_ = false;
    uint64_t signal_bis_ = 0;
    unsigned signale_ = 0;
    unsigned verloren_ = 0;

    // Dauerfunktion
    uint32_t halte_taste_ = 0;
    uint8_t  halte_code_ = 0;
    bool     repeat_aktiv_ = false;
    int64_t  repeat_rest_ = 0;
    TastenUhr repeat_uhr_{ZYKLEN_PRO_MS * 1000.0};
};
