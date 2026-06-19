/**
 * @file Ina228Driver.cpp
 * @brief Implementation file.
 */

#include "Ina228Driver.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"

namespace {
// Full-scale shunt voltage for the default ADC range (ADCRANGE=0 -> +/-163.84 mV).
constexpr float kIna228FullScaleShuntVoltage = 0.16384f;
}

Ina228Driver::Ina228Driver(const char* driverId, I2CBus* bus, const Ina228DriverConfig& cfg)
    : driverId_(driverId), bus_(bus), cfg_(cfg), ina_(cfg.address, bus ? bus->wire() : &Wire)
{
}

bool Ina228Driver::begin()
{
    if (!bus_) return false;
    if (cfg_.shuntOhms <= 0.0f) {
        LOGW("INA228 %s invalid shunt %.6f Ohm", driverId_ ? driverId_ : "sensor", (double)cfg_.shuntOhms);
        return false;
    }
    if (!bus_->lock(50)) return false;

    const bool ok = ina_.begin();
    if (ok) {
        const float maxCurrentA = kIna228FullScaleShuntVoltage / cfg_.shuntOhms;
        ina_.setADCRange(false);
        ina_.setMaxCurrentShunt(maxCurrentA, cfg_.shuntOhms);
        ina_.setMode(INA228_MODE_CONT_TEMP_BUS_SHUNT);
    }
    if (ok) {
        ready_ = true;
        valid_ = false;
        lastPollMs_ = 0;
        seq_ = 0;
    }

    bus_->unlock();

    if (!ok) {
        LOGW("INA228 %s not ready at 0x%02X", driverId_ ? driverId_ : "sensor", cfg_.address);
    }
    return ok;
}

void Ina228Driver::tick(uint32_t nowMs)
{
    if (!ready_ || !bus_) return;
    if ((uint32_t)(nowMs - lastPollMs_) < cfg_.pollMs) return;
    lastPollMs_ = nowMs;

    if (!bus_->lock(20)) return;

    // RobTillaart/INA228 has no per-read I2C error code (unlike INA226_WE), so
    // guard the whole acquisition cycle with a single connectivity probe.
    if (!ina_.isConnected()) {
        bus_->unlock();
        return;
    }

    const float shuntMv = ina_.getShuntMilliVolt();
    const float busV = ina_.getBusVoltage();
    const float currentMa = ina_.getMilliAmpere();
    const float powerMw = ina_.getMilliWatt();
    const float tempC = ina_.getTemperature();
    const float energyWh = (float)ina_.getWattHour();
    const float chargeMah = (float)(ina_.getCharge() / 3.6); // Coulomb -> mAh

    bus_->unlock();

    values_[0] = shuntMv;
    values_[1] = busV;
    values_[2] = currentMa;
    values_[3] = powerMw;
    values_[4] = busV + (shuntMv / 1000.0f);
    values_[5] = tempC;
    values_[6] = energyWh;
    values_[7] = chargeMah;
    valid_ = true;
    ++seq_;
}

bool Ina228Driver::readSample(uint8_t channel, IOAnalogSample& out) const
{
    out = IOAnalogSample{};
    if (!valid_ || channel > 7) return false;

    out.value = values_[channel];
    out.seq = seq_;
    out.hasSeq = true;
    return true;
}
