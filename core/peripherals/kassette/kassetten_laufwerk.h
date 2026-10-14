/**
 * @file kassetten_laufwerk.h
 * @brief Kassettenlaufwerk (K5200, LW1200/SK1200, PK-3) über einem @ref KassettenMedium
 *        (doc/design/29_kassette.md §5, doc/kassette/k5200.md, doc/kassette/treiber.md).
 *
 * Das Laufwerk kennt **keine Karte**: es nimmt die Leitungen der Schnittstelle IFKB als Aufrufe
 * entgegen (`reservieren`, `anwaehlen`, `vor`, `rueck`, `hge`, `aufzeichnen`, `wiedergabe`,
 * `umspulen`) und meldet die Rückleitungen (`nib`, `kng`, `schreibsperre` = AVE, `aebAktiv`,
 * `bms`, `seiteB`, `azv38`).  Pegelumsetzung (PIO-Bit ↔ IFKB), Auswahl zwischen den zwei Laufwerken
 * und das Verhalten der Karte (Präambelkontrolle, `STA`, `ZF`, `HGE` nach `AUF` = ohne Echo) sind
 * Sache der K5020 (AP K4).  Hohe Pegel sind hier immer „wahr“; @ref aebAktiv ist wahr, wenn
 * Vorspann oder Marke unter der Lichtschranke liegt (am Laufwerk low-aktiv, an Bit 7 der
 * Steuer-PIO A deshalb 0; treiber.md §3.1).
 *
 * **Zeit.**  Das Laufwerk lebt in Maschinenzeit: @ref clockTick (Takte) bzw. @ref advance
 * (Sekunden).  Die Bandposition läuft aus Anlauf-/Bremsrampen und Geschwindigkeit (k5200.md §6):
 * RES → bereit nach `t_anlauf` (300 ms), Anlauf auf Arbeitsgeschwindigkeit in `t_start`, Bremsen
 * auf `s_stopp` (4 mm), Suchlauf `HGE` mit `v_such` (1,5 m/s), `UMS` spult selbsttätig bis zum
 * Vorspann.  Nicht nachgebildet (ohne Wirkung für die Software): Sperrzeiten T5/T8, Magnet-/Bremsimpulse.
 *
 * **Daten.**  Bei Arbeitsgeschwindigkeit liefert das Laufwerk die Bytes unter dem Kopf im Takt der
 * Bandlänge (ein Byte, wenn das Bandstück dafür am Kopf vorbei ist): vorwärts in Schreibrichtung,
 * rückwärts als `~bitrev(b)` in umgekehrter Folge (treiber.md §2.4).  Ein Block wird nur gelesen,
 * wenn der Kopf seinen Anfang (rückwärts: sein Ende) überfährt — wer mitten im Block anläuft, sieht
 * ihn nicht.  Mit `AUF` löscht der Kopf, was er überfährt, und `schreibeByte` legt die Bytes ab der
 * Kopfstelle als Block ab; mit `wiedergabe` kommen sie 3,81 mm (≈ 10 ms) später als Echo zurück
 * (Lese-nach-Schreiben).  Ein Block gilt als zu Ende, wenn ≥ 2 Bytelängen kein Byte kam, `AUF` fällt
 * oder das Band steht.  Gelesene/Echo-Bytes holt die Karte mit @ref holeByte.
 *
 * **Datei.**  Optional ist das Medium an eine `.k52` gebunden (@ref dateiBinden); Änderungen schreibt
 * @ref autoFlush nach ≥ 0,5 s Maschinenzeit ohne weitere Änderung zurück (Vorbild `K6022::autoFlush`),
 * sofort bei Auswerfen/Lösen/@ref speichern.
 *
 * **Faden:** alles aus dem Lauffaden (bzw. unter der Kernsperre); es gibt keine eigene Sperre.
 */

#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/peripherals/kassette/kassetten_medium.h"

