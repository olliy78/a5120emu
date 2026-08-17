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
#include "core/api/k1520_sync_api.h"  ///< physische Diskette (Greaseweazle)

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
 * @brief Wie @ref k1520d_open, aber **oeffnet auch ohne Erkennung** (§12.6).
 *
 * Wird kein Dateisystem gefunden, kommt das Abbild trotzdem heraus — nur eben ohne
 * (@ref k1520d_has_filesystem == false).  Medium, Sektoreditor, „Speichern unter"
 * und die Schnittwerkzeuge arbeiten weiter; Dateien gibt es keine.
 *
 * Das gilt fuer eine DATEI genauso wie fuer eine physische Diskette: eine gemischte
 * oder unbekannte Geometrie ist kein Grund, das Abbild gar nicht herzugeben.
 */
K1520_API K1520Disk k1520d_open_raw(const char* path, const char* fs_name,
                                    bool read_only);

/**
 * @brief **Physische Diskette** in einem echten Laufwerk oeffnen.
 *
 * @p sync kommt aus @ref k1520s_create und wird von einem fremden Arbeitsfaden bedient.
 *
 * Der Aufruf holt die **Sondenspuren** der Formaterkennung
 * (@ref k1520d_probe_track_count) und danach das Verzeichnis; er dauert Sekunden und
 * gehoert deshalb in einen Arbeitsfaden mit Fortschrittsanzeige, nicht in den
 * Oberflaechenfaden (doc/design/14_physische_diskette.md §11.2/§11.2a).  Passt kein
 * Katalogformat, folgt die Vollmessung — dann dauert es entsprechend laenger.
 */
K1520_API K1520Disk k1520d_open_physical(K1520Sync sync, const char* fs_name,
                                         bool read_only);

/**
 * @brief Wie @ref k1520d_open_physical, aber **oeffnet auch ohne Erkennung**.
 *
 * Wird kein Dateisystem gefunden, kommt die Diskette trotzdem heraus — nur eben
 * ohne (@ref k1520d_has_filesystem == false).  Medium, Sektoreditor, Abbild sichern
 * und die Schnittwerkzeuge (@ref k1520d_keep_even_tracks) arbeiten weiter; Dateien
 * gibt es keine.  Der Grund steht in @ref k1520d_detection_remarks.
 */
K1520_API K1520Disk k1520d_open_physical_raw(K1520Sync sync, const char* fs_name,
                                             bool read_only);

/// @brief Ist ein Dateisystem gemountet?  false = roh geoeffnet.
K1520_API bool k1520d_has_filesystem(K1520Disk h);

/**
 * @brief Erkennung am **Speicherabbild** wiederholen — ohne die Diskette neu zu lesen.
 *
 * Fuer „Dateisystem von Hand waehlen" und fuer die Zeit nach einem Schnitt: das
 * bereits gelesene Medium wandert unveraendert in ein neues Volume.  Das alte Handle
 * bleibt gueltig und zeigt danach auf das neue Ergebnis.
 *
 * @param fs_name  Dateisystem erzwingen; NULL/"" = wieder erkennen lassen.
 * @return false, wenn selbst roh nichts daraus wurde (Grund: @ref k1520d_last_error).
 */
K1520_API bool k1520d_redetect(K1520Disk h, const char* fs_name);

/**
 * @brief Jede zweite Spur wegwerfen (Doppelschritt-Diskette geradeziehen), §12.6.
 *
 * **Loest das Abbild vom Laufwerk**: die Spurnummern stimmen danach nicht mehr mit
 * den Kopfpositionen ueberein, ein Rueckschreiben ginge auf die falschen Zylinder.
 * @return verbliebene Spuren, -1 bei Fehler.
 */
K1520_API int k1520d_keep_even_tracks(K1520Disk h);

/// @brief Seite 1 wegwerfen (§12.6).  Loest ebenfalls vom Laufwerk.
K1520_API int k1520d_drop_second_side(K1520Disk h);

/// @brief Einen ganzen Zylinder loeschen; alles dahinter rueckt auf (§19.6).
///        @return verbliebene Zylinder, -1 bei Fehler.  Loest vom Laufwerk.
K1520_API int k1520d_delete_cylinder(K1520Disk h, int cyl);

