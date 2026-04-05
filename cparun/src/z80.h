/**
 * @file z80.h
 * @brief Zilog Z80 CPU Emulator – Headerdatei
 *
 * Vollständige Emulation des Z80-Mikroprozessors, wie er im Robotron A5120
 * Bürocomputer (DDR) verbaut wurde. Implementiert den kompletten dokumentierten
 * und undokumentierten Z80-Befehlssatz inklusive:
 * - Alle 8-Bit- und 16-Bit-ALU-Operationen mit korrektem Flag-Verhalten
 * - Rotations- und Schiebebefehle (CB-Präfix)
 * - Indexregister-Operationen mit IX/IY (DD/FD-Präfix)
 * - Erweiterte Befehle (ED-Präfix): Blockoperationen, I/O, NEG, RETI/RETN
 * - Undokumentierte Befehle: SLL, IXH/IXL/IYH/IYL-Register, DDCB/FDCB-Varianten
 * - Alle drei Interruptmodi (IM 0, IM 1, IM 2) sowie NMI
 * - Zyklengenau für korrekte Taktung der Emulation
 *
 * Die Klasse verwendet Callback-Funktionen für Speicher- und I/O-Zugriffe,
 * sodass sie plattformunabhängig in beliebige Emulationsumgebungen eingebunden
 * werden kann.
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#pragma once
#include <cstdint>
#include <functional>

/**
 * @class Z80
 * @brief Emuliert einen Zilog Z80 Mikroprozessor.
 *
 * Diese Klasse bildet den vollständigen Z80-Prozessor nach, einschließlich
 * aller Register, Flags, Interrupt-Logik und des gesamten Befehlssatzes.
 * Speicher- und I/O-Zugriffe werden über konfigurierbare Callback-Funktionen
 * abstrahiert, wodurch die CPU-Emulation unabhängig von der konkreten
 * Hardwareumgebung bleibt.
 *
 * Typische Verwendung:
 * @code
 *   Z80 cpu;
 *   cpu.readByte  = [&](uint16_t addr) -> uint8_t { return memory[addr]; };
 *   cpu.writeByte = [&](uint16_t addr, uint8_t val) { memory[addr] = val; };
 *   cpu.readPort  = [&](uint16_t port) -> uint8_t { return io_read(port); };
 *   cpu.writePort = [&](uint16_t port, uint8_t val) { io_write(port, val); };
 *   cpu.reset();
 *   while (running) {
 *       int cycles = cpu.step();
 *   }
 * @endcode
 */
class Z80 {
public:
    // =========================================================================
    /// @name Hauptregistersatz
    /// @brief Die vier 16-Bit-Registerpaare AF, BC, DE, HL, die auch als
    ///        einzelne 8-Bit-Register angesprochen werden können.
    ///        Die Anordnung der Bytes innerhalb der Union berücksichtigt
    ///        die Endianness der Host-Plattform.
    // =========================================================================
    ///@{

    /**
     * @brief Akkumulator (A) und Flagregister (F).
     *
     * A ist der Hauptakkumulator für arithmetische und logische Operationen.
     * F enthält die Statusflags (S, Z, H, PV, N, C sowie undokumentiert Bit 3 und 5).
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t F, A;   ///< Little-Endian: F liegt an niedrigerer Adresse
            #else
            uint8_t A, F;   ///< Big-Endian: A liegt an niedrigerer Adresse
            #endif
        };
        uint16_t AF;         ///< Zugriff auf das gesamte AF-Registerpaar
    };

    /**
     * @brief Universalregister B und C.
     *
     * B wird häufig als Schleifenzähler verwendet (DJNZ-Befehl).
     * C dient oft als Port-Nummer bei I/O-Operationen.
     * BC wird als 16-Bit-Bytezähler bei Blockoperationen genutzt.
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t C, B;
            #else
            uint8_t B, C;
            #endif
        };
        uint16_t BC;         ///< Zugriff auf das gesamte BC-Registerpaar
    };

    /**
     * @brief Universalregister D und E.
     *
     * DE dient häufig als Zieladresse bei Blockübertragungen (LDI, LDIR etc.).
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t E, D;
            #else
            uint8_t D, E;
            #endif
        };
        uint16_t DE;         ///< Zugriff auf das gesamte DE-Registerpaar
    };

    /**
     * @brief Universalregister H und L.
     *
     * HL ist das primäre 16-Bit-Adressregister und wird für indirekte
     * Speicherzugriffe verwendet (z. B. LD A,(HL)). Bei DD/FD-präfixierten
     * Befehlen wird HL durch IX bzw. IY ersetzt.
     */
    union {
        struct {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t L, H;
            #else
            uint8_t H, L;
            #endif
        };
        uint16_t HL;         ///< Zugriff auf das gesamte HL-Registerpaar
    };

