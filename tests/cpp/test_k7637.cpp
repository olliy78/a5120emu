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
 * Key translation rules:
 *  - Printable ASCII characters are forwarded unchanged.
 *  - Ctrl+key is mapped to the control code (e.g. Ctrl+A → 0x01).
 *  - Cursor keys map to 0x1C–0x1F; Escape → 0x1B; Delete → 0x7F; etc.
 *  - Function keys F1–F8 map to 0x80–0x87.
 *
 * Auto-repeat: after a key is held for the initial delay (≈500 ms), the key
 * code is re-sent at the repeat period (≈100 ms) until the key is released.
 *
 * TX commands from the firmware (via SIO TX FIFO):
 *  - 0x00: keyboard reset (clears LED state)
 *  - 0x44: beep
 *  - 0x52 + LED byte: set LED state (bit0=CAPS, bit1=SCROLL, bit2=NUM)
 *
 * ## Test groups
 *
 * | Group                     | What is tested                                          |
 * |---------------------------|---------------------------------------------------------|
 * | Basic key press           | ASCII printable, Shift, Ctrl, Return, Backspace, etc.  |
 * | Cursor keys               | Up/Down/Left/Right → 0x1E/0x1F/0x1C/0x1D               |
 * | Function keys             | F1 → 0x80; F8 → 0x87                                   |
 * | Key repeat                | Initial delay, repeat period, key release stops repeat  |
 * | processTxCommands         | Beep (no crash), reset (clears LEDs), LED control byte |
 * | Channel B connectivity    | Keyboard can inject into SIO channel B                  |
 * | No connection             | keyPress/tick/processTxCommands without connect() = no crash |
 *
 * @see core/peripherals/k7637/k7637.h
 * @see core/primitives/z80_sio.h
 */

#include <gtest/gtest.h>
#include "core/peripherals/k7637/k7637.h"

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

// Helper: drain all bytes from the SIO channel A RX FIFO and return them.
static std::vector<uint8_t> drainRx(Z80SIO& sio) {
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

    auto bytes = drainRx(sio);
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

    auto bytes = drainRx(sio);
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

    auto bytes = drainRx(sio);
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

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x01);
}

/**
 * @test K7637/KeyPress_Return_Sends_CR
 * @brief Pressing Return (QK_RETURN) injects a carriage return (0x0D).
 * @par Pass criterion  drainRx returns one byte == 0x0D.
 */
TEST(K7637, KeyPress_Return_Sends_CR) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_RETURN, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x0D);
}

/**
 * @test K7637/KeyPress_Backspace_Sends_BS
 * @brief Pressing Backspace (QK_BACKSPACE) injects 0x08 (BS control code).
 * @par Pass criterion  drainRx returns one byte == 0x08.
 */
TEST(K7637, KeyPress_Backspace_Sends_BS) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_BACKSPACE, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x08);
}

/**
 * @test K7637/KeyPress_Delete_Sends_DEL
 * @brief Pressing Delete (QK_DELETE) injects 0x7F (DEL character).
 * @par Pass criterion  drainRx returns one byte == 0x7F.
 */
TEST(K7637, KeyPress_Delete_Sends_DEL) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_DELETE, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x7F);
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

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1B);
}

/**
 * @test K7637/KeyPress_Tab_Sends_0x09
 * @brief Pressing Tab (QK_TAB) injects 0x09 (HT character).
 * @par Pass criterion  drainRx returns one byte == 0x09.
 */
TEST(K7637, KeyPress_Tab_Sends_0x09) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_TAB, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x09);
}

// ─── Cursor keys ─────────────────────────────────────────────────────────────

/**
 * @test K7637/CursorUp_Sends_0x1E
 * @brief Pressing cursor Up (QK_UP) injects 0x1E.
 * @par Pass criterion  drainRx returns one byte == 0x1E.
 */
TEST(K7637, CursorUp_Sends_0x1E) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_UP, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1E);
}

/**
 * @test K7637/CursorDown_Sends_0x1F
 * @brief Pressing cursor Down (QK_DOWN) injects 0x1F.
 * @par Pass criterion  drainRx returns one byte == 0x1F.
 */
TEST(K7637, CursorDown_Sends_0x1F) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_DOWN, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1F);
}

/**
 * @test K7637/CursorLeft_Sends_0x1C
 * @brief Pressing cursor Left (QK_LEFT) injects 0x1C.
 * @par Pass criterion  drainRx returns one byte == 0x1C.
 */
TEST(K7637, CursorLeft_Sends_0x1C) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_LEFT, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1C);
}

