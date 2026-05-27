/**
 * @file k1520_bus.h
 * @brief K1520 system bus simulator and device interfaces.
 * 
 * The K1520Bus is the central communication layer between all emulated components.
 * It implements the K1520 system bus protocol (TGL 37271/01) and manages:
 * - I/O port access dispatch
 * - Memory access dispatch
 * - Interrupt daisy-chain priority
 * - System signal control (RESET, INT, NMI, WAIT, MEMDI, IODI)
 * 
 * All devices (CPU, cards, peripherals) register with the bus to receive
 * callbacks for their allocated address/port ranges.
 */

#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <functional>
#include <initializer_list>

/**
 * @brief Base class for devices that use I/O ports.
 * 
 * Devices like K2526, K8025, and K5122 communicate with the Z80 CPU via
 * I/O port (IN/OUT instructions). Each device registers itself with the
 * K1520Bus for a range of port addresses.
 */
class BusDevice {
public:
    virtual ~BusDevice() = default;
    
    /**
     * @brief Handle an IN instruction to a port assigned to this device.
     * @param port Relative port number (0 = base port, 1, 2, etc.)
     * @return Data byte to return to CPU (default: 0xFF)
     */
    virtual uint8_t ioRead(uint8_t port)             { return 0xFF; }
    
    /**
     * @brief Handle an OUT instruction to a port assigned to this device.
     * @param port Relative port number
     * @param d Data byte written by CPU
     */
    virtual void    ioWrite(uint8_t port, uint8_t d) {}
    
    /**
     * @brief Return human-readable device name for debugging.
     * @return Device name (e.g., "K2526 ZRE", "K7024 ABS")
     */
    virtual const char* deviceName() const            { return "?"; }
};

/**
 * @brief Base class for memory-mapped devices.
 * 
 * Devices with VRAM or RAM (K3526, K7024) register memory regions with
 * the K1520Bus. The bus dispatches all memory accesses to the appropriate
 * device based on address range.
 */
class MemDevice {
public:
    virtual ~MemDevice() = default;
    
    /**
     * @brief Read one byte from memory address.
     * @param addr 16-bit address (0x0000–0xFFFF)
     * @return Data byte at that address
     */
    virtual uint8_t memRead(uint16_t addr)              = 0;
    
    /**
     * @brief Write one byte to memory address.
     * @param addr 16-bit address
     * @param d Data byte to write
     */
    virtual void    memWrite(uint16_t addr, uint8_t d)  = 0;
    
    /**
     * @brief Check if this memory region is writable.
     * @return true for RAM, false for ROM
     */
    virtual bool    isWritable() const { return true; }

    /**
     * @brief Check if this device drives the data bus during reads.
     *
     * When false (Lesesperre active on K7024), this device's memRead() is
     * never called — a lower-priority device responds instead.  Writes still
     * go to this device when isWritable() returns true.
     *
     * @return true by default; override to false for Lesesperre/read-inhibit
     */
    virtual bool    isReadable() const { return true; }
};

/**
 * @brief Base class for interrupt-enabled devices (daisy-chain support).
 * 
 * Z80-compatible interrupt devices (K2526, K8025, K5122, K7024) support
 * the interrupt daisy-chain mechanism. This allows prioritizing interrupts
 * based on device order in the chain.
 * 
 * The K1520Bus maintains an ordered list of InterruptSlave devices.
 * When INT is asserted, each device in order can either:
 * - Pass through to the next device (setIEI(true) -> getIEO() = true)
 * - Block the chain (setIEI(true) -> getIEO() = false) and supply a vector
 */
class InterruptSlave {
public:
    virtual ~InterruptSlave() = default;
    
    /**
     * @brief Set the Interrupt Enable Input (/IEI) line.
     * 
     * When the bus asserts INT, the first device in the chain gets IEI=true.
     * If this device has no interrupt pending, it should pass through to the
     * next device by returning true from getIEO().
     * 
     * @param iei true when this device is selected by upstream device
     */
    virtual void    setIEI(bool iei)           = 0;
    
    /**
     * @brief Get the Interrupt Enable Output (/IEO) line.
     * 
     * Return true to pass interrupt to next device (this device has no pending int).
     * Return false to block chain (this device will supply interrupt vector).
     * 
     * @return false if this device has pending interrupt, true to pass through
     */
    virtual bool    getIEO() const             = 0;
    
    /**
     * @brief Check if this device has a pending interrupt.
     * @return true if interrupt waiting for acknowledgement
     */
    virtual bool    hasInterrupt() const       = 0;
    
    /**
     * @brief Return interrupt vector (Z80 Mode 2).
     *
     * Called when CPU acknowledges interrupt and this device blocks the chain.
     * Typical value is an 8-bit vector combined with I register.
     *
     * @return Interrupt vector (default 0xFF for open-drain)
     */
    virtual uint8_t getVector() const { return 0xFF; }

    /**
     * @brief Notify device that RETI instruction was executed.
     *
     * Called by the bus when the CPU executes a RETI instruction.
     * Devices should clear their "Interrupt Under Service" (IUS) flag
     * and allow lower-priority interrupts to proceed.
     */
    virtual void onRETI() {}
};


