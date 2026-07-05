#pragma once
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

// A single hand-wired backplane signal line: a level that any number of
// receivers subscribe to.  drive() only notifies on an actual level change,
// so callbacks model edge-driven wiring (e.g. a CTC ZC/TO feeding another
// channel's CLK/TRG input).  Default level is inactive (positive logic).
class KoppelbusSignal {
public:
    using Callback = std::function<void(bool level)>;

    void connect(Callback cb) { receivers_.push_back(std::move(cb)); }

    void drive(bool level) {
        if (level != current_) {
            current_ = level;
            for (auto& cb : receivers_) cb(level);
        }
    }

    bool read() const { return current_; }

private:
    bool current_ = true;  // default: inactive (positive logic)
    std::vector<Callback> receivers_;
};

// Signal router modelling the A5120 backplane's hand-wired links between
// cards — the connections that are not part of the regular K1520 bus:
// the CTC clock cascades (ZC/TO -> CLK/TRG), the second IEI/IEO interrupt
// chain, MEMDI for OPS groups, and the power-monitor lines.  Cards connect()
// to the named signals; machine wiring is what actually cross-links them.
class Koppelbus {
public:
    // A5120 named signals
    KoppelbusSignal iei1, ieo1;     // second interrupt chain
    KoppelbusSignal zc_to[3];       // CTC zero-count outputs (channels 0-2)
    KoppelbusSignal clk_trg[4];     // CTC clock/trigger inputs (channels 0-3)
    KoppelbusSignal sue;            // power monitor
    KoppelbusSignal memdi1, memdi2; // additional MEMDI for OPS groups
    KoppelbusSignal sa;             // power-off signal

    // Generic access for machine-specific signals
    KoppelbusSignal& signal(const std::string& name) {
        return extra_signals_[name];
    }

private:
    std::unordered_map<std::string, KoppelbusSignal> extra_signals_;
};
