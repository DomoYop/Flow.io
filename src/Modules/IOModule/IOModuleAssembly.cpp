/**
 * @file IOModuleAssembly.cpp
 * @brief IOModule hardware assembly: binding resolution, DS18B20 discovery, runtime configuration and driver/endpoint pools.
 */

#include "IOModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/IOModule/IoAnalogSlotDefaults.h"
#include "Modules/IOModule/IoBackendTraits.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <new>
#include <stdlib.h>
#include <string.h>

static void formatDs18Address_(const uint8_t addr[8], char* out, size_t outLen)
{
    if (!addr || !out || outLen == 0U) return;
    int pos = snprintf(out, outLen, "%02X", addr[0]);
    for (uint8_t i = 1; i < 8U && pos > 0 && (size_t)pos < outLen; ++i) {
        pos += snprintf(out + pos, outLen - (size_t)pos, ":%02X", addr[i]);
    }
}

static uint32_t counterDebounceUsFromConfigLocal(int32_t value)
{
    return (value <= 0) ? 0U : (uint32_t)value;
}

const IOBindingPortSpec* IOModule::bindingPortSpec_(PhysicalPortId portId) const
{
    if (portId == IO_PORT_INVALID || !bindingPorts_ || bindingPortCount_ == 0) return nullptr;
    for (uint8_t i = 0; i < bindingPortCount_; ++i) {
        if (bindingPorts_[i].portId == portId) return &bindingPorts_[i];
    }
    return nullptr;
}

void IOModule::autoBindEnabledAnalogDrivers_()
{
    if (!bindingPorts_ || bindingPortCount_ == 0) return;

    for (const IoBackendTraits& traits : kBackendTraits) {
        // Uniquement les backends analogiques dotes d'un toggle d'activation.
        if (!traits.analog || !traits.configurableToggle) continue;
        uint8_t enabled = 0U;
        uint8_t configurable = 0U;
        if (ioBackendInfo_(traits.backend, &enabled, &configurable) != IO_OK || !enabled) continue;

        for (uint8_t ch = 0; ch <= traits.maxChannel; ++ch) {
            // Canaux temperature/energie/charge fournis par l'INA228 seul.
            if (traits.backend == IO_BACKEND_POWERMON && cfgData_.powermonModel != 228 && ch >= 5U) {
                continue;
            }

            // Port physique correspondant a (backend, canal) pour cette carte.
            const IOBindingPortSpec* port = nullptr;
            for (uint8_t i = 0; i < bindingPortCount_; ++i) {
                if (bindingPorts_[i].backend == traits.backend && bindingPorts_[i].channel == ch) {
                    port = &bindingPorts_[i];
                    break;
                }
            }
            if (!port) continue;

            // Deja binde par un slot ? -> idempotent, on ne retouche rien.
            bool alreadyBound = false;
            for (uint8_t s = 0; s < ANALOG_CFG_SLOTS; ++s) {
                if (analogCfg_[s].bindingPort == port->portId) { alreadyBound = true; break; }
            }
            if (alreadyBound) continue;

            // Premier slot libre (non binde).
            int freeIdx = -1;
            for (uint8_t s = 0; s < ANALOG_CFG_SLOTS; ++s) {
                if (analogCfg_[s].bindingPort == IO_PORT_INVALID) { freeIdx = (int)s; break; }
            }
            if (freeIdx < 0) {
                LOGW("io.autobind: plus de slot analogique libre pour %s ch%u (port %u)",
                     ioBackendLabel(traits.backend), (unsigned)ch, (unsigned)port->portId);
                break;  // Inutile d'insister pour les canaux suivants de ce backend.
            }

            const uint8_t idx = (uint8_t)freeIdx;
            IOAnalogSlotConfig& slot = analogCfg_[idx];
            const IoAnalogSlotDefault* def = analogSlotDefault(traits.backend, ch);
            const char* name = (def && def->name) ? def->name : (port->name ? port->name : "");
            const float c0 = 1.0f;
            const float c1 = 0.0f;
            const int32_t precision = def ? def->precision : 1;

            // Persistance immediate du slot complet : le slot auto-binde devient
            // un vrai reglage NVS (nom + binding + calibration). Robuste aux
            // editions partielles de l'UI (un champ a la fois) et idempotent : au
            // boot suivant le port est deja binde en NVS -> saute, aucune reecriture.
            if (cfgStore_ && slotCfgVars_) {
                IoSlotConfigVars::AnalogVars& v = slotCfgVars_->analog[idx];
                cfgStore_->set(v.name, name);
                cfgStore_->set(v.binding, port->portId);
                cfgStore_->set(v.c0, c0);
                cfgStore_->set(v.c1, c1);
                cfgStore_->set(v.prec, precision);
            } else {
                // Repli non persistant si le store n'est pas encore pret.
                strncpy(slot.name, name, sizeof(slot.name) - 1);
                slot.name[sizeof(slot.name) - 1] = '\0';
                slot.bindingPort = port->portId;
                slot.c0 = c0;
                slot.c1 = c1;
                slot.precision = precision;
            }
            LOGI("io.autobind: %s ch%u -> slot a%02u (%s)",
                 ioBackendLabel(traits.backend), (unsigned)ch, (unsigned)idx, slot.name);
        }
    }
}

namespace {

// Transitional mapping to the analog provider pool index (IOAnalogSource).
// Chaque canal DS18B20 est un slot de temperature generique (rang 0..N-1).
uint8_t analogProviderSourceForSpec_(const IOBindingPortSpec& spec)
{
    switch (spec.backend) {
        case IO_BACKEND_ADS1115_INT: return IO_SRC_ADS_INTERNAL_SINGLE;
        case IO_BACKEND_ADS1115_EXT_DIFF: return IO_SRC_ADS_EXTERNAL_DIFF;
        case IO_BACKEND_DS18B20:
            // Borne obligatoire : un canal hors plage deborderait le pool de
            // providers, que le port vienne d'un profil ou de la config NVS.
            return (spec.channel < IO_DS18_SLOT_COUNT)
                       ? (uint8_t)(IO_SRC_DS18_1 + spec.channel)
                       : IO_ANALOG_SOURCE_INVALID;
        case IO_BACKEND_SHT40: return IO_SRC_SHT40;
        case IO_BACKEND_BMP280: return IO_SRC_BMP280;
        case IO_BACKEND_BME680: return IO_SRC_BME680;
        case IO_BACKEND_POWERMON: return IO_SRC_POWERMON;
        default: return IO_ANALOG_SOURCE_INVALID;
    }
}

}  // namespace

bool IOModule::resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;
    const IoBackendTraits* traits = backendTraits(spec->backend);
    if (!traits || !traits->analog) return false;
    if ((spec->flags & IO_PORT_DIR_IN) == 0U) return false;
    if (spec->channel > traits->maxChannel) return false;

    // `sourceOut` identifies the shared physical provider, while `channelOut` selects the logical measurement.
    const uint8_t source = analogProviderSourceForSpec_(*spec);
    if (source == IO_ANALOG_SOURCE_INVALID) return false;
    sourceOut = source;
    channelOut = spec->channel;
    backendOut = spec->backend;
    return true;
}

