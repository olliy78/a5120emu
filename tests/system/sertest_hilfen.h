/**
 * @file sertest_hilfen.h
 * @brief Testgerüst für SERTEST.COM (Entwurf 19 §14.8, AP-ST2): Systemdiskette mit
 *        SERTEST.COM, Kaltstart bis zum Prompt, Bedienung über die Tastatur und das
 *        Einsammeln der Ergebniszeilen (§14.3) — für A5120 (CP/A) und K8915 (SCPX 8915).
 *
 * Alle Maschinen (auch @ref SertestPc1715, PC 1715 mit CP/A 1715, V0.2, und @ref SertestPc1715W,
 * PC 1715W mit SCP 3.0, V0.3) haben dieselbe Oberfläche (@ref SertestA5120, @ref SertestK8915), ein
 * Fall kann also als Schablone über alle laufen:
 *
 * ```
 * SertestA5120 s;                       // TempDisk + SERTEST.COM + Laufwerk A:
 * ASSERT_TRUE(s.kaltstart()) << s.bild();
 * s.tippe("sertest t 2 /a\r");
 * ASSERT_TRUE(s.bisEnde(200'000'000)) << s.bild();
 * EXPECT_EQ(s.protokoll().wert("DFUE/IFSS", "LEITUNGEN-LOOP"), "ENTFAELLT");
 * ```
 *
 * **Ergebniszeilen werden während des Laufs eingesammelt** (@ref SertestProtokoll): jedes
 * `bis…` liest das Bild nach jedem Schritt und merkt sich jede Zeile, die in Spalte 0 mit
 * `SERTEST ` beginnt — eine Zeile, die später aus dem Bild rollt, geht damit nicht verloren.
 *
 * **Bild lesen:** A5120 über das VRAM (`vramText`), K8915 direkt aus der K7024 (dieselbe
 * Quelle wie `k1520_screen_char`) — nie über `memRead`, am K8915 ist 1000H nur bei
 * A8H-Bit0 = 0 für die CPU sichtbar.
 *
 * **Tastatur:** A5120 über `typeString` (K7637, 9600 Bd, kleine Batches à 5 000 Takte);
 * K8915 über `k8915test::tippe` (K7672 im DCP-Modus, Zeichen für Zeichen nach dem Abholen).
 * Ctrl+C: A5120 `typeCtrl(m,'c')` = `keyPress('c', false, true)`, K8915 Zeichen 03H
 * (die K7672 drückt dafür Strg + C).
 *
 * Zwei gekoppelte Maschinen (ST5/ST6): `tests/system/test_sertest_kopplung.cpp`
 * (`Paar`, `koppeln()`) — RFC 2217 Server ↔ Client über Loopback, Port 0, beide
 * Maschinen in EINEM Faden in gleichen Scheiben (`lauf(takte)`) abwechselnd, gedrosselt
 * auf ein Vielfaches der Echtzeit (die Leitungen laufen über den I/O-Faden, also in
 * Uhrzeit), am Ende `stopAlle()`.
 */
#pragma once

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/filesystem/disk_volume.h"
#include "core/filesystem/fs_catalog.h"
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/pc1715/pc1715.h"
#include "core/machines/prg710/prg710.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "tests/support/fixtures.h"
#include "tests/support/keyboard.h"
#include "tests/support/machine_run.h"
#include "tests/support/pc1715_input.h"
#include "tests/support/screen.h"
#include "tests/system/k8915_bedienung.h"
#include "tests/system/prg710_bedienung.h"

#ifndef K1520_SERTEST_COM
#error "K1520_SERTEST_COM fehlt — k1520_add_test(... DEFS K1520_SERTEST_COM=\"…\")"
#endif

