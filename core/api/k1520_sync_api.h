/**
 * @file k1520_sync_api.h
 * @brief C-ABI der physischen Diskette — Auftragsweg zum Greaseweazle-Arbeitsfaden.
 *
 * Diese Schnittstelle ist **nicht an eine Maschine gebunden**: das k1520DiskTool hat
 * keine.  Sie steht daher in **beiden** Bibliotheken (`libk1520core` und `libk1520disk`)
 * und wird von derselben Übersetzungseinheit erzeugt.
 *
 * ### Wer ruft was
 * @code
 * Vordergrund (Maschine / DiskTool)     Hintergrund (fremder Faden, z. B. Python+gw)
 * ─────────────────────────────────     ───────────────────────────────────────────
 * k1520s_create(spec)          ──┐
 * k1520_mount_physical(...)      │  ┌── k1520s_take_job(t)      blockiert bis Arbeit
 * (arbeiten wie immer)        [Warteschlange]                        │
 *                                │  │                                ▼
 *                                │  │                    Spur lesen/schreiben (gw)
 *                                │  └── k1520s_complete_read/_write ─┘
 * k1520s_shutdown()  ────────────┘      k1520s_fail_job(id, text)
 * @endcode
 *
 * ### Fünf Regeln des Vertrags (doc/design/14_physische_diskette.md §9)
 * 1. Genau **ein** Arbeitsfaden je Handle — ein zweiter @ref k1520s_take_job wird abgewiesen.
 * 2. Der Arbeitsfaden ruft **nur** die Funktionen dieses Kopfes, sonst nichts.
 * 3. Jeder abgeholte Auftrag wird abgeschlossen oder scheitert — sonst wartet der
 *    Vordergrund bis zur Frist.
 * 4. @ref k1520s_take_job ist die **einzige** blockierende Funktion; über `ctypes` gibt
 *    Python dabei die GIL frei.
 * 5. @ref k1520s_shutdown ist endgültig und löst jeden Wartenden.
 *
 * @see doc/design/14_physische_diskette.md §10
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#ifndef K1520_SYNC_API_H
#define K1520_SYNC_API_H

#include "core/api/k1520_export.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Undurchsichtiges Handle einer physischen Diskette samt Auftragsweg.
typedef void* K1520Sync;

/// @brief Was der Kern über Laufwerk und Diskette wissen muss.
typedef struct {
    uint8_t  num_cyls;          ///< Reichweite des Laufwerks (K5601 80, K5600.10 40, …)
    uint8_t  num_heads;
    uint16_t cell_rate_kbps;    ///< 250 (5,25″ DD) / 500 (8″ MFM) — 0 = 250
    uint16_t rpm;               ///< 300 / 360 — 0 = 300
    bool     writable;          ///< false = die echte Diskette wird NIE beschrieben
    uint8_t  default_encoding;  ///< 0 = FM, 1 = MFM (nur ein Vorschlag, s. §8.1)
    bool     read_ahead;        ///< unbekannte Spuren in Ruhephasen vorauslesen
    uint32_t write_settle_ms;   ///< Ruhezeit vor dem Rückschreiben (0 = 500)
    uint32_t request_timeout_ms;///< Frist einer Leseanforderung (0 = 30000)
} K1520SyncSpec;

/// @brief Auftragsart; entspricht `SyncJobKind`.
enum {
    K1520_SYNC_JOB_NONE  = 0,
    K1520_SYNC_JOB_READ  = 1,
    K1520_SYNC_JOB_WRITE = 2,
    K1520_SYNC_JOB_STOP  = 3   /**< kein Auftrag mehr — der Arbeitsfaden soll enden */
};

/// @brief Ein Auftrag für den Arbeitsfaden.
typedef struct {
    uint32_t id;      ///< 0 = kein Auftrag
    uint8_t  kind;    ///< K1520_SYNC_JOB_*
    uint8_t  cyl;
    uint8_t  head;
    uint8_t  prio;    ///< 1 = Anforderung, 2 = Rückführung, 3 = Vorauslesen
} K1520SyncJob;

