/**
 * @file IOModuleSnapshots.cpp
 * @brief IOModule endpoint labels, input/output/runtime snapshots and Runtime UI values.
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

static bool hasDecimalSuffixLocal(const char* p)
{
    if (!p || *p == '\0') return false;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        ++p;
    }
    return true;
}

static bool isInputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if ((id[0] == 'a' || id[0] == 'i') && hasDecimalSuffixLocal(id + 1)) return true;
    return false;
}

static bool isOutputEndpointIdLocal(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if (id[0] == 'd' && hasDecimalSuffixLocal(id + 1)) return true;
    return strcmp(id, "status_leds_mask") == 0;
}

const char* IOModule::endpointLabel(const char* endpointId) const
{
    if (!endpointId || endpointId[0] == '\0') return nullptr;
    if (endpointId[0] == 'a' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < ANALOG_CFG_SLOTS && analogCfg_[idx].name[0] != '\0') return analogCfg_[idx].name;
    }
    if (endpointId[0] == 'i' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        uint8_t slotIdx = 0xFF;
        if (findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, idx, slotIdx)) {
            const DigitalSlot& s = digitalSlots_[slotIdx];
            if (idx < MAX_DIGITAL_INPUTS && digitalInCfg_[idx].name[0] != '\0') return digitalInCfg_[idx].name;
            if (s.id[0] != '\0') return s.id;
        }
    }
    if (endpointId[0] == 'd' && hasDecimalSuffixLocal(endpointId + 1)) {
        uint8_t idx = (uint8_t)atoi(endpointId + 1);
        if (idx < DIGITAL_CFG_SLOTS && digitalCfg_[idx].name[0] != '\0') return digitalCfg_[idx].name;
    }
    return nullptr;
}

bool IOModule::buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, true, maxTsOut);
}

bool IOModule::buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const
{
    return buildGroupSnapshot_(out, len, false, maxTsOut);
}

bool IOModule::writeAnalogProviderRuntimeValue_(RuntimeUiId runtimeId,
                                                uint8_t source,
                                                uint8_t channel,
                                                IRuntimeUiWriter& writer) const
{
    const IOAnalogProvider* provider = analogProviderForSource_(source);
    if (!provider || !provider->isBound()) {
        return writer.writeUnavailable(runtimeId);
    }

    IOAnalogSample sample{};
    if (!provider->readSample(channel, sample)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeF32(runtimeId, sample.value);
}

bool IOModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    // Numero de slot analogique (a00, a01...), pas un index de registre : le
    // DataStore est resolu par IoId, cf. ioEndpointFloatByIoId.
    uint8_t analogSlotIdx = 0xFF;

    switch (valueId) {
        case RuntimeUiWaterCounter: {
            IoValue value{};
            const IoStatus st = ioReadValue_(
                ioIdFromSlot(digitalInputSlot(3)),
                &value
            );
            if (st != IO_OK || !value.valid) {
                return writer.writeUnavailable(runtimeId);
            }
            if (value.type == IO_VAL_FLOAT) return writer.writeF32(runtimeId, value.v.f);
            if (value.type == IO_VAL_INT32) return writer.writeI32(runtimeId, value.v.i32);
            return writer.writeUnavailable(runtimeId);
        }
        case RuntimeUiFlowSwitch: {
            IoValue value{};
            const IoStatus st = ioReadValue_(ioIdFromSlot(digitalInputSlot(4)), &value);
            if (st != IO_OK || !value.valid) return writer.writeUnavailable(runtimeId);
            if (value.type == IO_VAL_BOOL) return writer.writeBool(runtimeId, value.v.b != 0);
            return writer.writeUnavailable(runtimeId);
        }
        case RuntimeUiCoverClosed: {
            IoValue value{};
            const IoStatus st = ioReadValue_(ioIdFromSlot(digitalInputSlot(5)), &value);
            if (st != IO_OK || !value.valid) return writer.writeUnavailable(runtimeId);
            if (value.type == IO_VAL_BOOL) return writer.writeBool(runtimeId, value.v.b != 0);
            return writer.writeUnavailable(runtimeId);
        }
        case RuntimeUiPressure:
            analogSlotIdx = 2;
            break;
        case RuntimeUiBmp280Temp:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BMP280, 0U, writer);
        case RuntimeUiBme680Temp:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 0U, writer);
        case RuntimeUiBmp280Pressure:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BMP280, 1U, writer);
        case RuntimeUiSht40Temperature:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_SHT40, 0U, writer);
        case RuntimeUiSht40Humidity:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_SHT40, 1U, writer);
        case RuntimeUiBme680Humidity:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 1U, writer);
        case RuntimeUiBme680Pressure:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 2U, writer);
        case RuntimeUiBme680Gaz:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_BME680, 3U, writer);
        case RuntimeUiPowermonVoltage:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 1U, writer);
        case RuntimeUiPowermonCurrent:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 2U, writer);
        case RuntimeUiPowermonPower:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 3U, writer);
        case RuntimeUiPowermonTemperature:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 5U, writer);
        case RuntimeUiPowermonEnergy:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 6U, writer);
        case RuntimeUiPowermonCharge:
            return writeAnalogProviderRuntimeValue_(runtimeId, IO_SRC_POWERMON, 7U, writer);
        case RuntimeUiPh:
            analogSlotIdx = 1;
            break;
        case RuntimeUiOrp:
            analogSlotIdx = 0;
            break;
        default:
            return false;
    }

    if (!dataStore_) return writer.writeUnavailable(runtimeId);

    float value = 0.0f;
    if (!ioEndpointFloatByIoId(*dataStore_, ioIdFromSlot(analogInputSlot(analogSlotIdx)), value)) {
        return writer.writeUnavailable(runtimeId);
    }
    return writer.writeF32(runtimeId, value);
}

uint8_t IOModule::runtimeSnapshotCount() const
{
    if (!cfgData_.enabled) return 0;

    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (analogRuntimeRoutePublished_(i)) ++count;
    }
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (digitalRuntimeRoutePublished_(i)) ++count;
    }
    return count;
}

bool IOModule::runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    if (!cfgData_.enabled) return false;

    uint8_t seen = 0;
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogRuntimeRoutePublished_(i)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_ANALOG;
            slotIdxOut = i;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_INPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logical, slotIdx)) continue;
        if (!digitalRuntimeRoutePublished_(slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_INPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    for (uint8_t logical = 0; logical < MAX_DIGITAL_OUTPUTS; ++logical) {
        uint8_t slotIdx = 0xFF;
        if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logical, slotIdx)) continue;
        if (!digitalRuntimeRoutePublished_(slotIdx)) continue;
        if (seen == snapshotIdx) {
            routeTypeOut = ROUTE_DIGITAL_OUTPUT;
            slotIdxOut = slotIdx;
            return true;
        }
        ++seen;
    }
    return false;
}

bool IOModule::buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut, bool invalidAsUndefined) const
{
    if (!ep || !out || len == 0) return false;
    if ((ep->capabilities() & IO_CAP_READ) == 0) return false;

    IOEndpointValue v{};
    bool ok = ep->read(v);
    if (!ok) v.valid = false;

    const char* id = ep->id();
    const char* label = endpointLabel(id);
    int wrote = snprintf(out, len, "{\"id\":\"%s\",\"name\":\"%s\",\"available\":%s,\"value\":",
                         (id && id[0] != '\0') ? id : "",
                         (label && label[0] != '\0') ? label : ((id && id[0] != '\0') ? id : ""),
                         v.valid ? "true" : "false");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    size_t used = (size_t)wrote;

    if (!v.valid) {
        (void)invalidAsUndefined;
        wrote = snprintf(out + used, len - used, "null");
    } else if (v.valueType == IO_EP_VALUE_BOOL) {
        wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
    } else if (v.valueType == IO_EP_VALUE_FLOAT) {
        wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
    } else if (v.valueType == IO_EP_VALUE_INT32) {
        wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
    } else {
        wrote = snprintf(out + used, len - used, "null");
    }
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
    used += (size_t)wrote;

    // `held` n'apparait que quand il vaut vrai : la mesure est figee faute de
    // circulation. Une valeur plate sans marqueur serait indiscernable d'une
    // sonde morte, cote Home Assistant comme a l'ecran.
    if (v.held) {
        wrote = snprintf(out + used, len - used, ",\"held\":true");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;
    }

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    // Ensure one initial publish even if endpoint timestamp has not been set yet.
    maxTsOut = (v.timestampMs == 0U) ? 1U : v.timestampMs;
    return true;
}

const char* IOModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return nullptr;

    static char suffix[24];
    if (routeType == ROUTE_ANALOG) {
        snprintf(suffix, sizeof(suffix), "rt/io/input/a%02u", (unsigned)slotIdx);
    } else {
        const DigitalSlot& s = digitalSlots_[slotIdx];
        if (routeType == ROUTE_DIGITAL_INPUT) {
            snprintf(suffix, sizeof(suffix), "rt/io/input/i%02u", (unsigned)s.logicalIdx);
        } else {
            snprintf(suffix, sizeof(suffix), "rt/io/output/d%02u", (unsigned)s.logicalIdx);
        }
    }
    return suffix;
}

RuntimeRouteClass IOModule::runtimeSnapshotClass(uint8_t idx) const
{
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    (void)slotIdx;
    return (routeType == ROUTE_DIGITAL_OUTPUT)
        ? RuntimeRouteClass::ActuatorImmediate
        : RuntimeRouteClass::NumericThrottled;
}

bool IOModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (key < DATAKEY_IO_BASE || key >= (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return false;

    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;
    static constexpr uint8_t ROUTE_DIGITAL_OUTPUT = 2;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
    } else if (routeType == ROUTE_DIGITAL_INPUT || routeType == ROUTE_DIGITAL_OUTPUT) {
        ep = digitalSlots_[slotIdx].endpoint;
    } else {
        return false;
    }
    if (!ep || !ep->id()) return false;

    uint8_t endpointIdx = 0;
    if (!endpointIndexFromId_(ep->id(), endpointIdx)) return false;
    return key == (DataKey)(DATAKEY_IO_BASE + endpointIdx);
}

bool IOModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    static constexpr uint8_t ROUTE_ANALOG = 0;
    static constexpr uint8_t ROUTE_DIGITAL_INPUT = 1;

    uint8_t routeType = 0;
    uint8_t slotIdx = 0xFF;
    if (!runtimeSnapshotRouteFromIndex_(idx, routeType, slotIdx)) return false;

    IOEndpoint* ep = nullptr;
    if (routeType == ROUTE_ANALOG) {
        ep = static_cast<IOEndpoint*>(analogSlots_[slotIdx].endpoint);
        return buildEndpointSnapshot_(ep, out, len, maxTsOut, analogSlotUsesUndefinedInvalidValue_(slotIdx));
    }
    ep = digitalSlots_[slotIdx].endpoint;
    return buildEndpointSnapshot_(ep, out, len, maxTsOut);
}

bool IOModule::buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    size_t used = 0;
    int wrote = snprintf(out, len, "{");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    used += (size_t)wrote;

    bool first = true;
    uint32_t maxTs = 0;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep) continue;
        if ((ep->capabilities() & IO_CAP_READ) == 0) continue;

        const char* id = ep->id();
        if (!id || id[0] == '\0') continue;
        if (inputGroup && !isInputEndpointIdLocal(id)) continue;
        if (!inputGroup && !isOutputEndpointIdLocal(id)) continue;
        if (inputGroup && id[0] == 'a' && hasDecimalSuffixLocal(id + 1)) {
            const uint8_t analogIdx = (uint8_t)atoi(id + 1);
            if (!analogSlotPublished_(analogIdx)) continue;
        }

        IOEndpointValue v{};
        bool ok = ep->read(v);
        if (!ok) v.valid = false;

        const char* label = endpointLabel(id);
        wrote = snprintf(out + used, len - used, "%s\"%s\":{\"name\":\"%s\",\"available\":%s,\"value\":",
                         first ? "" : ",",
                         id,
                         (label && label[0] != '\0') ? label : id,
                         v.valid ? "true" : "false");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;
        first = false;

        if (!v.valid) {
            wrote = snprintf(out + used, len - used, "null");
        } else if (v.valueType == IO_EP_VALUE_BOOL) {
            wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
        } else if (v.valueType == IO_EP_VALUE_FLOAT) {
            wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
        } else if (v.valueType == IO_EP_VALUE_INT32) {
            wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
        } else {
            wrote = snprintf(out + used, len - used, "null");
        }
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        wrote = snprintf(out + used, len - used, "}");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        if (v.timestampMs > maxTs) maxTs = v.timestampMs;
    }

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    maxTsOut = maxTs;
    return true;
}
