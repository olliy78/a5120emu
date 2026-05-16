/**
 * @file z80_ctc.h
 * @brief Z80 Counter/Timer Circuit (CTC) chip emulation.
 * 
 * The Z80 CTC (Zilog Z8430) is a programmable component with four independent
 * channels that can provide counting and timing functions.
 * 
 * Features per channel:
 * - Counter mode: Counts external clock pulses
 * - Timer mode: Divides internal system clock (prescaler 16 or 256)
 * - Interrupt generation on zero/time-out
 * - Zero Count/Timeout (ZC/TO) exit pulse
 * 
 * The CTC is typically accessed via 4 consecutive I/O ports:
 * - Base+0: Channel 0 Control/Time Constant
 * - Base+1: Channel 1 Control/Time Constant
 * - Base+2: Channel 2 Control/Time Constant
 * - Base+3: Channel 3 Control/Time Constant
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
     * @return 8-bit vector.
     */
    uint8_t getVector() const override;

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
    // Control Word Bit Masks
    static constexpr uint8_t CTRL_RESET       = 0x02; ///< Software reset
    static constexpr uint8_t CTRL_TC_FOLLOWS  = 0x04; ///< Time Constant follows control word
    static constexpr uint8_t CTRL_COUNTER     = 0x08; ///< 1=Counter mode, 0=Timer mode
    static constexpr uint8_t CTRL_PRESCALE256 = 0x10; ///< 1=Prescale 256, 0=Prescale 16
    static constexpr uint8_t CTRL_EDGE_RISE   = 0x20; ///< 1=Rising edge trigger, 0=Falling
    static constexpr uint8_t CTRL_CLK_TRG    = 0x40; ///< 1=CLK/TRG pulses start timer, 0=Automatic
    static constexpr uint8_t CTRL_INT_EN     = 0x80; ///< 1=Interrupt enabled

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

    Channel     ch_[4];           ///< Four CTC channels
    uint8_t     vec_base_ = 0;    ///< Interrupt vector base address
    bool        iei_      = false;///< Interrupt Enable Input
    std::string name_;            ///< Device name
    ZCTOCallback zcto_cb_;        ///< Output pulse callback
};
