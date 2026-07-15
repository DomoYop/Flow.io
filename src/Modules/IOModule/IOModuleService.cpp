/**
 * @file IOModuleService.cpp
 * @brief IOServiceV2 implementation and status LED mask handling (PCF8574/TCA9554).
 */

#include "IOModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/IOModule/IoBackendTraits.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <new>
#include <stdlib.h>
#include <string.h>

uint8_t IOModule::ioCount_() const
{
    uint8_t count = 0;
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) ++count;
    }
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogSlots_[i].used) ++count;
    }
    return count;
}

IoStatus IOModule::ioIdAt_(uint8_t index, IoId* outId) const
{
    if (!outId) return IO_ERR_INVALID_ARG;
    uint8_t seen = 0;

    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (seen == index) {
            *outId = digitalSlots_[slotIdx].ioId;
            return IO_OK;
        }
        ++seen;
    }

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        if (seen == index) {
            *outId = analogSlots_[i].ioId;
            return IO_OK;
        }
        ++seen;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioMeta_(IoId id, IoEndpointMeta* outMeta) const
{
    if (!outMeta) return IO_ERR_INVALID_ARG;
    *outMeta = IoEndpointMeta{};
    outMeta->id = id;

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = (s.kind == DIGITAL_SLOT_OUTPUT) ? IO_KIND_DIGITAL_OUT : IO_KIND_DIGITAL_IN;
        outMeta->valueType = (s.kind == DIGITAL_SLOT_OUTPUT)
            ? IO_VAL_BOOL
            : ((s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_FLOAT : IO_VAL_BOOL);
        outMeta->backend = s.backend;
        outMeta->channel = s.channel;
        outMeta->capabilities = s.endpoint ? IO_CAP_R : 0;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.provider.isBound()) {
            outMeta->capabilities |= IO_CAP_W;
        }
        if (s.kind == DIGITAL_SLOT_INPUT && s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER && s.logicalIdx < MAX_DIGITAL_INPUTS) {
            outMeta->precision = sanitizeAnalogPrecision_(digitalInCfg_[s.logicalIdx].precision);
        }

        const char* name = nullptr;
        if (s.kind == DIGITAL_SLOT_OUTPUT && s.logicalIdx < DIGITAL_CFG_SLOTS) {
            name = digitalCfg_[s.logicalIdx].name;
        } else if (s.kind == DIGITAL_SLOT_INPUT) {
            if (s.logicalIdx < MAX_DIGITAL_INPUTS && digitalInCfg_[s.logicalIdx].name[0] != '\0') {
                name = digitalInCfg_[s.logicalIdx].name;
            } else {
                name = s.id;
            }
        }
        if (!name || name[0] == '\0') name = s.endpointId;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used) return IO_ERR_UNKNOWN_ID;

        outMeta->kind = IO_KIND_ANALOG_IN;
        outMeta->valueType = IO_VAL_FLOAT;
        outMeta->capabilities = s.endpoint ? IO_CAP_R : 0;
        outMeta->channel = s.channel;
        outMeta->backend = s.backend;
        outMeta->precision = s.cfg.precision;
        outMeta->minValid = 0.0f;
        outMeta->maxValid = 0.0f;

        const char* name = (analogIdx < ANALOG_CFG_SLOTS) ? analogCfg_[analogIdx].name : nullptr;
        if (!name || name[0] == '\0') name = s.id;
        if (!name) name = "";
        strncpy(outMeta->name, name, sizeof(outMeta->name) - 1);
        outMeta->name[sizeof(outMeta->name) - 1] = '\0';
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioReadValue_(IoId id, IoValue* outValue) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    *outValue = IoValue{};

    uint8_t slotIdx = 0xFF;
    if (findDigitalSlotByIoId_(id, slotIdx)) {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_ ? lastCycle_->seq : 0U;
        if (v.valueType == IO_EP_VALUE_BOOL) {
            outValue->type = IO_VAL_BOOL;
            outValue->v.b = v.v.b ? 1U : 0U;
            return IO_OK;
        }
        if (v.valueType == IO_EP_VALUE_INT32) {
            outValue->type = IO_VAL_INT32;
            outValue->v.i32 = v.v.i;
            return IO_OK;
        }
        if (v.valueType == IO_EP_VALUE_FLOAT) {
            outValue->type = IO_VAL_FLOAT;
            outValue->v.f = v.v.f;
            return IO_OK;
        }
        return IO_ERR_TYPE_MISMATCH;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        const AnalogSlot& s = analogSlots_[analogIdx];
        if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

        IOEndpointValue v{};
        if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

        outValue->valid = 1U;
        outValue->type = IO_VAL_FLOAT;
        outValue->tsMs = v.timestampMs;
        outValue->cycleSeq = lastCycle_ ? lastCycle_->seq : 0U;
        outValue->v.f = v.v.f;
        return IO_OK;
    }

    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outOn) return IO_ERR_INVALID_ARG;

    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid) return IO_ERR_NOT_READY;
    if (v.valueType != IO_EP_VALUE_BOOL) return IO_ERR_TYPE_MISMATCH;

    *outOn = v.v.b ? 1U : 0U;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_ ? lastCycle_->seq : 0U;
    return IO_OK;
}

