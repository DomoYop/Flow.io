#pragma once
/**
 * @file Module.h
 * @brief Base interface for all runtime modules.
 */
#include "ConfigStore.h"
#include "Core/ModuleId.h"
#include "Core/SystemLimits.h"
#include "Runtime.h"
#include "ServiceRegistry.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Base class for active modules backed by a FreeRTOS task.
 */
struct ModuleTaskSpec {
    const char* name;
    uint32_t stackSize;
    UBaseType_t priority;
    BaseType_t coreId;
    TaskFunction_t entry;
    void* context;
};

class Module {
public:
    /** @brief Virtual destructor. */
    virtual ~Module() = default;

    /** @brief Unique module identifier (used for dependency wiring). */
    virtual ModuleId moduleId() const = 0;
    /** @brief FreeRTOS task name for this module. */
    virtual const char* taskName() const = 0;

    /** @brief Number of declared dependencies. */
    virtual uint8_t dependencyCount() const { return 0; }
    /** @brief Dependency id at index, or ModuleId::Unknown if none. */
    virtual ModuleId dependency(uint8_t) const { return ModuleId::Unknown; }

    /** @brief Initialize module and register services/config. */
    virtual void init(ConfigStore& cfg, ServiceRegistry& services) = 0;
    /** @brief Called once all persistent config values are loaded. */
    virtual void onConfigLoaded(ConfigStore&, ServiceRegistry&) {}
    /** @brief Return whether the startup sequencer may release the module now. */
    virtual bool canStart(ConfigStore&, ServiceRegistry&) { return true; }
    /** @brief Called when the startup sequencer releases the module. */
    virtual void onStart(ConfigStore&, ServiceRegistry&) {}
    /** @brief Main module loop called from the module task. */
    virtual void loop() = 0;

    /** @brief Number of declared FreeRTOS tasks owned by this module. */
    virtual uint8_t taskCount() const { return 0; }
    /** @brief Array of declared FreeRTOS tasks owned by this module. */
    virtual const ModuleTaskSpec* taskSpecs() const { return nullptr; }

    /** @brief Stack size for the FreeRTOS task. */
    virtual uint16_t taskStackSize() const { return Limits::Core::Task::DefaultStackSize; }
    /** @brief Task priority for the FreeRTOS task. */
    virtual UBaseType_t taskPriority() const { return 1; }
    /**
     * @brief Memory capability flags used for the task stack allocation.
     *
     * Rendre MALLOC_CAP_SPIRAM place la pile *et* le TCB en memoire externe
     * (`xTaskCreatePinnedToCoreWithCaps`). Cinq modules le font sur le profil
     * Waveshare, et ce n'est pas un oubli : la DRAM interne de cette carte est la
     * ressource rare -- 226 Ko en tout, dont il ne restait que 7,4 Ko libres et
     * 2,5 Ko de plus gros bloc quand ces cinq piles ont ete rapatriees en interne
     * le 2026-08-19. Le serveur d'assets s'est mis a repondre 503 Busy
     * (`shouldRejectAssetByFreeHeap_`, seuil 10 240 o) et l'interface web est
     * tombee. Mesure du 2026-08-20 : plancher historique de DRAM interne libre a
     * **272 octets**.
     *
     * Le risque theorique existe -- une pile en PSRAM est inatteignable pendant
     * qu'une ecriture flash coupe le cache -- mais il a ete explicitement cherche
     * et **non trouve** : les paniques d'aout 2026 etaient des debordements de
     * pile (`exc_cause = 65`, exception de debogage levee par le point d'arret de
     * fin de pile), pas des erreurs de cache (qui auraient donne 71).
     *
     * Donc : ne pas rapatrier ces piles en interne sans avoir d'abord verifie la
     * marge avec `GET /api/system/heap`, et sans raison mesuree de le faire.
     * Voir docs/notes/audit-paniques-flash-cache.md.
     */
    virtual UBaseType_t taskStackCaps() const { return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT; }
    /** @brief CPU core affinity for the FreeRTOS task (`0` or `1` on ESP32). */
    virtual BaseType_t taskCore() const { return 1; }
    /** @brief Relative startup delay in ms applied by `ModuleManager` before `onStart`. */
    virtual uint32_t startDelayMs() const { return 0U; }

    /** @brief Get the first started FreeRTOS task handle for this module. */
    TaskHandle_t getTaskHandle() const { return primaryTaskHandle_; }

    /** @brief Whether this module owns at least one declared task. */
    virtual bool hasTask() const { return taskCount() > 0; }

protected:
    /** @brief Helper for classic "loop() in one task" modules. */
    const ModuleTaskSpec* singleLoopTaskSpec() const {
        singleLoopTaskSpecCache_ = {
            taskName(),
            (uint32_t)taskStackSize(),
            taskPriority(),
            taskCore(),
            &Module::taskEntry,
            const_cast<Module*>(this)
        };
        return &singleLoopTaskSpecCache_;
    }

private:
    friend class ModuleManager;

    static void taskEntry(void* arg) {
        Module* self = static_cast<Module*>(arg);
        if (!self) {
            vTaskDelete(nullptr);
            return;
        }
        while (true) {
            self->loop();
            vTaskDelay(pdMS_TO_TICKS(Limits::Core::Timing::LoopDelayMs));
        }
    }

    void resetPrimaryTaskHandle_() { primaryTaskHandle_ = nullptr; }
    void setPrimaryTaskHandle_(TaskHandle_t handle) {
        if (!primaryTaskHandle_) primaryTaskHandle_ = handle;
    }

    TaskHandle_t primaryTaskHandle_ = nullptr;
    mutable ModuleTaskSpec singleLoopTaskSpecCache_{};
};
