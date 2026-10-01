#include "k1520_api.h"
#include "core/api/k1520_sync_internal.h"
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/machine.h"
#include "core/peripherals/k7637/k7637.h"
#include "core/logger.h"
#include <cstring>
#include <memory>
#include <string>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "core/version.h"
#define VERSION K1520_VERSION_TEXT

// Das Handle zeigt IMMER auf die Basisklasse — erzeugt wird es in makeMachine()
// ausdrücklich als K1520Machine*, damit der Rückweg über void* kein Zeigerversatz
// einer Mehrfachvererbung verfehlen kann.
static K1520Machine* toMachine(K1520Handle h) {
    return static_cast<K1520Machine*>(h);
}

// Grund eines fehlgeschlagenen k1520_create*.  Ein Startabbruch (z. B. fehlender
// Diskettenformat-Katalog) liefert KEIN Handle — die Meldung muss deshalb ohne
// Handle abrufbar sein (k1520_last_init_error).
static std::string g_init_error;

// Helper to create and set up log file on first machine creation
static void setup_logging() {
    static bool logging_initialized_ = false;
    if (logging_initialized_) return;
    logging_initialized_ = true;

#if LOG_LEVEL > 0
    // Dateiname mit Zeitstempel: logs/k1520_YYYYMMDD_HHMMSS.log
    //
    // Das Unterverzeichnis ist Absicht: JEDES k1520_create legt eine Logdatei an
    // (GUI-Start, Testlauf, Werkzeugaufruf), und ins Arbeitsverzeichnis geschrieben
    // schwemmt das binnen Tagen dreistellige Dateizahlen ins Projektwurzelverzeichnis.
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char stamp[64];
    std::strftime(stamp, sizeof(stamp), "k1520_%Y%m%d_%H%M%S.log", tm);

    std::error_code ec;
    std::filesystem::create_directories("logs", ec);

    // Schlägt das Anlegen fehl (schreibgeschütztes CWD), bleibt es beim
    // Arbeitsverzeichnis; scheitert auch das, behält der Logger stderr.
    auto& logger = k1520::logging::Logger::instance();
    if (ec || !logger.setOutputFile(std::string("logs/") + stamp))
        logger.setOutputFile(stamp);
#endif
}

