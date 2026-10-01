/**
 * @file z80_sio.cpp
 * @brief Z80 Serial Input/Output (SIO) Chip Emulation - Implementation
 *
 * Contains the complete implementation of the Z80 SIO (Serial Input/Output)
 * controller emulation for the Robotron A5120 office computer. Implements all
 * operating modes (asynchronous, synchronous, SDLC), interrupt handling, CRC
 * calculation, and modem control.
 *
 * The implementation is organized into the following sections:
 * - Channel state management (reset, status updates, FIFO operations)
 * - Control register programming (WR0-WR7 processing)
 * - Status register reading (RR0-RR2)
 * - I/O port access (data and control reads/writes)
 * - Interrupt logic (vector generation, daisy-chain priority)
 * - Asynchronous mode (UART with start/stop bits, parity)
 * - Synchronous modes (Monosync/Bisync with sync character detection)
 * - SDLC/HDLC mode (flag detection, bit stuffing, address matching)
 * - CRC calculation (CRC-16 and CRC-CCITT polynomials)
 * - Modem signal monitoring (CTS, DCD, sync status changes)
 *
 * Operating Modes:
 * - **Asynchronous Mode**: Standard UART operation with configurable parameters
 *   - 5, 6, 7, or 8 data bits per character
 *   - Even or odd parity (optional)
 *   - 1, 1.5, or 2 stop bits
 *   - Clock multipliers: x1, x16, x32, x64
 *   - Break detection and generation
 *   - State machine for start bit, data, parity, stop bit
 *
 * - **Monosync Mode**: 8-bit synchronous communication
 *   - Single 8-bit sync character (WR6)
 *   - Hunt mode for sync pattern detection
 *   - Automatic sync character insertion on underrun
 *
 * - **Bisync Mode**: 16-bit synchronous communication (BSC protocol)
 *   - Double 8-bit sync characters (WR6 + WR7)
 *   - Hunt mode for 16-bit sync pattern
 *   - Alternating sync character transmission
 *
 * - **SDLC/HDLC Mode**: Bit-oriented protocol
 *   - Flag sequences: 01111110 (opening/closing)
 *   - Abort sequences: 01111111 or more
 *   - Automatic bit stuffing (insert 0 after five 1s)
 *   - Address field matching (optional)
 *   - Automatic CRC-CCITT generation and checking
 *   - Frame detection and validation
 *
 * Interrupt Handling:
 * Internal priority within SIO:
 * 1. Channel A Receive (highest)
 * 2. Channel A Transmit
 * 3. Channel A External/Status
 * 4. Channel B Receive
 * 5. Channel B Transmit
 * 6. Channel B External/Status (lowest)
 *
 * Interrupt modes:
 * - Receive: Disabled, First Character, All Characters, Special Condition
 * - Transmit: Enabled/Disabled
 * - External/Status: CTS, DCD, Sync changes, TX underrun, Break/Abort
 *
 * Implementation Notes:
 * - Receive FIFO depth is 3 bytes per channel
 * - CRC polynomials: CRC-16 (0x8005) for synchronous, CRC-CCITT (0x1021) for SDLC
 * - Bit stuffing in SDLC: After five consecutive 1s, a 0 is inserted
 * - Flag detection: 01111110 (exactly six 1s between 0s)
 * - Abort detection: Seven or more consecutive 1s
 * - Register pointer auto-resets to 0 after each write/read
 * - Channel A has priority over Channel B in daisy-chain
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8440 SIO Technical Manual
 * @see Robotron A5120 Technical Documentation (U856D Manual, pages 754-792)
 */
#include "z80_sio.h"
#include <cstring>
#include <type_traits>

/**
 * @brief Receive FIFO depth per channel.
 *
 * Each channel has a 3-byte deep receive FIFO for storing incoming data.
 * When the FIFO is full, subsequent receives generate an overrun error.
 */
static constexpr size_t RX_FIFO_DEPTH = 3;

// =============================================================================
/// @name Channel State Management
// =============================================================================

/**
 * @brief Reset channel to initial state.
 *
 * Clears all registers, resets FIFO, and sets default status flags.
 * Called on channel reset command (WR0 command 3) or device initialization.
 */
void Z80SIO::Channel::reset() {
    for (auto& r : wr) r = 0;
    rr0     = 0x04;
    rr1     = 0x01;
    reg_ptr = 0;
    rx_fifo.clear();
    tx_buf.reset();
    irq_rx = irq_tx = irq_ext = false;
    // Die aus WR1 abgeleiteten Freigaben gehören zu WR1 und gehen mit ihm (sonst
    // unterbräche ein Kanal nach Channel Reset weiter im alten Empfangsmodus).
    ext_int_enable = tx_int_enable = status_affects_vector = false;
    rx_int_mode = SIORxIntMode::DISABLED;
    rx_int_first_only = false;
    last_rx = 0x00;
    // Die Eingänge (cts_, dcd_, break_rx_) sind Pins und bleiben; nur das Latch
    // geht.  rr0 = 04H wie bisher, solange kein Eingang aktiv ist (Vorgabe).
    ext_latch_ = false;
    rr0 |= extStatusEingaenge();
}

void Z80SIO::Channel::updateRR0() {
    rr0 = 0x00;
    if (!rx_fifo.empty()) rr0 |= 0x01;  // Rx Character Available
    if (!tx_buf.has_value()) rr0 |= 0x04; // Tx Buffer Empty
    // D3 DCD, D5 CTS, D7 Break/Abort: festgehalten seit der letzten Ext/Status-
    // Änderung, sonst der Eingang.  Mit der Vorgabe (alle inaktiv) bleiben die
    // Bits 0 — genau das RR0 von vor AP-S3.
    rr0 |= ext_latch_ ? ext_rr0_ : extStatusEingaenge();
}

