/**
 * @file k1520_disk_api.h
 * @brief C-ABI von `libk1520disk.so` — die Schnittstelle des k1520DiskTool zu Python.
 *
 * Stil wie `k1520_api.h` (Emulator): opakes Handle, Index-plus-Getter statt Strukturen
 * ueber die Grenze, `bool` + Fehlertext in Klartext-Deutsch, keine Ausnahmen.
 *
 * **Bewusst getrennt von `libk1520core.so`**: das Werkzeug braucht keinen Z80 und keine
 * Karten, der Emulator keine Dateisysteme.  Beide teilen sich nur die *statischen*
 * Bausteine der Container-/Medium-Schicht.
 *
 * Zeichenketten-Rueckgaben zeigen auf Puffer **im Handle** (bzw. bei den
 * handle-losen Aufrufen auf einen globalen Puffer) und gelten bis zum naechsten
 * Aufruf derselben Funktion.  Wer sie behalten will, kopiert sie.
 *
 * @see doc/design/13_k1520disktool.md §10
 * @author Olaf Krieger
 * @date 2026
 * @license MIT License
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/// @brief Opakes Handle einer geoeffneten Diskette.
typedef void* K1520Disk;

/// @brief Uebertragungsmodus fuer @ref k1520d_extract / @ref k1520d_insert.
typedef enum {
    K1520D_BINARY = 0,   ///< Bytes unveraendert
    K1520D_TEXT   = 1,   ///< Zeilenenden umsetzen (LF ↔ CR LF), 0x1A als Dateiende
} K1520DMode;

/* ─── Oeffnen / Anlegen / Speichern ───────────────────────────────────────── */

/**
 * @brief Diskette oeffnen.
 * @param path       `.img`, `.hfe` oder `.dmk`
 * @param fs_name    Dateisystem erzwingen; NULL oder "" = erkennen
 * @param read_only  **so oeffnen, dass nichts kaputtgehen kann.**  Beim blossen
 *                   Lesen ist das die richtige Wahl; Aendern verlangt danach den
 *                   bewussten Schritt @ref k1520d_set_read_only(h, false).
 * @return Handle oder NULL — Grund dann ueber @ref k1520d_last_open_error.
 */
K1520Disk k1520d_open(const char* path, const char* fs_name, bool read_only);

/**
 * @brief Neue, leere Diskette anlegen (formatieren + Dateisystem initialisieren).
 * @param fs_name  Pflicht — hier gibt es nichts zu erkennen
 * @param label    Datentraegername (UDOS); NULL/"" = Vorgabe
 */
K1520Disk k1520d_create(const char* path, const char* fs_name, const char* label);

/// @brief Aenderungen in die gebundene Datei schreiben (legt beim ersten Mal `<name>~` an).
bool k1520d_flush(K1520Disk h);
/// @brief Unter neuem Namen/Container speichern und **umbinden** (auch bei Schreibschutz).
bool k1520d_save_as(K1520Disk h, const char* path);

/// @brief Kopie in einen anderen Container schreiben, **ohne** umzubinden
///        (Archivierung, Formatumwandlung).  Auch bei Schreibschutz erlaubt.
bool k1520d_export_image(K1520Disk h, const char* path);

/// @brief Schreibschutz abfragen bzw. setzen (Vorgabe beim Oeffnen: geschuetzt).
bool k1520d_read_only(K1520Disk h);
void k1520d_set_read_only(K1520Disk h, bool ro);
/// @brief Sicherungskopie beim ersten Schreiben an-/abschalten (Vorgabe: an).
void k1520d_set_backup(K1520Disk h, bool on);
/// @brief Handle schliessen (schreibt NICHT selbsttaetig — vorher @ref k1520d_flush).
void k1520d_close(K1520Disk h);

/// @brief Grund des letzten fehlgeschlagenen Aufrufs an diesem Handle.
const char* k1520d_last_error(K1520Disk h);
/// @brief Grund, wenn @ref k1520d_open / @ref k1520d_create NULL geliefert hat.
const char* k1520d_last_open_error(void);

/* ─── Katalog und Erkennung ───────────────────────────────────────────────── */

int         k1520d_fs_count(void);
const char* k1520d_fs_name(int i);
const char* k1520d_fs_description(const char* name);
/// @brief Geometrie, auf der dieses Dateisystem liegt.
const char* k1520d_fs_format(const char* name);
/// @brief "cpm" | "udos"
const char* k1520d_fs_type(const char* name);
/// @brief Geladene Katalogdateien (mehrzeilig) + Beanstandungen.
const char* k1520d_catalog_report(void);

