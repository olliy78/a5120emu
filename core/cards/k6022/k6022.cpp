/**
 * @file k6022.cpp
 * @brief ADA K6022 — Lochbandleser/-stanzer an zwei PIOs (SIF1000), siehe k6022.h.
 */

#include "core/cards/k6022/k6022.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
uint64_t takteJeZeichen(uint32_t cpu_hz, uint32_t zeichen_s) {
    return zeichen_s ? (cpu_hz + zeichen_s - 1) / zeichen_s : cpu_hz;
}

std::filesystem::path dateiPfad(const std::string& pfad) {
    return std::filesystem::u8path(pfad);   // UTF-8 (Windows)
}

/// Ganze Datei lesen; false + @p fehler, wenn sie fehlt oder nicht lesbar ist.
bool dateiLesen(const std::string& pfad, std::vector<uint8_t>& d, std::string& fehler) {
    std::ifstream f(dateiPfad(pfad), std::ios::binary);
    if (!f) {
        fehler = "Band nicht lesbar: " + pfad;
        return false;
    }
    d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (f.bad()) {
        fehler = "Lesefehler: " + pfad;
        return false;
    }
    return true;
}

/// Datei ersetzen; false + @p fehler bei Schreibfehler.
bool dateiSchreiben(const std::string& pfad, const std::vector<uint8_t>& d, std::string& fehler) {
    std::ofstream f(dateiPfad(pfad), std::ios::binary | std::ios::trunc);
    if (f) f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    if (f) f.close();
    if (!f) {
        fehler = "Stanzband nicht schreibbar: " + pfad;
        return false;
    }
    return true;
}
}  // namespace

K6022::K6022(const Config& cfg, uint32_t cpu_hz)
    : cfg_(cfg)
    , leser_takte_(takteJeZeichen(cpu_hz, cfg.leser_zeichen_s))
    , stanzer_takte_(takteJeZeichen(cpu_hz, cfg.stanzer_zeichen_s))
    , pause_takte_(static_cast<uint64_t>(cpu_hz * kStanzpauseS)) {}

K6022::~K6022() {
    // Wie das Aushängen einer Diskette: was gestanzt wurde, gehört in die Datei.
    stanzerZurueckschreiben();
}

void K6022::attachToBus(K1520Bus& bus) {
    bus.registerIO(&tor_st_, cfg_.basis, 4);
    bus.registerIO(&tor_le_, static_cast<uint8_t>(cfg_.basis + 4), 4);
}

void K6022::reset() {
    pio_st_.reset();
    pio_le_.reset();
    std::lock_guard<std::mutex> l(m_);
    leser_ruf_ = false;
    stanz_ruf_ = false;
    leser_sta_ = 0xFF;   // nach dem Reset neu an Tor B legen
    stanz_sta_ = 0xFF;
    arbeit_.store(true, std::memory_order_release);
}

// ─── Busseite ────────────────────────────────────────────────────────────────

uint8_t K6022::Tor::ioRead(uint8_t port) {
    const uint8_t rel = relativ(port & 3);
    Z80PIO& pio = leser ? karte.pio_le_ : karte.pio_st_;
    if (rel == 2) {            // STA vor dem Lesen aktuell machen
        std::lock_guard<std::mutex> l(karte.m_);
        karte.leserStatusSetzen();
    }
    const uint8_t v = pio.ioRead(rel);
    // Betriebsart 1: Lesen des Datenregisters setzt ARDY = RUF an den Leser.
    if (leser && rel == 0 && pio.debugState().port[0].mode == 1) karte.leserRuf();
    return v;
}

void K6022::Tor::ioWrite(uint8_t port, uint8_t d) {
    const uint8_t rel = relativ(port & 3);
    Z80PIO& pio = leser ? karte.pio_le_ : karte.pio_st_;
    pio.ioWrite(rel, d);
    // Betriebsart 0: Schreiben des Datenregisters setzt ARDY = RUF an den Stanzer.
    if (!leser && rel == 0 && pio.debugState().port[0].mode == 0) karte.stanzerRuf(d);
    if (rel == 2) LOG_DEBUG("K6022", "%s KOM := %02X", leser ? "Leser" : "Stanzer", d & 0x0F);
}

void K6022::leserRuf() {
    std::lock_guard<std::mutex> l(m_);
    if (leser_ruf_) return;                       // RUF steht schon
    leser_ruf_     = true;
    arbeit_.store(true, std::memory_order_release);
    leser_faellig_ = zeit_ + leser_takte_;
}

void K6022::stanzerRuf(uint8_t byte) {
    std::lock_guard<std::mutex> l(m_);
    stanz_byte_ = byte;
    if (!stanzer_ein_) return;                    // kein Gerät: RUF bleibt unbeantwortet
    stanz_ruf_     = true;
    arbeit_.store(true, std::memory_order_release);
    stanz_faellig_ = zeit_ + stanzer_takte_;
}

