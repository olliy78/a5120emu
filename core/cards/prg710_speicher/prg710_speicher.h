/**
 * @file prg710_speicher.h
 * @brief Speicherverwaltung E8H–EBH + 64-KB-OPS-RAM des PRG 710 / PRG 710-1.
 *
 * Quelle: doc/design/20_prg710.md §4, §4b (Arbeitsmodell Punkte 1–4), §7.2, AP-P1b.
 * **Hypothese [?]**: welche Karte die Register wirklich trägt, ist offen (§8.1); der
 * Name wird umgestellt, sobald der Kartenbefund ihn nennt.
 *
 * @code
 *   E8H  je Seite n (A12–A15 der E/A-Adresse): Herkunft
 *        unteres Halbbyte ≠ 0 → Systemkarte (Seite 0: ZRE 0000–0FFF, Seite F: VRAM F800–FFFF)
 *        unteres Halbbyte = 0 → OPS-RAM            (belegt: 0FH, 10H, F0H, FFH)
 *   E9H  unbelegt: Schreiben wird protokolliert, Lesen liefert FFH
 *   EAH  je Seite n: physische OPS-Seite (4 Bit)
 *   EBH  Freigabe: 0 = Abbildung aus (Grundzustand), ≠ 0 = Register wirken
 * @endcode
 *
 * Die Speicherverwaltung kennt weder die K2521 noch die K7024: ZRE- und VRAM-Zugriffe
 * gehen über zwei von außen gesetzte Zugriffswege (die Maschine verdrahtet sie).
 * Gelesen werden die Register nirgends (ROM und UDOS schreiben nur).
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstdint>
#include <functional>

class Prg710Speicher : public BusDevice {
public:
    /** @brief Wer bedient eine Adresse? */
    enum class Quelle : uint8_t { Zre, Vram, Ops, Leer };
    struct Ort {
        Quelle   quelle;
        uint16_t offset;   ///< Zre/Vram: Adresse wie die CPU sie sieht; Ops: Offset im OPS-RAM
    };

    using LeseFn    = std::function<uint8_t(uint16_t)>;
    using SchreibFn = std::function<void(uint16_t, uint8_t)>;

    static constexpr uint8_t  PORT_BASIS    = 0xE8;     ///< E8H–EBH
    static constexpr uint16_t VRAM_ANFANG   = 0xF800;   ///< Sichtfenster der K7024 (Seite F)
    static constexpr uint16_t ZRE_ENDE      = 0x1000;   ///< ZRE belegt 0000–0FFF (Seite 0)

    explicit Prg710Speicher(K1520Bus& bus) : bus_(bus) { reset(); }

    // ─── BusDevice (E8H–EBH) ─────────────────────────────────────────────────
    uint8_t     ioRead(uint8_t port) override;
    void        ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return "PRG710-Speicherverwaltung"; }

    /** @brief Ports E8H–EBH am Bus anmelden. */
    void attachToBus(K1520Bus& bus) { bus.registerIO(this, PORT_BASIS, 4); }

    // ─── Lebenslauf ──────────────────────────────────────────────────────────
    /** @brief /RESET: alle Register 0 (Abbildung aus); OPS-RAM bleibt. */
    void reset();
    /** @brief Netz-Ein: OPS-RAM mit @p fill belegen, dann reset(). */
    void powerOn(uint8_t fill = 0x00);

    // ─── Zugriffswege (von der Maschine gesetzt) ─────────────────────────────
    void setZreWeg(LeseFn lesen, SchreibFn schreiben)  { zre_r_ = std::move(lesen); zre_w_ = std::move(schreiben); }
    void setVramWeg(LeseFn lesen, SchreibFn schreiben) { vram_r_ = std::move(lesen); vram_w_ = std::move(schreiben); }

    // ─── Speicherbild ────────────────────────────────────────────────────────
    /** @brief Was liegt unter @p addr im aktuellen Speicherbild? */
    Ort     ortVon(uint16_t addr) const;
    /** @brief Speicherzugriff aus Sicht der CPU (Leer liest FFH, schreibt ins Leere). */
    uint8_t memRead(uint16_t addr);
    void    memWrite(uint16_t addr, uint8_t data);

    /** @brief Direkter Blick in den OPS-RAM (Tests, Debugger). */
    uint8_t opsPeek(uint16_t a) const        { return ops_[a]; }
    void    opsPoke(uint16_t a, uint8_t v)   { ops_[a] = v; }

    // ─── Diagnose (Debugger `map`) ───────────────────────────────────────────
    uint8_t attr(int seite) const    { return attr_[seite & 15]; }
    uint8_t seite(int seite) const   { return seite_[seite & 15]; }
    uint8_t freigabe() const         { return freigabe_; }

private:
    K1520Bus& bus_;
    std::array<uint8_t, 0x10000> ops_{};
    std::array<uint8_t, 16> attr_{};    ///< E8H je Seite
    std::array<uint8_t, 16> seite_{};   ///< EAH je Seite (4 Bit)
    uint8_t freigabe_ = 0;              ///< EBH
    LeseFn    zre_r_, vram_r_;
    SchreibFn zre_w_, vram_w_;
};
