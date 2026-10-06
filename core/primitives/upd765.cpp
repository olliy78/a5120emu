/**
 * @file upd765.cpp
 * @brief Upd765 – Implementierung (siehe upd765.h).
 */

#include "upd765.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

/// Sektorgröße → N des ID-Felds (128 << N); -1 = keine Zweierpotenz 128…16384.
int sizeToN(uint16_t size)
{
    for (int n = 0; n < 8; ++n)
        if ((128u << n) == size) return n;
    return -1;
}

/// Befehlslänge (inkl. Opcode) und erlaubte Flag-Bits (MT 80, MFM 40, SK 20); 0 = ungültig.
struct CmdInfo { int len; uint8_t flags; };
CmdInfo cmdInfo(uint8_t op)
{
    switch (op & 0x1F) {
        case 0x03: return {3, 0x00};   // SPECIFY
        case 0x04: return {2, 0x00};   // SENSE DRIVE STATUS
        case 0x05: return {9, 0xE0};   // WRITE DATA
        case 0x06: return {9, 0xE0};   // READ DATA
        case 0x07: return {2, 0x00};   // RECALIBRATE
        case 0x08: return {1, 0x00};   // SENSE INTERRUPT STATUS
        case 0x0A: return {2, 0x40};   // READ ID
        case 0x0D: return {6, 0x40};   // FORMAT TRACK
        case 0x0F: return {3, 0x00};   // SEEK
        case 0x11: return {9, 0xE0};   // SCAN EQUAL
        default:   return {0, 0};
    }
}

/// Ein Durchlauf für Speichern und Laden, damit beide Richtungen dieselbe Feldliste haben.
struct Ar {
    bool save;
    std::vector<uint8_t>* out = nullptr;
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool ok = true;

    template <class T> void num(T& v) {
        static_assert(std::is_integral<T>::value, "integral");
        if (save) {
            for (size_t i = 0; i < sizeof(T); ++i)
                out->push_back(static_cast<uint8_t>((static_cast<uint64_t>(v) >> (8 * i)) & 0xFF));
        } else {
            if (!ok || static_cast<size_t>(end - p) < sizeof(T)) { ok = false; return; }
            uint64_t x = 0;
            for (size_t i = 0; i < sizeof(T); ++i) x |= static_cast<uint64_t>(*p++) << (8 * i);
            v = static_cast<T>(x);
        }
    }
    template <class E> void en(E& v) { uint8_t t = static_cast<uint8_t>(v); num(t); v = static_cast<E>(t); }
    void flag(bool& v) { uint8_t t = v ? 1 : 0; num(t); v = t != 0; }
    template <class T, size_t N> void arr(T (&a)[N]) { for (auto& e : a) num(e); }
    template <size_t N> void flags(bool (&a)[N]) { for (auto& e : a) flag(e); }
    void vec(std::vector<uint8_t>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        num(n);
        if (save) { out->insert(out->end(), v.begin(), v.end()); return; }
        if (!ok || static_cast<size_t>(end - p) < n) { ok = false; return; }
        v.assign(p, p + n);
        p += n;
    }
};

constexpr uint8_t kStateVersion = 1;

}  // namespace

// ─── Zeit / Hilfen ────────────────────────────────────────────────────────────

int64_t Upd765::bytePeriod() const
{
    int64_t c = static_cast<int64_t>(cfg_.cpu_hz) * 8 / (static_cast<int64_t>(cfg_.mfm_kbit) * 1000);
    if (!mfm_) c *= 2;       // FM: halbe Datenrate
    return std::max<int64_t>(c, 1);
}

bool Upd765::unitReady(int u) const
{
    FloppyDriveV2* d = laufwerk(u);
    if (!d) return false;
    return ready ? ready(u) : d->isMounted();
}

void Upd765::setDrq(bool v)
{
    if (drq_ == v) return;
    drq_ = v;
    if (onDrq) onDrq(v);
}

void Upd765::setIrq(bool v)
{
    if (irq_ == v) return;
    irq_ = v;
    if (onIrq) onIrq(v);
}

