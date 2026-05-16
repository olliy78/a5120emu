#pragma once
#include "../bus/k1520_bus.h"
#include <cstdint>
#include <functional>
#include <string>

class Z80CTC : public BusDevice, public InterruptSlave {
public:
    explicit Z80CTC(const std::string& name = "CTC");

    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;

    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    void    clockTick();
    void    clkTrg(int channel, bool rising_edge);

    using ZCTOCallback = std::function<void(int channel, bool level)>;
    void    setZCTOCallback(ZCTOCallback cb);

    int     getCount(int channel) const;
    bool    isTimerMode(int channel) const;

    const char* deviceName() const override { return name_.c_str(); }

private:
    static constexpr uint8_t CTRL_RESET       = 0x02;
    static constexpr uint8_t CTRL_TC_FOLLOWS  = 0x04;
    static constexpr uint8_t CTRL_COUNTER     = 0x08;
    static constexpr uint8_t CTRL_PRESCALE256 = 0x10;
    static constexpr uint8_t CTRL_EDGE_RISE   = 0x20;
    static constexpr uint8_t CTRL_CLK_TRG    = 0x40;
    static constexpr uint8_t CTRL_INT_EN     = 0x80;

    struct Channel {
        uint8_t control     = CTRL_RESET;
        uint8_t timeConst   = 0;
        int     counter     = 0;
        int     prescaleCnt = 0;
        bool    awaiting_tc = false;
        bool    running     = false;
        bool    zcto_active = false;
        bool    int_pending = false;
    };

    void    fireZCTO(int ch);
    bool    anyPending() const;

    Channel     ch_[4];
    uint8_t     vec_base_ = 0;
    bool        iei_      = false;
    std::string name_;
    ZCTOCallback zcto_cb_;
};
