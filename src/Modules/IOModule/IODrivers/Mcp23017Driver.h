#pragma once
/**
 * @file Mcp23017Driver.h
 * @brief MCP23017 16-bit output-only driver.
 */

#include <stdint.h>
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

class Mcp23017Driver {
public:
    Mcp23017Driver(const char* driverId, I2CBus* bus, uint8_t address);

    const char* id() const { return driverId_; }
    bool begin();
    bool writeMask(uint16_t mask);
    bool readMask(uint16_t& mask) const;
    bool writePin(uint8_t pin, bool on);
    bool readShadow(uint8_t pin, bool& on) const;

private:
    bool writeReg_(uint8_t reg, uint8_t value);
    bool readReg_(uint8_t reg, uint8_t& value) const;
    bool writeReg16_(uint8_t reg, uint16_t value);

    static constexpr uint8_t kRegIodirA = 0x00;
    static constexpr uint8_t kRegIodirB = 0x01;
    static constexpr uint8_t kRegIpola = 0x02;
    static constexpr uint8_t kRegIpolb = 0x03;
    static constexpr uint8_t kRegGppuA = 0x0C;
    static constexpr uint8_t kRegGppuB = 0x0D;
    static constexpr uint8_t kRegGpioA = 0x12;
    static constexpr uint8_t kRegOlatA = 0x14;

    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    uint8_t address_ = 0x21;
    uint16_t state_ = 0x0000;
};