void Upd765::resetState()
{
    phase_ = Phase::Idle; kind_ = Kind::None; stage_ = Stage::Latency;
    nd_ = false; tc_latch_ = false; byte_ready_ = false; delay_ = -1;
    cmd_pos_ = cmd_len_ = 0;
    st0_ = st1_ = st2_ = 0;
    buf_.clear(); fmt_ids_.clear(); result_.clear(); res_pos_ = 0; res_irq_ = false;
    for (int u = 0; u < kUnits; ++u) {
        pcn_[u] = 0; seek_left_[u] = -1; int_pending_[u] = false; rid_pos_[u] = 0;
    }
    setDrq(false);
    setIrq(false);
}

void Upd765::setReset(bool level)
{
    if (level && !reset_) resetState();
    reset_ = level;
}

// ─── Busseite ─────────────────────────────────────────────────────────────────

uint8_t Upd765::readMsr() const
{
    if (reset_) return 0x00;
    uint8_t m = 0;
    for (int u = 0; u < kUnits; ++u)
        if (seek_left_[u] >= 0) m |= static_cast<uint8_t>(1u << u);   // DnB
    switch (phase_) {
        case Phase::Idle:    m |= 0x80; break;                       // RQM
        case Phase::Command: m |= 0x90; break;                       // RQM + CB
        case Phase::Result:  m |= 0xD0; break;                       // RQM + DIO + CB
        case Phase::Exec:
            m |= 0x10;                                               // CB
            if (nd_) {
                m |= 0x20;                                           // EXM
                if (byte_ready_) { m |= 0x80; if (kind_ == Kind::Read) m |= 0x40; }
            }
            break;
    }
    return m;
}

uint8_t Upd765::readData()
{
    if (reset_) return 0xFF;
    if (phase_ == Phase::Exec && nd_) return dmaRead();
    if (phase_ != Phase::Result) return 0xFF;
    const uint8_t b = result_[res_pos_];
    if (res_pos_ == 0 && res_irq_) setIrq(false);   // INT fällt mit dem ersten Ergebnisbyte
    if (++res_pos_ >= result_.size()) { phase_ = Phase::Idle; result_.clear(); res_pos_ = 0; }
    return b;
}

void Upd765::writeData(uint8_t b)
{
    if (reset_) return;
    switch (phase_) {
        case Phase::Idle: {
            cmd_[0] = b;
            const CmdInfo ci = cmdInfo(b);
            if (ci.len == 0 || (b & 0xE0 & ~ci.flags)) { invalid(); return; }
            cmd_len_ = ci.len;
            cmd_pos_ = 1;
            if (cmd_len_ == 1) command();
            else phase_ = Phase::Command;
            break;
        }
        case Phase::Command:
            cmd_[cmd_pos_++] = b;
            if (cmd_pos_ >= cmd_len_) command();
            break;
        case Phase::Exec:
            if (nd_) dmaWrite(b);
            break;
        case Phase::Result:
            break;   // Schreiben in die Ergebnisphase: ohne Wirkung
    }
}

// ─── DMA-Seite ────────────────────────────────────────────────────────────────

uint8_t Upd765::dmaRead()
{
    if (phase_ != Phase::Exec || kind_ != Kind::Read || !byte_ready_) return 0xFF;
    const uint8_t b = buf_[pos_];
    byte_ready_ = false;
    setDrq(false);
    if (nd_) setIrq(false);
    byteTaken(b);
    return b;
}

void Upd765::dmaWrite(uint8_t b)
{
    if (phase_ != Phase::Exec || !byte_ready_) return;
    if (kind_ != Kind::Write && kind_ != Kind::Scan && kind_ != Kind::Format) return;
    byte_ready_ = false;
    setDrq(false);
    if (nd_) setIrq(false);
    byteTaken(b);
}

void Upd765::setTC(bool level)
{
    const bool rise = level && !tc_;
    tc_ = level;
    if (!rise || phase_ != Phase::Exec) return;
    tc_latch_ = true;
    // Mitten im Sektor: sofort Schluss.  Nach dem letzten Byte (Stage Tail) entscheidet endSector().
    if (stage_ == Stage::Transfer && pos_ < size_ &&
        (kind_ == Kind::Read || kind_ == Kind::Write || kind_ == Kind::Scan))
        abortByTc();
}

// ─── Befehle ──────────────────────────────────────────────────────────────────

void Upd765::invalid()
{
    toResult({0x80}, false);
}

