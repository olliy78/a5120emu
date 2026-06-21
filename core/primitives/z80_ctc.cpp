/**
 * @file z80_ctc.cpp
 * @brief Z80 Counter/Timer Circuit (CTC) Chip Emulation - Implementation
 *
 * Contains the complete implementation of the Z80 CTC (Counter/Timer Circuit)
 * emulation for the Robotron A5120 office computer. Implements all timer and
 * counter modes with cycle-accurate timing and proper interrupt handling.
 *
 * The implementation is organized into the following sections:
 * - Initialization and configuration
 * - I/O port access (control register and time constant programming)
 * - Timer mode operation (system clock division with prescaler)
 * - Counter mode operation (external clock/trigger counting)
 * - Zero Count/Timeout pulse generation
 * - Interrupt handling with daisy-chain priority
 * - RETI detection for nested interrupt support
 *
 * Implementation Notes:
 * - Time constant 0 is treated as 256 (full 8-bit range)
 * - Prescaler values: 16 or 256 for timer mode
 * - Counter reloads to time constant value on zero crossing
 * - New time constant takes effect at next zero during operation (U857D p.613)
 * - Internal daisy chain: Ch0 (highest) > Ch1 > Ch2 > Ch3 (lowest)
 * - IUS (Interrupt Under Service) flag prevents lower priority interrupts
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8430 CTC Technical Manual
 * @see Robotron A5120 Technical Documentation (U857D Manual, pages 612-616)
 */
#include "z80_ctc.h"

// =============================================================================
/// @name Initialization and Configuration
// =============================================================================

/**
 * @brief Constructor - creates a CTC instance with the given name.
 * @param name Human-readable name for debugging purposes
 */
Z80CTC::Z80CTC(const std::string& name) : name_(name) {}

/**
 * @brief Set callback function for Zero Count/Timeout output pulses.
 *
 * The callback is invoked whenever a channel reaches zero and generates a
 * ZC/TO pulse. The pulse is active for one clock cycle (level=false during
 * the pulse, level=true to clear it).
 *
 * @param cb Callback function receiving (channel, level)
 */
void Z80CTC::setZCTOCallback(ZCTOCallback cb) {
    zcto_cb_ = std::move(cb);
}

// =============================================================================
/// @name I/O Port Access
// =============================================================================

/**
 * @brief Handle I/O write - program control register or time constant.
 *
 * This method implements the complete CTC programming interface:
 * - Bit 0 = 0: Interrupt vector (channel 0 only)
 * - Bit 0 = 1: Control word, optionally followed by time constant
 *
 * Control Word Format (when D0=1):
 * - D7: Interrupt enable (1=enabled)
 * - D6: Mode (0=Timer, 1=Counter)
 * - D5: Prescaler (0=16, 1=256) for Timer mode
 * - D4: Trigger edge (0=falling, 1=rising) for Counter mode
 * - D3: Time constant follows / Trigger starts timer
 * - D2: Time constant follows this control word
 * - D1: Software reset
 * - D0: Always 1 for control word
 *
 * @param port Channel number (0-3)
 * @param data Control byte or time constant value
 */
void Z80CTC::ioWrite(uint8_t port, uint8_t data) {
    int ch = port & 0x03;
    Channel& c = ch_[ch];

    if (c.awaiting_tc) {
        c.timeConst   = data;
        c.awaiting_tc = false;

        bool counter_mode = (c.control & CTRL_COUNTER) != 0;
        bool clk_trg_mode = (c.control & CTRL_CLK_TRG) != 0;

        // Only load counter immediately if not currently running
        // Per U857D manual p.613: during operation, new TC takes effect at next zero crossing
        if (!c.running) {
            c.counter     = (data == 0) ? 256 : static_cast<int>(data);
            c.prescaleCnt = 0;

            if (counter_mode) {
                c.running = true;
            } else if (!clk_trg_mode) {
                c.running = true;
            }
        }
        return;
    }

    if ((data & 0x01) == 0) {
        if (ch == 0) {
            vec_base_ = data & 0xF8;
        }
        return;
    }

    c.control     = data;
    c.awaiting_tc = (data & CTRL_TC_FOLLOWS) != 0;

    if (data & CTRL_RESET) {
        c.running     = false;
        c.counter     = 0;
        c.prescaleCnt = 0;
        c.int_pending = false;
        c.zcto_active = false;
    }
}

