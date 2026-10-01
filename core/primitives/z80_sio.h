/**
 * @file z80_sio.h
 * @brief Z80 Serial Input/Output (SIO) Chip Emulation - Header File
 *
 * Complete emulation of the Zilog Z8440 Serial Input/Output (SIO) controller
 * as used in the Robotron A5120 office computer. The Z80 SIO (DDR designation
 * U 856 D) provides two independent full-duplex serial communication channels
 * supporting multiple protocols and operating modes.
 *
 * Key Features:
 * - Two independent full-duplex serial channels (Channel A and Channel B)
 * - Multiple operating modes:
 *   - Asynchronous mode: Standard UART with configurable baud rates, data bits,
 *     parity, and stop bits
 *   - Monosync mode: 8-bit synchronous with single sync character
 *   - Bisync mode: 16-bit synchronous with double sync character (BSC protocol)
 *   - SDLC/HDLC mode: Bit-oriented protocol with flag sequences, bit stuffing,
 *     and automatic CRC generation/checking
 *   - External sync mode: Sync signal provided externally
 * - Interrupt generation with vectored interrupts and daisy-chain priority
 * - Separate 3-deep receive FIFO per channel
 * - Modem control signals: RTS, CTS, DCD, DTR
 * - Automatic CRC generation and checking (CRC-16 or CRC-CCITT)
 * - Error detection: Parity, framing, overrun
 * - Internal daisy-chain priority (Channel A > Channel B)
 * - RETI instruction detection for nested interrupt support
 *
 * Port Configuration:
 * The SIO occupies 4 consecutive I/O ports:
 * - Base+0: Channel A Data register (read/write)
 * - Base+1: Channel A Control/Status register (read/write)
 * - Base+2: Channel B Data register (read/write)
 * - Base+3: Channel B Control/Status register (read/write)
 *
 * Register Architecture:
 * - 8 Write Registers (WR0-WR7) per channel for configuration
 * - 3 Read Registers (RR0-RR2) per channel for status and data
 * - WR0: Command register and pointer register selection
 * - WR1: Interrupt and data transfer mode control
 * - WR2: Interrupt vector (Channel B only)
 * - WR3: Receive parameters and control
 * - WR4: Transmit/receive parameters and clock mode
 * - WR5: Transmit parameters and control
 * - WR6: Sync character 1 or SDLC address
 * - WR7: Sync character 2 or SDLC flag
 * - RR0: Transmit/receive buffer status and external status
 * - RR1: Special receive condition status
 * - RR2: Modified interrupt vector (Channel B only)
 *
 * Interrupt Priority (within SIO):
 * 1. Channel A Receive
 * 2. Channel A Transmit
 * 3. Channel A External/Status
 * 4. Channel B Receive
 * 5. Channel B Transmit
 * 6. Channel B External/Status
 *
 * Typical Usage in A5120:
 * - Serial console terminal communication
 * - Printer interface (serial)
 * - Modem communication
 * - Data link protocols (SDLC for networking)
 *
 * @author Olaf Krieger
 * @date 2024-2025
 * @license MIT License
 * @see Zilog Z8440 SIO Technical Manual
 * @see Robotron A5120 Technical Documentation (U856D Manual, pages 754-792)
 */

#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// ─── SIO Operating Modes ─────────────────────────────────────────────────────

/**
 * @enum Mode
 * @brief Operating modes for SIO channels.
 */
enum class SIOMode {
    ASYNC,      ///< Asynchronous mode
    MONOSYNC,   ///< Monosync (8-bit sync)
    BISYNC,     ///< Bisync (16-bit sync)
    SDLC,       ///< SDLC/HDLC
    EXTERNAL    ///< External sync
};

/**
 * @enum SyncMode
 * @brief Synchronous mode selector from WR4.
 */
enum class SIOSyncMode {
    SYNC_8BIT = 0,   ///< Monosync
    SYNC_16BIT = 1,  ///< Bisync
    SDLC = 2,        ///< SDLC mode
    EXTERNAL = 3     ///< External sync
};

/**
 * @enum RxIntMode
 * @brief Receive interrupt mode from WR1.
 */
enum class SIORxIntMode {
    DISABLED = 0,
    FIRST_CHAR = 1,
    ALL_CHARS = 2,
    SPECIAL_CONDITION = 3
};

/**
 * @enum StopBits
 * @brief Stop bit configuration from WR4.
 */