void Upd765::command()
{
    phase_ = Phase::Idle;
    st0_ = st1_ = st2_ = 0;
    tc_latch_ = false;
    switch (cmd_[0] & 0x1F) {
        case 0x03: nd_ = (cmd_[2] & 1) != 0; break;     // SPECIFY: kein Ergebnis, kein INT
        case 0x04: senseDrive(); break;
        case 0x05: startRw(Kind::Write); break;
        case 0x06: startRw(Kind::Read); break;
        case 0x07: startSeek(true); break;
        case 0x08: senseInterrupt(); break;
        case 0x0A: startReadId(); break;
        case 0x0D: startFormat(); break;
        case 0x0F: startSeek(false); break;
        case 0x11: startRw(Kind::Scan); break;
        default:   invalid(); break;
    }
}

void Upd765::toResult(std::vector<uint8_t> r, bool raiseIrq)
{
    phase_ = Phase::Result;
    kind_ = Kind::None;
    result_ = std::move(r);
    res_pos_ = 0;
    res_irq_ = raiseIrq;
    byte_ready_ = false;
    delay_ = -1;
    setDrq(false);
    if (raiseIrq) setIrq(true);
}

void Upd765::finish(uint8_t ic)
{
    const uint8_t st0 = static_cast<uint8_t>((st0_ & 0x38) | (ic << 6) | (head_ << 2) | unit_);
    toResult({st0, st1_, st2_, c_, h_, r_, n_}, true);
}

void Upd765::senseDrive()
{
    const int u = cmd_[1] & 3;
    const uint8_t hd = (cmd_[1] >> 2) & 1;
    FloppyDriveV2* d = laufwerk(u);
    uint8_t st3 = static_cast<uint8_t>(u | (hd << 2));
    if (d) {
        if (d->headReachable(1)) st3 |= 0x08;               // TS
        if (d->currentCylinder() == 0) st3 |= 0x10;         // T0
        if (unitReady(u)) st3 |= 0x20;                      // RDY
        if (d->isWriteProtect()) st3 |= 0x40;               // WP
    }
    toResult({st3}, false);
}

void Upd765::senseInterrupt()
{
    for (int u = 0; u < kUnits; ++u) {
        if (!int_pending_[u]) continue;
        int_pending_[u] = false;
        bool more = false;
        for (int v = 0; v < kUnits; ++v) more = more || int_pending_[v];
        setIrq(more);
        toResult({int_st0_[u], pcn_[u]}, false);
        return;
    }
    invalid();   // nichts ansteht: ST0 = 80H
}

void Upd765::startSeek(bool recal)
{
    const int u = cmd_[1] & 3;
    FloppyDriveV2* d = laufwerk(u);
    const int cur = d ? d->currentCylinder() : pcn_[u];
    const int target = recal ? 0 : cmd_[2];
    const int steps = std::max(1, std::abs(target - cur));
    const int64_t per = cfg_.step_cycles ? cfg_.step_cycles : std::max<uint32_t>(cfg_.cpu_hz / 1000, 1);
    seek_target_[u] = static_cast<uint8_t>(target);
    seek_recal_[u] = recal;
    seek_left_[u] = steps * per;
    int_pending_[u] = false;
}

void Upd765::completeSeek(int u)
{
    seek_left_[u] = -1;
    FloppyDriveV2* d = laufwerk(u);
    const bool ok = d && d->seek(seek_target_[u]);
    pcn_[u] = seek_target_[u];
    // RECALIBRATE ohne Laufwerk findet nie Spur 0: EC (Equipment Check) + IC = 01
    int_st0_[u] = (seek_recal_[u] && !ok) ? static_cast<uint8_t>(0x70 | u)
                                          : static_cast<uint8_t>(0x20 | u);
    int_pending_[u] = true;
    setIrq(true);
}

bool Upd765::prepare()
{
    if (!drv() || !unitReady(unit_)) {
        st0_ |= 0x08;                 // NR
        finish(1);
        return false;
    }
    if ((kind_ == Kind::Write || kind_ == Kind::Format) && drv()->isWriteProtect()) {
        st1_ |= kNW;
        finish(1);
        return false;
    }
    return true;
}

