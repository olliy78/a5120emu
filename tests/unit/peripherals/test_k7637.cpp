/**
 * @file test_k7637.cpp
 * @brief Unit tests for the K7637 keyboard controller emulation.
 *
 * @details
 * Emulator component under test: **K7637** (`core/peripherals/k7637/k7637.h`)
 *
 * The K7637 emulates the Robotron A5120 keyboard controller.  It translates
 * host key events (Qt keycodes) into the byte codes the A5120 firmware expects
 * and injects them into the Z80 SIO channel A or B RX FIFO.  It also processes
 * TX commands sent by the firmware (LED control, beep, reset).
 *
 * Key translation rules (the K7637 sends its *physical* code; the A5120 BIOS
 * `cp37` recode table then maps the high codes to virtual codes):
 *  - Printable ASCII characters are forwarded unchanged.
 *  - Ctrl+key is mapped to the control code (e.g. Ctrl+A → 0x01).
 *  - Main Return = ET1 → 0xFF (BIOS → CR); numeric ENTER → 0xC0 (distinct key).
 *  - Cursor Up/Down/Left/Right → 0x94/0x95/0x96/0x97; Escape → 0x1B (ESC-Taste);
 *    Tab → 0x9F (|<-|); Backspace und Delete → 0xBB (DEL CH).
 *  - Function keys F1–F8 → 0xC1–0xC8.
 *
 * Serial timing: keyboard→host bytes (key codes AND type-code acks) are not
 * delivered to the SIO RX instantly — they are released by service() after one
 * 9600-baud byte-time, so the tests flush the queue via drainRx()/deliver().
 *
 * Auto-repeat: after a key is held for the initial delay (≈500 ms), the key
 * code is re-sent at the repeat period (≈100 ms) until the key is released.
 *
 * TX commands from the firmware (via SIO TX FIFO).  The real keyboard decodes
 * them by the NUMBER OF PULSE EDGES, not by the byte value (manual §2.2.3):
 *  - counter 14 (e.g. 0x00, 0xFF): software reset — all displays off
 *  - counter 13 (0x20): error display blink on/off (+ ~1 s beep when enabled)
 *  - counter 12 (0x44): ~1 s beep
 *  - counter 11 (0x52) and 8/7/6/5 (0x55 + 0x20/0x44/0x52/0x55): toggle one of
 *    the five function displays G00…G04
 *  - counter 10 (0x55 alone): pre-command — the next byte keeps counting
 *
 * ## Test groups
 *
 * | Group                     | What is tested                                          |
 * |---------------------------|---------------------------------------------------------|
 * | Basic key press           | ASCII printable, Shift, Ctrl; ET1/Return 0xFF ≠ ENTER 0xC0 |
 * | Cursor keys               | Up/Down/Left/Right → 0x94/0x95/0x96/0x97               |
 * | Function keys             | F1 → 0xC1; F8 → 0xC8                                   |
 * | Serial timing             | Byte delivered only after one 9600-baud byte-time; FIFO order |
 * | Key repeat                | Initial delay, repeat period, key release stops repeat  |
 * | Rohcodes                  | `QK_RAW_BASE | code` unverändert, auch mit Ctrl          |
 * | processTxCommands         | Flankenzählung, die fünf LED-Kommandos (umschaltend), Fehleranzeige + Ton, Reset, Quittung |
 * | Channel B connectivity    | Keyboard can inject into SIO channel B                  |
 * | No connection             | keyPress/tick/processTxCommands without connect() = no crash |
 *
 * @see core/peripherals/k7637/k7637.h
 * @see core/primitives/z80_sio.h
 */

#include <gtest/gtest.h>
#include "core/peripherals/k7637/k7637.h"
#include <ios>
#include <initializer_list>