void Z80SIO::Channel::extStatusGeaendert() {
    // Datenblatt (U856, Befehl 2 und RR0 D3): eine Änderung hält die Statusbits
    // fest und fordert Ext/Status an; erst „Reset Ext/Status“ gibt sie wieder frei.
    // Ohne Freigabe (WR1 D0) kein Interrupt, RR0 folgt dem Eingang.
    if (ext_int_enable && !ext_latch_) {
        ext_latch_ = true;
        ext_rr0_   = extStatusEingaenge();
        irq_ext    = true;
    }
    updateRR0();
}

void Z80SIO::Channel::setzeCTS(bool aktiv) {
    if (cts_ == aktiv) return;
    cts_ = aktiv;
    extStatusGeaendert();
}

void Z80SIO::Channel::setzeDCD(bool aktiv) {
    if (dcd_ == aktiv) return;
    dcd_ = aktiv;
    extStatusGeaendert();
}

void Z80SIO::Channel::setzeBreakEmpfang(bool aktiv) {
    if (break_rx_ == aktiv) return;
    break_rx_ = aktiv;
    extStatusGeaendert();
}

Z80SIO::Channel::Format Z80SIO::Channel::format() const {
    // Kodierung der Bitzahl in WR3 D7–6 / WR5 D6–5: 00=5, 01=7, 10=6, 11=8.
    static constexpr uint8_t BITS[4] = {5, 7, 6, 8};
    static constexpr uint8_t TEILER[4] = {1, 16, 32, 64};
    Format f;
    f.teiler      = TEILER[(wr[4] >> 6) & 0x03];
    const uint8_t sb = (wr[4] >> 2) & 0x03;       // 00 synchron, 01 1, 10 1½, 11 2
    f.stopp_halbe = sb == 0 ? 0 : static_cast<uint8_t>(sb + 1);
    f.paritaet    = (wr[4] & 0x01) ? ((wr[4] & 0x02) ? 2 : 1) : 0;
    f.tx_bits     = BITS[(wr[5] >> 5) & 0x03];
    f.rx_bits     = BITS[(wr[3] >> 6) & 0x03];
    return f;
}

bool Z80SIO::Channel::empfaengerFrei() const {
    return rx_fifo.size() < RX_FIFO_DEPTH && (!autoEnables() || dcd_);
}

bool Z80SIO::Channel::rxIntEnabled() const {
    // WR1 D4–D3: 10 = jedes Zeichen (Parität beeinflusst den Vektor), 11 = jedes
    // Zeichen (Parität ohne Einfluss) — beide unterbrechen bei jedem Zeichen.
    return rx_int_mode == SIORxIntMode::ALL_CHARS ||
           rx_int_mode == SIORxIntMode::SPECIAL_CONDITION;
}

bool Z80SIO::Channel::rxIntFaellig() {
    if (rxIntEnabled()) return true;
    if (rx_int_mode == SIORxIntMode::FIRST_CHAR && rx_int_first_only) {
        rx_int_first_only = false;
        return true;
    }
    return false;
}

bool Z80SIO::Channel::txIntEnabled() const {
    return (wr[1] & 0x02) != 0;
}

void Z80SIO::Channel::rxByte(uint8_t byte) {
    // Auto Enables (WR3 D5): der Empfänger ist nur bei aktivem /DCD frei — ein
    // Zeichen bei inaktivem /DCD kommt nie an (Datenblatt, asynchroner Empfang).
    // Kein bekannter Gast setzt das Bit (s. h-Datei), die Tastaturwege sind unberührt.
    if (autoEnables() && !dcd_) return;
    last_rx = byte;  // Datenpfad hinter dem Schieberegister, unabhängig vom FIFO (s. h-Datei).
    if (rx_fifo.size() < RX_FIFO_DEPTH) {
        rx_fifo.push_back(byte);
        if (rx_fifo.size() == 1) {
            // Parity error detection not emulated — clear parity/framing/overrun
        }
    } else {
        rr1 |= 0x20; // Rx Overrun Error = RR1 D5 (Zilog; bis AP-ST3 fälschlich D3)
    }
    updateRR0();
    if (rxIntFaellig())
        irq_rx = true;
}

bool Z80SIO::Channel::txAvailable() const {
    // Auto Enables (WR3 D5): bei inaktivem /CTS gibt der Sender nichts ab — das
    // Zeichen bleibt im Puffer, RR0 D2 bleibt 0, der Gast wartet (Datenblatt).
    return tx_buf.has_value() && (!autoEnables() || cts_);
}

uint8_t Z80SIO::Channel::txGet() {
    uint8_t b = tx_buf.value_or(0xFF);
    tx_buf.reset();
    rr1 |= 0x01;  // All Sent
    updateRR0();
    if (txIntEnabled())
        irq_tx = true;
    return b;
}

bool Z80SIO::Channel::rxFull() const {
    return rx_fifo.size() >= RX_FIFO_DEPTH;
}

void Z80SIO::Channel::setClockSource(std::function<void()> clk_callback) {
    clk_cb_ = std::move(clk_callback);
}

void Z80SIO::Channel::setRTS(bool rts) {
    rts_ = rts;
    wr[5] = rts ? (wr[5] | 0x02) : (wr[5] & ~0x02);
}

bool Z80SIO::Channel::getCTS() const {
    return cts_;
}

// ─── Z80SIO ──────────────────────────────────────────────────────────────────

Z80SIO::Z80SIO(const std::string& name) : name_(name) {
    ch_a_.reset();
    ch_b_.reset();
}

bool Z80SIO::channelHasInterrupt(const Channel& ch) const {
    return ch.irq_rx || ch.irq_tx || ch.irq_ext;
}

/// Fordert @p ch gerade einen Interrupt an?  Ein Kanal, der bereits bedient wird
/// (IUS=1, noch kein RETI), fordert NICHT an.
bool Z80SIO::channelRequests(const Channel& ch) const {
    return channelHasInterrupt(ch) && !ch.ius;
}