bool IOModule::resolveDigitalInputBinding_(PhysicalPortId portId, uint8_t& pinOut, uint8_t& backendOut, uint8_t& channelOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;
    if (spec->backend != IO_BACKEND_GPIO || (spec->flags & IO_PORT_DIR_IN) == 0U) return false;

    pinOut = spec->channel;
    backendOut = IO_BACKEND_GPIO;
    channelOut = spec->channel;
    return true;
}

bool IOModule::resolveDigitalOutputBinding_(PhysicalPortId portId,
                                            uint8_t& pinOut,
                                            uint8_t& backendOut,
                                            uint8_t& channelOut,
                                            bool& usesPcfOut,
                                            bool& usesTcaOut,
                                            bool& usesMcpOut) const
{
    const IOBindingPortSpec* spec = bindingPortSpec_(portId);
    if (!spec) return false;
    const IoBackendTraits* traits = backendTraits(spec->backend);
    if (!traits || (spec->flags & IO_PORT_DIR_OUT) == 0U) return false;
    if (spec->channel > traits->maxChannel) return false;

    usesPcfOut = spec->backend == IO_BACKEND_PCF8574;
    usesTcaOut = spec->backend == IO_BACKEND_TCA9554;
    usesMcpOut = spec->backend == IO_BACKEND_MCP23017;
    if (spec->backend != IO_BACKEND_GPIO && !usesPcfOut && !usesTcaOut && !usesMcpOut) return false;

    pinOut = (spec->backend == IO_BACKEND_GPIO) ? spec->channel : 0U;
    backendOut = spec->backend;
    channelOut = spec->channel;
    return true;
}

bool IOModule::parseDs18Address_(const char* str, uint8_t out[8])
{
    if (!str || !out) return false;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    uint8_t bytes[8] = {0};
    uint8_t n = 0;
    const char* p = str;
    while (*p && n < 8) {
        while (*p == ':' || *p == ' ' || *p == '-') ++p;
        if (!*p) break;
        const int hi = hexVal(p[0]);
        if (hi < 0) return false;
        const int lo = hexVal(p[1]);
        if (lo < 0) return false;
        bytes[n++] = (uint8_t)((hi << 4) | lo);
        p += 2;
    }
    if (n != 8) return false;
    memcpy(out, bytes, 8);
    return true;
}

uint32_t IOModule::dsPollForBus_(const IOneWireBus* bus) const
{
    int32_t poll = cfgData_.dsPollMs;
    if (bus == &ds2484Bus_) poll = cfgData_.ds2484PollMs;
    else if (bus == oneWireGpio1_) poll = cfgData_.oneWire1PollMs;
    else if (bus == oneWireGpio2_) poll = cfgData_.oneWire2PollMs;
    return (poll < 750) ? 750U : (uint32_t)poll;
}

void IOModule::scanDs18Buses_(Ds18BusScan& scan)
{
    scan.busCount = 0;

    if (cfgData_.oneWire1Enabled && oneWireGpio1_) {
        if (cfgData_.oneWire1Gpio >= 0) oneWireGpio1_->setPin(cfgData_.oneWire1Gpio);
        oneWireGpio1_->begin();
        scan.buses[scan.busCount++] = oneWireGpio1_;
    }
    if (cfgData_.oneWire2Enabled && oneWireGpio2_) {
        if (cfgData_.oneWire2Gpio >= 0) oneWireGpio2_->setPin(cfgData_.oneWire2Gpio);
        oneWireGpio2_->begin();
        scan.buses[scan.busCount++] = oneWireGpio2_;
    }
    if (cfgData_.ds2484Enabled) {
        ds2484Bus_.setAddress(cfgData_.ds2484Address);
        ds2484Bus_.begin();
        if (ds2484Bus_.present()) {
            scan.buses[scan.busCount++] = &ds2484Bus_;
            LOGI("DS2484 1-Wire bridge at 0x%02X, %u sensor(s) found",
                 (unsigned)ds2484Bus_.i2cAddress(), (unsigned)ds2484Bus_.deviceCount());
        } else {
            LOGW("DS2484 enabled but not responding at 0x%02X", (unsigned)cfgData_.ds2484Address);
        }
    }

    // Un seul releve par bus : getAddress() relance une recherche 1-Wire
    // complete a chaque appel.
    for (uint8_t b = 0; b < scan.busCount; ++b) {
        scan.romCount[b] = 0;
        if (!scan.buses[b]) continue;
        uint8_t count = scan.buses[b]->deviceCount();
        if (count > Ds18BusScan::kMaxRomsPerBus) count = Ds18BusScan::kMaxRomsPerBus;
        for (uint8_t i = 0; i < count; ++i) {
            if (!scan.buses[b]->getAddress(i, scan.roms[b][scan.romCount[b]])) continue;
            char rom[24]{};
            formatDs18Address_(scan.roms[b][scan.romCount[b]], rom, sizeof(rom));
            LOGI("DS18B20 detectee bus=%u index=%u rom=%s", (unsigned)b, (unsigned)i, rom);
            ++scan.romCount[b];
        }
    }
}

bool IOModule::resolveDsSensor_(const Ds18BusScan& scan, uint8_t slotIdx,
                                const uint8_t (*takenAddrs)[8], uint8_t takenCount,
                                IOneWireBus** busOut, uint8_t outAddr[8])
{
    if (!busOut || !outAddr || slotIdx >= IO_DS18_SLOT_COUNT) return false;
    *busOut = nullptr;

    char* romCfg = cfgData_.dsRom[slotIdx];
    const size_t romCfgLen = sizeof(cfgData_.dsRom[slotIdx]);

    // 1) ROM configuree : elle fait foi, sur n'importe quel bus actif. C'est le
    //    seul moyen de corriger une affectation, donc jamais ecrasee ici.
    uint8_t target[8] = {0};
    if (romCfg[0] != '\0' && parseDs18Address_(romCfg, target)) {
        for (uint8_t b = 0; b < scan.busCount; ++b) {
            for (uint8_t i = 0; i < scan.romCount[b]; ++i) {
                if (memcmp(scan.roms[b][i], target, 8) != 0) continue;
                *busOut = scan.buses[b];
                memcpy(outAddr, target, 8);
                return true;
            }
        }
        LOGW("DS18B20 rom%u=%s absente des bus actifs", (unsigned)(slotIdx + 1U), romCfg);
        return false;
    }

    // 2) Champ vide : premiere ROM libre dans un ordre deterministe (bus 1,
    //    bus 2, DS2484 ; ordre de recherche 1-Wire). Le choix est ecrit dans la
    //    config, donc visible et modifiable ensuite depuis l'interface.
    for (uint8_t b = 0; b < scan.busCount; ++b) {
        for (uint8_t i = 0; i < scan.romCount[b]; ++i) {
            bool taken = false;
            for (uint8_t t = 0; t < takenCount && !taken; ++t) {
                taken = memcmp(scan.roms[b][i], takenAddrs[t], 8) == 0;
            }
            if (taken) continue;

            *busOut = scan.buses[b];
            memcpy(outAddr, scan.roms[b][i], 8);
            formatDs18Address_(outAddr, romCfg, romCfgLen);
            if (cfgStore_) (void)cfgStore_->set(dsRomVar_[slotIdx], romCfg);
            LOGI("DS18B20 rom%u auto-affectee bus=%u rom=%s",
                 (unsigned)(slotIdx + 1U), (unsigned)b, romCfg);
            return true;
        }
    }
    return false;
}

