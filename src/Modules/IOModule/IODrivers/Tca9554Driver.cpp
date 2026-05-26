#include "Tca9554Driver.h"

static constexpr uint8_t kTcaRegOutput    = 0x01;
static constexpr uint8_t kTcaRegConfig    = 0x03;
static constexpr uint8_t kTcaAllOutputs   = 0x00; // config register: 0 = output

Tca9554Driver::Tca9554Driver(const char* driverId, I2CBus* bus, uint8_t address)
    : driverId_(driverId), bus_(bus), address_(address)
{
}

bool Tca9554Driver::begin()
{
    if (!bus_) return false;
    // Configure all 8 pins as outputs before driving the output latch.
    if (!bus_->lock(20)) return false;
    const uint8_t dir = kTcaAllOutputs;
    bool ok = bus_->writeReg(address_, kTcaRegConfig, &dir, 1);
    bus_->unlock();
    if (!ok) return false;
    return flush_();
}

bool Tca9554Driver::writeMask(uint8_t mask)
{
    state_ = mask;
    return flush_();
}

bool Tca9554Driver::readMask(uint8_t& mask) const
{
    mask = state_;
    return true;
}

bool Tca9554Driver::writePin(uint8_t pin, bool on)
{
    if (pin > 7) return false;
    if (on) state_ |= (uint8_t)(1u << pin);
    else    state_ &= (uint8_t)~(1u << pin);
    return flush_();
}

bool Tca9554Driver::readShadow(uint8_t pin, bool& on) const
{
    if (pin > 7) return false;
    on = (state_ & (uint8_t)(1u << pin)) != 0;
    return true;
}

bool Tca9554Driver::flush_()
{
    if (!bus_) return false;
    if (!bus_->lock(20)) return false;
    bool ok = bus_->writeReg(address_, kTcaRegOutput, &state_, 1);
    bus_->unlock();
    return ok;
}
