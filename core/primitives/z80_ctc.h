/**
 * @file z80_ctc.h
 * @brief Z80 Counter/Timer Circuit (CTC) Chip Emulation - Header File
 *
 * Complete emulation of the Zilog Z8430 Counter/Timer Circuit (CTC) as used in
 * the Robotron A5120 office computer. The Z80 CTC is a programmable peripheral
 * providing four independent channels for timing, counting, and interrupt generation.
 *
 * Key Features:
 * - Four independent 8-bit down-counters
 * - Each channel operates in either Timer or Counter mode
 * - Timer mode: Divides system clock by prescaler (16 or 256) then by time constant
 * - Counter mode: Counts external clock/trigger pulses (rising or falling edge)
 * - Zero Count/Timeout (ZC/TO) output pulse generation
 * - Programmable interrupt generation with vector support
 * - Internal daisy-chain priority: Channel 0 > 1 > 2 > 3
 * - RETI instruction detection for nested interrupt support
 *
 * Programming Model:
 * Each channel has a control register and time constant register accessed via
 * consecutive I/O ports (typically base+0 through base+3). Control word format:
 * - D7: Interrupt enable
 * - D6: Mode (0=Timer, 1=Counter)
 * - D5: Prescaler (0=16, 1=256) for Timer mode
 * - D4: Trigger edge (0=falling, 1=rising) for Counter mode
 * - D3: Time constant follows / Trigger mode
 * - D2: Time constant follows this control word
 * - D1: Software reset
 * - D0: Interrupt vector (channel 0 only) / Control word flag
 *
 * Typical Usage:
 * @code
 *   Z80CTC ctc("CTC");
 *   bus.registerIO(&ctc, 0x00, 4);  // Map to ports 0x00-0x03
 *
 *   // Configure channel 0 as timer with interrupts
 *   ctc.ioWrite(0, 0xE5);  // INT enabled, Timer mode, prescaler=16, TC follows
 *   ctc.ioWrite(0, 100);   // Time constant = 100
 *
 *   // In main loop
 *   ctc.clockTick();       // Advance timer state
 *   if (ctc.hasInterrupt()) {
 *       uint8_t vector = ctc.getVector();  // Get interrupt vector
 *       cpu.interrupt(vector);
 *   }
 * @endcode
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8430 CTC Technical Manual
 * @see Robotron A5120 Technical Documentation (U857D Manual, pages 612-616)
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <functional>
#include <string>

/**
 * @class Z80CTC
 * @brief Emulation of the Z80 Counter/Timer Circuit (CTC) chip.
 * 
 * The CTC provides four clock/timer channels used for baud rate generation,
 * system timers, and event counting.
 * 
 * Daisy-chain interrupt priority: Channel 0 > Channel 1 > Channel 2 > Channel 3.
 */
