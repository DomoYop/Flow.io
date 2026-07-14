#pragma once
/**
 * @file IoSlotConfigVars.h
 * @brief Generated ConfigStore variables for every IO slot (analog / DI / DO).
 *
 * Replaces the hand-written per-slot ConfigVariable members and the Extra*
 * structs: one generator owns the NVS keys ("io_a07bp"), the jsonNames
 * ("a07_c0", "binding_port"...) and the module names ("io/input/a07") for all
 * slots. Allocated once at init (PSRAM first, internal heap fallback) with the
 * same placement-new pattern as the former ExtraAnalogConfigVars.
 *
 * jsonName/moduleName are kept identical to the historical values so the web
 * UI, JSON export/import, cfgdocs and MQTT config routes are unaffected. NVS
 * keys are regenerated uniformly (no NVS backward compatibility, by decision).
 */

#include <stdint.h>
#include <stdio.h>

#include "Core/ConfigTypes.h"
#include "Core/SystemLimits.h"
#include "Modules/IOModule/IOModuleTypes.h"

struct IoSlotConfigVars {
    static constexpr uint8_t AnalogSlots = Limits::Io::AnalogConfigSlots;
    static constexpr uint8_t DigitalInSlots = Limits::Io::DigitalInputConfigSlots;
    static constexpr uint8_t DigitalOutSlots = Limits::Io::DigitalOutputConfigSlots;
    static_assert(AnalogSlots <= 100U && DigitalInSlots <= 100U && DigitalOutSlots <= 100U,
                  "generated slot keys use a two-digit index");

    struct AnalogVars {
        char keyName[12] = {0};
        char keyBinding[12] = {0};
        char keyC0[12] = {0};
        char keyC1[12] = {0};
        char keyPrec[12] = {0};
        char jsonName[10] = {0};   // "a07_name"
        char jsonC0[10] = {0};     // "a07_c0"
        char jsonC1[10] = {0};     // "a07_c1"
        char jsonPrec[10] = {0};   // "a07_prec"
        char moduleName[13] = {0}; // "io/input/a07"
        ConfigVariable<char, 0> name{};
        ConfigVariable<PhysicalPortId, 0> binding{};
        ConfigVariable<float, 0> c0{};
        ConfigVariable<float, 0> c1{};
        ConfigVariable<int32_t, 0> prec{};
    };

    struct DigitalInVars {
        char keyName[12] = {0};
        char keyBinding[12] = {0};
        char keyActiveHigh[12] = {0};
        char keyPullMode[12] = {0};
        char keyMode[12] = {0};
        char keyEdgeMode[12] = {0};
        char keyDebounce[12] = {0};
        char keyC0[12] = {0};
        char keyPrec[12] = {0};
        char keyCounterTotal[12] = {0};
        char jsonName[10] = {0};       // "i03_name"
        char jsonActiveHigh[16] = {0}; // "i03_active_high"
        char jsonPullMode[14] = {0};   // "i03_pull_mode"
        char jsonC0[8] = {0};          // "i03_c0"
        char jsonPrec[10] = {0};       // "i03_prec"
        char moduleName[13] = {0};     // "io/input/i03"
        ConfigVariable<char, 0> name{};
        ConfigVariable<PhysicalPortId, 0> binding{};
        ConfigVariable<bool, 0> activeHigh{};
        ConfigVariable<uint8_t, 0> pullMode{};
        ConfigVariable<uint8_t, 0> mode{};
        ConfigVariable<uint8_t, 0> edgeMode{};
        ConfigVariable<int32_t, 0> debounce{};
        ConfigVariable<float, 0> c0{};
        ConfigVariable<int32_t, 0> prec{};
        ConfigVariable<float, 0> counterTotal{};
    };