/**
 * @brief Einen **unformatierten** Zylinder an Position @p pos einfuegen (§19.6).
 *
 * Der neue Zylinder TRAEGT die Nummer @p pos; alles von dort an rueckt nach hinten.
 * @p pos darf die Spurzahl sein (anhaengen) und **0** (vor alle bestehenden) — das
 * braucht man, um einer MFM-Diskette eine FM-Spur 0 vorzusetzen.
 *
 * @param mfm  Verfahren der neuen Spur; es folgt NICHT dem Nachbarn, denn gerade der
 *             Wechsel ist der Zweck (gemischte K1520-Formate).
 * @return verbliebene Zylinder, -1 bei Fehler.  Loest vom Laufwerk.
 */
K1520_API int k1520d_insert_cylinder_at(K1520Disk h, int pos, bool mfm);

/**
 * @brief Das Speicherabbild der geoeffneten Diskette auf ein **echtes Laufwerk** legen.
 *
 * Kopiert jede bekannte Spur in das Medium hinter @p sync.  Damit gilt sie dort als
 * **geaendert**, und der Arbeitsfaden schreibt sie im Hintergrund auf die eingelegte
 * Diskette — samt Pruef-Lesen (§7.1).  Gewartet wird nicht; der Aufrufer sieht den
 * Fortschritt ueber @ref k1520s_stats und schliesst mit @ref k1520s_flush ab.
 *
 * Das ist der Weg, eine geladene `.hfe` auf eine echte Diskette zu bringen: die
 * Quelle darf eine Datei sein, das Ziel ist immer ein Laufwerk.
 *
 * **Es wird nichts vorher gelesen.**  Was auf der Zieldiskette stand, ist danach fort;
 * die Rueckfrage gehoert in die Oberflaeche.
 *
 * @return Zahl der eingestellten Spuren, oder -1 bei einem Fehler (Grund ueber
 *         @ref k1520d_last_error).  Passt die Diskette nicht in die eingestellte
 *         Laufwerksgeometrie, wird **gar nichts** kopiert.
 */
K1520_API int k1520d_write_to_physical(K1520Disk h, K1520Sync sync);

/**
 * @brief Wie viele Spuren die Formaterkennung anfassen wird (Sondenzahl).
 *
 * Fuer die Fortschrittsanzeige: an einem echten Laufwerk ist die Zahl der
 * Sondenspuren das Ziel, nicht die Spurzahl der Diskette — ein Balken, der gegen 160
 * laeuft und bei 8 stehenbleibt, sagt dem Bedienenden das Falsche.
 *
 * Die Regel steht in @ref GeometryProbe::probeTracks; diese Funktion gibt es, damit
 * die Oberflaeche sie nicht nachbauen muss (und dabei von ihr abweicht).
 *
 * @return Zahl der Sondenspuren, 0 bei unsinnigen Angaben.
 */
K1520_API int k1520d_probe_track_count(int num_cyls, int num_heads);

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

/**
 * @brief Zahl der Spuren, ueber die @ref k1520d_detection_remarks urteilt; 0 = alle.
 *
 * Nach einer Stichprobenerkennung (physisches Laufwerk) sind die Zaehlungen darin
 * Aussagen ueber DIESE Spuren, nicht ueber die Diskette.
 */
K1520_API int k1520d_detection_examined_tracks(K1520Disk h);

/**
 * @brief Auffaelligkeiten neu bewerten, sobald die Diskette vollstaendig gelesen ist.
 *
 * @return true, wenn sich die Meldung geaendert hat — dann ist die Anzeige aufzufrischen.
 */
K1520_API bool k1520d_refresh_detection(K1520Disk h);

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

/**
 * @brief Verzeichnis **nur mit den Namen** einlesen; liefert die Anzahl der Eintraege.
 *
 * Bei CP/M gleichbedeutend mit @ref k1520d_list (dort steht alles im Eintrag selbst).
 * Bei UDOS bleiben Groesse, Typ und Datum zunaechst leer — sie stehen im Kopfsektor
 * jeder Datei, verstreut ueber die Diskette.  An einem echten Laufwerk ist das der
 * Unterschied zwischen drei und drei Dutzend Spuren (`CAT` gegen `CAT F=L`).
 *
 * Nachzutragen mit @ref k1520d_entry_load_details, am besten erst, wenn
 * @ref k1520d_entry_details_ready dafuer @c true sagt.
 */
K1520_API int         k1520d_list_names(K1520Disk h);

/// @brief Stehen die Angaben zu Eintrag @p i schon fest?
K1520_API bool        k1520d_entry_details_loaded(K1520Disk h, int i);

/// @brief Waeren die Angaben zu Eintrag @p i **ohne Warten** zu haben?
K1520_API bool        k1520d_entry_details_ready(K1520Disk h, int i);

