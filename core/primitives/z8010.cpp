/**
 * @file z8010.cpp
 * @brief UB8010/Z8010-MMU — Übersetzung, Verletzungslogik, Befehle.  Annahmen [A1]–[A15]
 *        und Quellen: z8010.h, doc/p8000/z8010_mmu.md.
 * @license MIT
 */
#include "z8010.h"

#include <cstring>
#include <type_traits>

namespace {
constexpr uint8_t ST_STACK    = 0x9;
constexpr uint8_t ST_INSTR    = 0xC;
constexpr uint8_t ST_IF1      = 0xD;
constexpr uint8_t SAVE_VERSION = 1;

bool istSpeicherzyklus(uint8_t st) { return st >= 0x8 && st <= 0xD; }   // [TB] §4.1 Nr. 1
}

Z8010::Z8010() { powerOn(); }

void Z8010::powerOn() {
    s_ = S{};
}

void Z8010::reset(bool csAktiv) {
    s_.mr = csAktiv ? MR_MSEN : 0;   // [TB] §7.2.1: /CS beim Reset ⇒ MSEN, TRNS = 0
    s_.vtr = 0;
    s_.dscr = 0;
    s_.supBisBefehlsende = 0;
    s_.befehl = BEFEHL_FREI;
    setzeSegt(false);
}

void Z8010::setzeSegt(bool an) {
    const uint8_t neu = an ? 1 : 0;
    if (s_.segt == neu) return;
    s_.segt = neu;
    if (onSegt) onSegt(an);
}

void Z8010::befehlsgrenze() {
    s_.supBisBefehlsende = 0;
    s_.befehl = BEFEHL_FREI;
}

// ─── Buszyklus ───────────────────────────────────────────────────────────────

Z8010::Probe Z8010::probe(const Zyklus& z) const {
    Probe p;
    const uint8_t st = z.status & 0x0F;
    if (!istSpeicherzyklus(st) || !(s_.mr & MR_MSEN)) return p;
    const uint8_t seg = z.seg & 0x7F;
    const uint8_t offHi = uint8_t(z.offset >> 8);

    // TRNS = 0: Durchreichen ohne jede Prüfung, URS/MST ignoriert ([TB] §4.1 Nr. 3).
    if (!(s_.mr & MR_TRNS)) {
        p.adresse = true;
        p.phys = (uint32_t(seg) << 16) | z.offset;   // A23 = L
        return p;
    }
    // URS = SN6, MST ⇒ NMS = N/S — sonst Tri-State, keine Wirkung ([TB] §4.1 Nr. 4/5).
    if (((s_.mr & MR_URS) != 0) != ((seg & 0x40) != 0) ||
        ((s_.mr & MR_MST) && ((s_.mr & MR_NMS) != 0) != z.nsHigh))
        return p;

    const Deskriptor& d = s_.sdr[seg & 0x3F];
    p.adresse = true;
    p.geprueft = true;
    p.phys = (uint32_t(uint16_t(d.basis + offHi)) << 8) | (z.offset & 0xFF);   // 16-Bit-Addition

    const bool schreiben = !z.lesen;
    const bool holen = st == ST_INSTR || st == ST_IF1;
    const bool stapel = (d.attr & ATTR_DIRW) != 0;
    if (schreiben && (d.attr & ATTR_RD))  p.verletzung |= VTR_RDV;
    if (z.nsHigh && (d.attr & ATTR_SYS))  p.verletzung |= VTR_SYSV;
    if (stapel ? offHi < d.limit : offHi > d.limit) p.verletzung |= VTR_SLV;
    if (!holen && (d.attr & ATTR_EXC))    p.verletzung |= VTR_EXCV;
    if (z.dma) {
        p.dmaiVerletzung = (d.attr & ATTR_DMAI) != 0;
    } else {
        if (d.attr & ATTR_CPUI) p.verletzung |= VTR_CPUIV;
        p.warnung = schreiben && stapel && offHi == d.limit;
    }
    return p;
}

