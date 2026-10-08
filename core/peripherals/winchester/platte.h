/**
 * @file platte.h
 * @brief Winchesterlaufwerk am WDC des P8000 (ST506, 5 Mbit/s MFM): rohes LBA-Abbild als Medium,
 *        Spursynthese als umlaufender Bytestrom mit Markenkennzeichen, Zerlegung geschriebener
 *        Spuren, Zustand formatiert/unformatiert je Spur, Mechanik (Schritt, SEEK COMPLETE,
 *        TRACK 0, READY).  AP P13b, doc/design/25_p8000.md §10.8, doc/p8000/wdc_firmware.md §7–§10.
 *
 * **Ebene (wdc_firmware.md §9):** die WDC-Firmware sieht nie Bits, sondern Bytes, die die
 * Kartenhardware zwischen Platte und RAM bewegt.  Deshalb ist eine Spur hier ein Ring aus
 * `BYTES_JE_SPUR` Worten: Bit 0–7 = Datenbyte, Bit 8 (`MARKE`) = Marke (A1 mit fehlendem Takt).
 * Position 0 = Indeximpuls.  Die Zeit (6,4 WDC-Takte je Byte) führt die Karte.
 *
 * **Abbild (Berichtigung §10.8 durch P13a §9.5):** ohne Kopf, Sektor (Z, K, S) mit S = Sektornummer
 * 1…n **aus dem Kennfeld** bei Byte `((Z·Köpfe + K)·n + S − 1)·512`; Z0/K0/S1 = PAR/BTT bei 0.
 * Interleave 2:1 und Kopfversatz gehören in die Spursynthese, nicht in die Datei.  Keine `.idmap`.
 *
 * **Spursynthese** (je formatierte Spur, Lage je Kennfeld `SLOT` Byte ab Index, Reihenfolge
 * `sc_tab` der Firmware um den Kopf rotiert, wie `ft_trk` sie schreibt):
 * @code
 *   +0  FF×18   +18 A1* A1* A1*  +21 FE CL CH HD SC CRC CRC   +28 FF×10  +38 00×7
 *   +45 FF×11   +56 A1*  +57 FB  +58 512 Daten  +570 CRC CRC  +572 FF×6         (SLOT = 578)
 * @endcode
 * CRC-CCITT (0x1021, Start FFFF) über Marken + Kennzeichen + Inhalt.  Unformatierte Spur = 00 ohne
 * Marke.  Lage des Datenfelds = wie die Firmware es schreibt: Datenmarke ≈ 35 Byte hinter der
 * letzten Kennfeldmarke (`isr_m2` ≈ 213 Takte + Schreibverzögerung der Karte); die Lese-ISR
 * `isr_m1` braucht ≈ 217 Takte (34 Byte), bis sie auf die Datenmarke wartet.  Der Slot ist so
 * lang, dass ein von der Firmware geschriebenes Datenfeld (≈ 533 Byte) das nächste Kennfeld nicht
 * erreicht.
 *
 * **Zerlegung** (beim Rückschreiben): Kennfeld = Markenfolge + FE + 4 Byte + CRC, gültig mit CRC,
 * passendem Zylinder/Kopf und Sektor 1…n; Datenfeld = Marke + FB + 512 + CRC bis zum nächsten
 * Kennfeld.  Formatiert ⇔ alle n Kennfelder gültig; dann gehen alle gültigen Datenfelder ins
 * Abbild.  Sonst gilt die Spur als unformatiert, das Abbild bleibt unverändert.
 *
 * **Zurückschreiben:** geschriebene Spuren bleiben als Strom im Zwischenspeicher (`eigen`), bis
 * der Kamm den Zylinder verlässt; zerlegt und sektorweise in die Datei geschrieben wird beim
 * Zylinderwechsel, beim Schließen, bei `flush()` und nach einer Schreibpause (`autoFlush`).
 *
 * **Annahmen** (benannt, Messfragen in wdc_firmware.md §12):
 *  - [P1] Zustand „unformatiert" lebt nur im Speicher (und im Save-State); ein vorhandenes
 *         Abbild gilt beim Öffnen als ganz formatiert.
 *  - [P2] Unformatierte Spur = 00 ohne Marke (real: Rauschen) — die Firmware findet keine Marke.
 *  - [P3] READY nach `Config::startzeit_takte`, SEEK COMPLETE `Config::seek_takte` nach dem
 *         letzten Schrittimpuls, Kamm begrenzt auf 0 … Zylinder − 1; kein WRITE FAULT.
 *  - [P4] Spurlänge 10 416 Byte (3600 U/min, 5 Mbit/s), alle Laufwerke ohne Phasenversatz.
 *  - [P5] Ist Z0/K0/S1 kein gültiger PAR-Sektor, die Geometrie aber aus der Typentabelle
 *         bekannt (WEGA-3.1-Abbild für den AVR-Emulator, wdc_firmware.md §11), liefert die
 *         Synthese für diesen Sektor einen erzeugten PAR-Sektor (nur im Speicher), bis die
 *         Firmware ihn selbst schreibt (`Config::par_ergaenzen`).
 */
