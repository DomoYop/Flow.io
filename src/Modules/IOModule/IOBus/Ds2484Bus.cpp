/**
 * @file Ds2484Bus.cpp
 * @brief Implementation of the DS2484 I2C-to-1-Wire master bus.
 */

#include "Modules/IOModule/IOBus/Ds2484Bus.h"

#include <Arduino.h>
#include <string.h>

#include "Modules/IOModule/IOBus/I2CBus.h"

namespace {
// DS2484 command opcodes.
constexpr uint8_t kCmdDeviceReset = 0xF0;
constexpr uint8_t kCmdSetReadPointer = 0xE1;
constexpr uint8_t kCmdWriteConfig = 0xD2;
constexpr uint8_t kCmd1WireReset = 0xB4;
constexpr uint8_t kCmd1WireWriteByte = 0xA5;
constexpr uint8_t kCmd1WireReadByte = 0x96;
constexpr uint8_t kCmd1WireTriplet = 0x78;

// Read pointer codes.
constexpr uint8_t kPtrStatus = 0xF0;
constexpr uint8_t kPtrReadData = 0xE1;

// Status register bits.
constexpr uint8_t kStatus1WB = 0x01;  // 1-Wire busy.
constexpr uint8_t kStatusPPD = 0x02;  // Presence pulse detected.
constexpr uint8_t kStatusSBR = 0x20;  // Single bit result.
constexpr uint8_t kStatusTSB = 0x40;  // Triplet second (complement) bit.
constexpr uint8_t kStatusDIR = 0x80;  // Branch direction taken.

// Configuration: active pull-up (APU) on, others off.
constexpr uint8_t kConfigActivePullup = 0x01;

// DS18B20 ROM/function commands.
constexpr uint8_t kRomSearch = 0xF0;
constexpr uint8_t kRomMatch = 0x55;
constexpr uint8_t kRomSkip = 0xCC;
constexpr uint8_t kFuncConvertT = 0x44;
constexpr uint8_t kFuncReadScratchpad = 0xBE;

constexpr uint32_t kI2cLockTimeoutMs = 50;
constexpr uint32_t kBusyTimeoutMs = 20;  // 1-Wire ops complete well under this.
}  // namespace

Ds2484Bus::Ds2484Bus(I2CBus* bus, uint8_t i2cAddress)
    : bus_(bus), i2cAddr_(i2cAddress) {}

bool Ds2484Bus::setReadPointer_(uint8_t pointer) const
{
    const uint8_t cmd[2] = {kCmdSetReadPointer, pointer};
    return bus_->writeBytes(i2cAddr_, cmd, sizeof(cmd));
}

bool Ds2484Bus::readStatus_(uint8_t& status) const
{
    if (!setReadPointer_(kPtrStatus)) return false;
    return bus_->readBytes(i2cAddr_, &status, 1);
}

bool Ds2484Bus::waitNotBusy_(uint8_t& status) const
{
    const uint32_t deadline = millis() + kBusyTimeoutMs;
    do {
        if (!readStatus_(status)) return false;
        if ((status & kStatus1WB) == 0) return true;
    } while ((int32_t)(millis() - deadline) < 0);
    return false;
}

bool Ds2484Bus::deviceReset_()
{
    const uint8_t cmd = kCmdDeviceReset;
    if (!bus_->writeBytes(i2cAddr_, &cmd, 1)) return false;
    uint8_t status = 0;
    return waitNotBusy_(status);
}

bool Ds2484Bus::writeConfig_(uint8_t config)
{
    // Low nibble = config, high nibble = one's complement (DS2484 requirement).
    const uint8_t payload = (uint8_t)((config & 0x0F) | ((~config & 0x0F) << 4));
    const uint8_t cmd[2] = {kCmdWriteConfig, payload};
    return bus_->writeBytes(i2cAddr_, cmd, sizeof(cmd));
}

bool Ds2484Bus::owReset_(bool& presencePulse) const
{
    uint8_t status = 0;
    if (!waitNotBusy_(status)) return false;
    const uint8_t cmd = kCmd1WireReset;
    if (!bus_->writeBytes(i2cAddr_, &cmd, 1)) return false;
    if (!waitNotBusy_(status)) return false;
    presencePulse = (status & kStatusPPD) != 0;
    return true;
}

