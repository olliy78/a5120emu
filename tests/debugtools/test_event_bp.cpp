// Unit-Tests für die Ereignis-Breakpoint-Erkennung (tools/event_bp.h).
#include "tools/event_bp.h"
#include <gtest/gtest.h>

using eventbp::Event;
using eventbp::Prev;
using eventbp::classify;

// armInt, armNmi, armReti = alle scharf, sofern nicht anders nötig.
static Event C(uint16_t pc, uint16_t sp, bool iff1, const Prev& prev,
               bool ai=true, bool an=true, bool ar=true, uint8_t o0=0, uint8_t o1=0){
    return classify(pc, sp, iff1, prev, ai, an, ar, o0, o1);
}

TEST(EventBp, MaskableInterruptSignature){
    Prev prev{true, 0x8000, /*iff1=*/true};      // vorher: Interrupts an, SP=0x8000
    // ISR-Eintritt: IFF1 1→0, SP um 2 gepusht, Ziel ≠ 0x0066
    EXPECT_EQ(C(0x1234, 0x7FFE, false, prev), Event::Interrupt);
}

TEST(EventBp, InterruptNotFiredWhenIff1DidNotDrop){
    Prev prev{true, 0x8000, true};
    // PUSH: SP-2, aber IFF1 bleibt 1 → kein Interrupt
    EXPECT_EQ(C(0x1235, 0x7FFE, true, prev), Event::None);
}

TEST(EventBp, InterruptDisarmedGivesNone){
    Prev prev{true, 0x8000, true};
    EXPECT_EQ(C(0x1234, 0x7FFE, false, prev, /*armInt=*/false), Event::None);
}

TEST(EventBp, NmiNormal){
    Prev prev{true, 0x8000, true};
    EXPECT_EQ(C(0x0066, 0x7FFE, false, prev), Event::NMI);   // Sprung auf 0x0066 + SP-Push
}

// Der wichtige Grenzfall: NMI feuert auch, wenn IFF1 vorher SCHON 0 war
// (die maskierbare IFF1-Signatur würde ihn verpassen).
TEST(EventBp, NmiWhenInterruptsAlreadyDisabled){
    Prev prev{true, 0x8000, /*iff1=*/false};     // Interrupts waren bereits aus
    EXPECT_EQ(C(0x0066, 0x7FFE, false, prev), Event::NMI);
    // … und ohne armNmi nicht:
    EXPECT_EQ(C(0x0066, 0x7FFE, false, prev, true, /*armNmi=*/false, false), Event::None);
}

TEST(EventBp, RetiAndRetn){
    Prev prev{true, 0x8000, false};
    EXPECT_EQ(C(0x01D2, 0x8000, false, prev, true, true, true, 0xED, 0x4D), Event::RETI);
    EXPECT_EQ(C(0x01D2, 0x8000, false, prev, true, true, true, 0xED, 0x45), Event::RETN);
    // disarmed:
    EXPECT_EQ(C(0x01D2, 0x8000, false, prev, true, true, /*armReti=*/false, 0xED, 0x4D), Event::None);
    // ED, aber kein RETI/RETN-Subopcode:
    EXPECT_EQ(C(0x01D2, 0x8000, false, prev, true, true, true, 0xED, 0xB0), Event::None);
}

TEST(EventBp, NoPrevGivesNoneForIntNmi){
    Prev prev;                                   // have=false
    EXPECT_EQ(C(0x1234, 0x7FFE, false, prev), Event::None);
    EXPECT_EQ(C(0x0066, 0x7FFE, false, prev), Event::None);
}

TEST(EventBp, OrdinaryInstructionNone){
    Prev prev{true, 0x8000, true};
    // normaler Folgebefehl: SP unverändert, IFF1 unverändert
    EXPECT_EQ(C(0x1235, 0x8000, true, prev), Event::None);
}

TEST(EventBp, PriorityNmiOverInterrupt){
    // Am NMI-Vektor schließt die Interrupt-Regel pc==0x0066 ohnehin aus → NMI gewinnt.
    Prev prev{true, 0x8000, true};
    EXPECT_EQ(C(0x0066, 0x7FFE, false, prev), Event::NMI);
}
