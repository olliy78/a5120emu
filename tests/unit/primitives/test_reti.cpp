/**
 * @file test_reti.cpp
 * @brief RETI-Erkennung und IUS/IEO-Verhalten von CTC, PIO und SIO.
 *
 * Im IM2-Betrieb hängen die Bausteine in einer Prioritätskette.  Wer eine
 * Interruptquittung bekommt (`getVector()`), setzt sein IUS-Bit („interrupt
 * under service") und nimmt es beim `RETI` der ISR wieder zurück
 * (`onRETI()` / `K1520Bus::signalRETI()`).  Solange IUS steht, quittiert der
 * Baustein KEINEN weiteren Interrupt derselben Quelle — bricht das, wird eine
 * ISR von ihrer eigenen Quelle unterbrochen, und der Fehler zeigt sich erst
 * viel später als verschluckter Handshake.
 *
 * Vorgeschichte: entstanden als loses `test_reti.cpp` im Projektwurzel-
 * verzeichnis (printf-Prüfungen, kein Build-Target, lief nie mit).  Am
 * 2026-08-07 nach GoogleTest überführt und registriert — dabei kam heraus, dass
 * seine IEO-Erwartung nicht dem implementierten Verhalten entspricht, siehe
 * `IeoBlocksOnPendingNotOnService` am Dateiende.
 */

#include <gtest/gtest.h>

#include "core/bus/k1520_bus.h"
#include "core/primitives/z80_ctc.h"
#include "core/primitives/z80_pio.h"
#include "core/primitives/z80_sio.h"

namespace {

/// CTC Kanal 0: Vektorbasis 0x20, Timer mit Vorteiler 16 und Konstante 5.
void armCtc(Z80CTC& ctc) {
    ctc.ioWrite(0, 0x20);   // D0=0 → Vektorbasis
    ctc.ioWrite(0, 0x85);   // IE=1, Timer, Vorteiler 16, Konstante folgt
    ctc.ioWrite(0, 5);      // 5 × 16 = 80 Takte
    ctc.setIEI(true);
}

/// PIO Port A: Vektor, Modus 1 (Eingabe), Interrupt freigegeben.
void armPio(Z80PIO& pio, uint8_t vector) {
    pio.ioWrite(1, vector); // Bit0=0 kennzeichnet die Vektorschreibung
    pio.ioWrite(1, 0x4F);   // Modus 1 (Eingabe)
    pio.ioWrite(1, 0x87);   // Interruptsteuerwort: IE=1
    pio.setIEI(true);
}

/// SIO Kanal A: Interrupt bei jedem Empfangszeichen, Vektorbasis 0x60 (WR2 von B).
void armSio(Z80SIO& sio) {
    sio.setIEI(true);
    sio.ioWrite(1, 0x01);   // WR0 (Kanal A): Zeiger auf WR1
    sio.ioWrite(1, 0x08);   // WR1 Bits[3:2]=10 → Interrupt on all received chars
    sio.ioWrite(3, 0x02);   // WR0 (Kanal B): Zeiger auf WR2
    sio.ioWrite(3, 0x60);   // WR2: Vektorbasis
}

}  // namespace

// ─── CTC ─────────────────────────────────────────────────────────────────────

TEST(RetiChain, CtcHoldsIusUntilRetiAndOnlyThenAcknowledgesAgain) {
    K1520Bus bus;
    Z80CTC ctc("CTC");
    bus.registerIO(&ctc, 0x00, 4);
    bus.setInterruptChain({&ctc});
    armCtc(ctc);
    bus.updateInterruptChain();

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    ASSERT_TRUE(ctc.hasInterrupt()) << "CTC meldete keinen Interrupt";
    EXPECT_FALSE(ctc.getIEO()) << "anstehender Interrupt muss IEO sperren";

    EXPECT_EQ(ctc.getVector(), 0x20) << "Vektor Kanal 0 = Basis|0";

    // Zweiter Zeitablauf WÄHREND der ISR: IUS steht, also keine neue Quittung.
    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_EQ(ctc.getVector(), 0xFF)
        << "CTC quittierte trotz gesetztem IUS erneut — ISR unterbricht sich selbst";

    ctc.onRETI();
    bus.updateInterruptChain();
    EXPECT_EQ(ctc.getVector(), 0x20)
        << "nach RETI muss der wartende Interrupt quittiert werden (IUS gelöscht)";
}

// ─── PIO ─────────────────────────────────────────────────────────────────────

