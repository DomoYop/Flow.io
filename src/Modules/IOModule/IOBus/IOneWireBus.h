#pragma once
/**
 * @file IOneWireBus.h
 * @brief Common interface for 1-Wire buses (bit-bang GPIO or DS2484 I2C bridge).
 *
 * DS18B20 reading in IOModule is written against this interface so the physical
 * transport (direct GPIO OneWire vs DS2484 I2C-to-1-Wire master) can be swapped
 * without touching the temperature logic.
 */

#include <stdint.h>

class IOneWireBus {
public:
    virtual ~IOneWireBus() = default;

    /** Bring the bus up (idempotent). */
    virtual void begin() = 0;
    /** Trigger a temperature conversion on every sensor of the bus. */
    virtual void request() = 0;
    /** Blocking vs non-blocking conversion behavior. */
    virtual void setWaitForConversion(bool enabled) = 0;
    /** Copy the ROM address of the device at @p index. */
    virtual bool getAddress(uint8_t index, uint8_t out[8]) const = 0;
    /** True when @p addr is present on the bus. */
    virtual bool hasAddress(const uint8_t addr[8]) const = 0;
    /** Last converted temperature in Celsius for @p addr. */
    virtual float readC(const uint8_t addr[8]) const = 0;
    /** Number of devices discovered on the bus. */
    virtual uint8_t deviceCount() const = 0;
    /** GPIO pin for bit-bang buses, or -1 for I2C-bridged buses. */
    virtual int pin() const = 0;
};