void Upd765::startRw(Kind k)
{
    kind_ = k;
    phase_ = Phase::Exec;
    mt_  = (cmd_[0] & 0x80) != 0;
    mfm_ = (cmd_[0] & 0x40) != 0;
    unit_ = cmd_[1] & 3;
    head_ = (cmd_[1] >> 2) & 1;
    c_ = cmd_[2]; h_ = cmd_[3]; r_ = cmd_[4]; n_ = cmd_[5]; eot_ = cmd_[6];
    stp_ = (k == Kind::Scan && cmd_[8] == 2) ? 2 : 1;
    scan_equal_ = true;
    if (!prepare()) return;
    startSector();
}

void Upd765::startSector()
{
    const TrackImage& tr = drv()->track(head_);
    const Encoding want = mfm_ ? Encoding::MFM : Encoding::FM;
    std::vector<LogicalSector> secs;
    // Nur eine Spur im passenden Verfahren hat für diesen Befehl lesbare Marken.
    if (!tr.empty() && tr.encoding == want) secs = TrackCodec::parseTrack(tr);
    if (secs.empty()) {
        st1_ |= kMA;
        finish(1);
        return;
    }
    int found = -1;
    bool wrong_c = false;
    for (size_t i = 0; i < secs.size(); ++i) {
        const LogicalSector& s = secs[i];
        if (s.id == r_ && s.head == h_) {
            if (s.cyl != c_) { wrong_c = true; if (s.cyl == 0xFF) st2_ |= kBC; continue; }
            if (sizeToN(s.size) == n_) { found = static_cast<int>(i); break; }
        }
    }
    if (found < 0) {
        st1_ |= kND;
        if (wrong_c) st2_ |= kWC;
        finish(1);
        return;
    }
    const LogicalSector& s = secs[found];
    if (!s.id_crc_ok) { st1_ |= kDE; finish(1); return; }
    if (s.data_pos == SIZE_MAX) { st1_ |= kMA; st2_ |= kMD; finish(1); return; }
    cur_index_ = static_cast<size_t>(found);
    cur_deleted_ = s.deleted;
    cur_data_crc_bad_ = !s.data_crc_ok;
    size_ = s.size;
    if (kind_ == Kind::Write) buf_.assign(size_, 0);
    else { buf_ = s.data; buf_.resize(size_, 0); }
    pos_ = 0;
    stage_ = Stage::Latency;
    delay_ = 20 * bytePeriod();       // Rest der ID + Lücke 2 bis zum Datenfeld
}

void Upd765::startReadId()
{
    kind_ = Kind::ReadId;
    phase_ = Phase::Exec;
    mfm_ = (cmd_[0] & 0x40) != 0;
    unit_ = cmd_[1] & 3;
    head_ = (cmd_[1] >> 2) & 1;
    c_ = h_ = r_ = n_ = 0;
    if (!prepare()) return;
    stage_ = Stage::Latency;
    delay_ = 10 * bytePeriod();
}

void Upd765::startFormat()
{
    kind_ = Kind::Format;
    phase_ = Phase::Exec;
    mfm_ = (cmd_[0] & 0x40) != 0;
    unit_ = cmd_[1] & 3;
    head_ = (cmd_[1] >> 2) & 1;
    n_ = cmd_[2]; fmt_sc_ = cmd_[3]; fmt_gpl_ = cmd_[4]; fmt_fill_ = cmd_[5];
    c_ = h_ = r_ = 0;
    if (!prepare()) return;
    if (n_ > 7 || fmt_sc_ == 0) { st1_ |= kND; finish(1); return; }
    fmt_ids_.clear();
    pos_ = 0;
    size_ = 4u * fmt_sc_;
    stage_ = Stage::Latency;
    delay_ = 10 * bytePeriod();
}

// ─── Ausführung ───────────────────────────────────────────────────────────────

void Upd765::presentByte()
{
    byte_ready_ = true;
    delay_ = -1;                       // jetzt wartet der Baustein auf die Maschine
    if (nd_) setIrq(true);
    else setDrq(true);
}