    ///@}

    // =========================================================================
    /// @name Schattenregistersatz
    /// @brief Zweiter Registersatz, der durch EX AF,AF' und EXX-Befehle
    ///        mit dem Hauptregistersatz ausgetauscht werden kann.
    ///        Wird typischerweise in Interrupt-Routinen verwendet.
    // =========================================================================
    ///@{
    uint16_t AF_;  ///< Schattenregister AF'
    uint16_t BC_;  ///< Schattenregister BC'
    uint16_t DE_;  ///< Schattenregister DE'
    uint16_t HL_;  ///< Schattenregister HL'
    ///@}

    // =========================================================================
    /// @name Indexregister
    /// @brief Ermöglichen indizierte Adressierung mit Displacement (IX+d, IY+d).
    ///        Undokumentiert können auch die oberen/unteren Bytes
    ///        (IXH, IXL, IYH, IYL) einzeln angesprochen werden.
    // =========================================================================
    ///@{
    uint16_t IX;   ///< Indexregister X
    uint16_t IY;   ///< Indexregister Y
    ///@}

    // =========================================================================
    /// @name Programmzähler und Stapelzeiger
    // =========================================================================
    ///@{
    uint16_t PC;   ///< Programmzähler (Program Counter) – zeigt auf den nächsten Befehl
    uint16_t SP;   ///< Stapelzeiger (Stack Pointer) – zeigt auf die Spitze des Stacks
    ///@}

    // =========================================================================
    /// @name Interrupt- und Auffrischungsregister
    // =========================================================================
    ///@{
    uint8_t I;     ///< Interrupt-Vektorregister – oberes Byte der Vektortabelle im IM 2
    uint8_t R;     ///< Speicher-Auffrischungsregister (Memory Refresh) – Bit 7 bleibt konstant
    ///@}

    // =========================================================================
    /// @name Interrupt-Zustand
    // =========================================================================
    ///@{
    bool IFF1;     ///< Interrupt-Flip-Flop 1 – steuert akzeptierte maskierbare Interrupts
    bool IFF2;     ///< Interrupt-Flip-Flop 2 – Zwischenspeicher von IFF1 bei NMI
    uint8_t IM;    ///< Interruptmodus (0, 1 oder 2)
    bool halted;   ///< True, wenn CPU durch HALT-Befehl angehalten wurde
    ///@}

    /**
     * @brief Gesamtzähler der bisher ausgeführten Taktzyklen.
     *
     * Wird bei jedem step()-Aufruf und bei Interruptbehandlung inkrementiert.
     * Kann zur Taktung von Peripheriegeräten und Timing-Synchronisation
     * verwendet werden.
     */
    uint64_t cycles;

