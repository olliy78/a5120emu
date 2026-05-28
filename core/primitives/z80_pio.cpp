/**
 * @file z80_pio.cpp
 * @brief Z80 Parallel Input/Output (PIO) Chip Emulation - Implementation
 *
 * Contains the complete implementation of the Z80 PIO (Parallel Input/Output)
 * controller emulation for the Robotron A5120 office computer. Implements all
 * four operating modes, interrupt generation logic, and handshake protocols.
 *
 * The implementation is organized into the following sections:
 * - Initialization and configuration
 * - Control register programming (mode selection, interrupt configuration)
 * - Data port access (CPU read/write operations)
 * - External port interface (connection to peripheral devices)
 * - Interrupt logic (condition checking, vector generation, daisy-chain)
 * - Handshake strobe processing (ASTB, BSTB)
 *
 * Operating Modes:
 * - Mode 0 (Output): CPU writes data to output latch, drives external pins
 * - Mode 1 (Input): External device writes to input latch, CPU reads
 * - Mode 2 (Bidirectional): Port A only, data flows both ways with handshake
 * - Mode 3 (Bit Control): Each bit independently configured as input or output
 *
 * Interrupt Generation:
 * Interrupts can be generated based on input port conditions:
 * - AND mode: All unmasked bits must match the active level
 * - OR mode: Any unmasked bit matching the active level triggers interrupt
 * - Active level: High (1) or Low (0) selectable per port
 * - Mask register: 0=participate in interrupt, 1=masked out (ignored)
 *
 * Implementation Notes:
 * - Port A has priority over Port B in the internal daisy-chain
 * - Mode 2 on Port A forces Port B into Mode 3
 * - Interrupt mask logic: bit=0 enables, bit=1 masks (per U855D manual p.609)
 * - Control register uses multi-byte protocol for complex configurations
 * - ASTB/BSTB strobes are falling-edge triggered for data ready indication
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8420 PIO Technical Manual
 * @see Robotron A5120 Technical Documentation (U855D Manual, pages 608-611)
 */
#include "z80_pio.h"

// =============================================================================
/// @name Initialization
// =============================================================================

/**
 * @brief Constructor - creates a PIO instance with the given name.
 * @param name Human-readable name for debugging purposes
 */
Z80PIO::Z80PIO(const std::string& name) : name_(name) {}

// =============================================================================
/// @name Control Register Programming
// =============================================================================

/**
 * @brief Process control word write to configure port operation.
 *
 * The Z80 PIO uses a multi-byte protocol for configuration. Control words
 * are identified by their lower 4 bits, and some control words require
 * additional data bytes to complete the configuration.
 *
 * Control Word Types:
 * - **D0=0**: Interrupt vector byte (8-bit vector address)
 * - **D[3:0]=1111 (0x0F)**: Mode control word
 *   - D[7:6]: Operating mode (0=Output, 1=Input, 2=Bidir, 3=BitCtl)
 *   - Mode 3 requires a follow-up direction mask byte
 * - **D[3:0]=0111 (0x07)**: Interrupt control word (full)
 *   - D7: Interrupt Enable (IE)
 *   - D6: AND (1) or OR (0) logic
 *   - D5: Active High (1) or Active Low (0)
 *   - D4: Mask Follows flag (1=next byte is interrupt mask)
 * - **D[3:0]=0011 (0x03)**: Simplified interrupt control (no mask)
 *   - D7: IE, D6: AND/OR, D5: H/L
 *
 * State Machine:
 * - IDLE: Ready for any control word
 * - EXPECT_DIRECTION: Waiting for direction mask (after mode 3 selection)
 * - EXPECT_MASK: Waiting for interrupt mask (after ICW with MaskFollows=1)
 *
 * @param p Port structure to configure (porta_ or portb_)
 * @param data Control byte or data byte
 */
void Z80PIO::writeCtrl(Port& p, uint8_t data) {
    if (p.ctrl_state == CtrlState::EXPECT_DIRECTION) {
        p.direction  = data;
        p.ctrl_state = CtrlState::IDLE;
        return;
    }
    if (p.ctrl_state == CtrlState::EXPECT_MASK) {
        p.int_mask   = data;
        p.ctrl_state = CtrlState::IDLE;
        return;
    }

    if ((data & 0x01) == 0) {
        p.int_vector = data;
        return;
    }
    if ((data & 0x0F) == 0x0F) {
        p.mode = (data >> 6) & 0x03;
        if (p.mode == 3)
            p.ctrl_state = CtrlState::EXPECT_DIRECTION;
        return;
    }
    if ((data & 0x0F) == 0x07) {
        p.ie               = (data >> 7) & 1;
        p.int_and          = (data >> 6) & 1;
        p.int_active_high  = (data >> 5) & 1;
        if ((data >> 4) & 1)
            p.ctrl_state = CtrlState::EXPECT_MASK;
        return;
    }
    if ((data & 0x0F) == 0x03) {
        // Simplified interrupt control word: IE=bit7, AND/OR=bit6, H/L=bit5
        p.ie               = (data >> 7) & 1;
        p.int_and          = (data >> 6) & 1;
        p.int_active_high  = (data >> 5) & 1;
        return;
    }
}