IoStatus IOModule::ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs)
{
    uint8_t slotIdx = 0xFF;
    if (!findDigitalSlotByIoId_(id, slotIdx)) return IO_ERR_UNKNOWN_ID;
    DigitalSlot& s = digitalSlots_[slotIdx];
    if (!s.used) return IO_ERR_UNKNOWN_ID;
    if (s.kind != DIGITAL_SLOT_OUTPUT) return IO_ERR_READ_ONLY;
    if (!s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue in{};
    in.timestampMs = (tsMs == 0) ? millis() : tsMs;
    in.valueType = IO_EP_VALUE_BOOL;
    in.v.b = (on != 0U);
    in.valid = true;
    if (!s.endpoint->write(in)) return IO_ERR_HW;

    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(s.endpointId, rtIdx)) {
            (void)setIoEndpointBool(*dataStore_, rtIdx, in.v.b, in.timestampMs);
        }
    }

    markIoCycleChanged_(s.ioId);
    return IO_OK;
}

IoStatus IOModule::ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const
{
    if (!outValue) return IO_ERR_INVALID_ARG;
    if (id < IO_ID_AI_BASE || id >= IO_ID_AI_MAX) return IO_ERR_UNKNOWN_ID;

    const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
    const AnalogSlot& s = analogSlots_[analogIdx];
    if (!s.used || !s.endpoint) return IO_ERR_NOT_READY;

    IOEndpointValue v{};
    if (!s.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return IO_ERR_NOT_READY;

    *outValue = v.v.f;
    if (outTsMs) *outTsMs = v.timestampMs;
    if (outSeq) *outSeq = lastCycle_ ? lastCycle_->seq : 0U;
    return IO_OK;
}

IoStatus IOModule::ioTick_(uint32_t nowMs)
{
    refreshAnalogConfigState_();

    if (!cfgData_.enabled) return IO_ERR_NOT_READY;
    if (!runtimeReady_) return IO_ERR_NOT_READY;

    if (pcfLastEnabled_ != cfgData_.pcfEnabled) {
        if (!cfgData_.pcfEnabled && ledMaskEp_) {
            uint8_t offLogical = 0;
            uint8_t offPhysical = pcfPhysicalFromLogical_(offLogical);
            ledMaskEp_->setMask(offPhysical, nowMs);
            pcfLogicalMask_ = offLogical;
            pcfLogicalValid_ = true;
            pcfEnableNeedsReinitWarned_ = false;
        } else if (cfgData_.pcfEnabled) {
            if (ledMaskEp_) {
                setLedMask_(cfgData_.pcfMaskDefault, nowMs);
                pcfEnableNeedsReinitWarned_ = false;
            } else if (!pcfEnableNeedsReinitWarned_) {
                LOGW("pcf_enabled changed at runtime but PCF endpoint was not provisioned at init; reboot required");
                pcfEnableNeedsReinitWarned_ = true;
            }
        }
        pcfLastEnabled_ = cfgData_.pcfEnabled;
    }

    beginIoCycle_(nowMs);
    scheduler_.tick(nowMs);
    traceDigitalCounters_(nowMs);
    return IO_OK;
}

IoStatus IOModule::ioLastCycle_(IoCycleInfo* outCycle) const
{
    if (!outCycle) return IO_ERR_INVALID_ARG;
    *outCycle = lastCycle_ ? *lastCycle_ : IoCycleInfo{};
    return IO_OK;
}

IoStatus IOModule::ioSensorStatus_(IoId id, IoSensorStatus* outStatus) const
{
    if (!outStatus) return IO_ERR_INVALID_ARG;
    *outStatus = IoSensorStatus{};
    outStatus->id = id;

    if (!cfgData_.enabled) {
        outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;
        return IO_OK;
    }

    if (id >= IO_ID_AI_BASE && id < IO_ID_AI_MAX) {
        const uint8_t analogIdx = (uint8_t)(id - IO_ID_AI_BASE);
        outStatus->kind = IO_KIND_ANALOG_IN;

        if (!analogSlots_[analogIdx].used) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_UNKNOWN_ID;
            return IO_ERR_UNKNOWN_ID;
        }

        if (analogIdx < ANALOG_CFG_SLOTS && analogCfg_[analogIdx].bindingPort == IO_PORT_INVALID) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED | IO_SENSOR_INVALID_NO_BINDING;
            return IO_OK;
        }

        if (!analogSlotPublished_(analogIdx)) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;

            if (analogIdx < ANALOG_CFG_SLOTS) {
                if (analogCfg_[analogIdx].bindingPort == IO_PORT_INVALID) {
                    outStatus->invalidReasons |= IO_SENSOR_INVALID_NO_BINDING;
                } else {
                    uint8_t source = IO_ANALOG_SOURCE_INVALID;
                    if (!resolveConfiguredAnalogSource_(analogIdx, source)) {
                        outStatus->invalidReasons |= IO_SENSOR_INVALID_NO_BINDING;
                    } else if (analogSourceRequiresDriverEnable_(source) && !analogSourceDriverEnabled_(source)) {
                        outStatus->invalidReasons |= IO_SENSOR_INVALID_DRIVER_DISABLED;
                    }
                }
            }

            return IO_OK;
        }

        outStatus->enabled = 1U;
        if (!analogSlots_[analogIdx].endpoint) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }

        IOEndpointValue v{};
        if (!analogSlots_[analogIdx].endpoint->read(v)) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }
        if (!v.valid) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NO_VALUE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }
        if (v.valueType != IO_EP_VALUE_FLOAT) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_TYPE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }

        outStatus->valid = 1U;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NONE;
        outStatus->tsMs = v.timestampMs;
        return IO_OK;
    }

    if (id >= IO_ID_DI_BASE && id < IO_ID_DI_MAX) {
        const uint8_t logicalIdx = (uint8_t)(id - IO_ID_DI_BASE);
        outStatus->kind = IO_KIND_DIGITAL_IN;

        if (logicalIdx < DIGITAL_INPUT_CFG_SLOTS &&
            digitalInCfg_[logicalIdx].bindingPort == IO_PORT_INVALID) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED | IO_SENSOR_INVALID_NO_BINDING;
            return IO_OK;
        }

        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByIoId_(id, slotIdx)) {
            outStatus->enabled = 0U;
            outStatus->invalidReasons = IO_SENSOR_INVALID_DISABLED;
            outStatus->invalidReasons |= IO_SENSOR_INVALID_UNKNOWN_ID;
            return IO_OK;
        }

        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (!s.used || s.kind != DIGITAL_SLOT_INPUT) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_SENSOR;
            return IO_ERR_TYPE_MISMATCH;
        }

        outStatus->enabled = 1U;
        if (!s.endpoint) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }

        IOEndpointValue v{};
        if (!s.endpoint->read(v)) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_READY;
            return IO_OK;
        }
        if (!v.valid) {
            outStatus->invalidReasons = IO_SENSOR_INVALID_NO_VALUE;
            outStatus->tsMs = v.timestampMs;
            return IO_OK;
        }

        outStatus->valid = 1U;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NONE;
        outStatus->tsMs = v.timestampMs;
        return IO_OK;
    }

    if (id >= IO_ID_DO_BASE && id < IO_ID_DO_MAX) {
        outStatus->kind = IO_KIND_DIGITAL_OUT;
        outStatus->invalidReasons = IO_SENSOR_INVALID_NOT_SENSOR;
        return IO_ERR_TYPE_MISMATCH;
    }

    outStatus->invalidReasons = IO_SENSOR_INVALID_UNKNOWN_ID;
    return IO_ERR_UNKNOWN_ID;
}