// Qt keycode constants (must match k7637.h / k7637.cpp)
static constexpr int QK_ESCAPE    = 0x01000000;
static constexpr int QK_TAB       = 0x01000001;
static constexpr int QK_BACKSPACE = 0x01000003;
static constexpr int QK_RETURN    = 0x01000004;
static constexpr int QK_ENTER     = 0x01000005;
static constexpr int QK_DELETE    = 0x01000007;
static constexpr int QK_LEFT      = 0x01000012;
static constexpr int QK_UP        = 0x01000013;
static constexpr int QK_RIGHT     = 0x01000014;
static constexpr int QK_DOWN      = 0x01000015;
static constexpr int QK_F1        = 0x01000030;

// The K7637 models the 9600-baud serial link: bytes are not delivered to the
// SIO RX the instant keyPress()/tick()/processTxCommands() run — they are
// released by service() once their transmission time has elapsed.  These tests
// therefore advance a monotonically increasing cycle clock and call service()
// to flush the queue before inspecting the SIO.  The increment is far larger
// than one byte-time so every queued byte is released.
static uint64_t g_clk = 0;
static void deliver(K7637& kb) { g_clk += 1000000; kb.service(g_clk); }

// Helper: flush the serial queue, then drain all bytes from the SIO channel A
// RX FIFO and return them.
static std::vector<uint8_t> drainRx(K7637& kb, Z80SIO& sio) {
    deliver(kb);
    std::vector<uint8_t> out;
    while (sio.ioRead(1) & 0x01) {   // while RR0 bit0 (Rx Character Available) is set
        out.push_back(sio.ioRead(0));
    }
    return out;
}

// ─── Basic key press / translation ───────────────────────────────────────────

/**
 * @test K7637/KeyPress_UppercaseA_Sends_0x41
 * @brief Pressing 'A' (keycode 0x41) without modifiers injects 0x41 into SIO channel A.
 * @par Pass criterion  drainRx returns one byte == 0x41.
 */
TEST(K7637, KeyPress_UppercaseA_Sends_0x41) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);   // channel A

    kb.keyPress(0x41, false, false);   // 'A' – no shift, no ctrl

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x41);
}

/**
 * @test K7637/KeyPress_UppercaseA_WithShift_Sends_0x41
 * @brief 'A' with Shift still produces 0x41 (shift is encoded in keycode, not separately).
 * @par Pass criterion  drainRx returns one byte == 0x41.
 */
TEST(K7637, KeyPress_UppercaseA_WithShift_Sends_0x41) {
    // Shift is already encoded in the keycode; 'A' stays 'A'.
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, true, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x41);
}

/**
 * @test K7637/KeyPress_LowercaseA_Sends_0x61
 * @brief Pressing 'a' (keycode 0x61) injects 0x61 into the SIO.
 * @par Pass criterion  drainRx returns one byte == 0x61.
 */
TEST(K7637, KeyPress_LowercaseA_Sends_0x61) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x61, false, false);   // 'a'

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x61);
}

/**
 * @test K7637/KeyPress_Ctrl_A_Sends_0x01
 * @brief Ctrl + 'A' (keycode 0x41, ctrl=true) produces the control code 0x01.
 * @par Pass criterion  drainRx returns one byte == 0x01.
 */
TEST(K7637, KeyPress_Ctrl_A_Sends_0x01) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, true);    // Ctrl + 'A'

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x01);
}

/**
 * @test K7637/KeyPress_Return_Sends_ET1
 * @brief The main Return key is the ET1 key: it sends physical code 0xFF, which
 *        the BIOS cp37 table recodes to a carriage return (0x0D).  This is a
 *        DIFFERENT physical key from the numeric ENTER (see below).
 * @par Pass criterion  drainRx returns one byte == 0xFF.
 */
TEST(K7637, KeyPress_Return_Sends_ET1) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_RETURN, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xFF);
}

/**
 * @test K7637/KeyPress_Enter_Sends_PF0
 * @brief The numeric ENTER key is physically distinct from ET1/Return: it sends
 *        physical code 0xC0 (recoded to the programmable pf0c), not 0xFF/CR.
 * @par Pass criterion  drainRx returns one byte == 0xC0.
 */
