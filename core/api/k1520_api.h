#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "core/api/k1520_export.h"
#include "core/api/k1520_sync_api.h"   /* K1520_API — Ausfuhrkennzeichnung (Windows!) */

typedef void* K1520Handle;

typedef enum {
    K1520_MACHINE_A5120  = 0,
    K1520_MACHINE_PRG710 = 1,
    K1520_MACHINE_K8915  = 2,
} K1520MachineType;

typedef struct {
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint8_t  i, r, im;
    bool     iff1, iff2, halted;
} K1520CpuState;

typedef enum {
    K1520_SERIAL_DFU     = 0,
    K1520_SERIAL_PRINTER = 1,
} K1520SerialPort;

typedef void (*K1520SerialCallback)(void* ctx, uint8_t byte);

/* ─── Lifecycle ──────────────────────────────────────────────────────────── */
K1520_API K1520Handle k1520_create(K1520MachineType type);

/**
 * Create a machine with an explicit drive-bay configuration.
 *
 * @param drive0..3  DriveProfile name per K5122 slot — the real drive names:
 *                   "K5601" (5.25" DS 80 tracks, 800K, default), "K5600.10"
 *                   (5.25" SS 40 tracks, 200K), "K5600.20" (5.25" SS 80 tracks,
 *                   400K), "MF3200" (8" SS 77 tracks, FM only, 300K), "MF6400"
 *                   (8" SS 77 tracks, FM+MFM, 600K).  The special name "none"
 *                   marks an EMPTY slot (no drive wired: mounting/creating a disk
 *                   there is refused).  NULL or "" keeps the default (K5601);
 *                   unknown names fall back to K5601.  Former technical names
 *                   (e.g. "mf3200_8_ss77") still resolve as aliases.
 * @return handle, or NULL on error.  Equivalent to k1520_create() when all four
 *         names are NULL/"".
 */
K1520_API K1520Handle k1520_create_configured(K1520MachineType type,
                                              const char* drive0, const char* drive1,
                                              const char* drive2, const char* drive3);

/**
 * @brief PRG 710 / PRG 710-1 (K1520_MACHINE_PRG710) mit gewählter Variante.
 *
 * @param variante   0 = PRG 710 (Marken-FF low-aktiv, 8279-Tastatur), 1 = PRG 710-1.
 *                   Anderes → NULL (Grund in k1520_last_init_error).
 * @param drive0..3  wie bei k1520_create_configured (Vorgabe K5601, K5601, none, none).
 * k1520_create(K1520_MACHINE_PRG710) und _configured bauen die Variante 0.
 * Tastatur: keyPress/keyRelease wie bei den anderen Maschinen (710: K7609 hinter dem 8279,
 * 710-1: K7672 an A32-B; Codes `0x03000000 | Position` = physische Taste).
 * k1520_machine_type() = 1; Variante: k1520_prg710_variant().
 */
K1520_API K1520Handle k1520_create_prg710(int variante,
                                          const char* drive0, const char* drive1,
                                          const char* drive2, const char* drive3);

/**
 * @brief Reason the last k1520_create*() returned NULL ("" if none).
 *
 * A startup abort (e.g. missing/broken disk format catalog `formats.yaml`) yields
 * no handle, so the message cannot be fetched via k1520_last_error(). The returned
 * pointer stays valid until the next k1520_create*() call.
 */
K1520_API const char* k1520_last_init_error(void);

K1520_API void        k1520_destroy(K1520Handle h);
K1520_API void        k1520_reset(K1520Handle h);
K1520_API void        k1520_power_on(K1520Handle h);

/* ─── Execution ──────────────────────────────────────────────────────────── */
K1520_API int  k1520_run(K1520Handle h, int max_cycles);
K1520_API void k1520_stop(K1520Handle h);

/* ─── Framebuffer ────────────────────────────────────────────────────────── */
K1520_API const uint8_t* k1520_framebuffer(K1520Handle h);
K1520_API int            k1520_fb_width(K1520Handle h);
K1520_API int            k1520_fb_height(K1520Handle h);
K1520_API bool           k1520_fb_dirty(K1520Handle h);
K1520_API void           k1520_fb_clear_dirty(K1520Handle h);