    struct DigitalOutVars {
        char keyName[12] = {0};
        char keyBinding[12] = {0};
        char keyActiveHigh[12] = {0};
        char keyInitialOn[12] = {0};
        char keyRetainWarm[12] = {0};
        char keyMomentary[12] = {0};
        char keyPulseMs[12] = {0};
        char jsonName[10] = {0};       // "d12_name"
        char jsonActiveHigh[16] = {0}; // "d12_active_high"
        char jsonInitialOn[15] = {0};  // "d12_initial_on"
        char jsonMomentary[14] = {0};  // "d12_momentary"
        char jsonPulseMs[13] = {0};    // "d12_pulse_ms"
        char moduleName[14] = {0};     // "io/output/d12"
        ConfigVariable<char, 0> name{};
        ConfigVariable<PhysicalPortId, 0> binding{};
        ConfigVariable<bool, 0> activeHigh{};
        ConfigVariable<bool, 0> initialOn{};
        ConfigVariable<bool, 0> retainWarm{};
        ConfigVariable<bool, 0> momentary{};
        ConfigVariable<int32_t, 0> pulseMs{};
    };

    AnalogVars analog[AnalogSlots]{};
    DigitalInVars din[DigitalInSlots]{};
    DigitalOutVars dout[DigitalOutSlots]{};