TEST(K7637, KeyPress_Enter_Sends_PF0) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_ENTER, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xC0);
}

/**
 * @test K7637/KeyPress_Backspace_Sends_BS
 * @brief Backspace liegt auf DEL CH (0xBB) — der Taste, die im Gast ein Zeichen
 *        rückwärts löscht (am laufenden CP/A nachgemessen).
 * @par Pass criterion  drainRx returns one byte == 0xBB.
 */
TEST(K7637, KeyPress_Backspace_Sends_BS) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_BACKSPACE, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xBB);
}

/**
 * @test K7637/KeyPress_Delete_Sends_DELCH
 * @brief Pressing Delete (QK_DELETE) injects 0x7F (DEL character).
 * @par Pass criterion  drainRx returns one byte == 0x7F.
 */
TEST(K7637, KeyPress_Delete_Sends_DELCH) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_DELETE, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xBB);
}

/**
 * @test K7637/KeyPress_Escape_Sends_ESC
 * @brief Pressing Escape (QK_ESCAPE) injects 0x1B (ESC character).
 * @par Pass criterion  drainRx returns one byte == 0x1B.
 */
TEST(K7637, KeyPress_Escape_Sends_ESC) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_ESCAPE, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1B);
}

/**
 * @test K7637/KeyPress_Tab_Sends_0x9F
 * @brief Pressing Tab (QK_TAB) injects 0x09 (HT character).
 * @par Pass criterion  drainRx returns one byte == 0x09.
 */
TEST(K7637, KeyPress_Tab_Sends_0x9F) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_TAB, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x9F);
}

// ─── Cursor keys ─────────────────────────────────────────────────────────────

/**
 * @test K7637/CursorUp_Sends_0x94
 * @brief Pressing cursor Up (QK_UP) injects 0x1E.
 * @par Pass criterion  drainRx returns one byte == 0x1E.
 */
TEST(K7637, CursorUp_Sends_0x94) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_UP, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x94);
}

/**
 * @test K7637/CursorDown_Sends_0x95
 * @brief Pressing cursor Down (QK_DOWN) injects 0x1F.
 * @par Pass criterion  drainRx returns one byte == 0x1F.
 */
TEST(K7637, CursorDown_Sends_0x95) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_DOWN, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x95);
}

/**
 * @test K7637/CursorLeft_Sends_0x96
 * @brief Pressing cursor Left (QK_LEFT) injects 0x1C.
 * @par Pass criterion  drainRx returns one byte == 0x1C.
 */
TEST(K7637, CursorLeft_Sends_0x96) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_LEFT, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x96);
}

/**
 * @test K7637/CursorRight_Sends_0x97
 * @brief Pressing cursor Right (QK_RIGHT) injects 0x1D.
 * @par Pass criterion  drainRx returns one byte == 0x1D.
 */
TEST(K7637, CursorRight_Sends_0x97) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_RIGHT, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x97);
}

// ─── Function keys ────────────────────────────────────────────────────────────

/**
 * @test K7637/FunctionKey_F1_Sends_0xC1
 * @brief Pressing F1 (QK_F1) injects 0x80.
 * @par Pass criterion  drainRx returns one byte == 0x80.
 */
TEST(K7637, FunctionKey_F1_Sends_0xC1) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_F1, false, false);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xC1);
}

/**
 * @test K7637/FunctionKey_F8_Sends_0xC8
 * @brief Pressing F8 (QK_F1 + 7) injects 0x87 (F1 base code + 7).
 * @par Pass criterion  drainRx returns one byte == 0x87.
 */
TEST(K7637, FunctionKey_F8_Sends_0xC8) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_F1 + 7, false, false);   // F8

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xC8);
}

// ─── Rohcodes (Bildschirmtastatur) ──────────────────────────────────────────