bool Z80SIO::hasInterrupt() const {
    if (!iei_) return false;
    // WICHTIG: dieselbe Bedingung wie getVector() — inklusive `!ius`.  Ein Kanal,
    // der bereits bedient wird, darf keinen Interrupt mehr anfordern, auch wenn
    // inzwischen ein neues Zeichen aufgelaufen ist.  Sonst zieht die Karte /INT,
    // die Quittung findet in getVector() aber keinen vektorfähigen Kanal und
    // liefert die reine Vektorbasis → die ISR wird endlos erneut angesprungen.
    // Analog zu Z80CTC::anyServiceable() und Z80PIO::hasInterrupt() (f3b7ab1).
    //
    // NUR die Anforderungsseite.  getIEO() bleibt bewusst unverändert: die
    // Kettenseite ist bei allen drei Bausteinen gleich („sperrt bei ANSTEHENDEM,
    // nicht bei laufendem Interrupt") — sie hier allein umzustellen, würde die
    // Bausteine gegeneinander verstimmen.  Bewertung: doc/testsystem_rework.md §8.
    return channelRequests(ch_a_) || channelRequests(ch_b_);
}

bool Z80SIO::getIEO() const {
    // IEO is blocked (low) when this device has a pending acknowledged interrupt
    if (iei_ && (channelHasInterrupt(ch_a_) || channelHasInterrupt(ch_b_)))
        return false;
    return true;
}

Z80SIO::DebugState Z80SIO::debugState() const {
    DebugState d;
    d.iei = iei_;
    d.ieo = getIEO();
    const Channel* c[2] = { &ch_a_, &ch_b_ };
    for (int i = 0; i < 2; ++i) {
        d.ch[i].wr1      = c[i]->wr[1];
        d.ch[i].wr2      = c[i]->wr[2];   // interrupt vector (ch B programs it)
        d.ch[i].rr0      = c[i]->rr0;
        d.ch[i].rr1      = c[i]->rr1;
        d.ch[i].irqRx    = c[i]->irq_rx;
        d.ch[i].irqTx    = c[i]->irq_tx;
        d.ch[i].irqExt   = c[i]->irq_ext;
        d.ch[i].iei      = c[i]->iei;
        d.ch[i].ius      = c[i]->ius;
        d.ch[i].rxQueued = c[i]->rx_fifo.size();
        d.ch[i].txBusy   = c[i]->tx_buf.has_value();
    }
    return d;
}

// ─── Snapshot serialisation ──────────────────────────────────────────────────
//
// We serialise every data member of Channel *except* the wiring callback
// (clk_cb_) and the std::deque/std::optional members, which are handled
// explicitly.  A single field-visitor (visitChannelPod) lists the POD fields
// once and is reused for both write and read, so the two directions can never
// drift out of sync.
namespace {
struct SioWriter {
    std::vector<uint8_t>& o;
    template <class T> void operator()(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "POD fields only");
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        o.insert(o.end(), p, p + sizeof(T));
    }
};
struct SioReader {
    const uint8_t*& p;
    const uint8_t*  end;
    bool ok = true;
    template <class T> void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "POD fields only");
        if (!ok || static_cast<size_t>(end - p) < sizeof(T)) { ok = false; return; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
    }
};
// Visit the trivially-copyable fields of a Channel (const or mutable) in a
// fixed order.  rx_fifo / tx_buf / clk_cb_ are intentionally excluded.
template <class ChT, class F>
void visitChannelPod(ChT& ch, F& f) {
    f(ch.wr);
    f(ch.rr0); f(ch.rr1); f(ch.reg_ptr);
    f(ch.cts_); f(ch.rts_);
    f(ch.irq_rx); f(ch.irq_tx); f(ch.irq_ext); f(ch.iei); f(ch.ius);
    f(ch.mode); f(ch.sync_mode); f(ch.rx_int_mode); f(ch.stop_bits);
    f(ch.hunt_mode); f(ch.sync_found); f(ch.sync1); f(ch.sync2); f(ch.sync_shift_reg);
    f(ch.ones_count); f(ch.in_frame); f(ch.tx_abort);
    f(ch.rx_bits_per_char); f(ch.tx_bits_per_char); f(ch.clock_multiplier);
    f(ch.parity_enable); f(ch.parity_even);
    f(ch.rx_enable); f(ch.tx_enable); f(ch.auto_enables); f(ch.send_break);
    f(ch.sync_load_inhibit); f(ch.addr_search_mode);
    f(ch.rx_crc_enable); f(ch.tx_crc_enable); f(ch.rx_crc); f(ch.tx_crc); f(ch.crc_ccitt);
    f(ch.ext_int_enable); f(ch.tx_int_enable); f(ch.rx_int_first_only); f(ch.status_affects_vector);
    f(ch.dtr_); f(ch.dcd_); f(ch.cts_latch); f(ch.dcd_latch); f(ch.sync_latch);
    f(ch.rx_state); f(ch.rx_shift_reg); f(ch.rx_bit_count); f(ch.rx_sample_count);
    f(ch.tx_underrun); f(ch.break_abort_detected);
    f(ch.last_rx);
    f(ch.break_rx_); f(ch.ext_latch_); f(ch.ext_rr0_);   // AP-S3 (Savestate v6)
}
}  // namespace

void Z80SIO::serialize(std::vector<uint8_t>& out) const {
    SioWriter w{out};
    auto writeChannel = [&](const Channel& ch) {
        visitChannelPod(ch, w);
        uint32_t n = static_cast<uint32_t>(ch.rx_fifo.size());
        w(n);
        for (uint8_t b : ch.rx_fifo) w(b);
        uint8_t has = ch.tx_buf.has_value() ? 1 : 0;
        uint8_t val = ch.tx_buf.value_or(0);
        w(has); w(val);
    };
    writeChannel(ch_a_);
    writeChannel(ch_b_);
    w(iei_);
}

