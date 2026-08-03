/**
 * @file Ds18b20Driver.cpp
 * @brief Implementation file.
 */

#include "Ds18b20Driver.h"
#include <DallasTemperature.h>

Ds18b20Driver::Ds18b20Driver(const char* driverId, IOneWireBus* bus, const uint8_t address[8],
                             const Ds18b20DriverConfig& cfg)
    : driverId_(driverId), bus_(bus), cfg_(cfg)
{
    if (address) memcpy(address_, address, sizeof(address_));
}

bool Ds18b20Driver::begin()
{
    if (!bus_) return false;
    bus_->begin();
    bus_->setWaitForConversion(false);
    readSeq_ = 0;
    valid_ = false;
    return true;
}

void Ds18b20Driver::tick(uint32_t nowMs)
{
    if (!bus_) return;

    // La conversion est arbitree par le bus : plusieurs sondes peuvent le
    // partager, une seule conversion est en vol et toutes lisent la meme.
    if (!bus_->tickConversion(nowMs, cfg_.pollMs, cfg_.conversionWaitMs)) return;

    // Une seule lecture par conversion.
    const uint32_t seq = bus_->conversionSeq();
    if (seq == readSeq_) return;
    readSeq_ = seq;

    const float c = bus_->readC(address_);
    if (c != DEVICE_DISCONNECTED_C) {
        celsius_ = c;
        valid_ = true;
    } else {
        valid_ = false;
    }
}

bool Ds18b20Driver::readCelsius(float& out) const
{
    if (!valid_) return false;
    out = celsius_;
    return true;
}

bool Ds18b20Driver::readSample(uint8_t, IOAnalogSample& out) const
{
    float c = 0.0f;
    if (!readCelsius(c)) return false;
    out = IOAnalogSample{};
    out.value = c;
    // Sequence de conversion : evite de recalculer le slot a chaque tick alors
    // qu'une mesure DS18B20 ne change qu'une fois par periode de scrutation.
    out.seq = readSeq_;
    out.hasSeq = true;
    return true;
}