extern "C" {

K1520Handle k1520_create(K1520MachineType type) {
    return k1520_create_configured(type, nullptr, nullptr, nullptr, nullptr);
}

K1520Handle k1520_create_configured(K1520MachineType type,
                                    const char* d0, const char* d1,
                                    const char* d2, const char* d3) {
    g_init_error.clear();
    if (type != K1520_MACHINE_A5120 && type != K1520_MACHINE_K8915) {
        // PRG710 ist im Typ vorgesehen, aber nicht gebaut.  Kein stilles NULL: die
        // Oberfläche soll sagen können, warum.
        g_init_error = "Maschinentyp " + std::to_string(static_cast<int>(type)) +
                       " ist noch nicht implementiert (nur A5120, K8915)";
        return nullptr;
    }

    setup_logging();

    try {
        const char* names[4] = { d0, d1, d2, d3 };
        K1520Machine* m = nullptr;
        if (type == K1520_MACHINE_K8915) {
            // doc/design/16_k8915.md §8a AP-E4b.  Vorgabe = Gerät des Anwenders:
            // K5601/K5601/none/none; NULL/"" behält den Platz bei dieser Vorgabe.
            K8915Machine::Config cfg;
            for (int i = 0; i < 4; ++i)
                if (names[i] && names[i][0]) cfg.laufwerke[i] = names[i];
            m = new K8915Machine(cfg);
        } else {
            A5120Machine::Config cfg;                  // Default = 4× K5601
            for (int i = 0; i < 4; ++i)
                if (names[i] && names[i][0]) cfg.drive_profiles[i] = names[i];
            m = new A5120Machine(cfg);
        }
        return m;
    } catch (const std::exception& e) {
        g_init_error = e.what();
        std::fprintf(stderr, "k1520: %s\n", g_init_error.c_str());
        return nullptr;
    } catch (...) {
        g_init_error = "Unbekannter Fehler beim Erzeugen der Maschine";
        return nullptr;
    }
}

const char* k1520_last_init_error(void) {
    return g_init_error.c_str();
}

void k1520_destroy(K1520Handle h) {
    delete toMachine(h);
}

void k1520_reset(K1520Handle h)     { toMachine(h)->reset(); }
void k1520_power_on(K1520Handle h)  { toMachine(h)->powerOn(); }

int  k1520_run(K1520Handle h, int max_cycles) {
    return toMachine(h)->run(max_cycles);
}

void k1520_stop(K1520Handle h) { toMachine(h)->stop(); }

const uint8_t* k1520_framebuffer(K1520Handle h) {
    return toMachine(h)->framebuffer();
}

int  k1520_fb_width(K1520Handle h)  { return toMachine(h)->fbWidth(); }
int  k1520_fb_height(K1520Handle h) { return toMachine(h)->fbHeight(); }

bool k1520_fb_dirty(K1520Handle h) {
    return toMachine(h)->fbDirty();
}

void k1520_fb_clear_dirty(K1520Handle h) {
    toMachine(h)->fbClearDirty();
}

void k1520_set_console_mode(K1520Handle h, bool enable) {
    toMachine(h)->setConsoleMode(enable);
}

bool k1520_console_poll(K1520Handle h, int* x, int* y, char* ch) {
    int cx, cy; char c;
    if (toMachine(h)->consolePoll(cx, cy, c)) {
        if (x)  *x  = cx;
        if (y)  *y  = cy;
        if (ch) *ch = c;
        return true;
    }
    return false;
}

void k1520_key_press(K1520Handle h, uint32_t kc, bool shift, bool ctrl) {
    toMachine(h)->keyPress(kc, shift, ctrl);
}

void k1520_key_release(K1520Handle h, uint32_t kc) {
    toMachine(h)->keyRelease(kc);
}

uint8_t k1520_translate_key(uint32_t keycode, bool shift, bool ctrl) {
    return K7637::codeFor(static_cast<int>(keycode), shift, ctrl);
}

uint32_t k1520_keyboard_leds(K1520Handle h) {
    return toMachine(h)->keyboardLeds();
}

void k1520_console_key(K1520Handle h, char c) {
    // Inject ASCII char as if typed (keycode = ASCII value, no modifiers)
    toMachine(h)->keyPress(static_cast<uint32_t>(c), false, false);
}

bool k1520_mount_disk(K1520Handle h, int drive,
                      const char* image_path, const char* format_name,
                      bool write_protect) {
    if (!image_path || !format_name) return false;
    return toMachine(h)->mountDisk(drive, image_path, format_name, write_protect);
}

bool k1520_create_disk(K1520Handle h, int drive,
                       const char* image_path, const char* format_name,
                       bool write_protect) {
    if (!image_path) return false;
    // NULL/"" format_name → genuinely blank, unformatted disk in drive geometry.
    return toMachine(h)->createDisk(drive, image_path,
                                  format_name ? format_name : "", write_protect);
}

bool k1520_save_disk_as(K1520Handle h, int drive,
                        const char* image_path, const char* format_name) {
    if (!image_path) return false;
    return toMachine(h)->saveDiskAs(drive, image_path, format_name ? format_name : "");
}

bool k1520_disk_raw_compatible(K1520Handle h, int drive) {
    return toMachine(h)->isDiskRawCompatible(drive);
}

const char* k1520_disk_path(K1520Handle h, int drive) {
    static thread_local std::string buf;
    buf = toMachine(h)->diskPath(drive);
    return buf.c_str();
}

const char* k1520_disk_container(K1520Handle h, int drive) {
    static thread_local std::string buf;
    buf = toMachine(h)->diskContainer(drive);
    return buf.c_str();
}

const char* k1520_disk_detected_format(K1520Handle h, int drive) {
    static thread_local std::string buf;
    buf = toMachine(h)->detectedFormatName(drive);
    return buf.c_str();
}

const char* k1520_disk_notice(K1520Handle h, int drive) {
    static thread_local std::string buf;
    buf = toMachine(h)->diskNotice(drive);
    return buf.c_str();
}

bool k1520_mount_physical(K1520Handle h, int drive, K1520Sync sync, bool write_protect) {
    // Das Abbild wandert AUS dem Synchronisierer-Handle ins Laufwerk; der
    // Synchronisierer selbst bleibt beim Handle, damit der Arbeitsfaden weiterarbeitet
    // (doc/design/14_physische_diskette.md §10).
    std::unique_ptr<DiskImage> abbild = k1520s_take_image(sync);
    if (!abbild) return false;
    return toMachine(h)->mountDiskImage(drive, std::move(abbild), write_protect);
}

bool k1520_flush_disks(K1520Handle h) {
    return toMachine(h)->flushDisks();
}

bool k1520_unmount_disk(K1520Handle h, int drive) {
    return toMachine(h)->unmountDisk(drive);
}

int k1520_drive_format_count(K1520Handle h, int drive) {
    return static_cast<int>(toMachine(h)->compatibleFormats(drive).size());
}

const char* k1520_drive_format_name(K1520Handle h, int drive, int index) {
    static thread_local std::string buf;
    const auto v = toMachine(h)->compatibleFormats(drive);
    if (index < 0 || index >= static_cast<int>(v.size())) return nullptr;
    buf = v[static_cast<size_t>(index)];
    return buf.c_str();
}

const char* k1520_drive_default_format(K1520Handle h, int drive) {
    static thread_local std::string buf;
    buf = toMachine(h)->defaultFormatName(drive);
    return buf.c_str();
}

const char* k1520_format_description(K1520Handle h, const char* name) {
    static thread_local std::string buf;
    buf = name ? toMachine(h)->formatDescription(name) : std::string();
    return buf.c_str();
}

const char* k1520_formats_source(K1520Handle h) {
    static thread_local std::string buf;
    buf.clear();
    for (const auto& s : toMachine(h)->formatCatalog().sources()) {
        if (!buf.empty()) buf += ":";
        buf += s;
    }
    return buf.c_str();
}

bool k1520_disk_active(K1520Handle h, int drive) {
    return toMachine(h)->isDiskActive(drive);
}

bool k1520_disk_write_protected(K1520Handle h, int drive) {
    return toMachine(h)->isDiskWriteProtected(drive);
}

bool k1520_disk_led(K1520Handle h, int drive) {
    return toMachine(h)->isDiskLedOn(drive);
}

bool k1520_disk_motor(K1520Handle h, int drive) {
    return toMachine(h)->isMotorOn(drive);
}

bool k1520_head_loaded(K1520Handle h) {
    return toMachine(h)->isHeadLoaded();
}

void k1520_set_write_protect(K1520Handle h, int drive, bool wp) {
    toMachine(h)->setDiskWriteProtect(drive, wp);
}

void k1520_serial_set_rx_cb(K1520Handle h, K1520SerialPort port,
                              K1520SerialCallback cb, void* ctx) {
    auto m = toMachine(h);
    // Alter Unterbau (Entwurf 19 §8, seit AP-S5 über die Anschlüsse der Karten): der
    // Rückruf bekommt die Bytes, die der Gast sendet, in ihrer Zeichenzeit — aber nur,
    // solange weder ein Transport (k1520_serial_start) noch der Rx/Tx-Loop die
    // Schnittstelle belegt; sonst geht er ins Leere.  Ein leerer Rückruf (cb == NULL)
    // meldet ab.  A5120: DFU = DFÜ/V.24, PRINTER = Drucker (A32-B);
    // K8915: DFU = IFS 2, PRINTER = IFS 1.
    K1520Machine::SerialCb f;
    if (cb) f = [cb, ctx](uint8_t b) { cb(ctx, b); };
    if (port == K1520_SERIAL_DFU)
        m->setDFUECallback(std::move(f));
    else if (port == K1520_SERIAL_PRINTER)
        m->setPrinterCallback(std::move(f));
}

// Alter Unterbau: das Byte landet sofort im Empfänger — ins Leere, solange ein
// Transport oder der Loop die Schnittstelle belegt (Entwurf 19 §8).
void k1520_serial_send(K1520Handle h, K1520SerialPort port, uint8_t byte) {
    auto m = toMachine(h);
    if (port == K1520_SERIAL_DFU)
        m->dfueSend(byte);
    else if (port == K1520_SERIAL_PRINTER)
        m->printerSend(byte);
}

uint8_t k1520_mem_read(K1520Handle h, uint16_t addr) {
    // Direct bus access for debugging (not thread-safe, call from run thread).
    return toMachine(h)->memReadDebug(addr);
}

void k1520_mem_write(K1520Handle h, uint16_t addr, uint8_t data) {
    toMachine(h)->memWriteDebug(addr, data);
}

uint8_t k1520_io_read(K1520Handle h, uint8_t port) {
    return toMachine(h)->ioReadDebug(port);
}

const char* k1520_last_error(K1520Handle h) {
    static thread_local std::string buf;
    buf = toMachine(h)->lastError();
    return buf.c_str();
}

const char* k1520_version(void) {
    return VERSION;
}

// ─── Maschinenneutrale Anzeigen (doc/design/16_k8915.md §8a AP-E4b) ─────────

int k1520_machine_type(K1520Handle h) {
    return toMachine(h)->machineType();
}

uint8_t k1520_screen_char(K1520Handle h, int col, int row) {
    if (col < 0 || col >= 80 || row < 0 || row >= 24) return 0;
    return toMachine(h)->screenChar(col, row);
}

uint8_t k1520_panel_lamps(K1520Handle h) {
    return toMachine(h)->panelLamps();
}

uint32_t k1520_bell_count(K1520Handle h) {
    return toMachine(h)->bellCount();
}

void k1520_nmi(K1520Handle h) {
    toMachine(h)->nmi();
}

} // extern "C"