// =============================================================================
/// @name Data Port Access - CPU Operations
// =============================================================================

/**
 * @brief Read data from port as seen by the CPU (IN instruction).
 *
 * Return value depends on the current operating mode:
 * - **Mode 0 (Output)**: Returns the output latch value (CPU-written data)
 * - **Mode 1 (Input)**: Returns the input latch (externally-written data)
 * - **Mode 2 (Bidirectional)**: Returns the input latch
 * - **Mode 3 (Bit Control)**: Returns mixed data:
 *   - Input-configured bits: from input latch
 *   - Output-configured bits: from output latch
 *
 * @param p Port to read (porta_ or portb_)
 * @return 8-bit data value for CPU IN instruction
 */
uint8_t Z80PIO::readDataCPU(const Port& p) const {
    switch (p.mode) {
        case 0: return p.output_latch;
        case 1: return p.input_latch;
        case 2: return p.input_latch;
        case 3: return (p.input_latch  &  p.direction)
                     | (p.output_latch & ~p.direction);
        default: return 0xFF;
    }
}

// =============================================================================
/// @name External Pin State
// =============================================================================

/**
 * @brief Read the actual state driven onto external pins by the PIO.
 *
 * This returns what the PIO is actively driving on its output pins,
 * which differs from CPU-visible data in input modes:
 * - **Mode 0 (Output)**: Returns output latch (PIO drives data out)
 * - **Mode 1 (Input)**: Returns 0xFF (PIO drives nothing, pins are inputs)
 * - **Mode 2 (Bidirectional)**: Returns output latch (PIO drives data out)
 * - **Mode 3 (Bit Control)**: Returns output latch masked by direction
 *   - Only bits configured as outputs are driven
 *   - Input-configured bits return 0xFF (high-impedance)
 *
 * Used by external devices to read the PIO's output state.
 *
 * @param p Port to read (porta_ or portb_)
 * @return 8-bit value driven onto external pins
 */
uint8_t Z80PIO::readPins(const Port& p) const {
    switch (p.mode) {
        case 0: return p.output_latch;
        case 1: return 0xFF;                           // pins are inputs, PIO drives nothing
        case 2: return p.output_latch;
        case 3: return p.output_latch & ~p.direction;  // only output-configured bits
        default: return 0xFF;
    }
}

/**
 * @brief Handle CPU write to data port (OUT instruction).
 *
 * Stores the written value in the output latch and invokes the port output
 * callback if registered. Behavior depends on operating mode:
 * - **Mode 0 (Output)**: Data is stored and driven to external pins
 * - **Mode 1 (Input)**: Data is stored in the output latch but NOT driven to
 *   external pins and the callback is NOT invoked. The latch retains the value
 *   so that when the port later switches to Mode 0 or Mode 3, the pre-loaded
 *   output values take effect immediately. (Real Z80 PIO behaviour.)
 * - **Mode 2 (Bidirectional)**: Data is stored and driven to external pins
 * - **Mode 3 (Bit Control)**: Data is stored; only output-configured bits
 *   are driven to external pins
 *
 * The output callback allows external logic (e.g., keyboard matrix scanner)
 * to respond immediately to CPU output changes.
 *
 * @param p Port to write (porta_ or portb_)
 * @param data 8-bit value from CPU OUT instruction
 * @param cb Output callback to invoke (if set)
 */
void Z80PIO::writeDataCPU(Port& p, uint8_t data, PortCallback& cb) {
    p.output_latch = data;
    if (p.mode == 1) return;   // input mode: latch updated but outputs not driven
    if (cb) cb(data);
}

// =============================================================================
/// @name Interrupt Logic
// =============================================================================

