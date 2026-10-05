/**
 * @file machine.h
 * @brief Maschinenneutrale Schnittstelle, über die die C-ABI eine Maschine bedient.
 *
 * Der Kern kennt mehr als eine K1520-Maschine (A5120, K8915 — `K1520MachineType`
 * in `core/api/k1520_api.h`).  `k1520_api.cpp` spricht ausschließlich über diese
 * Klasse; welche Maschine hinter dem Handle steht, entscheidet allein
 * `k1520_create*`.  Enthalten ist **genau** das, was die C-ABI braucht — nicht
 * mehr.  Was einer Maschine eigen ist (beim A5120 die ZVE2, der DMA-Beobachter,
 * der gehaltene Bus, Savestates), bleibt in der konkreten Klasse und wird von
 * Werkzeugen (`k1520dbg`, `boot_trace`) direkt über sie benutzt.
 *
 * @see doc/design/16_k8915.md §7.1
 */

#pragma once
#include "core/bus/k1520_bus.h"
#include "core/cards/k6022/k6022.h"
#include "core/cards/raf/raf.h"
#include "core/peripherals/floppy_drive/disk_image.h"
#include "core/peripherals/floppy_drive/format_catalog.h"
#include "core/serial/hub.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class K1520Machine {
public:
    virtual ~K1520Machine() = default;

    // ─── Lebenslauf ────────────────────────────────────────────────────────────
    virtual void powerOn() = 0;
    virtual void reset() = 0;
    /** @brief Bis zu @p max_cycles Takte ausführen; liefert die verbrauchten Takte. */
    virtual int  run(int max_cycles) = 0;
    /** @brief Anhalten anfordern (wirkt nach dem laufenden Befehl, fadensicher). */
    virtual void stop() = 0;
    /**
     * @brief NMI-Taster: eine /NMI-Flanke an die CPU (fadensicher, zugestellt am
     *        Anfang des nächsten run()).  Vorgabe: keine Wirkung — der A5120 hat
     *        keinen NMI-Taster (die K2526 kennt /NMI nur aus der Q240-Schutzlogik).
     *        K8915: ROM 0066H = Lampen aus + Selbsttest von vorn; unter SCPX liegt
     *        dort RAM — Verhalten wie am Gerät, nicht abgefangen (§3.6, §6.12).
     */
    virtual void nmi() {}

    // ─── Bild ──────────────────────────────────────────────────────────────────
    virtual const uint8_t* framebuffer() const = 0;
    virtual int  fbWidth()  const = 0;
    virtual int  fbHeight() const = 0;
    virtual bool fbDirty()  const = 0;
    virtual void fbClearDirty() = 0;
    virtual void setConsoleMode(bool on) = 0;
    virtual bool consolePoll(int& x, int& y, char& ch) = 0;
    /**
     * @brief Ein Byte des Textbildspeichers (80 × 24, zeilenweise) direkt von der
     *        Bildschirmkarte — NICHT über die CPU-Sicht (`memReadDebug`), die beim
     *        K8915 je nach A8H den Bildspeicher überdeckt (§8a AP-E4b).
     * @return Rohbyte samt Bit 7 (Attribut/Cursor); 0 außerhalb des Bildes.
     */
    virtual uint8_t screenChar(int col, int row) const = 0;

    // ─── Tastatur (fadensicher eingereiht, verbraucht in run()) ───────────────
    virtual void keyPress(uint32_t qt_keycode, bool shift, bool ctrl) = 0;
    virtual void keyRelease(uint32_t qt_keycode) = 0;
    /**
     * @brief Tastenwiederholung in Echtzeit statt in Maschinentakten zählen.
     *
     * Für die Oberfläche: dort ist der Rechnertakt einstellbar, die Tastatur hat
     * aber ihren eigenen Quarz.  Vorgabe aus (Tests/Werkzeuge bleiben
     * wiederholbar).  Fadensicher.  @see core/peripherals/tasten_uhr.h
     */
    virtual void setKeyRepeatRealtime(bool an) = 0;
    /** @brief Anzeigen der Tastatur; Bitbelegung je Tastaturmodell (0 = keine). */
    virtual uint8_t keyboardLeds() const { return 0; }

    // ─── Anzeigen außerhalb des Bildes (AP-E4b; Vorgabe = keine, A5120) ───────
    /** @brief Maschinentyp als Wert von `K1520MachineType` (0 = A5120). */
    virtual int machineType() const { return 0; }
    /** @brief Rohbyte des Anzeigefelds (K8915: Latch 61H, aktiv low); 0 = keins. */
    virtual uint8_t panelLamps() const { return 0; }
    /** @brief Fortlaufender Zähler der Summertöne (die Oberfläche bildet die Differenz). */
    virtual uint32_t bellCount() const { return 0; }

    // ─── Disketten ─────────────────────────────────────────────────────────────
    virtual bool mountDisk(int drive, const std::string& path,
                           const std::string& format_name, bool write_protect) = 0;
    virtual bool mountDiskImage(int drive, std::unique_ptr<DiskImage> img,
                                bool write_protect) = 0;
    virtual bool createDisk(int drive, const std::string& path,
                            const std::string& format_name, bool write_protect) = 0;
    virtual bool saveDiskAs(int drive, const std::string& path,
                            const std::string& format_name) = 0;
    virtual bool unmountDisk(int drive) = 0;
    virtual bool flushDisks() = 0;

    virtual bool isDiskRawCompatible(int drive) const = 0;
    virtual std::string diskPath(int drive) const = 0;
    virtual std::string diskContainer(int drive) const = 0;
    virtual std::string diskNotice(int drive) const = 0;
    virtual std::string detectedFormatName(int drive) const = 0;
    virtual std::string defaultFormatName(int drive) const = 0;
    virtual std::vector<std::string> compatibleFormats(int drive) const = 0;
    virtual std::string formatDescription(const std::string& format_name) const = 0;
    virtual const FormatCatalog& formatCatalog() const = 0;

    virtual bool isDiskActive(int drive) const = 0;
    virtual bool isDiskWriteProtected(int drive) const = 0;
    virtual bool isDiskLedOn(int drive) const = 0;
    virtual bool isMotorOn(int drive) const = 0;
    virtual bool isHeadLoaded() const = 0;
    virtual void setDiskWriteProtect(int drive, bool wp) = 0;

    // ─── Serielle Schnittstellen nach außen (Entwurf 19 §8, AP-S5) ────────────
    /**
     * @brief Der `SerialHub` der Maschine (einer je Maschine): Einstellen, Starten,
     *        Status der einstellbaren Schnittstellen.  Index i = Reihenfolge der
     *        Anmeldung = Reihenfolge von `serielleAnschluesse()` = Index der C-ABI.
     *        nullptr = Maschine ohne Schnittstellen.
     */
    virtual k1520::serial::SerialHub* serialHub() { return nullptr; }
    /// Die einstellbaren Schnittstellen (Anschlüsse der Karten), in Hub-Reihenfolge.
    virtual std::vector<k1520::serial::SerialAnschluss*> serielleAnschluesse() { return {}; }
    /// Fest verdrahtete, NICHT einstellbare Schnittstellen (Tastatur) als Anzeigename,
    /// z. B. "Tastatur K7637 (X4)" — Quelle für `k1520_serial_fixed_name` (Leitsatz 7/8).
    virtual std::vector<std::string> festeSchnittstellen() const { return {}; }

    // ─── Alter Unterbau: DFÜ und Drucker als Rückruf/Einspeisen ───────────────
    // Bleibt für Tests und `k1520_serial_set_rx_cb`/`k1520_serial_send` (Entwurf 19 §8).
    // `set…Callback`: Byte, das der Gast SENDET (in seiner Zeichenzeit, über den
    // Wandler getaktet); `…Send`: Byte, das von AUSSEN im Empfänger ankommt.  Belegt
    // ein Transport oder der Rx/Tx-Loop die Schnittstelle, gehen beide ins Leere.
    // A5120: DFÜ = DFÜ/V.24 (K8025 A33-A), Drucker = K8025 A32-B.
    // K8915: DFÜ = DFÜ/IFSS2 (SIO2-A), Drucker = Drucker/IFSS1 (SIO1-B).
    using SerialCb = std::function<void(uint8_t)>;
    virtual void setDFUECallback(SerialCb cb) = 0;
    virtual void dfueSend(uint8_t byte) = 0;
    virtual void setPrinterCallback(SerialCb) {}
    virtual void printerSend(uint8_t) {}

    // ─── Diagnose ──────────────────────────────────────────────────────────────
    virtual uint8_t memReadDebug(uint16_t addr) = 0;
    virtual void    memWriteDebug(uint16_t addr, uint8_t data) = 0;
    virtual uint8_t ioReadDebug(uint8_t port) = 0;
    virtual std::string lastError() const = 0;

    // ─── RAM-Floppy RAF 128/512/2M (doc/design/22_raf512.md §5.2, AP-R2) ──────
    // In allen drei Maschinen dieselbe Karte auf 88H/89H.  Gesteckt wird NACH dem
    // Anlegen und VOR dem ersten run()/reset() — so braucht die C-ABI keine weitere
    // `k1520_create_*`-Variante je Kombination (EM × RAF × PRG-Variante, §6).  Ein
    // powerOn() davor ist erlaubt: die frische Karte ist im Netz-Ein-Zustand.
    /**
     * @brief Steckt eine RAF der Bauart @p typ auf 88H/89H.
     * @return false (Grund in @ref rafFehler) nach dem ersten run()/reset(), wenn
     *         schon eine RAF steckt oder 88H/89H belegt ist.
     */
    bool installRaf(RAF::Typ typ) {
        raf_fehler_.clear();
        if (!bestueckbar_) {
            raf_fehler_ = "RAF nur vor dem ersten Lauf/Reset steckbar";
            return false;
        }
        if (raf_) {
            raf_fehler_ = "Es steckt bereits eine RAF";
            return false;
        }
        RAF::Config c;
        c.typ = typ;
        auto karte = std::make_unique<RAF>(c);
        try {
            karte->attachToBus(systemBus());   // wirft bei belegtem Port
        } catch (const std::exception& e) {
            raf_fehler_ = e.what();
            return false;
        }
        karte->powerOn();
        raf_ = std::move(karte);
        return true;
    }
    /** @brief Die gesteckte RAF (nullptr = keine). */
    RAF*       raf()       { return raf_.get(); }
    const RAF* raf() const { return raf_.get(); }
    /** @brief Grund des letzten gescheiterten @ref installRaf ("" = keiner). */
    const std::string& rafFehler() const { return raf_fehler_; }

    // ─── Lochstreifen: ADA K6022 / SIF1000 (doc/design/23_lochstreifen.md §3, AP-L1) ──
    // In allen drei Maschinen dieselbe Karte, fest auf E0H–E7H (Stanzer E0H–E3H, Leser
    // E4H–E7H).  Gesteckt wird wie die RAF NACH dem Anlegen und VOR dem ersten
    // run()/reset().  Die Maschine hängt beide PIOs in ihre Interruptkette
    // (@ref k6022Einketten), taktet die Karte im Laufweg (@ref k6022Takt) und setzt sie
    // beim systemweiten /RESET zurück (@ref k6022Reset).  Kein Teil des Save-State (§2).
    /**
     * @brief Steckt die K6022 auf E0H–E7H.
     * @return false (Grund in @ref k6022Fehler) nach dem ersten run()/reset(), wenn
     *         schon eine K6022 steckt oder eines der Tore E0H–E7H belegt ist.
     */
    bool installK6022() {
        k6022_fehler_.clear();
        if (!bestueckbar_) {
            k6022_fehler_ = "K6022 nur vor dem ersten Lauf/Reset steckbar";
            return false;
        }
        if (k6022_) {
            k6022_fehler_ = "Es steckt bereits eine K6022";
            return false;
        }
        // Erst prüfen, dann anmelden: registerIO wirft erst am belegten Tor und ließe
        // die davor angemeldeten auf eine Karte zeigen, die es gleich nicht mehr gibt.
        K1520Bus& bus = systemBus();
        for (int p = K6022_BASIS; p < K6022_BASIS + 8; ++p) {
            if (const BusDevice* d = bus.ioOwner(static_cast<uint8_t>(p))) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "E/A-Tor %02XH belegt (%s)", p, d->deviceName());
                k6022_fehler_ = buf;
                return false;
            }
        }
        auto karte = std::make_unique<K6022>(k6022TaktHz());
        karte->attachToBus(bus);
        k6022_ = std::move(karte);
        k6022Einketten(*k6022_);
        bus.markIntDirty();
        return true;
    }
    /** @brief Die gesteckte K6022 (nullptr = keine). */
    K6022*       k6022()       { return k6022_.get(); }
    const K6022* k6022() const { return k6022_.get(); }
    /** @brief Grund des letzten gescheiterten @ref installK6022 ("" = keiner). */
    const std::string& k6022Fehler() const { return k6022_fehler_; }
    /// Feste Basis der K6022 in allen Maschinen (Entwurf 23 §2).
    static constexpr uint8_t K6022_BASIS = 0xE0;