void IOModule::resolveDs18Sensors_()
{
    Ds18BusScan scan{};
    scanDs18Buses_(scan);

    uint8_t taken[IO_DS18_SLOT_COUNT][8] = {{0}};
    uint8_t takenCount = 0;

    for (uint8_t slot = 0; slot < IO_DS18_SLOT_COUNT; ++slot) {
        dsSlotBus_[slot] = nullptr;
        dsSlotAddrValid_[slot] = resolveDsSensor_(scan, slot, taken, takenCount,
                                                  &dsSlotBus_[slot], dsSlotAddr_[slot]);
        if (dsSlotAddrValid_[slot]) {
            memcpy(taken[takenCount++], dsSlotAddr_[slot], 8);
        }
    }
}

void IOModule::configureAnalogSlots_(bool (&needAnalogSource)[IO_SRC_COUNT])
{
    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!analogSlots_[i].used) continue;
        analogSlots_[i].ioId = (IoId)(IO_ID_AI_BASE + i);
        analogSlots_[i].source = IO_ANALOG_SOURCE_INVALID;
        analogSlots_[i].channel = 0U;
        analogSlots_[i].backend = IO_BACKEND_GPIO;
        analogSlots_[i].lastSampleSeqValid = false;
        analogSlots_[i].lastSampleSeq = 0;
        analogSlots_[i].lastRoundedValid = false;
        analogSlots_[i].lastRounded = 0.0f;

        if (i < ANALOG_CFG_SLOTS) {
            snprintf(analogSlots_[i].id, sizeof(analogSlots_[i].id), "a%02u", (unsigned)i);
            analogSlots_[i].cfg.bindingPort = analogCfg_[i].bindingPort;
            analogSlots_[i].cfg.c0 = analogCfg_[i].c0;
            analogSlots_[i].cfg.c1 = analogCfg_[i].c1;
            analogSlots_[i].cfg.precision = analogCfg_[i].precision;

            uint8_t source = IO_ANALOG_SOURCE_INVALID;
            uint8_t channel = 0U;
            uint8_t backend = IO_BACKEND_GPIO;
            if (resolveAnalogBinding_(analogSlots_[i].cfg.bindingPort, source, channel, backend)) {
                analogSlots_[i].source = source;
                analogSlots_[i].channel = channel;
                analogSlots_[i].backend = backend;
            } else if (analogSlots_[i].cfg.bindingPort != IO_PORT_INVALID) {
                LOGW("Analog %s unresolved binding_port=%u",
                     analogSlots_[i].id,
                     (unsigned)analogSlots_[i].cfg.bindingPort);
            }

            if (i < 3 && analogSlots_[i].source != IO_ANALOG_SOURCE_INVALID) {
                LOGI("Analog map %s binding_port=%u source=%u channel=%u",
                     analogSlots_[i].id,
                     (unsigned)analogSlots_[i].cfg.bindingPort,
                     (unsigned)analogSlots_[i].source,
                     (unsigned)analogSlots_[i].channel);
            }
        }

        if (analogSlots_[i].source < IO_SRC_COUNT) {
            needAnalogSource[analogSlots_[i].source] = true;
        } else {
            continue;
        }

        analogSlots_[i].endpoint = allocAnalogEndpoint_(analogSlots_[i].id);
        if (!analogSlots_[i].endpoint) continue;
        registry_.add(analogSlots_[i].endpoint);
    }
}

IOModule::ExpanderNeeds IOModule::scanExpanderNeeds_() const
{
    bool needPcfOutput = false;
    bool needTcaOutput = false;
    bool needMcpOutput = false;
    bool needTcaPreserveStartup = false;
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used || s.kind != DIGITAL_SLOT_OUTPUT) continue;
        PhysicalPortId bindingPort = s.outCfg.bindingPort;
        IOOutputStartupPolicy startupPolicy = s.outCfg.startupPolicy;
        if (s.logicalIdx < DIGITAL_CFG_SLOTS) {
            bindingPort = digitalCfg_[s.logicalIdx].bindingPort;
            startupPolicy = digitalCfg_[s.logicalIdx].startupPolicy;
        }
        const IOBindingPortSpec* spec = bindingPortSpec_(bindingPort);
        if (spec && spec->backend == IO_BACKEND_PCF8574) {
            needPcfOutput = true;
        }
        if (spec && spec->backend == IO_BACKEND_TCA9554) {
            needTcaOutput = true;
            if (startupPolicy == IOOutputStartupPolicy::PreserveHardwareState) {
                needTcaPreserveStartup = true;
            }
        }
        if (spec && spec->backend == IO_BACKEND_MCP23017) {
            needMcpOutput = true;
        }
    }

    ExpanderNeeds needs{};
    needs.pcf = needPcfOutput;
    needs.tca = needTcaOutput;
    needs.mcp = needMcpOutput;
    needs.tcaPreserveStartup = needTcaPreserveStartup;
    return needs;
}

void IOModule::beginI2cIfNeeded_(const bool (&needAnalogSource)[IO_SRC_COUNT], const ExpanderNeeds& needs)
{
    const bool needPcfOutput = needs.pcf;
    const bool needTcaOutput = needs.tca;
    const bool needMcpOutput = needs.mcp;

    const bool needI2c =
        needAnalogSource[IO_SRC_ADS_INTERNAL_SINGLE] ||
        needAnalogSource[IO_SRC_ADS_EXTERNAL_DIFF] ||
        needAnalogSource[IO_SRC_SHT40] ||
        needAnalogSource[IO_SRC_BMP280] ||
        needAnalogSource[IO_SRC_BME680] ||
        needAnalogSource[IO_SRC_POWERMON] ||
        cfgData_.sht40Enabled ||
        cfgData_.bmp280Enabled ||
        cfgData_.bme680Enabled ||
        cfgData_.powermonEnabled ||
        needPcfOutput ||
        needTcaOutput ||
        needMcpOutput;

    if (needI2c) {
        // Concrete bus/driver assembly is centralized here so the rest of the module can stay on kernel types.
        i2cBus_.begin(cfgData_.i2cSda, cfgData_.i2cScl);
        if (!i2cBus_.beginOk()) {
            LOGW("i2c.begin failed sda=%d scl=%d freq=%lu",
                 i2cBus_.beginSda(),
                 i2cBus_.beginScl(),
                 (unsigned long)i2cBus_.beginFrequencyHz());
        }
        const bool ads48Present = i2cBus_.probe(0x48);
        const bool ads49Present = i2cBus_.probe(0x49);
        LOGI("ADS1115 probe 0x48: %s", ads48Present ? "found" : "not found");
        LOGI("ADS1115 probe 0x49: %s", ads49Present ? "found" : "not found");
    }
}

