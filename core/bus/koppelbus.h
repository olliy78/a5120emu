#pragma once
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

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
