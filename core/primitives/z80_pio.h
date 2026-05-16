#pragma once
#include "../bus/k1520_bus.h"
#include <functional>
#include <string>

class Z80PIO : public BusDevice, public InterruptSlave {
public:
    explicit Z80PIO(const std::string& name = "PIO");

    // BusDevice: 4 I/O-Ports (portA_data=0, portA_ctrl=1, portB_data=2, portB_ctrl=3 relative)
    uint8_t ioRead(uint8_t port) override;
    void    ioWrite(uint8_t port, uint8_t data) override;
    const char* deviceName() const override { return name_.c_str(); }

    // InterruptSlave (Daisy-Chain)
    void    setIEI(bool) override;
    bool    getIEO() const override;
    bool    hasInterrupt() const override;
    uint8_t getVector() const override;

    // External port pins (read by other devices, written by external sources)
    void    portAWrite(uint8_t data);
    uint8_t portARead() const;
    void    setASTB(bool);

    void    portBWrite(uint8_t data);
    uint8_t portBRead() const;
    void    setBSTB(bool);

    bool    isInterruptEnabled() const;
    uint8_t getInterruptVector() const;

    using PortCallback = std::function<void(uint8_t data)>;
    void    setPortAOutputCallback(PortCallback);
    void    setPortBOutputCallback(PortCallback);

private:
    enum class CtrlState { IDLE, EXPECT_DIRECTION, EXPECT_MASK };

    struct Port {
        uint8_t   mode          = 1;       // 0=Out,1=In,2=Bidir,3=BitCtl; default Input
        uint8_t   output_latch  = 0x00;
        uint8_t   input_latch   = 0xFF;
        uint8_t   direction     = 0xFF;    // mode 3: bit=1 → input, bit=0 → output
        uint8_t   int_vector    = 0xFF;
        bool      ie            = false;
        bool      int_and       = false;   // true=AND logic, false=OR
        bool      int_active_high = false;
        uint8_t   int_mask      = 0xFF;
        bool      pending       = false;
        bool      iei           = false;
        CtrlState ctrl_state    = CtrlState::IDLE;
    };

    void    writeCtrl(Port& p, uint8_t data);
    uint8_t readDataCPU(const Port& p) const;   // CPU ioRead path
    uint8_t readPins(const Port& p) const;      // external portXRead path
    void    writeDataCPU(Port& p, uint8_t data, PortCallback& cb);
    void    checkInterrupt(Port& p, uint8_t new_input);

    Port         porta_;
    Port         portb_;
    std::string  name_;
    PortCallback cb_a_;
    PortCallback cb_b_;
};
