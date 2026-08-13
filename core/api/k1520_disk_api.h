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

#include "core/api/k1520_export.h"   ///< `K1520_API` — Ausfuhrkennzeichnung (Windows!)

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
K1520_API K1520Disk k1520d_open(const char* path, const char* fs_name, bool read_only);

/**
 * @brief Neue, leere Diskette anlegen (formatieren + Dateisystem initialisieren).
 * @param fs_name  Pflicht — hier gibt es nichts zu erkennen
 * @param label    Datentraegername (UDOS); NULL/"" = Vorgabe
 */
K1520_API K1520Disk k1520d_create(const char* path, const char* fs_name, const char* label);

/**
 * @brief Wie @ref k1520d_create, aber mit **Bootabbild** in den Systemspuren.
 *
 * Bootfaehig wird eine Diskette durch die Spuren VOR dem Dateisystem — das Lade-ROM
 * liest Spur 0 blind, lange bevor es ein Dateisystem gibt.  @p boot_image ist eine
 * rohe `.bin`-Datei mit genau diesem Byteband (herausholen laesst es sich aus einer
 * vorhandenen Diskette: @ref k1520d_read_boot_image).
 *
 * Passt die Datei nicht in die Systemspuren, wird **gar nichts angelegt** — der Grund
 * steht dann in @ref k1520d_last_open_error.
 *
 * @param boot_image NULL oder "" = gewoehnliche, nicht bootfaehige Diskette
 */
K1520_API K1520Disk k1520d_create_bootable(const char* path, const char* fs_name,
                                           const char* label, const char* boot_image);

/* ─── Bootabbild (Systemspuren) ───────────────────────────────────────────── */

/**
 * @brief Wie viele Byte fassen die Systemspuren dieses Dateisystems?
 *
 * Ohne Diskette zu beantworten — die Oberflaeche kann damit schon bei der Auswahl
 * sagen, ob eine Bootdiskette ueberhaupt moeglich ist.
 * @return 0 = dieses Dateisystem hat keine Systemspuren (beginnt auf Zylinder 0).
 */
K1520_API uint64_t k1520d_fs_boot_capacity(const char* fs_name);

/// @brief Dasselbe fuer eine geoeffnete Diskette (@p volume = Seite, UDOS).
K1520_API uint64_t k1520d_boot_area_size(K1520Disk h, int volume);

/// @brief Systemspuren in eine Datei schreiben (Bootabbild sichern).
K1520_API bool k1520d_read_boot_image(K1520Disk h, int volume, const char* path);

/// @brief Bootabbild aus einer Datei in die Systemspuren schreiben (danach @ref k1520d_flush).
K1520_API bool k1520d_write_boot_image(K1520Disk h, int volume, const char* path);

/// @brief Aenderungen in die gebundene Datei schreiben (legt beim ersten Mal `<name>~` an).
K1520_API bool k1520d_flush(K1520Disk h);
/// @brief Unter neuem Namen/Container speichern und **umbinden** (auch bei Schreibschutz).
K1520_API bool k1520d_save_as(K1520Disk h, const char* path);

/// @brief Kopie in einen anderen Container schreiben, **ohne** umzubinden
///        (Archivierung, Formatumwandlung).  Auch bei Schreibschutz erlaubt.
K1520_API bool k1520d_export_image(K1520Disk h, const char* path);

/// @brief Schreibschutz abfragen bzw. setzen (Vorgabe beim Oeffnen: geschuetzt).
K1520_API bool k1520d_read_only(K1520Disk h);
K1520_API void k1520d_set_read_only(K1520Disk h, bool ro);
/// @brief Sicherungskopie beim ersten Schreiben an-/abschalten (Vorgabe: an).
K1520_API void k1520d_set_backup(K1520Disk h, bool on);
/// @brief Handle schliessen (schreibt NICHT selbsttaetig — vorher @ref k1520d_flush).
K1520_API void k1520d_close(K1520Disk h);

/// @brief Grund des letzten fehlgeschlagenen Aufrufs an diesem Handle.
K1520_API const char* k1520d_last_error(K1520Disk h);
/// @brief Grund, wenn @ref k1520d_open / @ref k1520d_create NULL geliefert hat.
K1520_API const char* k1520d_last_open_error(void);

/* ─── Katalog und Erkennung ───────────────────────────────────────────────── */

K1520_API int         k1520d_fs_count(void);
K1520_API const char* k1520d_fs_name(int i);
K1520_API const char* k1520d_fs_description(const char* name);
/// @brief Geometrie, auf der dieses Dateisystem liegt.
K1520_API const char* k1520d_fs_format(const char* name);
/// @brief "cpm" | "udos"
K1520_API const char* k1520d_fs_type(const char* name);
/// @brief Geladene Katalogdateien (mehrzeilig) + Beanstandungen.
K1520_API const char* k1520d_catalog_report(void);