    // =========================================================================
    /// @name Z80-Statusflags
    /// @brief Bitmasken für die einzelnen Bits des Flagregisters F.
    // =========================================================================
    ///@{
    static constexpr uint8_t FLAG_C  = 0x01;  ///< Carry-Flag (Bit 0) – Übertrag bei Addition/Subtraktion
    static constexpr uint8_t FLAG_N  = 0x02;  ///< Subtraktions-Flag (Bit 1) – gesetzt nach Subtraktionsbefehlen, für DAA
    static constexpr uint8_t FLAG_PV = 0x04;  ///< Parity/Overflow-Flag (Bit 2) – Parität oder arithmetischer Überlauf
    static constexpr uint8_t FLAG_3  = 0x08;  ///< Undokumentiertes Flag (Bit 3) – Kopie von Bit 3 des Ergebnisses
    static constexpr uint8_t FLAG_H  = 0x10;  ///< Half-Carry-Flag (Bit 4) – Halbübertrag (BCD-Korrektur)
    static constexpr uint8_t FLAG_5  = 0x20;  ///< Undokumentiertes Flag (Bit 5) – Kopie von Bit 5 des Ergebnisses
    static constexpr uint8_t FLAG_Z  = 0x40;  ///< Zero-Flag (Bit 6) – gesetzt wenn Ergebnis Null ist
    static constexpr uint8_t FLAG_S  = 0x80;  ///< Sign-Flag (Bit 7) – Vorzeichen (Kopie von Bit 7 des Ergebnisses)
    ///@}

    // =========================================================================
    /// @name Callback-Funktionen für Speicher- und I/O-Zugriffe
    /// @brief Müssen vor der Verwendung der CPU-Emulation gesetzt werden.
    ///        Sie stellen die Verbindung zur emulierten Hardware her.
    // =========================================================================
    ///@{
    std::function<uint8_t(uint16_t)> readByte;           ///< Callback: Byte aus dem Speicher lesen (Adresse → Wert)
    std::function<void(uint16_t, uint8_t)> writeByte;    ///< Callback: Byte in den Speicher schreiben (Adresse, Wert)
    std::function<uint8_t(uint16_t)> readPort;           ///< Callback: Byte von einem I/O-Port lesen (Portadresse → Wert)
    std::function<void(uint16_t, uint8_t)> writePort;    ///< Callback: Byte auf einen I/O-Port schreiben (Portadresse, Wert)
    ///@}

    // =========================================================================
    /// @name Öffentliche Methoden
    // =========================================================================
    ///@{

    /** @brief Konstruktor – initialisiert die CPU durch Aufruf von reset(). */
    Z80();

    /**
     * @brief Setzt die CPU in den Ausgangszustand zurück.
     *
     * Alle Register werden auf 0 gesetzt, SP auf 0xFFFF, Interrupts werden
     * deaktiviert, IM auf 0 gesetzt und der HALT-Zustand aufgehoben.
     */
    void reset();

    /**
     * @brief Führt einen einzelnen Z80-Befehl aus.
     *
     * Liest den Opcode an der Adresse PC, dekodiert und führt den Befehl aus.
     * Im HALT-Zustand werden nur NOP-Zyklen verbraucht (4 Takte).
     *
     * @return Anzahl der verbrauchten Taktzyklen für diesen Befehl.
     */
    int step();

    /**
     * @brief Löst einen maskierbaren Interrupt (INT) aus.
     *
     * Wird nur akzeptiert, wenn IFF1 gesetzt ist. Das Verhalten hängt
     * vom aktuellen Interruptmodus ab:
     * - IM 0: Führt den Befehl auf dem Datenbus aus (typisch RST).
     * - IM 1: Springt zu Adresse 0x0038.
     * - IM 2: Liest die Zieladresse aus der Vektortabelle (I*256 + vector).
     *
     * @param vector Interruptvektor – wird je nach IM unterschiedlich interpretiert.
     */
    void interrupt(uint8_t vector);

    /**
     * @brief Löst einen nicht-maskierbaren Interrupt (NMI) aus.
     *
     * Kann nicht deaktiviert werden. Sichert IFF1 in IFF2, deaktiviert
     * Interrupts und springt zur festen Adresse 0x0066.
     */
    void nmi();

    /**
     * @brief Legt einen 16-Bit-Wert auf den Stack.
     *
     * Dekrementiert SP um 2 und schreibt den Wert in Little-Endian-Reihenfolge.
     *
     * @param val Der auf den Stack zu legende Wert.
     */
    void push(uint16_t val);

