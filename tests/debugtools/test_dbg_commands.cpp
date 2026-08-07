// Unit-Tests für die k1520dbg-Kommando-Vervollständigung (tools/dbg_commands.h).
#include "tools/dbg_commands.h"
#include <gtest/gtest.h>
#include <algorithm>

using dbgcmd::match;

namespace {
bool has(const std::vector<std::string>& v, const std::string& s){
    return std::find(v.begin(), v.end(), s) != v.end();
}
}

TEST(DbgCommands, PrefixMatchesAllStartingWith){
    auto m = match("b");                 // b, b2, bd, be, bi, bl, bint, bnmi, breti, bs, bt, bdis…
    EXPECT_TRUE(has(m,"b"));
    EXPECT_TRUE(has(m,"b2"));
    EXPECT_TRUE(has(m,"bl"));
    EXPECT_TRUE(has(m,"breti"));
    EXPECT_FALSE(has(m,"g"));             // not starting with b
}

TEST(DbgCommands, DistinguishesLongerPrefix){
    auto m = match("bi");                // bi, bi2 (not bl/bint? bint starts "bi" → included)
    EXPECT_TRUE(has(m,"bi"));
    EXPECT_TRUE(has(m,"bi2"));
    EXPECT_TRUE(has(m,"bint"));           // "bint" also starts with "bi"
    EXPECT_FALSE(has(m,"bl"));
}

TEST(DbgCommands, ExactAndEmptyPrefix){
    auto m = match("snap");
    EXPECT_TRUE(has(m,"snap"));
    EXPECT_FALSE(has(m,"source"));        // "snap" ≠ prefix of source

    // empty prefix → everything
    EXPECT_EQ(match("").size(), dbgcmd::names().size());
}

TEST(DbgCommands, NoMatchForUnknownPrefix){
    EXPECT_TRUE(match("zzz").empty());
}

TEST(DbgCommands, KnownCommandsPresent){
    // a few representative commands across groups must be in the list
    for (const char* c : {"g","rs","b","wp","logpoint","trace","x","list",
                          "dev","alias","source","reset","q"})
        EXPECT_TRUE(has(dbgcmd::names(), c)) << c;
}
