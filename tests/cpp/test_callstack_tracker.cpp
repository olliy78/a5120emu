// Unit-Tests für den History-Backtrace-Tracker (tools/callstack_tracker.h).
#include "tools/callstack_tracker.h"
#include <gtest/gtest.h>
#include <array>

using cstrack::CallStackTracker;

namespace {
// 64 KB Test-Speicher + Byte-Leser.
struct Mem {
    std::array<uint8_t,65536> b{};
    uint8_t operator()(uint16_t a) const { return b[a]; }
    void put(uint16_t a, std::initializer_list<uint8_t> bytes){
        uint16_t i=a; for(uint8_t x:bytes) b[i++]=x;
    }
};
// Eine Instruktionsfolge (Start-PCs in Ausführungsreihenfolge) einspeisen.
void feed(CallStackTracker& t, const Mem& m, std::initializer_list<uint16_t> pcs){
    for(uint16_t pc:pcs) t.onInstruction(pc,[&](uint16_t a){return m(a);});
}
} // namespace

TEST(CallStack, NestedCallReturnExact){
    Mem m;
    m.put(0x0100,{0xCD,0x00,0x02});   // CALL 0x0200
    m.put(0x0200,{0xCD,0x00,0x03});   // CALL 0x0300
    m.put(0x0300,{0xC9});             // RET
    m.put(0x0203,{0xC9});             // RET
    CallStackTracker t;

    feed(t,m,{0x0100});               EXPECT_EQ(t.depth(),0u);
    feed(t,m,{0x0200});               ASSERT_EQ(t.depth(),1u);
    EXPECT_EQ(t.frames()[0].site,0x0100);
    EXPECT_EQ(t.frames()[0].target,0x0200);
    EXPECT_EQ(t.frames()[0].ret,0x0103);
    feed(t,m,{0x0300});               ASSERT_EQ(t.depth(),2u);
    EXPECT_EQ(t.frames()[1].site,0x0200);
    EXPECT_EQ(t.frames()[1].ret,0x0203);
    feed(t,m,{0x0203});               EXPECT_EQ(t.depth(),1u);   // inner RET
    feed(t,m,{0x0103});               EXPECT_EQ(t.depth(),0u);   // outer RET
}

TEST(CallStack, RstPushesOneByteReturn){
    Mem m;
    m.put(0x0100,{0xD7});             // RST 10H → target 0x0010
    m.put(0x0010,{0xC9});             // RET
    CallStackTracker t;
    feed(t,m,{0x0100,0x0010});
    ASSERT_EQ(t.depth(),1u);
    EXPECT_EQ(t.frames()[0].target,0x0010);
    EXPECT_EQ(t.frames()[0].ret,0x0101);   // RST is 1 byte
    feed(t,m,{0x0101});               EXPECT_EQ(t.depth(),0u);
}

TEST(CallStack, ConditionalCallNotTakenIgnored){
    Mem m;
    m.put(0x0100,{0xC4,0x00,0x02});   // CALL NZ,0x0200
    CallStackTracker t;
    // not taken → execution continues at 0x0103 (≠ target 0x0200)
    feed(t,m,{0x0100,0x0103});
    EXPECT_EQ(t.depth(),0u);
}

TEST(CallStack, ConditionalCallTakenPushes){
    Mem m;
    m.put(0x0100,{0xCC,0x00,0x02});   // CALL Z,0x0200
    CallStackTracker t;
    feed(t,m,{0x0100,0x0200});        // taken
    EXPECT_EQ(t.depth(),1u);
}

TEST(CallStack, RetiPopsMatchingFrame){
    Mem m;
    m.put(0x0100,{0xCD,0x00,0x02});   // CALL 0x0200
    m.put(0x0200,{0xED,0x4D});        // RETI
    CallStackTracker t;
    feed(t,m,{0x0100,0x0200});        EXPECT_EQ(t.depth(),1u);
    feed(t,m,{0x0103});               EXPECT_EQ(t.depth(),0u);
}

TEST(CallStack, UnmatchedRetNeverUnderflowsOrCorrupts){
    Mem m;
    m.put(0x0100,{0xCD,0x00,0x02});   // CALL 0x0200
    m.put(0x0200,{0xC9});             // RET, but execution diverts (stack tweaked) to 0x9999
    CallStackTracker t;
    feed(t,m,{0x0100,0x0200});        EXPECT_EQ(t.depth(),1u);
    // RET lands somewhere other than the tracked return (0x0103) → frame kept, no underflow
    feed(t,m,{0x9999});               EXPECT_EQ(t.depth(),1u);
    // a bare RET with empty stack also must not underflow
    CallStackTracker t2;
    m.put(0x0400,{0xC9});
    feed(t2,m,{0x0400,0x1234});       EXPECT_EQ(t2.depth(),0u);
}

TEST(CallStack, ClearResets){
    Mem m;
    m.put(0x0100,{0xCD,0x00,0x02});
    CallStackTracker t;
    feed(t,m,{0x0100,0x0200});        EXPECT_EQ(t.depth(),1u);
    t.clear();                        EXPECT_EQ(t.depth(),0u);
    // after clear, prev is forgotten → first instr establishes no frame
    feed(t,m,{0x0200});               EXPECT_EQ(t.depth(),0u);
}