/**
 * @test K7637/RawCode_IsSentVerbatim
 * @brief `QK_RAW_BASE | code` sendet genau dieses Byte.
 * @details Die Bildschirmtastatur bildet die echte K7637 nach und kennt Tasten,
 *          die eine PC-Tastatur nicht hat (CE 0xB9, SEL0 0xA0, PF10 0xCA, …).
 *          Ohne den Rohcode-Weg wären sie über die Qt-Abbildung unerreichbar.
 * @par Pass criterion  drainRx liefert je ein Byte == dem übergebenen Code.
 */
TEST(K7637, RawCode_IsSentVerbatim) {
    for (uint8_t code : {uint8_t(0xB9), uint8_t(0xA0), uint8_t(0xCA),
                         uint8_t(0xFA), uint8_t(0x9B), uint8_t(0xB1)}) {
        Z80SIO sio;
        sio.setIEI(true);
        K7637 kb;
        kb.connect(sio, 0);

        kb.keyPress(K7637::QK_RAW_BASE | code, false, false);

        auto bytes = drainRx(kb, sio);
        ASSERT_EQ(bytes.size(), 1u) << "Code 0x" << std::hex << int(code);
        EXPECT_EQ(bytes[0], code);
    }
}

/**
 * @test K7637/RawCode_IgnoresCtrl
 * @brief Ein Rohcode ist der physische Tastencode — Ctrl rechnet nicht daran.
 * @details `& 0x1F` gilt nur für druckbares ASCII; ein Rohcode 0xC1 (PF1) bliebe
 *          sonst als 0x01 liegen und käme im Gast als Steuerzeichen an.
 * @par Pass criterion  drainRx liefert 0xC1, nicht 0x01.
 */
TEST(K7637, RawCode_IgnoresCtrl) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(K7637::QK_RAW_BASE | 0xC1, false, /*ctrl=*/true);

    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0xC1);
}

// ─── Key repeat ───────────────────────────────────────────────────────────────

/**
 * @test K7637/KeyRepeat_AfterDelay_SendsAgain
 * @brief After holding a key for the initial delay (≈500 ms), one auto-repeat byte is sent.
 * @par Pass criterion  drainRx returns at least one byte == 0x41 after tick(500).
 */
TEST(K7637, KeyRepeat_AfterDelay_SendsAgain) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, false);   // press 'A', sends one byte immediately
    drainRx(kb, sio);                      // consume the initial byte

    // Advance exactly the repeat delay: one auto-repeat should fire.
    kb.tick(500);

    auto bytes = drainRx(kb, sio);
    EXPECT_GE(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x41);
}

/**
 * @test K7637/KeyRepeat_PeriodRepeat
 * @brief After the initial delay fires, the repeat continues at the shorter period (≈100 ms).
 * @par Pass criterion  drainRx returns at least one byte == 0x41 after the second tick(100).
 */
TEST(K7637, KeyRepeat_PeriodRepeat) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, false);
    drainRx(kb, sio);

    kb.tick(500);   // fires first auto-repeat, switches to period phase
    drainRx(kb, sio);

    kb.tick(100);   // one period repeat
    auto bytes = drainRx(kb, sio);
    EXPECT_GE(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x41);
}

/**
 * @test K7637/KeyRelease_StopsRepeat
 * @brief Releasing the pressed key stops auto-repeat; no further bytes are sent.
 * @par Pass criterion  drainRx returns empty vector after keyRelease() + tick(1000).
 */
TEST(K7637, KeyRelease_StopsRepeat) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, false);
    drainRx(kb, sio);

    kb.keyRelease(0x41);

    // Tick well past the delay: no repeat should be sent.
    kb.tick(1000);
    auto bytes = drainRx(kb, sio);
    EXPECT_EQ(bytes.size(), 0u);
}

