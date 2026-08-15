/**
 * @file track_sync.h
 * @brief TrackSync — hält ein @ref DiskMedium mit einer ECHTEN Diskette im Gleichstand.
 *
 * Neben der Dateibindung (@ref DiskImage, Container-Codecs) gibt es eine zweite Art,
 * an eine Diskette zu kommen: ein **echtes Laufwerk** an einem Flusswechsel-Adapter
 * (Greaseweazle).  Der Unterschied ist nicht das Medium, sondern die **Körnung** —
 * gelesen und geschrieben wird **spurweise nach Bedarf**, nicht am Stück:
 *
 * @code
 *                                                     ┌── K5122 (Emulator)
 * echte Diskette ◄─gw─► Arbeitsfaden ◄─Aufträge─► DiskMedium
 *                        (Python)                     └── DiskVolume (DiskTool)
 * @endcode
 *
 * **Diese Klasse kennt weder Greaseweazle noch USB noch Python.**  Sie verwaltet
 * Spurzustände und eine Auftragswarteschlange und hat **keinen eigenen Faden**; ein
 * fremder Arbeitsfaden holt sich die Aufträge ab (@ref takeJob) und liefert Bitzellen
 * zurück (@ref completeRead).  Ein anderer Adapter ist damit ein anderer Arbeitsfaden
 * und keine Kernänderung.
 *
 * **Drei Prioritäten** (@ref SyncPriority): Lesen auf Anforderung verdrängt das
 * Zurückschreiben, das wiederum das vorausschauende Lesen verdrängt.  Ein bereits
 * laufender Auftrag wird nie abgebrochen — der Arbeitsfaden steckt dann in einer
 * Übertragung.
 *
 * @see doc/design/14_physische_diskette.md  (voller Feinentwurf)
 * @see doc/design/09_floppy_drive.md §12
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#include "disk_medium.h"
#include "track_image.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/**
 * @struct TrackSyncSpec
 * @brief Was der Synchronisierer über Laufwerk und Diskette wissen muss.
 */
struct TrackSyncSpec {
    uint8_t  num_cyls  = 80;   ///< Reichweite des Laufwerks (K5601 80, K5600.10 40, …)
    uint8_t  num_heads = 2;
    /// Zellrate in kbit/s: 5,25″ DD = 250, 8″ MFM = 500.  Steht auf BEIDEN Seiten —
    /// der Arbeitsfaden taktet damit seine PLL, der Kern codiert damit zurück.
    uint16_t cell_rate_kbps = 250;
    uint16_t rpm            = 300;   ///< nur für Ersatz-Spurlängen beim Schreiben
    /// false = die echte Diskette wird **nie** beschrieben (Vorgabe, s. Entwurf §7).
    bool     writable = false;
    /// Vorschlag für die Verfahrenserkennung je Spur (@ref BitCodec::decodeAuto).
    Encoding default_encoding = Encoding::MFM;
    /// Ruhezeit, die eine geänderte Spur „stillhalten" muss, bevor sie zurückgeschrieben
    /// wird.  Ohne sie schriebe eine einzige UDOS-Dateioperation dieselbe Spur dutzendfach.
    uint32_t write_settle_ms = 500;
    /// Frist für @ref ensureLoaded, bevor der Wartende aufgibt.
    uint32_t request_timeout_ms = 30000;
    bool     read_ahead = true;      ///< unbekannte Spuren in Ruhephasen vorauslesen
};

/// @brief Was ein Auftrag verlangt.
enum class SyncJobKind : uint8_t {
    None  = 0,
    Read  = 1,   ///< Spur von der Diskette lesen
    Write = 2,   ///< Spur auf die Diskette schreiben
    Stop  = 3    ///< kein Auftrag mehr — @ref TrackSync::shutdown wurde gerufen
};

/// @brief Dringlichkeit; kleiner = wichtiger.  Siehe Feinentwurf §5.1.
enum class SyncPriority : uint8_t {
    None      = 0,
    Demand    = 1,   ///< jemand wartet auf diese Spur (@ref TrackSync::ensureLoaded)
    Writeback = 2,   ///< geänderte Spur, zur Ruhe gekommen
    Readahead = 3    ///< unbekannte Spur, niemand wartet
};

/// @brief Ein Auftrag, wie ihn der Arbeitsfaden bekommt.
struct SyncJob {
    uint32_t     id   = 0;                     ///< 0 = kein Auftrag
    SyncJobKind  kind = SyncJobKind::None;
    uint8_t      cyl  = 0;
    uint8_t      head = 0;
    SyncPriority prio = SyncPriority::None;
};