protected:
    /** @brief Der Systembus, auf dem Zusatzkarten (RAF) ihre E/A-Tore anmelden. */
    virtual K1520Bus& systemBus() = 0;
    /** @brief Aus run() und reset(): ab hier ist die Bestückung fest. */
    void bestueckungAbschliessen() { bestueckbar_ = false; }
    /** @brief Netz-Ein der RAF: Inhalt verworfen (ohne Stand-by-Pufferung, §3.4). */
    void rafPowerOn() { if (raf_) raf_->powerOn(); }
    /** @brief Systemweiter /RESET: nur das Latch sperrt, der Inhalt bleibt (§3.4). */
    void rafReset() { if (raf_) raf_->reset(); }

    /** @brief CPU-Takt für die Zeichenzeiten der K6022 (alle drei Maschinen: 2,4576 MHz). */
    virtual uint32_t k6022TaktHz() const { return 2'457'600; }
    /**
     * @brief Die PIOs der frisch gesteckten K6022 in die Interruptkette aufnehmen.
     *        Vorgabe: HINTEN anhängen, Stanzer vor Leser (Entwurf 23 §2).  Eine Maschine
     *        mit eigener Stellung (PRG 710: vor dem Fernschreiber) setzt ihre Kette neu.
     */
    virtual void k6022Einketten(K6022& k) {
        systemBus().appendInterruptChain(&k.pioStanzer());
        systemBus().appendInterruptChain(&k.pioLeser());
    }
    /** @brief Laufweg: Karte fortschreiben; true = Interruptzustand evtl. geändert. */
    bool k6022Takt(int takte) { return k6022_ && k6022_->clockTick(takte); }
    /** @brief Systemweiter /RESET: PIOs zurück; Band und Stanzband bleiben. */
    void k6022Reset() { if (k6022_) k6022_->reset(); }

    /// Erst NACH Bus und Karten der abgeleiteten Klasse zerstört — unschädlich, die
    /// RAF fasst den Bus in ihrem Destruktor nicht an.
    std::unique_ptr<RAF> raf_;
    /// Wie raf_: nach Bus und Karten der abgeleiteten Klasse zerstört; die K6022 fasst
    /// den Bus im Destruktor nicht an (Bus samt Kette sind dann schon fort).
    std::unique_ptr<K6022> k6022_;

private:
    bool        bestueckbar_ = true;
    std::string raf_fehler_;
    std::string k6022_fehler_;
};