/// @brief Angaben zu Eintrag @p i nachtragen; **blockiert** ggf. (siehe @c _ready).
K1520_API bool        k1520d_entry_load_details(K1520Disk h, int i);
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

/* ─── Sektoransicht (Diskeditor) ───────────────────────────────────────────────
 * Eine Ebene UNTER dem Dateisystem: Spuren, Sektoren, Gaps und CRCs.  Fuer einen
 * Editor, der eine schadhafte Diskette begutachten oder von Hand reparieren soll
 * (doc/design/13_k1520disktool.md §19).
 *
 * Winkelangaben sind Bruchteile EINER UMDREHUNG (0 = Index).  Sie kommen aus der
 * Byteposition in der Spur — eine Spur ist genau eine Umdrehung —, nicht aus der
 * Drehzahl im HFE-Kopf.                                                        */

/// @brief Ausdehnung des MEDIUMS (was da ist, nicht was das Format vorsieht).
K1520_API int k1520d_medium_cylinders(K1520Disk h);
K1520_API int k1520d_medium_heads(K1520Disk h);

/**
 * @brief Eine Spur einlesen; liefert die Anzahl ihrer Abschnitte (-1 = Fehler).
 *
 * Wie @ref k1520d_list ein Zustandswechsel: die `k1520d_track_*`- und
 * `k1520d_span_*`-Abfragen beziehen sich auf die zuletzt eingelesene Spur.
 *
 * @warning An einem echten Laufwerk **blockiert** der Aufruf, wenn die Spur noch
 *          unbekannt ist (sie wird dann geholt).  Ansichten ueber die ganze Diskette
 *          fragen deshalb zuerst @ref k1520d_track_state.
 */
K1520_API int k1520d_track_scan(K1520Disk h, int cyl, int head);

/**
 * @brief Zustand einer Spur, **ohne** sie zu beschaffen (0=unbekannt, 1=sauber, 2=geaendert).
 *
 * Damit kann eine Uebersicht zeichnen, was sie weiss, statt 160 Spuren nachzuladen.
 */
K1520_API int k1520d_track_state(K1520Disk h, int cyl, int head);

/// @brief false = diese Spur gibt es in der Ausdehnung des Mediums nicht.
K1520_API bool        k1520d_track_exists(K1520Disk h);
/// @brief false = keine einzige Adressmarke (unformatiert bzw. markenloser Gap-Fluss).
K1520_API bool        k1520d_track_formatted(K1520Disk h);
/// @brief "MFM" | "FM"
K1520_API const char* k1520d_track_encoding(K1520Disk h);
/// @brief Spurlaenge in Byte (eine Umdrehung).
K1520_API int         k1520d_track_bytes(K1520Disk h);
K1520_API int         k1520d_track_sectors(K1520Disk h);

/// @brief 0 = unformatiert · 1 = Gap · 2 = Sektor
K1520_API int    k1520d_span_kind (K1520Disk h, int i);
/// @brief Anfang/Ende als Bruchteil der Umdrehung; die Abschnitte decken [0,1) lueckenlos ab.
K1520_API double k1520d_span_start(K1520Disk h, int i);
K1520_API double k1520d_span_end  (K1520Disk h, int i);
/// @brief Laufende Nummer des Sektors in der Spur — der Schluessel fuer Lesen/Schreiben.
K1520_API int    k1520d_span_index(K1520Disk h, int i);
/// @brief Angaben aus dem ID-FELD (koennen von der tatsaechlichen Lage abweichen).
K1520_API int    k1520d_span_id   (K1520Disk h, int i);
K1520_API int    k1520d_span_cyl  (K1520Disk h, int i);
K1520_API int    k1520d_span_head (K1520Disk h, int i);
K1520_API int    k1520d_span_size (K1520Disk h, int i);
K1520_API bool   k1520d_span_id_crc_ok  (K1520Disk h, int i);
K1520_API bool   k1520d_span_data_crc_ok(K1520Disk h, int i);
/// @brief Datenmarke war 0xF8 (geloeschter Sektor) statt 0xFB.
K1520_API bool   k1520d_span_deleted    (K1520Disk h, int i);
/// @brief Traegt das DATENFELD nichts Unterscheidbares (alle Bytes gleich)?  So sieht
///        ein formatierter, nie beschriebener Sektor aus.  Der UDOS-Anhang hinter der
///        Daten-CRC zaehlt nicht mit — er ist auch auf einer leeren Diskette belegt.
K1520_API bool   k1520d_span_blank      (K1520Disk h, int i);
/// @brief Bytes hinter der Daten-CRC, die KEIN Gap sind (0 = keine, UDOS = 4).
///        Am Inhalt entschieden, nicht am erkannten Dateisystem.
K1520_API int    k1520d_span_tail_bytes (K1520Disk h, int i);