/**
 * @brief Evaluate interrupt condition based on port configuration and input.
 *
 * This method determines whether an interrupt should be triggered based on
 * the current port mode, interrupt enable flag, and input state.
 *
 * Interrupt Generation by Mode:
 * - **Mode 1 (Input)** or **Mode 2 (Bidirectional)**: Interrupt triggered
 *   immediately when IE is set (simple data-ready interrupt)
 * - **Mode 3 (Bit Control)**: Complex logic using mask and AND/OR combination:
 *   - Interrupt mask: bit=0 means participate, bit=1 means masked out
 *   - Unmasked bits are tested against active level (H or L)
 *   - AND logic: ALL unmasked bits must match active level
 *   - OR logic: ANY unmasked bit matching active level triggers interrupt
 *
 * Example (Mode 3, OR logic, Active High, Mask=0xF0):
 * - Only lower 4 bits (mask=0) participate
 * - If any of bits 0-3 is high, interrupt is triggered
 *
 * Note: Per U855D manual page 609, mask bit MB_n=0 means "channel participates
 * in interrupt generation", opposite of typical masking convention.
 *
 * @param p Port to check (porta_ or portb_)
 * @param new_input Current input state from external device
 */
void Z80PIO::checkInterrupt(Port& p, uint8_t new_input) {
    if (!p.ie) return;
    if (p.mode == 1 || p.mode == 2) {
        p.pending = true;
        return;
    }
    if (p.mode == 3) {
        // Mask logic: int_mask bit=0 means "participate", bit=1 means "masked out"
        // According to U855D manual page 609: "MB_n = 0 → Kanalanschluß wird zur Interrupterzeugung herangezogen"
        uint8_t unmasked_bits = ~p.int_mask;
        uint8_t active_input = new_input & unmasked_bits;
        bool triggered;
        if (p.int_and) {
            // AND: all unmasked bits must match active level
            uint8_t expected = p.int_active_high ? unmasked_bits : 0;
            triggered = (active_input == expected);
        } else {
            // OR: at least one unmasked bit must match active level
            triggered = p.int_active_high ? (active_input != 0) : (active_input != unmasked_bits);
        }
        if (triggered) p.pending = true;
    }
}

// =============================================================================
/// @name BusDevice Interface - I/O Port Access
// =============================================================================

/**
 * @brief Handle CPU I/O read (IN instruction) from PIO ports.
 *
 * Reads from the four PIO ports:
 * - Port 0: Port A Data - returns current port A data (see readDataCPU)
 * - Port 1: Port A Control - returns 0xFF (status register not implemented)
 * - Port 2: Port B Data - returns current port B data
 * - Port 3: Port B Control - returns 0xFF (status register not implemented)
 *
 * @param port Relative port number (0-3)
 * @return Data byte read from the specified port
 */
uint8_t Z80PIO::ioRead(uint8_t port) {
    switch (port & 0x03) {
        case 0: return readDataCPU(porta_);
        case 1: return 0xFF;   // control port read: status not implemented
        case 2: return readDataCPU(portb_);
        case 3: return 0xFF;
        default: return 0xFF;
    }
}

/**
 * @brief Handle CPU I/O write (OUT instruction) to PIO ports.
 *
 * Writes to the four PIO ports:
 * - Port 0: Port A Data - writes to output latch, invokes callback
 * - Port 1: Port A Control - programs port configuration
 * - Port 2: Port B Data - writes to output latch, invokes callback
 * - Port 3: Port B Control - programs port configuration
 *
 * Mode 2 Restriction: When Port A is set to Mode 2 (Bidirectional),
 * Port B is automatically forced to Mode 3 (Bit Control) because
 * Port B's control signals are used for Port A's handshake protocol.
 *
 * @param port Relative port number (0-3)
 * @param data Data byte to write
 */
void Z80PIO::ioWrite(uint8_t port, uint8_t data) {
    switch (port & 0x03) {
        case 0: writeDataCPU(porta_, data, cb_a_); break;
        case 1:
            writeCtrl(porta_, data);
            // Mode 2 restriction: when Port A is set to Mode 2, Port B must be in Mode 3
            if (porta_.mode == 2 && portb_.mode != 3) {
                portb_.mode = 3;  // Force Port B to Mode 3
            }
            break;
        case 2: writeDataCPU(portb_, data, cb_b_); break;
        case 3:
            writeCtrl(portb_, data);
            // Mode 2 restriction: when Port A is in Mode 2, Port B must be in Mode 3
            if (porta_.mode == 2 && portb_.mode != 3) {
                portb_.mode = 3;  // Force Port B to Mode 3
            }
            break;
    }
}

// =============================================================================
/// @name InterruptSlave Interface - Daisy Chain
// =============================================================================

