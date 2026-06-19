/**
 * @file Ds2484Bus.cpp
 * @brief Implementation of the DS2484 I2C-to-1-Wire master bus.
 *
 * The DS2484 protocol, 1-Wire primitives and ROM search are handled by the
 * Adafruit_DS248x library; only the DS18B20 scratchpad decoding stays local.
 * Every library call sequence runs under the shared I2CBus lock so accesses to
 * the bus stay atomic against the other I2C peripherals.
 */

#include "Modules/IOModule/IOBus/Ds2484Bus.h"

#include <Arduino.h>
#include <string.h>

#include "Modules/IOModule/IOBus/I2CBus.h"

namespace {
// DS18B20 ROM/function commands (issued through the bridge's 1-Wire primitives).
constexpr uint8_t kRomMatch = 0x55;
constexpr uint8_t kRomSkip = 0xCC;
constexpr uint8_t kFuncConvertT = 0x44;
constexpr uint8_t kFuncReadScratchpad = 0xBE;

constexpr uint32_t kI2cLockTimeoutMs = 50;
constexpr uint32_t kConversionDelayMs = 750;  // DS18B20 12-bit conversion time.
}  // namespace

Ds2484Bus::Ds2484Bus(I2CBus* bus, uint8_t i2cAddress)
    : bus_(bus), i2cAddr_(i2cAddress) {}

uint8_t Ds2484Bus::crc8_(const uint8_t* data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            const uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01);
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

bool Ds2484Bus::searchRoms_()
{
    romCount_ = 0;
    ds_.OneWireSearchReset();

    uint8_t addr[8];
    while (romCount_ < kMaxDevices && ds_.OneWireSearch(addr)) {
        if (crc8_(addr, 7) != addr[7]) continue;  // Skip device with bad ROM CRC.
        memcpy(roms_[romCount_], addr, 8);
        ++romCount_;
    }
    return romCount_ > 0;
}

void Ds2484Bus::begin()
{
    if (started_) return;
    if (!bus_) return;
    if (!bus_->lock(kI2cLockTimeoutMs)) return;

    present_ = bus_->probe(i2cAddr_);
    if (present_) {
        // begin() resets the bridge; activePullup() must be set explicitly
        // afterwards (the library does not enable APU on its own).
        present_ = ds_.begin(bus_->wire(), i2cAddr_);
        if (present_) {
            ds_.activePullup(true);
            searchRoms_();
        }
    }

    bus_->unlock();
    started_ = true;
}

void Ds2484Bus::rescan()
{
    if (!bus_ || !present_) return;
    if (!bus_->lock(kI2cLockTimeoutMs)) return;
    searchRoms_();
    bus_->unlock();
}

void Ds2484Bus::request()
{
    if (!present_ || !bus_) return;
    if (!bus_->lock(kI2cLockTimeoutMs)) return;

    if (ds_.OneWireReset()) {           // Returns true on presence pulse.
        ds_.OneWireWriteByte(kRomSkip);     // Address all sensors.
        ds_.OneWireWriteByte(kFuncConvertT);
        if (waitForConversion_) delay(kConversionDelayMs);
    }

    bus_->unlock();
}

bool Ds2484Bus::getAddress(uint8_t index, uint8_t out[8]) const
{
    if (!out || index >= romCount_) return false;
    memcpy(out, roms_[index], 8);
    return true;
}

bool Ds2484Bus::hasAddress(const uint8_t addr[8]) const
{
    if (!addr) return false;
    for (uint8_t i = 0; i < romCount_; ++i) {
        if (memcmp(roms_[i], addr, 8) == 0) return true;
    }
    return false;
}

bool Ds2484Bus::readScratchpad_(const uint8_t addr[8], uint8_t out[9]) const
{
    if (!ds_.OneWireReset()) return false;
    if (!ds_.OneWireWriteByte(kRomMatch)) return false;
    for (uint8_t i = 0; i < 8; ++i) {
        if (!ds_.OneWireWriteByte(addr[i])) return false;
    }
    if (!ds_.OneWireWriteByte(kFuncReadScratchpad)) return false;
    for (uint8_t i = 0; i < 9; ++i) {
        if (!ds_.OneWireReadByte(&out[i])) return false;
    }
    return true;
}

float Ds2484Bus::readC(const uint8_t addr[8]) const
{
    constexpr float kDisconnected = -127.0f;  // Matches DEVICE_DISCONNECTED_C.
    if (!present_ || !bus_ || !addr) return kDisconnected;
    if (!bus_->lock(kI2cLockTimeoutMs)) return kDisconnected;

    uint8_t scratch[9] = {0};
    const bool ok = readScratchpad_(addr, scratch);
    bus_->unlock();

    if (!ok) return kDisconnected;
    if (crc8_(scratch, 8) != scratch[8]) return kDisconnected;

    const int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
    if (raw == 0x0550) return kDisconnected;  // DS18B20 power-on reset value.
    return (float)raw / 16.0f;
}