bool Ds2484Bus::owWriteByte_(uint8_t value) const
{
    uint8_t status = 0;
    if (!waitNotBusy_(status)) return false;
    const uint8_t cmd[2] = {kCmd1WireWriteByte, value};
    if (!bus_->writeBytes(i2cAddr_, cmd, sizeof(cmd))) return false;
    return waitNotBusy_(status);
}

bool Ds2484Bus::owReadByte_(uint8_t& value) const
{
    uint8_t status = 0;
    if (!waitNotBusy_(status)) return false;
    const uint8_t cmd = kCmd1WireReadByte;
    if (!bus_->writeBytes(i2cAddr_, &cmd, 1)) return false;
    if (!waitNotBusy_(status)) return false;
    if (!setReadPointer_(kPtrReadData)) return false;
    return bus_->readBytes(i2cAddr_, &value, 1);
}

bool Ds2484Bus::owTriplet_(bool dir, uint8_t& status) const
{
    if (!waitNotBusy_(status)) return false;
    const uint8_t cmd[2] = {kCmd1WireTriplet, (uint8_t)(dir ? 0x80 : 0x00)};
    if (!bus_->writeBytes(i2cAddr_, cmd, sizeof(cmd))) return false;
    return waitNotBusy_(status);
}

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
    lastDiscrepancy_ = 0;
    lastDeviceFlag_ = false;
    memset(searchRom_, 0, sizeof(searchRom_));

    while (romCount_ < kMaxDevices && !lastDeviceFlag_) {
        bool presence = false;
        if (!owReset_(presence) || !presence) break;  // No (more) devices.
        if (!owWriteByte_(kRomSearch)) break;

        int lastZero = 0;
        bool searchError = false;
        for (uint8_t bitNumber = 1; bitNumber <= 64; ++bitNumber) {
            const uint8_t byteIdx = (uint8_t)((bitNumber - 1) / 8);
            const uint8_t bitMask = (uint8_t)(1 << ((bitNumber - 1) % 8));

            bool dir;
            if (bitNumber < lastDiscrepancy_) {
                dir = (searchRom_[byteIdx] & bitMask) != 0;
            } else {
                dir = (bitNumber == lastDiscrepancy_);
            }

            uint8_t status = 0;
            if (!owTriplet_(dir, status)) { searchError = true; break; }
            const bool sbr = (status & kStatusSBR) != 0;
            const bool tsb = (status & kStatusTSB) != 0;
            const bool dirTaken = (status & kStatusDIR) != 0;

            if (sbr && tsb) { searchError = true; break; }  // No devices on bus.
            if (!sbr && !tsb && !dirTaken) {
                lastZero = bitNumber;  // Discrepancy, took the 0 branch.
            }

            if (dirTaken) searchRom_[byteIdx] |= bitMask;
            else searchRom_[byteIdx] &= (uint8_t)~bitMask;
        }

        if (searchError) break;
        if (crc8_(searchRom_, 7) != searchRom_[7]) break;  // Bad ROM CRC.

        memcpy(roms_[romCount_], searchRom_, 8);
        ++romCount_;

        lastDiscrepancy_ = lastZero;
        if (lastDiscrepancy_ == 0) lastDeviceFlag_ = true;
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
        present_ = deviceReset_() && writeConfig_(kConfigActivePullup);
        if (present_) searchRoms_();
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

    bool presence = false;
    if (owReset_(presence) && presence) {
        owWriteByte_(kRomSkip);     // Address all sensors.
        owWriteByte_(kFuncConvertT);
        if (waitForConversion_) delay(750);
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
    bool presence = false;
    if (!owReset_(presence) || !presence) return false;
    if (!owWriteByte_(kRomMatch)) return false;
    for (uint8_t i = 0; i < 8; ++i) {
        if (!owWriteByte_(addr[i])) return false;
    }
    if (!owWriteByte_(kFuncReadScratchpad)) return false;
    for (uint8_t i = 0; i < 9; ++i) {
        if (!owReadByte_(out[i])) return false;
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
