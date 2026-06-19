#pragma once
/**
 * @file Ina228Driver.h
 * @brief INA228 polled current/power monitor driver (20-bit, +temperature/energy/charge).
 */

#include <INA228.h>
#include <stdint.h>

#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Ina228DriverConfig {
    uint8_t address = 0x40;
    uint32_t pollMs = 500;
    float shuntOhms = 0.1f;
};

class Ina228Driver : public IAnalogSourceDriver {
public:
    Ina228Driver(const char* driverId, I2CBus* bus, const Ina228DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool readSample(uint8_t channel, IOAnalogSample& out) const override;

private:
    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Ina228DriverConfig cfg_{};

    INA228 ina_;
    bool ready_ = false;
    bool valid_ = false;
    uint32_t lastPollMs_ = 0;
    uint32_t seq_ = 0;
    // [0]=shunt mV, [1]=bus V, [2]=current mA, [3]=power mW,
    // [4]=load V, [5]=temperature degC, [6]=energy Wh, [7]=charge mAh.
    float values_[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};