/// @brief Erkanntes Dateisystem einer Datei ohne sie offen zu halten; "" = nicht erkannt.
K1520_API const char* k1520d_detect(const char* path);

K1520_API const char* k1520d_detected_format(K1520Disk h);
K1520_API const char* k1520d_detected_fs(K1520Disk h);
/// @brief false = mehrere gleich gute Kandidaten (Alternativen in @ref k1520d_detection_alternatives).
K1520_API bool        k1520d_detection_unambiguous(K1520Disk h);
K1520_API const char* k1520d_detection_alternatives(K1520Disk h);
/// @brief Auffaelligkeiten des Mediums (Altbestand, CRC-Fehler, Schaeden); "" = ohne Befund.
K1520_API const char* k1520d_detection_remarks(K1520Disk h);

/* ─── Seiten (UDOS) ───────────────────────────────────────────────────────────
 * Es wird NICHT umgeschaltet — alle Seiten sind gleichzeitig sichtbar, und jeder
 * Verzeichniseintrag nennt seine Seite (@ref k1520d_entry_volume).            */

K1520_API int         k1520d_volume_count(K1520Disk h);
/// @brief "" bei einem Dateisystem, sonst "Side0"/"Side1" — auch der Ordnername.
K1520_API const char* k1520d_volume_dir(K1520Disk h, int v);
K1520_API const char* k1520d_volume_label(K1520Disk h, int v);
K1520_API uint64_t    k1520d_volume_total(K1520Disk h, int v);
K1520_API uint64_t    k1520d_volume_free(K1520Disk h, int v);
K1520_API uint64_t    k1520d_volume_used(K1520Disk h, int v);

/* ─── Verzeichnis ─────────────────────────────────────────────────────────────
 * IMMER frisch aus dem Medium gelesen — es gibt keinen zwischengespeicherten
 * Verzeichnisstand (doc/design/13_k1520disktool.md §9.3).                     */

/// @brief Verzeichnis aller Seiten einlesen; liefert die Anzahl der Eintraege.
K1520_API int         k1520d_list(K1520Disk h);
K1520_API int         k1520d_entry_volume(K1520Disk h, int i);
K1520_API const char* k1520d_entry_name(K1520Disk h, int i);
K1520_API int         k1520d_entry_user(K1520Disk h, int i);
K1520_API uint64_t    k1520d_entry_size(K1520Disk h, int i);
K1520_API const char* k1520d_entry_type(K1520Disk h, int i);
K1520_API const char* k1520d_entry_attrs(K1520Disk h, int i);
K1520_API const char* k1520d_entry_date(K1520Disk h, int i);
K1520_API bool        k1520d_entry_hidden(K1520Disk h, int i);
K1520_API bool        k1520d_entry_damaged(K1520Disk h, int i);

/* ─── UDOS-Kopfsektorangaben eines Eintrags ───────────────────────────────────
 * Eine UDOS-Datei traegt mehr als Name und Bytes; diese Angaben steuern, wie das
 * Betriebssystem sie LAEDT (doc/udos_diskettenformat.md §6 und §14).  Bei CP/M sind
 * sie alle 0 bzw. leer.                                                        */

/// @brief ENTRY — Einsprungadresse (Typ P/P1).
K1520_API uint16_t    k1520d_entry_start(K1520Disk h, int i);
/// @brief Satzlaenge in Byte (Zuteilungseinheit).
K1520_API uint16_t    k1520d_entry_record_len(K1520Disk h, int i);
/// @brief Zweite Laengenangabe (Kopfsektor Offset 17) — bei 256/512 Byte Satzlaenge 0.
K1520_API uint16_t    k1520d_entry_block_len(K1520Disk h, int i);
/// @brief SEGMENTS: Anfang und Laenge des Speichersegments.
K1520_API uint16_t    k1520d_entry_segment(K1520Disk h, int i);
K1520_API uint16_t    k1520d_entry_segment_len(K1520Disk h, int i);
/// @brief LOW/HIGH ADDRESS und STACK SIZE — der zugeteilte Speicher.
K1520_API uint16_t    k1520d_entry_low_addr(K1520Disk h, int i);
K1520_API uint16_t    k1520d_entry_high_addr(K1520Disk h, int i);
K1520_API uint16_t    k1520d_entry_stack_size(K1520Disk h, int i);
/// @brief Bytes im letzten Satz (Kopfsektor Offset 22) — bestimmt die logische Laenge.
K1520_API uint16_t    k1520d_entry_bytes_in_last(K1520Disk h, int i);
/// @brief Kopfsektor Offset 44…47 (Bedeutung offen, unveraendert uebernehmen).
K1520_API uint32_t    k1520d_entry_extra(K1520Disk h, int i);
/// @brief Erstellungsvermerk (Datum ODER Versionstext wie "V 4.3").
K1520_API const char* k1520d_entry_created(K1520Disk h, int i);