    /**
     * @brief Nimmt einen 16-Bit-Wert vom Stack.
     *
     * Liest den Wert in Little-Endian-Reihenfolge und inkrementiert SP um 2.
     *
     * @return Der vom Stack gelesene 16-Bit-Wert.
     */
    uint16_t pop();

    ///@}

private:
    // =========================================================================
    /// @name Interne Speicherzugriffshilfen
    // =========================================================================
    ///@{

    /**
     * @brief Liest ein 16-Bit-Wort aus dem Speicher (Little-Endian).
     * @param addr Startadresse (niederwertiges Byte).
     * @return Gelesenes 16-Bit-Wort.
     */
    uint16_t readWord(uint16_t addr);

    /**
     * @brief Schreibt ein 16-Bit-Wort in den Speicher (Little-Endian).
     * @param addr Startadresse (niederwertiges Byte).
     * @param val Zu schreibendes 16-Bit-Wort.
     */
    void writeWord(uint16_t addr, uint16_t val);

    /**
     * @brief Liest das nächste Byte an PC und inkrementiert den Programmzähler.
     * @return Gelesenes Byte.
     */
    uint8_t fetchByte();

    /**
     * @brief Liest das nächste 16-Bit-Wort an PC und erhöht PC um 2.
     * @return Gelesenes 16-Bit-Wort (Little-Endian).
     */
    uint16_t fetchWord();

    ///@}

    // =========================================================================
    /// @name 8-Bit-ALU-Operationen
    /// @brief Alle arithmetischen und logischen 8-Bit-Operationen mit korrekter
    ///        Berechnung aller Flags (S, Z, H, PV, N, C sowie Bit 3 und 5).
    // =========================================================================
    ///@{

    /**
     * @brief 8-Bit-Addition: a + b.
     * @param a Erster Operand.
     * @param b Zweiter Operand.
     * @return Ergebnis (8 Bit). Flags: S, Z, H, PV (Überlauf), C.
     */
    uint8_t add8(uint8_t a, uint8_t b);

    /**
     * @brief 8-Bit-Addition mit Carry: a + b + Carry.
     * @param a Erster Operand.
     * @param b Zweiter Operand.
     * @return Ergebnis (8 Bit). Flags: S, Z, H, PV (Überlauf), C.
     */
    uint8_t adc8(uint8_t a, uint8_t b);

    /**
     * @brief 8-Bit-Subtraktion: a - b.
     * @param a Erster Operand (Minuend).
     * @param b Zweiter Operand (Subtrahend).
     * @return Ergebnis (8 Bit). Flags: S, Z, H, PV (Überlauf), N=1, C.
     */
    uint8_t sub8(uint8_t a, uint8_t b);

    /**
     * @brief 8-Bit-Subtraktion mit Carry: a - b - Carry.
     * @param a Erster Operand (Minuend).
     * @param b Zweiter Operand (Subtrahend).
     * @return Ergebnis (8 Bit). Flags: S, Z, H, PV (Überlauf), N=1, C.
     */
    uint8_t sbc8(uint8_t a, uint8_t b);

    /**
     * @brief Logisches UND: A = A & val.
     *
     * Setzt H-Flag, löscht N- und C-Flag. PV zeigt Parität an.
     * @param val Operand für die UND-Verknüpfung.
     */
    void and8(uint8_t val);

    /**
     * @brief Logisches ODER: A = A | val.
     *
     * Löscht H-, N- und C-Flag. PV zeigt Parität an.
     * @param val Operand für die ODER-Verknüpfung.
     */
    void or8(uint8_t val);

    /**
     * @brief Logisches Exklusiv-ODER: A = A ^ val.
     *
     * Löscht H-, N- und C-Flag. PV zeigt Parität an.
     * @param val Operand für die XOR-Verknüpfung.
     */
    void xor8(uint8_t val);

