/**
 * @file Mcp23017BitDriver.cpp
 * @brief Implementation file.
 */

#include "Mcp23017BitDriver.h"

Mcp23017BitDriver::Mcp23017BitDriver(const char* driverId, Mcp23017Driver* parent, uint8_t bit, bool activeHigh)
    : driverId_(driverId), parent_(parent), bit_(bit), activeHigh_(activeHigh)
{
}

bool Mcp23017BitDriver::begin()
{
    return parent_ != nullptr;
}

bool Mcp23017BitDriver::write(bool on)
{
    if (!parent_) return false;
    const bool rawOn = activeHigh_ ? on : !on;
    return parent_->writePin(bit_, rawOn);
}

bool Mcp23017BitDriver::read(bool& on) const
{
    if (!parent_) return false;
    bool rawOn = false;
    if (!parent_->readShadow(bit_, rawOn)) return false;
    on = activeHigh_ ? rawOn : !rawOn;
    return true;
}