IoStatus IOModule::ioListInvalidSensors_(IoId* outIds, uint8_t maxIds, uint8_t* outCount) const
{
    if (!outCount) return IO_ERR_INVALID_ARG;
    *outCount = 0U;

    uint8_t written = 0U;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        IoSensorStatus st{};
        if (ioSensorStatus_((IoId)(IO_ID_AI_BASE + i), &st) != IO_OK) continue;
        if (!st.enabled || st.valid) continue;
        if (outIds && written < maxIds) outIds[written++] = st.id;
        if (*outCount < 0xFFU) ++(*outCount);
    }

    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        IoSensorStatus st{};
        if (ioSensorStatus_((IoId)(IO_ID_DI_BASE + logical), &st) != IO_OK) continue;
        if (!st.enabled || st.valid) continue;
        if (outIds && written < maxIds) outIds[written++] = st.id;
        if (*outCount < 0xFFU) ++(*outCount);
    }

    return IO_OK;
}

IoStatus IOModule::ioBackendInfo_(uint8_t backend, uint8_t* outEnabled, uint8_t* outConfigurable) const
{
    const IoBackendTraits* traits = backendTraits(backend);
    if (!traits) return IO_ERR_INVALID_ARG;

    // Always-on drivers (no config toggle) report enabled unconditionally.
    bool enabled = true;
    switch (backend) {
        case IO_BACKEND_PCF8574:    enabled = cfgData_.pcfEnabled; break;
        case IO_BACKEND_TCA9554:
#if defined(FLOW_PROFILE_WAVESHARE)
            // Mandatory on this board: the only path to drive digital outputs (EXIO1..8).
            enabled = true;
#else
            enabled = cfgData_.tca9554Enabled;
#endif
            break;
        // DS18B20 are read through any enabled 1-Wire transport (DS2484 or GPIO buses).
        case IO_BACKEND_DS18B20:
            enabled = cfgData_.ds2484Enabled || cfgData_.oneWire1Enabled || cfgData_.oneWire2Enabled;
            break;
        case IO_BACKEND_SHT40:      enabled = cfgData_.sht40Enabled; break;
        case IO_BACKEND_BMP280:     enabled = cfgData_.bmp280Enabled; break;
        case IO_BACKEND_BME680:     enabled = cfgData_.bme680Enabled; break;
        case IO_BACKEND_POWERMON:   enabled = cfgData_.powermonEnabled; break;
        case IO_BACKEND_MCP23017:   enabled = cfgData_.mcp23017Enabled; break;
        default: break;
    }
    if (outEnabled) *outEnabled = enabled ? 1U : 0U;
    if (outConfigurable) *outConfigurable = traits->configurableToggle ? 1U : 0U;
    return IO_OK;
}

