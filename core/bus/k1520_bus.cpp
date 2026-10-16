#include "k1520_bus.h"
#include "core/logger.h"
#include <stdexcept>
#include <algorithm>

void K1520Bus::registerIO(BusDevice* dev, uint8_t basePort, uint8_t numPorts) {
    for (int i = 0; i < numPorts; ++i) {
        uint8_t p = basePort + i;
        if (io_map_[p] && io_map_[p] != dev)
            throw std::runtime_error("K1520Bus: I/O port conflict");
        io_map_[p] = dev;
    }
    LOG_DEBUG("K1520Bus", "registerIO: %s ports 0x%02X-0x%02X (%u ports)",
              dev->deviceName(), basePort, (uint8_t)(basePort + numPorts - 1), numPorts);
}

void K1520Bus::registerMem(MemDevice* dev, uint16_t base, uint16_t size) {
    mem_regions_.push_back({base, size, dev});
    rebuildPageTable();
    LOG_DEBUG("K1520Bus", "registerMem: Geraet @0x%04X-0x%04X (Groesse=%u)",
              base, (uint16_t)(base + size - 1), size);
}

void K1520Bus::unregisterMem(MemDevice* dev) {
    mem_regions_.erase(
        std::remove_if(mem_regions_.begin(), mem_regions_.end(),
            [dev](const MemRegion& r) { return r.dev == dev; }),
        mem_regions_.end());
    rebuildPageTable();
    LOG_DEBUG("K1520Bus", "unregisterMem: Geraet entfernt");
}

// Baut die 256-B-Seiten-Dispatch-Tabelle aus mem_regions_ neu auf.  Nur bei
// register/unregisterMem aufgerufen (selten).  Repliziert exakt die Semantik
// des linearen Scans: Lesen = LETZTES registriertes lesbares Geraet gewinnt;
// Schreiben = Broadcast an ALLE schreibbaren Geraete.  Wird eine Region gefunden,
// die nicht 256-B-ausgerichtet ist oder mehr als kMaxWriteFanout Schreiber auf
// eine Seite legt, wird die Tabelle als ungueltig markiert → Zugriffe nutzen den
// linearen Fallback (immer korrekt).
void K1520Bus::rebuildPageTable() {
    page_table_.fill(PageEntry{});
    page_table_valid_ = true;
    for (const auto& r : mem_regions_) {
        if ((r.base & ((1 << kPageShift) - 1)) || (r.size & ((1 << kPageShift) - 1))) {
            page_table_valid_ = false;   // nicht seitenausgerichtet → Fallback
            return;
        }
        const bool readable = r.dev->isReadable();
        const bool writable = r.dev->isWritable();
        const int first = r.base >> kPageShift;
        const int last  = (int)((uint32_t)r.base + r.size - 1) >> kPageShift;
        for (int p = first; p <= last; ++p) {
            PageEntry& e = page_table_[p];
            if (readable) e.read = r.dev;                 // last-registered wins
            if (writable) {
                if (e.write_count >= kMaxWriteFanout) {   // zu viele Broadcast-Ziele
                    page_table_valid_ = false;
                    return;
                }
                e.write[e.write_count++] = r.dev;
            }
        }
    }
}

void K1520Bus::setInterruptChain(std::initializer_list<InterruptSlave*> chain) {
    int_chain_.assign(chain.begin(), chain.end());
    int_dirty_ = true;
    LOG_DEBUG("K1520Bus", "Interrupt-Daisy-Chain: %zu Geraete gesetzt", int_chain_.size());
}

// Welcher Vorrangspeicher zieht fuer @p addr /MEMDI?  nullptr = keiner (Normalfall).
MemdiDriver* K1520Bus::memdiDriverFor(uint16_t addr) {
    for (auto* d : memdi_drivers_)
        if (d->drivesMemdi(addr)) return d;
    return nullptr;
}