#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace k1520::winchester {

/// CRC-CCITT (0x1021) wie der CRC-Generator der WDC-Karte (Startwert vom Aufrufer).
uint16_t crcCcitt(uint16_t crc, uint8_t byte);

struct Geometrie {
    uint16_t zylinder = 0;
    uint8_t  koepfe   = 0;
    uint8_t  sektoren = 0;    ///< 17 oder 18 je Spur, je 512 Byte
    uint64_t bytes() const { return uint64_t(zylinder) * koepfe * sektoren * 512u; }
    bool operator==(const Geometrie& o) const {
        return zylinder == o.zylinder && koepfe == o.koepfe && sektoren == o.sektoren;
    }
};

/// Laufwerkstyp mit den PAR-Werten (sa.format Z. 36–40; Werte wdc_firmware.md §7).
struct Typ {
    const char* kennung;      ///< 12 Zeichen Laufwerksbezeichnung im PAR-Sektor
    Geometrie   g;
    uint16_t    vorkomp;      ///< Zylinder der Vorkompensation
    uint8_t     ramp;         ///< rp_mod
    uint8_t     ztk40, ztk41, zmn40, zmx40, zmn41, zmx41;
    const char* name;         ///< kurzer Name für Konfiguration/Text („K5504.50")
    const char* kuerzel = ""; ///< Dateinamenkürzel (klein): `<name>.<kuerzel>.img`, P24
};

/// K5504.50, NEC D5126, NEC D5146, ROBOTRON VS, WEGA-3.1-AVR (1380/10/18).
const std::vector<Typ>& typen();
const Typ* typNachName(const std::string& name);
/// Typ zu einem Dateinamenkürzel (`k5504`, `d5126`, `d5146`, `vs`; Groß-/Kleinschreibung egal).
const Typ* typNachKuerzel(const std::string& kuerzel);
/// Kürzel aus einem Dateinamen: `platte.k5504.img` → `k5504`; sonst "" (Datei ohne Kürzel).
std::string kuerzelAusDateiname(const std::string& pfad);

/// Erzeugt den PAR/BTT-Sektor Z0/K0/S1 (512 B): „DEFEKT", leere BTT mit Endekennung, „PARMTR"…
std::array<uint8_t, 512> parSektor(const Typ& t);
/// Prüft wie die Firmware 4.2 (`t_par` + `t_btt`) und liefert dann die Geometrie.
std::optional<Geometrie> parGeometrie(const uint8_t* sektor);

class Platte {
public:
    static constexpr int      BYTES_JE_SPUR = 10416;
    static constexpr int      SLOT          = 578;
    static constexpr uint16_t MARKE         = 0x100;
    static constexpr int      SEKTOR        = 512;

    struct Config {
        std::optional<Geometrie> geometrie;       ///< fest; sonst PAR-Sektor bzw. Dateigröße
        uint64_t startzeit_takte = 0;             ///< READY nach so vielen Takten ab Öffnen [P3]
        uint64_t seek_takte      = 4000;          ///< SEEK COMPLETE nach letztem Schritt (1 ms @ 4 MHz) [P3]
        uint64_t flush_pause     = 2'000'000;     ///< Schreibpause vor dem Zurückschreiben (0,5 s @ 4 MHz)
        bool     par_ergaenzen   = true;          ///< [P5]
    };

    Platte() = default;
    ~Platte();
    Platte(const Platte&) = delete;
    Platte& operator=(const Platte&) = delete;

    /// Legt ein neues Abbild in voller Größe an: Datenbytes E5, Z0/K0/S1 = PAR/BTT von @p t.
    /// @p mit_par = false: ein UNFORMATIERTES Laufwerk wie neu aus der Verpackung — die Datei ist
    /// durchgehend E5, auch Z0/K0/S1 (kein Parametersatz; der WDC meldet „Error in PAR&BTT",
    /// `sa.format` legt ihn an).
    static bool neu(const std::string& pfad, const Typ& t, std::string* fehler = nullptr, bool mit_par = true);
    /// Geometrie zur Dateigröße (nur eindeutige Typen; 45 342 720 B ist D5146 oder VS ⇒ keine).
    static std::optional<Geometrie> geometrieAusGroesse(uint64_t bytes);

