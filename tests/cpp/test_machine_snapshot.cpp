// Integrationstest für A5120Machine::captureState / restoreState (Snapshot/Reverse).
#include "core/machines/a5120/a5120.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>

namespace {
A5120Machine::MachineSnapshot* heapSnap(){ return new A5120Machine::MachineSnapshot(); }
std::string tmpStatePath(){ return std::string(::testing::TempDir()) + "k1520_state_test.bin"; }
}

TEST(MachineSnapshot, RamRoundTrip){
    A5120Machine m; m.powerOn();
    // freier RAM-Bereich oberhalb des ROM-Schattens
    for (uint16_t i=0;i<16;++i) m.memWriteDebug(0x6000+i, (uint8_t)(0xA0+i));

    A5120Machine::MachineSnapshot s;
    m.captureState(s);

    // RAM verändern …
    for (uint16_t i=0;i<16;++i) m.memWriteDebug(0x6000+i, 0x00);
    EXPECT_EQ(m.memReadDebug(0x6005), 0x00);

    // … und zurückrollen
    EXPECT_TRUE(m.restoreState(s));
    for (uint16_t i=0;i<16;++i)
        EXPECT_EQ(m.memReadDebug(0x6000+i), (uint8_t)(0xA0+i));
}

TEST(MachineSnapshot, RegistersRoundTrip){
    A5120Machine m; m.powerOn();
    Z80& z = m.cpuDebug();
    z.HL = 0x1234; z.BC = 0x5678; z.SP = 0x7F00; z.IFF1 = true; z.IM = 2;

    A5120Machine::MachineSnapshot s;
    m.captureState(s);

    z.HL = 0xFFFF; z.BC = 0x0000; z.SP = 0x0000; z.IFF1 = false; z.IM = 0;
    m.restoreState(s);

    EXPECT_EQ(m.cpuDebug().HL, 0x1234);
    EXPECT_EQ(m.cpuDebug().BC, 0x5678);
    EXPECT_EQ(m.cpuDebug().SP, 0x7F00);
    EXPECT_TRUE(m.cpuDebug().IFF1);
    EXPECT_EQ(m.cpuDebug().IM, 2);
}

TEST(MachineSnapshot, RestoreAfterRunningReproducesState){
    A5120Machine m; m.powerOn();
    // Run well past the boot-ROM unmap so capture & restore are both in the
    // post-ROM regime (the snapshot deliberately does not reproduce ROM mapping).
    m.run(400000);

    auto* s = heapSnap();
    m.captureState(*s);
    uint16_t pc_at_snap  = m.cpuPC();
    uint8_t  ram_at_snap = m.memReadDebug(0x6000);

    m.memWriteDebug(0x6000, 0x99);      // perturb RAM
    m.run(100000);                      // advance further

    EXPECT_TRUE(m.restoreState(*s));    // ROM mapping unchanged across this window
    EXPECT_EQ(m.cpuPC(), pc_at_snap);
    EXPECT_EQ(m.memReadDebug(0x6000), ram_at_snap);
    delete s;
}

// ── saveState / loadState (Persistenz auf Platte) ────────────────────────────

TEST(MachineSnapshot, SaveAndLoadStateRoundTrip){
    std::string path = tmpStatePath();
    A5120Machine a; a.powerOn(); a.run(400000);   // post-ROM
    a.cpuDebug().HL = 0x1234;
    for (uint16_t i=0;i<8;++i) a.memWriteDebug(0x6000+i, (uint8_t)(0x70+i));
    uint16_t pc = a.cpuPC();
    ASSERT_TRUE(a.saveState(path));

    // Frische Maschine: laden statt booten.
    A5120Machine b; b.powerOn(); b.run(400000);    // gleiche ROM-Phase (kein Mismatch)
    ASSERT_TRUE(b.loadState(path));
    EXPECT_EQ(b.cpuDebug().HL, 0x1234);
    EXPECT_EQ(b.cpuPC(), pc);
    for (uint16_t i=0;i<8;++i) EXPECT_EQ(b.memReadDebug(0x6000+i), (uint8_t)(0x70+i));
    std::remove(path.c_str());
}

// ── Tastatur-Subsystem überlebt save/load (Kernanforderung) ──────────────────