/* ─── Console (CLI) mode ─────────────────────────────────────────────────── */
K1520_API void k1520_set_console_mode(K1520Handle h, bool enable);
K1520_API bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch);

/* ─── Keyboard ───────────────────────────────────────────────────────────── */
K1520_API void k1520_key_press(K1520Handle h, uint32_t keycode, bool shift, bool ctrl);
K1520_API void k1520_key_release(K1520Handle h, uint32_t keycode);
K1520_API void k1520_console_key(K1520Handle h, char c);
/**
 * @brief Zeitbasis der Tastenwiederholung: Echtzeit (true) oder Maschinentakte.
 *
 * Die echten Tastaturen (K7637, K7672) haben ihren eigenen Quarz.  Läuft der
 * Rechner schneller als im Nenntakt, wiederholten sie in Maschinentakten
 * gezählt entsprechend früher und schneller — bei 10 × käme schon ein
 * gewöhnlicher Anschlag mehrfach an.  Die Oberfläche schaltet deshalb Echtzeit
 * ein; Vorgabe ist Maschinenzeit (wiederholbare Tests).  Fadensicher.
 */
K1520_API void k1520_set_key_repeat_realtime(K1520Handle h, bool realtime);
/**
 * @brief Zustand der Tastaturanzeigen — Bitbelegung je Tastaturmodell.
 *
 * **K8915 (K7672):** Abbild des Firmware-Registers 21H, nachgebildet Bit 3 =
 * Senden frei (an nach `DC1`, aus nach `DC3`) und Bit 0 = `ESC [?13h`/`l` [?].
 *
 * **A5120 (K7637):**
 *
 * Bit 0…4 = Funktionsanzeigen G00…G04 (der Rechner schaltet sie mit den fünf
 * LED-Kommandos UM), Bit 5 = Fehleranzeige G53 — sie **blinkt**, solange das
 * Bit gesetzt ist —, Bit 7 = akustisches Signal läuft (≈1 s).  Die
 * Betriebsanzeige E54 und die LOCK-Anzeige C99 stehen nicht darin: die eine
 * hängt an der Spannung, die andere am Umschaltfeststeller der Tastatur.
 */
K1520_API uint32_t k1520_keyboard_leds(K1520Handle h);
/**
 * @brief Welchen physischen K7637-Code erzeugt dieser Tastencode?
 *
 * Dieselbe Abbildung, die `k1520_key_press` benutzt — ohne Maschine und ohne
 * Seiteneffekt.  Gedacht für Tests und für die Fehlersuche an der Oberfläche:
 * beantwortet „welche Taste der echten Tastatur spricht dieser Anschlag an?"
 * ohne den Umweg über einen laufenden Gast.  0 heißt: keine.
 */
K1520_API uint8_t k1520_translate_key(uint32_t keycode, bool shift, bool ctrl);

/* ─── Disk drives ────────────────────────────────────────────────────────── */
/** @brief Mount a disk image into a drive slot. */
K1520_API bool k1520_mount_disk(K1520Handle h, int drive,
                                const char* image_path,
                                const char* format_name,
                                bool write_protect);
/**
 * @brief Create a NEW disk and mount it.
 *
 * @p format_name NULL or "" → a genuinely BLANK (unformatted) disk in the geometry of
 * the *drive* (K5601 80×2, K5600.10 40×1, …), ready to be formatted by the guest OS —
 * including foreign systems such as UDOS that append data behind the data CRC.  A `.img`
 * target is rejected in that case (a raw sector image cannot express "unformatted");
 * use `.hfe` or `.dmk`.
 *
 * @p format_name set → a PRE-FORMATTED disk per catalog format (real IDAM/DATA/CRC,
 * 0xE5 data); `.img` is allowed then.
 *
 * Overwrites an existing file.  Returns false on error (see k1520_last_error).
 */
K1520_API bool k1520_create_disk(K1520Handle h, int drive,
                                 const char* image_path,
                                 const char* format_name,
                                 bool write_protect);