void IOModule::configureDigitalInputSlot_(DigitalSlot& s, uint8_t slotIdx)
{
    const uint8_t cfgIdx = s.logicalIdx;
    if (cfgIdx < MAX_DIGITAL_INPUTS) {
        if (digitalInCfg_[cfgIdx].name[0] != '\0') {
            strncpy(s.id, digitalInCfg_[cfgIdx].name, sizeof(s.id) - 1);
            s.id[sizeof(s.id) - 1] = '\0';
        }
        s.inCfg.bindingPort = digitalInCfg_[cfgIdx].bindingPort;
        s.inCfg.activeHigh = digitalInCfg_[cfgIdx].activeHigh;
        uint8_t pull = digitalInCfg_[cfgIdx].pullMode;
        if (pull > IO_PULL_DOWN) pull = IO_PULL_NONE;
        s.inCfg.pullMode = pull;
        s.inCfg.mode = digitalInCfg_[cfgIdx].mode;
        s.inCfg.edgeMode = digitalInCfg_[cfgIdx].edgeMode;
        s.inCfg.counterDebounceUs = (int32_t)counterDebounceUsFromConfigLocal(digitalInCfg_[cfgIdx].counterDebounceUs);
    }

    snprintf(s.endpointId, sizeof(s.endpointId), "i%02u", (unsigned)s.logicalIdx);
    uint8_t pin = 0U;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t channel = 0U;
    if (!resolveDigitalInputBinding_(s.inCfg.bindingPort, pin, backend, channel)) {
        if (s.inCfg.bindingPort != IO_PORT_INVALID) {
            LOGW("Digital input %s unresolved binding_port=%u",
                 s.endpointId,
                 (unsigned)s.inCfg.bindingPort);
        }
        return;
    }
    s.backend = backend;
    s.channel = channel;
    IDigitalCounterDriver* driver = allocGpioDriver_(
        s.endpointId,
        pin,
        false,
        s.inCfg.activeHigh,
        s.inCfg.pullMode,
        s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER,
        s.inCfg.edgeMode,
        s.inCfg.counterDebounceUs
    );
    if (!driver) {
        LOGW("Digital input %s driver alloc failed pin=%u binding_port=%u mode=%u debounce_us=%lu",
             s.endpointId,
             (unsigned)pin,
             (unsigned)s.inCfg.bindingPort,
             (unsigned)s.inCfg.mode,
             (unsigned long)s.inCfg.counterDebounceUs);
        return;
    }

    s.provider = makeDigitalProvider(driver);
    if (!s.provider.begin()) {
        LOGW("Digital input %s driver begin failed id=%s pin=%u binding_port=%u mode=%u debounce_us=%lu",
             s.endpointId,
             driver->id() ? driver->id() : "?",
             (unsigned)pin,
             (unsigned)s.inCfg.bindingPort,
             (unsigned)s.inCfg.mode,
             (unsigned long)s.inCfg.counterDebounceUs);
        return;
    }

    const uint8_t valueType = (s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_EP_VALUE_FLOAT : IO_EP_VALUE_BOOL;
    s.endpoint = allocDigitalSensorEndpoint_(s.endpointId, valueType);
    if (!s.endpoint) return;
    if (s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER) {
        eraseLegacyCounterPersistedTotal_(s.logicalIdx);
        int32_t initialRawCount = 0;
        if (driver) {
            (void)driver->readCount(initialRawCount);
        }

        const IODigitalInputSlotConfig* cfg = (s.logicalIdx < MAX_DIGITAL_INPUTS) ? &digitalInCfg_[s.logicalIdx] : nullptr;
        const float c0 = cfg ? cfg->c0 : 1.0f;
        const int32_t precision = sanitizeAnalogPrecision_(cfg ? cfg->precision : 0);
        const float configTotal = cfg ? cfg->counterTotal : 0.0f;

        if (float* lastConfigTotal = counterConfigTotalState_(s.logicalIdx)) {
            *lastConfigTotal = configTotal;
        }
        s.counterScaledTotal = configTotal;
        s.counterScaledTotal += ((float)initialRawCount * c0);
        s.counterLastPersistedTotal = configTotal;
        s.counterLastRawCount = initialRawCount;
        s.counterLastFlushedRawCount = initialRawCount;
        s.counterLastPersistMs = millis();
        s.lastValid = false;
        const float scaledValue = ioRoundToPrecision(s.counterScaledTotal, precision);
        static_cast<DigitalSensorEndpoint*>(s.endpoint)->updateFloat(scaledValue, true, millis());
    }
    registry_.add(s.endpoint);
    (void)processDigitalInputDefinition_(slotIdx, millis());
}

void IOModule::configureDigitalOutputSlot_(DigitalSlot& s, const ExpanderNeeds& needs, bool& mcpProbeFailed)
{
    const bool needPcfOutput = needs.pcf;
    const bool needTcaOutput = needs.tca;
    const bool needTcaPreserveStartup = needs.tcaPreserveStartup;

    const uint8_t cfgIdx = s.logicalIdx;
    if (cfgIdx < DIGITAL_CFG_SLOTS) {
        snprintf(s.id, sizeof(s.id), "d%02u", (unsigned)cfgIdx);
        s.outCfg.bindingPort = digitalCfg_[cfgIdx].bindingPort;
        s.outCfg.activeHigh = digitalCfg_[cfgIdx].activeHigh;
        s.outCfg.initialOn = digitalCfg_[cfgIdx].initialOn;
        s.outCfg.startupPolicy = digitalCfg_[cfgIdx].startupPolicy;
        s.outCfg.retainOnWarmReboot = digitalCfg_[cfgIdx].retainOnWarmReboot;
        s.outCfg.momentary = digitalCfg_[cfgIdx].momentary;
        int32_t p = digitalCfg_[cfgIdx].pulseMs;
        if (p <= 0) p = 500;
        if (p > 60000) p = 60000;
        s.outCfg.pulseMs = (uint16_t)p;
    } else {
        snprintf(s.id, sizeof(s.id), "d%02u", (unsigned)s.logicalIdx);
    }

    strncpy(s.endpointId, s.id, sizeof(s.endpointId) - 1);
    s.endpointId[sizeof(s.endpointId) - 1] = '\0';

    uint8_t pin = 0U;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t channel = 0U;
    bool usesPcfOut = false;
    bool usesTcaOut = false;
    bool usesMcpOut = false;
    if (!resolveDigitalOutputBinding_(s.outCfg.bindingPort, pin, backend, channel, usesPcfOut, usesTcaOut, usesMcpOut)) {
        if (s.outCfg.bindingPort != IO_PORT_INVALID) {
            LOGW("Digital output %s unresolved binding_port=%u",
                 s.endpointId,
                 (unsigned)s.outCfg.bindingPort);
        }
        return;
    }
    s.backend = backend;
    s.channel = channel;
    if (s.outCfg.retainOnWarmReboot && !usesTcaOut) {
        LOGW("Digital output %s retain_on_warm_reboot ignored: backend is not TCA9554", s.endpointId);
    }

    IDigitalPinDriver* driver = nullptr;
    if (usesPcfOut) {
        if (needTcaOutput) {
            LOGW("Digital output %s uses PCF8574 but TCA9554 outputs are also configured; mixed expanders not supported", s.endpointId);
            return;
        }
        if (!cfgData_.pcfEnabled) {
            LOGW("Digital output %s requires PCF8574 but module is disabled", s.endpointId);
            return;
        }
        if (!pcfDriver_) {
            IMaskOutputDriver* pcfMaskDriver = allocPcfDriver_("pcf8574", &i2cBus_, cfgData_.pcfAddress);
            if (!pcfMaskDriver) {
                LOGW("PCF8574 pool exhausted");
                return;
            }
            pcfDriver_ = static_cast<Pcf8574Driver*>(pcfMaskDriver);
            if (!makeMaskProvider(pcfDriver_).begin()) {
                LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
                pcfDriver_ = nullptr;
                return;
            }
        }
        driver = allocPcfBitDriver_(s.id, pcfDriver_, channel, s.outCfg.activeHigh);
    } else if (usesTcaOut) {
        if (needPcfOutput) {
            LOGW("Digital output %s uses TCA9554 but PCF8574 outputs are also configured; mixed expanders not supported", s.endpointId);
            return;
        }
        if (!cfgData_.tca9554Enabled) {
            LOGW("Digital output %s requires TCA9554 but expander module is disabled", s.endpointId);
            return;
        }
        if (!tcaDriver_) {
            IMaskOutputDriver* tcaMaskDriver = allocTcaDriver_("tca9554", &i2cBus_, cfgData_.tca9554Address);
            if (!tcaMaskDriver) {
                LOGW("TCA9554 pool exhausted");
                return;
            }
            tcaDriver_ = static_cast<Tca9554Driver*>(tcaMaskDriver);
            const bool tcaBeginOk = needTcaPreserveStartup
                ? tcaDriver_->beginPreserveHardwareState()
                : makeMaskProvider(tcaDriver_).begin();
            if (!tcaBeginOk) {
                LOGW("TCA9554 not detected at 0x%02X", cfgData_.tca9554Address);
                tcaDriver_ = nullptr;
                return;
            }
        }
        driver = allocTcaBitDriver_(s.id, tcaDriver_, channel, s.outCfg.activeHigh);
    } else if (usesMcpOut) {
        if (needPcfOutput) {
            LOGW("Digital output %s uses MCP23017 but PCF8574 outputs are also configured; mixed expanders not supported", s.endpointId);
            return;
        }
        if (mcpProbeFailed) {
            LOGW("Digital output %s requires MCP23017 but expander is unavailable", s.endpointId);
            return;
        }
        if (!cfgData_.mcp23017Enabled) {
            LOGW("Digital output %s requires MCP23017 but module is disabled", s.endpointId);
            return;
        }
        if (!mcpDriver_) {
            mcpDriver_ = allocMcpDriver_("mcp23017", &i2cBus_, cfgData_.mcp23017Address);
            if (!mcpDriver_) {
                LOGW("MCP23017 pool exhausted");
                return;
            }
            if (!mcpDriver_->begin()) {
                LOGW("MCP23017 not detected at 0x%02X", cfgData_.mcp23017Address);
                mcpDriver_ = nullptr;
                mcpProbeFailed = true;
                return;
            }
        }
        driver = allocMcpBitDriver_(s.id, mcpDriver_, channel, s.outCfg.activeHigh);
    } else {
        driver = allocGpioDriver_(s.id, pin, true, s.outCfg.activeHigh);
    }
    if (!driver) return;

    s.provider = makeDigitalProvider(driver);
    if (!s.provider.begin()) return;
    s.pulseArmed = false;
    s.pulseDeadlineMs = 0;

    s.endpoint = static_cast<IOEndpoint*>(allocDigitalActuatorEndpoint_(
        s.id,
        &IOModule::writeDigitalOut_,
        &s
    ));
    if (!s.endpoint) return;
    registry_.add(s.endpoint);

    bool actualOn = s.outCfg.initialOn;
    const bool preserveStartup =
        s.outCfg.startupPolicy == IOOutputStartupPolicy::PreserveHardwareState;
    if (preserveStartup) {
        if (!s.provider.read(actualOn)) {
            LOGW("Digital output %s startup state adoption failed", s.endpointId);
        }
    } else {
        (void)s.provider.write(s.outCfg.initialOn);
        actualOn = s.outCfg.initialOn;
    }

    const uint32_t nowMs = millis();
    static_cast<DigitalActuatorEndpoint*>(s.endpoint)->adoptValue(actualOn, nowMs);
    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(s.endpointId, rtIdx)) {
            (void)setIoEndpointBool(*dataStore_, rtIdx, actualOn, nowMs);
        }
    }
    markIoCycleChanged_(s.ioId);
}