/**
 * @test K7637/KeyRelease_WrongKey_DoesNotClearRepeat
 * @brief Releasing a different key than the one held does not stop the auto-repeat.
 * @par Pass criterion  drainRx returns at least one byte after tick(500) despite keyRelease('B').
 */
TEST(K7637, KeyRelease_WrongKey_DoesNotClearRepeat) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, false);   // 'A' held
    drainRx(kb, sio);

    kb.keyRelease(0x42);   // release 'B' (wrong key) – should be a no-op

    kb.tick(500);   // 'A' should still repeat
    auto bytes = drainRx(kb, sio);
    EXPECT_GE(bytes.size(), 1u);
}

// ─── processTxCommands ───────────────────────────────────────────────────────

/**
 * @test K7637/TxCommand_Beep_NoCrash
 * @brief A beep command (0x44) in the SIO TX buffer is consumed by processTxCommands() without crashing.
 * @par Pass criterion  No exception; SIO TX buffer empty after the call.
 */
TEST(K7637, TxCommand_Beep_NoCrash) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    // Write a beep command into the SIO TX buffer (as if K8025 sent it).
    sio.ioWrite(0, 0x44);

    // processTxCommands should consume it without crashing.
    EXPECT_NO_FATAL_FAILURE(kb.processTxCommands());
    EXPECT_FALSE(sio.channelA().txAvailable());
}

/**
 * @test K7637/CommandDecoding_CountsFallingEdges
 * @brief Die Kommandoerkennung zählt Flanken, nicht Bytewerte.
 * @details Handbuch §2.2.3: die Impulse des empfangenen Bytes zählen einen auf
 *          15 voreingestellten Zähler herunter; sein Stand IST das Kommando.
 *          Die Tabelle dort nennt neun Zählerstände — sie müssen sich alle aus
 *          der Zahl der fallenden Flanken ergeben, sonst stimmt das Modell nicht.
 * @par Pass criterion  15 − Flanken == der im Handbuch angegebene Zählerstand.
 */
TEST(K7637, CommandDecoding_CountsFallingEdges) {
    EXPECT_EQ(15 - K7637::fallingEdges(0x00), 14);   // Software-RESET
    EXPECT_EQ(15 - K7637::fallingEdges(0x20), 13);   // Fehleranzeige
    EXPECT_EQ(15 - K7637::fallingEdges(0x44), 12);   // akustisches Signal
    EXPECT_EQ(15 - K7637::fallingEdges(0x52), 11);   // LED G00
    EXPECT_EQ(15 - K7637::fallingEdges(0x55), 10);   // Vorkommando
    // Zweibyte-Kommandos: der Zähler läuft über beide Bytes weiter.
    const int pre = K7637::fallingEdges(0x55);
    EXPECT_EQ(15 - (pre + K7637::fallingEdges(0x00)), 9);   // Grundzustand
    EXPECT_EQ(15 - (pre + K7637::fallingEdges(0x20)), 8);   // LED G01
    EXPECT_EQ(15 - (pre + K7637::fallingEdges(0x44)), 7);   // LED G02
    EXPECT_EQ(15 - (pre + K7637::fallingEdges(0x52)), 6);   // LED G03
    EXPECT_EQ(15 - (pre + K7637::fallingEdges(0x55)), 5);   // LED G04
}

// Ein Kommando an die Tastatur schicken (ein oder zwei Bytes).
static void sendCmd(K7637& kb, Z80SIO& sio, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) { sio.ioWrite(0, b); kb.processTxCommands(); }
}

/**
 * @test K7637/LedCommands_ToggleTheirDisplay
 * @brief Die fünf LED-Kommandos schalten je eine Funktionsanzeige UM.
 * @details „Ein- bzw. Ausschalten von LED-Anzeigen (vorheriger Zustand wird
 *          negiert)" — ein zweites gleiches Kommando schaltet wieder aus.
 * @par Pass criterion  Jedes Kommando setzt genau sein Bit und löscht es wieder.
 */