    /**
     * @brief Vergleich (Compare): A - val, Ergebnis wird verworfen.
     *
     * Setzt Flags wie SUB, aber der Akkumulator bleibt unverändert.
     * Bits 3 und 5 der Flags werden vom Operanden (val) übernommen,
     * nicht vom Ergebnis.
     * @param val Vergleichsoperand.
     */
    void cp8(uint8_t val);

    /**
     * @brief 8-Bit-Inkrement: val + 1.
     *
     * C-Flag bleibt unverändert. PV wird gesetzt bei Überlauf von 0x7F.
     * @param val Zu inkrementierender Wert.
     * @return Ergebnis (8 Bit).
     */
    uint8_t inc8(uint8_t val);

    /**
     * @brief 8-Bit-Dekrement: val - 1.
     *
     * C-Flag bleibt unverändert. PV wird gesetzt bei Unterlauf von 0x80.
     * @param val Zu dekrementierender Wert.
     * @return Ergebnis (8 Bit).
     */
    uint8_t dec8(uint8_t val);

    ///@}

    // =========================================================================
    /// @name 16-Bit-ALU-Operationen
    // =========================================================================
    ///@{

    /**
     * @brief 16-Bit-Addition: a + b.
     *
     * S-, Z- und PV-Flags bleiben erhalten. H bezieht sich auf Bit 11.
     * @param a Erster Operand.
     * @param b Zweiter Operand.
     * @return Ergebnis (16 Bit).
     */
    uint16_t add16(uint16_t a, uint16_t b);

    /**
     * @brief 16-Bit-Addition mit Carry: a + b + Carry.
     *
     * Alle Flags werden beeinflusst. PV zeigt 16-Bit-Überlauf an.
     * @param a Erster Operand.
     * @param b Zweiter Operand.
     * @return Ergebnis (16 Bit).
     */
    uint16_t adc16(uint16_t a, uint16_t b);

    /**
     * @brief 16-Bit-Subtraktion mit Carry: a - b - Carry.
     *
     * Alle Flags werden beeinflusst. PV zeigt 16-Bit-Überlauf an.
     * @param a Erster Operand (Minuend).
     * @param b Zweiter Operand (Subtrahend).
     * @return Ergebnis (16 Bit).
     */
    uint16_t sbc16(uint16_t a, uint16_t b);

    ///@}

    // =========================================================================
    /// @name Rotations- und Schiebebefehle
    /// @brief Implementierung der CB-Präfix-Operationen.
    ///        Alle Funktionen setzen S, Z, PV (Parität), Bit 3, Bit 5
    ///        und C (herausgeschobenes Bit). H und N werden gelöscht.
    // =========================================================================
    ///@{

    /**
     * @brief Rotate Left Circular – Bit 7 wird nach Bit 0 und ins C-Flag rotiert.
     * @param val Eingabewert.
     * @return Rotiertes Ergebnis.
     */
    uint8_t rlc(uint8_t val);

    /**
     * @brief Rotate Right Circular – Bit 0 wird nach Bit 7 und ins C-Flag rotiert.
     * @param val Eingabewert.
     * @return Rotiertes Ergebnis.
     */
    uint8_t rrc(uint8_t val);

    /**
     * @brief Rotate Left durch Carry – Bit 7 → C-Flag, altes C-Flag → Bit 0.
     * @param val Eingabewert.
     * @return Rotiertes Ergebnis.
     */
    uint8_t rl(uint8_t val);

    /**
     * @brief Rotate Right durch Carry – Bit 0 → C-Flag, altes C-Flag → Bit 7.
     * @param val Eingabewert.
     * @return Rotiertes Ergebnis.
     */
    uint8_t rr(uint8_t val);

    /**
     * @brief Shift Left Arithmetic – Bit 7 → C-Flag, Bit 0 wird 0.
     * @param val Eingabewert.
     * @return Geschobenes Ergebnis.
     */
    uint8_t sla(uint8_t val);

    /**
     * @brief Shift Right Arithmetic – Bit 0 → C-Flag, Bit 7 bleibt erhalten (Vorzeichen).
     * @param val Eingabewert.
     * @return Geschobenes Ergebnis.
     */
    uint8_t sra(uint8_t val);