/**
 * @brief Handle I/O read - returns the current down-counter value.
 *
 * Reading a CTC channel port returns the current state of the down-counter.
 * This allows software to monitor the counter progress without stopping it.
 *
 * @param port Channel number (0-3)
 * @return Current down-counter value (8-bit)
 */
uint8_t Z80CTC::ioRead(uint8_t port) {
    int ch = port & 0x03;
    return static_cast<uint8_t>(ch_[ch].counter & 0xFF);
}

/**
 * @brief Advance timer counters by one system clock cycle.
 *
 * This method should be called once per system clock cycle to update all
 * timer-mode channels. For each running channel in Timer mode:
 * 1. Clear any active ZC/TO output pulse (returns to high level)
 * 2. Increment prescaler counter (divides by 16 or 256)
 * 3. When prescaler overflows, decrement main counter
 * 4. When main counter reaches zero, reload from time constant and fire ZC/TO pulse
 *
 * Counter-mode channels are not affected by clockTick - they are updated by
 * external clock/trigger events via clkTrg().
 */
void Z80CTC::clockTick() {
    for (int i = 0; i < 4; i++) {
        Channel& c = ch_[i];

        if (c.zcto_active) {
            c.zcto_active = false;
            if (zcto_cb_) zcto_cb_(i, true);
        }

        if (!c.running || (c.control & CTRL_COUNTER)) continue;

        int prescale = (c.control & CTRL_PRESCALE256) ? 256 : 16;
        if (++c.prescaleCnt >= prescale) {
            c.prescaleCnt = 0;
            if (--c.counter <= 0) {
                c.counter = (c.timeConst == 0) ? 256 : static_cast<int>(c.timeConst);
                fireZCTO(i);
            }
        }
    }
}

/**
 * @brief Handle external clock/trigger pulse for a specific channel.
 *
 * This method processes external clock/trigger events for both Counter mode
 * and Timer mode with trigger enabled (CLK_TRG bit set).
 *
 * Behavior depends on channel configuration:
 * - **Counter mode**: Each matching edge decrements the counter. When zero is
 *   reached, the counter reloads and a ZC/TO pulse is generated.
 * - **Timer mode with CLK_TRG**: A matching trigger edge starts the timer.
 *   The timer must be configured with a time constant before triggering.
 *
 * Edge selection is controlled by CTRL_EDGE_RISE bit:
 * - 0 = falling edge (high-to-low transition)
 * - 1 = rising edge (low-to-high transition)
 *
 * @param channel Channel number (0-3)
 * @param rising_edge true for rising edge, false for falling edge
 */
void Z80CTC::clkTrg(int channel, bool rising_edge) {
    Channel& c = ch_[channel];
    bool want_rising = (c.control & CTRL_EDGE_RISE) != 0;
    if (rising_edge != want_rising) return;

    if (c.control & CTRL_COUNTER) {
        if (!c.running) return;
        if (--c.counter <= 0) {
            c.counter = (c.timeConst == 0) ? 256 : static_cast<int>(c.timeConst);
            fireZCTO(channel);
        }
    } else if (c.control & CTRL_CLK_TRG) {
        if (!c.running && !c.awaiting_tc && c.timeConst != 0) {
            c.running     = true;
            c.prescaleCnt = 0;
        }
    }
}

/**
 * @brief Generate Zero Count/Timeout pulse and handle interrupt generation.
 *
 * This internal helper is called when a channel's counter reaches zero.
 * It performs the following actions:
 * 1. Activates the ZC/TO output pulse (low level) for one clock cycle
 * 2. Invokes the ZC/TO callback if registered
 * 3. Sets the interrupt pending flag if interrupts are enabled
 * 4. Stops the timer if in CLK_TRG mode (single-shot timer)
 *
 * The ZC/TO pulse can be used to trigger other CTC channels or external devices.
 * In the A5120, ZC/TO pulses are used for baud rate generation for serial ports.
 *
 * @param ch Channel number (0-3)
 */
