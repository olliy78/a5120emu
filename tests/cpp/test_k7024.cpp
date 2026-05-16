// tests/cpp/test_k7024.cpp
// Unit tests for the K7024 ABS screen controller card.

#include <gtest/gtest.h>
#include "core/bus/k1520_bus.h"
#include "core/cards/k7024/k7024.h"

// ─── Helper ──────────────────────────────────────────────────────────────────

// Returns true if ANY byte in the framebuffer is non-zero.
static bool fbHasAnyPixel(const uint8_t* fb, int n)
{
    for (int i = 0; i < n; ++i)
        if (fb[i]) return true;
    return false;
}

// Returns true if ALL bytes in a region are zero.
static bool fbRegionAllZero(const uint8_t* fb, int startByte, int len)
{
    for (int i = startByte; i < startByte + len; ++i)
        if (fb[i]) return false;
    return true;
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class K7024Test : public ::testing::Test {
protected:
    K1520Bus bus;
    K7024    screen{bus};   // default config: Latin charset, VRAM @ 0xF800
};

// ─── 1. VRAM write → framebuffer pixel set ───────────────────────────────────
//
// Write 'A' (0x41) to column 0, row 0 via the bus memWrite interface.
// The 'A' glyph has pixels set in rows 1–7 of the character cell (0-indexed).
// At least one framebuffer byte in the first 8 pixel rows of cell (0,0) must
// be non-zero.

TEST_F(K7024Test, CharA_ProducesNonZeroPixels)
{
    // Write 'A' via bus
    bus.memWrite(0xF800, 0x41);

    const uint8_t* fb = screen.getFramebuffer();

    // Character cell (col=0, row=0) occupies fb_x=0..7, fb_y=0..11
    // Check the first 8 pixel rows (rows 0..7) = framebuffer bytes 0..63
    // (8 pixels wide × 8 pixel rows = 64 bytes at the top-left of fb)
    bool found = false;
    for (int py = 0; py < 8 && !found; ++py)
        for (int px = 0; px < 8 && !found; ++px)
            if (fb[py * 640 + px] != 0x00)
                found = true;

    EXPECT_TRUE(found) << "Expected at least one set pixel for character 'A'";
}

// ─── 2. Cursor bit → inverted pixels at rows 10–11 ───────────────────────────

TEST_F(K7024Test, CursorBit_InvertsRows10And11)
{
    // Write ' ' (space, 0x20) with cursor bit set → charCode = 0x20, cursor=true
    // Space renders as blank pixels in rows 0–9, so rows 10–11 should be 0xFF.
    screen.vramWrite(0, 0, 0x20 | 0x80);

    const uint8_t* fb = screen.getFramebuffer();

    // Pixel rows 10 and 11 of cell (col=0, row=0) are fb_y = 10 and 11.
    // Each spans px = 0..7 (8 bytes).
    for (int py : {10, 11}) {
        for (int px = 0; px < 8; ++px) {
            EXPECT_EQ(fb[py * 640 + px], 0xFF)
                << "Expected 0xFF (inverted blank) at fb_y=" << py
                << " fb_x=" << px;
        }
    }
}

TEST_F(K7024Test, CursorBit_DoesNotAffectRows0To9)
{
    // Space with cursor → rows 0–9 should stay blank (space glyph is all zeros).
    screen.vramWrite(0, 0, 0x20 | 0x80);

    const uint8_t* fb = screen.getFramebuffer();

    for (int py = 0; py < 10; ++py) {
        for (int px = 0; px < 8; ++px) {
            EXPECT_EQ(fb[py * 640 + px], 0x00)
                << "Expected 0x00 (blank space glyph) at fb_y=" << py
                << " fb_x=" << px;
        }
    }
}

// ─── 3. fbDirty flag lifecycle ────────────────────────────────────────────────

TEST_F(K7024Test, FbDirty_SetAfterWrite)
{
    // The K7024 constructor renders all cells; check that dirty is initially false.
    // (constructor doesn't set dirty_ since it's just the initial render)
    EXPECT_FALSE(screen.fbDirty());

    bus.memWrite(0xF800, 0x41);  // write 'A'

    EXPECT_TRUE(screen.fbDirty());
}

TEST_F(K7024Test, FbDirty_ClearedByFbClearDirty)
{
    bus.memWrite(0xF800, 0x41);
    ASSERT_TRUE(screen.fbDirty());

    screen.fbClearDirty();
    EXPECT_FALSE(screen.fbDirty());
}

// ─── 4. Console mode: pollTextChange ─────────────────────────────────────────

TEST_F(K7024Test, ConsoleMode_PollReturnsWrittenChar)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x41);   // write 'A' at col=0, row=0

    int x = -1, y = -1;
    char ch = 0;
    bool got = screen.pollTextChange(x, y, ch);

    EXPECT_TRUE(got);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(ch, 'A');
}