namespace sertest {

/// Die eingecheckte `tools/sertest/sertest.com` (CMake-Definition `K1520_SERTEST_COM`).
inline std::string comPfad() { return K1520_SERTEST_COM; }

/// Systemdisketten der beiden Maschinen (Fixtures, immer als TempDisk benutzt).
inline constexpr const char* kDisketteA5120 = "cpa_cpa780_k5601_noclock.img";
inline constexpr const char* kDisketteK8915 = "k8915scpx_cpa800_k5601_bios55k-disk900.hfe";
inline constexpr const char* kDisketteP1715 = "pc1715_cpa1715_boot_4lw.hfe";
inline constexpr const char* kDisketteP1715W = "pc1715w_scp30_system.hfe";
// PRG: prg710test::scpxDiskette(V) — SCPX V1.5 (710) bzw. V1.7 (710-1), bootfähig als Fixture.

/// `SERTEST.COM` mit dem k1520DiskTool (DiskVolume) auf die Diskette @p pfad schreiben.
/// Dateisystem wird erkannt (CP/A bzw. SCPX 8915).  Liefert leer bei Erfolg, sonst den Grund.
inline std::string aufspielen(const std::string& pfad, const std::string& com = "",
                              const std::string& fsName = "") {
    std::string f;
    static const FormatCatalog formate = FormatCatalog::loadDefault(&f);
    static const FsCatalog     fs      = FsCatalog::loadDefault(formate, &f);
    std::string err;
    auto v = DiskVolume::open(pfad, fsName, formate, fs, err, /*read_only=*/false);
    if (!v) return "DiskVolume::open: " + err;
    v->setBackup(false);   // TempDisk räumt nur die Kopie selbst weg, kein `…~` liegen lassen
    TransferOptions o;
    o.overwrite = true;
    if (!v->insert(com.empty() ? comPfad() : com, FileRef::parse("SERTEST.COM"), o)) return "insert: " + v->lastError();
    if (!v->flush()) return "flush: " + v->lastError();
    return {};
}

/// Eine Ergebniszeile nach §14.3, zerlegt.
///   `SERTEST DFUE/V.24 DATEN-LOOP: FEHLER KEIN ECHO BEI 00H` → {DFUE/V.24, DATEN-LOOP, FEHLER KEIN ECHO BEI 00H}
///   `SERTEST ENDE OK`                                     → {"", ENDE, OK}
///   `SERTEST INTERRUPT OK`                                → {"", INTERRUPT, OK}
struct Zeile {
    std::string name, teil, wert, roh;
};

inline std::optional<Zeile> zerlege(const std::string& z) {
    static const std::string kVor = "SERTEST ";
    if (z.compare(0, kVor.size(), kVor) != 0) return std::nullopt;
    Zeile e;
    e.roh = z;
    const std::string rest = z.substr(kVor.size());
    const size_t dp = rest.find(": ");
    if (dp == std::string::npos) {               // ENDE / INTERRUPT: zwei Wörter
        const size_t sp = rest.find(' ');
        if (sp == std::string::npos) return std::nullopt;
        e.teil = rest.substr(0, sp);
        e.wert = rest.substr(sp + 1);
        return e;
    }
    const std::string kopf = rest.substr(0, dp);
    const size_t sp = kopf.rfind(' ');
    if (sp == std::string::npos) return std::nullopt;
    e.name = kopf.substr(0, sp);
    e.teil = kopf.substr(sp + 1);
    e.wert = rest.substr(dp + 2);
    return e;
}

/// Bildzeilen (80 Spalten, rechts ohne Leerzeichen) aus `vramLines`.
inline std::vector<std::string> zeilenAus(const std::string& bild) {
    std::vector<std::string> v;
    size_t a = 0;
    while (a <= bild.size()) {
        size_t e = bild.find('\n', a);
        if (e == std::string::npos) e = bild.size();
        std::string z = bild.substr(a, e - a);
        while (!z.empty() && (z.back() == ' ' || z.back() == '\0')) z.pop_back();
        v.push_back(z);
        a = e + 1;
    }
    return v;
}

/// Sammelt die Ergebniszeilen über den ganzen Lauf (auch herausgerollte).
///
/// Das Bild ist nicht immer eine fertige Seite: die Ausgabe kommt zeichenweise, und das
/// Rollen ist ein Blockverschieben über mehrere zehntausend Takte — dazwischen abgetastet
/// steht in einer Zeile ein Gemisch aus neuem und altem Text
/// (`SERTEST DFUE/IFSS DATEN-LOOPKanal A   (Tastatur)`).  Eine Zeile gilt deshalb erst,
/// wenn sie @ref kStabil Takte lang ununterbrochen im Bild stand (an welcher Stelle, ist
/// gleich — beim Rollen wandert sie).  Eine Ergebniszeile steht bis zum Herausrollen
/// mehrere Millionen Takte da; ein Zwischenstand hält nur einige tausend.
class SertestProtokoll {
public:
    static constexpr long long kStabil = 500'000;

