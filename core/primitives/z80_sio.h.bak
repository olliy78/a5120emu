#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

class Z80SIO : public BusDevice, public InterruptSlave {
public:
    struct Channel {
        void    rxByte(uint8_t byte);
        bool    txAvailable() const;
        uint8_t txGet();
        bool    rxFull() const;

        void setClockSource(std::function<void()> clk_callback);
        void setRTS(bool rts);
        bool getCTS() const;

        // Internal state — public for Z80SIO access
        uint8_t wr[8]  = {};
        uint8_t rr0    = 0x04;  // TxEmpty set by default
        uint8_t rr1    = 0x01;  // AllSent set by default
        uint8_t reg_ptr = 0;

        std::deque<uint8_t>      rx_fifo;
        std::optional<uint8_t>   tx_buf;

        bool cts_    = false;
        bool rts_    = false;

        // Interrupt sources
        bool irq_rx  = false;
        bool irq_tx  = false;
        bool irq_ext = false;

        std::function<void()> clk_cb_;

        void reset();
        void updateRR0();
        bool rxIntEnabled() const;
        bool txIntEnabled() const;
    };

    explicit Z80SIO(const std::string& name = "SIO");

    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    void    setIEI(bool iei) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    Channel& channelA() { return ch_a_; }
    Channel& channelB() { return ch_b_; }

private:
    Channel     ch_a_;
    Channel     ch_b_;
    std::string name_;
    bool        iei_ = false;

    void writeControl(Channel& ch, uint8_t data, bool is_b);
    uint8_t readControl(Channel& ch, bool is_b) const;

    bool channelHasInterrupt(const Channel& ch) const;
};