/// @brief Momentaufnahme für die Anzeige.
typedef struct {
    uint16_t tracks_total;
    uint16_t tracks_known;    ///< schon gelesen (oder geschrieben)
    uint16_t tracks_dirty;    ///< warten auf Rückführung
    uint16_t tracks_failed;   ///< beim letzten Versuch nicht lesbar
    uint32_t reads_done;
    uint32_t writes_done;
    uint32_t errors;
    uint8_t  busy_kind;       ///< laufender Auftrag (0 = gerade nichts)
    uint8_t  busy_cyl;        ///< 255 = nichts
    uint8_t  busy_head;
    bool     stopped;
} K1520SyncStats;

/* ── Lebenszyklus ──────────────────────────────────────────────────────────── */

/**
 * @brief Physische Diskette anlegen — **es wird nichts gelesen**.
 *
 * Alle Spuren gelten als unbekannt; gelesen wird erst beim ersten Zugriff.
 * @return Handle oder NULL bei unbrauchbarer Angabe.
 */
K1520_API K1520Sync k1520s_create(const K1520SyncSpec* spec);

/**
 * @brief Handle freigeben.  Löst vorher @ref k1520s_shutdown aus.
 *
 * **Erst rufen, wenn der Arbeitsfaden beendet ist** — sonst stirbt ihm der
 * Synchronisierer unter der Hand.  Übliche Reihenfolge: @ref k1520s_shutdown,
 * Faden abwarten (`join`), dann @ref k1520s_destroy.
 */
K1520_API void k1520s_destroy(K1520Sync h);

/// @brief Schluss: @ref k1520s_take_job liefert `STOP`, alle Wartenden werden gelöst.
K1520_API void k1520s_shutdown(K1520Sync h);

/* ── Arbeitsfaden ──────────────────────────────────────────────────────────── */

/**
 * @brief Nächsten Auftrag abholen — **blockiert**, bis es Arbeit gibt.
 * @param timeout_ms Wartezeit ohne Arbeit (0 = nur nachsehen)
 * @return true, wenn @p out einen Auftrag trägt (auch `STOP`)
 */
K1520_API bool k1520s_take_job(K1520Sync h, int timeout_ms, K1520SyncJob* out);

/**
 * @brief Zellstrom eines Schreibauftrags abholen (HFE-Konvention, LSB-first je Byte).
 * @return Zahl geschriebener Bytes, oder -1 bei Fehler / zu kleinem Puffer.
 */
K1520_API int k1520s_fetch_write(K1520Sync h, uint32_t id, uint8_t* buf, int buf_len,
                                 uint32_t* bitcells);

/**
 * @brief Leseauftrag abschließen: Bitzellen einer Spurseite abliefern.
 *
 * @p cells in HFE-Konvention (LSB-first je Byte), @p bitcells = Zahl gültiger Zellen.
 * FM/MFM wird dabei selbst erkannt; eine markenlose Spur gilt als unformatiert.
 */
K1520_API bool k1520s_complete_read(K1520Sync h, uint32_t id, const uint8_t* cells,
                                    int len, uint32_t bitcells);

/// @brief Schreibauftrag abschließen.
K1520_API bool k1520s_complete_write(K1520Sync h, uint32_t id);

/// @brief Auftrag scheitern lassen (Lesen: Spur bleibt unbekannt; Schreiben: bleibt fällig).
K1520_API void k1520s_fail_job(K1520Sync h, uint32_t id, const char* msg);

/* ── Steuerung / Anzeige ───────────────────────────────────────────────────── */

K1520_API void        k1520s_set_read_ahead(K1520Sync h, bool on);
K1520_API bool        k1520s_stats(K1520Sync h, K1520SyncStats* out);
K1520_API const char* k1520s_last_error(K1520Sync h);

/// @brief Alle noch unbekannten Spuren lesen (für „Speichern unter…"). Blockiert.
K1520_API bool k1520s_load_all(K1520Sync h);

/// @brief Alle geänderten Spuren sofort zurückschreiben und darauf warten.
K1520_API bool k1520s_flush(K1520Sync h, int timeout_ms);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* K1520_SYNC_API_H */
