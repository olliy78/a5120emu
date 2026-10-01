/**
 * @file attrappe_anschluss.h
 * @brief Test-Attrappe eines `SerialAnschluss` (Entwurf 19 §5.1) — statt einer Karte mit
 *        SIO: ein Sendepuffer des „Gastes", ein Empfangs-FIFO mit SIO-Tiefe 3, Leitungen.
 *
 * Zählt Überläufe: der Wandler darf NIE in ein volles FIFO liefern (Leitsatz 2).
 */
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/serial/anschluss.h"

namespace k1520test {

struct AttrappeAnschluss : k1520::serial::SerialAnschluss {
    bool istV24 = true;
    /// Vorgabe 9600 8N1: SIO ×16, CTC-Teiler 16 → 2560 Takte je Zeichen.
    k1520::serial::SerialFormat fmt = k1520::serial::serialFormatRechnen(16, 8, 0, 2, 16);

    std::deque<uint8_t> gastSendet;   ///< was der Gast noch senden will (front = Tx-Puffer)
    std::deque<uint8_t> fifo;         ///< SIO-Empfangs-FIFO
    size_t tiefe = 3;
    std::vector<uint8_t> gelesen;     ///< was der Gast aus dem FIFO geholt hat
    uint64_t ueberlauf = 0;

    bool rtsAus = true, dtrAus = true, brkAus = false;
    bool cts = false, dsr = false, dcd = false, brkEin = false;
    int eingaengeAufrufe = 0;
    int taktquelle = -1;

    const char* name() const override { return "Attrappe"; }
    const char* stecker() const override { return "X0"; }
    bool v24() const override { return istV24; }
    std::vector<k1520::serial::Taktquelle> taktquellen() const override {
        return {{"Quelle A"}, {"Quelle B"}};
    }
    void waehleTaktquelle(int i) override { taktquelle = i; }
    k1520::serial::SerialFormat format() const override { return fmt; }
    bool senderHatZeichen() const override { return !gastSendet.empty(); }
    uint8_t senderNimm() override {
        const uint8_t b = gastSendet.front();
        gastSendet.pop_front();
        return b;
    }
    bool empfaengerFrei() const override { return fifo.size() < tiefe; }
    void empfange(uint8_t b) override {
        if (fifo.size() >= tiefe) ++ueberlauf;
        fifo.push_back(b);
    }
    bool rts() const override { return rtsAus; }
    bool dtr() const override { return dtrAus; }
    void setzeEingaenge(bool c, bool s, bool d) override {
        cts = c;
        dsr = s;
        dcd = d;
        ++eingaengeAufrufe;
    }
    bool breakGesendet() const override { return brkAus; }
    void breakEmpfang(bool a) override { brkEin = a; }
    std::vector<bool> belegtMeldungen;   ///< jede `leitungBelegt`-Meldung (AP-S5)
    void leitungBelegt(bool b) override { belegtMeldungen.push_back(b); }

    void sende(const std::string& s) { for (char c : s) gastSendet.push_back(static_cast<uint8_t>(c)); }
    void sende(const std::vector<uint8_t>& v) { gastSendet.insert(gastSendet.end(), v.begin(), v.end()); }
    /// Der Gast liest sein FIFO leer.
    void lies() {
        gelesen.insert(gelesen.end(), fifo.begin(), fifo.end());
        fifo.clear();
    }
    std::string gelesenText() const { return std::string(gelesen.begin(), gelesen.end()); }
};

}  // namespace k1520test