bool Z80SIO::deserialize(const uint8_t*& p, const uint8_t* end) {
    SioReader r{p, end};
    auto readChannel = [&](Channel& ch) {
        visitChannelPod(ch, r);
        uint32_t n = 0; r(n);
        ch.rx_fifo.clear();
        for (uint32_t i = 0; i < n && r.ok; ++i) { uint8_t b = 0; r(b); ch.rx_fifo.push_back(b); }
        uint8_t has = 0, val = 0; r(has); r(val);
        if (has) ch.tx_buf = val; else ch.tx_buf.reset();
    };
    readChannel(ch_a_);
    readChannel(ch_b_);
    bool iei = false; r(iei);
    iei_ = iei;
    // r.p aliases the caller's p, so it is already advanced.
    return r.ok;
}

void Z80SIO::setIEI(bool iei) {
    iei_ = iei;
    // Propagate IEI through internal daisy chain (Ch A > Ch B)
    ch_a_.iei = iei;
    // Block chain if Channel A has pending interrupt or is under service
    if (iei && (channelHasInterrupt(ch_a_) || ch_a_.ius)) {
        ch_b_.iei = false;
    } else {
        ch_b_.iei = iei;
    }
}

uint8_t Z80SIO::getVector() const {
    if (!iei_) return 0xFF;

    // Basisvektor aus WR2 von Kanal B.  Mit „status affects vector“ (WR1 Bit2 von
    // Kanal B) ersetzt die SIO die Bits 3…1 durch die Quelle (Zilog Z80 SIO, WR1/WR2;
    // U856 gleich):  B: Tx 000, Ext/Status 001, Rx 010, Sonderfall 011 —
    //                A: Tx 100, Ext/Status 101, Rx 110, Sonderfall 111.
    // Ohne das Bit bleibt der Vektor unverändert.  (Bis 2026-09-28 waren A-Ext/Status
    // und B-Tx vertauscht (000/101) und das Bit wurde nicht beachtet; das BIOS des
    // K8915 legt seine Tx-Routine auf D0H und lief dadurch in die Leere, AP-E3.)
    const uint8_t basis = ch_b_.wr[2];
    const bool    sav   = ch_b_.status_affects_vector;
    auto vektor = [&](uint8_t v321) -> uint8_t {
        return sav ? static_cast<uint8_t>((basis & 0xF1) | (v321 << 1)) : basis;
    };

    // Rangfolge innerhalb der SIO: A vor B, Rx vor Tx vor Ext/Status.
    if (ch_a_.iei && ch_a_.irq_rx && !ch_a_.ius) {
        ch_a_.irq_rx = false;
        ch_a_.ius = true;
        return vektor(0b110);
    }
    if (ch_a_.iei && ch_a_.irq_tx && !ch_a_.ius) {
        ch_a_.irq_tx = false;
        ch_a_.ius = true;
        return vektor(0b100);
    }
    if (ch_a_.iei && ch_a_.irq_ext && !ch_a_.ius) {
        ch_a_.irq_ext = false;
        ch_a_.ius = true;
        return vektor(0b101);
    }
    if (ch_b_.iei && ch_b_.irq_rx && !ch_b_.ius) {
        ch_b_.irq_rx = false;
        ch_b_.ius = true;
        return vektor(0b010);
    }
    if (ch_b_.iei && ch_b_.irq_tx && !ch_b_.ius) {
        ch_b_.irq_tx = false;
        ch_b_.ius = true;
        return vektor(0b000);
    }
    if (ch_b_.iei && ch_b_.irq_ext && !ch_b_.ius) {
        ch_b_.irq_ext = false;
        ch_b_.ius = true;
        return vektor(0b001);
    }

    return basis;
}

uint8_t Z80SIO::rr2Vektor() const {
    const uint8_t basis = ch_b_.wr[2];
    if (!ch_b_.status_affects_vector) return basis;
    auto vektor = [&](uint8_t v321) { return static_cast<uint8_t>((basis & 0xF1) | (v321 << 1)); };
    // Dieselbe Rangfolge wie getVector().
    if (ch_a_.iei && ch_a_.irq_rx  && !ch_a_.ius) return vektor(0b110);
    if (ch_a_.iei && ch_a_.irq_tx  && !ch_a_.ius) return vektor(0b100);
    if (ch_a_.iei && ch_a_.irq_ext && !ch_a_.ius) return vektor(0b101);
    if (ch_b_.iei && ch_b_.irq_rx  && !ch_b_.ius) return vektor(0b010);
    if (ch_b_.iei && ch_b_.irq_tx  && !ch_b_.ius) return vektor(0b000);
    if (ch_b_.iei && ch_b_.irq_ext && !ch_b_.ius) return vektor(0b001);
    return vektor(0b011);
}

void Z80SIO::onRETI() {
    // Clear IUS for whichever channel was being serviced
    // Channel A has higher priority, check it first
    if (ch_a_.iei && ch_a_.ius) {
        ch_a_.ius = false;
    } else if (ch_b_.iei && ch_b_.ius) {
        ch_b_.ius = false;
    }
}