/**
 * @test K7637/CursorRight_Sends_0x1D
 * @brief Pressing cursor Right (QK_RIGHT) injects 0x1D.
 * @par Pass criterion  drainRx returns one byte == 0x1D.
 */
TEST(K7637, CursorRight_Sends_0x1D) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_RIGHT, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x1D);
}

// ─── Function keys ────────────────────────────────────────────────────────────

/**
 * @test K7637/FunctionKey_F1_Sends_0x80
 * @brief Pressing F1 (QK_F1) injects 0x80.
 * @par Pass criterion  drainRx returns one byte == 0x80.
 */
TEST(K7637, FunctionKey_F1_Sends_0x80) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_F1, false, false);

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x80);
}

/**
 * @test K7637/FunctionKey_F8_Sends_0x87
 * @brief Pressing F8 (QK_F1 + 7) injects 0x87 (F1 base code + 7).
 * @par Pass criterion  drainRx returns one byte == 0x87.
 */
TEST(K7637, FunctionKey_F8_Sends_0x87) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    kb.keyPress(QK_F1 + 7, false, false);   // F8

    auto bytes = drainRx(sio);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x87);
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
    drainRx(sio);                      // consume the initial byte

    // Advance exactly the repeat delay: one auto-repeat should fire.
    kb.tick(500);

    auto bytes = drainRx(sio);
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
    drainRx(sio);

    kb.tick(500);   // fires first auto-repeat, switches to period phase
    drainRx(sio);

    kb.tick(100);   // one period repeat
    auto bytes = drainRx(sio);
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
    drainRx(sio);

    kb.keyRelease(0x41);

    // Tick well past the delay: no repeat should be sent.
    kb.tick(1000);
    auto bytes = drainRx(sio);
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
    drainRx(sio);

    kb.keyRelease(0x42);   // release 'B' (wrong key) – should be a no-op

    kb.tick(500);   // 'A' should still repeat
    auto bytes = drainRx(sio);
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
 * @test K7637/TxCommand_Reset_ClearsLedState
 * @brief Sending command 0x00 (reset) clears all LED lock flags set by a previous 0x52 command.
 * @par Pass criterion  capsLock(), scrollLock(), numLock() all return false after reset command.
 */
TEST(K7637, TxCommand_Reset_ClearsLedState) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    // First set some LED state via 0x52 command.
    sio.ioWrite(0, 0x52);
    kb.processTxCommands();   // consumes 0x52, awaits LED byte
    sio.ioWrite(0, 0x07);     // caps + scroll + num lock
    kb.processTxCommands();   // consumes LED byte

    EXPECT_TRUE(kb.capsLock());
    EXPECT_TRUE(kb.scrollLock());
    EXPECT_TRUE(kb.numLock());

    // Now send reset.
    sio.ioWrite(0, 0x00);
    kb.processTxCommands();

    EXPECT_FALSE(kb.capsLock());
    EXPECT_FALSE(kb.scrollLock());
    EXPECT_FALSE(kb.numLock());
}

/**
 * @test K7637/TxCommand_LedControl_SetsLockFlags
 * @brief The LED control command (0x52 + LED byte) sets the CAPS, SCROLL, and NUM lock flags.
 * @details LED byte bit 0 = CAPS, bit 1 = SCROLL, bit 2 = NUM lock.
 * @par Pass criterion  capsLock() == true; scrollLock() == false; numLock() == true for LED byte 0x05.
 */
TEST(K7637, TxCommand_LedControl_SetsLockFlags) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    // Send 0x52 followed by the LED byte (bit0=CAPS, bit1=SCROLL, bit2=NUM).
    sio.ioWrite(0, 0x52);
    kb.processTxCommands();
    sio.ioWrite(0, 0x05);   // CAPS + NUM
    kb.processTxCommands();

    EXPECT_TRUE(kb.capsLock());
    EXPECT_FALSE(kb.scrollLock());
    EXPECT_TRUE(kb.numLock());
}

/**
 * @test K7637/TxCommand_ExtendedCmd_NoCrash
 * @brief An extended two-byte command (0x55 + second byte) is consumed without crashing.
 * @par Pass criterion  No exception or assertion failure.
 */
TEST(K7637, TxCommand_ExtendedCmd_NoCrash) {
    Z80SIO sio;
    sio.setIEI(true);
    K7637 kb;
    kb.connect(sio, 0);

    sio.ioWrite(0, 0x55);
    kb.processTxCommands();
    sio.ioWrite(0, 0xAA);   // second byte
    EXPECT_NO_FATAL_FAILURE(kb.processTxCommands());
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