    /// Ein Bild zum Zeitpunkt @p takt (fortlaufender Taktzähler des Laufs) auswerten.
    void erfasse(const std::string& bild, long long takt) {
        std::map<std::string, long long> jetzt;
        for (const std::string& z : zeilenAus(bild)) {
            if (z.compare(0, 8, "SERTEST ") != 0 || gesehen_.count(z)) continue;
            auto it = offen_.find(z);
            const long long seit = it == offen_.end() ? takt : it->second;
            if (takt - seit >= kStabil) {
                if (auto e = zerlege(z)) zeilen_.push_back(*e);
                gesehen_.insert(z);
            } else {
                jetzt[z] = seit;
            }
        }
        offen_.swap(jetzt);
    }
    const std::vector<Zeile>& zeilen() const { return zeilen_; }

    /// Wert der Zeile `<name> <teil>` (bzw. `ENDE` mit leerem Namen), sonst nullopt.
    std::optional<std::string> wert(const std::string& name, const std::string& teil) const {
        for (const Zeile& z : zeilen_)
            if (z.name == name && z.teil == teil) return z.wert;
        return std::nullopt;
    }
    /// `OK` / `FEHLER`, sobald `SERTEST ENDE …` eingesammelt ist.
    std::optional<std::string> ende() const { return wert("", "ENDE"); }

    std::string text() const {
        std::string t;
        for (const Zeile& z : zeilen_) t += z.roh + "\n";
        return t;
    }
    void leeren() { zeilen_.clear(); gesehen_.clear(); offen_.clear(); }

private:
    std::vector<Zeile> zeilen_;
    std::set<std::string> gesehen_;
    std::map<std::string, long long> offen_;   ///< noch nicht stabil: Zeile → seit Takt
};

/// Letzte nicht leere Bildzeile.
inline std::string letzteZeile(const std::string& bild) {
    std::string l;
    for (const std::string& z : zeilenAus(bild))
        if (!z.empty()) l = z;
    return l;
}

/// Steht @p text als ganze Bildzeile da (rechts ohne Leerzeichen verglichen)?
inline bool hatZeile(const std::string& bild, const std::string& text) {
    for (const std::string& z : zeilenAus(bild))
        if (z == text) return true;
    return false;
}

/// Gemeinsamer Teil beider Maschinen (CRTP); die Maschine liefert bild(), schritt(),
/// tippe(), tastaturLeer().
template <class Selbst>
class SertestBasis {
public:
    SertestProtokoll& protokoll() { return protokoll_; }

    /// Läuft, bis @p text im Bild steht (Protokoll wird mitgeführt).
    bool bis(const std::string& text, long long frist) {
        for (long long t = 0;; ) {
            const std::string b = selbst().bild();
            protokoll_.erfasse(b, takt_);
            if (b.find(text) != std::string::npos) return true;
            if (t >= frist) return false;
            t += lauf();
        }
    }
    /// Läuft, bis `SERTEST ENDE …` eingesammelt ist.
    bool bisEnde(long long frist) {
        for (long long t = 0;; ) {
            protokoll_.erfasse(selbst().bild(), takt_);
            if (protokoll_.ende()) return true;
            if (t >= frist) return false;
            t += lauf();
        }
    }
    /// Läuft, bis `A>` die letzte Bildzeile ist und die Tastatur nichts mehr anstehen hat.
    bool bisPrompt(long long frist) {
        for (long long t = 0;; ) {
            const std::string b = selbst().bild();
            protokoll_.erfasse(b, takt_);
            if (letzteZeile(b) == "A>" && selbst().tastaturLeer()) return true;
            if (t >= frist) return false;
            t += lauf();
        }
    }
    /// Kommando am Prompt + CR, dann zurück am Prompt (Echo erst abwarten).
    bool kommando(const std::string& cmd, long long frist = 100'000'000) {
        selbst().tippe(cmd + "\r");
        for (long long t = 0; t < frist && letzteZeile(selbst().bild()) == "A>"; t += lauf()) {}
        return bisPrompt(frist);
    }
    /// Bedienbar? `dir sertest.com` listet die Datei und kehrt zum Prompt zurück.
    bool dirFindetSertest() {
        if (!kommando("dir sertest.com")) return false;
        const std::string b = selbst().bild();
        return b.find("SERTEST  COM") != std::string::npos;
    }