void Z80SIO::writeControl(Channel& ch, uint8_t data, bool is_b) {
    if (ch.reg_ptr == 0) {
        uint8_t reg_sel = data & 0x07;
        uint8_t cmd = (data >> 3) & 0x07;
        uint8_t crc_cmd = (data >> 6) & 0x03;

        // Process commands (Manual p. 754-792)
        switch (cmd) {
            case 0: // Null command
                break;
            case 1: // Send Abort (SDLC)
                if (ch.mode == SIOMode::SDLC) {
                    ch.tx_abort = true;
                }
                break;
            case 2: // Reset Ext/Status Interrupts
                ch.rr1 &= ~0x1C;
                ch.irq_ext = false;
                ch.ext_latch_ = false;   // RR0 D3/D5/D7 zeigen wieder die Eingänge
                ch.updateRR0();
                break;
            case 3: // Channel Reset (full initialization)
                ch.reset();
                break;
            case 4: // Enable Int on Next Rx Character
                ch.rx_int_first_only = true;
                break;
            case 5: // Reset Tx Int Pending
                ch.irq_tx = false;
                break;
            case 6: // Error Reset (clear error flags)
                ch.rr1 &= ~0x70;  // Clear parity, overrun, framing errors
                break;
            case 7: // Return from Interrupt (High/Low reset)
                ch.ius = false;
                break;
        }

        // Process CRC reset commands (Manual p. 791)
        switch (crc_cmd) {
            case 1: // Reset Rx CRC Checker
                ch.rx_crc = 0xFFFF;
                break;
            case 2: // Reset Tx CRC Generator
                ch.tx_crc = 0xFFFF;
                break;
            case 3: // Reset both
                ch.rx_crc = ch.tx_crc = 0xFFFF;
                break;
        }

        if (reg_sel != 0)
            ch.reg_ptr = reg_sel;

    } else {
        // Writing to selected register
        ch.wr[ch.reg_ptr] = data;

        // Process specific registers immediately
        switch (ch.reg_ptr) {
            case 1: // Interrupt mode and data transfer
                processWR1(ch, data, is_b);
                break;
            case 3: // Receive parameters
                processWR3(ch, data);
                break;
            case 4: // Tx/Rx parameters and modes
                processWR4(ch, data);
                updateMode(ch);  // Recalculate operating mode
                break;
            case 5: // Transmit parameters
                processWR5(ch, data);
                break;
            case 6: // Sync char or SDLC address
                ch.sync1 = data;
                break;
            case 7: // Sync char or SDLC flag
                ch.sync2 = data;
                break;
        }

        ch.reg_ptr = 0;
    }
}

uint8_t Z80SIO::readControl(Channel& ch, bool is_b) const {
    uint8_t reg = ch.reg_ptr;
    ch.reg_ptr  = 0;  // reading resets pointer (mutable)

    switch (reg) {
        case 0: return ch.rr0;
        case 1: return ch.rr1;
        case 2:
            // Channel B returns modified vector; channel A returns raw WR2 of B.
            // Lesen ist KEINE Quittung (bis AP-ST3 stand hier getVector(): ein
            // Lesen von RR2 setzte IUS und löschte die Anforderung — am K8915 nahm
            // das der Tastatur ihren nächsten Interrupt).
            if (is_b) return rr2Vektor();
            return ch_b_.wr[2];
        default: return 0xFF;
    }
}

uint8_t Z80SIO::ioRead(uint8_t port) {
    switch (port & 0x03) {
        case 0: { // Ch A data
            if (!ch_a_.rx_fifo.empty()) {
                uint8_t b = ch_a_.rx_fifo.front();
                ch_a_.rx_fifo.pop_front();
                // „Jedes Zeichen": solange der FIFO noch Zeichen hält, bleibt die
                // Anforderung stehen (das nächste Zeichen unterbricht erneut, sobald
                // IUS per RETI fällt).  Vorher ging sie mit dem ersten Lesen verloren —
                // eine Tastatur, deren Zeichen sich während eines langen DI stauten,
                // blieb stumm (K8915, AP-ST3).
                ch_a_.irq_rx = !ch_a_.rx_fifo.empty() && ch_a_.rxIntEnabled();
                ch_a_.updateRR0();
                return b;
            }
            // Leerer Empfänger: keine eigene Ruhelage am Datenregister — die echte
            // U856 liefert das zuletzt empfangene Byte (last_rx), nicht FFH.
            return ch_a_.last_rx;
        }
        case 1: // Ch A control
            return readControl(ch_a_, false);
        case 2: { // Ch B data
            if (!ch_b_.rx_fifo.empty()) {
                uint8_t b = ch_b_.rx_fifo.front();
                ch_b_.rx_fifo.pop_front();
                // „Jedes Zeichen": solange der FIFO noch Zeichen hält, bleibt die
                // Anforderung stehen (das nächste Zeichen unterbricht erneut, sobald
                // IUS per RETI fällt).  Vorher ging sie mit dem ersten Lesen verloren —
                // eine Tastatur, deren Zeichen sich während eines langen DI stauten,
                // blieb stumm (K8915, AP-ST3).
                ch_b_.irq_rx = !ch_b_.rx_fifo.empty() && ch_b_.rxIntEnabled();
                ch_b_.updateRR0();
                return b;
            }
            return ch_b_.last_rx;
        }
        case 3: // Ch B control
            return readControl(ch_b_, true);
    }
    return 0xFF;
}

void Z80SIO::ioWrite(uint8_t port, uint8_t data) {
    switch (port & 0x03) {
        case 0: // Ch A data (TX)
            ch_a_.tx_buf = data;
            ch_a_.rr1 &= ~0x01;  // Clear All Sent
            ch_a_.updateRR0();
            ch_a_.irq_tx = false; // TX buffer now full, clear old TX int
            break;
        case 1: // Ch A control
            writeControl(ch_a_, data, false);
            break;
        case 2: // Ch B data (TX)
            ch_b_.tx_buf = data;
            ch_b_.rr1 &= ~0x01;
            ch_b_.updateRR0();
            ch_b_.irq_tx = false;
            break;
        case 3: // Ch B control
            writeControl(ch_b_, data, true);
            break;
    }
}

// ─── Register processing helpers ──────────────────────────────────────────────

void Z80SIO::processWR1(Channel& ch, uint8_t data, bool is_b) {
    // Ext Int Enable (D0)
    ch.ext_int_enable = (data & 0x01) != 0;

    // Tx Int Enable (D1)
    ch.tx_int_enable = (data & 0x02) != 0;

    // Status Affects Vector (D2) - only Channel B
    if (is_b) {
        ch.status_affects_vector = (data & 0x04) != 0;
    }

    // Rx Int Mode (D4:D3)
    uint8_t rx_mode = (data >> 3) & 0x03;
    ch.rx_int_mode = static_cast<SIORxIntMode>(rx_mode);
    // Betriebsart 01 ist nach dem Setzen für das nächste Zeichen scharf (Datenblatt).
    ch.rx_int_first_only = (ch.rx_int_mode == SIORxIntMode::FIRST_CHAR);

    // WAIT/READY mode (D7:D5) - not implemented in basic emulation
}

