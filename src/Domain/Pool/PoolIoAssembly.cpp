// Compile uniquement pour les profils piscine : les autres envs excluent
// Modules/IOModule de leur build_src_filter mais compilent Domain/.
#if defined(FLOW_PROFILE_WAVESHARE) || defined(FLOW_PROFILE_FLOWIO)

#include "Domain/Pool/PoolIoAssembly.h"

#include <stdio.h>
#include <string.h>

#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/SystemLimits.h"
#include "Modules/IOModule/IOModule.h"

namespace {

bool fail_(const char* step)
{
    Log::error((LogModuleId)LogModuleIdValue::Core, "pool io setup failure: %s", step ? step : "unknown");
    return false;
}

const char* portName_(const PoolIoProfileSpec& spec, PhysicalPortId port)
{
    for (uint8_t i = 0; i < spec.portCount; ++i) {
        if (spec.ports[i].portId == port) return spec.ports[i].name;
    }
    return nullptr;
}

void setRegId_(IOEndpointRegistration& reg, const char* preferred, const char* fallback)
{
    const char* id = (preferred && preferred[0] != '\0') ? preferred : fallback;
    snprintf(reg.id, sizeof(reg.id), "%s", id ? id : "io");
}

}  // namespace

namespace PoolIo {

bool configure(const DomainSpec& domain, IOModule& io, const PoolIoProfileSpec& spec)
{
    if (!spec.ports || spec.portCount == 0U) return fail_("missing binding ports");
    io.setBindingPorts(spec.ports, spec.portCount);

    // 1) Tous les slots analogiques existent, nommes a00..aNN par defaut.
    for (uint8_t i = 0; i < Limits::Io::MaxAnalogEndpoints; ++i) {
        IOEndpointRegistration reg{};
        snprintf(reg.id, sizeof(reg.id), "a%02u", (unsigned)i);
        reg.ioId = (IoId)(IO_ID_AI_BASE + i);
        if (!io.defineAnalogInput(reg, IOAnalogSlotConfig{})) return fail_("define analog input slot");
    }

    // 2) Roles metier : capteurs analogiques et entrees digitales.
    for (uint8_t i = 0; i < domain.roleCount; ++i) {
        const PoolRoleSpec& role = domain.roles[i];
        if (role.ioSlot == IO_SLOT_INVALID) continue;
        const IoId ioId = ioIdFromSlot(role.ioSlot);
        if (ioId == IO_ID_INVALID) return fail_("invalid role IO mapping");

        if (ioSlotKind(role.ioSlot) == IO_SLOT_ANALOG_INPUT) {
            const AnalogRoleDefault* def = roleDefaultIn(spec.analogDefaults, spec.analogDefaultCount, role.id);
            if (!def) return fail_("unsupported analog domain role");
            IOEndpointRegistration reg{};
            setRegId_(reg, role.endpointId, "analog");
            reg.ioId = ioId;
            IOAnalogSlotConfig cfg{};
            cfg.bindingPort = def->bindingPort;
            cfg.c0 = def->c0;
            cfg.c1 = def->c1;
            cfg.precision = def->precision;
            if (!io.applyAnalogInputDefaults(reg, cfg)) return fail_("apply analog input defaults");
            continue;
        }

        if (ioSlotKind(role.ioSlot) == IO_SLOT_DIGITAL_INPUT) {
            const DigitalInputRoleDefault* def = roleDefaultIn(spec.dinDefaults, spec.dinDefaultCount, role.id);
            if (!def) return fail_("unsupported digital input domain role");
            IOEndpointRegistration reg{};
            // Le role metier nomme l'endpoint ; le port physique n'est qu'un
            // repli pour un role sans identite (comme sur le chemin analogique).
            setRegId_(reg, role.endpointId, portName_(spec, def->bindingPort));
            reg.ioId = ioId;
            IODigitalInputSlotConfig cfg{};
            cfg.bindingPort = def->bindingPort;
            cfg.activeHigh = false;
            cfg.pullMode = IO_PULL_UP;
            cfg.mode = def->mode;
            cfg.edgeMode = def->edgeMode;
            cfg.counterDebounceUs = (int32_t)def->debounceUs;
            if (!io.defineDigitalInput(reg, cfg)) return fail_("define digital input");
        }
    }

    // 3) Entrees digitales restantes (nommees d'apres leur port).
    for (uint8_t i = 0; i < spec.extraDigitalInputCount; ++i) {
        const PoolIoExtraEndpoint& extra = spec.extraDigitalInputs[i];
        if (extra.ioId < IO_ID_DI_BASE) return fail_("extra digital input id");
        const uint8_t logical = (uint8_t)(extra.ioId - IO_ID_DI_BASE);
        if (io.digitalInputSlotUsed(logical)) continue;
        IOEndpointRegistration reg{};
        char fallback[8];
        snprintf(fallback, sizeof(fallback), "i%02u", (unsigned)logical);
        setRegId_(reg, portName_(spec, extra.port), fallback);
        reg.ioId = extra.ioId;
        IODigitalInputSlotConfig cfg{};
        cfg.bindingPort = extra.port;
        cfg.activeHigh = false;
        cfg.pullMode = IO_PULL_UP;
        cfg.mode = IO_DIGITAL_INPUT_STATE;
        cfg.edgeMode = IO_EDGE_RISING;
        cfg.counterDebounceUs = 0;
        if (!io.defineDigitalInput(reg, cfg)) return fail_("define extra digital input");
    }

    // 4) Roles metier : sorties digitales.
    for (uint8_t i = 0; i < domain.roleCount; ++i) {
        const PoolRoleSpec& role = domain.roles[i];
        if (role.ioSlot == IO_SLOT_INVALID) continue;
        if (ioSlotKind(role.ioSlot) != IO_SLOT_DIGITAL_OUTPUT) continue;

        const DigitalOutputRoleDefault* def = roleDefaultIn(spec.doutDefaults, spec.doutDefaultCount, role.id);
        if (!def) return fail_("missing output layout binding");
        IOEndpointRegistration reg{};
        // Idem entrees digitales : "io_flt_pmp" et non "EXIO1". Un role non lie
        // (bindingPort invalide) gardait deja son identite metier, un role lie
        // la perdait au profit du nom de port : l'ordre etait inverse.
        setRegId_(reg, role.endpointId, portName_(spec, def->bindingPort));
        reg.ioId = ioIdFromSlot(role.ioSlot);
        IODigitalOutputSlotConfig cfg{};
        cfg.bindingPort = def->bindingPort;
        cfg.activeHigh = def->activeHigh;
        cfg.initialOn = false;
        cfg.startupPolicy = def->retainOnWarmReboot
            ? IOOutputStartupPolicy::PreserveHardwareState
            : IOOutputStartupPolicy::ApplyInitial;
        cfg.retainOnWarmReboot = def->retainOnWarmReboot;
        cfg.momentary = def->momentary;
        cfg.pulseMs = def->momentary ? (int32_t)def->pulseMs : 0;
        if (!io.defineDigitalOutput(reg, cfg)) return fail_("define digital output");
    }

    // 5) Sorties d'extension hors role (COMP...), nommees d'apres leur port.
    for (uint8_t i = 0; i < spec.extraDigitalOutputCount; ++i) {
        const PoolIoExtraEndpoint& extra = spec.extraDigitalOutputs[i];
        if (extra.ioId < IO_ID_DO_BASE) return fail_("extra digital output id");
        IOEndpointRegistration reg{};
        char fallback[8];
        snprintf(fallback, sizeof(fallback), "d%02u", (unsigned)(extra.ioId - IO_ID_DO_BASE));
        setRegId_(reg, portName_(spec, extra.port), fallback);
        reg.ioId = extra.ioId;
        IODigitalOutputSlotConfig cfg{};
        cfg.bindingPort = extra.port;
        cfg.activeHigh = true;
        cfg.initialOn = false;
        cfg.startupPolicy = IOOutputStartupPolicy::ApplyInitial;
        cfg.retainOnWarmReboot = false;
        cfg.momentary = false;
        cfg.pulseMs = 0;
        if (!io.defineDigitalOutput(reg, cfg)) return fail_("define extra digital output");
    }

    return true;
}

}  // namespace PoolIo

#endif  // FLOW_PROFILE_WAVESHARE || FLOW_PROFILE_FLOWIO