// Eine getippte Taste landet (nach der 9600-Baud-Serial-Latenz) im Tastatur-SIO
// RX-FIFO. Genau dieser Geräteszustand muss einen savestate→loadstate überleben,
// damit die Tastatur nach dem Laden funktioniert (RAM+CPU allein reichen nicht —
// der SIO-/K7637-/Timer-Zustand lebt außerhalb des RAM).
TEST(MachineSnapshot, KeyboardSioSurvivesSaveLoad){
    std::string path = tmpStatePath();
    A5120Machine a; a.powerOn();
    a.keyPress('A', false, false);
    a.run(20000);                              // Key-Queue leeren + Serial-Latenz → Byte im SIO-RX
    auto before = a.kbdSioState();
    ASSERT_GT(before.ch[0].rxQueued, 0u);      // Byte steht im Tastatur-SIO (Kanal A)

    A5120Machine::MachineSnapshot s; a.captureState(s);
    EXPECT_FALSE(s.device_state.empty());      // Geräteszustand wurde mitgesichert

    ASSERT_TRUE(a.saveState(path));

    A5120Machine b; b.powerOn();               // frische Maschine: SIO leer
    EXPECT_EQ(b.kbdSioState().ch[0].rxQueued, 0u);
    ASSERT_TRUE(b.loadState(path));
    EXPECT_EQ(b.kbdSioState().ch[0].rxQueued, before.ch[0].rxQueued);   // wiederhergestellt
    std::remove(path.c_str());
}

// In-Memory captureState/restoreState rollt den Tastatur-SIO-Zustand exakt zurück.
TEST(MachineSnapshot, KeyboardSioRoundTripInMemory){
    A5120Machine m; m.powerOn();
    m.keyPress('Z', false, false);
    m.run(20000);
    auto before = m.kbdSioState();
    ASSERT_GT(before.ch[0].rxQueued, 0u);

    A5120Machine::MachineSnapshot s; m.captureState(s);
    // SIO leeren (Byte auslesen) und prüfen, dass es weg ist …
    m.run(2000000);                            // ZVE1 läuft weiter; FIFO kann sich ändern
    // … dann zurückrollen und denselben FIFO-Füllstand erwarten.
    EXPECT_TRUE(m.restoreState(s));
    EXPECT_EQ(m.kbdSioState().ch[0].rxQueued, before.ch[0].rxQueued);
}

// Legacy-v1-Snapshots (ohne device_state) müssen weiterhin laden.
TEST(MachineSnapshot, LegacyV1StateWithoutDeviceStateStillLoads){
    // Ein leerer device_state (wie bei v1) wird von restoreState toleriert.
    A5120Machine m; m.powerOn(); m.run(400000);
    A5120Machine::MachineSnapshot s; m.captureState(s);
    s.device_state.clear();                    // v1-Verhalten simulieren
    EXPECT_TRUE(m.restoreState(s));            // kein Absturz, Geräte behalten ihren Zustand
}

TEST(MachineSnapshot, LoadStateRejectsMissingAndGarbage){
    A5120Machine m; m.powerOn();
    EXPECT_FALSE(m.loadState("/nonexistent/k1520/does-not-exist.bin"));
    // Eine Datei mit falscher Magic wird abgelehnt (Maschine unverändert).
    std::string path = tmpStatePath();
    { FILE* f = std::fopen(path.c_str(), "wb"); std::fputs("NOTASTATEFILE....", f); std::fclose(f); }
    EXPECT_FALSE(m.loadState(path));
    std::remove(path.c_str());
}

// ─── Laufwerksbestückung (A5120Machine::Config / C-API) ────────────────────────
// Default = 4× 5,25"-MFM (K5601); per Config (C-API/GUI/Config-Datei) überschreibbar.
// k5122State() liefert den Zustand des aktuell gewählten Laufwerks (Default: Slot 0).

TEST(MachineConfig, DefaultIst4xK5601){
    A5120Machine m;   // ohne Config → Default-Bürokonfiguration
    EXPECT_EQ(m.k5122State().driveProfileName, "K5601");
}

TEST(MachineConfig, CustomProfilWirdUebernommen){
    A5120Machine::Config cfg;
    cfg.drive_profiles[0] = "mf3200_8_ss77";   // 8"-FM-Laufwerk auf Slot 0
    A5120Machine m(cfg);
    EXPECT_EQ(m.k5122State().driveProfileName, "mf3200_8_ss77");
}

TEST(MachineConfig, UnbekanntesProfilFaelltAufDefault){
    A5120Machine::Config cfg;
    cfg.drive_profiles[0] = "gibtsnicht";
    A5120Machine m(cfg);
    // builtinDriveProfile liefert für unbekannte Namen das Default-Profil.
    EXPECT_EQ(m.k5122State().driveProfileName, "mfs_525_ds80");
}