/**
 * @brief Kopfsektorangaben einer vorhandenen Datei aendern (nur UDOS).
 *
 * Fuer die Oberflaeche gedacht: der Dateiinhalt bleibt unangetastet.  **Leere
 * Zeichenketten und `false`-Kennzeichen lassen das jeweilige Feld unveraendert**, so
 * dass ein einzelnes Feld gesetzt werden kann, ohne die uebrigen zu kennen.
 *
 * @param name      wie @ref k1520d_entry_name, ggf. mit `SideN/`-Praefix
 * @param type      "A"/"P"/"P1"/"B"; NULL/"" = unveraendert
 * @param props     "WELS"; NULL/"" = unveraendert, ";" = alle loeschen
 * @param created   6 Zeichen; NULL/"" = unveraendert
 * @param modified  "JJMMTT"; NULL/"" = unveraendert
 */
K1520_API bool k1520d_set_udos_attrs(K1520Disk h, const char* name,
                                     const char* type, const char* props,
                                     const char* created, const char* modified,
                                     bool set_entry, uint16_t entry,
                                     bool set_block_len, uint16_t block_len,
                                     bool set_segment, uint16_t segment, uint16_t segment_len,
                                     bool set_memory, uint16_t low, uint16_t high,
                                     uint16_t stack,
                                     bool set_extra, uint32_t extra);

/**
 * @brief Attribute und Nutzerbereich einer vorhandenen Datei aendern (nur CP/M).
 *
 * Das Gegenstueck zu @ref k1520d_set_udos_attrs fuer die andere Dateisystemfamilie.
 * CP/M fuehrt nur drei Attributbits und den Nutzerbereich; auch hier gilt
 * **`set_*` = false laesst das Feld unveraendert**.
 *
 * Ein Wechsel des Nutzerbereichs verschiebt die Datei nach `3:NAME.TYP` und wird
 * abgelehnt, wenn dort schon eine Datei gleichen Namens liegt.
 *
 * @param name  wie @ref k1520d_entry_name, ggf. mit Nutzerbereich ("3:NAME.TYP")
 */
K1520_API bool k1520d_set_cpm_attrs(K1520Disk h, const char* name,
                                    bool set_read_only, bool read_only,
                                    bool set_system,    bool system,
                                    bool set_archived,  bool archived,
                                    bool set_user,      int  user);

/* ─── Uebertragung ───────────────────────────────────────────────────────────
 * `name` darf das Seitenpraefix tragen: "Side1/HELP.DAT.00".                  */

K1520_API bool k1520d_extract(K1520Disk h, const char* name, const char* dest, K1520DMode mode);
K1520_API bool k1520d_insert (K1520Disk h, const char* src, const char* name, K1520DMode mode,
                              bool overwrite);
K1520_API bool k1520d_erase  (K1520Disk h, const char* name);

/**
 * @brief Alles extrahieren.  Bei mehreren Seiten entstehen `Side0/`, `Side1/` …
 */
K1520_API bool k1520d_extract_all(K1520Disk h, const char* dest_dir, K1520DMode mode);

/**
 * @brief Einen ganzen Ordner einfuegen — transaktional (§9.2).
 *
 * Bei mehreren Seiten MUSS der Ordner genau die `SideN/`-Unterverzeichnisse tragen.
 * Passt der Inhalt nicht, wird **nichts** geschrieben; ein Fehler mittendrin nimmt
 * die ganze Aenderung zurueck.
 */
K1520_API bool k1520d_insert_all(K1520Disk h, const char* src_dir, K1520DMode mode, bool overwrite);

/// @brief Wuerde @p src_dir passen?  "passt" oder der Grund; schreibt nichts.
K1520_API const char* k1520d_check_fit(K1520Disk h, const char* src_dir);

/* ─── Zustand ─────────────────────────────────────────────────────────────── */

/// @brief Ungespeicherte Aenderungen im Speicher?
K1520_API bool        k1520d_dirty(K1520Disk h);
/// @brief Mehrzeiliger Pruefbericht (Datentraeger, Belegung, Auffaelligkeiten).
K1520_API const char* k1520d_check(K1520Disk h);
K1520_API const char* k1520d_version(void);

#ifdef __cplusplus
}
#endif