TEST(K7637, LedCommands_ToggleTheirDisplay) {
    struct { std::initializer_list<uint8_t> cmd; uint8_t bit; } cases[] = {
        { {0x52},       K7637::LED_G00 },
        { {0x55, 0x20}, K7637::LED_G01 },
        { {0x55, 0x44}, K7637::LED_G02 },
        { {0x55, 0x52}, K7637::LED_G03 },
        { {0x55, 0x55}, K7637::LED_G04 },
    };
    for (const auto& c : cases) {
        Z80SIO sio; sio.setIEI(true);
        K7637 kb;   kb.connect(sio, 0);

        sendCmd(kb, sio, c.cmd);
        EXPECT_EQ(kb.leds(), c.bit);
        sendCmd(kb, sio, c.cmd);
        EXPECT_EQ(kb.leds(), 0);
    }
}

/**
 * @test K7637/ErrorDisplay_TogglesAndBeepsWhenSwitchedOn
 * @brief Kommando 20H schaltet die Fehleranzeige um; beim EINschalten piept es ~1 s.
 * @par Pass criterion  LED_ERROR gesetzt und beeping(); beim zweiten Mal beides aus.
 */
TEST(K7637, ErrorDisplay_TogglesAndBeepsWhenSwitchedOn) {
    Z80SIO sio; sio.setIEI(true);
    K7637 kb;   kb.connect(sio, 0);

    sendCmd(kb, sio, {0x20});
    EXPECT_TRUE(kb.leds() & K7637::LED_ERROR);
    EXPECT_TRUE(kb.beeping());

    // Nach einer Sekunde Maschinenzeit ist der Ton vorbei, die Anzeige bleibt.
    kb.service(3000000);
    EXPECT_FALSE(kb.beeping());
    EXPECT_TRUE(kb.leds() & K7637::LED_ERROR);

    sendCmd(kb, sio, {0x20});
    EXPECT_FALSE(kb.leds() & K7637::LED_ERROR);
    EXPECT_FALSE(kb.beeping());
}

/**
 * @test K7637/BeepCommand_RunsForAboutOneSecond
 * @brief Kommando 44H löst ein akustisches Signal von ca. 1 s aus — ohne Anzeige.
 * @par Pass criterion  beeping() ist an, nach 1 s Maschinenzeit aus; leds() bleibt 0.
 */
TEST(K7637, BeepCommand_RunsForAboutOneSecond) {
    Z80SIO sio; sio.setIEI(true);
    K7637 kb;   kb.connect(sio, 0);

    sendCmd(kb, sio, {0x44});
    EXPECT_TRUE(kb.beeping());
    EXPECT_EQ(kb.leds(), 0);

    kb.service(2400000);          // knapp unter einer Sekunde
    EXPECT_TRUE(kb.beeping());
    kb.service(2600000);
    EXPECT_FALSE(kb.beeping());
}

/**
 * @test K7637/ResetCommand_ClearsAllDisplays
 * @brief 00H und 55H,00H stellen den Grundzustand her: alle Funktionsanzeigen aus.
 * @par Pass criterion  leds() == 0 nach jedem der beiden Reset-Kommandos.
 */
TEST(K7637, ResetCommand_ClearsAllDisplays) {
    for (std::initializer_list<uint8_t> reset : {std::initializer_list<uint8_t>{0x00},
                                                 std::initializer_list<uint8_t>{0x55, 0x00}}) {
        Z80SIO sio; sio.setIEI(true);
        K7637 kb;   kb.connect(sio, 0);

        sendCmd(kb, sio, {0x52});          // G00 an
        sendCmd(kb, sio, {0x20});          // Fehleranzeige an
        EXPECT_NE(kb.leds(), 0);

        sendCmd(kb, sio, reset);
        EXPECT_EQ(kb.leds(), 0);
        EXPECT_FALSE(kb.beeping());
    }
}

