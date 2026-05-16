/**
 * @file z80_sio.h
 * @brief Z80 Serial Input/Output (SIO) chip emulation.
 * 
 * The Z80 SIO (Zilog Z8440) is a dual-channel serial communication interface
 * that supports a wide range of serial protocols (Asynchronous, Bisync, SDLC).
 * 
 * Each channel (A and B) features:
 * - Full-duplex operation
 * - Separate receiver and transmitter buffers
 * - Modem control signals (RTS, CTS, DCD)
 * - Interrupt support with daisy-chain priority
 * 
 * The SIO is typically accessed via 4 I/O ports:
 * - Base+0: Channel A Data
 * - Base+1: Channel A Control/Status
 * - Base+2: Channel B Data
 * - Base+3: Channel B Control/Status
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

/**
 * @class Z80SIO
 * @brief Emulation of the Z80 Serial Input/Output (SIO) chip.
 * 
 * Features:
 * - Two independent full-duplex channels (A, B)
 * - Interrupt daisy-chaining (IEI/IEO)
 * - Status and control registers per channel
 * - Basic asynchronous serial support
 */
class Z80SIO : public BusDevice, public InterruptSlave {
public:
    /**
     * @struct Channel
     * @brief Represents one of the two SIO serial channels.
     */
    struct Channel {
        /**
         * @brief Receive a byte into the RX FIFO.
         * @param byte The received data byte.
         */
        void    rxByte(uint8_t byte);
        
        /**
         * @brief Check if transmitter is ready to accept a byte.
         * @return true if TX buffer is empty.
         */
        bool    txAvailable() const;
        
        /**
         * @brief Retrieve the byte written to the TX buffer.
         * @return The data byte to be transmitted.
         */
        uint8_t txGet();
        
        /**
         * @brief Check if the RX FIFO contains data.
         * @return true if data is available for reading.
         */
        bool    rxFull() const;

        /**
         * @brief Set a callback for clock transitions.
         * @param clk_callback Function to call on clock events.
         */
        void setClockSource(std::function<void()> clk_callback);
        
        /**
         * @brief Set the Request to Send (RTS) modem signal.
         * @param rts Status of the RTS line.
         */
        void setRTS(bool rts);
        
        /**
         * @brief Get the Clear to Send (CTS) modem signal status.
         * @return Current state of CTS.
         */
        bool getCTS() const;

        // Internal state (public for Z80SIO access)
        uint8_t wr[8]  = {};    ///< Write registers 0-7
        uint8_t rr0    = 0x04;  ///< Read register 0 (Status): TxEmpty set by default
        uint8_t rr1    = 0x01;  ///< Read register 1 (Special Receive Condition): AllSent set by default
        uint8_t reg_ptr = 0;    ///< Pointer to current write register

        std::deque<uint8_t>      rx_fifo; ///< Receive FIFO buffer
        std::optional<uint8_t>   tx_buf;  ///< Transmit buffer

        bool cts_    = false;   ///< Internal CTS state
        bool rts_    = false;   ///< Internal RTS state

        // Interrupt sources
        bool irq_rx  = false;   ///< Receive interrupt pending
        bool irq_tx  = false;   ///< Transmit interrupt pending
        bool irq_ext = false;   ///< External/Status interrupt pending

        std::function<void()> clk_cb_; ///< Clock callback function

        /**
         * @brief Reset the channel state.
         */
        void reset();
        
        /**
         * @brief Update status register RR0 based on internal state.
         */
        void updateRR0();
        
        /**
         * @brief Check if receive interrupts are enabled.
         * @return true if enabled.
         */
        bool rxIntEnabled() const;
        
        /**
         * @brief Check if transmit interrupts are enabled.
         * @return true if enabled.
         */
        bool txIntEnabled() const;
    };

    /**
     * @brief Create a Z80SIO instance.
     * @param name Human-readable name (e.g., "SIO-A").
     */
    explicit Z80SIO(const std::string& name = "SIO");

    // ─── BusDevice interface (I/O port access) ────────────────────────────
    
    /**
     * @brief Handle I/O read access.
     * @param port Relative port (0=A Data, 1=A Control, 2=B Data, 3=B Control)
     * @return Data byte from the SIO
     */
    uint8_t ioRead(uint8_t port) override;
    
    /**
     * @brief Handle I/O write access.
     * @param port Relative port (0=A Data, 1=A Control, 2=B Data, 3=B Control)
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
     * @brief Check if the SIO has a pending interrupt.
     * @return true if requesting an interrupt.
     */
    bool    hasInterrupt() const override;
    
    /**
     * @brief Get the interrupt vector from the SIO.
     * @return 8-bit interrupt vector.
     */
    uint8_t getVector() const override;

    /**
     * @brief Access Channel A.
     * @return Reference to Channel A.
     */
    Channel& channelA() { return ch_a_; }
    
    /**
     * @brief Access Channel B.
     * @return Reference to Channel B.
     */
    Channel& channelB() { return ch_b_; }

    /**
     * @brief Return the name of this device.
     * @return Device name for debugging.
     */
    const char* deviceName() const override { return name_.c_str(); }

private:
    Channel     ch_a_;          ///< SIO Channel A
    Channel     ch_b_;          ///< SIO Channel B
    std::string name_;          ///< Device name
    bool        iei_ = false;   ///< Current IEI line state

    /**
     * @brief Internal write handler for control registers.
     * @param ch Channel object
     * @param data Command/Register data byte
     * @param is_b true if writing to channel B (affects vector handling)
     */
    void writeControl(Channel& ch, uint8_t data, bool is_b);
    
    /**
     * @brief Internal read handler for status registers.
     * @param ch Channel object
     * @param is_b true if reading from channel B
     * @return Status byte
     */
    uint8_t readControl(Channel& ch, bool is_b) const;

    /**
     * @brief Check if a specific channel has an active interrupt request.
     * @param ch Channel reference
     * @return true if channel has pending interrupt
     */
    bool channelHasInterrupt(const Channel& ch) const;
};