void K6022::leserStatusSetzen() {
    // Stanzer: STA = 0 (bereit, kein Fehler) — `PTAPE.6022` prüft nach END D5|D6 (F181H).
    if (stanz_sta_ != 0x00) {
        stanz_sta_ = 0x00;
        pio_st_.portBWrite(0x00);
    }
    const uint8_t sta = (!band_drin_ || bandende_) ? STA_BANDENDE : 0x00;
    if (sta == leser_sta_) return;
    leser_sta_ = sta;
    pio_le_.portBWrite(sta);                      // D4–D7 Eingänge; D0–D3 bleiben Ausgänge
}

bool K6022::clockTick(int takte) {
    zeit_ += static_cast<uint64_t>(takte);
    // Stanzpause abgelaufen?  Nur feststellen — geschrieben wird in autoFlush().
    if (flush_ab_ != 0 && zeit_ >= flush_ab_) {
        flush_ab_ = 0;
        flush_faellig_.store(true, std::memory_order_release);
    }
    // Schneller Weg je Instruktion: nichts unterwegs, kein neuer Zustand von außen.
    if (!arbeit_.load(std::memory_order_acquire)) return false;
    bool geaendert = false;
    std::lock_guard<std::mutex> l(m_);
    leserStatusSetzen();
    if (leser_ruf_ && zeit_ >= leser_faellig_) {
        leser_ruf_ = false;
        if (band_drin_ && pos_ < bandGesamt()) {
            const uint64_t i = pos_++;
            const uint8_t b = (i >= cfg_.vorlauf && i - cfg_.vorlauf < band_.size())
                                  ? band_[i - cfg_.vorlauf] : 0x00;
            pio_le_.portAWrite(b);                // END: Byte übernommen, Interrupt
            geaendert = true;
        } else if (band_drin_) {
            bandende_ = true;                     // Band ist durch: kein END mehr, STA D6
            leserStatusSetzen();
            LOG_DEBUG("K6022", "Leser: Bandende");
        }
    }
    if (stanz_ruf_ && zeit_ >= stanz_faellig_) {
        stanz_ruf_ = false;
        stanz_.push_back(stanz_byte_);
        if (!stanz_pfad_.empty()) {
            // Jede Sprosse schiebt die Frist vor sich her: geschrieben wird in der Pause.
            stanz_dirty_ = true;
            flush_ab_    = zeit_ + pause_takte_;
            if (flush_ab_ == 0) flush_ab_ = 1;
        }
        pio_st_.setASTB(true);                    // END-Impuls: fallende Flanke → Interrupt
        pio_st_.setASTB(false);
        geaendert = true;
    }
    arbeit_.store(leser_ruf_ || stanz_ruf_, std::memory_order_release);
    return geaendert;
}

// ─── Leser ───────────────────────────────────────────────────────────────────

void K6022::bandEinlegen(std::vector<uint8_t> inhalt) {
    std::lock_guard<std::mutex> l(m_);
    band_      = std::move(inhalt);
    band_drin_ = true;
    pos_       = 0;
    bandende_  = false;
    leser_pfad_.clear();
    leser_format_ = Format::Roh;
    arbeit_.store(true, std::memory_order_release);   // STA im Lauffaden neu anlegen
}

bool K6022::bandEinlegenDatei(const std::string& pfad, Format fmt, std::string& fehler) {
    std::vector<uint8_t> d;
    if (!dateiLesen(pfad, d, fehler)) return false;
    std::string grund;
    std::optional<std::vector<uint8_t>> band = lochstreifen::lesen(d, fmt, grund);
    if (!band) {
        fehler = pfad + " (" + lochstreifen::formatName(fmt) + "): " + grund;
        return false;
    }
    bandEinlegen(std::move(*band));
    std::lock_guard<std::mutex> l(m_);
    leser_pfad_   = pfad;
    leser_format_ = fmt;
    return true;
}

std::string K6022::leserDatei() const {
    std::lock_guard<std::mutex> l(m_);
    return leser_pfad_;
}

K6022::Format K6022::leserFormat() const {
    std::lock_guard<std::mutex> l(m_);
    return leser_format_;
}

void K6022::bandEntnehmen() {
    std::lock_guard<std::mutex> l(m_);
    band_.clear();
    band_drin_ = false;
    pos_       = 0;
    bandende_  = false;
    leser_pfad_.clear();
    leser_format_ = Format::Roh;
    arbeit_.store(true, std::memory_order_release);   // STA im Lauffaden neu anlegen
}

K6022::LeserStand K6022::leserStand() const {
    std::lock_guard<std::mutex> l(m_);
    LeserStand s;
    s.eingelegt = band_drin_;
    s.laenge    = band_.size();
    s.gelesen   = pos_ <= cfg_.vorlauf ? 0 : std::min<uint64_t>(pos_ - cfg_.vorlauf, band_.size());
    s.bandende  = bandende_;
    return s;
}

// ─── Stanzer ─────────────────────────────────────────────────────────────────

std::vector<uint8_t> K6022::stanzband() const {
    std::lock_guard<std::mutex> l(m_);
    return stanz_;
}