    /// Vor einem weiteren Programmlauf in derselben Sitzung: alte Ergebniszeilen aus dem
    /// Bild rollen (CR am Prompt) und das Protokoll leeren.  Sonst fände `bisEnde` das
    /// alte `SERTEST ENDE …` sofort wieder, und eine gleichlautende neue Zeile gälte als
    /// schon gesehen.
    bool neuerLauf(long long frist = 100'000'000) {
        for (int i = 0; i < 40; ++i) {
            bool alt = false;
            for (const std::string& z : zeilenAus(selbst().bild()))
                if (z.compare(0, 8, "SERTEST ") == 0) alt = true;
            if (!alt) {
                protokoll_.leeren();
                return true;
            }
            selbst().tippe("\r");
            if (!bisPrompt(frist)) return false;
        }
        return false;
    }

    /// Systemtakt der Maschine (φ) in Hz: Zeitbasis für gleich schnell laufende Paare.
    static constexpr double kPhi = 2'457'600.0;

    /// Ein Schritt der Maschine; zählt die Takte für das Protokoll mit.
    long long lauf() { const long long n = selbst().schritt(); takt_ += n; return n; }
    /// Wie lauf(), aber @p takte Takte — zwei gekoppelte Maschinen laufen damit in
    /// gleichen Scheiben Maschinenzeit (die Schrittweiten von A5120 und K8915 sind
    /// verschieden).
    long long lauf(long long takte) {
        const long long n = selbst().maschine().run(takte);
        takt_ += n;
        return n;
    }
    /// Das aktuelle Bild ins Protokoll nehmen (für eigene Laufschleifen).
    void erfasse() { protokoll_.erfasse(selbst().bild(), takt_); }
    long long takt() const { return takt_; }

protected:
    SertestProtokoll protokoll_;
    long long        takt_ = 0;

private:
    Selbst& selbst() { return static_cast<Selbst&>(*this); }
};

/**
 * @brief A5120 mit CP/A (`cpa_cpa780_k5601_noclock.img`) und SERTEST.COM auf A:.
 * Die Diskette hat keine Uhr — der Kaltstart läuft ohne Rückfrage bis `A>`.
 */
class SertestA5120 : public SertestBasis<SertestA5120> {
public:
    /// @p com leer = die eingecheckte V0.2, sonst ein anderes SERTEST.COM (z. B. V0.1).
    explicit SertestA5120(const std::string& com = "", const std::string& fixture = kDisketteA5120)
        : disk_(fixture, "sertest_a5120_" + fixture) {
        fehler_ = aufspielen(disk_.path(), com);
        if (fehler_.empty() && !m_.mountDisk(0, disk_.path(), "cpa780", false))
            fehler_ = "mountDisk: " + m_.lastError();
    }
    /// Leer, wenn Diskette + Laufwerk bereit sind.
    const std::string& fehler() const { return fehler_; }

    bool kaltstart(long long frist = 200'000'000) {
        if (!fehler_.empty()) return false;
        m_.powerOn();
        return bisPrompt(frist);
    }
    void tippe(const std::string& s) {
        for (char c : s) {
            if (c == '\r') k1520test::typeKey(m_, k1520test::QK_RETURN);
            else           k1520test::typeKey(m_, static_cast<uint8_t>(c));
        }
    }
    void ctrlC() { k1520test::typeCtrl(m_, 'c'); }
    std::string bild() { return k1520test::vramLines(m_); }
    long long schritt() { return m_.run(k1520test::kSmallBatch); }
    bool tastaturLeer() { return true; }   // typeKey wartet die Abholzeit selbst ab
    A5120Machine& maschine() { return m_; }

private:
    k1520test::TempDisk disk_;
    A5120Machine        m_;
    std::string         fehler_;
};

/**
 * @brief K8915 mit SCPX 8915 V5.3 (Diskette 900, BIOS „55 K“) und SERTEST.COM auf A:.
 * Kaltstart = `k8915test::kaltstartBisPrompt` (ROM-Selbsttest übersprungen, `<ENTER>`
 * an der Coldstart-Meldung, Autostart `rade` abwarten — vorher Getipptes ginge verloren).
 */
class SertestK8915 : public SertestBasis<SertestK8915> {
public:
    explicit SertestK8915(const std::string& fixture = kDisketteK8915)
        : disk_(fixture, "sertest_k8915_" + fixture) {
        fehler_ = aufspielen(disk_.path());
        if (fehler_.empty() && !m_.mountDisk(0, disk_.path(), "cpa800", false))
            fehler_ = "mountDisk: " + m_.lastError();
    }
    const std::string& fehler() const { return fehler_; }

