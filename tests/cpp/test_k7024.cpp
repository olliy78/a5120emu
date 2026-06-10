/**
 * @file test_k7024.cpp
 * @brief Unit tests for the K7024 ABS screen controller card emulation.
 *
 * @details
 * Emulator component under test: **K7024** (`core/cards/k7024/k7024.h`)
 *
 * The K7024 ABS (Anschaltung Bildschirm) card is the character-generator and
 * display controller of the Robotron A5120.  It maintains a 80×24-character
 * VRAM at bus address 0xF800–0xF9BF and renders each character through a ROM
 * character set into a 640×288-pixel framebuffer (8 pixels wide, 12 rows tall
 * per character cell).
 *
 * Special features:
 *  - **Cursor bit** (bit 7 of VRAM byte): inverts pixel rows 10 and 11 of the
 *    character cell to display a hardware cursor.
 *  - **fbDirty flag**: set on every VRAM write; cleared by fbClearDirty() after
 *    the host UI has consumed the updated framebuffer.
 *  - **Console mode**: optional queue-based text-change notification for
 *    terminal-style host output without full pixel rendering.
 *
 * ## Test groups
 *
 * | Group                       | What is tested                                        |
 * |-----------------------------|-------------------------------------------------------|
 * | VRAM → framebuffer          | Character glyph produces non-zero pixels              |
 * | Cursor bit                  | Rows 10–11 inverted; rows 0–9 unaffected              |
 * | fbDirty flag lifecycle      | Set on write, cleared by fbClearDirty()               |
 * | Console mode (pollTextChange)| Queue character/position; control char mapping       |
 * | memRead returns written data | VRAM read-back via bus and vramRead()                 |
 * | All-space clear             | Space glyph → framebuffer all zeros                   |
 * | vramWrite() direct helper   | Sets cell without going through bus                   |
 * | Framebuffer dimensions      | 640×288 pixel buffer, non-null pointer                |
 * | Row independence            | Adjacent VRAM rows do not bleed into each other       |
 *
 * @see core/cards/k7024/k7024.h
 * @see core/bus/k1520_bus.h
 */

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

/**
 * @test K7024Test/CharA_ProducesNonZeroPixels
 * @brief Writing 'A' (0x41) to VRAM cell (0,0) produces at least one set pixel in the framebuffer.
 * @details The 'A' glyph in the character ROM has pixels in rows 1–7.
 *   Check the first 8 pixel rows (fb_x 0–7, fb_y 0–7) of cell (0,0).
 * @par Pass criterion  At least one framebuffer byte in the 8×8 region is != 0x00.
 */
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

/**
 * @test K7024Test/CursorBit_InvertsRows10And11
 * @brief Bit 7 (cursor bit) of a VRAM byte causes pixel rows 10 and 11 to be inverted (0xFF for space).
 * @details Space (0x20) has an all-zero glyph in rows 0–9; with cursor bit set, rows 10–11 = 0xFF.
 * @par Pass criterion  All 8 bytes in pixel rows 10 and 11 of cell (0,0) == 0xFF.
 */
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

/**
 * @test K7024Test/CursorBit_DoesNotAffectRows0To9
 * @brief Bit 7 (cursor bit) does not disturb glyph rows 0–9 when the character is a space.
 * @details Space (0x20) is all-zero in rows 0–9; cursor bit must not affect these rows.
 * @par Pass criterion  All 8 bytes in pixel rows 0–9 of cell (0,0) == 0x00.
 */
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

/**
 * @test K7024Test/FbDirty_SetAfterWrite
 * @brief A VRAM write via the bus sets the fbDirty flag.
 * @details fbDirty is initially cleared with fbClearDirty() to establish a clean baseline,
 *   then a bus write is performed.
 * @par Pass criterion  fbDirty() == true after bus.memWrite(0xF800, 0x41).
 */
TEST_F(K7024Test, FbDirty_SetAfterWrite)
{
    // Constructor renders initial noise and sets dirty=true. Clear it first so
    // we can test the write→dirty lifecycle independently.
    screen.fbClearDirty();
    EXPECT_FALSE(screen.fbDirty());

    bus.memWrite(0xF800, 0x41);  // write 'A'

    EXPECT_TRUE(screen.fbDirty());
}

/**
 * @test K7024Test/FbDirty_ClearedByFbClearDirty
 * @brief fbClearDirty() resets the dirty flag after a VRAM write.
 * @par Pass criterion  fbDirty() == false after fbClearDirty() is called.
 */
TEST_F(K7024Test, FbDirty_ClearedByFbClearDirty)
{
    bus.memWrite(0xF800, 0x41);
    ASSERT_TRUE(screen.fbDirty());

    screen.fbClearDirty();
    EXPECT_FALSE(screen.fbDirty());
}

// ─── 4. Console mode: pollTextChange ─────────────────────────────────────────