/// @brief Erkanntes Dateisystem einer Datei ohne sie offen zu halten; "" = nicht erkannt.
const char* k1520d_detect(const char* path);

const char* k1520d_detected_format(K1520Disk h);
const char* k1520d_detected_fs(K1520Disk h);
/// @brief false = mehrere gleich gute Kandidaten (Alternativen in @ref k1520d_detection_alternatives).
bool        k1520d_detection_unambiguous(K1520Disk h);
const char* k1520d_detection_alternatives(K1520Disk h);
/// @brief Auffaelligkeiten des Mediums (Altbestand, CRC-Fehler, Schaeden); "" = ohne Befund.
const char* k1520d_detection_remarks(K1520Disk h);

/* ─── Seiten (UDOS) ───────────────────────────────────────────────────────────
 * Es wird NICHT umgeschaltet — alle Seiten sind gleichzeitig sichtbar, und jeder
 * Verzeichniseintrag nennt seine Seite (@ref k1520d_entry_volume).            */

int         k1520d_volume_count(K1520Disk h);
/// @brief "" bei einem Dateisystem, sonst "Side0"/"Side1" — auch der Ordnername.
const char* k1520d_volume_dir(K1520Disk h, int v);
const char* k1520d_volume_label(K1520Disk h, int v);
uint64_t    k1520d_volume_total(K1520Disk h, int v);
uint64_t    k1520d_volume_free(K1520Disk h, int v);
uint64_t    k1520d_volume_used(K1520Disk h, int v);

/* ─── Verzeichnis ─────────────────────────────────────────────────────────────
 * IMMER frisch aus dem Medium gelesen — es gibt keinen zwischengespeicherten
 * Verzeichnisstand (doc/design/13_k1520disktool.md §9.3).                     */

/// @brief Verzeichnis aller Seiten einlesen; liefert die Anzahl der Eintraege.
int         k1520d_list(K1520Disk h);
int         k1520d_entry_volume(K1520Disk h, int i);
const char* k1520d_entry_name(K1520Disk h, int i);
int         k1520d_entry_user(K1520Disk h, int i);
uint64_t    k1520d_entry_size(K1520Disk h, int i);
const char* k1520d_entry_type(K1520Disk h, int i);
const char* k1520d_entry_attrs(K1520Disk h, int i);
const char* k1520d_entry_date(K1520Disk h, int i);
bool        k1520d_entry_hidden(K1520Disk h, int i);
bool        k1520d_entry_damaged(K1520Disk h, int i);

/* ─── Uebertragung ───────────────────────────────────────────────────────────
 * `name` darf das Seitenpraefix tragen: "Side1/HELP.DAT.00".                  */

bool k1520d_extract(K1520Disk h, const char* name, const char* dest, K1520DMode mode);
bool k1520d_insert (K1520Disk h, const char* src, const char* name, K1520DMode mode,
                    bool overwrite);
bool k1520d_erase  (K1520Disk h, const char* name);

/**
 * @brief Alles extrahieren.  Bei mehreren Seiten entstehen `Side0/`, `Side1/` …
 */
bool k1520d_extract_all(K1520Disk h, const char* dest_dir, K1520DMode mode);

/**
 * @brief Einen ganzen Ordner einfuegen — transaktional (§9.2).
 *
 * Bei mehreren Seiten MUSS der Ordner genau die `SideN/`-Unterverzeichnisse tragen.
 * Passt der Inhalt nicht, wird **nichts** geschrieben; ein Fehler mittendrin nimmt
 * die ganze Aenderung zurueck.
 */
bool k1520d_insert_all(K1520Disk h, const char* src_dir, K1520DMode mode, bool overwrite);

/// @brief Wuerde @p src_dir passen?  "passt" oder der Grund; schreibt nichts.
const char* k1520d_check_fit(K1520Disk h, const char* src_dir);

/* ─── Zustand ─────────────────────────────────────────────────────────────── */

/// @brief Ungespeicherte Aenderungen im Speicher?
bool        k1520d_dirty(K1520Disk h);
/// @brief Mehrzeiliger Pruefbericht (Datentraeger, Belegung, Auffaelligkeiten).
const char* k1520d_check(K1520Disk h);
const char* k1520d_version(void);

#ifdef __cplusplus
}
#endif
