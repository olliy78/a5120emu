#include "core/serial/wandler.h"

#include <algorithm>

namespace k1520::serial {

namespace {
constexpr uint8_t XON  = 0x11;
constexpr uint8_t XOFF = 0x13;
}  // namespace

Wandler::Wandler(SerialAnschluss& anschluss, uint64_t phiNenn)
    : a_(anschluss),
      v24_(anschluss.v24()),
      entprellTakte_(phiNenn / 10),
      ersatz_(ersatzFormat(phiNenn)) {
    wirksam_ = kandidat_ = gemeldet_ = ersatz_;
}

/// Abstand bis zum nächsten Blick: 1/16 Zeichenzeit, aber höchstens 1/16 der Ersatz-Zeichenzeit
/// (9600 Bd).  Der Wandler sieht ein neues Format erst beim nächsten Blick; stand der Gast vorher
/// auf einer langsamen Rate (oder einer, die ein stehender Zähler-Kanal fast auf null drückt), lag
/// der nächste Blick sonst um 1/16 der ALTEN Zeichenzeit entfernt — ein dann mit 9600 Bd
/// geschriebenes Zeichen galt erst zig Millisekunden später als gesendet (MON16-Hardwaretest
/// Fehler 61 „SIO-Port interruptet nicht", Befund P22).
uint64_t Wandler::blickAbstand(uint64_t zt) const {
    const uint64_t deckel = ersatz_.zeichen_takte ? ersatz_.zeichen_takte : zt;
    return std::max<uint64_t>(1, std::min(zt, deckel) / 16);
}

uint64_t Wandler::takt(uint64_t zyklus) {
    if (zyklus < letzterZyklus_) {
        // Taktzähler zurückgesetzt (neue Maschine, Reset des Zählers): alle Fristen neu.
        naechsterBlick_ = 0;
        std::lock_guard<std::mutex> l(m_);
        naechstesSenden_ = naechsteZustellung_ = 0;
        kandidatSeit_ = zyklus;
    }
    letzterZyklus_ = zyklus;
    if (zyklus < naechsterBlick_) return naechsterBlick_;
    if (ruhig_.load(std::memory_order_acquire) && (++ruhZaehler_ & 15u) != 0 &&
        !a_.senderHatZeichen()) {
        naechsterBlick_ = zyklus + blickAbstand(ztLetzte_);
        return naechsterBlick_;
    }

    // Kartenzustand lesen — Emulationsfaden, ohne Sperre.
    const SerialFormat fmt = a_.format();
    const SerialFormat wirk = fmt.gueltig && fmt.zeichen_takte ? fmt : ersatz_;
    const uint64_t zt = wirk.zeichen_takte;
    // Ein Blick je 1/16 Zeichenzeit: die Zeichenzeit wird damit auf ~6 % genau
    // eingehalten, und die Sperre fällt bei 9600 Bd nur alle 160 Takte an statt je
    // Instruktion.
    naechsterBlick_ = zyklus + blickAbstand(zt);
    ztLetzte_       = zt;
    const bool rts = v24_ && a_.rts();
    const bool dtr = v24_ && a_.dtr();
    const bool brk = a_.breakGesendet();

    bool wecken = false;
    std::lock_guard<std::mutex> l(m_);

    if (einst_.taktquelle != taktquelleGesetzt_) {
        a_.waehleTaktquelle(einst_.taktquelle);
        taktquelleGesetzt_ = einst_.taktquelle;
    }

    // ── Format + Entprellung (RFC 2217 meldet erst, wenn der Gast fertig ist) ──
    format_  = fmt;
    wirksam_ = wirk;
    if (wirk != kandidat_) {
        kandidat_     = wirk;
        kandidatSeit_ = zyklus;
    } else if (kandidat_ != gemeldet_ && zyklus - kandidatSeit_ >= entprellTakte_) {
        gemeldet_ = kandidat_;
        ++gemeldetStand_;
        wecken = true;
    }

    // ── Ausgänge des Gastes ────────────────────────────────────────────────
    if (rts) rtsBenutzt_ = true;
    if (rts != rts_ || dtr != dtr_ || brk != brk_) {
        rts_ = rts;
        dtr_ = dtr;
        brk_ = brk;
        wecken = true;
    }

    // ── Belegung des Steckers an die Karte (alter Unterbau stumm, AP-S5) ────
    const int belegt = (einst_.loop || angebunden_) ? 1 : 0;
    if (belegt != belegtGemeldet_) {
        a_.leitungBelegt(belegt != 0);
        belegtGemeldet_ = belegt;
    }
    const int stecker = einst_.loop ? 1 : 0;
    if (stecker != steckerGemeldet_) {
        a_.pruefstecker(stecker != 0);
        steckerGemeldet_ = stecker;
    }

    // ── Eingänge am Stecker (§6.4/§6.5) ─────────────────────────────────────
    bool cts, dsr, dcd;
    if (v24_ && (einst_.loop || einst_.rtscts_bruecke)) {
        // Prüfstecker bzw. Brücke: RTS→CTS, DTR→DSR/DCD — überstimmt die Gegenseite.
        cts = rts;
        dsr = dcd = dtr;
    } else if (angebunden_) {
        cts = fernCts_;
        dsr = fernDsr_;
        dcd = fernDcd_;
    } else {
        cts = dsr = dcd = false;   // Kabel ab
    }
    if (!eingGesetzt_ || cts != eingCts_ || dsr != eingDsr_ || dcd != eingDcd_) {
        a_.setzeEingaenge(cts, dsr, dcd);
        eingCts_ = cts;
        eingDsr_ = dsr;
        eingDcd_ = dcd;
        eingGesetzt_ = true;
    }
    const bool brkEmpf = einst_.loop ? brk : (angebunden_ && fernBrk_);
    if (brkEmpf != brkEmpf_) {
        a_.breakEmpfang(brkEmpf);
        brkEmpf_ = brkEmpf;
    }

    // ── Senden: Gast → Sendepuffer (bzw. Loop / Verfall) ────────────────────
    if (zyklus >= naechstesSenden_ && a_.senderHatZeichen()) {
        bool nehmen;
        if (einst_.loop)      nehmen = !empf_.voll();   // Rückstau über den Empfangsweg
        else if (angebunden_) nehmen = !send_.voll();   // Rückstau: der Gast wartet
        else                  nehmen = true;            // Kabel ab: Zeichen verfällt
        if (nehmen) {
            const uint8_t b = a_.senderNimm();
            naechstesSenden_ = zyklus + zt;
            // §6.3: das Zeichen geht trotzdem hinaus — XOFF hält nur UNSEREN Empfang an.
            if (einst_.xonxoff) {
                if (b == XOFF) xoff_ = true;
                else if (b == XON) xoff_ = false;
            }
            if (einst_.loop) {
                // Das Zeichen kommt erst an, wenn es ganz über den Prüfstecker ist.
                if (empf_.leer()) naechsteZustellung_ = std::max(naechsteZustellung_, zyklus + zt);
                empf_.rein(b);
                ++bytesGesendet_;
            } else if (angebunden_) {
                if (send_.leer()) wecken = true;
                send_.rein(b);
                ++bytesGesendet_;
            }
        }
    }

    // ── Empfangen: Empfangspuffer → Gast ──────────────────────────────────
    // Halt bei XOFF (§6.3) und bei weggenommenem RTS (§6.4, nur V.24; nicht im Loop —
    // der Prüfstecker hat keine Gegenstelle, die auf CTS hören könnte).  Der RTS-Halt
    // gilt erst, wenn der Gast RTS überhaupt benutzt (AP-S5): SCPX 8915 schreibt
    // WR5 = 68H (RTS und DTR aus) und CP/A fasst den V.24-Kanal gar nicht an — wörtlich
    // genommen empfinge ein solcher Gast nie etwas, obwohl am Gerät die Gegenstelle
    // (Drucker mit XON/XOFF, Terminal ohne Handshake) trotzdem sendet.
    const bool halt = (einst_.xonxoff && xoff_) ||
                      (v24_ && !einst_.loop && rtsBenutzt_ && !rts);
    if (!halt && !empf_.leer() && zyklus >= naechsteZustellung_ && a_.empfaengerFrei()) {
        const bool warVoll = empf_.voll();
        a_.empfange(empf_.raus());
        ++bytesEmpfangen_;
        naechsteZustellung_ = zyklus + zt;
        if (warVoll) wecken = true;   // I/O-Faden liest den Socket wieder
    }

    ruhig_.store(!angebunden_ && !einst_.loop && !einst_.rtscts_bruecke && send_.leer() &&
                     empf_.leer(),
                 std::memory_order_release);
    if (wecken && wecker_) wecker_();
    return naechsterBlick_;
}

void Wandler::einstellen(const WandlerEinstellung& e) {
    std::lock_guard<std::mutex> l(m_);
    ruhig_.store(false, std::memory_order_release);
    if (!e.xonxoff) xoff_ = false;   // ausgeschaltet → kein hängender Halt
    const bool wurdeLoop = !einst_.loop && e.loop;
    einst_ = e;
    // Loop an: Kabel ist ab (die Verbindung beendet der Hub, §6.5); Liegengebliebenes
    // im Sendepuffer hat keinen Weg mehr.
    if (wurdeLoop) send_.leeren();
}

WandlerEinstellung Wandler::einstellung() const {
    std::lock_guard<std::mutex> l(m_);
    return einst_;
}

WandlerSicht Wandler::sicht() const {
    std::lock_guard<std::mutex> l(m_);
    WandlerSicht s;
    s.format          = format_;
    s.wirksam         = wirksam_;
    s.gemeldet        = gemeldet_;
    s.gemeldetStand   = gemeldetStand_;
    s.rts             = rts_;
    s.dtr             = dtr_;
    s.brk             = brk_;
    s.cts             = eingCts_;
    s.dsr             = eingDsr_;
    s.dcd             = eingDcd_;
    s.xoffHalt        = xoff_;
    s.angebunden      = angebunden_;
    s.bytes_gesendet  = bytesGesendet_;
    s.bytes_empfangen = bytesEmpfangen_;
    s.puffer_senden   = static_cast<uint32_t>(send_.n);
    s.puffer_empfangen = static_cast<uint32_t>(empf_.n);
    return s;
}

void Wandler::gastZurueckgesetzt() {
    std::lock_guard<std::mutex> l(m_);
    ruhig_.store(false, std::memory_order_release);
    xoff_ = false;
    rtsBenutzt_ = false;
}

void Wandler::anbinden() {
    std::lock_guard<std::mutex> l(m_);
    ruhig_.store(false, std::memory_order_release);
    angebunden_ = true;
    rtsBenutzt_ = false;
    fernCts_ = fernDsr_ = fernDcd_ = true;
    fernBrk_ = false;
}

void Wandler::abbinden() {
    std::lock_guard<std::mutex> l(m_);
    angebunden_ = false;
    rtsBenutzt_ = false;
    fernCts_ = fernDsr_ = fernDcd_ = fernBrk_ = false;
    send_.leeren();
}

bool Wandler::angebunden() const {
    std::lock_guard<std::mutex> l(m_);
    return angebunden_;
}

size_t Wandler::fernNimm(uint8_t* ziel, size_t max) {
    std::lock_guard<std::mutex> l(m_);
    size_t k = 0;
    while (k < max && !send_.leer()) ziel[k++] = send_.raus();
    return k;
}

size_t Wandler::fernBelegt() const {
    std::lock_guard<std::mutex> l(m_);
    return send_.n;
}

size_t Wandler::fernGib(const uint8_t* daten, size_t n) {
    std::lock_guard<std::mutex> l(m_);
    size_t k = 0;
    while (k < n && !empf_.voll()) empf_.rein(daten[k++]);
    if (k) ruhig_.store(false, std::memory_order_release);
    return k;
}

size_t Wandler::fernFrei() const {
    std::lock_guard<std::mutex> l(m_);
    return empf_.frei();
}

void Wandler::fernLeitungen(bool cts, bool dsr, bool dcd) {
    std::lock_guard<std::mutex> l(m_);
    fernCts_ = cts;
    fernDsr_ = dsr;
    fernDcd_ = dcd;
}

void Wandler::fernBreak(bool aktiv) {
    std::lock_guard<std::mutex> l(m_);
    fernBrk_ = aktiv;
}

void Wandler::leeren(bool sendepuffer, bool empfangspuffer) {
    std::lock_guard<std::mutex> l(m_);
    if (sendepuffer) send_.leeren();
    if (empfangspuffer) empf_.leeren();
}

void Wandler::setzeWecker(std::function<void()> wecken) {
    std::lock_guard<std::mutex> l(m_);
    wecker_ = std::move(wecken);
}

}  // namespace k1520::serial