    bool oeffnen(const std::string& pfad, const Config& cfg);
    bool oeffnen(const std::string& pfad);
    void schliessen();
    bool offen() const { return datei_.is_open(); }
    const std::string& fehler() const { return fehler_; }
    const std::string& pfad() const { return pfad_; }
    const Geometrie& geometrie() const { return geo_; }
    bool parErgaenzt() const { return par_ueberlagert_; }
    /// Geometrie aus dem PAR-Sektor der DATEI (Z0/K0/S1), falls dort ein gültiger steht — auch wenn
    /// eine feste Geometrie in `Config` gilt (P24: die Maschine prüft beide gegeneinander).
    const std::optional<Geometrie>& parImAbbild() const { return par_im_abbild_; }

    // ─── Mechanik (Zeitbasis: Takte des Controllers) ─────────────────────────
    bool bereit(uint64_t t) const { return offen() && t >= bereit_ab_; }
    int  zylinder() const { return zyl_; }
    bool spur0() const { return zyl_ == 0; }
    /// Ein Schrittimpuls; Kamm bewegt sich sofort, SEEK COMPLETE erst `seek_takte` später.
    void schritt(bool nach_innen, uint64_t t);
    bool seekFertig(uint64_t t) const { return t >= seek_ende_; }

    // ─── Spur als Bytestrom (aktueller Zylinder) ─────────────────────────────
    uint16_t lies(int kopf, int pos);
    void     schreibe(int kopf, int pos, uint16_t wort, uint64_t t);
    /// Ganze Spur (synthetisiert oder zwischengespeichert) — Test/Debugger.
    const std::vector<uint16_t>& spur(int zyl, int kopf);

    bool formatiert(int zyl, int kopf) const;
    /// Zustand setzen (Prüfungen; neue Abbilder „unformatiert" vorbereiten).
    void setzeFormatiert(int zyl, int kopf, bool f);

    // ─── Sektorzugriff (Abbild, ohne Zwischenspeicher) ───────────────────────
    bool sektorLesen(int zyl, int kopf, int sektor, uint8_t* daten512);
    bool sektorSchreiben(int zyl, int kopf, int sektor, const uint8_t* daten512);

    // ─── Zurückschreiben ─────────────────────────────────────────────────────
    /// Zerlegt alle geänderten Spuren und schreibt sie ins Abbild; Ströme bleiben erhalten.
    void flush();
    /// `flush()`, wenn seit dem letzten Schreiben `flush_pause` Takte vergangen sind.
    bool autoFlush(uint64_t t);
    bool schmutzig() const;

    /// Zerlegt einen Strom (öffentlich für Tests): Sektordaten je gültigem Datenfeld.
    struct Zerlegung {
        bool formatiert = false;
        std::map<int, std::vector<uint8_t>> daten;   ///< Sektornummer → 512 B
        int kennfelder = 0;                           ///< gültige, passende Kennfelder
    };
    Zerlegung zerlege(int zyl, int kopf, const std::vector<uint16_t>& strom) const;
    /// Synthese einer Spur aus dem Abbild (öffentlich für Tests).
    std::vector<uint16_t> synthetisiere(int zyl, int kopf);

    // ─── Save-State: Mechanik, Formatzustand, eigene Spurströme ──────────────
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    struct Spur {
        std::vector<uint16_t> strom;
        bool eigen     = false;    ///< weicht von der Synthese ab (wurde beschrieben)
        bool schmutzig = false;    ///< seit dem letzten Zurückschreiben geändert
    };
    Spur& spurRef(int kopf);
    void  zylinderVerlassen();
    void  zurueckschreiben(int zyl, int kopf, Spur& s);
    /// Sektorinhalt, wie die Synthese ihn liefert (PAR-Überlagerung [P5] eingerechnet).
    bool  sektorSicht(int zyl, int kopf, int sektor, uint8_t* daten512);
    uint64_t offset(int zyl, int kopf, int sektor) const {
        return ((uint64_t(zyl) * geo_.koepfe + uint64_t(kopf)) * geo_.sektoren + uint64_t(sektor - 1)) * SEKTOR;
    }
    size_t spurIndex(int zyl, int kopf) const { return size_t(zyl) * geo_.koepfe + size_t(kopf); }

    std::fstream datei_;
    std::string  pfad_, fehler_;
    Config       cfg_;
    Geometrie    geo_;
    bool         par_ueberlagert_ = false;
    std::optional<Geometrie> par_im_abbild_;
    std::array<uint8_t, SEKTOR> par_{};
    std::vector<uint8_t> unformatiert_;            ///< je Spur 1 = unformatiert [P1]
    std::map<int, Spur> cache_;                    ///< Kopf → Spur des aktuellen Zylinders
    std::vector<uint16_t> fremd_;                  ///< Rückgabepuffer von spur() für andere Zylinder
    int      zyl_        = 0;
    uint64_t seek_ende_  = 0;
    uint64_t bereit_ab_  = 0;
    uint64_t letzt_schreiben_ = 0;
};

}  // namespace k1520::winchester