TEST_F(K7024Test, ConsoleMode_QueueEmptyAfterPoll)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x42);   // 'B'

    int x, y; char ch;
    ASSERT_TRUE(screen.pollTextChange(x, y, ch));   // consume the change

    EXPECT_FALSE(screen.pollTextChange(x, y, ch));  // queue is now empty
}

TEST_F(K7024Test, ConsoleMode_ControlCharMappedToSpace)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x01);   // SOH (control char < 0x20)

    int x, y; char ch;
    ASSERT_TRUE(screen.pollTextChange(x, y, ch));

    EXPECT_EQ(ch, ' ');   // control chars reported as space
}

TEST_F(K7024Test, ConsoleMode_MultipleChangesQueued)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x48);   // 'H' at col=0,row=0
    bus.memWrite(0xF801, 0x69);   // 'i' at col=1,row=0

    int x, y; char ch;
    ASSERT_TRUE(screen.pollTextChange(x, y, ch));
    EXPECT_EQ(ch, 'H');  EXPECT_EQ(x, 0);

    ASSERT_TRUE(screen.pollTextChange(x, y, ch));
    EXPECT_EQ(ch, 'i');  EXPECT_EQ(x, 1);

    EXPECT_FALSE(screen.pollTextChange(x, y, ch));
}

TEST_F(K7024Test, ConsoleMode_Disabled_NoPollChanges)
{
    // console_mode_ defaults to false
    bus.memWrite(0xF800, 0x41);

    int x, y; char ch;
    EXPECT_FALSE(screen.pollTextChange(x, y, ch));
}

// ─── 5. memRead returns what was written ─────────────────────────────────────

TEST_F(K7024Test, MemRead_ReturnsWrittenValue)
{
    bus.memWrite(0xF800, 0x41);
    EXPECT_EQ(bus.memRead(0xF800), 0x41);

    bus.memWrite(0xF8A0, 0xBE);   // col=0,row=2 (offset 160 = 2×80)
    EXPECT_EQ(bus.memRead(0xF8A0), 0xBE);
}

TEST_F(K7024Test, MemRead_VramReadMatchesBusRead)
{
    bus.memWrite(0xF800, 0x55);
    EXPECT_EQ(screen.vramRead(0, 0), 0x55);
}

// ─── 6. All-space clear → framebuffer all zeros ──────────────────────────────
//
// The EPROM stores printable characters starting at code 0x20 (space).
// The space glyph is all-zero bytes (blank cell).  Writing 0x20 everywhere
// must produce an entirely black framebuffer.

TEST_F(K7024Test, AllSpaceClear_FramebufferAllZeros)
{
    // Fill every VRAM cell with space (0x20)
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 80; ++col)
            screen.vramWrite(col, row, 0x20);

    const uint8_t* fb = screen.getFramebuffer();
    const int fb_size = 640 * 288;

    for (int i = 0; i < fb_size; ++i) {
        ASSERT_EQ(fb[i], 0x00)
            << "Non-zero pixel at framebuffer offset " << i;
    }
}

// ─── 7. vramWrite direct helper sets cell correctly ──────────────────────────

TEST_F(K7024Test, VramWrite_DirectHelper)
{
    screen.vramWrite(5, 3, 0x42);   // 'B' at col=5, row=3
    EXPECT_EQ(screen.vramRead(5, 3), 0x42);
}

// ─── 8. Framebuffer dimensions ───────────────────────────────────────────────

TEST_F(K7024Test, FramebufferDimensions)
{
    EXPECT_EQ(screen.fbWidth(),  640);
    EXPECT_EQ(screen.fbHeight(), 288);
    EXPECT_NE(screen.getFramebuffer(), nullptr);
}

// ─── 9. Row 1 vs row 0 of VRAM are independent ───────────────────────────────

TEST_F(K7024Test, VramRows_AreIndependent)
{
    screen.vramWrite(0, 0, 0x41);   // 'A' in cell (0,0)
    screen.vramWrite(0, 1, 0x20);   // space in cell (0,1)

    const uint8_t* fb = screen.getFramebuffer();

    // Cell row 1 starts at pixel row 12 → fb_y = 12..23
    // It should be all zeros (space glyph)
    for (int py = 12; py < 24; ++py)
        for (int px = 0; px < 8; ++px)
            EXPECT_EQ(fb[py * 640 + px], 0x00)
                << "Cell (0,1) should be blank at fb_y=" << py;
}