Z8010::Ergebnis Z8010::zyklus(const Zyklus& z) {
    Ergebnis e;
    const uint8_t st = z.status & 0x0F;
    if (!istSpeicherzyklus(st)) return e;   // Spezial-E/A über kommando*, Quittung über segtQuittung()

    const bool msen = (s_.mr & MR_MSEN) != 0;
    const uint8_t seg = z.seg & 0x7F;
    const uint8_t offHi = uint8_t(z.offset >> 8);

    // Befehlsgrenze und unechtes Holen [A1][A2]; ISN/IOFF mitlaufend [A8].
    bool unecht = false;
    if (!z.dma && st == ST_IF1) {
        unecht = s_.segt != 0;
        befehlsgrenze();
    }

    // SUP aus einer früheren Verletzung desselben Befehls [A3].
    if (!z.dma && s_.supBisBefehlsende) e.sup = true;

    // Erst nach der Prüfung übernehmen: verletzt dieses Holen, bleibt der Vorgänger in ISN/IOFF.
    auto merkeHolen = [&] {
        if (!z.dma && st == ST_IF1 && !unecht && msen) {
            s_.if1Seg = seg;
            s_.if1Off = offHi;
        }
    };

    const Probe pr = probe(z);
    e.adresse = pr.adresse;
    e.phys = pr.phys;
    if (!pr.geprueft) { merkeHolen(); return e; }   // Tri-State oder Durchreichen (TRNS = 0)

    if (z.dma) {   // [A14]: nur SUP im eigenen Zyklus, kein Zustand, kein REF/CHG
        if (pr.verletzung || pr.dmaiVerletzung) e.sup = true;
        return e;
    }
    Deskriptor& d = s_.sdr[seg & 0x3F];
    const bool schreiben = !z.lesen;
    const uint8_t v = pr.verletzung;
    const bool warnung = pr.warnung;

    if (unecht) {   // [A2]
        if (v) e.sup = true;
    } else if (v || warnung) {
        if (v) { e.sup = true; s_.supBisBefehlsende = 1; }
        const bool sysStapel = st == ST_STACK && !z.nsHigh;
        const uint8_t vtr = s_.vtr;
        if (vtr == 0) {
            // Normal → einfacher Violationszustand: Statusregister festhalten.
            s_.vtr = v | (warnung ? VTR_PWW : 0);   // [A5]
            s_.vsn = seg & 0x3F;                    // [A9]
            s_.voff = offHi;
            s_.bcsr = uint8_t((z.nsHigh ? BCSR_NS : 0) | (z.lesen ? BCSR_RW : 0) | st);
            s_.isn = s_.if1Seg;
            s_.ioff = s_.if1Off;
            s_.befehl = BEFEHL_ERSTMELDUNG;
            setzeSegt(true);
        } else if (s_.befehl == BEFEHL_ERSTMELDUNG) {
            s_.vtr |= v;                            // [A4] gleicher Befehl: Flags sammeln
        } else if (s_.befehl == BEFEHL_FREI) {
            // Späterer Befehl ([TB] §5.1/5.2, Anhang A).
            const bool fatl = (vtr & VTR_FATL) != 0;
            const bool sww = (vtr & VTR_SWW) != 0;
            if (!fatl) {
                if (!v && sysStapel) {
                    if (!sww) {                     // einfach → SWW, Trap ohne SUP
                        s_.vtr |= VTR_SWW;
                        s_.befehl = BEFEHL_WECHSEL;
                        setzeSegt(true);
                    }                               // im SWW-Zustand: nichts
                } else {                            // → FATL bzw. SWW/FATL [A6]
                    s_.vtr |= VTR_FATL;
                    s_.befehl = BEFEHL_WECHSEL;
                    setzeSegt(true);
                }
            }
            // FATL, SWW/FATL: Verletzung nur SUP (oben), Schreibwarnung nichts.
        }
        // BEFEHL_WECHSEL: kein zweiter Zustandswechsel im selben Befehl.
    }

    if (!e.sup) {   // [A7]
        d.attr |= ATTR_REF;
        if (schreiben) d.attr |= ATTR_CHG;
    }
    merkeHolen();
    return e;
}

Z8010::Kennung Z8010::segtQuittung() {
    Kennung k;
    if (s_.mr & MR_MSEN) {
        k.maske = uint16_t(1u << (8 + (s_.mr & MR_ID)));
        if (s_.segt) k.wert = k.maske;
    }
    setzeSegt(false);
    befehlsgrenze();   // [A1]
    return k;
}

// ─── Spezial-E/A ─────────────────────────────────────────────────────────────