void Z80SIO::processWR3(Channel& ch, uint8_t data) {
    // Rx Enable (D0)
    ch.rx_enable = (data & 0x01) != 0;

    // Sync Char Load Inhibit (D1)
    ch.sync_load_inhibit = (data & 0x02) != 0;

    // Address Search Mode/SDLC (D2)
    ch.addr_search_mode = (data & 0x04) != 0;

    // Rx CRC Enable (D3)
    ch.rx_crc_enable = (data & 0x08) != 0;

    // Enter Hunt Mode (D4)
    if (data & 0x10) {
        ch.hunt_mode = true;
        ch.sync_found = false;
    }

    // Auto Enables (D5)
    ch.auto_enables = (data & 0x20) != 0;

    // Rx Bits/Character (D7:D6)
    uint8_t rx_bits = (data >> 6) & 0x03;
    ch.rx_bits_per_char = (rx_bits == 0) ? 5 : (rx_bits == 1) ? 7 : (rx_bits == 2) ? 6 : 8;
}

void Z80SIO::processWR4(Channel& ch, uint8_t data) {
    // Parity Enable (D0)
    ch.parity_enable = (data & 0x01) != 0;

    // Parity Even/Odd (D1)
    ch.parity_even = (data & 0x02) != 0;

    // Stop Bits (D3:D2)
    uint8_t stop_bits = (data >> 2) & 0x03;
    ch.stop_bits = (stop_bits == 0) ? SIOStopBits::SYNC :
                   (stop_bits == 1) ? SIOStopBits::ONE :
                   (stop_bits == 2) ? SIOStopBits::ONE_HALF : SIOStopBits::TWO;

    // Sync Mode (D5:D4)
    uint8_t sync_mode = (data >> 4) & 0x03;
    ch.sync_mode = static_cast<SIOSyncMode>(sync_mode);

    // Clock Rate (D7:D6)
    uint8_t clock_rate = (data >> 6) & 0x03;
    ch.clock_multiplier = (clock_rate == 0) ? 1 :
                          (clock_rate == 1) ? 16 :
                          (clock_rate == 2) ? 32 : 64;
}

void Z80SIO::processWR5(Channel& ch, uint8_t data) {
    // Tx CRC Enable (D0)
    ch.tx_crc_enable = (data & 0x01) != 0;

    // RTS (D1)
    ch.rts_ = (data & 0x02) != 0;

    // Tx Enable (D3)
    ch.tx_enable = (data & 0x08) != 0;

    // Send Break (D4)
    ch.send_break = (data & 0x10) != 0;

    // Tx Bits/Character (D6:D5)
    uint8_t tx_bits = (data >> 5) & 0x03;
    ch.tx_bits_per_char = (tx_bits == 0) ? 5 : (tx_bits == 1) ? 7 : (tx_bits == 2) ? 6 : 8;

    // DTR (D7)
    ch.dtr_ = (data & 0x80) != 0;
}

void Z80SIO::updateMode(Channel& ch) {
    // Determine mode from WR4 settings
    if (ch.stop_bits == SIOStopBits::SYNC) {
        // Synchronous mode
        switch (ch.sync_mode) {
            case SIOSyncMode::SYNC_8BIT:
                ch.mode = SIOMode::MONOSYNC;
                break;
            case SIOSyncMode::SYNC_16BIT:
                ch.mode = SIOMode::BISYNC;
                break;
            case SIOSyncMode::SDLC:
                ch.mode = SIOMode::SDLC;
                ch.crc_ccitt = true;  // SDLC uses CRC-CCITT
                break;
            case SIOSyncMode::EXTERNAL:
                ch.mode = SIOMode::EXTERNAL;
                break;
        }
    } else {
        ch.mode = SIOMode::ASYNC;
    }
}

// ─── CRC calculation ──────────────────────────────────────────────────────────

uint16_t Z80SIO::updateCRC(uint16_t crc, uint8_t data, bool ccitt) const {
    // CRC-16: x^16 + x^15 + x^2 + 1 (polynomial 0x8005)
    // CRC-CCITT: x^16 + x^12 + x^5 + 1 (polynomial 0x1021)
    uint16_t polynomial = ccitt ? 0x1021 : 0x8005;

    for (int i = 0; i < 8; i++) {
        bool bit = (crc & 0x8000) != 0;
        crc <<= 1;
        if ((data & (0x80 >> i)) != 0) {
            crc ^= 0x0001;
        }
        if (bit) {
            crc ^= polynomial;
        }
    }

    return crc;
}

bool Z80SIO::checkCRCResidue(uint16_t crc, bool ccitt) const {
    // Valid CRC residue after including CRC bytes
    uint16_t valid_residue = ccitt ? 0xF0B8 : 0x0000;
    return crc == valid_residue;
}

// ─── Asynchronous mode ────────────────────────────────────────────────────────

void Z80SIO::asyncTransmit(Channel& ch) {
    if (!ch.tx_enable || !ch.tx_buf.has_value()) return;

    uint8_t data = ch.tx_buf.value();

    // In a real implementation, bits would be clocked out serially
    // For emulation, we mark the transmission complete immediately
    // and trigger callbacks if connected to another device

    // Build complete async frame for reference:
    // - Start bit (0)
    // - Data bits (LSB first)
    // - Parity bit (if enabled)
    // - Stop bits (1 or 2)

    // Signal transmission complete
    ch.tx_buf.reset();
    ch.rr1 |= 0x01;  // All Sent
    ch.updateRR0();

    if (ch.tx_int_enable) {
        ch.irq_tx = true;
    }

    // In real hardware, would call serial output callback here
    // For now, stub implementation for basic emulation
}