uint8_t K1520Bus::memRead(uint16_t addr) {
    // Bus-/MEMDI (X1 C09) ist ein JE ZUGRIFF getriebenes Signal: ein Vorrangspeicher
    // (Erweiterungsmodul) zieht es allein aus der anliegenden Adresse und bedient den
    // Zyklus selbst; die auf /MEMDI gebrueckten K3526-Gruppen bleiben still.
    //
    // WAECHTER: Der BS-PIO (K2526 Port A Bit7) treibt NICHT diese Leitung, sondern
    // MEMDI1/2 der Rueckverdrahtung (Koppelbus), auf die am A5120 keine Gruppe hoert.
    // Beides war frueher EIN Flag — und weil HARDYs MEMDI-Test A7 setzt und danach
    // weiterlaeuft (EI/RET, Stack, BDOS), war /MEMDI deshalb ganz wirkungslos gemacht
    // worden.  Ein globales Lese-Gate hier liesse die CPU 0xFF (RST 38H) holen.
    // Ohne angemeldeten Vorrangspeicher kostet das genau einen Vergleich.
    if (!memdi_drivers_.empty()) {
        if (MemdiDriver* d = memdiDriverFor(addr)) {
            memdi_ = true;
            const uint8_t v = d->memRead(addr);
            memdi_ = false;
            if (trace_cb_) trace_cb_(false, true, addr, v);
            LOG_TRACE("K1520Bus", "MEM RD 0x%04X => 0x%02X (/MEMDI, Vorrangspeicher)", addr, v);
            return v;
        }
    }

    // Take the last READABLE device that covers this address.
    // Devices with isReadable()=false (e.g. K7024 with Lesesperre active)
    // do not drive the data bus and are skipped.
    // Schnellpfad: O(1) ueber die Seiten-Dispatch-Tabelle (Aufbau s. rebuildPageTable).
    MemDevice* hit;
    if (page_table_valid_) {
        hit = page_table_[addr >> kPageShift].read;
    } else {
        hit = nullptr;                       // linearer Fallback (nicht seitenausgerichtet)
        for (auto& r : mem_regions_)
            if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
                if (r.dev->isReadable())
                    hit = r.dev;
    }
    uint8_t val = hit ? hit->memRead(addr) : 0xFF;
    if (trace_cb_) trace_cb_(false, true, addr, val);
    LOG_TRACE("K1520Bus", "MEM RD 0x%04X => 0x%02X%s", addr, val, hit ? "" : " (kein Geraet)");
    return val;
}

void K1520Bus::memWrite(uint16_t addr, uint8_t data) {
    // /MEMDI je Zugriff (s. memRead): zieht ein Vorrangspeicher es, bekommt er den
    // Schreibzyklus; der Broadcast unten laeuft trotzdem an alle, und wer auf /MEMDI
    // hoert (K3526-Gruppe am Bus-/MEMDI), verwirft ihn ueber memdiActive().
    MemdiDriver* md = memdi_drivers_.empty() ? nullptr : memdiDriverFor(addr);
    memdi_ = md != nullptr;
    // Write to ALL writable devices that cover this address.
    // On real K1520 hardware the write signal (/WR + /MREQ) is broadcast on the
    // bus; every device whose address decoder fires will latch the data.
    // This is required for the K7024 Lesesperre configuration:
    //   - K7024 (isWritable=true, isReadable=false): receives write for screen update
    //   - K3526 (isWritable=true, isReadable=true):  also receives write for storage
    bool wrote = false;
    if (page_table_valid_) {                 // Schnellpfad: O(1) Broadcast-Ziele
        const PageEntry& e = page_table_[addr >> kPageShift];
        for (uint8_t i = 0; i < e.write_count; ++i)
            e.write[i]->memWrite(addr, data);
        wrote = e.write_count != 0;
    } else {
        for (auto& r : mem_regions_)         // linearer Fallback
            if (addr >= r.base && addr < static_cast<uint32_t>(r.base + r.size))
                if (r.dev->isWritable()) {
                    r.dev->memWrite(addr, data);
                    wrote = true;
                }
    }
    if (md) { md->memWrite(addr, data); wrote = true; }
    memdi_ = false;
    if (trace_cb_) trace_cb_(false, false, addr, data);
    LOG_TRACE("K1520Bus", "MEM WR 0x%04X <= 0x%02X%s", addr, data, wrote ? "" : " (kein beschreibbares Geraet)");
}

uint8_t K1520Bus::ioRead(uint16_t addr) {
    io_addr_ = addr;
    const uint8_t port = static_cast<uint8_t>(addr);
    if (iodi_) {
        LOG_TRACE("K1520Bus", "ioRead 0x%02X: /IODI aktiv → 0xFF", port);
        return 0xFF;
    }
    uint8_t val = io_map_[port] ? io_map_[port]->ioRead(port) : 0xFF;
    if (io_map_[port]) int_dirty_ = true;   // Status-/Daten-Read kann IRQ loeschen
    if (trace_cb_) trace_cb_(true, true, port, val);
    if (!io_map_[port])
        LOG_DEBUG("K1520Bus", "I/O RD 0x%02X => 0xFF (kein Geraet registriert)", port);
    return val;
}

