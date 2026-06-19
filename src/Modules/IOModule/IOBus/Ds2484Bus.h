#pragma once
/**
 * @file Ds2484Bus.h
 * @brief 1-Wire bus driven through a DS2484 I2C-to-1-Wire master.
 *
 * Implements the IOneWireBus interface on top of the Adafruit_DS248x library:
 * the DS2484 protocol, 1-Wire primitives and ROM search are delegated to the
 * library, while DS18B20 scratchpad decoding stays here. Lets IOModule treat
 * DS18B20 sensors wired on a DS2484 bridge exactly like sensors on a bit-bang
 * GPIO OneWireBus. All I2C traffic is serialized through the shared I2CBus lock.
 */

#include <Adafruit_DS248x.h>
#include <stdint.h>

#include "Modules/IOModule/IOBus/IOneWireBus.h"

class I2CBus;

class Ds2484Bus : public IOneWireBus {
public:
    static constexpr uint8_t kDefaultI2cAddress = 0x18;
    static constexpr uint8_t kMaxDevices = 8;

    Ds2484Bus(I2CBus* bus, uint8_t i2cAddress = kDefaultI2cAddress);

    void begin() override;
    void request() override;
    void setWaitForConversion(bool enabled) override { waitForConversion_ = enabled; }
    bool getAddress(uint8_t index, uint8_t out[8]) const override;
    bool hasAddress(const uint8_t addr[8]) const override;
    float readC(const uint8_t addr[8]) const override;
    uint8_t deviceCount() const override { return romCount_; }
    int pin() const override { return -1; }

    /** Reassign the DS2484 I2C address before begin() (config-driven). */
    void setAddress(uint8_t address) { if (address != i2cAddr_) { i2cAddr_ = address; started_ = false; } }
    /** Re-run the ROM search (also done by begin()). */
    void rescan();
    uint8_t i2cAddress() const { return i2cAddr_; }
    bool present() const { return present_; }

private:
    bool searchRoms_();
    bool readScratchpad_(const uint8_t addr[8], uint8_t out[9]) const;
    static uint8_t crc8_(const uint8_t* data, uint8_t len);

    I2CBus* bus_ = nullptr;
    // mutable: readC()/readScratchpad_() are const but the library's OneWire ops
    // are not (same pattern as `mutable DallasTemperature dt_` in OneWireBus.h).
    mutable Adafruit_DS248x ds_;
    uint8_t i2cAddr_ = kDefaultI2cAddress;
    bool started_ = false;
    bool present_ = false;
    bool waitForConversion_ = false;

    uint8_t roms_[kMaxDevices][8] = {{0}};
    uint8_t romCount_ = 0;
};
