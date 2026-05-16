#include "core/cards/k8025/k8025.h"

// ─── Constructor ──────────────────────────────────────────────────────────────

K8025::K8025(K1520Bus& bus, const A5120Config& cfg)
    : cfg_(cfg)
{
    bus.registerIO(this, cfg_.io_base, 16);
    updateInternalChain();
}

// ─── Internal daisy-chain propagation ────────────────────────────────────────

void K8025::updateInternalChain()
{
    // Priority: SIO A33 (highest) → SIO A32 → CTC A34 (lowest) → /IEO out
    sio_dfue_.setIEI(iei_in_);
    sio_kbd_printer_.setIEI(sio_dfue_.getIEO());
    ctc_a34_.setIEI(sio_kbd_printer_.getIEO());
}

// ─── BusDevice ───────────────────────────────────────────────────────────────

uint8_t K8025::ioRead(uint8_t port)
{
    uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    uint8_t sub = port & 0x03;

    uint8_t result;
    if      (rel < 4)  result = sio_dfue_.ioRead(sub);           // 0x50–0x53
    else if (rel < 8)  result = pio_a31_.ioRead(sub);             // 0x54–0x57
    else if (rel < 12) result = ctc_a34_.ioRead(sub);             // 0x58–0x5B
    else               result = sio_kbd_printer_.ioRead(sub);     // 0x5C–0x5F

    updateInternalChain();
    return result;
}

void K8025::ioWrite(uint8_t port, uint8_t data)
{
    uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    uint8_t sub = port & 0x03;

    if      (rel < 4)  sio_dfue_.ioWrite(sub, data);             // 0x50–0x53
    else if (rel < 8)  pio_a31_.ioWrite(sub, data);               // 0x54–0x57
    else if (rel < 12) ctc_a34_.ioWrite(sub, data);               // 0x58–0x5B
    else               sio_kbd_printer_.ioWrite(sub, data);       // 0x5C–0x5F

    updateInternalChain();
}

// ─── InterruptSlave ───────────────────────────────────────────────────────────

void K8025::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

bool K8025::getIEO() const
{
    // If our IEI is not asserted, block IEO.
    if (!iei_in_) return false;
    // IEO is blocked when any chip in our chain has an active interrupt.
    // Using hasInterrupt() works because updateInternalChain() has set each
    // chip's iei_ correctly, so hasInterrupt() already considers priority.
    return !sio_dfue_.hasInterrupt()
        && !sio_kbd_printer_.hasInterrupt()
        && !ctc_a34_.hasInterrupt();
}

bool K8025::hasInterrupt() const
{
    if (!iei_in_) return false;
    return sio_dfue_.hasInterrupt()
        || sio_kbd_printer_.hasInterrupt()
        || ctc_a34_.hasInterrupt();
}

uint8_t K8025::getVector() const
{
    // Priority: SIO A33 first, then SIO A32, then CTC A34.
    // After updateInternalChain(), the lower-priority chips have iei_=false
    // when a higher-priority chip has a pending interrupt, so their
    // hasInterrupt() returns false automatically.
    if (sio_dfue_.hasInterrupt())        return sio_dfue_.getVector();
    if (sio_kbd_printer_.hasInterrupt()) return sio_kbd_printer_.getVector();
    if (ctc_a34_.hasInterrupt())         return ctc_a34_.getVector();
    return 0xFF;
}

// ─── Keyboard interface ───────────────────────────────────────────────────────

void K8025::keyboardRxByte(uint8_t byte)
{
    sio_kbd_printer_.channelA().rxByte(byte);
    updateInternalChain();
}

bool K8025::keyboardTxAvailable()
{
    return sio_kbd_printer_.channelA().txAvailable();
}

uint8_t K8025::keyboardTxGet()
{
    return sio_kbd_printer_.channelA().txGet();
}

// ─── DFÜ interface ────────────────────────────────────────────────────────────

void K8025::dfueRxByte(uint8_t byte)
{
    sio_dfue_.channelA().rxByte(byte);
    updateInternalChain();
}

bool K8025::dfueTxAvailable()
{
    return sio_dfue_.channelA().txAvailable();
}

uint8_t K8025::dfueTxGet()
{
    return sio_dfue_.channelA().txGet();
}

void K8025::setDFUERxCallback(SerialCallback cb)
{
    dfue_rx_cb_ = std::move(cb);
}

// ─── Printer interface ────────────────────────────────────────────────────────

bool K8025::printerTxAvailable()
{
    return sio_kbd_printer_.channelB().txAvailable();
}

uint8_t K8025::printerTxGet()
{
    return sio_kbd_printer_.channelB().txGet();
}

// ─── CTC clock tick ───────────────────────────────────────────────────────────

void K8025::clockTick()
{
    ctc_a34_.clockTick();
    updateInternalChain();
}