    IoSlotConfigVars(IOAnalogSlotConfig* analogCfg,
                     IODigitalInputSlotConfig* dinCfg,
                     IODigitalOutputSlotConfig* doutCfg)
    {
        for (uint8_t i = 0; i < AnalogSlots; ++i) {
            AnalogVars& v = analog[i];
            snprintf(v.keyName, sizeof(v.keyName), "io_a%02unm", (unsigned)i);
            snprintf(v.keyBinding, sizeof(v.keyBinding), "io_a%02ubp", (unsigned)i);
            snprintf(v.keyC0, sizeof(v.keyC0), "io_a%02uc0", (unsigned)i);
            snprintf(v.keyC1, sizeof(v.keyC1), "io_a%02uc1", (unsigned)i);
            snprintf(v.keyPrec, sizeof(v.keyPrec), "io_a%02upr", (unsigned)i);
            snprintf(v.jsonName, sizeof(v.jsonName), "a%02u_name", (unsigned)i);
            snprintf(v.jsonC0, sizeof(v.jsonC0), "a%02u_c0", (unsigned)i);
            snprintf(v.jsonC1, sizeof(v.jsonC1), "a%02u_c1", (unsigned)i);
            snprintf(v.jsonPrec, sizeof(v.jsonPrec), "a%02u_prec", (unsigned)i);
            snprintf(v.moduleName, sizeof(v.moduleName), "io/input/a%02u", (unsigned)i);
            v.name = {v.keyName, v.jsonName, v.moduleName, ConfigType::CharArray, (char*)analogCfg[i].name, ConfigPersistence::Persistent, sizeof(analogCfg[i].name)};
            v.binding = {v.keyBinding, "binding_port", v.moduleName, ConfigType::UInt16, &analogCfg[i].bindingPort, ConfigPersistence::Persistent, 0};
            v.c0 = {v.keyC0, v.jsonC0, v.moduleName, ConfigType::Float, &analogCfg[i].c0, ConfigPersistence::Persistent, 0};
            v.c1 = {v.keyC1, v.jsonC1, v.moduleName, ConfigType::Float, &analogCfg[i].c1, ConfigPersistence::Persistent, 0};
            v.prec = {v.keyPrec, v.jsonPrec, v.moduleName, ConfigType::Int32, &analogCfg[i].precision, ConfigPersistence::Persistent, 0};
        }

        for (uint8_t i = 0; i < DigitalInSlots; ++i) {
            DigitalInVars& v = din[i];
            snprintf(v.keyName, sizeof(v.keyName), "io_i%02unm", (unsigned)i);
            snprintf(v.keyBinding, sizeof(v.keyBinding), "io_i%02ubp", (unsigned)i);
            snprintf(v.keyActiveHigh, sizeof(v.keyActiveHigh), "io_i%02uah", (unsigned)i);
            snprintf(v.keyPullMode, sizeof(v.keyPullMode), "io_i%02upu", (unsigned)i);
            snprintf(v.keyMode, sizeof(v.keyMode), "io_i%02umd", (unsigned)i);
            snprintf(v.keyEdgeMode, sizeof(v.keyEdgeMode), "io_i%02ued", (unsigned)i);
            snprintf(v.keyDebounce, sizeof(v.keyDebounce), "io_i%02udb", (unsigned)i);
            snprintf(v.keyC0, sizeof(v.keyC0), "io_i%02uc0", (unsigned)i);
            snprintf(v.keyPrec, sizeof(v.keyPrec), "io_i%02upr", (unsigned)i);
            snprintf(v.keyCounterTotal, sizeof(v.keyCounterTotal), "io_i%02uct", (unsigned)i);
            snprintf(v.jsonName, sizeof(v.jsonName), "i%02u_name", (unsigned)i);
            snprintf(v.jsonActiveHigh, sizeof(v.jsonActiveHigh), "i%02u_active_high", (unsigned)i);
            snprintf(v.jsonPullMode, sizeof(v.jsonPullMode), "i%02u_pull_mode", (unsigned)i);
            snprintf(v.jsonC0, sizeof(v.jsonC0), "i%02u_c0", (unsigned)i);
            snprintf(v.jsonPrec, sizeof(v.jsonPrec), "i%02u_prec", (unsigned)i);
            snprintf(v.moduleName, sizeof(v.moduleName), "io/input/i%02u", (unsigned)i);
            v.name = {v.keyName, v.jsonName, v.moduleName, ConfigType::CharArray, (char*)dinCfg[i].name, ConfigPersistence::Persistent, sizeof(dinCfg[i].name)};
            v.binding = {v.keyBinding, "binding_port", v.moduleName, ConfigType::UInt16, &dinCfg[i].bindingPort, ConfigPersistence::Persistent, 0};
            v.activeHigh = {v.keyActiveHigh, v.jsonActiveHigh, v.moduleName, ConfigType::Bool, &dinCfg[i].activeHigh, ConfigPersistence::Persistent, 0};
            v.pullMode = {v.keyPullMode, v.jsonPullMode, v.moduleName, ConfigType::UInt8, &dinCfg[i].pullMode, ConfigPersistence::Persistent, 0};
            v.mode = {v.keyMode, "mode", v.moduleName, ConfigType::UInt8, &dinCfg[i].mode, ConfigPersistence::Persistent, 0};
            v.edgeMode = {v.keyEdgeMode, "edge_mode", v.moduleName, ConfigType::UInt8, &dinCfg[i].edgeMode, ConfigPersistence::Persistent, 0};
            v.debounce = {v.keyDebounce, "counter_debounce_us", v.moduleName, ConfigType::Int32, &dinCfg[i].counterDebounceUs, ConfigPersistence::Persistent, 0};
            v.c0 = {v.keyC0, v.jsonC0, v.moduleName, ConfigType::Float, &dinCfg[i].c0, ConfigPersistence::Persistent, 0};
            v.prec = {v.keyPrec, v.jsonPrec, v.moduleName, ConfigType::Int32, &dinCfg[i].precision, ConfigPersistence::Persistent, 0};
            v.counterTotal = {v.keyCounterTotal, "counter_total", v.moduleName, ConfigType::Float, &dinCfg[i].counterTotal, ConfigPersistence::Persistent, 0};
        }

        for (uint8_t i = 0; i < DigitalOutSlots; ++i) {
            DigitalOutVars& v = dout[i];
            snprintf(v.keyName, sizeof(v.keyName), "io_d%02unm", (unsigned)i);
            snprintf(v.keyBinding, sizeof(v.keyBinding), "io_d%02ubp", (unsigned)i);
            snprintf(v.keyActiveHigh, sizeof(v.keyActiveHigh), "io_d%02uah", (unsigned)i);
            snprintf(v.keyInitialOn, sizeof(v.keyInitialOn), "io_d%02uin", (unsigned)i);
            snprintf(v.keyRetainWarm, sizeof(v.keyRetainWarm), "io_d%02urt", (unsigned)i);
            snprintf(v.keyMomentary, sizeof(v.keyMomentary), "io_d%02umo", (unsigned)i);
            snprintf(v.keyPulseMs, sizeof(v.keyPulseMs), "io_d%02upm", (unsigned)i);
            snprintf(v.jsonName, sizeof(v.jsonName), "d%02u_name", (unsigned)i);
            snprintf(v.jsonActiveHigh, sizeof(v.jsonActiveHigh), "d%02u_active_high", (unsigned)i);
            snprintf(v.jsonInitialOn, sizeof(v.jsonInitialOn), "d%02u_initial_on", (unsigned)i);
            snprintf(v.jsonMomentary, sizeof(v.jsonMomentary), "d%02u_momentary", (unsigned)i);
            snprintf(v.jsonPulseMs, sizeof(v.jsonPulseMs), "d%02u_pulse_ms", (unsigned)i);
            snprintf(v.moduleName, sizeof(v.moduleName), "io/output/d%02u", (unsigned)i);
            v.name = {v.keyName, v.jsonName, v.moduleName, ConfigType::CharArray, (char*)doutCfg[i].name, ConfigPersistence::Persistent, sizeof(doutCfg[i].name)};
            v.binding = {v.keyBinding, "binding_port", v.moduleName, ConfigType::UInt16, &doutCfg[i].bindingPort, ConfigPersistence::Persistent, 0};
            v.activeHigh = {v.keyActiveHigh, v.jsonActiveHigh, v.moduleName, ConfigType::Bool, &doutCfg[i].activeHigh, ConfigPersistence::Persistent, 0};
            v.initialOn = {v.keyInitialOn, v.jsonInitialOn, v.moduleName, ConfigType::Bool, &doutCfg[i].initialOn, ConfigPersistence::Persistent, 0};
            v.retainWarm = {v.keyRetainWarm, "retain_on_warm_reboot", v.moduleName, ConfigType::Bool, &doutCfg[i].retainOnWarmReboot, ConfigPersistence::Persistent, 0};
            v.momentary = {v.keyMomentary, v.jsonMomentary, v.moduleName, ConfigType::Bool, &doutCfg[i].momentary, ConfigPersistence::Persistent, 0};
            v.pulseMs = {v.keyPulseMs, v.jsonPulseMs, v.moduleName, ConfigType::Int32, &doutCfg[i].pulseMs, ConfigPersistence::Persistent, 0};
        }
    }