    /**
     * @brief Shift Left Logical (undokumentiert) – Bit 7 → C-Flag, Bit 0 wird 1.
     *
     * Dies ist ein undokumentierter Z80-Befehl, manchmal auch als SL1 oder SLS bezeichnet.
     * @param val Eingabewert.
     * @return Geschobenes Ergebnis.
     */
    uint8_t sll(uint8_t val);

    /**
     * @brief Shift Right Logical – Bit 0 → C-Flag, Bit 7 wird 0.
     * @param val Eingabewert.
     * @return Geschobenes Ergebnis.
     */
    uint8_t srl(uint8_t val);

    ///@}

    // =========================================================================
    /// @name Bit-Operationen
    /// @brief BIT, RES und SET aus dem CB-Präfix-Befehlssatz.
    // =========================================================================
    ///@{

    /**
     * @brief BIT b,val – Testet das angegebene Bit.
     *
     * Z-Flag wird gesetzt wenn das Bit 0 ist. H wird gesetzt, N gelöscht.
     * C bleibt unverändert.
     * @param bit Bitnummer (0–7).
     * @param val Zu testender Wert.
     */
    void bit_op(uint8_t bit, uint8_t val);

    /**
     * @brief RES b,val – Setzt das angegebene Bit auf 0.
     * @param bit Bitnummer (0–7).
     * @param val Eingabewert.
     * @return Ergebnis mit gelöschtem Bit.
     */
    uint8_t res_op(uint8_t bit, uint8_t val);

    /**
     * @brief SET b,val – Setzt das angegebene Bit auf 1.
     * @param bit Bitnummer (0–7).
     * @param val Eingabewert.
     * @return Ergebnis mit gesetztem Bit.
     */
    uint8_t set_op(uint8_t bit, uint8_t val);

    ///@}

    // =========================================================================
    /// @name Hilfsfunktionen
    // =========================================================================
    ///@{

    /**
     * @brief Berechnet die Parität eines 8-Bit-Wertes.
     * @param val Zu prüfender Wert.
     * @return true bei gerader Parität (gerade Anzahl gesetzter Bits).
     */
    static bool parity(uint8_t val);

    ///@}

    // =========================================================================
    /// @name Befehlsdekodierung und -ausführung
    /// @brief Jede Funktion behandelt eine Gruppe von Z80-Opcodes,
    ///        geordnet nach Präfix-Bytes.
    // =========================================================================
    ///@{

    /**
     * @brief Hauptdekodierung – führt unpräfixierte Opcodes (0x00–0xFF) aus.
     *
     * Umfasst: 8-Bit-Lade-/ALU-/Sprung-/Aufruf-/Rücksprungbefehle,
     * 16-Bit-Lade- und Arithmetikbefehle, I/O, Austauschbefehle,
     * sowie Verzweigung zu den Präfix-Handlern (CB, DD, ED, FD).
     * @param opcode Der auszuführende Opcode.
     * @return Verbrauchte Taktzyklen.
     */
    int execMain(uint8_t opcode);

    /**
     * @brief CB-Präfix-Handler – Rotations-, Schiebe- und Bitoperationen.
     *
     * Dekodiert den zweiten Opcode nach dem CB-Präfix und führt die
     * entsprechende Operation auf dem ausgewählten Register oder (HL) aus.
     * @return Verbrauchte Taktzyklen (8 für Register, 12/15 für (HL)).
     */
    int execCB();

    /**
     * @brief DD-Präfix-Handler – IX-Indexregister-Operationen.
     *
     * Ersetzt in den meisten Befehlen HL durch IX. Speicherzugriffe über
     * (HL) werden zu (IX+d) mit vorzeichenbehaftetem 8-Bit-Displacement.
     * Undokumentiert: IXH/IXL als einzelne 8-Bit-Register.
     * @return Verbrauchte Taktzyklen.
     */
    int execDD();

