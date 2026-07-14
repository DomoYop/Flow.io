// Compile uniquement pour les profils piscine : les autres envs excluent
// Modules/IOModule et Modules/Network/HAModule de leur build_src_filter.
#if defined(FLOW_PROFILE_WAVESHARE) || defined(FLOW_PROFILE_FLOWIO)

#include "Domain/Pool/PoolIoHaDiscovery.h"

#include <stdio.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "Board/BoardSerialMap.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/MqttTopics.h"
#include "Core/Services/Services.h"
#include "Core/SystemLimits.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IOModule.h"
#include "Modules/Network/HAModule/HARuntime.h"

#ifndef FLOW_HA_BOOT_TRACE
#define FLOW_HA_BOOT_TRACE 0
#endif

#if FLOW_HA_BOOT_TRACE
#define POOLIO_HA_BOOT_TRACE(FMT, ...) Board::SerialMap::logSerial().printf("[HA-BOOT] " FMT "\r\n", ##__VA_ARGS__)
#else
#define POOLIO_HA_BOOT_TRACE(FMT, ...) do {} while (0)
#endif

namespace {

// Nombre de slots analogiques exposes a Home Assistant (historique).
constexpr uint8_t kAnalogHaSlots = 17;
constexpr uint8_t kDigitalHaSlots = Limits::Io::MaxDigitalInputs;

struct PoolIoDiscoveryHeap {
    char analogObjectSuffix[kAnalogHaSlots][24]{};
    char analogFallbackName[kAnalogHaSlots][24]{};
    char analogValueTpl[kAnalogHaSlots][128]{};
    char analogStateSuffix[kAnalogHaSlots][24]{};
    char digitalObjectSuffix[kDigitalHaSlots][24]{};
    char digitalFallbackName[kDigitalHaSlots][24]{};
    char digitalStateSuffix[kDigitalHaSlots][24]{};
    char switchStateSuffix[Limits::Io::MaxPoolDevices][24]{};
    char switchPayloadOn[Limits::Io::MaxPoolDevices][Limits::IoHaSwitchPayloadBuf]{};
    char switchPayloadOff[Limits::Io::MaxPoolDevices][Limits::IoHaSwitchPayloadBuf]{};
};

PoolIoDiscoveryHeap* gDiscoveryHeap = nullptr;
bool gDiscoveryHeapReleaseWaitLogged = false;
bool gOneShotRefreshBypassedLogged = false;

bool ensureDiscoveryHeap()
{
    if (gDiscoveryHeap) return true;
    gDiscoveryHeap = static_cast<PoolIoDiscoveryHeap*>(
        heap_caps_calloc(1, sizeof(PoolIoDiscoveryHeap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (!gDiscoveryHeap) {
        gDiscoveryHeap = static_cast<PoolIoDiscoveryHeap*>(
            heap_caps_calloc(1, sizeof(PoolIoDiscoveryHeap), MALLOC_CAP_8BIT)
        );
    }
    if (gDiscoveryHeap) {
        POOLIO_HA_BOOT_TRACE("flow.io discovery heap allocated (%u bytes)", (unsigned)sizeof(PoolIoDiscoveryHeap));
    } else {
        Log::error((LogModuleId)LogModuleIdValue::Core, "pool io HA discovery heap allocation failed");
    }
    return gDiscoveryHeap != nullptr;
}

void releaseDiscoveryHeapIfReady(const PoolIoHaContext& ctx)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gDiscoveryHeap || !ctx.dataStore) return;
    if (!haAutoconfigPublished(*ctx.dataStore)) {
        if (!gDiscoveryHeapReleaseWaitLogged) {
            POOLIO_HA_BOOT_TRACE("flow.io discovery heap waiting for HA publish completion");
            gDiscoveryHeapReleaseWaitLogged = true;
        }
        return;
    }
    heap_caps_free(gDiscoveryHeap);
    gDiscoveryHeap = nullptr;
    gDiscoveryHeapReleaseWaitLogged = false;
    POOLIO_HA_BOOT_TRACE("flow.io discovery heap released after HA one-shot publish");
#else
    (void)ctx;
#endif
}

void buildAnalogValueTemplate(const IOModule& io, uint8_t analogIdx, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    const int32_t precision = io.analogPrecision(analogIdx);
    snprintf(
        out,
        outLen,
        "{%% if value_json.value is number %%}{{ value_json.value | float | round(%ld) }}{%% else %%}unavailable{%% endif %%}",
        (long)precision
    );
}

void syncAnalogSensors(const PoolIoHaContext& ctx)
{
    if (!ctx.ha || !ctx.ha->addSensor || !ctx.io || !ctx.domain) return;
    if (!ensureDiscoveryHeap()) return;
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    for (uint8_t i = 0; i < kAnalogHaSlots; ++i) {
        if (!ctx.io->analogSlotPublished(i)) continue;
        const PoolRoleSpec* role = domainRoleForIoSlot(*ctx.domain, analogInputSlot(i));

        buildAnalogValueTemplate(*ctx.io, i, gDiscoveryHeap->analogValueTpl[i], sizeof(gDiscoveryHeap->analogValueTpl[i]));
        snprintf(gDiscoveryHeap->analogStateSuffix[i], sizeof(gDiscoveryHeap->analogStateSuffix[i]),
                 "rt/io/input/a%02u", (unsigned)i);
        if (role && role->haObjectSuffix) {
            snprintf(gDiscoveryHeap->analogObjectSuffix[i], sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                     "%s", role->haObjectSuffix);
        } else {
            snprintf(gDiscoveryHeap->analogObjectSuffix[i], sizeof(gDiscoveryHeap->analogObjectSuffix[i]),
                     "io_a%02u", (unsigned)i);
        }
        snprintf(gDiscoveryHeap->analogFallbackName[i], sizeof(gDiscoveryHeap->analogFallbackName[i]),
                 "A%02u", (unsigned)i);
        char endpointId[8] = {0};
        snprintf(endpointId, sizeof(endpointId), "a%02u", (unsigned)i);
        const char* label = role ? (role->haName ? role->haName : role->displayName) : nullptr;
        if (!label || label[0] == '\0') {
            label = ctx.io->endpointLabel(endpointId);
        }
        if (!label || label[0] == '\0') {
            label = gDiscoveryHeap->analogFallbackName[i];
        }
        const HASensorEntry entry{
            "io",
            gDiscoveryHeap->analogObjectSuffix[i],
            label,
            gDiscoveryHeap->analogStateSuffix[i],
            gDiscoveryHeap->analogValueTpl[i],
            nullptr,
            role ? role->haIcon : "mdi:sine-wave",
            role ? role->haUnit : nullptr,
            false,
            kAvailabilityTpl
        };
        (void)ctx.ha->addSensor(ctx.ha->ctx, &entry);
    }
}

void syncDigitalInputBinarySensors(const PoolIoHaContext& ctx)
{
    if (!ctx.ha || !ctx.ha->addBinarySensor || !ctx.ha->addSensor || !ctx.io || !ctx.domain) return;
    if (!ensureDiscoveryHeap()) return;
    static constexpr const char* kBoolTpl = "{{ 'True' if value_json.value else 'False' }}";
    static constexpr const char* kAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";
    static constexpr const char* kNumericTpl =
        "{% if value_json.value is number %}{{ value_json.value | float }}{% else %}unavailable{% endif %}";

    for (uint8_t logical = 0; logical < kDigitalHaSlots; ++logical) {
        if (!ctx.io->digitalInputSlotPublished(logical)) continue;
        const PoolRoleSpec* role = domainRoleForIoSlot(*ctx.domain, digitalInputSlot(logical));

        snprintf(gDiscoveryHeap->digitalStateSuffix[logical], sizeof(gDiscoveryHeap->digitalStateSuffix[logical]),
                 "rt/io/input/i%02u", (unsigned)logical);
        const char* objectSuffix = role ? role->haObjectSuffix : nullptr;
        if (!objectSuffix) {
            snprintf(gDiscoveryHeap->digitalObjectSuffix[logical], sizeof(gDiscoveryHeap->digitalObjectSuffix[logical]),
                     "io_di%u", (unsigned)(logical + 1U));
            objectSuffix = gDiscoveryHeap->digitalObjectSuffix[logical];
        }
        const char* name = role ? (role->haName ? role->haName : role->displayName) : nullptr;
        if (!name) {
            snprintf(gDiscoveryHeap->digitalFallbackName[logical], sizeof(gDiscoveryHeap->digitalFallbackName[logical]),
                     "Digital Input %u", (unsigned)(logical + 1U));
            name = gDiscoveryHeap->digitalFallbackName[logical];
        }
        const char* icon = role ? role->haIcon : "mdi:electric-switch";

        if (ctx.io->digitalInputValueType(logical) != IO_VAL_BOOL) {
            const HASensorEntry entry{
                "io",
                objectSuffix,
                name,
                gDiscoveryHeap->digitalStateSuffix[logical],
                kNumericTpl,
                nullptr,
                icon,
                role ? role->haUnit : nullptr,
                false,
                kAvailabilityTpl
            };
            (void)ctx.ha->addSensor(ctx.ha->ctx, &entry);
            continue;
        }

        const HABinarySensorEntry entry{
            "io",
            objectSuffix,
            name,
            gDiscoveryHeap->digitalStateSuffix[logical],
            kBoolTpl,
            nullptr,
            nullptr,
            icon
        };
        (void)ctx.ha->addBinarySensor(ctx.ha->ctx, &entry);
    }
}

void syncSwitches(const PoolIoHaContext& ctx)
{
    if (!ctx.ha || !ctx.ha->addSwitch || !ctx.io || !ctx.domain) return;
    if (!ensureDiscoveryHeap()) return;

    const DomainSpec& domain = *ctx.domain;
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& device = domain.poolDevices[i];
        const PoolRoleSpec* commandRole = domainRoleById(domain, device.commandSlot);
        if (!commandRole) continue;
        const IoSlotId ioSlot = commandRole->ioSlot;
        if (ioSlot == IO_SLOT_INVALID || ioSlotKind(ioSlot) != IO_SLOT_DIGITAL_OUTPUT) continue;

        const uint8_t logical = ioSlotIndex(ioSlot);
        if (!ctx.io->digitalOutputSlotWritable(logical)) continue;

        snprintf(gDiscoveryHeap->switchStateSuffix[i], sizeof(gDiscoveryHeap->switchStateSuffix[i]),
                 "rt/pdm/state/pd%u", (unsigned)device.id);
        bool payloadOk = true;

        if (device.id == PoolIds::DeviceFiltrationPump) {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":true}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"poollogic.filtration.write\\\",\\\"args\\\":{\\\"value\\\":false}}"
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        } else {
            int wrote = snprintf(
                gDiscoveryHeap->switchPayloadOn[i],
                sizeof(gDiscoveryHeap->switchPayloadOn[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":true}}",
                (unsigned)device.id
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOn[i]))) payloadOk = false;
            wrote = snprintf(
                gDiscoveryHeap->switchPayloadOff[i],
                sizeof(gDiscoveryHeap->switchPayloadOff[i]),
                "{\\\"cmd\\\":\\\"pooldevice.write\\\",\\\"args\\\":{\\\"slot\\\":%u,\\\"value\\\":false}}",
                (unsigned)device.id
            );
            if (!(wrote > 0 && wrote < (int)sizeof(gDiscoveryHeap->switchPayloadOff[i]))) payloadOk = false;
        }

        if (!payloadOk) {
            Log::error((LogModuleId)LogModuleIdValue::Core, "pool io HA switch payload overflow pd%u", (unsigned)device.id);
            continue;
        }

        const HASwitchEntry entry{
            "io",
            device.objectSuffix,
            commandRole->displayName,
            gDiscoveryHeap->switchStateSuffix[i],
            "{% if value_json.on %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCmd,
            gDiscoveryHeap->switchPayloadOn[i],
            gDiscoveryHeap->switchPayloadOff[i],
            device.haIcon,
            nullptr
        };
        (void)ctx.ha->addSwitch(ctx.ha->ctx, &entry);
    }
}

}  // namespace