TEST(RetiChain, PioHoldsIusUntilRetiAndOnlyThenAcknowledgesAgain) {
    K1520Bus bus;
    Z80PIO pio("PIO");
    bus.registerIO(&pio, 0x00, 4);
    bus.setInterruptChain({&pio});
    armPio(pio, 0xC4);
    bus.updateInterruptChain();

    pio.portAWrite(0x01);
    ASSERT_TRUE(pio.hasInterrupt()) << "PIO meldete keinen Interrupt";
    EXPECT_FALSE(pio.getIEO()) << "anstehender Interrupt muss IEO sperren";

    EXPECT_EQ(pio.getVector(), 0xC4);

    pio.portAWrite(0x02);                     // neue Daten während der ISR
    EXPECT_EQ(pio.getVector(), 0xFF)
        << "PIO quittierte trotz gesetztem IUS erneut";

    pio.onRETI();
    bus.updateInterruptChain();
    EXPECT_EQ(pio.getVector(), 0xC4) << "nach RETI wieder quittierbar";
}

// ─── SIO ─────────────────────────────────────────────────────────────────────

TEST(RetiChain, SioHoldsIusUntilRetiAndOnlyThenAcknowledgesAgain) {
    K1520Bus bus;
    Z80SIO sio("SIO");
    bus.registerIO(&sio, 0x00, 4);
    bus.setInterruptChain({&sio});
    armSio(sio);
    bus.updateInterruptChain();

    sio.channelA().rxByte(0x42);
    ASSERT_TRUE(sio.hasInterrupt()) << "SIO meldete keinen Interrupt";
    EXPECT_FALSE(sio.getIEO()) << "anstehender Interrupt muss IEO sperren";

    // Kanal A / Rx codiert 110 in den Vektorbits [3:1] → Basis 0x60 wird zu 0x6C.
    const uint8_t kChARx = 0x6C, kBase = 0x60;
    EXPECT_EQ(sio.getVector(), kChARx) << "Vektor für Kanal A Rx falsch codiert";

    sio.channelA().rxByte(0x43);              // zweites Zeichen während der ISR
    // Anders als CTC/PIO liefert der SIO bei „nichts quittierbar" die reine
    // Vektorbasis statt 0xFF — beides bedeutet „kein Quellcode gesetzt".
    EXPECT_EQ(sio.getVector(), kBase)
        << "SIO quittierte trotz gesetztem IUS erneut";

    sio.onRETI();
    bus.updateInterruptChain();
    EXPECT_EQ(sio.getVector(), kChARx) << "nach RETI wieder quittierbar";
}

// ─── Kette: CTC vor PIO ──────────────────────────────────────────────────────

TEST(RetiChain, BusAcknowledgeFollowsTheDaisyChainOrder) {
    K1520Bus bus;
    Z80CTC ctc("CTC");
    Z80PIO pio("PIO");
    bus.registerIO(&ctc, 0x00, 4);
    bus.registerIO(&pio, 0x04, 4);
    bus.setInterruptChain({&ctc, &pio});      // CTC hat Vorrang
    armCtc(ctc);
    armPio(pio, 0x90);
    bus.updateInterruptChain();

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    pio.portAWrite(0x01);
    ASSERT_TRUE(ctc.hasInterrupt());
    ASSERT_TRUE(pio.hasInterrupt());

    EXPECT_EQ(bus.interruptAcknowledge(), 0x20) << "erster Vektor kam nicht vom CTC";
    bus.signalRETI();                          // Ende der CTC-ISR

    EXPECT_EQ(bus.interruptAcknowledge(), 0x90) << "zweiter Vektor kam nicht vom PIO";
    bus.signalRETI();                          // Ende der PIO-ISR

    // Beide Quellen abgearbeitet → nichts steht mehr an, die Kette ist offen.
    EXPECT_FALSE(ctc.hasInterrupt());
    EXPECT_FALSE(pio.hasInterrupt());
    EXPECT_TRUE(ctc.getIEO());
    EXPECT_TRUE(pio.getIEO());
}


// ─── Anforderungsseite: hasInterrupt() bei gesetztem IUS ─────────────────────
//
// Neben dem IEO-Signal (unten) gibt es die zweite Frage: fordert ein Baustein
// ueberhaupt noch einen Interrupt an, waehrend er bedient wird?  Muss er NICHT —
// sonst zieht die Karte /INT, die Quittung findet in getVector() aber keinen
// vektorfaehigen Kanal und liefert den Fallback → Endlos-Sturm mit Pseudo-Vektor.
// Genau das brach den UDOS-Boot (origin/main f3b7ab1, doc/analyse_udos.md).
//
// Alle drei Bausteine halten diesen Vertrag: CTC ueber anyServiceable(), PIO seit
// f3b7ab1, SIO seit dem Angleich vom 2026-08-07.  Die KETTENseite (getIEO) ist
// davon unberuehrt — siehe IeoBlocksOnPendingNotOnService am Dateiende.