/**
 * @brief Save the mounted disk under a new name/container and re-bind to it.
 *
 * Container follows the extension (`.img` / `.hfe` / `.dmk`).  From then on all further
 * writes go (delayed) into the new file.  @p format_name is only needed for `.img`
 * (the other containers are self-describing) and is validated against the medium.
 */
K1520_API bool k1520_save_disk_as(K1520Handle h, int drive,
                                  const char* image_path,
                                  const char* format_name);
/** @brief True if the mounted disk may be saved as a raw sector image (.img). */
K1520_API bool k1520_disk_raw_compatible(K1520Handle h, int drive);
/** @brief Currently bound image file of a slot ("" = memory only / empty drive). */
K1520_API const char* k1520_disk_path(K1520Handle h, int drive);
/** @brief Container of the bound file ("img" | "hfe" | "dmk"; "" = none). */
K1520_API const char* k1520_disk_container(K1520Handle h, int drive);
/**
 * @brief Catalog format DETECTED on the mounted disk ("" = unknown).
 *
 * The geometry detection of the k1520DiskTool (`GeometryProbe`), run over the medium
 * that is actually in the drive.  "" means *unknown* and covers all three cases the
 * GUI must treat alike: nothing mounted / no catalog format matches / two formats
 * match equally well.  A disk of unknown format must not be exported as `.img` —
 * the sector order would be guessed.  For a raw `.img` the format is not measured
 * but the one DECLARED at mount time.  See A5120Machine::detectedFormatName.
 */
K1520_API const char* k1520_disk_detected_format(K1520Handle h, int drive);
/**
 * @brief Operating notices about how the mounted disk had to be adapted to the drive.
 *
 * One line per restriction, separated by '\n'; "" = the disk fits as it is.  This is
 * NOT an error — the disk is mounted and readable, just translated (track pitch /
 * single-sided drive).  The GUI shows the lines in the drive box under the file name.
 */
K1520_API const char* k1520_disk_notice(K1520Handle h, int drive);

/**
 * @brief **Physische Diskette** aus einem echten Laufwerk am Greaseweazle anmelden.
 *
 * @p sync kommt aus @ref k1520s_create (core/api/k1520_sync_api.h) und wird von einem
 * fremden Arbeitsfaden bedient.  Es wird beim Anmelden **nichts gelesen** — Spuren
 * kommen einzeln, sobald der Gast sie anfasst.
 *
 * Ein Handle laesst sich nur EINMAL anmelden.
 *
 * @see doc/design/14_physische_diskette.md
 */
K1520_API bool k1520_mount_physical(K1520Handle h, int drive, K1520Sync sync,
                                    bool write_protect);
/** @brief Write pending changes of all drives to their files immediately. */
K1520_API bool k1520_flush_disks(K1520Handle h);
/** @brief Unmount disk image from a drive slot. */
K1520_API bool k1520_unmount_disk(K1520Handle h, int drive);

/* ─── Disk formats per drive ─────────────────────────────────────────────────
 * The built-in disk formats that geometrically fit the drive configured in a
 * slot (for the GUI format selection).  The drive-type default is index 0.
 * Returned name pointers stay valid until the next call on the same thread. */
/** @brief Number of built-in formats compatible with the drive in @p drive. */
K1520_API int         k1520_drive_format_count(K1520Handle h, int drive);
/** @brief Name of the @p index-th compatible format (NULL if out of range). */
K1520_API const char* k1520_drive_format_name(K1520Handle h, int drive, int index);
/** @brief Drive-type default format name for @p drive (what empty-create uses). */
K1520_API const char* k1520_drive_default_format(K1520Handle h, int drive);
/** @brief Human-readable description of a catalog format ("" if unknown). */
K1520_API const char* k1520_format_description(K1520Handle h, const char* name);
/** @brief Colon-separated list of the loaded formats.yaml file(s) — diagnostics. */
K1520_API const char* k1520_formats_source(K1520Handle h);
/** @brief Return true if a disk image is mounted in the drive. */
K1520_API bool k1520_disk_active(K1520Handle h, int drive);
/** @brief Return true if mounted image is write protected. */
K1520_API bool k1520_disk_write_protected(K1520Handle h, int drive);
/** @brief Return true while the drive LED should be lit (drive selected or motor on). */
K1520_API bool k1520_disk_led(K1520Handle h, int drive);
/** @brief Return true while the drive's spindle motor is running (/LCK, port 0x18). */
K1520_API bool k1520_disk_motor(K1520Handle h, int drive);
/** @brief Return true while the read/write head is loaded (/HL, ctrl port A bit6). */
K1520_API bool k1520_head_loaded(K1520Handle h);
/** @brief Update mounted image write-protect state. */
K1520_API void k1520_set_write_protect(K1520Handle h, int drive, bool wp);