bool IOModule::getLedMaskSvc_(uint8_t* mask) const
{
    if (!mask) return false;
    return getLedMask_(*mask);
}

bool IOModule::setLedMask_(uint8_t mask, uint32_t tsMs)
{
    if (!ledMaskEp_) return false;
    uint8_t physical = pcfPhysicalFromLogical_(mask);
    bool ok = ledMaskEp_->setMask(physical, tsMs);
    if (ok) {
        pcfLogicalMask_ = mask;
        pcfLogicalValid_ = true;
    }
    return ok;
}

bool IOModule::turnLedOn_(uint8_t bit, uint32_t tsMs)
{
    if (bit > 7) return false;
    uint8_t mask = 0;
    if (!getLedMask_(mask)) mask = 0;
    mask = (uint8_t)(mask | (uint8_t)(1u << bit));
    return setLedMask_(mask, tsMs);
}

bool IOModule::turnLedOff_(uint8_t bit, uint32_t tsMs)
{
    if (bit > 7) return false;
    uint8_t mask = 0;
    if (!getLedMask_(mask)) mask = 0;
    mask = (uint8_t)(mask & (uint8_t)~(1u << bit));
    return setLedMask_(mask, tsMs);
}

bool IOModule::getLedMask_(uint8_t& mask) const
{
    if (!ledMaskEp_ && !pcfLogicalValid_) return false;
    if (pcfLogicalValid_) {
        mask = pcfLogicalMask_;
        return true;
    }
    if (!ledMaskEp_) return false;
    uint8_t physical = 0;
    if (!ledMaskEp_->getMask(physical)) return false;
    mask = pcfLogicalFromPhysical_(physical);
    return true;
}

uint8_t IOModule::pcfPhysicalFromLogical_(uint8_t logicalMask) const
{
    return cfgData_.pcfActiveLow ? (uint8_t)~logicalMask : logicalMask;
}

uint8_t IOModule::pcfLogicalFromPhysical_(uint8_t physicalMask) const
{
    return cfgData_.pcfActiveLow ? (uint8_t)~physicalMask : physicalMask;
}