enum class SIOStopBits {
    SYNC,       ///< Synchronous mode
    ONE,        ///< 1 stop bit
    ONE_HALF,   ///< 1.5 stop bits
    TWO         ///< 2 stop bits
};

/**
 * @enum RxState
 * @brief Receive state machine for asynchronous mode.
 */
enum class SIORxState {
    IDLE,
    START_BIT,
    DATA_BITS,
    PARITY_BIT,
    STOP_BIT
};

/**
 * @class Z80SIO
 * @brief Emulation of the Z80 Serial Input/Output (SIO) chip.
 *
 * Features:
 * - Two independent full-duplex channels (A, B)
 * - Interrupt daisy-chaining (IEI/IEO)
 * - Status and control registers per channel
 * - Multiple operating modes: Async, Monosync, Bisync, SDLC/HDLC
 * - CRC generation and checking
 * - Complete error detection
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

        // ─── Abfrage- und Leitungsweg für Schnittstellen nach außen (AP-S3) ────
        // doc/design/19_serielle_schnittstellen.md §6.2/§6.4.  Alles hier liest
        // die REGISTER (wr[]), nicht die abgeleiteten Felder — die überleben einen
        // Kanalreset, die Register nicht.

        /// Zeichenformat eines Kanals, wie es der Gast programmiert hat.
        struct Format {
            uint8_t teiler      = 1; ///< Taktteiler ×1/×16/×32/×64 (WR4 D7–6)
            uint8_t stopp_halbe = 0; ///< Stoppbits in halben Bits: 2=1, 3=1½, 4=2; 0 = synchron (WR4 D3–2 = 00)
            uint8_t paritaet    = 0; ///< 0 keine, 1 ungerade, 2 gerade (WR4 D1–0)
            uint8_t tx_bits     = 5; ///< Sendebits je Zeichen (WR5 D6–5)
            uint8_t rx_bits     = 5; ///< Empfangsbits je Zeichen (WR3 D7–6)
            bool asynchron() const { return stopp_halbe != 0; }
        };
        Format format() const;

        bool rts() const         { return (wr[5] & 0x02) != 0; } ///< Ausgang RTS (WR5 D1), true = aktiv
        bool dtr() const         { return (wr[5] & 0x80) != 0; } ///< Ausgang DTR (WR5 D7), true = aktiv
        bool breakSenden() const { return (wr[5] & 0x10) != 0; } ///< Break senden (WR5 D4)
        bool autoEnables() const { return (wr[3] & 0x20) != 0; } ///< WR3 D5

        /// Eingänge /CTS, /DCD — true = aktiv (Pin low; so zeigt es RR0 D5/D3).
        /// Vorgabe nach dem Einschalten: INAKTIV.  Grund: so lief die Emulation bis
        /// AP-S3 (RR0 D5/D3 = 0, niemand setzte die Eingänge); jeder heutige Gast-
        /// pfad (K8025/K7637, K7028/K7672/Drucker/Prüfstecker) sieht damit dasselbe
        /// RR0 wie vorher.  Auto Enables setzt kein bekannter Gast (Messung über die
        /// volle Regression + test-format, AP-S3) — sonst stünde deren Sender still.
        /// Ein Kanalreset ändert die Eingänge nicht (sie sind Pins, keine Register).
        bool cts() const { return cts_; }
        bool dcd() const { return dcd_; }
        /// Pegelwechsel an /CTS bzw. /DCD.  Bei freigegebenem Ext/Status-Interrupt
        /// (WR1 D0) hält RR0 den Stand zur Zeit der Änderung fest und der Kanal
        /// fordert einen Ext/Status-Interrupt an, bis „Reset Ext/Status“ (WR0
        /// Befehl 2) das Latch freigibt; ohne Freigabe folgt RR0 dem Eingang.
        void setzeCTS(bool aktiv);
        void setzeDCD(bool aktiv);
        /// Empfangenes Break (RxD dauerhaft Space): RR0 D7, Ext/Status an Anfang
        /// UND Ende (Datenblatt RR0 D7).
        void setzeBreakEmpfang(bool aktiv);
        bool breakEmpfangen() const { return break_rx_; }

        /// Wandlerseite (§5.1/§6.1).  „Sender hat Zeichen“: Tx-Puffer belegt und
        /// — bei Auto Enables — /CTS aktiv.  Abnehmen mit txGet(), einspeisen mit
        /// rxByte().  „Empfänger hat Platz“: FIFO nicht voll und — bei Auto Enables —
        /// /DCD aktiv.  Rx/Tx-Enable (WR3 D0/WR5 D3) werden bewusst NICHT
        /// ausgewertet, wie in den übrigen Pfaden dieses Bausteins.
        bool senderHatZeichen() const { return txAvailable(); }
        bool empfaengerFrei() const;

        // Internal state (public for Z80SIO access)
        uint8_t wr[8]  = {};    ///< Write registers 0-7
        uint8_t rr0    = 0x04;  ///< Read register 0 (Status): TxEmpty set by default
        uint8_t rr1    = 0x01;  ///< Read register 1 (Special Receive Condition): AllSent set by default
        uint8_t reg_ptr = 0;    ///< Pointer to current write register

        std::deque<uint8_t>      rx_fifo; ///< Receive FIFO buffer
        std::optional<uint8_t>   tx_buf;  ///< Transmit buffer

        /**
         * @brief Letztes physisch empfangenes Byte (Datenpfad hinter dem
         *        Empfangsschieberegister, unabhängig vom FIFO-Füllstand).
         *
         * Die echte U856/Z80-SIO hat am Datenregister keinen "leer"-Zustand — es ist
         * der Ausgang des FIFOs, kein separat abschaltbarer Bustreiber. Liest die CPU,
         * während der Empfangs-FIFO leer ist, kommt daher **das zuletzt empfangene
         * Byte** zurück, nicht FFH (`doc/design/16_k8915.md` §8a AP-E4c, Befund aus
         * AP-E2: K8915-BIOS `LISTST` liest 42H ohne RR0-Prüfung und erwartet dort das
         * zuletzt empfangene XON/XOFF). Das Datenblatt (`doc/trascripted/SIO_U856D.md`)
         * beschreibt diesen Grenzfall nicht ausdrücklich; `last_rx` wie alle anderen
         * Kanalregister mit dem Reset auf 00H gesetzt (Annahme, keine Messung).
         */
        uint8_t last_rx = 0x00;

        bool cts_    = false;   ///< Internal CTS state
        bool rts_    = false;   ///< Internal RTS state

        // Interrupt sources
        bool irq_rx  = false;   ///< Receive interrupt pending
        bool irq_tx  = false;   ///< Transmit interrupt pending
        bool irq_ext = false;   ///< External/Status interrupt pending
        bool iei     = false;   ///< Interrupt Enable Input (daisy chain)
        bool ius     = false;   ///< Interrupt Under Service flag

        std::function<void()> clk_cb_; ///< Clock callback function

        // ─── Operating mode and configuration ───────────────────────────────
        SIOMode mode = SIOMode::ASYNC;
        SIOSyncMode sync_mode = SIOSyncMode::SYNC_8BIT;
        SIORxIntMode rx_int_mode = SIORxIntMode::DISABLED;
        SIOStopBits stop_bits = SIOStopBits::ONE;

        // ─── Sync mode state ────────────────────────────────────────────────
        bool hunt_mode = false;       ///< Searching for sync pattern
        bool sync_found = false;      ///< Sync pattern detected
        uint8_t sync1 = 0;            ///< WR6: Sync char 1 / SDLC address
        uint8_t sync2 = 0;            ///< WR7: Sync char 2 / SDLC flag
        uint16_t sync_shift_reg = 0;  ///< Shift register for sync detection

        // ─── SDLC state ─────────────────────────────────────────────────────
        uint8_t ones_count = 0;       ///< Count consecutive 1s for flag/abort/stuffing
        bool in_frame = false;        ///< Currently receiving/transmitting frame
        bool tx_abort = false;        ///< Send abort sequence

        // ─── Character format ───────────────────────────────────────────────
        uint8_t rx_bits_per_char = 8;
        uint8_t tx_bits_per_char = 8;
        uint8_t clock_multiplier = 16;
        bool parity_enable = false;
        bool parity_even = false;

        // ─── Control flags ──────────────────────────────────────────────────
        bool rx_enable = false;
        bool tx_enable = false;
        bool auto_enables = false;
        bool send_break = false;
        bool sync_load_inhibit = false;
        bool addr_search_mode = false;

        // ─── CRC state ──────────────────────────────────────────────────────
        bool rx_crc_enable = false;
        bool tx_crc_enable = false;
        uint16_t rx_crc = 0xFFFF;
        uint16_t tx_crc = 0xFFFF;
        bool crc_ccitt = false;       ///< true=CCITT, false=CRC-16

        // ─── Interrupt control ──────────────────────────────────────────────
        bool ext_int_enable = false;
        bool tx_int_enable = false;
        /// Betriebsart „Interrupt beim ersten Zeichen" (WR1 D4–D3 = 01): scharf nach dem
        /// Setzen der Betriebsart und nach WR0-Befehl 4 („Enable Int on Next Rx
        /// Character"), vom nächsten empfangenen Zeichen verbraucht.
        bool rx_int_first_only = false;
        bool status_affects_vector = false;

        // ─── Modem control ──────────────────────────────────────────────────
        bool dtr_ = false;
        bool dcd_ = false;            ///< Data Carrier Detect
        bool cts_latch = false;       ///< CTS state latch for change detection
        bool dcd_latch = false;       ///< DCD state latch for change detection
        bool sync_latch = false;      ///< Sync status latch
        bool break_rx_  = false;      ///< Eingang: Break wird empfangen (AP-S3)
        bool ext_latch_ = false;      ///< RR0 D3/D5/D7 festgehalten (Ext/Status-Änderung)
        uint8_t ext_rr0_ = 0;         ///< festgehaltener Stand von RR0 D3/D5/D7

        /// RR0 D3/D5/D7 aus den Eingängen (ungelatcht).
        uint8_t extStatusEingaenge() const {
            return static_cast<uint8_t>((dcd_ ? 0x08 : 0) | (cts_ ? 0x20 : 0) | (break_rx_ ? 0x80 : 0));
        }
        /// Eine Ext/Status-Quelle hat sich geändert (Datenblatt Befehl 2, RR0 D3/D5/D7).
        void extStatusGeaendert();

        // ─── Receive state machine ──────────────────────────────────────────
        SIORxState rx_state = SIORxState::IDLE;
        uint8_t rx_shift_reg = 0;
        uint8_t rx_bit_count = 0;
        uint16_t rx_sample_count = 0;

        // ─── Transmit state ─────────────────────────────────────────────────
        bool tx_underrun = false;
        bool break_abort_detected = false;

        /**
         * @brief Reset the channel state.
         */
        void reset();
        
        /**
         * @brief Update status register RR0 based on internal state.
         */
        void updateRR0();
        
        /**
         * @brief Empfangsinterrupt bei JEDEM Zeichen frei (WR1 D4–D3 = 10 oder 11)?
         *
         * Maßgeblich ist `rx_int_mode` aus processWR1 — bis 2026-10-01 las diese
         * Funktion D3–D2 statt D4–D3 und ließ 10H/13H ohne Empfangsinterrupt.
         */
        bool rxIntEnabled() const;

        /**
         * @brief Löst ein soeben in den FIFO gelegtes Zeichen den Empfangsinterrupt aus?
         *
         * Eine Stelle für alle Empfangswege (asynchron, synchron, SDLC): „jedes
         * Zeichen" (10/11) oder „erstes Zeichen" (01, einmal je Scharfmachen).
         * Verbraucht die Scharfstellung der Betriebsart 01.
         */
        bool rxIntFaellig();
        
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
     * @brief Notify SIO that RETI was executed.
     *
     * Clears the Interrupt Under Service (IUS) flag for the channel
     * that was being serviced, allowing lower-priority interrupts.
     */
    void    onRETI() override;

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

    /**
     * @brief Read-only snapshot of both channels + daisy-chain state (debugger `dev sio`).
     */
    struct DebugState {
        bool iei = false, ieo = false;
        struct Ch {
            uint8_t wr1 = 0, wr2 = 0, rr0 = 0, rr1 = 0;
            bool    irqRx = false, irqTx = false, irqExt = false, iei = false, ius = false;
            size_t  rxQueued = 0;
            bool    txBusy = false;
        } ch[2];   // [0]=A, [1]=B
    };
    DebugState debugState() const;

    /**
     * @brief Hardware-Reset (/RESET-Leitung des K1520-Backplane).
     *
     * Beide Kanäle gehen in den Einschaltzustand (alle WRs 0, leere FIFOs, keine
     * Interruptanforderung, kein IUS).  Verdrahtung (Takt-Callback, Name) bleibt.
     */
    void reset() {
        ch_a_.reset(); ch_b_.reset();
        ch_a_.ius = ch_b_.ius = false;
    }

    // ─── Snapshot serialisation (savestate/loadstate) ────────────────────────
    // Append the complete, restorable chip state (both channels + IEI line) to
    // @p out. The wiring callback (Channel::clk_cb_) and bus/daisy-chain
    // registration are NOT serialised — they are re-established by the owning
    // card's connect()/registration, so a deserialise into an already-wired
    // chip keeps working. Used by A5120Machine::captureState so a loadstate
    // resumes with a consistent SIO (e.g. the keyboard channel).
    void serialize(std::vector<uint8_t>& out) const;
    // Restore state previously written by serialize(). Advances @p p past the
    // consumed bytes. Returns false on truncation/format error.
    bool deserialize(const uint8_t*& p, const uint8_t* end);