/**
 * @brief Set the Interrupt Enable Input (IEI) signal for daisy-chain operation.
 *
 * The IEI signal controls whether this PIO can request interrupts. In the
 * internal daisy chain, Port A has higher priority than Port B.
 *
 * IEI Propagation:
 * - External IEI connects to Port A
 * - Port A's internal IEO connects to Port B's IEI
 * - Port B is blocked if Port A has pending interrupt or is being serviced
 *
 * This implements the two-level priority: Port A > Port B.
 *
 * @param v IEI signal state (true = interrupts enabled from upstream)
 */
void Z80PIO::setIEI(bool v) {
    porta_.iei = v;
    // Propagate IEI to Port B only when Port A has no pending interrupt or service
    portb_.iei = v && !(porta_.pending || porta_.ius);
}

/**
 * @brief Get the Interrupt Enable Output (IEO) signal for daisy-chain operation.
 *
 * IEO indicates whether interrupts can be passed to the next device in the
 * daisy-chain. The PIO blocks IEO (returns false) if either Port A or Port B
 * has a pending interrupt.
 *
 * @return true if interrupts can pass to downstream devices, false if blocked
 */
bool Z80PIO::getIEO() const {
    if (porta_.iei && porta_.pending) return false;
    if (portb_.iei && portb_.pending) return false;
    return porta_.iei;
}

/**
 * @brief Check if the PIO is currently requesting an interrupt.
 *
 * Returns true if either Port A or Port B has a pending interrupt AND
 * its corresponding IEI is asserted (allowing it to interrupt).
 *
 * Priority: Port A is checked first, followed by Port B.
 *
 * @return true if requesting interrupt, false otherwise
 */
bool Z80PIO::hasInterrupt() const {
    return (porta_.iei && porta_.pending) || (portb_.iei && portb_.pending);
}

/**
 * @brief Get the interrupt vector for the highest-priority requesting port.
 *
 * This method is called during interrupt acknowledge cycle to obtain the
 * vector address. Priority order: Port A > Port B.
 *
 * Side effects (per Z80 interrupt acknowledge protocol):
 * 1. Clears the pending flag for the acknowledged port
 * 2. Sets the IUS (Interrupt Under Service) flag for that port
 * 3. This blocks lower-priority interrupts until RETI is executed
 *
 * @return 8-bit interrupt vector for the interrupting port, or 0xFF if none
 */
uint8_t Z80PIO::getVector() const {
    if (porta_.iei && porta_.pending && !porta_.ius) {
        porta_.pending = false;
        porta_.ius = true;
        return porta_.int_vector;
    }
    if (portb_.iei && portb_.pending && !portb_.ius) {
        portb_.pending = false;
        portb_.ius = true;
        return portb_.int_vector;
    }
    return 0xFF;
}

/**
 * @brief Handle RETI (Return from Interrupt) instruction notification.
 *
 * The Z80 RETI instruction signals to peripherals that interrupt service
 * has completed. The PIO responds by clearing the IUS (Interrupt Under Service)
 * flag of the port that was being serviced.
 *
 * Priority: Port A is checked first (higher priority), then Port B.
 * Only the port with IEI=true and IUS=true has its IUS flag cleared.
 *
 * This is crucial for nested interrupt handling - without RETI detection,
 * Port B cannot interrupt while Port A is being serviced.
 */
void Z80PIO::onRETI() {
    // Clear IUS for the port being serviced
    // Port A has higher priority, check it first
    if (porta_.iei && porta_.ius) {
        porta_.ius = false;
    } else if (portb_.iei && portb_.ius) {
        portb_.ius = false;
    }
}

// =============================================================================
/// @name External Port Interface
// =============================================================================

/**
 * @brief Write data to Port A from an external device.
 *
 * This method is called by external hardware (e.g., keyboard matrix scanner,
 * peripheral device) to supply input data to Port A. The data is latched
 * into the input register and may trigger an interrupt if enabled.
 *
 * Effect by mode:
 * - Mode 0 (Output): No effect (port is output-only)
 * - Mode 1 (Input): Data is latched, interrupt checked
 * - Mode 2 (Bidirectional): Data is latched, interrupt checked
 * - Mode 3 (Bit Control): Data is latched, interrupt checked per mask
 *
 * @param data 8-bit input data from external source
 */
void Z80PIO::portAWrite(uint8_t data) {
    porta_.input_latch = data;
    checkInterrupt(porta_, data);
}

/**
 * @brief Read the current output state of Port A pins.
 *
 * Returns what the PIO is driving on Port A's external pins. This is
 * different from CPU reads (ioRead) in input modes.
 *
 * @return Current state of Port A output pins (see readPins)
 */
uint8_t Z80PIO::portARead() const { return readPins(porta_); }

