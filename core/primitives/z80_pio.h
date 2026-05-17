/**
 * @file z80_pio.h
 * @brief Z80 Parallel Input/Output (PIO) Chip Emulation - Header File
 *
 * Complete emulation of the Zilog Z8420 Parallel Input/Output (PIO) controller
 * as used in the Robotron A5120 office computer. The Z80 PIO (DDR designation
 * U 855 D) provides two independent 8-bit parallel I/O ports with programmable
 * operating modes and interrupt generation capabilities.
 *
 * Key Features:
 * - Two independent 8-bit ports (Port A and Port B)
 * - Four operating modes per port:
 *   - Mode 0: Output mode (CPU writes data to external device)
 *   - Mode 1: Input mode (CPU reads data from external device)
 *   - Mode 2: Bidirectional mode (Port A only, with handshake)
 *   - Mode 3: Bit Control mode (per-bit input/output configuration)
 * - Programmable interrupt generation with vectored interrupts
 * - Handshake strobe signals (ASTB for Port A, BSTB for Port B)
 * - Internal daisy-chain priority (Port A > Port B)
 * - Interrupt logic: AND/OR combination of masked input bits
 * - RETI instruction detection for nested interrupt support
 *
 * Port Configuration:
 * The PIO occupies 4 consecutive I/O ports:
 * - Base+0: Port A Data register (read/write)
 * - Base+1: Port A Control register (write only)
 * - Base+2: Port B Data register (read/write)
 * - Base+3: Port B Control register (write only)
 *
 * Control Register Protocol:
 * - D0=0: Sets interrupt vector (8-bit vector address)
 * - D[3:0]=1111 (0x0F): Mode control word (mode in D[7:6])
 * - D[3:0]=0111 (0x07): Interrupt control word (IE, AND/OR, H/L, Mask Follows)
 * - D[3:0]=0011 (0x03): Simplified interrupt control (IE, AND/OR, H/L)
 * - After mode 3 selection: I/O direction mask (1=input, 0=output)
 * - After interrupt control with MaskFollows: Interrupt mask register
 *
 * Mode 2 Restriction:
 * When Port A is configured for Mode 2 (Bidirectional), Port B is automatically
 * forced to Mode 3 (Bit Control) as its control lines are used for Port A handshake.
 *
 * Typical Usage in A5120:
 * - K2526 (BS-PIO): Keyboard and button matrix scanning
 * - K8025 PIO-A/PIO-B: External device interfaces
 * - Parallel printer port interface
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8420 PIO Technical Manual
 * @see Robotron A5120 Technical Documentation (U855D Manual, pages 608-611)
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <functional>
#include <string>

/**
 * @class Z80PIO
 * @brief Emulation of the Z80 Parallel Input/Output (PIO) chip.
 * 
 * Features:
 * - Two independent 8-bit ports (A, B)
 * - Each port: Output, Input, Bidirectional, or Bit-Control mode
 * - Interrupt capability with daisy-chain support
 * - Handshake strobe inputs (ASTB, BSTB)
 * - External port pin access for integration with other devices
 * 
 * The PIO must be registered with the K1520Bus for I/O port access.
 * The bus will call ioRead/ioWrite with relative port numbers 0–3.
 */
class Z80PIO : public BusDevice, public InterruptSlave {
public:
    /**
     * @brief Create a Z80PIO instance.
     * 
     * @param name Human-readable name for debugging (e.g., "BS-PIO", "K8025-PIO-A")
     */
    explicit Z80PIO(const std::string& name = "PIO");

    // ─── BusDevice interface (I/O port access) ────────────────────────────
    
    /**
     * @brief Handle an IN instruction to ports assigned to this PIO.
     * 
     * The K1520Bus dispatches port reads to this function.
     * Ports are numbered 0–3 (relative to base port).
     * 
     * @param port Relative port (0=A data, 1=A ctrl, 2=B data, 3=B ctrl)
     * @return Data byte read from the port
     */
    uint8_t ioRead(uint8_t port) override;
    