/* ─── Serial ports ───────────────────────────────────────────────────────── */
/* Alter Unterbau für Tests (Entwurf 19 §8): set_rx_cb liefert die Bytes, die der Gast
 * SENDET (cb == NULL meldet ab), send legt ein Byte in seinen Empfänger.  Belegt ein
 * Transport oder der Rx/Tx-Loop die Schnittstelle, gehen beide ins Leere.
 * A5120: DFU = DFÜ/V.24 (K8025 A33-A), PRINTER = Drucker (A32-B);
 * K8915: DFU = DFÜ/IFSS2 (SIO2-A), PRINTER = Drucker/IFSS1 (SIO1-B). */
K1520_API void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                                       K1520SerialCallback cb, void* ctx);
K1520_API void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte);

/* ─── Serielle Schnittstellen nach außen (Entwurf 19 §8, AP-S6) ───────────────
 * Je Maschine eine Liste einstellbarer Schnittstellen (Index = Reihenfolge der Karten,
 * A5120: 0 DFÜ/V.24, 1 DFÜ/IFSS, 2 Drucker; K8915: 0 Drucker/IFSS1, 1 V.24, 2 DFÜ/IFSS2).
 * Die Betriebsart (Telnet / RFC 2217 / Datei) läuft in einem eigenen I/O-Faden des
 * Kerns; die GUI FRAGT AB
 * (k1520_serial_status), es gibt keinen Rückruf aus dem I/O-Faden.  Alle Funktionen dürfen
 * aus jedem Faden gerufen werden, auch während k1520_run() läuft.
 *
 * Regel für `groesse` (ABI-Erweiterung ohne Bruch): der Aufrufer setzt vor dem Aufruf
 * `groesse = sizeof(Struktur)` seiner Fassung des Headers.
 *   - Ausgabestrukturen (Info, Status): der Kern schreibt nur so viele Bytes, wie `groesse`
 *     angibt (nie über das Ende), und trägt in `groesse` die Zahl der tatsächlich
 *     geschriebenen Bytes zurück.  `groesse` < 4 → false, nichts geschrieben.
 *   - Eingabestruktur (Konfig): Felder jenseits von `groesse` behalten ihren aktuellen Wert;
 *     `groesse` < 4 → false.
 * Zeichenketten: UTF-8, nullterminiert, bei zu kleinem Feld an einer Zeichengrenze
 * abgeschnitten.  Aufzählungen sind `int` (Zahlenwerte = Entwurf 19 §8). */
typedef enum { K1520_SER_TELNET = 0, K1520_SER_RFC2217 = 1, K1520_SER_DATEI = 2 } K1520SerBetriebsart;
typedef enum { K1520_SER_SERVER = 0, K1520_SER_CLIENT = 1 } K1520SerRolle;
typedef enum { K1520_SER_AUS = 0, K1520_SER_VERBINDET, K1520_SER_LAUSCHT,
               K1520_SER_VERBUNDEN, K1520_SER_FEHLER } K1520SerZustand;
typedef enum { K1520_HOST_UNGUELTIG = 0, K1520_HOST_IPV4, K1520_HOST_IPV6,
               K1520_HOST_NAME } K1520HostArt;

typedef struct {
    uint32_t groesse;
    char     name[32];
    char     stecker[8];
    bool     v24;                       /* Steuerleitungen RTS/CTS/DTR/DSR/DCD vorhanden */
    int      taktquellen;               /* 0 = fester Takt; sonst Zahl der Einträge (max. 4) */
    char     taktquelle_name[4][32];
} K1520SerInfo;