TEST(RetiChain, CtcRequestIsIusGatedUntilReti) {
    Z80CTC ctc("CTC");
    armCtc(ctc);
    for (int i = 0; i < 80; ++i) ctc.clockTick();
    ASSERT_TRUE(ctc.hasInterrupt());

    ctc.getVector();                            // IUS gesetzt
    for (int i = 0; i < 80; ++i) ctc.clockTick();  // neuer Zeitablauf waehrend der ISR
    EXPECT_FALSE(ctc.hasInterrupt())
        << "CTC fordert waehrend der eigenen ISR erneut an — Pseudo-Vektor-Sturm droht";

    ctc.onRETI();
    EXPECT_TRUE(ctc.hasInterrupt()) << "nach RETI muss die wartende Anforderung wieder gelten";
}

TEST(RetiChain, PioRequestIsIusGatedUntilReti) {
    Z80PIO pio("PIO");
    armPio(pio, 0xC4);
    pio.portAWrite(0x01);
    ASSERT_TRUE(pio.hasInterrupt());

    pio.getVector();                            // IUS gesetzt
    pio.portAWrite(0x02);                       // neue Daten waehrend der ISR
    EXPECT_FALSE(pio.hasInterrupt())
        << "PIO fordert waehrend der eigenen ISR erneut an — das war der "
           "UDOS-Pseudo-Interrupt-Sturm (origin/main f3b7ab1)";

    pio.onRETI();
    EXPECT_TRUE(pio.hasInterrupt()) << "nach RETI muss die wartende Anforderung wieder gelten";
}

TEST(RetiChain, SioRequestIsIusGatedUntilReti) {
    Z80SIO sio("SIO");
    armSio(sio);
    sio.channelA().rxByte(0x42);
    ASSERT_TRUE(sio.hasInterrupt());

    sio.getVector();                            // IUS gesetzt
    sio.channelA().rxByte(0x43);                // zweites Zeichen waehrend der ISR
    EXPECT_FALSE(sio.hasInterrupt())
        << "SIO fordert waehrend der eigenen ISR erneut an — die Quittung findet "
           "dann keinen vektorfaehigen Kanal und die ISR wird endlos wiederholt";

    sio.onRETI();
    EXPECT_TRUE(sio.hasInterrupt()) << "nach RETI muss die wartende Anforderung wieder gelten";
}

// ─── Dokumentierte Abweichung vom Z80-Daisy-Chain-Standard ───────────────────

/**
 * @test RetiChain/IeoBlocksOnPendingNotOnService
 * @brief Hält fest, dass IEO NUR bei anstehendem, nicht bei laufendem Interrupt sperrt.
 *
 * Alle drei Bausteine implementieren `getIEO()` als „IEI && kein Interrupt
 * ANSTEHEND" (`z80_ctc.cpp:381`, `z80_pio.cpp:382`, `z80_sio.cpp:192`).  Nach
 * der Quittung ist `pending` gelöscht und `ius` gesetzt — IEO gibt also wieder
 * frei, obwohl die ISR noch läuft.  Auf echter Hardware bliebe IEO gesperrt,
 * bis `RETI` das IUS zurücknimmt; nachrangige Bausteine könnten die laufende
 * ISR also nicht unterbrechen.
 *
 * Der Schutz greift hier eine Ebene tiefer: `getVector()` verweigert die
 * Quittung, solange IUS steht (die Tests oben) — die Verschachtelung wird also
 * je Baustein verhindert, nicht über die Kette.  Für den A5120 hat sich das als
 * ausreichend erwiesen (voller CP/A-Boot + alle Integrationstests grün).
 *
 * Dieser Test ist bewusst ein WÄCHTER DES IST-ZUSTANDS: schlägt er fehl, wurde
 * das IEO-Verhalten geändert — dann gehört dieser Kommentar korrigiert und die
 * Kette braucht neue Tests.  Bewertung: `doc/testsystem_rework.md` §8.
 */
TEST(RetiChain, IeoBlocksOnPendingNotOnService) {
    K1520Bus bus;
    Z80CTC ctc("CTC");
    bus.registerIO(&ctc, 0x00, 4);
    bus.setInterruptChain({&ctc});
    armCtc(ctc);
    bus.updateInterruptChain();

    for (int i = 0; i < 80; ++i) ctc.clockTick();
    EXPECT_FALSE(ctc.getIEO()) << "anstehender Interrupt sperrt IEO";

    ctc.getVector();                           // pending → 0, ius → 1
    EXPECT_TRUE(ctc.getIEO())
        << "IST-Zustand: IEO gibt nach der Quittung wieder frei (IUS wird ignoriert)";

    ctc.onRETI();
    EXPECT_TRUE(ctc.getIEO());
}