namespace kassette {

/// Laufwerksmodelle mit ihren Zeiten.  Werte aus k5200.md §6 (K5200), doc/mc80/hardware_3x.md (PK-3),
/// MC80.2x-Handbuch §3.1.8 (SK 1200); `[?]` im Kommentar = nicht belegt, begründete Annahme.
enum class ProfilId : int { K5200_38 = 0, K5200_19 = 1, LW1200 = 2, PK3 = 3, SK1200 = 4 };

struct LaufwerksProfil {
    ProfilId id;
    const char* name;
    double v_arbeit_mm_s;   ///< Schreib-/Lesegeschwindigkeit
    double v_such_mm_s;     ///< Suchlauf (`HGE`) und Umspulen
    double bitrate_bit_s;   ///< nominale Bitrate bei v_arbeit (Bittakt der Karte)
    double t_anlauf_s;      ///< RES → bereit (T1)
    double t_start_s;       ///< Anlauf auf Arbeitsgeschwindigkeit
    double s_stopp_mm;      ///< Bremsweg aus Arbeitsgeschwindigkeit
    double t_hge_s;         ///< Beschleunigen auf/Bremsen von v_such (T7: ≤ 200 ms)
    double spalt_mm;        ///< Abstand Schreib-/Lesespalt (Echo)
    double aeb_vorlauf_mm;  ///< Lichtschranke liegt so weit vor der Kopfmitte (Vorwärtsrichtung)
    bool azv_38;            ///< Leitung AZV niedrig (= 38 cm/s)

    double byteZeitS() const { return 8.0 / bitrate_bit_s; }
};

/// Das Profil zu @p id; nie null.
const LaufwerksProfil& profil(ProfilId id);
/// Profil nach Name („K5200-38“, „K5200-19“, „LW1200“, „PK3“, „SK1200“; Groß-/Kleinschreibung egal) oder nullptr.
const LaufwerksProfil* profilNachName(const std::string& name);
std::vector<ProfilId> alleProfile();

class KassettenLaufwerk {
public:
    /// @p takt_hz: Maschinentakt, nur für @ref clockTick (A5120: 2 457 600).
    explicit KassettenLaufwerk(ProfilId p = ProfilId::K5200_38, double takt_hz = 2'457'600.0);

    const LaufwerksProfil& profil() const { return *profil_; }
    void setTakt(double hz) { takt_hz_ = hz; }

    // ── Kassette ──
    /// Kassette einlegen (false, wenn schon eine liegt oder @p m null ist).
    bool kassetteEinlegen(std::shared_ptr<KassettenMedium> m);
    /// Kassette herausnehmen — nur bei nicht reserviertem Gerät und stehendem Band (Entladetaste
    /// ist mechanisch gesperrt, k5200.md §2.1).  Eine gebundene Datei wird vorher zurückgeschrieben.
    /// @p erzwingen: auch auswerfen, wenn das Zurückschreiben der gebundenen Datei scheitert
    /// (die Änderungen gehen dann verloren; @ref letzterDateifehler sagt warum).
    bool kassetteAuswerfen(bool erzwingen = false);
    std::shared_ptr<KassettenMedium> medium() const { return medium_; }
    /// Kopfstelle setzen (Bedienung/Tests; Seitenkoordinate in µm).  Nur bei stehendem Band.
    bool positionSetzen(int64_t um);
    /// Kassette wenden (nur nicht reserviert, Band steht).
    bool wenden();

    // ── Bindung an eine `.k52` ──
    /**
     * @brief Datei als Kassette einlegen.  Gibt es sie nicht, entsteht eine leere (und wird sofort
     *        angelegt); sonst wird sie gelesen.  false und @p fehler bei Lesefehler, liegender
     *        Kassette oder nicht schreibbarem Ort.
     */
    bool dateiBinden(const std::string& pfad, std::string& fehler);
    /// Bindung lösen (die Kassette bleibt im Laufwerk) — vorher zurückschreiben.
    bool dateiLoesen(std::string& fehler);
    bool gebunden() const { return !pfad_.empty(); }
    const std::string& pfad() const { return pfad_; }
    /// Sofort zurückschreiben (ohne Pause abzuwarten).  Ohne Bindung/ohne Änderung: true.
    bool speichern(std::string& fehler);
    /// Fällige Datei schreiben (einmal je run()-Scheibe).  @return true, wenn geschrieben wurde.
    bool autoFlush();
    std::string letzterDateifehler() const { return datei_fehler_; }

    // ── Leitungen vom Rechner (IFKB) ──
    void reservieren(bool an);   ///< RES
    void anwaehlen(bool an);     ///< AWA
    void vor(bool an);           ///< VOR (TRV)
    void rueck(bool an);         ///< RCK (TRR)
    void hge(bool an);           ///< HGE (Suchlauf, nur mit VOR/RCK wirksam)
    void aufzeichnen(bool an);   ///< AUF
    void wiedergabe(bool an);    ///< WID — Vorgabe an (fest verdrahtet, k5200.md §2.3 [?])
    /// UMS: Rückspulen bis zum Vorspann starten.  Wirkt nur bei RES, AWA und bereitem Laufwerk;
    /// wird im Gerät gespeichert und endet selbsttätig (RES/AWA dürfen danach fallen).
    void umspulen();