typedef struct {
    uint32_t groesse;
    int      betriebsart;               /* K1520SerBetriebsart */
    int      rolle;                     /* K1520SerRolle */
    char     host[256];
    uint16_t port;                      /* 1..65535; 0 wird von k1520_serial_configure abgewiesen */
    bool     loop, rtscts_bruecke, xonxoff;
    int      taktquelle;
    char     datei[1024];
} K1520SerKonfig;

typedef struct {
    uint32_t groesse;
    int      zustand;                   /* K1520SerZustand */
    uint16_t port_aktiv;                /* Server: tatsächlicher Port */
    char     gegenstelle[96];
    char     meldung[160];
    uint32_t baud_nenn;
    uint8_t  daten, paritaet, stopp_halbe;
    bool     format_gueltig;
    uint32_t baud_gegenseite;           /* RFC 2217; 0 = unbekannt */
    bool     baud_abweichend;
    bool     rts, cts, dtr, dsr, dcd;
    uint64_t bytes_gesendet, bytes_empfangen;
    uint32_t puffer_senden, puffer_empfangen;
    uint16_t port_vorschlag;            /* freier Port, wenn der eingestellte belegt war (start_auto) */
    int      rolle, betriebsart;
    uint32_t versuche;                  /* Client: Versuche seit dem letzten Verbinden */
    /* AP-S11: Format und Steuerleitungen der Gegenseite (nur RFC 2217; Telnet/Datei: nichts
     * bekannt).  Rolle Server (Gegenseite = Client): Format = zuletzt GEWUENSCHTE Werte
     * (SET-DATASIZE/-PARITY/-STOPSIZE); Leitungen = RTS, DTR des Clients (ab dem ersten
     * SET-CONTROL der jeweiligen Leitung).  Rolle Client (Gegenseite = Server): Format =
     * Antwort des Servers auf unsere SET-*; Leitungen = CTS, DSR, DCD, RI aus
     * NOTIFY-MODEMSTATE (ab der ersten Meldung).  Was die Rolle nicht liefert, bleibt in
     * leitungen_gegenseite_bekannt aus. */
    uint8_t  daten_gegenseite;          /* 5..8; 0 = unbekannt */
    uint8_t  paritaet_gegenseite;       /* K1520_SER_PAR_*; nur mit format_gegenseite_bekannt */
    uint8_t  stopp_halbe_gegenseite;    /* halbe Stoppbits (2, 3, 4); 0 = unbekannt */
    bool     format_gegenseite_bekannt; /* alle drei Formatfelder bekannt */
    bool     format_abweichend;         /* Format bekannt und != Format des Gastes (ohne Baud) */
    uint8_t  leitungen_gegenseite;      /* Bitmaske K1520_SER_L_*; nur Bits aus ..._bekannt */
    uint8_t  leitungen_gegenseite_bekannt;  /* Bitmaske: welche Leitungen die Rolle kennt */
} K1520SerStatus;

/* Paritaet der Gegenseite (paritaet_gegenseite) */
#define K1520_SER_PAR_KEINE 0
#define K1520_SER_PAR_UNGERADE 1
#define K1520_SER_PAR_GERADE 2
#define K1520_SER_PAR_MARK 3
#define K1520_SER_PAR_SPACE 4
/* Bits von leitungen_gegenseite(_bekannt) */
#define K1520_SER_L_RTS 0x01
#define K1520_SER_L_DTR 0x02
#define K1520_SER_L_CTS 0x04
#define K1520_SER_L_DSR 0x08
#define K1520_SER_L_DCD 0x10
#define K1520_SER_L_RI  0x20

/** @brief Zahl der einstellbaren Schnittstellen (0 bei einer Maschine ohne). */
K1520_API int  k1520_serial_count(K1520Handle h);
K1520_API bool k1520_serial_info(K1520Handle h, int i, K1520SerInfo* out);
/** @brief Name der @p i-ten FESTEN Schnittstelle (Tastatur-Zeilen; nicht einstellbar).
 *         false jenseits der Liste — so wird sie durchlaufen. */