    bool kaltstart() {
        if (!fehler_.empty()) return false;
        return k8915test::kaltstartBisPrompt(m_);
    }
    void tippe(const std::string& s) { k8915test::tippe(m_, s); }
    void ctrlC() { k8915test::tippe(m_, "\x03"); }
    std::string bild() { return k1520test::vramLines(m_); }
    long long schritt() { return m_.run(k8915test::kSchritt); }
    bool tastaturLeer() { return !m_.keyboard().sendetNoch() && m_.memReadDebug(0xF150) == 0; }
    K8915Machine& maschine() { return m_; }

private:
    k1520test::TempDisk disk_;
    K8915Machine        m_;
    std::string         fehler_;
};

/**
 * @brief PC 1715 mit CP/A 1715 (`pc1715_cpa1715_boot_4lw.hfe`, BIOS 24.05.88) und SERTEST.COM
 * auf A:.  Tastatur über die Tastatur1715 (`pc1715_input.h`: je Taste 150 000 Takte
 * gedrückt + 100 000 Pause), das Bild ohne die CP/A-Statuszeile (Zeile 25).
 * Die Maschine wird mit der Programmversion (@p fixture) der Wahl aufgespielt; SERTEST.COM
 * ist wahlweise die eingecheckte V0.2 oder eine andere (@p com, z. B. die alte V0.1).
 */
class SertestPc1715 : public SertestBasis<SertestPc1715> {
public:
    explicit SertestPc1715(const std::string& com = "", const std::string& fixture = kDisketteP1715)
        : disk_(fixture, "sertest_pc1715_" + fixture) {
        fehler_ = aufspielen(disk_.path(), com);
        if (fehler_.empty() && !m_.mountDisk(0, disk_.path(), m_.defaultFormatName(0), false))
            fehler_ = "mountDisk: " + m_.lastError();
    }
    const std::string& fehler() const { return fehler_; }

    bool kaltstart(long long frist = 200'000'000) {
        if (!fehler_.empty()) return false;
        m_.powerOn();
        return bisPrompt(frist);
    }
    void tippe(const std::string& s) { k1520test::pc1715::tippe(m_, s); }
    void ctrlC() {
        m_.keyPress('c', false, true);
        k1520test::pc1715::laufe(m_, k1520test::pc1715::kHalten);
        m_.keyRelease('c');
        k1520test::pc1715::laufe(m_, k1520test::pc1715::kPause);
    }
    std::string bild() {
        std::string s;
        for (int r = 0; r < 24; ++r) s += k1520test::pc1715::zeile(m_, r) + "\n";
        return s;
    }
    long long schritt() { return m_.run(static_cast<int>(k1520test::pc1715::kBatch)); }
    bool tastaturLeer() { return true; }
    Pc1715Machine& maschine() { return m_; }

private:
    k1520test::TempDisk disk_;
    Pc1715Machine       m_;
    std::string         fehler_;
};

/**
 * @brief PC 1715W mit SCP 3.0 (`pc1715w_scp30_system.hfe`, CP/M 3, 3,9936 MHz) und SERTEST.COM
 * auf A:.  Der Kaltstart ist fertig, wenn PROFILE.SUB (`modcs sc619.zgf[1]`) durch ist und der
 * Prompt `A>` wieder steht.  Tastatur und Bild wie am PC 1715 (`pc1715_input.h`).
 */
class SertestPc1715W : public SertestBasis<SertestPc1715W> {
public:
    static constexpr double kPhi = 3'993'600.0;

