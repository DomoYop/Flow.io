#pragma once

#include <stdint.h>
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

// TCA9554 8-bit I2C I/O expander output driver.
// Unlike PCF8574, TCA9554 uses register-addressed writes:
//   reg 0x03 = direction (0x00 = all outputs)
//   reg 0x01 = output port latch
class Tca9554Driver : public IMaskOutputDriver {
public:
    Tca9554Driver(const char* driverId, I2CBus* bus, uint8_t address);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t) override {}

    bool writeMask(uint8_t mask) override;
    bool readMask(uint8_t& mask) const override;
    bool writePin(uint8_t pin, bool on);
    bool readShadow(uint8_t pin, bool& on) const;

private:
    bool flush_();

    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    uint8_t address_ = 0x20;
    uint8_t state_ = 0x00; // all relays off at startup (active-high)
};