K1520_API bool k1520_serial_fixed_name(K1520Handle h, int i, char* buf, int n);
/** @brief Einstellungen lesen (die zuletzt übernommenen). */
K1520_API bool k1520_serial_get_config(K1520Handle h, int i, K1520SerKonfig* out);
/** @brief Einstellungen übernehmen.  false (nichts übernommen) bei: ungültigem Index,
 *         Port 0 oder ungültiger Betriebsart/Rolle/Taktquelle, oder wenn die Schnittstelle
 *         aktiv ist und ein gesperrtes Feld (Betriebsart, Rolle, Host, Port, Datei) geändert wird. */
K1520_API bool k1520_serial_configure(K1520Handle h, int i, const K1520SerKonfig* k);
/** @brief Start von Hand: Server mit Portsuche, Client im Dauerversuch, Datei überschreibend. */
K1520_API bool k1520_serial_start(K1520Handle h, int i);
/** @brief Wiederaufnahme beim Programmstart: Server NUR auf dem eingestellten Port (belegt →
 *         false, Status AUS mit `port_vorschlag`), Datei anhängend. */
K1520_API bool k1520_serial_start_auto(K1520Handle h, int i);
K1520_API void k1520_serial_stop(K1520Handle h, int i);
K1520_API bool k1520_serial_status(K1520Handle h, int i, K1520SerStatus* out);
/** @brief Host-Feld klassifizieren (K1520HostArt). */
K1520_API int  k1520_serial_classify_host(const char* host);

/* ─── A5120.16: Erweiterungsmodul EM064/EM256 mit U8001 ─────────────────────
 * doc/design/17_a5120_16.md (S4/S5).  Ohne EM (k1520_create/_configured) liefern
 * alle k1520_em_* „nichts": Variante "", LEDs aus, Zustand false. */

/**
 * @brief Wie k1520_create_configured, zusätzlich mit Erweiterungsmodul.
 *
 * @param em  "none" / NULL / "" = A5120 ohne EM, "em064" (U8002, 64 KB),
 *            "em256" (U8001, 256 KB) = A5120.16.  Unbekannter Name → NULL
 *            (Grund in k1520_last_init_error).
 */
K1520_API K1520Handle k1520_create_with_em(K1520MachineType type,
                                           const char* drive0, const char* drive1,
                                           const char* drive2, const char* drive3,
                                           const char* em);
/** @brief Bestückung: "" (kein EM), "em064" oder "em256". */
K1520_API const char* k1520_em_variant(K1520Handle h);
/** @brief LED V1 der Steuerkarte — zeigt RAMEN (nicht den Paritätsfehler). */
K1520_API bool k1520_em_led_v1(K1520Handle h);
/** @brief LED V2 der Steuerkarte — leuchtet im 8-Bit-Mode (FF A29). */
K1520_API bool k1520_em_led_v2(K1520Handle h);
/** @brief true im 16-Bit-Mode (FF A29 zurückgesetzt, der U8001 hat den Bus). */
K1520_API bool k1520_em_mode16(K1520Handle h);

/** Zustand von U8001 und Steuerkarte (Momentaufnahme, nur lesen). */
typedef struct {
    uint16_t r[14];          /* R0..R13 (ungebankt) */
    uint16_t r14[2];         /* R14 [0] = Normal, [1] = System */
    uint16_t r15[2];         /* R15 [0] = Normal, [1] = System */
    uint16_t fcw;
    uint16_t pc;
    uint16_t psap_seg;       /* wie LDCTL PSAPSEG */
    uint16_t psap_off;
    uint16_t refresh;
    uint8_t  pc_seg;
    uint8_t  model;          /* 1 = U8001 (Z8001), 2 = U8002 */
    uint64_t cycles;         /* Takte des U8001 */
    /* Zustand der CPU */
    bool     in_reset, halted, stopped, bus_ack, mo_active;
    /* Steuerkarte */
    bool     mode8;          /* FF A29: 1 = 8-Bit-Mode */
    bool     ramen, tren, trq8, busrq16, stop16, reset16, vi_pending, nvi, parity_error;
    uint8_t  a33, a35, status8, vector8, a53, segment, seg_mode, reserved;
} K1520EmState;