/**
 * @brief Handle Port A Strobe (ASTB) signal for handshake protocol.
 *
 * The ASTB signal is used in Input and Bidirectional modes to indicate
 * that valid data is available on the port's input pins. The PIO responds
 * to the falling edge of ASTB by triggering an interrupt (if IE is set).
 *
 * Typical handshake sequence:
 * 1. External device places data on Port A pins
 * 2. External device asserts ASTB (high-to-low transition)
 * 3. PIO triggers interrupt to notify CPU
 * 4. CPU reads data via IN instruction
 * 5. External device deasserts ASTB
 *
 * @param asserted true for high level, false for falling edge (data ready)
 */
void Z80PIO::setASTB(bool asserted) {
    // ASTB (Port A Strobe): falling edge triggers interrupt in all modes except Mode 3.
    // In Mode 0 (output), ASTB serves as an acknowledge strobe from the peripheral;
    // the falling edge fires an interrupt if IE is set (e.g. floppy index pulse on /ASTB).
    if (!asserted && porta_.mode != 3) {
        if (porta_.ie) porta_.pending = true;
    }
}

/**
 * @brief Write data to Port B from an external device.
 *
 * Functionally identical to portAWrite(), but operates on Port B.
 * Port B cannot use Mode 2 (Bidirectional) - if Port A is in Mode 2,
 * Port B is forced to Mode 3.
 *
 * @param data 8-bit input data from external source
 * @see portAWrite
 */
void Z80PIO::portBWrite(uint8_t data) {
    portb_.input_latch = data;
    checkInterrupt(portb_, data);
}

/**
 * @brief Read the current output state of Port B pins.
 *
 * Returns what the PIO is driving on Port B's external pins.
 *
 * @return Current state of Port B output pins (see readPins)
 * @see portARead
 */
uint8_t Z80PIO::portBRead() const { return readPins(portb_); }

/**
 * @brief Handle Port B Strobe (BSTB) signal for handshake protocol.
 *
 * Similar to ASTB, but for Port B. Port B supports strobes in Mode 1 (Input)
 * and Mode 3 (Bit Control), but not Mode 2 (Bidirectional) which is Port A only.
 *
 * The falling edge of BSTB triggers an interrupt if IE is enabled.
 *
 * @param asserted true for high level, false for falling edge (data ready)
 * @see setASTB
 */
void Z80PIO::setBSTB(bool asserted) {
    // BSTB (Port B Strobe): signals data ready in Input mode (Port B cannot use Mode 2)
    // Typically falling-edge triggered
    if (!asserted && (portb_.mode == 1 || portb_.mode == 3)) {  // falling edge
        if (portb_.ie) portb_.pending = true;
    }
}

// =============================================================================
/// @name Status Accessors and Configuration
// =============================================================================

/**
 * @brief Check if interrupt generation is enabled for either port.
 *
 * Returns true if at least one port has its IE (Interrupt Enable) flag set
 * in the interrupt control word.
 *
 * @return true if interrupts are enabled on Port A or Port B
 */
bool    Z80PIO::isInterruptEnabled() const { return porta_.ie || portb_.ie; }

/**
 * @brief Get the configured interrupt vector for Port A.
 *
 * Returns the 8-bit vector address programmed into Port A's control register.
 * This is the vector that will be placed on the data bus during interrupt
 * acknowledge for Port A interrupts.
 *
 * @return 8-bit interrupt vector for Port A
 */
uint8_t Z80PIO::getInterruptVector() const { return porta_.int_vector; }

// =============================================================================
/// @name Output Callbacks
// =============================================================================

/**
 * @brief Register a callback function for Port A output changes.
 *
 * The callback is invoked whenever the CPU writes to Port A's data register
 * (via OUT instruction). This allows external logic to respond immediately
 * to output changes without polling.
 *
 * Typical use cases:
 * - Keyboard matrix scanner: Output selects row, callback reads columns
 * - LED drivers: Output data drives display segments
 * - Control signals: Output bits control external hardware
 *
 * @param cb Callback function receiving uint8_t data, or empty to disable
 */
void Z80PIO::setPortAOutputCallback(PortCallback cb) { cb_a_ = std::move(cb); }

/**
 * @brief Register a callback function for Port B output changes.
 *
 * Functionally identical to setPortAOutputCallback(), but for Port B.
 *
 * @param cb Callback function receiving uint8_t data, or empty to disable
 * @see setPortAOutputCallback
 */
void Z80PIO::setPortBOutputCallback(PortCallback cb) { cb_b_ = std::move(cb); }