private:
    mutable Channel ch_a_;      ///< SIO Channel A (mutable for getVector)
    mutable Channel ch_b_;      ///< SIO Channel B (mutable for getVector)
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
    /// Wie channelHasInterrupt(), aber ohne bereits bediente Kanäle (IUS).
    bool channelRequests(const Channel& ch) const;
    /// RR2 von Kanal B: der Vektor, den eine Quittung JETZT liefern würde, ohne zu
    /// quittieren (kein IUS, keine gelöschte Anforderung).  Mit „status affects
    /// vector" und ohne anstehende Anforderung V3–V1 = 011 (Datenblatt).
    uint8_t rr2Vektor() const;

    // ─── Register processing helpers ────────────────────────────────────────

    /**
     * @brief Process WR1 (interrupt and data transfer mode).
     * @param ch Channel to configure
     * @param data WR1 byte
     * @param is_b true if Channel B
     */
    void processWR1(Channel& ch, uint8_t data, bool is_b);

    /**
     * @brief Process WR3 (receive parameters and control).
     * @param ch Channel to configure
     * @param data WR3 byte
     */
    void processWR3(Channel& ch, uint8_t data);

    /**
     * @brief Process WR4 (transmit/receive parameters and clock mode).
     * @param ch Channel to configure
     * @param data WR4 byte
     */
    void processWR4(Channel& ch, uint8_t data);

    /**
     * @brief Process WR5 (transmit parameters and control).
     * @param ch Channel to configure
     * @param data WR5 byte
     */
    void processWR5(Channel& ch, uint8_t data);

    /**
     * @brief Update channel operating mode based on current register settings.
     * @param ch Channel to update
     */
    void updateMode(Channel& ch);

    // ─── CRC calculation ────────────────────────────────────────────────────

    /**
     * @brief Update CRC with a data byte.
     * @param crc Current CRC value
     * @param data Data byte to include
     * @param ccitt true for CRC-CCITT, false for CRC-16
     * @return Updated CRC value
     */
    uint16_t updateCRC(uint16_t crc, uint8_t data, bool ccitt) const;

    /**
     * @brief Check CRC residue for validity.
     * @param crc CRC value after including CRC bytes
     * @param ccitt true for CRC-CCITT
     * @return true if CRC is valid
     */
    bool checkCRCResidue(uint16_t crc, bool ccitt) const;

    // ─── Mode-specific transmission/reception ───────────────────────────────

    /**
     * @brief Transmit in asynchronous mode with proper framing.
     * @param ch Channel to transmit on
     */
    void asyncTransmit(Channel& ch);

    /**
     * @brief Receive a bit in asynchronous mode.
     * @param ch Channel receiving
     * @param bit Received bit value
     */
    void asyncReceive(Channel& ch, bool bit);

    /**
     * @brief Receive in synchronous mode (Monosync/Bisync).
     * @param ch Channel receiving
     * @param bit Received bit value
     */
    void syncReceive(Channel& ch, bool bit);

    /**
     * @brief Transmit in synchronous mode (Monosync/Bisync).
     * @param ch Channel to transmit on
     */
    void syncTransmit(Channel& ch);

    /**
     * @brief Receive in SDLC/HDLC mode.
     * @param ch Channel receiving
     * @param bit Received bit value
     */
    void sdlcReceive(Channel& ch, bool bit);

    /**
     * @brief Transmit in SDLC/HDLC mode with bit stuffing.
     * @param ch Channel to transmit on
     */
    void sdlcTransmit(Channel& ch);

    /**
     * @brief Send a byte in SDLC mode with bit stuffing.
     * @param ch Channel to send on
     * @param data Byte to send
     * @param stuff true to apply bit stuffing
     */
    void sendSDLCByte(Channel& ch, uint8_t data, bool stuff);

    /**
     * @brief Monitor modem signals for external/status interrupts.
     * @param ch Channel to monitor
     */
    void monitorModemSignals(Channel& ch);
};