    /**
     * Registers every slot variable (analog, then DI, then DO — this order is
     * the JSON export order). The branch resolvers keep the historical MQTT
     * config route ids per slot.
     */
    template <typename Store>
    void registerAll(Store& cfg,
                     uint8_t moduleId,
                     uint8_t (*analogBranch)(uint8_t),
                     uint8_t (*dinBranch)(uint8_t),
                     uint8_t (*doutBranch)(uint8_t))
    {
        for (uint8_t i = 0; i < AnalogSlots; ++i) {
            AnalogVars& v = analog[i];
            const uint8_t branch = analogBranch(i);
            cfg.registerVar(v.name, moduleId, branch);
            cfg.registerVar(v.binding, moduleId, branch);
            cfg.registerVar(v.c0, moduleId, branch);
            cfg.registerVar(v.c1, moduleId, branch);
            cfg.registerVar(v.prec, moduleId, branch);
        }
        for (uint8_t i = 0; i < DigitalInSlots; ++i) {
            DigitalInVars& v = din[i];
            const uint8_t branch = dinBranch(i);
            cfg.registerVar(v.name, moduleId, branch);
            cfg.registerVar(v.binding, moduleId, branch);
            cfg.registerVar(v.activeHigh, moduleId, branch);
            cfg.registerVar(v.pullMode, moduleId, branch);
            cfg.registerVar(v.mode, moduleId, branch);
            cfg.registerVar(v.edgeMode, moduleId, branch);
            cfg.registerVar(v.debounce, moduleId, branch);
            cfg.registerVar(v.c0, moduleId, branch);
            cfg.registerVar(v.prec, moduleId, branch);
            cfg.registerVar(v.counterTotal, moduleId, branch);
        }
        for (uint8_t i = 0; i < DigitalOutSlots; ++i) {
            DigitalOutVars& v = dout[i];
            const uint8_t branch = doutBranch(i);
            cfg.registerVar(v.name, moduleId, branch);
            cfg.registerVar(v.binding, moduleId, branch);
            cfg.registerVar(v.activeHigh, moduleId, branch);
            cfg.registerVar(v.initialOn, moduleId, branch);
            cfg.registerVar(v.retainWarm, moduleId, branch);
            cfg.registerVar(v.momentary, moduleId, branch);
            cfg.registerVar(v.pulseMs, moduleId, branch);
        }
    }
};