/**
 * @test K7637/CommandDecoding_IgnoresTheByteValue
 * @brief Ein Byte mit derselben Flankenzahl wirkt wie das Kommando aus der Tabelle.
 * @details Die Bytes der Handbuchtabelle sind nur die übliche Schreibweise; die
 *          Hardware zählt Flanken.  0xFF hat wie 0x00 genau eine fallende
 *          Flanke (nur das Startbit) und ist damit derselbe Software-RESET.
 *          Ein Modell, das auf den Bytewert schaut, verwürfe es als unbekanntes
 *          Kommando — und träfe die Hardware nur dort, wo die Systemsoftware
 *          zufällig das kanonische Byte sendet.
 * @par Pass criterion  0xFF löscht die Anzeigen wie 0x00.
 */
TEST(K7637, CommandDecoding_IgnoresTheByteValue) {
    Z80SIO sio; sio.setIEI(true);
    K7637 kb;   kb.connect(sio, 0);

    sendCmd(kb, sio, {0x52});
    EXPECT_EQ(kb.leds(), K7637::LED_G00);

    sendCmd(kb, sio, {0xFF});          // eine fallende Flanke, wie 0x00
    EXPECT_EQ(kb.leds(), 0);
}

/**
 * @test K7637/PreCommand_NeedsTheSecondByte
 * @brief Nach dem Vorkommando 55H allein passiert nichts — der Zähler wartet.
 * @par Pass criterion  leds() bleibt 0, bis das zweite Byte kommt.
 */
TEST(K7637, PreCommand_NeedsTheSecondByte) {
    Z80SIO sio; sio.setIEI(true);
    K7637 kb;   kb.connect(sio, 0);

    sendCmd(kb, sio, {0x55});
    EXPECT_EQ(kb.leds(), 0);
    sendCmd(kb, sio, {0x20});
    EXPECT_EQ(kb.leds(), K7637::LED_G01);
}

/**
 * @test K7637/EveryCommandByteIsAcknowledged
 * @brief Die Tastatur quittiert JEDES empfangene Byte mit dem Typcode 0x80.
 * @details Daran hängt die Tastaturerkennung des BIOS und der LED-Handschlag;
 *          ohne die Quittung wartet `lmpout` ewig.
 * @par Pass criterion  Nach zwei Kommandobytes stehen zwei 0x80 im RX.
 */
TEST(K7637, EveryCommandByteIsAcknowledged) {
    Z80SIO sio; sio.setIEI(true);
    K7637 kb;   kb.connect(sio, 0);

    sendCmd(kb, sio, {0x55, 0x52});
    auto bytes = drainRx(kb, sio);
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0x80);
    EXPECT_EQ(bytes[1], 0x80);
}

// ─── Channel B connectivity ───────────────────────────────────────────────────

/**
 * @test K7637/ChannelB_KeyPress_InjectsIntoChannelB
 * @brief When connected to SIO channel B (connect(sio, 1)), key presses go into channel B, not A.
 * @par Pass criterion  Channel A has no data; channel B data port (0x02) returns 0x41.
 */
TEST(K7637, ChannelB_KeyPress_InjectsIntoChannelB) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 1);   // channel B

    kb.keyPress(0x41, false, false);
    deliver(kb);   // flush the serial queue into the SIO RX

    // Channel A should be empty; channel B should have the byte.
    EXPECT_FALSE(sio.channelA().rxFull() || (sio.ioRead(1) & 0x01));
    EXPECT_EQ(sio.ioRead(2), 0x41);   // port 2 = channel B data
}

// ─── No connection – no crash ────────────────────────────────────────────────

/**
 * @test K7637/NoConnect_KeyPress_NoCrash
 * @brief K7637 without a connected SIO handles keyPress(), tick(), and processTxCommands() safely.
 * @par Pass criterion  No exception or assertion failure for any of the three calls.
 */
