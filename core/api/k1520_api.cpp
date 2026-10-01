#include "k1520_api.h"
#include "core/api/k1520_sync_internal.h"
#include "core/machines/a5120/a5120.h"
#include "core/machines/k8915/k8915.h"
#include "core/machines/machine.h"
#include "core/peripherals/k7637/k7637.h"
#include "core/logger.h"
#include "core/serial/hub.h"
#include "core/serial/net/adresse.h"
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

// Erweiterungsmodul (A5120.16): nur ein A5120 kann eins tragen; am K8915 „keins“.
static const EM* emOf(K1520Handle h) {
    auto* a = dynamic_cast<A5120Machine*>(toMachine(h));
    return a ? a->em() : nullptr;
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

K1520Handle k1520_create_with_em(K1520MachineType type,
                                 const char* d0, const char* d1,
                                 const char* d2, const char* d3, const char* em) {
    const std::string e = em ? em : "";
    // Ohne EM ist es dieselbe Maschine wie k1520_create_configured — auch der K8915.
    if (e.empty() || e == "none") return k1520_create_configured(type, d0, d1, d2, d3);
    if (type != K1520_MACHINE_A5120) {
        g_init_error = "Ein Erweiterungsmodul gibt es nur am A5120 (A5120.16)";
        return nullptr;
    }

    setup_logging();

    try {
        g_init_error.clear();
        A5120Machine::Config cfg;
        const char* names[4] = { d0, d1, d2, d3 };
        for (int i = 0; i < 4; ++i)
            if (names[i] && names[i][0]) cfg.drive_profiles[i] = names[i];
        if (e == "em064")         cfg.em = A5120Machine::Config::Em::em064;
        else if (e == "em256")         cfg.em = A5120Machine::Config::Em::em256;
        else {
            g_init_error = "Unbekanntes Erweiterungsmodul '" + e + "' (none|em064|em256)";
            return nullptr;
        }
        K1520Machine* m = new A5120Machine(cfg);   // Handle = K1520Machine* (s. toMachine)
        return m;
    } catch (const std::exception& ex) {
        g_init_error = ex.what();
        std::fprintf(stderr, "k1520: %s\n", g_init_error.c_str());
        return nullptr;
    } catch (...) {
        g_init_error = "Unbekannter Fehler beim Erzeugen der Maschine";
        return nullptr;
    }
}

const char* k1520_em_variant(K1520Handle h) {
    const EM* em = emOf(h);
    if (!em) return "";
    return em->config().variante == EM::Variante::EM256 ? "em256" : "em064";
}

bool k1520_em_led_v1(K1520Handle h) {
    const EM* em = emOf(h);
    return em && em->ledV1();
}

bool k1520_em_led_v2(K1520Handle h) {
    const EM* em = emOf(h);
    return em && em->ledV2();
}

bool k1520_em_mode16(K1520Handle h) {
    const EM* em = emOf(h);
    return em && !em->mode8();
}

int k1520_em_state_size(void) {
    return static_cast<int>(sizeof(K1520EmState));
}

bool k1520_em_state(K1520Handle h, K1520EmState* out) {
    const EM* em = emOf(h);
    if (!em || !out) return false;
    const Z8000& z = em->u8001();
    K1520EmState s{};
    for (int i = 0; i < 14; ++i) s.r[i] = z.Rg[i];
    s.r14[0] = z.R14[0]; s.r14[1] = z.R14[1];
    s.r15[0] = z.R15[0]; s.r15[1] = z.R15[1];
    s.fcw = z.fcw; s.pc = z.pc; s.pc_seg = z.pcSeg;
    s.psap_seg = z.psapSeg; s.psap_off = z.psapOff; s.refresh = z.refresh;
    s.model = z.isZ8001() ? 1 : 2;
    s.cycles = z.cycles;
    s.in_reset = z.inReset(); s.halted = z.halted(); s.stopped = z.stopped();
    s.bus_ack = z.busAck(); s.mo_active = z.moActive();
    s.mode8 = em->mode8(); s.ramen = em->ramEnabled(); s.tren = em->tren();
    s.trq8 = em->trq8(); s.busrq16 = em->busRq16(); s.stop16 = em->stop16();
    s.reset16 = em->reset16(); s.vi_pending = em->viPending(); s.nvi = em->nviLine();
    s.parity_error = em->parityError();
    s.a33 = em->steuer16(); s.a35 = em->status16(); s.status8 = em->status8();
    s.vector8 = em->vector8(); s.a53 = em->a53(); s.segment = em->segment();
    s.seg_mode = em->segMode();
    *out = s;
    return true;
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
    // K8915: DFU = DFÜ/IFSS2, PRINTER = Drucker/IFSS1.
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

// ─── Serielle Schnittstellen nach außen (Entwurf 19 §8) ─────────────────────────

// Die ganze Datei steht in extern "C" — Vorlagen brauchen C++-Bindung.
extern "C++" {
namespace {

using k1520::serial::SerialHub;

SerialHub* hubOf(K1520Handle h) { return toMachine(h)->serialHub(); }

// Gültiger Index → Hub, sonst nullptr.
SerialHub* hubFor(K1520Handle h, int i) {
    SerialHub* hub = hubOf(h);
    return (hub && i >= 0 && i < hub->anzahl()) ? hub : nullptr;
}

// Nullterminiert und höchstens n-1 Bytes; ein UTF-8-Zeichen wird nie zerschnitten
// (Fortsetzungsbytes 10xxxxxx am Schnitt gehören zum davor begonnenen Zeichen).
template <size_t N>
void copyStr(char (&dst)[N], const std::string& src) {
    size_t n = src.size() < N - 1 ? src.size() : N - 1;
    if (n < src.size())
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) --n;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// Feld mit Nullterminator-Sicherung lesen (ein Aufrufer könnte unterminiert füllen).
template <size_t N>
std::string readStr(const char (&src)[N]) {
    size_t n = 0;
    while (n < N && src[n]) ++n;
    return std::string(src, n);
}

// Ausgabestruktur nach der groesse-Regel (Header): nur so viele Bytes, wie der Aufrufer
// angibt; groesse trägt zurück, wie viel geschrieben wurde.  false bei groesse < 4.
template <class S>
bool deliver(S* out, const S& full) {
    if (!out || out->groesse < sizeof(uint32_t)) return false;
    const uint32_t n = out->groesse < sizeof(S) ? out->groesse : static_cast<uint32_t>(sizeof(S));
    S tmp = full;
    tmp.groesse = n;
    std::memcpy(out, &tmp, n);
    return true;
}

K1520SerKonfig toAbi(const k1520::serial::SerialKonfig& k) {
    K1520SerKonfig r;
    std::memset(&r, 0, sizeof r);
    r.groesse = sizeof r;
    r.betriebsart = static_cast<int>(k.betriebsart);
    r.rolle = static_cast<int>(k.rolle);
    copyStr(r.host, k.host);
    r.port = k.port;
    r.loop = k.loop;
    r.rtscts_bruecke = k.rtscts_bruecke;
    r.xonxoff = k.xonxoff;
    r.taktquelle = k.taktquelle;
    copyStr(r.datei, k.datei);
    return r;
}

}  // namespace
}  // extern "C++"

int k1520_serial_count(K1520Handle h) {
    SerialHub* hub = hubOf(h);
    return hub ? hub->anzahl() : 0;
}

bool k1520_serial_info(K1520Handle h, int i, K1520SerInfo* out) {
    SerialHub* hub = hubFor(h, i);
    if (!hub) return false;
    const auto in = hub->info(i);
    K1520SerInfo r;
    std::memset(&r, 0, sizeof r);
    copyStr(r.name, in.name);
    copyStr(r.stecker, in.stecker);
    r.v24 = in.v24;
    r.taktquellen = static_cast<int>(in.taktquellen.size() < 4 ? in.taktquellen.size() : 4);
    for (int q = 0; q < r.taktquellen; ++q) copyStr(r.taktquelle_name[q], in.taktquellen[q]);
    return deliver(out, r);
}

bool k1520_serial_fixed_name(K1520Handle h, int i, char* buf, int n) {
    const auto fest = toMachine(h)->festeSchnittstellen();
    if (!buf || n <= 0 || i < 0 || i >= static_cast<int>(fest.size())) return false;
    const std::string& s = fest[static_cast<size_t>(i)];
    size_t k = s.size() < static_cast<size_t>(n) - 1 ? s.size() : static_cast<size_t>(n) - 1;
    if (k < s.size())
        while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    std::memcpy(buf, s.data(), k);
    buf[k] = '\0';
    return true;
}

bool k1520_serial_get_config(K1520Handle h, int i, K1520SerKonfig* out) {
    SerialHub* hub = hubFor(h, i);
    return hub && deliver(out, toAbi(hub->konfig(i)));
}

bool k1520_serial_configure(K1520Handle h, int i, const K1520SerKonfig* k) {
    SerialHub* hub = hubFor(h, i);
    if (!hub || !k || k->groesse < sizeof(uint32_t)) return false;
    // Felder jenseits von groesse (ältere Aufrufer) behalten ihren aktuellen Wert.
    K1520SerKonfig full = toAbi(hub->konfig(i));
    const size_t n = k->groesse < sizeof full ? k->groesse : sizeof full;
    std::memcpy(reinterpret_cast<char*>(&full) + sizeof(uint32_t),
                reinterpret_cast<const char*>(k) + sizeof(uint32_t), n - sizeof(uint32_t));
    if (full.port == 0) return false;   // „vom System gewählt" gibt es nur im Kern (Tests)
    k1520::serial::SerialKonfig c;
    c.betriebsart = static_cast<k1520::serial::Betriebsart>(full.betriebsart);
    c.rolle = static_cast<k1520::serial::Rolle>(full.rolle);
    c.host = readStr(full.host);
    c.port = full.port;
    c.loop = full.loop;
    c.rtscts_bruecke = full.rtscts_bruecke;
    c.xonxoff = full.xonxoff;
    c.taktquelle = full.taktquelle;
    c.datei = readStr(full.datei);
    return hub->konfigurieren(i, c);   // prüft Aufzählungen, Taktquelle, Sperren
}

bool k1520_serial_start(K1520Handle h, int i) {
    SerialHub* hub = hubFor(h, i);
    return hub && hub->start(i);
}

bool k1520_serial_start_auto(K1520Handle h, int i) {
    SerialHub* hub = hubFor(h, i);
    return hub && hub->startAuto(i);
}

void k1520_serial_stop(K1520Handle h, int i) {
    if (SerialHub* hub = hubFor(h, i)) hub->stop(i);
}

bool k1520_serial_status(K1520Handle h, int i, K1520SerStatus* out) {
    SerialHub* hub = hubFor(h, i);
    if (!hub) return false;
    const auto s = hub->status(i);
    K1520SerStatus r;
    std::memset(&r, 0, sizeof r);
    r.zustand = static_cast<int>(s.zustand);
    r.port_aktiv = s.port_aktiv;
    copyStr(r.gegenstelle, s.gegenstelle);
    copyStr(r.meldung, s.meldung);
    r.baud_nenn = s.baud_nenn;
    r.daten = s.daten;
    r.paritaet = s.paritaet;
    r.stopp_halbe = s.stopp_halbe;
    r.format_gueltig = s.format_gueltig;
    r.baud_gegenseite = s.baud_gegenseite;
    r.baud_abweichend = s.baud_abweichend;
    r.rts = s.rts; r.cts = s.cts; r.dtr = s.dtr; r.dsr = s.dsr; r.dcd = s.dcd;
    r.bytes_gesendet = s.bytes_gesendet;
    r.bytes_empfangen = s.bytes_empfangen;
    r.puffer_senden = s.puffer_senden;
    r.puffer_empfangen = s.puffer_empfangen;
    r.port_vorschlag = s.port_vorschlag;
    r.rolle = static_cast<int>(s.rolle);
    r.betriebsart = static_cast<int>(s.betriebsart);
    r.versuche = s.versuche;
    r.daten_gegenseite = s.daten_gegenseite;
    r.paritaet_gegenseite = s.paritaet_gegenseite;
    r.stopp_halbe_gegenseite = s.stopp_halbe_gegenseite;
    r.format_gegenseite_bekannt = s.format_gegenseite_bekannt;
    r.format_abweichend = s.format_abweichend;
    r.leitungen_gegenseite = s.leitungen_gegenseite;
    r.leitungen_gegenseite_bekannt = s.leitungen_gegenseite_bekannt;
    return deliver(out, r);
}

int k1520_serial_classify_host(const char* host) {
    using k1520::serial::net::AdressArt;
    switch (k1520::serial::net::adresseKlassifizieren(host ? host : "")) {
        case AdressArt::IPv4:     return K1520_HOST_IPV4;
        case AdressArt::IPv6:     return K1520_HOST_IPV6;
        case AdressArt::Hostname: return K1520_HOST_NAME;
        default:                  return K1520_HOST_UNGUELTIG;
    }
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