/**
 * @class K1520Bus
 * @brief Central K1520 system bus simulator.
 * 
 * The K1520Bus is the core component that:
 * 1. Routes I/O port accesses to registered devices
 * 2. Routes memory accesses to registered devices
 * 3. Manages interrupt daisy-chain priority
 * 4. Handles system-wide signals (RESET, INT, NMI, WAIT, etc.)
 * 5. Implements /MEMDI and /IODI (bus disable signals)
 * 
 * Thread model: Not thread-safe. All accesses must be from the Z80CPU thread.
 * For thread-safe access from Python, synchronize at the A5120Machine level.
 */
class K1520Bus {
public:
    K1520Bus() = default;
    ~K1520Bus() = default;

    // ─── Device registration ──────────────────────────────────────────────
    
    /**
     * @brief Register an I/O device for a range of port addresses.
     * 
     * Example: registerIO(pioA, 0x08, 4) assigns ports 0x08–0x0B to pioA.
     * The device's ioRead/ioWrite will be called with port = 0–3 respectively.
     * 
     * @param dev Device pointer (lifetime must exceed bus lifetime)
     * @param basePort First port address (0–255)
     * @param numPorts Number of consecutive ports (default 1)
     * @throws std::runtime_error if ports are already registered
     */
    void registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts = 1);
    
    /**
     * @brief Register a memory-mapped device for an address range.
     * 
     * Example: registerMem(&ram, 0x0400, 0xF400) assigns 0x0400–0xF7FF.
     * The device's memRead/memWrite will be called with addr = offset in range.
     * 
     * @param dev Memory device pointer
     * @param base Start address of range (0–0xFFFF)
     * @param size Size of range in bytes
     * @throws std::runtime_error if range overlaps
     */
    void registerMem(MemDevice* dev, uint16_t base, uint16_t size);
    
    /**
     * @brief Unregister a memory device.
     * @param dev Device to unregister
     */
    void unregisterMem(MemDevice* dev);
    
    /**
     * @brief Set the interrupt daisy-chain priority order.
     * 
     * Devices in the list are processed in order when INT is asserted.
     * The first device with a pending interrupt blocks the chain and
     * supplies the interrupt vector.
     * 
     * @param chain List of devices in priority order (highest first)
     */
    void setInterruptChain(std::initializer_list<InterruptSlave*> chain);

    // ─── Bus operations (called by Z80CPU) ────────────────────────────────
    
    /**
     * @brief Perform a memory read access.
     * 
     * Dispatched to the MemDevice containing the address.
     * If /MEMDI is asserted, returns 0xFF (open drain).
     * 
     * @param addr Address to read (0x0000–0xFFFF)
     * @return Data byte at that address
     */
    uint8_t memRead(uint16_t addr);
    
    /**
     * @brief Perform a memory write access.
     * 
     * Dispatched to the MemDevice containing the address.
     * If /MEMDI is asserted, write is blocked (no-op).
     * 
     * @param addr Address to write
     * @param data Byte to write
     */
    void    memWrite(uint16_t addr, uint8_t data);
    
    /**
     * @brief Perform an I/O read (IN instruction).
     * 
     * Dispatched to the BusDevice registered for this port.
     * If /IODI is asserted, returns 0xFF.
     * 
     * @param port Port address (0x00–0xFF)
     * @return Data byte from device
     */
    uint8_t ioRead(uint8_t port);
    
    /**
     * @brief Perform an I/O write (OUT instruction).
     * 
     * Dispatched to the BusDevice registered for this port.
     * If /IODI is asserted, write is blocked.
     * 
     * @param port Port address
     * @param data Byte to write
     */
    void    ioWrite(uint8_t port, uint8_t data);
    
    /**
     * @brief Interrupt acknowledge cycle (CPU's interrupt response).
     * 
     * When Z80 acknowledges an INT with an INTA cycle (/M1 + /IORQ):
     * 1. First device in daisy-chain with pending INT supplies vector
     * 2. That device is marked as acknowledged
     * 3. Vector is returned for Mode 2 interrupt
     * 
     * @return Interrupt vector (8-bit) to use with I register, or 0xFF if none
     */
    uint8_t interruptAcknowledge();

    // ─── Signal control ──────────────────────────────────────────────────
    
    /**
     * @brief Assert the /INT (maskable interrupt) signal.
     * 
     * Requested by any device that needs to interrupt the CPU.
     * The CPU samples /INT on each instruction and updates it next cycle.
     */
    void assertINT();
    
    /**
     * @brief Release the /INT signal.
     * 
     * When no device has a pending interrupt, release INT so CPU can continue.
     */
    void releaseINT();
    
    /**
     * @brief Assert /NMI (non-maskable interrupt) signal.
     *
     * NMI is edge-triggered on the Z80. This is called once per NMI event.
     */
    void assertNMI();

    /**
     * @brief Clear /NMI pending flag after delivery to CPU.
     *
     * Must be called by the run loop immediately after cpu_.nmi() so the
     * NMI is not re-delivered on every subsequent step.
     */
    void clearNMI();

    /**
     * @brief Assert /RESET signal to reset the entire system.
     *
     * CPU returns to address 0x0000 and all devices re-initialize.
     */
    void assertRESET();

    /**
     * @brief Signal all interrupt devices that RETI was executed.
     *
     * Called by the CPU/machine when RETI instruction completes.
     * Propagates onRETI() to all devices in the interrupt chain.
     */
    void signalRETI();

    /**
     * @brief Assert /WAIT to stall the bus for one T-cycle.
     *
     * Used by slow devices to extend timing. Each assertion adds one T-cycle
     * to the current bus access.
     */
    void assertWAIT();

    /**
     * @brief Release /WAIT signal.
     */
    void releaseWAIT();

    /**
     * @brief Assert /BUSRQ – DMA device requests the bus from ZVE1.
     *
     * Called by K5122 when /STR is asserted (start of DMA transfer).
     * ZVE1 (in K2526) will finish its current instruction and then suspend
     * (release the bus). The run loop checks isBUSRQ() each iteration and
     * skips the CPU step while asserted.
     */
    void assertBUSRQ();

    /**
     * @brief Release /BUSRQ – DMA transfer complete, ZVE1 resumes.
     *
     * Called by K5122 after the DMA sector transfer has completed.
     */
    void releaseBUSRQ();

    /**
     * @brief Check whether /BUSRQ is currently asserted by a DMA device.
     * @return true while DMA transfer is in progress (ZVE1 should pause)
     */
    bool isBUSRQ() const { return busrq_asserted_; }
    
    /**
     * @brief Disable memory access via /MEMDI signal (from BS-PIO).
     * 
     * When /MEMDI is asserted by the BS-PIO, memory accesses return 0xFF
     * and writes are blocked. Used during initialization to prevent
     * uncontrolled memory access.
     * 
     * @param disabled true to assert /MEMDI, false to release
     */
    void setMEMDI(bool disabled) { memdi_ = disabled; }
    
    /**
     * @brief Query /MEMDI status.
     * @return true if memory access is currently disabled
     */
    bool getMEMDI() const { return memdi_; }
    
    /**
     * @brief Disable I/O access via /IODI signal.
     * 
     * Similar to /MEMDI but for I/O ports.
     * 
     * @param disabled true to disable I/O access
     */
    void setIODI(bool disabled) { iodi_ = disabled; }

    // ─── Signal status queries ────────────────────────────────────────────
    
    /**
     * @brief Check if /INT is currently asserted.
     * @return true if any device has a pending interrupt
     */
    bool isINT()   const { return int_asserted_; }
    
    /**
     * @brief Check if /NMI is pending.
     * @return true if NMI was asserted since last acknowledgement
     */
    bool isNMI()   const { return nmi_pending_; }
    
    /**
     * @brief Check if /WAIT is asserted.
     * @return true if bus access should be stalled
     */
    bool isWAIT()  const { return wait_asserted_; }
    
    /**
     * @brief Check if /RESET is asserted.
     * @return true if system reset is in progress
     */
    bool isRESET() const { return reset_asserted_; }

    // ─── Debugging and tracing ────────────────────────────────────────────
    
    /**
     * @brief Bus trace callback type.
     * 
     * Called for every bus access (read/write, I/O/memory).
     * Used for debugging and performance analysis.
     * Signature: void callback(bool isIO, bool isRead, uint16_t addr, uint8_t data)
     */
    using BusTrace = std::function<void(bool isIO, bool isRead,
                                        uint16_t addr, uint8_t data)>;
    
    /**
     * @brief Set optional trace callback.
     * 
     * The callback will be invoked (at high frequency) for every bus access
     * if set. Set to nullptr to disable tracing.
     * 
     * @param cb Callback function, or empty std::function to disable
     */
    void setTraceCallback(BusTrace cb) { trace_cb_ = std::move(cb); }
    
    /**
     * @brief Recalculate interrupt chain state.
     * 
     * Called internally after device registration or when interrupt state changes.
     * Can also be called manually to resync after complex operations.
     */
    void updateInterruptChain();


private:
    /// Array of I/O devices indexed by port address
    std::array<BusDevice*, 256> io_map_{};

    /// List of memory regions and their devices (checked in order)
    struct MemRegion { uint16_t base, size; MemDevice* dev; };
    std::vector<MemRegion> mem_regions_;

    /// Interrupt priority chain (first = highest priority)
    std::vector<InterruptSlave*> int_chain_;

    // Signal flags
    bool int_asserted_   = false;  ///< /INT is currently asserted
    bool nmi_pending_    = false;  ///< /NMI edge was detected
    bool wait_asserted_  = false;  ///< /WAIT is asserted (stall access)
    bool reset_asserted_ = false;  ///< /RESET is asserted (system initializing)
    bool memdi_          = false;  ///< /MEMDI: memory access disabled
    bool iodi_           = false;  ///< /IODI: I/O access disabled
    bool busrq_asserted_ = false;  ///< /BUSRQ: DMA device holds the bus (ZVE1 suspended)

    /// Optional trace callback for debugging
    BusTrace trace_cb_;
};