void K1520Bus::ioWrite(uint16_t addr, uint8_t data) {
    io_addr_ = addr;
    const uint8_t port = static_cast<uint8_t>(addr);
    if (iodi_) {
        LOG_TRACE("K1520Bus", "ioWrite 0x%02X=0x%02X: /IODI aktiv → ignoriert", port, data);
        return;
    }
    if (!io_map_[port])
        LOG_DEBUG("K1520Bus", "I/O WR 0x%02X <= 0x%02X (kein Geraet registriert)", port, data);
    if (io_map_[port]) { io_map_[port]->ioWrite(port, data); int_dirty_ = true; }
    if (trace_cb_) trace_cb_(true, false, port, data);
}

void K1520Bus::updateInterruptChain() {
    // Schnellpfad: Ohne Zustandsaenderung (kein I/O, kein CTC-ZC/TO, keine
    // serielle/Floppy-IRQ, keine Quittung/RETI seit dem letzten Walk) ist der
    // gecachte Chain-Zustand (int_asserted_ + iei je Geraet) noch gueltig.
    if (!int_dirty_) return;
    int_dirty_ = false;

    bool iei = true;
    for (auto* dev : int_chain_) {
        dev->setIEI(iei);
        if (iei && dev->hasInterrupt())
            iei = false;
        iei = iei && dev->getIEO();
    }
    bool was_int = int_asserted_;
    int_asserted_ = false;
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) { int_asserted_ = true; break; }
    if (int_asserted_ != was_int)
        LOG_DEBUG("K1520Bus", "INT: %s → %s", was_int ? "aktiv" : "inaktiv",
                  int_asserted_ ? "aktiv" : "inaktiv");
}

uint8_t K1520Bus::interruptAcknowledge() {
    int_dirty_ = true;   // IUS gesetzt → Chain neu bewerten
    ++last_int_ack_.count;
    for (auto* dev : int_chain_)
        if (dev->hasInterrupt()) {
            // Namen VOR getVector() holen — die Quittung loescht die Anforderung.
            const char* who = dev->intDeviceName();
            uint8_t vec = dev->getVector();
            last_int_ack_.vector   = vec;
            last_int_ack_.spurious = false;
            last_int_ack_.device   = who;
            LOG_DEBUG("K1520Bus", "INT-Quittung: Vektor=0x%02X (%s)", vec, who);
            return vec;
        }
    // Kein Geraet hat geantwortet: der 0xFF ist der offene Bus, KEIN programmierter
    // Vektor.  Ein Fremd-OS ohne Tabelleneintrag fuer 0xFF laeuft damit ins Leere —
    // deshalb hier ausdruecklich als „spurious" protokolliert (Debugger `bint`).
    last_int_ack_.vector   = 0xFF;
    last_int_ack_.spurious = true;
    last_int_ack_.device   = nullptr;
    LOG_DEBUG("K1520Bus", "INT-Quittung: SPURIOUS (kein Geraet) → 0xFF");
    return 0xFF;
}

void K1520Bus::assertINT()    {
    if (!int_asserted_) LOG_DEBUG("K1520Bus", "assertINT()");
    int_asserted_ = true;
    int_dirty_    = true;
}
void K1520Bus::releaseINT()   {
    if (int_asserted_) LOG_DEBUG("K1520Bus", "releaseINT()");
    int_asserted_ = false;
    int_dirty_    = true;
}
void K1520Bus::assertNMI()    {
    LOG_DEBUG("K1520Bus", "assertNMI()");
    nmi_pending_ = true;
    int_dirty_   = true;
}
void K1520Bus::clearNMI()     {
    nmi_pending_ = false;
    int_dirty_   = true;
}
void K1520Bus::assertRESET()  {
    LOG_INFO("K1520Bus", "assertRESET()");
    reset_asserted_ = true;
}

void K1520Bus::signalRETI() {
    LOG_DEBUG("K1520Bus", "RETI-Signal: alle Geraete in Daisy-Chain benachrichtigt");
    for (auto* dev : int_chain_)
        dev->onRETI();
    int_dirty_ = true;   // IUS geloescht → Chain neu bewerten
    updateInterruptChain();
}

void K1520Bus::assertWAIT()  {
    if (!wait_asserted_) LOG_DEBUG("K1520Bus", "assertWAIT()");
    wait_asserted_ = true;
}
void K1520Bus::releaseWAIT() {
    if (wait_asserted_) LOG_DEBUG("K1520Bus", "releaseWAIT()");
    wait_asserted_ = false;
}

void K1520Bus::assertBUSRQ() {
    if (!busrq_asserted_) LOG_DEBUG("K1520Bus", "assertBUSRQ(): ZVE2-DMA fordert Bus an");
    busrq_asserted_ = true;
}

void K1520Bus::releaseBUSRQ() {
    if (busrq_asserted_) LOG_DEBUG("K1520Bus", "releaseBUSRQ(): DMA abgeschlossen, ZVE1 uebernimmt");
    busrq_asserted_ = false;
}