uint64_t K6022::stanzbandLaenge() const {
    std::lock_guard<std::mutex> l(m_);
    return stanz_.size();
}

bool K6022::stanzbandSpeichern(const std::string& pfad, std::string& fehler, Format fmt) const {
    return dateiSchreiben(pfad, lochstreifen::schreiben(stanzband(), fmt), fehler);
}

// ─── Stanzer: Dateibindung (Entwurf 23 §5) ───────────────────────────────────

bool K6022::zurueckschreibenGesperrt(bool loesen) {
    // Schnappschuss und Freigabe in EINEM Griff unter m_; geschrieben wird außerhalb
    // von m_ (der Lauffaden stanzt derweil weiter), aber unter datei_m_ — so kann kein
    // zweiter Schreiber mit älterem Stand den neueren überholen.
    std::vector<uint8_t> band;
    std::string pfad;
    Format fmt;
    {
        std::lock_guard<std::mutex> l(m_);
        if (stanz_pfad_.empty()) return true;
        pfad = stanz_pfad_;
        fmt  = stanz_format_;
        const bool noetig = stanz_dirty_;
        if (noetig) band = stanz_;
        stanz_dirty_ = false;
        if (loesen) stanz_pfad_.clear();
        if (!noetig) return true;
    }
    std::string fehler;
    if (dateiSchreiben(pfad, lochstreifen::schreiben(band, fmt), fehler)) {
        std::lock_guard<std::mutex> l(m_);
        stanz_fehler_.clear();
        return true;
    }
    LOG_WARN("K6022", "%s", fehler.c_str());
    std::lock_guard<std::mutex> l(m_);
    stanz_fehler_ = fehler;
    if (!loesen && stanz_pfad_ == pfad) stanz_dirty_ = true;   // beim nächsten Anlass erneut
    return false;
}

bool K6022::stanzerZurueckschreiben() {
    std::lock_guard<std::mutex> dl(datei_m_);
    return zurueckschreibenGesperrt(false);
}

bool K6022::autoFlush() {
    if (!flush_faellig_.exchange(false, std::memory_order_acq_rel)) return false;
    return stanzerZurueckschreiben();
}

bool K6022::stanzerBinden(const std::string& pfad, Format fmt, std::string& fehler) {
    std::lock_guard<std::mutex> dl(datei_m_);
    // Wechsel der Datei: die alte bekommt zuerst, was für sie gestanzt wurde.
    if (!zurueckschreibenGesperrt(false)) {
        fehler = stanzerFehler();
        return false;
    }
    std::vector<uint8_t> band;
    std::error_code ec;
    if (std::filesystem::exists(dateiPfad(pfad), ec)) {
        std::vector<uint8_t> d;
        if (!dateiLesen(pfad, d, fehler)) return false;
        std::string grund;
        std::optional<std::vector<uint8_t>> b = lochstreifen::lesen(d, fmt, grund);
        if (!b) {
            fehler = pfad + " (" + lochstreifen::formatName(fmt) + "): " + grund;
            return false;
        }
        band = std::move(*b);
        // Schreibbar?  Anhängend öffnen ändert nichts am Inhalt.
        std::ofstream probe(dateiPfad(pfad), std::ios::binary | std::ios::app);
        if (!probe) {
            fehler = "Stanzband nicht schreibbar: " + pfad;
            return false;
        }
    } else if (!dateiSchreiben(pfad, lochstreifen::schreiben({}, fmt), fehler)) {
        return false;
    }
    std::lock_guard<std::mutex> l(m_);
    stanz_        = std::move(band);
    stanz_pfad_   = pfad;
    stanz_format_ = fmt;
    stanz_dirty_  = false;
    stanz_fehler_.clear();
    return true;
}

bool K6022::stanzerLoesen() {
    std::lock_guard<std::mutex> dl(datei_m_);
    return zurueckschreibenGesperrt(true);
}

bool K6022::neuesBand() {
    std::lock_guard<std::mutex> dl(datei_m_);
    {
        std::lock_guard<std::mutex> l(m_);
        stanz_.clear();
        if (stanz_pfad_.empty()) return true;
        stanz_dirty_ = true;          // leeres Band gegen volle Datei: sofort schreiben
    }
    return zurueckschreibenGesperrt(false);
}

std::string K6022::stanzerDatei() const {
    std::lock_guard<std::mutex> l(m_);
    return stanz_pfad_;
}

K6022::Format K6022::stanzerFormat() const {
    std::lock_guard<std::mutex> l(m_);
    return stanz_format_;
}

std::string K6022::stanzerFehler() const {
    std::lock_guard<std::mutex> l(m_);
    return stanz_fehler_;
}

void K6022::setStanzerEin(bool ein) {
    std::lock_guard<std::mutex> l(m_);
    stanzer_ein_ = ein;
    if (!ein) stanz_ruf_ = false;
}

bool K6022::stanzerEin() const {
    std::lock_guard<std::mutex> l(m_);
    return stanzer_ein_;
}