/// @brief Momentaufnahme für die Anzeige.
struct SyncStats {
    uint16_t tracks_total  = 0;
    uint16_t tracks_known  = 0;   ///< schon gelesen (oder geschrieben)
    uint16_t tracks_dirty  = 0;   ///< warten auf Rückführung
    uint16_t tracks_failed = 0;   ///< beim letzten Versuch nicht lesbar
    uint32_t reads_done    = 0;
    uint32_t writes_done   = 0;
    uint32_t errors        = 0;
    uint8_t  busy_kind     = 0;   ///< @ref SyncJobKind des laufenden Auftrags
    uint8_t  busy_cyl      = 255; ///< 255 = gerade nichts zu tun
    uint8_t  busy_head     = 255;
    bool     stopped       = false;
};

/**
 * @class TrackSync
 * @brief Auftragswarteschlange zwischen Abbild und echter Diskette.
 *
 * **Fadensicherheit:** alle öffentlichen Methoden sind fadensicher.  Es darf genau
 * **einen** Arbeitsfaden geben (@ref takeJob weist einen zweiten ab); Vordergrundfäden
 * (Maschine, DiskTool) dürfen beliebig viele sein.
 */
class TrackSync final : public TrackLoader {
public:
    /// @brief Bindet sich an @p medium und erklärt **alle** Spuren für unbekannt.
    TrackSync(const TrackSyncSpec& spec, DiskMedium& medium);
    ~TrackSync() override;

    TrackSync(const TrackSync&)            = delete;
    TrackSync& operator=(const TrackSync&) = delete;

    // ─── Vordergrund (Maschine / DiskTool) ───────────────────────────────────

    /**
     * @brief Spur beschaffen — **blockiert**, bis sie da ist (Priorität 1).
     *
     * Ist die Spur bereits bekannt, kehrt der Aufruf sofort zurück.  Sonst wird ein
     * Auftrag mit @ref SyncPriority::Demand eingestellt (bzw. ein vorhandener
     * heraufgestuft) und gewartet, längstens `spec.request_timeout_ms`.
     *
     * @return false bei Zeitüberschreitung, Lesefehler oder nach @ref shutdown —
     *         der Aufrufer sieht dann die leere (unformatierte) Spur.
     */
    bool ensureLoaded(uint8_t cyl, uint8_t head) override;

    /// @brief Meldung des Mediums: diese Spur wurde geändert → Rückführung anmelden.
    void trackChanged(uint8_t cyl, uint8_t head) override;

    /**
     * @brief Alle noch unbekannten Spuren lesen (Priorität 1, der Reihe nach).
     *
     * Für „Speichern unter…": eine Abbilddatei ist eine Aussage über die **ganze**
     * Diskette, also darf keine Spur unbekannt bleiben (Feinentwurf §4.2).
     *
     * @return false, wenn mindestens eine Spur nicht gelesen werden konnte.
     */
    bool loadAll();

    /**
     * @brief Alle geänderten Spuren sofort einstellen und auf ihre Rückführung warten.
     *
     * Ohne Rücksicht auf die Ruhezeit — für Abmelden/Schließen.  Ist nichts geändert
     * oder die Diskette nicht schreibbar, kehrt der Aufruf sofort zurück.
     *
     * @param timeout_ms Frist; ≤ 0 = @ref TrackSyncSpec::request_timeout_ms.
     * @return false bei Zeitüberschreitung oder Schreibfehler.
     */
    bool flushPending(int timeout_ms = 0);

    // ─── Arbeitsfaden ────────────────────────────────────────────────────────

    /**
     * @brief Nächsten Auftrag abholen — **blockiert**, bis es Arbeit gibt.
     *
     * Die Auswahl wird bei jeder Abholung neu getroffen (kein FIFO): ein
     * @ref SyncPriority::Demand-Auftrag, der während einer Wartezeit eintrifft, kommt
     * vor einem längst angemeldeten @ref SyncPriority::Readahead.
     *
     * @param out        gefüllt mit dem Auftrag; `kind == Stop` nach @ref shutdown
     * @param timeout_ms Wartezeit ohne Arbeit; danach `false` (nichts zu tun)
     * @return true, wenn @p out einen Auftrag trägt (auch `Stop`)
     */
    bool takeJob(SyncJob& out, int timeout_ms);

    /**
     * @brief Zellstrom für einen Schreibauftrag holen (HFE-Konvention, LSB-first).
     * @return false bei unbekanntem @p id.
     */
    bool fetchWrite(uint32_t id, std::vector<uint8_t>& cells, uint32_t& bitcells);