/**
 * @test K7024Test/ConsoleMode_PollReturnsWrittenChar
 * @brief In console mode, writing a character queues it for retrieval by pollTextChange().
 * @par Pass criterion  pollTextChange() returns true; ch == 'A'; x == 0; y == 0.
 */
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

/**
 * @test K7024Test/ConsoleMode_QueueEmptyAfterPoll
 * @brief pollTextChange() returns false once all queued changes have been consumed.
 * @par Pass criterion  Second pollTextChange() call returns false.
 */
TEST_F(K7024Test, ConsoleMode_QueueEmptyAfterPoll)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x42);   // 'B'

    int x, y; char ch;
    ASSERT_TRUE(screen.pollTextChange(x, y, ch));   // consume the change

    EXPECT_FALSE(screen.pollTextChange(x, y, ch));  // queue is now empty
}

/**
 * @test K7024Test/ConsoleMode_ControlCharMappedToSpace
 * @brief Control characters (< 0x20) are reported as space ' ' in console mode.
 * @details SOH (0x01) has no printable glyph; the console interface maps it to space.
 * @par Pass criterion  pollTextChange() returns ch == ' '.
 */
TEST_F(K7024Test, ConsoleMode_ControlCharMappedToSpace)
{
    screen.setConsoleMode(true);
    bus.memWrite(0xF800, 0x01);   // SOH (control char < 0x20)

    int x, y; char ch;
    ASSERT_TRUE(screen.pollTextChange(x, y, ch));

    EXPECT_EQ(ch, ' ');   // control chars reported as space
}

/**
 * @test K7024Test/ConsoleMode_MultipleChangesQueued
 * @brief Multiple VRAM writes in console mode are individually dequeued in order.
 * @details Writes 'H' at col 0 and 'i' at col 1; both must be dequeued correctly.
 * @par Pass criterion  Two successful pollTextChange() calls with correct ch and x values;
 *   third call returns false.
 */
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

/**
 * @test K7024Test/ConsoleMode_Disabled_NoPollChanges
 * @brief When console mode is off (default), pollTextChange() always returns false.
 * @par Pass criterion  pollTextChange() == false despite a VRAM write.
 */
TEST_F(K7024Test, ConsoleMode_Disabled_NoPollChanges)
{
    // console_mode_ defaults to false
    bus.memWrite(0xF800, 0x41);

    int x, y; char ch;
    EXPECT_FALSE(screen.pollTextChange(x, y, ch));
}

// ─── 5. memRead returns what was written ─────────────────────────────────────

/**
 * @test K7024/MemRead_ReturnsWrittenValue
 * @brief With the read-lock disabled (read_protect=false), VRAM data written via
 *        bus.memWrite() is returned by bus.memRead().
 * @details Only when the Lesesperre is OFF does the K7024 drive the data bus on reads
 *   (isReadable()==true), so the CPU can read VRAM back over the bus.  Under the A5120
 *   default (read_protect=true) the bus read instead returns 0xFF — that case is pinned
 *   by @ref MemRead_LesesperreAktiv_BusGibtFF below.
 * @par Pass criterion  bus.memRead(0xF800) == 0x41; bus.memRead(0xF8A0) == 0xBE.
 */
TEST(K7024, MemRead_ReturnsWrittenValue)
{
    K1520Bus bus;
    K7024::A5120Config cfg;
    cfg.read_protect = false;           // Lesesperre inaktiv → K7024 ist über den Bus lesbar
    K7024 screen(bus, cfg);             // registriert sich im Ctor selbst am Bus (attachToBus)

    bus.memWrite(0xF800, 0x41);
    EXPECT_EQ(bus.memRead(0xF800), 0x41);

    bus.memWrite(0xF8A0, 0xBE);   // col=0,row=2 (offset 160 = 2×80)
    EXPECT_EQ(bus.memRead(0xF8A0), 0xBE);
}

/**
 * @test K7024Test/MemRead_LesesperreAktiv_BusGibtFF
 * @brief Under the A5120 default (read_protect=true, Lesesperre aktiv) the K7024 does NOT
 *        drive the data bus on reads: a bus read of VRAM returns 0xFF, while the written
 *        value is still stored (and readable via the direct vramRead() accessor).
 * @details This is the real A5120 behaviour: the CPU writes the screen but cannot read it
 *   back over the bus; the K3526 RAM underneath keeps a readable shadow (the bus broadcasts
 *   writes to every writable device).  In this standalone fixture there is no K3526, so the
 *   bus read finds no readable device and returns 0xFF.
 * @par Pass criterion  bus.memRead(0xF800) == 0xFF; screen.vramRead(0,0) == 0x41.
 */
TEST_F(K7024Test, MemRead_LesesperreAktiv_BusGibtFF)
{
    bus.memWrite(0xF800, 0x41);
    EXPECT_EQ(bus.memRead(0xF800), 0xFF) << "Lesesperre: K7024 treibt den Bus bei Reads nicht";
    EXPECT_EQ(screen.vramRead(0, 0), 0x41) << "VRAM hat den Wert dennoch gespeichert";
}

