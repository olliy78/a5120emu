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

TEST(K7637, NoConnect_KeyPress_NoCrash) {
    K7637 kb;   // not connected to any SIO
    EXPECT_NO_FATAL_FAILURE(kb.keyPress(0x41, false, false));
    EXPECT_NO_FATAL_FAILURE(kb.tick(500));
    EXPECT_NO_FATAL_FAILURE(kb.processTxCommands());
}