void Z80SIO::asyncReceive(Channel& ch, bool bit) {
    // State machine for async receive with error detection
    switch (ch.rx_state) {
        case SIORxState::IDLE:
            if (!bit) {  // Start bit detected (falling edge)
                ch.rx_state = SIORxState::START_BIT;
                ch.rx_bit_count = 0;
                ch.rx_shift_reg = 0;
                ch.rx_sample_count = 0;
            }
            break;

        case SIORxState::START_BIT:
            // Sample at middle of start bit
            if (ch.rx_sample_count++ >= ch.clock_multiplier / 2) {
                if (!bit) {  // Valid start bit (still 0)
                    ch.rx_state = SIORxState::DATA_BITS;
                    ch.rx_sample_count = 0;
                } else {
                    ch.rx_state = SIORxState::IDLE;  // False start
                }
            }
            break;

        case SIORxState::DATA_BITS:
            if (ch.rx_sample_count++ >= ch.clock_multiplier) {
                ch.rx_shift_reg |= (bit << ch.rx_bit_count);
                ch.rx_bit_count++;
                ch.rx_sample_count = 0;

                if (ch.rx_bit_count >= ch.rx_bits_per_char) {
                    if (ch.parity_enable) {
                        ch.rx_state = SIORxState::PARITY_BIT;
                    } else {
                        ch.rx_state = SIORxState::STOP_BIT;
                    }
                }
            }
            break;

        case SIORxState::PARITY_BIT:
            if (ch.rx_sample_count++ >= ch.clock_multiplier) {
                // Check parity
                int ones = 0;
                for (int i = 0; i < ch.rx_bits_per_char; i++) {
                    if ((ch.rx_shift_reg >> i) & 1) ones++;
                }
                bool expected_parity = ch.parity_even ? (ones % 2 == 1) : (ones % 2 == 0);

                if (bit != expected_parity) {
                    ch.rr1 |= 0x10;  // Parity error
                }

                ch.rx_state = SIORxState::STOP_BIT;
                ch.rx_sample_count = 0;
            }
            break;

        case SIORxState::STOP_BIT:
            if (ch.rx_sample_count++ >= ch.clock_multiplier) {
                if (!bit) {
                    ch.rr1 |= 0x40;  // Framing error (stop bit not 1)
                }

                // Push to FIFO
                const size_t RX_FIFO_DEPTH = 3;
                if (ch.rx_fifo.size() < RX_FIFO_DEPTH) {
                    ch.rx_fifo.push_back(ch.rx_shift_reg);
                    ch.updateRR0();

                    // Trigger interrupt
                    if (ch.rxIntFaellig()) {
                        ch.irq_rx = true;
                    }
                } else {
                    ch.rr1 |= 0x20;  // Overrun error
                }

                ch.rx_state = SIORxState::IDLE;
                ch.rx_sample_count = 0;
            }
            break;
    }
}

// ─── Synchronous modes (Monosync/Bisync) ──────────────────────────────────────

void Z80SIO::syncReceive(Channel& ch, bool bit) {
    // Shift incoming bit into sync shift register
    ch.sync_shift_reg = (ch.sync_shift_reg << 1) | (bit ? 1 : 0);

    if (ch.hunt_mode) {
        // Hunt for sync pattern
        if (ch.mode == SIOMode::MONOSYNC) {
            // Compare low 8 bits with sync1
            if ((ch.sync_shift_reg & 0xFF) == ch.sync1) {
                ch.sync_found = true;
                ch.hunt_mode = false;
                ch.rx_bit_count = 0;
                ch.rx_shift_reg = 0;
            }
        } else if (ch.mode == SIOMode::BISYNC) {
            // Compare all 16 bits with sync1 + sync2
            uint16_t sync_pattern = (static_cast<uint16_t>(ch.sync1) << 8) | ch.sync2;
            if (ch.sync_shift_reg == sync_pattern) {
                ch.sync_found = true;
                ch.hunt_mode = false;
                ch.rx_bit_count = 0;
                ch.rx_shift_reg = 0;
            }
        }
    } else {
        // Synchronized - receive data
        ch.rx_shift_reg = (ch.rx_shift_reg << 1) | (bit ? 1 : 0);
        ch.rx_bit_count++;

        if (ch.rx_bit_count >= 8) {
            // Complete character received
            uint8_t data = ch.rx_shift_reg & 0xFF;

            // Update CRC if enabled
            if (ch.rx_crc_enable) {
                ch.rx_crc = updateCRC(ch.rx_crc, data, ch.crc_ccitt);
            }

            // Push to FIFO
            const size_t RX_FIFO_DEPTH = 3;
            if (ch.rx_fifo.size() < RX_FIFO_DEPTH) {
                ch.rx_fifo.push_back(data);
                ch.updateRR0();

                if (ch.rxIntFaellig()) {
                    ch.irq_rx = true;
                }
            } else {
                ch.rr1 |= 0x20;  // Overrun
            }

            ch.rx_bit_count = 0;
            ch.rx_shift_reg = 0;
        }
    }
}

void Z80SIO::syncTransmit(Channel& ch) {
    if (!ch.tx_enable) return;

    if (!ch.tx_buf.has_value()) {
        // Underrun - send sync characters
        if (ch.mode == SIOMode::MONOSYNC) {
            ch.tx_buf = ch.sync1;
        } else if (ch.mode == SIOMode::BISYNC) {
            // In real implementation would alternate sync1 and sync2
            ch.tx_buf = ch.sync1;
        }
        ch.tx_underrun = true;
    }

    if (ch.tx_buf.has_value()) {
        uint8_t data = ch.tx_buf.value();

        // Update CRC if enabled
        if (ch.tx_crc_enable) {
            ch.tx_crc = updateCRC(ch.tx_crc, data, ch.crc_ccitt);
        }

        // In real hardware, would clock out bits serially
        // For emulation, mark complete immediately
        ch.tx_buf.reset();
        ch.updateRR0();

        if (ch.tx_int_enable) {
            ch.irq_tx = true;
        }
    }
}

// ─── SDLC/HDLC mode ───────────────────────────────────────────────────────────

