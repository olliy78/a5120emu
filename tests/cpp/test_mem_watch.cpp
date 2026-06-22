// Unit-Tests für das Speicher-Watchpoint-Matching (tools/mem_watch.h).
#include "tools/mem_watch.h"
#include <gtest/gtest.h>

using memwatch::MemWatch;

namespace {
MemWatch wpWrite(uint16_t lo, uint16_t hi){ MemWatch w; w.lo=lo; w.hi=hi; w.wr=true; return w; }
}

TEST(MemWatch, RangeAndDirection){
    MemWatch w = wpWrite(0x6000, 0x6003);        // write-watch über Bereich
    EXPECT_TRUE (w.matches(false, 0x6000, 0x11)); // im Bereich, Schreiben
    EXPECT_TRUE (w.matches(false, 0x6003, 0x22));
    EXPECT_FALSE(w.matches(false, 0x6004, 0x33)); // außerhalb
    EXPECT_FALSE(w.matches(false, 0x5FFF, 0x33));
    EXPECT_FALSE(w.matches(true,  0x6000, 0x11)); // Lesen, aber nur Schreib-Watch
}

TEST(MemWatch, ReadWatch){
    MemWatch w; w.lo=w.hi=0x6000; w.rd=true;
    EXPECT_TRUE (w.matches(true,  0x6000, 0x11));
    EXPECT_FALSE(w.matches(false, 0x6000, 0x11)); // Schreiben, aber nur Lese-Watch
}

TEST(MemWatch, ConditionEqNe){
    MemWatch eq = wpWrite(0x10,0x10); eq.cond=MemWatch::EQ; eq.val=0x99;
    EXPECT_TRUE (eq.matches(false, 0x10, 0x99));
    EXPECT_FALSE(eq.matches(false, 0x10, 0x11));
    MemWatch ne = wpWrite(0x10,0x10); ne.cond=MemWatch::NE; ne.val=0x00;
    EXPECT_TRUE (ne.matches(false, 0x10, 0x05));
    EXPECT_FALSE(ne.matches(false, 0x10, 0x00));
}

TEST(MemWatch, ConditionChanged){
    MemWatch w = wpWrite(0x20,0x20); w.cond=MemWatch::CHG;
    EXPECT_TRUE (w.matches(false, 0x20, 0x05));    // erstes Mal: gilt als geändert
    EXPECT_FALSE(w.matches(false, 0x20, 0x05));    // gleicher Wert: nicht geändert
    EXPECT_TRUE (w.matches(false, 0x20, 0x06));    // anderer Wert: geändert
    EXPECT_FALSE(w.matches(false, 0x20, 0x06));    // wieder gleich
}

TEST(MemWatch, ChangedTracksPerAddress){
    MemWatch w; w.lo=0x30; w.hi=0x31; w.wr=true; w.cond=MemWatch::CHG;
    EXPECT_TRUE (w.matches(false, 0x30, 0xAA));    // 0x30 erstmals
    EXPECT_TRUE (w.matches(false, 0x31, 0xBB));    // 0x31 erstmals (eigene Historie)
    EXPECT_FALSE(w.matches(false, 0x30, 0xAA));    // 0x30 unverändert
    EXPECT_TRUE (w.matches(false, 0x31, 0xCC));    // 0x31 geändert
}

TEST(MemWatch, AnyConditionAlwaysFires){
    MemWatch w = wpWrite(0x40,0x40);               // cond=ANY (Default)
    EXPECT_TRUE(w.matches(false, 0x40, 0x00));
    EXPECT_TRUE(w.matches(false, 0x40, 0xFF));
}