/**
 * @test K7024Test/MemRead_VramReadMatchesBusRead
 * @brief vramRead(col, row) returns the same data as bus.memRead() for the same cell.
 * @par Pass criterion  screen.vramRead(0, 0) == 0x55 after bus.memWrite(0xF800, 0x55).
 */
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

/**
 * @test K7024Test/AllSpaceClear_FramebufferAllZeros
 * @brief Writing 0x20 (space) to every VRAM cell produces a fully zero framebuffer.
 * @details The space glyph in the character ROM is all-zero, so the entire 640×288
 *   pixel framebuffer must be zero.
 * @par Pass criterion  Every byte in the 640×288 framebuffer == 0x00.
 */
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

/**
 * @test K7024Test/VramWrite_DirectHelper
 * @brief vramWrite(col, row, val) stores the value and vramRead(col, row) returns it.
 * @par Pass criterion  vramRead(5, 3) == 0x42 after vramWrite(5, 3, 0x42).
 */
TEST_F(K7024Test, VramWrite_DirectHelper)
{
    screen.vramWrite(5, 3, 0x42);   // 'B' at col=5, row=3
    EXPECT_EQ(screen.vramRead(5, 3), 0x42);
}

// ─── 8. Framebuffer dimensions ───────────────────────────────────────────────

/**
 * @test K7024Test/FramebufferDimensions
 * @brief fbWidth() == 640, fbHeight() == 288, and getFramebuffer() returns a non-null pointer.
 * @par Pass criterion  fbWidth() == 640; fbHeight() == 288; getFramebuffer() != nullptr.
 */
TEST_F(K7024Test, FramebufferDimensions)
{
    EXPECT_EQ(screen.fbWidth(),  640);
    EXPECT_EQ(screen.fbHeight(), 288);
    EXPECT_NE(screen.getFramebuffer(), nullptr);
}

// ─── 10. A5120Config: read_protect steuert isReadable() (Lesesperre) ─────────
//
// Korrektur: read_protect (X13/X14) ist die *Lesesperre* und steuert isReadable()
// — ob der K7024 bei CPU-Lesezugriffen den Datenbus treibt (/DIEN).  isWritable()
// ist davon UNABHÄNGIG und immer true (die CPU schreibt den Bildschirm stets).

/**
 * @test K7024Test/Config_ReadProtect_True_NotReadable
 * @brief Default config (read_protect=true, X13/X14 pos1 closed, Lesesperre aktiv) →
 *        isReadable() == false; isWritable() bleibt true.
 * @par Pass criterion  screen.isReadable() == false; screen.isWritable() == true.
 */
TEST_F(K7024Test, Config_ReadProtect_True_NotReadable)
{
    EXPECT_FALSE(screen.isReadable());
    EXPECT_TRUE(screen.isWritable());
}

/**
 * @test K7024/Config_ReadProtect_False_IsReadable
 * @brief read_protect=false (X13/X14 pos2, Lesesperre inaktiv) → isReadable() == true;
 *        isWritable() bleibt true.
 * @par Pass criterion  screen.isReadable() == true; screen.isWritable() == true.
 */
TEST(K7024, Config_ReadProtect_False_IsReadable)
{
    K1520Bus bus;
    K7024::A5120Config cfg;
    cfg.read_protect = false;
    K7024 screen(bus, cfg);
    EXPECT_TRUE(screen.isReadable());
    EXPECT_TRUE(screen.isWritable());
}

/**
 * @test K7024/Config_CursorBlink_DefaultFalse
 * @brief Default A5120Config has cursor_blink == false (X15/X16 pos1 closed = ruhend).
 * @par Pass criterion  A5120Config{}.cursor_blink == false.
 */
TEST(K7024, Config_CursorBlink_DefaultFalse)
{
    K7024::A5120Config cfg;
    EXPECT_FALSE(cfg.cursor_blink);
}

/**
 * @test K7024/Config_VramBase_DefaultF800
 * @brief Default A5120Config places VRAM at 0xF800 (X11/X12 all closed).
 * @par Pass criterion  A5120Config{}.vram_base_hi == 0xF8.
 */
TEST(K7024, Config_VramBase_DefaultF800)
{
    K7024::A5120Config cfg;
    EXPECT_EQ(cfg.vram_base_hi, 0xF8);
}

/**
 * @test K7024Test/VramRows_AreIndependent
 * @brief Writing to VRAM row 0 does not affect the framebuffer region of row 1.
 * @details 'A' is placed in cell (0,0) and space in cell (0,1); pixel rows 12–23
 *   (belonging to VRAM row 1) must be entirely zero.
 * @par Pass criterion  All 8 bytes per pixel row in fb_y 12–23 == 0x00.
 */
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