void Z80CTC::fireZCTO(int ch) {
    Channel& c = ch_[ch];
    c.zcto_active = true;
    if (zcto_cb_) zcto_cb_(ch, false);

    if (c.control & CTRL_INT_EN) c.int_pending = true;

    if (c.control & CTRL_CLK_TRG) c.running = false;
}

/**
 * @brief Check if any channel has a pending interrupt request.
 *
 * Scans all four channels to determine if any have their interrupt pending
 * flag set. This is used internally by the interrupt daisy-chain logic.
 *
 * @return true if at least one channel has int_pending set, false otherwise
 */
bool Z80CTC::anyPending() const {
    for (const auto& c : ch_) {
        if (c.int_pending) return true;
    }
    return false;
}

/**
 * @brief Check for a channel that can actually be vectored right now.
 *
 * A channel only pulls /INT low if it has a pending interrupt AND is enabled
 * by the internal daisy chain (iei) AND is not already under service (ius).
 * A channel whose interrupt is blocked by a higher-priority channel's IUS
 * (iei=false) must NOT assert /INT — otherwise the CPU would acknowledge an
 * interrupt for which getVector() has no valid vector and would read the
 * spurious 0xFF, re-triggering every instruction.  This mirrors exactly the
 * acceptance condition in getVector().
 */
bool Z80CTC::anyServiceable() const {
    for (const auto& c : ch_) {
        if (c.int_pending && c.iei && !c.ius) return true;
    }
    return false;
}

/**
 * @brief Set the Interrupt Enable Input (IEI) signal for daisy-chain operation.
 *
 * The IEI input controls whether this CTC can request interrupts. In a Z80
 * daisy-chain, devices pass the IEI signal downstream through their IEO output.
 *
 * This method propagates IEI through the internal four-channel daisy chain:
 * - Channel 0 receives the external IEI
 * - Each subsequent channel receives the previous channel's internal IEO
 * - A channel blocks IEI propagation if it has a pending interrupt or is
 *   currently being serviced (IUS flag set)
 *
 * Internal priority: Channel 0 (highest) > Ch1 > Ch2 > Ch3 (lowest)
 *
 * @param v IEI signal state (true = interrupts enabled from upstream)
 */
void Z80CTC::setIEI(bool v) {
    iei_ = v;
    // Propagate IEI through internal daisy chain (Ch0 > Ch1 > Ch2 > Ch3)
    bool iei = v;
    for (int i = 0; i < 4; i++) {
        ch_[i].iei = iei;
        // Block chain if this channel has pending interrupt or is under service
        if (iei && (ch_[i].int_pending || ch_[i].ius)) {
            iei = false;
        }
    }
}

/**
 * @brief Check if the CTC is currently requesting an interrupt.
 *
 * Returns true only if both conditions are met:
 * 1. IEI is asserted (interrupts enabled from upstream in daisy-chain)
 * 2. At least one channel has a pending interrupt
 *
 * This method is called by the interrupt controller to determine if the
 * CTC needs attention.
 *
 * @return true if requesting interrupt, false otherwise
 */
bool Z80CTC::hasInterrupt() const {
    return iei_ && anyServiceable();
}

/**
 * @brief Get the Interrupt Enable Output (IEO) signal for daisy-chain operation.
 *
 * IEO indicates whether interrupts can be passed to the next device in the
 * daisy-chain. The CTC asserts IEO (returns true) only if:
 * 1. IEI is asserted (interrupts enabled from upstream)
 * 2. No channel has a pending interrupt
 *
 * If any channel is requesting an interrupt, IEO is blocked (returns false)
 * to prevent lower-priority devices from interrupting.
 *
 * @return true if interrupts can pass to downstream devices, false if blocked
 */
bool Z80CTC::getIEO() const {
    return iei_ && !anyPending();
}