void IOModule::probeConfiguredI2cDevices_(const bool (&needAnalogSource)[IO_SRC_COUNT], const ExpanderNeeds& needs)
{
    const bool needPcfOutput = needs.pcf;
    const bool needTcaOutput = needs.tca;

    if (needAnalogSource[IO_SRC_SHT40] || cfgData_.sht40Enabled) {
        const bool present = i2cBus_.probe(cfgData_.sht40Address);
        LOGI("SHT40 probe 0x%02X: %s", cfgData_.sht40Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_BMP280] || cfgData_.bmp280Enabled) {
        const bool present = i2cBus_.probe(cfgData_.bmp280Address);
        LOGI("BMP280 probe 0x%02X: %s", cfgData_.bmp280Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_BME680] || cfgData_.bme680Enabled) {
        const bool present = i2cBus_.probe(cfgData_.bme680Address);
        LOGI("BME680 probe 0x%02X: %s", cfgData_.bme680Address, present ? "found" : "not found");
    }

    if (needAnalogSource[IO_SRC_POWERMON] || cfgData_.powermonEnabled) {
        const bool present = i2cBus_.probe(cfgData_.powermonAddress);
        LOGI("Power monitor INA%u probe 0x%02X: %s", (unsigned)cfgData_.powermonModel,
             cfgData_.powermonAddress, present ? "found" : "not found");
    }

    if (needTcaOutput && !needPcfOutput && cfgData_.tca9554Enabled) {
        const bool present = i2cBus_.probe(cfgData_.tca9554Address);
        LOGI("TCA9554 probe 0x%02X: %s", cfgData_.tca9554Address, present ? "found" : "not found");
    }
}