/**
 * @brief Nutzdaten eines Sektors lesen.
 * @param index laufende Nummer aus @ref k1520d_span_index
 * @return Anzahl gelesener Bytes, oder -1 (auch wenn @p max_len zu klein ist —
 *         es wird dann NICHTS kopiert)
 */
K1520_API int k1520d_sector_read(K1520Disk h, int cyl, int head, int index,
                                 uint8_t* out, int max_len);

/// @brief Daten-CRC, wie sie auf dem Medium steht; -1 = Fehler.
K1520_API int k1520d_sector_crc(K1520Disk h, int cyl, int head, int index);

/// @brief Welche Daten-CRC gehoerte zu @p data?  Aendert nichts; -1 = Fehler.
K1520_API int k1520d_sector_crc_for(K1520Disk h, int cyl, int head, int index,
                                    const uint8_t* data, int len);

/**
 * @brief Datenfeld eines Sektors ersetzen (in die Diskette im Speicher).
 *
 * @param crc `< 0` = CRC neu rechnen; sonst wird **genau dieser Wert** ins CRC-Feld
 *        geschrieben — damit laesst sich ein Sektor absichtlich defekt lassen oder
 *        machen (eine schadhafte Diskette originalgetreu nachbilden).
 * @param len MUSS der Sektorgroesse entsprechen.
 */
K1520_API bool k1520d_sector_write(K1520Disk h, int cyl, int head, int index,
                                   const uint8_t* data, int len, int crc);

/**
 * @brief Bytes HINTER der Daten-CRC lesen (bei UDOS der 4-Byte-Kontrollblock).
 * @return Anzahl gelesener Bytes, oder -1.
 */
K1520_API int k1520d_sector_tail(K1520Disk h, int cyl, int head, int index,
                                 uint8_t* out, int max_len);

/**
 * @brief Nur den Nachspann schreiben — Nutzdaten und Daten-CRC bleiben unangetastet.
 *
 * Bei UDOS ist das die Dateiverkettung; sie zu aendern ist etwas anderes, als die
 * Nutzdaten zu aendern.  Eine absichtlich falsche CRC bleibt falsch.
 */
K1520_API bool k1520d_sector_write_tail(K1520Disk h, int cyl, int head, int index,
                                        const uint8_t* tail, int len);

/**
 * @brief Sektor loeschen — sein Bereich wird wieder Gap.
 * @param tail_bytes zusaetzlich zu loeschende Bytes hinter der Daten-CRC (UDOS: 4)
 */
K1520_API bool k1520d_sector_erase(K1520Disk h, int cyl, int head, int index,
                                   int tail_bytes);

/**
 * @brief Sektor anlegen.  **Die ID bestimmt die Lage**: der neue Sektor kommt hinter
 *        den vorhandenen mit der naechstkleineren ID, um @p gap Bytes versetzt; gibt
 *        es keinen kleineren, hinter den Index (12 Uhr).
 *
 * Die Spurlaenge bleibt fest — geschrieben wird ueber vorhandenes Gap und, wenn der
 * Gap zu knapp ist, ueber den Nachbarn.  Was dabei ueberschrieben wird, sagt
 * @ref k1520d_sector_plan_pos / @ref k1520d_sector_plan_len **vorher**.
 *
 * @param mfm  Verfahren; muss zur Spur passen, ausser sie traegt noch keine Marke —
 *             dann legt der erste Sektor es fest (FM und MFM sind nicht mischbar).
 * @param tail_bytes Bytes hinter der Daten-CRC (UDOS-Kontrollblock: 4; 0 = keiner)
 */
K1520_API bool k1520d_sector_create(K1520Disk h, int cyl, int head,
                                    int id, int id_cyl, int id_head, int size,
                                    int gap, int tail_bytes, int fill, bool mfm);

/// @brief Byte-Position, an der @ref k1520d_sector_create anlegen wuerde; -1 = Fehler.
K1520_API int k1520d_sector_plan_pos(K1520Disk h, int cyl, int head, int id, int gap);
/// @brief Wie viele Bytes der neue Sektor belegen wuerde; -1 = Fehler.
K1520_API int k1520d_sector_plan_len(K1520Disk h, int cyl, int head,
                                     int size, int tail_bytes, bool mfm);

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
