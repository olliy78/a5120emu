#include "tests/support/keyboard.h"

#include "tests/support/machine_run.h"

namespace k1520test {

void typeKey(A5120Machine& m, uint32_t keycode) {
    m.keyPress(keycode, /*shift=*/false, /*ctrl=*/false);
    runCycles(m, kKeyHoldCycles);
    m.keyRelease(keycode);
    runCycles(m, kKeyReleaseCycles);
}

void typeString(A5120Machine& m, const std::string& s) {
    for (char c : s) typeKey(m, static_cast<uint8_t>(c));
}

void typeCtrl(A5120Machine& m, char c) {
    m.keyPress(static_cast<uint8_t>(c), /*shift=*/false, /*ctrl=*/true);
    runCycles(m, kKeyHoldCycles);
    m.keyRelease(static_cast<uint8_t>(c));
    runCycles(m, kKeyReleaseCycles);
}

bool pressKeyUntil(A5120Machine& m, uint32_t keycode,
                   const std::string& needle, int attempts,
                   long long cycles_per_attempt) {
    for (int i = 0; i < attempts; ++i) {
        typeKey(m, keycode);
        if (runSmallUntil(m, needle, cycles_per_attempt)) return true;
    }
    return false;
}

}  // namespace k1520test