void Upd765::byteTaken(uint8_t b)
{
    const int64_t bp = bytePeriod();
    switch (kind_) {
        case Kind::Read:  break;
        case Kind::Write: buf_[pos_] = b; break;
        case Kind::Scan:
            if (b != 0xFF && b != buf_[pos_]) scan_equal_ = false;   // FFH = Platzhalter
            break;
        case Kind::Format:
            fmt_ids_.push_back(b);
            break;
        default: return;
    }
    ++pos_;
    if (pos_ < size_) {
        // Beim FORMAT liegt zwischen zwei ID-Quadrupeln der Rest des Sektors samt Lücke.
        delay_ = (kind_ == Kind::Format && pos_ % 4 == 0) ? 40 * bp : bp;
    } else {
        stage_ = Stage::Tail;          // CRC-Bytes
        delay_ = 2 * bp;
    }
}

void Upd765::onDelay()
{
    if (kind_ == Kind::ReadId) {
        const TrackImage& tr = drv()->track(head_);
        std::vector<LogicalSector> secs;
        if (!tr.empty() && tr.encoding == (mfm_ ? Encoding::MFM : Encoding::FM))
            secs = TrackCodec::parseTrack(tr);
        if (secs.empty()) { st1_ |= kMA; finish(1); return; }
        const LogicalSector& s = secs[rid_pos_[unit_]++ % secs.size()];
        c_ = s.cyl; h_ = s.head; r_ = s.id;
        n_ = static_cast<uint8_t>(std::max(sizeToN(s.size), 0));
        if (!s.id_crc_ok) { st1_ |= kDE; finish(1); return; }
        finish(0);
        return;
    }
    switch (stage_) {
        case Stage::Latency:
            stage_ = Stage::Transfer;
            presentByte();
            break;
        case Stage::Transfer:
            presentByte();
            break;
        case Stage::Tail:
            if (kind_ == Kind::Format) commitFormat();
            else endSector();
            break;
    }
}

void Upd765::tick(uint32_t cycles)
{
    if (reset_) return;
    for (int u = 0; u < kUnits; ++u) {
        if (seek_left_[u] < 0) continue;
        seek_left_[u] -= cycles;
        if (seek_left_[u] <= 0) completeSeek(u);
    }
    int64_t left = cycles;
    while (phase_ == Phase::Exec && delay_ >= 0) {
        if (delay_ > left) { delay_ -= left; break; }
        left -= delay_;
        delay_ = -1;
        onDelay();
    }
}

void Upd765::commitWrite()
{
    TrackImage& t = drv()->mutableTrack(head_);
    if (TrackCodec::writeSectorAt(t, cur_index_, buf_))
        drv()->markTrackDirty(head_);
    else
        st1_ |= kND;               // Sektor unter dem Kopf verschwunden: nichts geschrieben
}

void Upd765::commitFormat()
{
    const Encoding enc = mfm_ ? Encoding::MFM : Encoding::FM;
    std::vector<LogicalSector> secs;
    for (size_t i = 0; i + 3 < fmt_ids_.size(); i += 4) {
        LogicalSector s;
        s.cyl = fmt_ids_[i]; s.head = fmt_ids_[i + 1]; s.id = fmt_ids_[i + 2];
        s.size = static_cast<uint16_t>(128u << fmt_ids_[i + 3]);
        s.data.assign(s.size, fmt_fill_);
        secs.push_back(std::move(s));
    }
    if (secs.empty()) { st1_ |= kND; finish(1); return; }
    GapParams g = TrackCodec::normGaps(secs, enc);
    if (fmt_gpl_) g.gap3 = fmt_gpl_;
    const TrackImage t = TrackCodec::buildTrack(secs, enc, g);
    if (!drv()->writeTrackAt(drv()->currentCylinder(), head_, t)) {
        st1_ |= kNW;
        finish(1);
        return;
    }
    // Ergebnis: die zuletzt übergebene C/H/R/N
    c_ = secs.back().cyl; h_ = secs.back().head; r_ = secs.back().id; n_ = fmt_ids_.back();
    finish(0);
}

void Upd765::abortByTc()
{
    byte_ready_ = false;
    setDrq(false);
    if (nd_) setIrq(false);
    delay_ = -1;
    if (kind_ == Kind::Write) {            // Rest des Datenfelds wird mit 00 aufgefüllt
        std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(pos_), buf_.end(), 0);
        commitWrite();
    } else if (kind_ == Kind::Scan && !scan_equal_) {
        st2_ |= kSN;
    }
    finish(0);
}