void Z80SIO::sdlcReceive(Channel& ch, bool bit) {
    // Count consecutive 1s
    if (bit) {
        ch.ones_count++;
    } else {
        if (ch.ones_count == 5) {
            // Bit stuffing - skip this 0
            ch.ones_count = 0;
            return;
        } else if (ch.ones_count >= 6) {
            // Flag or abort detected
            if (ch.ones_count == 6) {
                // Flag: 01111110
                if (ch.hunt_mode) {
                    ch.hunt_mode = false;
                    ch.in_frame = true;
                    ch.rx_bit_count = 0;
                    ch.rx_shift_reg = 0;
                    ch.rx_crc = 0xFFFF;
                } else {
                    // End of frame
                    ch.in_frame = false;

                    // Check CRC
                    if (ch.rx_crc_enable && !checkCRCResidue(ch.rx_crc, true)) {
                        ch.rr1 |= 0x40;  // CRC error
                    } else {
                        ch.rr1 |= 0x80;  // End of Frame flag
                    }
                }
            } else {
                // Abort: 7 or more consecutive 1s
                ch.in_frame = false;
                ch.hunt_mode = true;
                ch.rr1 |= 0x80;  // Abort condition

                if (ch.ext_int_enable) {
                    ch.irq_ext = true;
                }
            }
            ch.ones_count = 0;
            return;
        }
        ch.ones_count = 0;
    }

    if (ch.in_frame) {
        // Receive data bits
        ch.rx_shift_reg = (ch.rx_shift_reg << 1) | (bit ? 1 : 0);
        ch.rx_bit_count++;

        if (ch.rx_bit_count >= 8) {
            uint8_t data = ch.rx_shift_reg & 0xFF;

            // Update CRC
            if (ch.rx_crc_enable) {
                ch.rx_crc = updateCRC(ch.rx_crc, data, true);  // CCITT polynomial
            }

            // Address field matching (if enabled)
            if (ch.addr_search_mode && ch.rx_fifo.empty()) {
                if (data != ch.sync1 && data != 0xFF) {  // Not our address or broadcast
                    ch.hunt_mode = true;
                    ch.in_frame = false;
                    ch.rx_bit_count = 0;
                    return;
                }
            }

            // Push to FIFO
            const size_t RX_FIFO_DEPTH = 3;
            if (ch.rx_fifo.size() < RX_FIFO_DEPTH) {
                ch.rx_fifo.push_back(data);
                ch.updateRR0();

                if (ch.rxIntFaellig()) {
                    ch.irq_rx = true;
                }
            } else {
                ch.rr1 |= 0x20;  // Overrun
            }

            ch.rx_bit_count = 0;
            ch.rx_shift_reg = 0;
        }
    }
}

void Z80SIO::sdlcTransmit(Channel& ch) {
    if (!ch.tx_enable) return;

    if (ch.tx_abort) {
        // Send abort sequence: 01111111 (or more 1s)
        // In real hardware would clock out bits
        ch.tx_abort = false;
        ch.in_frame = false;
        return;
    }

    if (!ch.in_frame) {
        // Send opening flag: 01111110
        sendSDLCByte(ch, 0x7E, false);  // No stuffing for flags
        ch.in_frame = true;
        ch.tx_crc = 0xFFFF;
        return;
    }

    if (!ch.tx_buf.has_value()) {
        // End of data - send CRC and closing flag
        if (ch.tx_crc_enable) {
            // Send CRC (16 bits, complemented)
            uint16_t crc_out = ch.tx_crc ^ 0xFFFF;
            sendSDLCByte(ch, crc_out & 0xFF, true);
            sendSDLCByte(ch, (crc_out >> 8) & 0xFF, true);
        }

        // Send closing flag
        sendSDLCByte(ch, 0x7E, false);
        ch.in_frame = false;

        ch.rr1 |= 0x01;  // All sent
        ch.updateRR0();
        return;
    }

    // Send data byte with bit stuffing
    uint8_t data = ch.tx_buf.value();

    if (ch.tx_crc_enable) {
        ch.tx_crc = updateCRC(ch.tx_crc, data, true);
    }

    sendSDLCByte(ch, data, true);

    ch.tx_buf.reset();
    ch.updateRR0();

    if (ch.tx_int_enable) {
        ch.irq_tx = true;
    }
}

void Z80SIO::sendSDLCByte(Channel& ch, uint8_t data, bool stuff) {
    // In real hardware, would clock out bits with stuffing
    // For emulation, this is a reference implementation
    ch.ones_count = 0;

    for (int i = 0; i < 8; i++) {
        bool bit = (data >> i) & 1;

        // Send bit (would call clock callback in real implementation)

        if (stuff && bit) {
            ch.ones_count++;
            if (ch.ones_count == 5) {
                // Insert stuffing bit (0)
                // Send 0
                ch.ones_count = 0;
            }
        } else {
            ch.ones_count = 0;
        }
    }
}

// ─── External/Status interrupts ───────────────────────────────────────────────

void Z80SIO::monitorModemSignals(Channel& ch) {
    if (!ch.ext_int_enable) return;

    // Latch current modem signals
    bool prev_cts = ch.cts_latch;
    bool prev_dcd = ch.dcd_latch;
    bool prev_sync = ch.sync_latch;

    ch.cts_latch = ch.cts_;
    ch.dcd_latch = ch.dcd_;
    ch.sync_latch = (ch.mode != SIOMode::ASYNC) && ch.sync_found;

    // Detect transitions
    if (ch.cts_latch != prev_cts ||
        ch.dcd_latch != prev_dcd ||
        ch.sync_latch != prev_sync) {
        ch.irq_ext = true;
    }

    // Tx Underrun/EOM condition
    if (ch.tx_underrun) {
        ch.irq_ext = true;
        ch.tx_underrun = false;
    }

    // Break/Abort detection
    if (ch.break_abort_detected) {
        ch.irq_ext = true;
    }
}