TEST(K7637, NoConnect_KeyPress_NoCrash) {
    K7637 kb;   // not connected to any SIO
    EXPECT_NO_FATAL_FAILURE(kb.keyPress(0x41, false, false));
    EXPECT_NO_FATAL_FAILURE(kb.tick(500));
    EXPECT_NO_FATAL_FAILURE(kb.processTxCommands());
}

// ─── Serial-transmit timing (the 2026-06 fix) ────────────────────────────────

/**
 * @test K7637/SerialLatency_ByteDeliveredAfterOneByteTime
 * @brief A key byte is NOT in the SIO RX immediately; it appears only after one
 *        9600-baud byte-time (~2604 ZVE1 cycles) of service().  This is the fix
 *        that stops the keyboard's type-code acks from racing the OS LED
 *        handshake and flooding the keyboard buffer (no input at the CCP).
 */
TEST(K7637, SerialLatency_ByteDeliveredAfterOneByteTime) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(0x41, false, false);   // 'A' queued at cycle 0, release ≈ 2604

    // Well within one byte-time: the byte must NOT be available yet.
    kb.service(1000);
    EXPECT_FALSE(sio.ioRead(1) & 0x01)
        << "key byte arrived with no serial latency — the ack-vs-ISR race is back";

    // Past one byte-time: now it is delivered, with the correct value.
    kb.service(3000);
    ASSERT_TRUE(sio.ioRead(1) & 0x01) << "key byte was never delivered by service()";
    EXPECT_EQ(sio.ioRead(0), 0x41);
}

/**
 * @test K7637/SerialLatency_BytesSerialiseInOrder
 * @brief Two keys queued back-to-back occupy the line sequentially: the second
 *        only appears one byte-time after the first, in FIFO order.
 */
TEST(K7637, SerialLatency_BytesSerialiseInOrder) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress('a', false, false);    // release ≈ 2604
    kb.keyPress('b', false, false);    // release ≈ 2*2604

    kb.service(3000);                  // only the first byte-time has passed
    ASSERT_TRUE(sio.ioRead(1) & 0x01);
    EXPECT_EQ(sio.ioRead(0), 'a');
    EXPECT_FALSE(sio.ioRead(1) & 0x01) << "second byte arrived too early";

    kb.service(6000);                  // past the second byte-time
    ASSERT_TRUE(sio.ioRead(1) & 0x01);
    EXPECT_EQ(sio.ioRead(0), 'b');
}

/**
 * @test K7637/SerializeRoundTrip
 * @brief serialize() → deserialize() restores the pending serial-TX queue and
 *        its 9600-baud timing into a fresh keyboard, so a byte typed just before
 *        a savestate is still delivered after the matching loadstate.
 * @par Pass criterion  The queued byte is delivered by service() on the restored
 *      keyboard and the blob is fully consumed.
 */
TEST(K7637, SerializeRoundTrip) {
    Z80SIO sio1; sio1.setIEI(true);
    K7637 a; a.connect(sio1, 0);
    a.keyPress(0x42, false, false);    // 'B' → queued in tx_queue_, not yet delivered

    std::vector<uint8_t> blob;
    a.serialize(blob);
    ASSERT_FALSE(blob.empty());

    // Fresh keyboard on a fresh SIO: the pending byte must survive the load.
    Z80SIO sio2; sio2.setIEI(true);
    K7637 b; b.connect(sio2, 0);
    const uint8_t* p   = blob.data();
    const uint8_t* end = p + blob.size();
    ASSERT_TRUE(b.deserialize(p, end));
    EXPECT_EQ(p, end);

    EXPECT_FALSE(sio2.ioRead(1) & 0x01);   // RR0 bit0: nothing delivered yet
    b.service(6000);                       // past the 9600-baud byte-time
    ASSERT_TRUE(sio2.ioRead(1) & 0x01);
    EXPECT_EQ(sio2.ioRead(0), 0x42);       // the restored byte arrives
}
