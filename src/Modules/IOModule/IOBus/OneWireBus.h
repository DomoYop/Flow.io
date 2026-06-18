#pragma once
/**
 * @file OneWireBus.h
 * @brief OneWire + DallasTemperature wrapper.
 */

#include <stdint.h>
#include <stddef.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include "Modules/IOModule/IOBus/IOneWireBus.h"

class OneWireBus : public IOneWireBus {
public:
    explicit OneWireBus(int pin);

    /** Reassign the GPIO before begin() (for config-driven pins). */
    void setPin(int pin);
    void begin() override;
    void request() override;
    void setWaitForConversion(bool enabled) override;
    bool getAddress(uint8_t index, uint8_t out[8]) const override;
    bool hasAddress(const uint8_t addr[8]) const override;
    float readC(const uint8_t addr[8]) const override;
    uint8_t deviceCount() const override;
    int pin() const override { return pin_; }

private:
    int pin_ = -1;
    OneWire oneWire_;
    mutable DallasTemperature dt_;
    bool started_ = false;
};