    /**
     * @brief Handle an OUT instruction to ports assigned to this PIO.
     * 
     * @param port Relative port (0=A data, 1=A ctrl, 2=B data, 3=B ctrl)
     * @param data Byte written by CPU
     */
    void    ioWrite(uint8_t port, uint8_t data) override;
    
    /**
     * @brief Return the name of this device.
     * @return Device name for debugging
     */
    const char* deviceName() const override { return name_.c_str(); }

    // ─── InterruptSlave interface (daisy-chain) ──────────────────────────
    
    /**
     * @brief Set the Interrupt Enable Input (/IEI) line.
     * 
     * Called by the K1520Bus when another device in the daisy-chain
     * passes the interrupt enable signal to this device.
     * 
     * @param iei true if interrupt enable is asserted upstream
     */
    void    setIEI(bool iei) override;
    
    /**
     * @brief Get the Interrupt Enable Output (/IEO) line.
     * 
     * Return true to pass interrupt to next device (this device has no pending int).
     * Return false to block the chain and signal that this device will provide
     * an interrupt vector.
     * 
     * @return false if this device has pending interrupt, true to pass through
     */
    bool    getIEO() const override;
    
    /**
     * @brief Check if this PIO has a pending interrupt.
     * 
     * @return true if either port has an interrupt condition and IE is enabled
     */
    bool    hasInterrupt() const override;
    
    /**
     * @brief Return the interrupt vector for this PIO (Z80 Mode 2).
     *
     * The vector is configured via the Port Control register.
     *
     * @return 8-bit interrupt vector
     */
    uint8_t getVector() const override;

    /**
     * @brief Notify PIO that RETI was executed.
     *
     * Clears the Interrupt Under Service (IUS) flag for the port
     * that was being serviced, allowing lower-priority interrupts.
     */
    void    onRETI() override;

    // ─── External port access (from other devices or tests) ────────────────
    
    /**
     * @brief Write data to Port A from external source (strobe pulse).
     * 
     * Used when external hardware (e.g., keyboard matrix) provides input data.
     * In Input or Bidirectional mode, this latches the data and may trigger INT.
     * In Output mode, this has no effect (data flows out only).
     * 
     * @param data 8-bit data from external source
     */
    void    portAWrite(uint8_t data);
    
    /**
     * @brief Read the current output of Port A.
     * 
     * In Output or Bidirectional mode, returns the CPU-written output latch.
     * In Input mode, returns the last externally-written input.
     * 
     * @return Current 8-bit data on Port A output pins
     */
    uint8_t portARead() const;
    
    /**
     * @brief Strobe input for Port A handshake.
     * 
     * In Bidirectional mode, a strobe pulse tells the PIO that external
     * data on portA is ready to be read.
     * 
     * @param asserted true for rising edge (data ready), false for falling edge
     */
    void    setASTB(bool asserted);

    /**
     * @brief Write data to Port B from external source.
     * @param data 8-bit data from external source
     * @see portAWrite
     */
    void    portBWrite(uint8_t data);
    
    /**
     * @brief Read the current output of Port B.
     * @return Current 8-bit data on Port B output pins
     * @see portARead
     */
    uint8_t portBRead() const;
    
    /**
     * @brief Strobe input for Port B handshake.
     * @param asserted true for rising edge
     * @see setASTB
     */
    void    setBSTB(bool asserted);

    /**
     * @brief Check if interrupt output is enabled for this PIO.
     * 
     * Returns true if at least one port has interrupt enabled (IE flag set).
     * 
     * @return true if interrupts are enabled
     */
    bool    isInterruptEnabled() const;
    
    /**
     * @brief Get the configured interrupt vector.
     * 
     * Returns the vector set via control register.
     * 
     * @return 8-bit interrupt vector
     */
    uint8_t getInterruptVector() const;

    // ─── Port output callbacks ────────────────────────────────────────────
    
    /**
     * @brief Callback signature when Port A output changes.
     * 
     * Invoked when CPU writes to Port A in Output or Bit-Control mode.
     * Signature: void callback(uint8_t new_data)
     */
    using PortCallback = std::function<void(uint8_t data)>;
    
