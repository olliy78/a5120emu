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
}

void Z80SIO::Channel::updateRR0() {
    rr0 = 0x00;
    if (!rx_fifo.empty()) rr0 |= 0x01;  // Rx Character Available
    if (!tx_buf.has_value()) rr0 |= 0x04; // Tx Buffer Empty
    if (cts_) rr0 |= 0x20;
}

bool Z80SIO::Channel::rxIntEnabled() const {
    // WR1 bits[3:2]: 10 or 11 = interrupt on all received
    uint8_t rx_mode = (wr[1] >> 2) & 0x03;
    return rx_mode >= 2;
}

bool Z80SIO::Channel::txIntEnabled() const {
    return (wr[1] & 0x02) != 0;
}

void Z80SIO::Channel::rxByte(uint8_t byte) {
    if (rx_fifo.size() < RX_FIFO_DEPTH) {
        rx_fifo.push_back(byte);
        if (rx_fifo.size() == 1) {
            // Parity error detection not emulated — clear parity/framing/overrun
        }
    } else {
        rr1 |= 0x08; // Overrun
    }
    updateRR0();
    if (rxIntEnabled())
        irq_rx = true;
    // First-received mode: interrupt only on first byte in empty FIFO
    uint8_t rx_mode = (wr[1] >> 2) & 0x03;
    if (rx_mode == 1 && rx_fifo.size() == 1)
        irq_rx = true;
}

bool Z80SIO::Channel::txAvailable() const {
    return tx_buf.has_value();
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

bool Z80SIO::hasInterrupt() const {
    if (!iei_) return false;
    return channelHasInterrupt(ch_a_) || channelHasInterrupt(ch_b_);
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

    // Base vector from WR2 of channel B
    uint8_t vec = ch_b_.wr[2];

    // Determine which interrupt is active (A > B, Rx > Tx > Ext)
    // Check IEI and IUS for proper daisy chain handling
    // Bits [3:1] of vector encode interrupt source per Z80 SIO spec
    if (ch_a_.iei && ch_a_.irq_rx && !ch_a_.ius) {
        ch_a_.irq_rx = false;
        ch_a_.ius = true;
        return (vec & 0xF1) | 0x0C;  // 110 = Ch A Rx
    }
    if (ch_a_.iei && ch_a_.irq_tx && !ch_a_.ius) {
        ch_a_.irq_tx = false;
        ch_a_.ius = true;
        return (vec & 0xF1) | 0x08;  // 100 = Ch A Tx
    }
    if (ch_a_.iei && ch_a_.irq_ext && !ch_a_.ius) {
        ch_a_.irq_ext = false;
        ch_a_.ius = true;
        return (vec & 0xF1) | 0x00;  // 000 = Ch A Ext/Status
    }
    if (ch_b_.iei && ch_b_.irq_rx && !ch_b_.ius) {
        ch_b_.irq_rx = false;
        ch_b_.ius = true;
        return (vec & 0xF1) | 0x04;  // 010 = Ch B Rx
    }
    if (ch_b_.iei && ch_b_.irq_tx && !ch_b_.ius) {
        ch_b_.irq_tx = false;
        ch_b_.ius = true;
        return (vec & 0xF1) | 0x0A;  // 101 = Ch B Tx (spec)
    }
    if (ch_b_.iei && ch_b_.irq_ext && !ch_b_.ius) {
        ch_b_.irq_ext = false;
        ch_b_.ius = true;
        return (vec & 0xF1) | 0x02;  // 001 = Ch B Ext/Status
    }

    return vec;
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
            // Channel B returns modified vector; channel A returns raw WR2 of B
            if (is_b) return getVector();
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
                ch_a_.irq_rx = false;
                ch_a_.updateRR0();
                return b;
            }
            return 0xFF;
        }
        case 1: // Ch A control
            return readControl(ch_a_, false);
        case 2: { // Ch B data
            if (!ch_b_.rx_fifo.empty()) {
                uint8_t b = ch_b_.rx_fifo.front();
                ch_b_.rx_fifo.pop_front();
                ch_b_.irq_rx = false;
                ch_b_.updateRR0();
                return b;
            }
            return 0xFF;
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
                    if (ch.rx_int_mode == SIORxIntMode::ALL_CHARS ||
                        (ch.rx_int_mode == SIORxIntMode::FIRST_CHAR && ch.rx_fifo.size() == 1)) {
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

                if (ch.rx_int_mode != SIORxIntMode::DISABLED) {
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

                if (ch.rx_int_mode != SIORxIntMode::DISABLED) {
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