    explicit SertestPc1715W(const std::string& com = "", const std::string& fixture = kDisketteP1715W)
        : disk_(fixture, "sertest_pc1715w_" + fixture), m_(konfig()) {
        fehler_ = aufspielen(disk_.path(), com);
        if (fehler_.empty() && !m_.mountDisk(0, disk_.path(), m_.defaultFormatName(0), false))
            fehler_ = "mountDisk: " + m_.lastError();
    }
    const std::string& fehler() const { return fehler_; }

    bool kaltstart(long long frist = 300'000'000) {
        if (!fehler_.empty()) return false;
        m_.powerOn();
        // Die Kommandozeile von PROFILE.SUB steht vor dem zweiten Prompt im Bild.
        return bis("A>modcs sc619.zgf[1]", frist) && bisPrompt(frist);
    }
    void tippe(const std::string& s) { k1520test::pc1715::tippe(m_, s); }
    void ctrlC() {
        m_.keyPress('c', false, true);
        k1520test::pc1715::laufe(m_, k1520test::pc1715::kHalten);
        m_.keyRelease('c');
        k1520test::pc1715::laufe(m_, k1520test::pc1715::kPause);
    }
    std::string bild() {
        std::string s;
        for (int r = 0; r < 24; ++r) s += k1520test::pc1715::zeile(m_, r) + "\n";
        return s;
    }
    long long schritt() { return m_.run(static_cast<int>(k1520test::pc1715::kBatch)); }
    bool tastaturLeer() { return true; }
    Pc1715Machine& maschine() { return m_; }

private:
    static Pc1715Machine::Config konfig() {
        Pc1715Machine::Config c;
        c.variante = Pc1715Machine::Config::Variante::Pc1715W;
        return c;
    }
    k1520test::TempDisk disk_;
    Pc1715Machine       m_;
    std::string         fehler_;
};

/**
 * @brief PRG 710 bzw. 710-1 mit SCPX (V1.5 / V1.7, `prg710test::scpxDiskette`) und SERTEST.COM auf A:.
 * Kaltstart wie die FORMAT-Wächter: Netz ein, 3 Mio. Takte, Return (710-1: ENTER startet das
 * Laden), SCPX-Gruß, `A>`.  Tastatur über `keyPress` (710: K7609/8279, 710-1: K7672 an A32-B —
 * deshalb ist diese Schnittstelle für SERTEST tabu); Ctrl+C = `keyPress('c', false, true)`.
 */
class SertestPrg : public SertestBasis<SertestPrg> {
public:
    using V = prg710test::V;
    explicit SertestPrg(V v, const std::string& com = "")
        : v_(v), disk_(prg710test::scpxDiskette(v), std::string("sertest_prg_") + prg710test::name(v) + ".hfe"),
          m_(konfig(v)) {
        fehler_ = aufspielen(disk_.path(), com, "scpx640");
        if (fehler_.empty() && !m_.mountDisk(0, disk_.path(), m_.defaultFormatName(0), false))
            fehler_ = "mountDisk: " + m_.lastError();
    }
    const std::string& fehler() const { return fehler_; }

    bool kaltstart(long long frist = prg710test::kBoot) {
        if (!fehler_.empty()) return false;
        m_.powerOn();
        prg710test::lauf(m_, 3'000'000);
        prg710test::taste(m_, prg710test::QK_RETURN);
        return prg710test::bisText(m_, prg710test::scpxGruss(v_), frist) && bisPrompt(frist);
    }
    void tippe(const std::string& s) {
        for (char c : s)
            prg710test::taste(m_, c == '\r' ? prg710test::QK_RETURN : static_cast<uint32_t>(uint8_t(c)));
    }
    void ctrlC() {
        m_.keyPress('c', false, true);
        prg710test::lauf(m_, 100'000);
        m_.keyRelease('c');
        prg710test::lauf(m_, 100'000);
    }
    std::string bild() { return prg710test::bild(m_); }
    long long schritt() { return m_.run(prg710test::kSchritt); }
    bool tastaturLeer() { return true; }
    Prg710Machine& maschine() { return m_; }

private:
    static Prg710Machine::Config konfig(V v) {
        Prg710Machine::Config c;
        c.variante = v;
        return c;
    }
    V                   v_;
    k1520test::TempDisk disk_;
    Prg710Machine       m_;
    std::string         fehler_;
};

}  // namespace sertest