void IOModule::configureAnalogProviders_(const bool (&needAnalogSource)[IO_SRC_COUNT])
{
    Ads1115DriverConfig adsInternalCfg{};
    adsInternalCfg.address = cfgData_.adsInternalAddr;
    adsInternalCfg.gain = (uint8_t)cfgData_.adsGain;
    adsInternalCfg.dataRate = (uint8_t)cfgData_.adsRate;
    adsInternalCfg.pollMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsInternalCfg.differentialPairs = false;

    Ads1115DriverConfig adsExternalCfg = adsInternalCfg;
    adsExternalCfg.address = cfgData_.adsExternalAddr;
    adsExternalCfg.differentialPairs = true;

    if (needAnalogSource[IO_SRC_ADS_INTERNAL_SINGLE]) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_internal", &i2cBus_, adsInternalCfg);
        if (!driver) {
            LOGW("ADS internal pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS internal not detected at 0x%02X", cfgData_.adsInternalAddr);
        } else {
            analogProviders_[IO_SRC_ADS_INTERNAL_SINGLE] = makeAnalogProvider(driver);
            if (cfgData_.adsInternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (internal)");
            }
        }
    }

    if (needAnalogSource[IO_SRC_ADS_EXTERNAL_DIFF]) {
        IAnalogSourceDriver* driver = allocAdsDriver_("ads_external", &i2cBus_, adsExternalCfg);
        if (!driver) {
            LOGW("ADS external pool exhausted");
        } else
        if (!makeAnalogProvider(driver).begin()) {
            LOGW("ADS external not detected at 0x%02X", cfgData_.adsExternalAddr);
        } else {
            analogProviders_[IO_SRC_ADS_EXTERNAL_DIFF] = makeAnalogProvider(driver);
            if (cfgData_.adsExternalAddr == 0x49) {
                LOGI("ADS1115 found at 0x49 (external)");
            }
        }
    }

    // Affecte une sonde (par ROM) a chaque slot de temperature, sur n'importe
    // lequel des bus 1-Wire actifs (pont DS2484 et/ou bus GPIO bit-bang).
    bool needAnyDs18 = false;
    for (uint8_t slot = 0; slot < IO_DS18_SLOT_COUNT && !needAnyDs18; ++slot) {
        needAnyDs18 = needAnalogSource[IO_SRC_DS18_1 + slot];
    }
    if (needAnyDs18) resolveDs18Sensors_();

    Ds18b20DriverConfig dsCfg{};
    dsCfg.conversionWaitMs = 750;

    for (uint8_t slot = 0; slot < IO_DS18_SLOT_COUNT; ++slot) {
        const uint8_t source = (uint8_t)(IO_SRC_DS18_1 + slot);
        if (!needAnalogSource[source]) continue;
        if (!dsSlotAddrValid_[slot] || !dsSlotBus_[slot]) {
            LOGW("Aucune sonde DS18B20 exploitable pour la temperature %u", (unsigned)(slot + 1U));
            continue;
        }

        // Ds18b20Driver conserve le pointeur d'identifiant : litteraux statiques
        // obligatoires, pas de buffer de pile.
        static const char* const kDsDriverIds[IO_DS18_SLOT_COUNT] = {
            "ds18_1", "ds18_2", "ds18_3", "ds18_4"
        };
        dsCfg.pollMs = dsPollForBus_(dsSlotBus_[slot]);
        IAnalogSourceDriver* driver =
            allocDsDriver_(kDsDriverIds[slot], dsSlotBus_[slot], dsSlotAddr_[slot], dsCfg);
        if (!driver) {
            LOGW("DS18 pool epuise pour la temperature %u", (unsigned)(slot + 1U));
            continue;
        }
        analogProviders_[source] = makeAnalogProvider(driver);
        (void)analogProviders_[source].begin();
    }

    if (needAnalogSource[IO_SRC_SHT40]) {
        if (!cfgData_.sht40Enabled) {
            LOGW("SHT40 required by analog slots but disabled");
        } else {
            Sht40DriverConfig shtCfg{};
            shtCfg.address = cfgData_.sht40Address;
            shtCfg.pollMs = (cfgData_.sht40PollMs < 250) ? 250U : (uint32_t)cfgData_.sht40PollMs;

            IAnalogSourceDriver* driver = allocSht40Driver_("sht40", &i2cBus_, shtCfg);
            if (!driver) {
                LOGW("SHT40 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_SHT40] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_BMP280]) {
        if (!cfgData_.bmp280Enabled) {
            LOGW("BMP280 required by analog slots but disabled");
        } else {
            Bmp280DriverConfig bmpCfg{};
            bmpCfg.address = cfgData_.bmp280Address;
            bmpCfg.pollMs = (cfgData_.bmp280PollMs < 100) ? 100U : (uint32_t)cfgData_.bmp280PollMs;

            IAnalogSourceDriver* driver = allocBmp280Driver_("bmp280", &i2cBus_, bmpCfg);
            if (!driver) {
                LOGW("BMP280 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_BMP280] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_BME680]) {
        if (!cfgData_.bme680Enabled) {
            LOGW("BME680 required by analog slots but disabled");
        } else {
            Bme680DriverConfig bmeCfg{};
            bmeCfg.address = cfgData_.bme680Address;
            bmeCfg.pollMs = (cfgData_.bme680PollMs < 250) ? 250U : (uint32_t)cfgData_.bme680PollMs;

            IAnalogSourceDriver* driver = allocBme680Driver_("bme680", &i2cBus_, bmeCfg);
            if (!driver) {
                LOGW("BME680 pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_BME680] = provider;
                }
            }
        }
    }

    if (needAnalogSource[IO_SRC_POWERMON]) {
        if (!cfgData_.powermonEnabled) {
            LOGW("Power monitor required by analog slots but disabled");
        } else {
            const uint32_t pollMs = (cfgData_.powermonPollMs < 100) ? 100U : (uint32_t)cfgData_.powermonPollMs;
            const float shuntOhms = (cfgData_.powermonShuntOhms > 0.0f) ? cfgData_.powermonShuntOhms : 0.1f;

            // Le modele choisit la puce physique. En 226, les canaux temperature/
            // energie/charge (5-7) ne sont pas fournis par le driver -> endpoints inactifs.
            IAnalogSourceDriver* driver = nullptr;
            if (cfgData_.powermonModel == 226) {
                Ina226DriverConfig inaCfg{};
                inaCfg.address = cfgData_.powermonAddress;
                inaCfg.pollMs = pollMs;
                inaCfg.shuntOhms = shuntOhms;
                driver = allocIna226Driver_("powermon", &i2cBus_, inaCfg);
            } else {
                Ina228DriverConfig inaCfg{};
                inaCfg.address = cfgData_.powermonAddress;
                inaCfg.pollMs = pollMs;
                inaCfg.shuntOhms = shuntOhms;
                driver = allocIna228Driver_("powermon", &i2cBus_, inaCfg);
            }

            if (!driver) {
                LOGW("Power monitor pool exhausted");
            } else {
                IOAnalogProvider provider = makeAnalogProvider(driver);
                if (provider.begin()) {
                    analogProviders_[IO_SRC_POWERMON] = provider;
                }
            }
        }
    }
}

void IOModule::configureLedMaskEndpoint_(const ExpanderNeeds& needs)
{
    const bool needPcfOutput = needs.pcf;
    const bool needTcaOutput = needs.tca;

    const bool ledExpanderEnabled = (needTcaOutput && !needPcfOutput) ? cfgData_.tca9554Enabled : cfgData_.pcfEnabled;
    if (ledExpanderEnabled) {
        // Do not expose/use the status LED mask endpoint when the expander lines
        // are already allocated to digital outputs (relays). Writing a global mask
        // would overwrite output startup states.
        const bool expanderUsedByDigitalOutputs = needPcfOutput || needTcaOutput;
        if (expanderUsedByDigitalOutputs) {
            LOGI("Status LED mask disabled: expander is mapped to digital outputs");
        } else {
        IMaskOutputDriver* driver = nullptr;
        if (needTcaOutput && !needPcfOutput) {
            driver = tcaDriver_ ? static_cast<IMaskOutputDriver*>(tcaDriver_)
                                : allocTcaDriver_("tca9554_led", &i2cBus_, cfgData_.tca9554Address);
            if (driver && !tcaDriver_) {
                tcaDriver_ = static_cast<Tca9554Driver*>(driver);
                if (!makeMaskProvider(driver).begin()) {
                    LOGW("TCA9554 not detected at 0x%02X", cfgData_.tca9554Address);
                    tcaDriver_ = nullptr;
                    driver = nullptr;
                }
            }
        } else {
            driver = pcfDriver_ ? static_cast<IMaskOutputDriver*>(pcfDriver_)
                                : allocPcfDriver_("pcf8574_led", &i2cBus_, cfgData_.pcfAddress);
            if (driver && !pcfDriver_) {
                pcfDriver_ = static_cast<Pcf8574Driver*>(driver);
                if (!makeMaskProvider(driver).begin()) {
                    LOGW("PCF8574 not detected at 0x%02X", cfgData_.pcfAddress);
                    pcfDriver_ = nullptr;
                    driver = nullptr;
                }
            }
        }
        if (!driver) {
            LOGW("%s pool exhausted",
                 (needTcaOutput && !needPcfOutput) ? "TCA9554" : "PCF8574");
        }
        if (driver) {
            ledMaskProvider_ = makeMaskProvider(driver);
            ledMaskEp_ = allocMaskEndpoint_(
                "status_leds_mask",
                [](void* ctx, uint8_t mask) -> bool {
                    return static_cast<IMaskOutputDriver*>(ctx)->writeMask(mask);
                },
                [](void* ctx, uint8_t* mask) -> bool {
                    if (!mask) return false;
                    return static_cast<IMaskOutputDriver*>(ctx)->readMask(*mask);
                },
                driver
            );
            if (ledMaskEp_) {
                registry_.add(ledMaskEp_);
                setLedMask_(cfgData_.pcfMaskDefault, millis());
            }
        }
        }
    }
}

void IOModule::registerSchedulerJobs_(bool needI2cAnalogJob, const ExpanderNeeds& needs)
{
    const bool needPcfOutput = needs.pcf;
    const bool needTcaOutput = needs.tca;

    IOScheduledJob adsJob{};
    adsJob.id = "ads_fast";
    adsJob.periodMs = (cfgData_.adsPollMs < 20) ? 20 : (uint32_t)cfgData_.adsPollMs;
    adsJob.fn = &IOModule::tickFastAds_;
    adsJob.ctx = this;
    scheduler_.add(adsJob);

    IOScheduledJob dsJob{};
    dsJob.id = "ds_slow";
    dsJob.periodMs = (cfgData_.dsPollMs < 250) ? 250 : (uint32_t)cfgData_.dsPollMs;
    dsJob.fn = &IOModule::tickSlowDs_;
    dsJob.ctx = this;
    scheduler_.add(dsJob);

    IOScheduledJob i2cAnalogJob{};
    if (needI2cAnalogJob) {
        i2cAnalogJob.id = "i2c_analog";
        i2cAnalogJob.periodMs = 20U;
        i2cAnalogJob.fn = &IOModule::tickI2cAnalogs_;
        i2cAnalogJob.ctx = this;
        scheduler_.add(i2cAnalogJob);
    }

    IOScheduledJob dinJob{};
    dinJob.id = "din_poll";
    dinJob.periodMs = (cfgData_.digitalPollMs < 20) ? 20 : (uint32_t)cfgData_.digitalPollMs;
    dinJob.fn = &IOModule::tickDigitalInputs_;
    dinJob.ctx = this;
    scheduler_.add(dinJob);

    // Le registre est fige : publier l'identite logique de chaque case avant
    // d'ouvrir le runtime aux consommateurs.
    publishEndpointIoIds_();

    runtimeReady_ = true;
    pcfLastEnabled_ = cfgData_.pcfEnabled;

    const char* expanderState = "off";
    if (needTcaOutput && !needPcfOutput) {
        if (cfgData_.tca9554Enabled) expanderState = "tca9554";
    } else if (cfgData_.pcfEnabled) {
        expanderState = "pcf8574";
    }

    LOGI("I/O ready (ads=%ldms ds=%ldms i2c_ai=%s din=%ldms endpoints=%u expander=%s)",
         (long)adsJob.periodMs,
         (long)dsJob.periodMs,
         needI2cAnalogJob ? "20ms" : "off",
         (long)dinJob.periodMs,
         (unsigned)registry_.count(),
         expanderState);
}

bool IOModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!cfgData_.enabled) return false;

    bool needAnalogSource[IO_SRC_COUNT] = {false};
    configureAnalogSlots_(needAnalogSource);

    const ExpanderNeeds expanders = scanExpanderNeeds_();
    beginI2cIfNeeded_(needAnalogSource, expanders);

    bool mcpProbeFailed = false;
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!digitalSlots_[i].used) continue;
        DigitalSlot& s = digitalSlots_[i];
        s.owner = this;
        s.ioId = (s.kind == DIGITAL_SLOT_OUTPUT)
                   ? (IoId)(IO_ID_DO_BASE + s.logicalIdx)
                   : (IoId)(IO_ID_DI_BASE + s.logicalIdx);

        if (s.kind == DIGITAL_SLOT_INPUT) {
            configureDigitalInputSlot_(s, i);
        } else {
            configureDigitalOutputSlot_(s, expanders, mcpProbeFailed);
        }
    }

    probeConfiguredI2cDevices_(needAnalogSource, expanders);
    configureAnalogProviders_(needAnalogSource);
    configureLedMaskEndpoint_(expanders);

    const bool needI2cAnalogJob = needAnalogSource[IO_SRC_SHT40]
        || needAnalogSource[IO_SRC_BMP280]
        || needAnalogSource[IO_SRC_BME680]
        || needAnalogSource[IO_SRC_POWERMON];
    registerSchedulerJobs_(needI2cAnalogJob, expanders);
    return true;
}

AnalogSensorEndpoint* IOModule::allocAnalogEndpoint_(const char* endpointId)
{
    if (!analogEndpointPool_) {
        analogEndpointPool_ = static_cast<AnalogSensorEndpoint*>(
            heap_caps_malloc(sizeof(AnalogSensorEndpoint) * MAX_ANALOG_ENDPOINTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (!analogEndpointPool_) {
            analogEndpointPool_ = static_cast<AnalogSensorEndpoint*>(
                heap_caps_malloc(sizeof(AnalogSensorEndpoint) * MAX_ANALOG_ENDPOINTS, MALLOC_CAP_8BIT)
            );
        }
        if (!analogEndpointPool_) return nullptr;
    }
    if (analogEndpointPoolUsed_ >= MAX_ANALOG_ENDPOINTS) return nullptr;
    void* mem = &analogEndpointPool_[analogEndpointPoolUsed_++];
    return new (mem) AnalogSensorEndpoint(endpointId);
}

DigitalSensorEndpoint* IOModule::allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType)
{
    if (digitalSensorEndpointPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
    void* mem = digitalSensorEndpointPool_[digitalSensorEndpointPoolUsed_++];
    return new (mem) DigitalSensorEndpoint(endpointId, valueType);
}

DigitalActuatorEndpoint* IOModule::allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx)
{
    if (digitalActuatorEndpointPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = digitalActuatorEndpointPool_[digitalActuatorEndpointPoolUsed_++];
    return new (mem) DigitalActuatorEndpoint(endpointId, writeFn, writeCtx);
}

IDigitalCounterDriver* IOModule::allocGpioDriver_(const char* driverId,
                                                  uint8_t pin,
                                                  bool output,
                                                  bool activeHigh,
                                                  uint8_t inputPullMode,
                                                  bool counterEnabled,
                                                  uint8_t edgeMode,
                                                  uint32_t counterDebounceUs)
{
    if (counterEnabled && !output) {
        if (gpioCounterDriverPoolUsed_ >= MAX_DIGITAL_INPUTS) return nullptr;
        void* mem = gpioCounterDriverPool_[gpioCounterDriverPoolUsed_++];
        return new (mem) PcntCounterDriver(driverId, pin, activeHigh, inputPullMode, edgeMode, counterDebounceUs);
    }

    if (gpioDriverPoolUsed_ >= MAX_DIGITAL_SLOTS) return nullptr;
    void* mem = gpioDriverPool_[gpioDriverPoolUsed_++];
    return new (mem) GpioDriver(driverId, pin, output, activeHigh, inputPullMode, false, 0);
}

IAnalogSourceDriver* IOModule::allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg)
{
    if (adsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = adsDriverPool_[adsDriverPoolUsed_++];
    return new (mem) Ads1115Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocDsDriver_(const char* driverId, IOneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg)
{
    if (dsDriverPoolUsed_ >= 2) return nullptr;
    void* mem = dsDriverPool_[dsDriverPoolUsed_++];
    return new (mem) Ds18b20Driver(driverId, bus, address, cfg);
}

IAnalogSourceDriver* IOModule::allocSht40Driver_(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg)
{
    if (sht40DriverPoolUsed_ >= 1) return nullptr;
    void* mem = sht40DriverPool_[sht40DriverPoolUsed_++];
    return new (mem) Sht40Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocBmp280Driver_(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg)
{
    if (bmp280DriverPoolUsed_ >= 1) return nullptr;
    void* mem = bmp280DriverPool_[bmp280DriverPoolUsed_++];
    return new (mem) Bmp280Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocBme680Driver_(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg)
{
    if (bme680DriverPoolUsed_ >= 1) return nullptr;
    void* mem = bme680DriverPool_[bme680DriverPoolUsed_++];
    return new (mem) Bme680Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocIna226Driver_(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg)
{
    if (ina226DriverPoolUsed_ >= 1) return nullptr;
    void* mem = ina226DriverPool_[ina226DriverPoolUsed_++];
    return new (mem) Ina226Driver(driverId, bus, cfg);
}

IAnalogSourceDriver* IOModule::allocIna228Driver_(const char* driverId, I2CBus* bus, const Ina228DriverConfig& cfg)
{
    if (ina228DriverPoolUsed_ >= 1) return nullptr;
    void* mem = ina228DriverPool_[ina228DriverPoolUsed_++];
    return new (mem) Ina228Driver(driverId, bus, cfg);
}

IDigitalPinDriver* IOModule::allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh)
{
    if (pcfBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Pcf8574BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Pcf8574BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++pcfBitDriverPoolUsed_;
    return new (mem) Pcf8574BitDriver(driverId, parent, bit, activeHigh);
}

IDigitalPinDriver* IOModule::allocTcaBitDriver_(const char* driverId, Tca9554Driver* parent, uint8_t bit, bool activeHigh)
{
    if (tcaBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Tca9554BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Tca9554BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++tcaBitDriverPoolUsed_;
    return new (mem) Tca9554BitDriver(driverId, parent, bit, activeHigh);
}

IDigitalPinDriver* IOModule::allocMcpBitDriver_(const char* driverId, Mcp23017Driver* parent, uint8_t bit, bool activeHigh)
{
    if (mcpBitDriverPoolUsed_ >= MAX_DIGITAL_OUTPUTS) return nullptr;
    void* mem = heap_caps_malloc(sizeof(Mcp23017BitDriver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Mcp23017BitDriver), MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    ++mcpBitDriverPoolUsed_;
    return new (mem) Mcp23017BitDriver(driverId, parent, bit, activeHigh);
}

IMaskOutputDriver* IOModule::allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (pcfDriverPoolUsed_ >= 1) return nullptr;
    void* mem = pcfDriverPool_[pcfDriverPoolUsed_++];
    return new (mem) Pcf8574Driver(driverId, bus, address);
}

IMaskOutputDriver* IOModule::allocTcaDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (tcaDriverPoolUsed_ >= 1) return nullptr;
    void* mem = tcaDriverPool_[tcaDriverPoolUsed_++];
    return new (mem) Tca9554Driver(driverId, bus, address);
}

Mcp23017Driver* IOModule::allocMcpDriver_(const char* driverId, I2CBus* bus, uint8_t address)
{
    if (mcpDriverPoolUsed_ >= 1) return nullptr;
    void* mem = mcpDriverPool_[mcpDriverPoolUsed_++];
    return new (mem) Mcp23017Driver(driverId, bus, address);
}

Pcf8574MaskEndpoint* IOModule::allocMaskEndpoint_(const char* endpointId, MaskWriteFn writeFn, MaskReadFn readFn, void* fnCtx)
{
    if (maskEndpointPoolUsed_ >= 1) return nullptr;
    void* mem = maskEndpointPool_[maskEndpointPoolUsed_++];
    return new (mem) Pcf8574MaskEndpoint(endpointId, writeFn, readFn, fnCtx);
}