namespace PoolIoHa {

void registerDiscovery(const PoolIoHaContext& ctx)
{
    if (!ctx.ha) return;

    syncAnalogSensors(ctx);
    syncDigitalInputBinarySensors(ctx);
    syncSwitches(ctx);

    if (ctx.ha->requestRefresh) {
        (void)ctx.ha->requestRefresh(ctx.ha->ctx);
    }
}

void refreshIfNeeded(const PoolIoHaContext& ctx)
{
#if FLOW_HA_ONESHOT_DISCOVERY
    if (!gOneShotRefreshBypassedLogged) {
        POOLIO_HA_BOOT_TRACE("flow.io IO->HA dynamic refresh bypassed in one-shot mode");
        gOneShotRefreshBypassedLogged = true;
    }
    releaseDiscoveryHeapIfReady(ctx);
    return;
#else
    if (!ctx.ha || !ctx.io) return;
    const uint32_t dirtyMask = ctx.io->takeAnalogConfigDirtyMask();
    if (dirtyMask == 0) return;

    syncAnalogSensors(ctx);
    if (ctx.ha->requestRefresh) {
        (void)ctx.ha->requestRefresh(ctx.ha->ctx);
    }
#endif
}

void releaseDiscoveryHeapIfDone(const PoolIoHaContext& ctx)
{
    releaseDiscoveryHeapIfReady(ctx);
}

}  // namespace PoolIoHa

#endif  // FLOW_PROFILE_WAVESHARE || FLOW_PROFILE_FLOWIO