uint8_t Z8010::sdrZugriff(uint8_t code, bool schreiben, uint8_t daten) {
    Deskriptor& d = s_.sdr[s_.sar & 63];
    auto zugriff = [&](uint8_t f) -> uint8_t {
        switch (f & 3) {
        case 0: if (schreiben) d.basis = uint16_t((d.basis & 0x00FF) | (daten << 8)); return uint8_t(d.basis >> 8);
        case 1: if (schreiben) d.basis = uint16_t((d.basis & 0xFF00) | daten);        return uint8_t(d.basis);
        case 2: if (schreiben) d.limit = daten; return d.limit;
        default: if (schreiben) d.attr = daten; return d.attr;
        }
    };
    auto naechsterSar = [&] { s_.sar = uint8_t((s_.sar + 1) & 63); };   // 63 → 0 ([TB] §6.2.13)
    uint8_t r = 0;
    switch (code) {
    case CMD_BASIS:
    case CMD_BASIS_INC:   // [A13]
        r = zugriff(s_.dscr);
        s_.dscr = uint8_t(s_.dscr + 1);
        if (s_.dscr >= 2) {
            s_.dscr = 0;
            if (code == CMD_BASIS_INC) naechsterSar();
        }
        break;
    case CMD_LIMIT:
    case CMD_LIMIT_INC:
        s_.dscr = 2;
        r = zugriff(2);
        s_.dscr = 0;
        if (code == CMD_LIMIT_INC) naechsterSar();
        break;
    case CMD_ATTR:
    case CMD_ATTR_INC:
        s_.dscr = 3;
        r = zugriff(3);
        s_.dscr = 0;
        if (code == CMD_ATTR_INC) naechsterSar();
        break;
    case CMD_SDR:
    case CMD_SDR_INC:
        r = zugriff(s_.dscr);
        s_.dscr = uint8_t((s_.dscr + 1) & 3);
        if (s_.dscr == 0 && code == CMD_SDR_INC) naechsterSar();
        break;
    default:
        break;
    }
    return r;
}

uint8_t Z8010::kommandoLesen(uint8_t code) {
    switch (code) {
    case CMD_MR:   return s_.mr;
    case CMD_SAR:  return s_.sar;
    case CMD_DSCR: return s_.dscr;
    case CMD_VTR:  return s_.vtr;
    case CMD_VSN:  return s_.vsn;
    case CMD_VOFF: return s_.voff;
    case CMD_BCSR: return s_.bcsr;
    case CMD_ISN:  return s_.isn;
    case CMD_IOFF: return s_.ioff;
    case CMD_BASIS: case CMD_LIMIT: case CMD_ATTR: case CMD_SDR:
    case CMD_BASIS_INC: case CMD_LIMIT_INC: case CMD_ATTR_INC: case CMD_SDR_INC:
        return sdrZugriff(code, false, 0);
    default:
        return 0xFF;   // [A11]
    }
}

void Z8010::kommandoSchreiben(uint8_t code, uint8_t daten) {
    switch (code) {
    case CMD_MR:   s_.mr = daten; break;
    case CMD_SAR:  s_.sar = daten & 0x3F; break;   // [A13] DSCR bleibt
    case CMD_DSCR: s_.dscr = daten & 0x03; break;
    case CMD_BASIS: case CMD_LIMIT: case CMD_ATTR: case CMD_SDR:
    case CMD_BASIS_INC: case CMD_LIMIT_INC: case CMD_ATTR_INC: case CMD_SDR_INC:
        sdrZugriff(code, true, daten);
        break;
    case CMD_RESET:   // [A10]
        s_.mr = 0; s_.vtr = 0; s_.dscr = 0;
        s_.befehl = BEFEHL_FREI;
        setzeSegt(false);
        break;
    case CMD_VTR_LOESCHEN:  s_.vtr = 0; s_.befehl = BEFEHL_FREI; break;
    case CMD_SWW_LOESCHEN:  s_.vtr &= uint8_t(~VTR_SWW); break;
    case CMD_FATL_LOESCHEN: s_.vtr &= uint8_t(~VTR_FATL); break;
    case CMD_CPUI_SETZEN:   for (auto& d : s_.sdr) d.attr |= ATTR_CPUI; break;   // Daten egal ([TB] §6.2.21)
    case CMD_DMAI_SETZEN:   for (auto& d : s_.sdr) d.attr |= ATTR_DMAI; break;
    default: break;   // [A11]
    }
}

// ─── Save-State ──────────────────────────────────────────────────────────────

void Z8010::serialize(std::vector<uint8_t>& out) const {
    static_assert(std::is_trivially_copyable_v<S>, "S muss POD sein");
    out.push_back(SAVE_VERSION);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s_);
    out.insert(out.end(), p, p + sizeof(S));
}

bool Z8010::deserialize(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1 || *p != SAVE_VERSION) return false;
    if (static_cast<size_t>(end - p) < 1 + sizeof(S)) return false;
    ++p;
    std::memcpy(&s_, p, sizeof(S));
    p += sizeof(S);
    return true;
}
