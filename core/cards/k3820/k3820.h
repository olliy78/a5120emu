/**
 * @file k3820.h
 * @brief PFS K3820 (Platine 012-7040/-7041) — Festwertspeicher 16 × 1 KB (Q260 = U555/2708).
 *
 * Quelle: Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf §6
 * (gedr. S. 22–27), ausgewertet in doc/k8915g2/karten.md §3; Plan
 * doc/design/24_k8915_varianten.md AP-V11.
 *
 * **Von keiner Maschine benutzt** (Anwenderwunsch 2026-10-05): die Karte steckt im K8915 V2
 * des Anwenders, wird dort aber nicht angesprochen.  Sie steht im Kern für spätere Geräte
 * (K8911/K8912).  Keine C-ABI, keine Oberfläche.
 *
 * @code
 *   Kapazität   16 KB = 16 Sockel à 1 KB, zusammenhängend ab der Startadresse
 *   X8/X9       Startadresse binär, :1/:2/:3/:4 = 4/8/16/32 KB (alle gebrückt = F000H)
 *   Auswahl     intern = AB12…AB15 − Startadresse (Subtrahierer 74LS83);
 *               AB14K/AB15K = 0 wählt die Karte, AB12K/AB13K + AB10/AB11 den Sockel
 *   X6/X7       welches Signal sperrt: :1 /MEMDI (X1:B09), :2 /MEMDI1, :3 /MEMDI2
 *   X10–X11     offen = WAIT im M1-Zyklus, geschlossen = keine WAIT-Bildung
 * @endcode
 *
 * Sockel-Index = relative Adresse >> 10 (Abb. 7: Sockel „3C00“ = Index 15).  Ein leerer
 * Sockel liest FFH (offener Datenbus bzw. gelöschter 2708 — beides FFH).
 *
 * **Sperre:** die Bus-Leitung /MEMDI ist im Kern ein je Zugriff getriebenes Signal
 * (K1520Bus::MemdiDriver): zieht ein Vorrangspeicher sie, fragt der Bus beim Lesen nur
 * ihn — die Karte bleibt still, ohne selbst etwas tun zu müssen.  Diese Bus-Mechanik
 * unterscheidet NICHT nach Brücken: auch eine Karte mit X6/X7 auf /MEMDI1 oder offen
 * würde dort verdrängt [?, Bus-Grenze; heute zieht nur das EM des A5120.16 /MEMDI].
 * /MEMDI1 und /MEMDI2 sind statische Pegel der Rückverdrahtung (Koppelbus); wer sie
 * führt, meldet sie über @ref setMemdi1 / @ref setMemdi2.  Ist die gebrückte Leitung
 * aktiv, meldet sich die Karte vom Bus ab (Ausgänge hochohmig) und beim Lösen wieder an.
 *
 * Nicht modelliert: RDY (OC-Ausgang), Zugriffszeit, die Wartetakte selbst — die WAIT-Brücke
 * steht nur als Konfiguration (@ref wartetM1) für eine künftige Maschine.
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include <array>
#include <cstddef>
#include <cstdint>

class K3820 : public MemDevice {
public:
    static constexpr int      kChips    = 16;
    static constexpr uint16_t kChipSize = 0x0400;   ///< 1 KB je 2708
    static constexpr uint16_t kGroesse  = 0x4000;   ///< 16 KB

    /** @brief Brücke X6/X7: welches Sperrsignal die Karte abschaltet. */
    enum class MemdiLeitung : uint8_t {
        Memdi,    ///< X6:1–X7:1, /MEMDI (X1:B09) — Normalkonfiguration ≤ 64 KB
        Memdi1,   ///< X6:2–X7:2, /MEMDI1 (X2:A21)
        Memdi2,   ///< X6:3–X7:3, /MEMDI2 (X2:B21)
        Keine,    ///< X6/X7 ganz offen: nie gesperrt
    };

    struct Config {
        uint16_t     startadresse = 0x0000;          ///< X8/X9, Vielfaches von 1000H
        MemdiLeitung memdi        = MemdiLeitung::Memdi;
        bool         wait_m1      = true;            ///< X10–X11 offen = WAIT im M1-Zyklus

        /**
         * @brief Startadresse aus dem Brückenstand X8/X9.
         * @param bruecken Bit 0…3 = :1…:4 gebrückt (Wertigkeit 4/8/16/32 KB).
         *
         * Polarität „gebrückt = Bit gesetzt“ folgt aus der Tabelle S.23 (alle vier
         * Brücken = F000H) [?, nicht am Gerät geprüft, F22].
         */
        static Config ausBruecken(uint8_t bruecken);
    };

    K3820();
    explicit K3820(const Config& cfg);   ///< wirft std::invalid_argument bei krummer Startadresse

    // ─── MemDevice ───────────────────────────────────────────────────────────
    uint8_t memRead(uint16_t addr) override;            ///< absolute Busadresse
    void    memWrite(uint16_t, uint8_t) override {}     ///< EPROM: wirkungslos
    bool    isWritable() const override { return false; }

    /** @brief 16 KB ab Startadresse anmelden (über FFFFH hinaus ab 0000H, s. Kopf). */
    void attachToBus(K1520Bus& bus);

    // ─── Bestückung ──────────────────────────────────────────────────────────
    /** @brief Sockel @p idx (0…15) mit @p len ≤ 1 KB Byte belegen; der Rest bleibt FFH. */
    void setChip(int idx, const uint8_t* data, std::size_t len);
    /** @brief Sockel @p idx leeren (liest FFH). */
    void leereChip(int idx);
    /** @brief Gesamtabzug (bis 16 KB, relativ ab 0000H) auf die Sockel verteilen. */
    void ladeAbbild(const uint8_t* data, std::size_t len);
    bool chipBelegt(int idx) const;
    /** @brief Byte an relativer Adresse @p rel (0…3FFFH), ohne Busweg. */
    uint8_t peek(uint16_t rel) const;

    // ─── Signale ─────────────────────────────────────────────────────────────
    /** @brief Pegel /MEMDI1 der Rückverdrahtung (true = aktiv). */
    void setMemdi1(bool aktiv);
    /** @brief Pegel /MEMDI2 der Rückverdrahtung (true = aktiv). */
    void setMemdi2(bool aktiv);
    /** @brief Ist die Karte gerade über /MEMDI1/2 abgeschaltet? */
    bool gesperrt() const;

    /** @brief Liegt @p addr im Fenster der Karte (unabhängig von der Sperre)? */
    bool belegt(uint16_t addr) const { return uint16_t(addr - cfg_.startadresse) < kGroesse; }
    /** @brief Bildet die Karte im M1-Zyklus WAIT (X10–X11 offen)? */
    bool wartetM1() const { return cfg_.wait_m1; }

    const Config& config() const { return cfg_; }

private:
    void anmelden();
    void sperreNeuBewerten();

    Config     cfg_;
    K1520Bus*  bus_ = nullptr;
    bool       angemeldet_ = false;
    bool       memdi1_ = false;
    bool       memdi2_ = false;
    std::array<std::array<uint8_t, kChipSize>, kChips> chip_{};
    std::array<bool, kChips> belegt_{};
};