/**
 * @brief Get the interrupt vector for the highest-priority requesting channel.
 *
 * This method is called during interrupt acknowledge cycle to obtain the
 * vector for the interrupting channel. The vector is computed as:
 * - Base vector (programmed into channel 0, D7-D3)
 * - Channel number encoded in bits D2-D1 (0=Ch0, 2=Ch1, 4=Ch2, 6=Ch3)
 * - Bit D0 is always 0
 *
 * Side effects (per Z80 interrupt acknowledge protocol):
 * 1. Clears the int_pending flag for the acknowledged channel
 * 2. Sets the IUS (Interrupt Under Service) flag for that channel
 * 3. This blocks lower-priority channels until RETI is executed
 *
 * Priority order: Channel 0 > 1 > 2 > 3 (first channel with int_pending=true
 * and iei=true and ius=false is acknowledged)
 *
 * @return 8-bit interrupt vector, or 0xFF if no valid interrupt
 */
uint8_t Z80CTC::getVector() const {
    for (int i = 0; i < 4; i++) {
        if (ch_[i].int_pending && ch_[i].iei && !ch_[i].ius) {
            ch_[i].int_pending = false;  // Clear pending
            ch_[i].ius = true;           // Mark as under service
            return vec_base_ | static_cast<uint8_t>(i << 1);
        }
    }
    return 0xFF;
}

/**
 * @brief Handle RETI (Return from Interrupt) instruction notification.
 *
 * The Z80 RETI instruction signals to peripherals that interrupt service
 * has completed. The CTC responds by clearing the IUS (Interrupt Under Service)
 * flag of the channel that was being serviced, allowing lower-priority
 * interrupts to be acknowledged.
 *
 * Only the highest-priority channel with IEI=true and IUS=true is cleared,
 * following the daisy-chain priority: Channel 0 > 1 > 2 > 3.
 *
 * This is crucial for proper nested interrupt handling - without RETI detection,
 * a lower-priority channel cannot interrupt while a higher-priority channel
 * is being serviced.
 */
void Z80CTC::onRETI() {
    // Clear IUS flag for the channel that was being serviced
    // Only the channel with IEI=true and IUS=true should be cleared
    for (int i = 0; i < 4; i++) {
        if (ch_[i].iei && ch_[i].ius) {
            ch_[i].ius = false;
            return;  // Only one channel can be under service
        }
    }
}

/**
 * @brief Get the current down-counter value of a specific channel.
 *
 * This method provides direct read access to the channel's counter state
 * without performing an I/O operation. Useful for debugging and monitoring.
 *
 * @param channel Channel number (0-3)
 * @return Current counter value (1-256, where 256 is represented as 0)
 */
int Z80CTC::getCount(int channel) const {
    return ch_[channel].counter;
}

/**
 * @brief Check if a channel is configured in Timer mode.
 *
 * Returns the operating mode of the specified channel:
 * - Timer mode (false in CTRL_COUNTER bit): Divides system clock by
 *   prescaler (16 or 256) and time constant
 * - Counter mode (true in CTRL_COUNTER bit): Counts external clock/trigger
 *   pulses on the CLK/TRG input
 *
 * @param channel Channel number (0-3)
 * @return true if Timer mode, false if Counter mode
 */
bool Z80CTC::isTimerMode(int channel) const {
    return (ch_[channel].control & CTRL_COUNTER) == 0;
}

Z80CTC::DebugState Z80CTC::debugState() const {
    DebugState d;
    d.vecBase = vec_base_;
    d.iei     = iei_;
    d.ieo     = getIEO();
    for (int i = 0; i < 4; ++i) {
        d.ch[i].control    = ch_[i].control;
        d.ch[i].timeConst  = ch_[i].timeConst;
        d.ch[i].counter    = ch_[i].counter;
        d.ch[i].running    = ch_[i].running;
        d.ch[i].intEn      = (ch_[i].control & CTRL_INT_EN) != 0;
        d.ch[i].intPending = ch_[i].int_pending;
        d.ch[i].ius        = ch_[i].ius;
        d.ch[i].iei        = ch_[i].iei;
    }
    return d;
}