    /**
     * @brief Leseauftrag abschließen: Bitzellen → @ref TrackImage → Medium.
     *
     * @p cells ist der Zellstrom EINER Spurseite in HFE-Konvention (LSB-first je Byte),
     * @p bitcells die Zahl gültiger Zellen.  Das Verfahren (FM/MFM) wird dabei selbst
     * bestimmt (@ref BitCodec::decodeAuto); eine markenlose Spur wird als **leere**
     * (unformatierte) Spur abgelegt, damit der Controller Gap-Flux streamt.
     */
    bool completeRead(uint32_t id, const uint8_t* cells, size_t len, uint32_t bitcells);

    /// @brief Schreibauftrag abschließen; die Spur gilt danach als sauber — es sei denn,
    ///        sie wurde während des Schreibens erneut geändert.
    bool completeWrite(uint32_t id);

    /**
     * @brief Auftrag scheitern lassen.
     *
     * Lesen: die Spur bleibt unbekannt und wird als „gescheitert" vermerkt, damit das
     * Vorauslesen sie nicht endlos wiederholt; der Wartende bekommt die leere Spur.
     * Schreiben: die Spur bleibt **geändert** und wird erneut eingestellt — eine
     * verlorene Änderung wäre der schlimmere Ausgang.
     */
    void failJob(uint32_t id, const std::string& msg);

    /// @brief Endgültig Schluss: @ref takeJob liefert `Stop`, alle Wartenden werden gelöst.
    void shutdown();
    bool stopped() const;

    /**
     * @brief Vom Medium lösen — **bevor** das Medium stirbt.
     *
     * Gerufen aus `~DiskImage`.  Danach scheitert jeder Aufruf des Arbeitsfadens
     * sauber, statt auf ein totes Medium zu greifen: der Faden kann noch mitten in
     * einer Übertragung stecken, wenn die Diskette abgemeldet wird.  Die Sperre macht
     * daraus ein Entweder-Oder statt eines Wettlaufs.
     */
    void detach();

    // ─── Steuerung / Anzeige ─────────────────────────────────────────────────

    void        setReadAhead(bool on);
    bool        readAhead() const;
    SyncStats   stats() const;
    std::string lastError() const;
    const TrackSyncSpec& spec() const { return spec_; }

    /// @brief Ersatz-Spurlänge in Bitzellen (eine Umdrehung bei Zellrate und Drehzahl).
    uint32_t nominalBitcells() const;

private:
    /// Buchführung je Spur.
    struct Eintrag {
        SyncPriority prio    = SyncPriority::None;  ///< None = kein Auftrag angemeldet
        SyncJobKind  kind    = SyncJobKind::None;
        uint32_t     job_id  = 0;                   ///< != 0 = beim Arbeitsfaden
        uint64_t     seq     = 0;                   ///< Eintreffreihenfolge
        bool         failed  = false;               ///< letzter Leseversuch scheiterte
        uint32_t     changes = 0;                   ///< zählt @ref trackChanged
        uint32_t     changes_at_handout = 0;
        std::chrono::steady_clock::time_point dirty_since{};
        bool         dirty_pending = false;
    };

    size_t   idx(uint8_t cyl, uint8_t head) const {
        return static_cast<size_t>(cyl) * spec_.num_heads + head;
    }
    bool     gueltig(uint8_t cyl, uint8_t head) const {
        return cyl < spec_.num_cyls && head < spec_.num_heads;
    }
    /// @brief Sucht den nächsten auszuführenden Auftrag (Sperre gehalten).
    bool     waehleAuftrag(SyncJob& out);
    /// @brief Trägt einen Auftrag für den laufenden Vorgang nach (Sperre gehalten).
    Eintrag* eintragZuAuftrag(uint32_t id, uint8_t& cyl, uint8_t& head);

    TrackSyncSpec spec_;
    /// nullptr nach @ref detach — das Medium ist weg, jeder Zugriff scheitert sauber.
    DiskMedium*   medium_;

    mutable std::mutex      m_;
    std::condition_variable cv_arbeit_;   ///< weckt den Arbeitsfaden
    std::condition_variable cv_fertig_;   ///< weckt die Wartenden im Vordergrund

    std::vector<Eintrag> eintraege_;
    uint64_t  seq_zaehler_ = 0;
    uint32_t  naechste_id_ = 1;
    bool      stop_        = false;
    bool      abholung_laeuft_ = false;   ///< genau ein Arbeitsfaden
    uint8_t   letzter_cyl_ = 0;           ///< Kopfweg-Schätzung für das Vorauslesen
    SyncJob   laufend_{};
    /// Einmal bestimmter Ueberabtastfaktor der Quelle (0 = noch unbekannt, §8.1).
    /// Ausserhalb der Sperre gelesen — daher atomar.
    std::atomic<uint32_t> ueberabtastung_{0};
    SyncStats zaehler_{};
    std::string letzter_fehler_;
};
