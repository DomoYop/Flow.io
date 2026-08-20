#pragma once
/**
 * @file EventBusModule.h
 * @brief Active module hosting the EventBus task/dispatch loop.
 */
#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Core/EventBus/EventBus.h"

/**
 * @brief Active module that owns the EventBus instance.
 */
class EventBusModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::EventBus; }
    /** @brief Task name. */
    const char* taskName() const override { return "EventBus"; }
    /** @brief Pin control-path module on core 1. */
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    /** @brief EventBus depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }
    /** @brief Initialize and register EventBus service. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Emit the startup event once every module finished config loading. */
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Dispatch events from the queue. */
    void loop() override;

    /**
     * @brief Stack size override.
     *
     * Waveshare : 2560 -> 3584. Mesure du 2026-08-20 sur cible, 260 octets de
     * marge seulement (89,8 % consommes), sous le seuil d'alerte de sysmon. Le
     * vidage du 2026-08-18 19:51 accusait justement cette tache. Les autres
     * profils gardent 2560, faute de mesure sur leur materiel.
     * Voir docs/notes/audit-paniques-flash-cache.md.
     */
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 3584;
#else
        return 2560;
#endif
    }
    /** @brief Task priority override. */
    UBaseType_t taskPriority() const override { return 1; }

private:
    EventBus _bus;
    EventBusService _svc { &_bus };

    const LogHubService* logHub = nullptr;
};