/** @brief sizeof(K1520EmState) — damit die Bindung ihren Aufbau prüfen kann. */
K1520_API int  k1520_em_state_size(void);
/** @brief Zustand lesen; false ohne EM (dann bleibt @p out unverändert). */
K1520_API bool k1520_em_state(K1520Handle h, K1520EmState* out);

/* ─── Debug ──────────────────────────────────────────────────────────────── */
/** @brief Read memory through the machine bus for diagnostics. */
K1520_API uint8_t     k1520_mem_read(K1520Handle h, uint16_t addr);
/** @brief Write memory through the machine bus for diagnostics. */
K1520_API void        k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data);
/** @brief Read I/O port through the machine bus for diagnostics. */
K1520_API uint8_t     k1520_io_read(K1520Handle h, uint8_t port);
K1520_API const char* k1520_last_error(K1520Handle h);
K1520_API const char* k1520_version(void);

/* ─── Machine-neutral indicators (appended 2026-09-28, AP-E4b) ────────────────
 * Every machine answers these; the A5120 has no panel and no bell counter and
 * returns 0.  The K8915 mirrors its indicators at the end of each k1520_run(),
 * so they may be read from any thread. */
/** @brief K1520MachineType of the handle (0 = A5120, 1 = PRG 710/710-1, 2 = K8915). */
K1520_API int      k1520_machine_type(K1520Handle h);
/**
 * @brief Raw byte of the text screen memory (80 × 24, bit 7 = attribute/cursor),
 *        read directly from the screen card — NOT through the CPU view.
 *
 * On the K8915 the CPU view (k1520_mem_read) of the screen memory at 1000H is
 * covered by ZRE RAM as soon as port A8H bit0 is set; use this function to read
 * the screen.  0 for col/row outside 0…79 / 0…23.  Not thread-safe (like mem_read).
 */
K1520_API uint8_t  k1520_screen_char(K1520Handle h, int col, int row);
/**
 * @brief Front panel lamps: raw byte of the K8915 display latch at port 61H,
 *        ACTIVE LOW (bit4 read, bit5 write, bit6 ready, bit7 error; FFH after
 *        /RESET = all dark).  A5120: 0 (no panel — check k1520_machine_type).
 */
K1520_API uint8_t  k1520_panel_lamps(K1520Handle h);
/**
 * @brief Running count of bell tones (K8915: BEL received by the K7672).
 *        Never decreases; the GUI beeps on the difference.  A5120: 0.
 */
K1520_API uint32_t k1520_bell_count(K1520Handle h);
/**
 * @brief NMI button (K8915 front panel): one /NMI edge, delivered at the start of
 *        the next k1520_run().  No /RESET — memory map, SIO, CTC stay as they are.
 *        With the boot ROM mapped (A8H bit0 = 0) the ROM restarts its self-test
 *        (0066H); under SCPX RAM sits at 0066H and the CPU jumps there, exactly
 *        as on the device (not intercepted).  A5120: no effect (no NMI button).
 *        Thread-safe.
 */
K1520_API void     k1520_nmi(K1520Handle h);

/**
 * @brief Variante eines PRG-Handles: 0 = PRG 710, 1 = PRG 710-1; -1 bei anderer Maschine.
 */
K1520_API int      k1520_prg710_variant(K1520Handle h);
/**
 * @brief Speicherverwaltung des PRG (Ports E8H/EAH), Diagnose für Debugger/Oberfläche.
 * @param n      Seite 0…15 (A12–A15).
 * @param attr   E8H dieser Seite (unteres Halbbyte 0 = OPS-RAM, sonst Systemkarte); darf NULL sein.
 * @param seite  EAH dieser Seite (physische OPS-Seite, 4 Bit); darf NULL sein.
 * @return false bei anderer Maschine oder n ausserhalb 0…15 (Ausgaben unberührt).
 */
K1520_API bool     k1520_prg710_page(K1520Handle h, int n, uint8_t* attr, uint8_t* seite);
/** @brief Freigabe-Register EBH (0 = Abbildung aus); -1 bei anderer Maschine. */
K1520_API int      k1520_prg710_freigabe(K1520Handle h);

#ifdef __cplusplus
}
#endif