    /**
     * @brief Set callback for Port A output changes.
     * 
     * The callback is invoked whenever the CPU writes to Port A (ioWrite on port 0).
     * Useful for connecting keyboard/button matrix logic or other external hardware.
     * 
     * @param cb Callback function, or empty std::function to disable
     */
    void    setPortAOutputCallback(PortCallback cb);
    
    /**
     * @brief Set callback for Port B output changes.
     * 
     * @param cb Callback function
     * @see setPortAOutputCallback
     */
    void    setPortBOutputCallback(PortCallback cb);

private:
    /**
     * @brief State machine for control register programming.
     * 
     * The Z80 PIO uses a complex protocol:
     * 1. Write control word with IE=1, Mode, etc.
     * 2. For modes 0–2, write direction/mask (mode 3)
     * 3. Write interrupt vector
     * 4. Write interrupt control mask
     */
    enum class CtrlState {
        IDLE,                 ///< Waiting for control word
        EXPECT_DIRECTION,     ///< Mode 3 selected, waiting for direction mask
        EXPECT_MASK           ///< Waiting for interrupt mask
    };

    /**
     * @brief Per-port state structure.
     * 
     * Tracks operating mode, latches, handshake, and interrupt state.
     */
    struct Port {
        uint8_t   mode          = 1;       ///< 0=Out, 1=In, 2=Bidir, 3=BitCtl
        uint8_t   output_latch  = 0x00;    ///< CPU writes (output to external)
        uint8_t   input_latch   = 0xFF;    ///< External writes (read by CPU)
        uint8_t   direction     = 0xFF;    ///< Mode 3: 1=input, 0=output per bit
        uint8_t   int_vector    = 0xFF;    ///< Interrupt vector from control
        bool      ie            = false;   ///< Interrupt enabled
        bool      int_and       = false;   ///< true=AND logic, false=OR
        bool      int_active_high = false; ///< true=level sensitive (1), false (0)
        uint8_t   int_mask      = 0xFF;    ///< Interrupt enable mask (1=enabled)
        bool      pending       = false;   ///< Interrupt pending
        bool      iei           = false;   ///< Interrupt Enable Input (daisy-chain)
        bool      ius           = false;   ///< Interrupt Under Service flag
        CtrlState ctrl_state    = CtrlState::IDLE;
    };

    /**
     * @brief Process a control word write.
     * 
     * Implements the Z80 PIO control register protocol.
     * 
     * @param p Port to configure (porta_ or portb_)
     * @param data Control word
     */
    void    writeCtrl(Port& p, uint8_t data);
    
    /**
     * @brief Read data as if CPU was reading (use output_latch if output mode).
     * 
     * @param p Port to read
     * @return Data for CPU to read
     */
    uint8_t readDataCPU(const Port& p) const;
    
    /**
     * @brief Read the actual pin state (external input if input mode).
     * 
     * @param p Port to read
     * @return Current pin state
     */
    uint8_t readPins(const Port& p) const;
    
    /**
     * @brief Handle CPU write to data port (apply to output latch).
     * 
     * @param p Port to write
     * @param data Byte from CPU
     * @param cb Callback to invoke if set
     */
    void    writeDataCPU(Port& p, uint8_t data, PortCallback& cb);
    
    /**
     * @brief Re-evaluate interrupt condition for a port.
     * 
     * Called whenever port mode, input, or mask changes.
     * 
     * @param p Port to check
     * @param new_input New external input value (for edge detection)
     */
    void    checkInterrupt(Port& p, uint8_t new_input);

    // State
    mutable Port porta_;        ///< Port A configuration and state (mutable for getVector)
    mutable Port portb_;        ///< Port B configuration and state (mutable for getVector)
    std::string  name_;         ///< Device name for debugging
    PortCallback cb_a_;         ///< Callback for Port A output changes
    PortCallback cb_b_;         ///< Callback for Port B output changes
};