    // ── Leitungen zum Rechner ──
    bool nib() const { return !bereit(); }       ///< nicht bereit (hoch)
    bool bereit() const;
    bool kng() const;                            ///< Kassette nicht geladen/arbeitsbereit (hoch)
    bool kassetteGeladen() const { return medium_ != nullptr; }
    bool reserviert() const { return reserviert_; }
    bool angewaehlt() const { return angewaehlt_; }
    bool schreibsperre() const;                  ///< AVE: Aufzeichnen verboten (Seite schreibgeschützt)
    bool seiteB() const;                         ///< KSB
    bool azv38() const { return profil_->azv_38; }
    /// `AEB`: Vorspann/BOT-/EOT-Marke unter der Lichtschranke; bei laufendem Umspulen ausgeblendet.
    bool aebAktiv() const;
    /// `BMS`/Analogsignal: der Kopf steht über einem Block und das Band läuft (rein geometrisch;
    /// den Abfall nach 108 µs ohne Flusswechsel bildet die Karte).
    bool bms() const;
    bool umspulenAktiv() const { return ums_; }
    bool fehler() const { return fehler_; }      ///< VOR und RCK gleichzeitig (NIB und KNG hoch bis RES fällt)

    // ── Daten ──
    /// Byte von der Karte (Daten-PIO A) aufs Band.  false, wenn nicht geschrieben werden kann
    /// (nicht aufzeichnungsbereit, Schreibsperre, Band nicht auf Arbeitsgeschwindigkeit vorwärts).
    bool schreibeByte(uint8_t b);
    /// Nächstes gelesenes/Echo-Byte, falls eines am Kopf vorbeigekommen ist.
    std::optional<uint8_t> holeByte();
    size_t bytesBereit() const { return rx_.size(); }

    // ── Zeit ──
    void clockTick(int takte);
    void advance(double sekunden);

    // ── Zustand für Oberfläche/Tests ──
    double positionMm() const { return pos_um_ / 1000.0; }
    double geschwindigkeitMmS() const { return v_mm_s_; }
    bool bewegt() const { return v_mm_s_ != 0.0; }
    /// Anzahl Blöcke der aktiven Seite, die der Kopf schon hinter sich hat (Blockzähler der Anzeige).
    size_t blockNummer() const;

private:
    void schritt(double dt);
    double rampe(double v, double ziel) const;
    void integriere(double ziel, double dt);
    void datenSchritt(int64_t p0, int64_t p1);
    void leseNeu();
    bool schreibeErlaubt() const;
    void schreibblockAbschliessen();
    void rxPush(uint8_t b);
    void pauseUndFlush(double dt);
    bool dateiSchreiben(std::string& fehler);

    const LaufwerksProfil* profil_;
    double takt_hz_;
    double tick_rest_ = 0.0;

    std::shared_ptr<KassettenMedium> medium_;
    std::string pfad_;
    uint64_t rev_gespeichert_ = 0, rev_gesehen_ = 0;
    double pause_s_ = 0.0;
    bool flush_faellig_ = false;
    std::string datei_fehler_;

    bool reserviert_ = false, angewaehlt_ = false, vor_ = false, rueck_ = false, hge_ = false;
    bool auf_ = false, wid_ = true, ums_ = false, fehler_ = false;
    double t_motor_ = 0.0;
    double pos_um_ = 0.0;   ///< Kopfstelle in Koordinaten der aktiven Seite
    double v_mm_s_ = 0.0;   ///< Bandgeschwindigkeit, vorwärts positiv

    // Lesemaschine (siehe .cpp)
    struct Lesen {
        bool aktiv = false;
        int richtung = 0;
        int64_t anker = 0;
        std::vector<uint8_t> folge;
        size_t n = 0;
        int64_t grenze_vor = INT64_MIN, grenze_rueck = INT64_MAX;
    } rd_;
    std::deque<uint8_t> rx_;

    // Schreibmaschine
    struct Schreiben {
        bool aktiv = false;
        int64_t start = 0;
        std::vector<uint8_t> bytes;
    } wr_;
    std::deque<std::pair<int64_t, uint8_t>> echo_;  ///< (Kopfstelle, ab der das Byte am Lesespalt ist), Byte
};

}  // namespace kassette