    /**
     * @brief ED-Präfix-Handler – Erweiterte Befehle.
     *
     * Umfasst: IN/OUT mit Register, SBC/ADC HL, LD (nn)/rr, NEG,
     * RETN/RETI, IM-Modi, LD I,A / LD A,I / LD R,A / LD A,R,
     * RRD/RLD und alle Blockoperationen (LDI, LDIR, CPI, CPIR etc.).
     * @return Verbrauchte Taktzyklen.
     */
    int execED();

    /**
     * @brief FD-Präfix-Handler – IY-Indexregister-Operationen.
     *
     * Funktional identisch zum DD-Präfix, verwendet aber IY statt IX.
     * @return Verbrauchte Taktzyklen.
     */
    int execFD();

    /**
     * @brief DDCB-Doppelpräfix-Handler – Bit-/Rotationsoperationen auf (IX+d).
     *
     * Format: DD CB dd op – zuerst das Displacement, dann der Opcode.
     * Bei Registeroperanden (z != 6) wird das Ergebnis zusätzlich zum
     * Speicher auch in das angegebene Register geschrieben (undokumentiert).
     * @return Verbrauchte Taktzyklen (20 für BIT, 23 für andere).
     */
    int execDDCB();

    /**
     * @brief FDCB-Doppelpräfix-Handler – Bit-/Rotationsoperationen auf (IY+d).
     *
     * Funktional identisch zu DDCB, verwendet aber IY statt IX.
     * @return Verbrauchte Taktzyklen (20 für BIT, 23 für andere).
     */
    int execFDCB();

    ///@}

    // =========================================================================
    /// @name Bedingungsprüfung und Registerzugriff
    // =========================================================================
    ///@{

    /**
     * @brief Prüft eine Sprung-/Aufruf-/Rücksprungbedingung.
     *
     * @param cc Bedingungscode (0=NZ, 1=Z, 2=NC, 3=C, 4=PO, 5=PE, 6=P, 7=M).
     * @return true, wenn die Bedingung erfüllt ist.
     */
    bool checkCondition(uint8_t cc);

    /**
     * @brief Liest ein 8-Bit-Register anhand seines Opcode-Index.
     *
     * Kodierung: 0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A.
     * @param idx Registerindex (0–7).
     * @return Registerwert (bei idx=6 wird aus dem Speicher an Adresse HL gelesen).
     */
    uint8_t getReg8(uint8_t idx);

    /**
     * @brief Schreibt einen Wert in ein 8-Bit-Register anhand seines Opcode-Index.
     * @param idx Registerindex (0–7, siehe getReg8).
     * @param val Zu schreibender Wert.
     */
    void setReg8(uint8_t idx, uint8_t val);

    /**
     * @brief Liest ein 16-Bit-Registerpaar anhand seines Opcode-Index (SP-Variante).
     *
     * Kodierung: 0=BC, 1=DE, 2=HL, 3=SP.
     * @param idx Registerpaarindex (0–3).
     * @return 16-Bit-Registerwert.
     */
    uint16_t getReg16(uint8_t idx);

    /**
     * @brief Schreibt einen Wert in ein 16-Bit-Registerpaar (SP-Variante).
     * @param idx Registerpaarindex (0–3, siehe getReg16).
     * @param val Zu schreibender 16-Bit-Wert.
     */
    void setReg16(uint8_t idx, uint16_t val);

    /**
     * @brief Liest ein 16-Bit-Registerpaar anhand seines Opcode-Index (AF-Variante).
     *
     * Kodierung: 0=BC, 1=DE, 2=HL, 3=AF (statt SP).
     * Wird für PUSH/POP-Befehle verwendet.
     * @param idx Registerpaarindex (0–3).
     * @return 16-Bit-Registerwert.
     */
    uint16_t getReg16AF(uint8_t idx);

    /**
     * @brief Schreibt einen Wert in ein 16-Bit-Registerpaar (AF-Variante).
     * @param idx Registerpaarindex (0–3, siehe getReg16AF).
     * @param val Zu schreibender 16-Bit-Wert.
     */
    void setReg16AF(uint8_t idx, uint16_t val);

    ///@}
};