void Upd765::endSector()
{
    if (kind_ == Kind::Write) commitWrite();
    else if (cur_data_crc_bad_) {
        st1_ |= kDE; st2_ |= kDD;
        finish(1);
        return;
    }
    const int step = (kind_ == Kind::Scan) ? stp_ : 1;
    // R fortschreiben; am Zylinderende (R = EOT) C+1/R = 1 bzw. mit MT Wechsel auf Kopf 1.
    auto advance = [&] {
        if (r_ + step > eot_) {
            if (mt_ && head_ == 0) { head_ = 1; h_ = 1; r_ = 1; }
            else { ++c_; r_ = 1; }
        } else {
            r_ = static_cast<uint8_t>(r_ + step);
        }
    };
    if (kind_ == Kind::Scan && scan_equal_) {      // Treffer: sofort fertig, IC = 00
        st2_ |= kSH;
        advance();
        finish(0);
        return;
    }
    if (kind_ == Kind::Scan) st2_ |= kSN;
    if (kind_ == Kind::Read && cur_deleted_) {     // SK = 0: gelöschter Sektor wird gelesen, dann Ende
        st2_ |= kCM;
        advance();
        finish(0);
        return;
    }
    const bool eoc = r_ + step > eot_;
    const bool weiter = !tc_latch_ && !(eoc && !(mt_ && head_ == 0));
    advance();
    if (weiter) { startSector(); return; }
    if (!tc_latch_) st1_ |= kEN;                   // EOT ohne TC: Abnormal Termination
    finish(tc_latch_ ? 0 : 1);
}

// ─── Save-State ───────────────────────────────────────────────────────────────

template <class A> void Upd765::visit(A& a)
{
    a.en(phase_); a.en(kind_); a.en(stage_);
    a.flag(reset_); a.flag(nd_); a.flag(drq_); a.flag(irq_); a.flag(tc_); a.flag(tc_latch_);
    a.flag(byte_ready_);
    a.num(delay_);
    a.arr(cmd_); a.num(cmd_len_); a.num(cmd_pos_);
    a.num(unit_); a.num(head_); a.flag(mfm_); a.flag(mt_);
    a.num(c_); a.num(h_); a.num(r_); a.num(n_); a.num(eot_); a.num(stp_);
    a.num(st0_); a.num(st1_); a.num(st2_);
    a.vec(buf_); a.num(pos_); a.num(size_); a.num(cur_index_);
    a.flag(cur_deleted_); a.flag(cur_data_crc_bad_); a.flag(scan_equal_);
    a.num(fmt_sc_); a.num(fmt_gpl_); a.num(fmt_fill_); a.vec(fmt_ids_);
    a.vec(result_); a.num(res_pos_); a.flag(res_irq_);
    a.arr(pcn_); a.arr(seek_left_); a.arr(seek_target_); a.flags(seek_recal_);
    a.flags(int_pending_); a.arr(int_st0_); a.arr(rid_pos_);
}

void Upd765::serialize(std::vector<uint8_t>& out) const
{
    out.push_back(kStateVersion);
    Ar a{true, &out};
    const_cast<Upd765*>(this)->visit(a);
}

bool Upd765::deserialize(const uint8_t*& p, const uint8_t* end)
{
    if (p >= end || *p++ != kStateVersion) return false;
    Ar a{false, nullptr, p, end};
    Upd765 tmp(cfg_);
    tmp.visit(a);
    if (!a.ok) return false;
    p = a.p;
    // Verdrahtung/Rückrufe behalten, Zustand übernehmen
    FloppyDriveV2* d[kUnits];
    for (int u = 0; u < kUnits; ++u) d[u] = drive_[u];
    const bool drq = tmp.drq_, irq = tmp.irq_;
    tmp.drq_ = drq_; tmp.irq_ = irq_;
    auto rdy = ready; auto od = onDrq; auto oi = onIrq; auto ua = unitAuswahl;
    *this = std::move(tmp);
    for (int u = 0; u < kUnits; ++u) drive_[u] = d[u];
    ready = rdy; onDrq = od; onIrq = oi; unitAuswahl = ua;
    setDrq(drq);
    setIrq(irq);
    return true;
}