class Z80CTC : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Create a Z80CTC instance.
     * @param name Human-readable name for debugging.
     */
    explicit Z80CTC(const std::string& name = "CTC");

    // ─── BusDevice interface (I/O port access) ────────────────────────────
    
    /**
     * @brief Handle I/O read (Counter balance or vector).
     * @param port Relative port (0-3 for channels)
     * @return Current down-counter value
     */
    uint8_t ioRead(uint8_t port) override;
    
    /**
     * @brief Handle I/O write (Control word or time constant).
     * @param port Relative port (0-3 for channels)
     * @param data Byte to write
     */
    void    ioWrite(uint8_t port, uint8_t data) override;

    // ─── InterruptSlave interface (daisy-chain) ──────────────────────────
    
    /**
     * @brief Set Interrupt Enable Input (/IEI).
     * @param iei true if interrupt allowed from upstream.
     */
    void    setIEI(bool iei) override;
    
    /**
     * @brief Get Interrupt Enable Output (/IEO).
     * @return true if interrupts can be passed downstream.
     */
    bool    getIEO() const override;
    
    /**
     * @brief Check if any channel has a pending interrupt.
     * @return true if requesting interrupt.
     */
    bool    hasInterrupt() const override;
    
    /**
     * @brief Get the interrupt vector for the highest priority requesting channel.
     *
     * Note: This clears the pending interrupt for the acknowledged channel.
     * RETI detection is not implemented, so nested interrupts are not fully supported.
     *
     * @return 8-bit vector.
     */
    uint8_t getVector() const override;

    /**
     * @brief Notify CTC that RETI was executed.
     *
     * Clears the Interrupt Under Service (IUS) flag for the channel
     * that was being serviced, allowing lower-priority interrupts.
     */
    void    onRETI() override;

    /**
     * @brief Advance the internal timer counters (system clock).
     * Call this at system clock frequency.
     */
    void    clockTick();
    
    /**
     * @brief Trigger an external clock/trigger event for a channel.
     * @param channel Channel index (0-3)
     * @param rising_edge true if pulse is a rising edge
     */
    void    clkTrg(int channel, bool rising_edge);

    /**
     * @brief Callback type for Zero Count/Timeout events.
     */
    using ZCTOCallback = std::function<void(int channel, bool level)>;
    
    /**
     * @brief Set global callback for all ZC/TO output pulses.
     * @param cb Callback function.
     */
    void    setZCTOCallback(ZCTOCallback cb);

    /**
     * @brief Get the current down-counter value of a channel.
     * @param channel Channel index (0-3)
     * @return Counter value
     */
    int     getCount(int channel) const;
    
    /**
     * @brief Check if a channel is in Timer mode (vs. Counter mode).
     * @param channel Channel index (0-3)
     * @return true if Timer mode
     */
    bool    isTimerMode(int channel) const;

    /**
     * @brief Return the name of this device.
     * @return Device name for debugging.
     */
    const char* deviceName() const override { return name_.c_str(); }

private:
    // Control Word Bit Masks (U857D manual page 612-613, Bild 3)
    // D0 = 1 (always for control word)
    static constexpr uint8_t CTRL_RESET       = 0x02; ///< D1: Software reset
    static constexpr uint8_t CTRL_TC_FOLLOWS  = 0x04; ///< D2: Time Constant follows control word
    static constexpr uint8_t CTRL_CLK_TRG     = 0x08; ///< D3: Timer trigger mode (0=passive, 1=C/TRG active)
    static constexpr uint8_t CTRL_EDGE_RISE   = 0x10; ///< D4: Trigger edge (0=falling, 1=rising)
    static constexpr uint8_t CTRL_PRESCALE256 = 0x20; ///< D5: Prescaler (0=16, 1=256)
    static constexpr uint8_t CTRL_COUNTER     = 0x40; ///< D6: Mode (0=Timer, 1=Counter)
    static constexpr uint8_t CTRL_INT_EN      = 0x80; ///< D7: Interrupt enabled

    /**
     * @struct Channel
     * @brief Internal state for one CTC channel.
     */
    struct Channel {
        uint8_t control     = CTRL_RESET; ///< Current control register
        uint8_t timeConst   = 0;          ///< Decoded time constant
        int     counter     = 0;          ///< Current down-counter value
        int     prescaleCnt = 0;          ///< Internal prescaler
        bool    awaiting_tc = false;      ///< Waiting for next write as TC
        bool    running     = false;      ///< Timer is active
        bool    zcto_active = false;      ///< ZC/TO line state
        bool    int_pending = false;      ///< Interrupt is waiting for ack
        bool    ius         = false;      ///< Interrupt Under Service flag
        bool    iei         = false;      ///< Interrupt Enable Input (daisy chain)
    };

    /**
     * @brief Helper to fire ZC/TO pulse and interrupt.
     * @param ch Channel index.
     */
    void    fireZCTO(int ch);
    
    /**
     * @brief Check if any channel has an interrupt pending.
     * @return true if any int_pending is set.
     */
    bool    anyPending() const;

    /**
     * @brief Check if any channel can be vectored now (pending, iei, not under service).
     * @return true if a channel would yield a valid vector via getVector().
     */
    bool    anyServiceable() const;

    mutable Channel ch_[4];            ///< Four CTC channels (mutable for getVector)
    uint8_t     vec_base_ = 0;         ///< Interrupt vector base address
    bool        iei_      = false;     ///< Interrupt Enable Input
    std::string name_;                 ///< Device name
    ZCTOCallback zcto_cb_;             ///< Output pulse callback
};
