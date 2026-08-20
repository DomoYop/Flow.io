#pragma once
/**
 * @file AlarmModule.h
 * @brief Central alarm registry/evaluation engine.
 */

#include "Core/Module.h"
#include "Core/RuntimeUi.h"
#include "Core/ServiceBinding.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#include "Core/Services/Services.h"

struct CommandRequest;

class AlarmModule : public Module, public IRuntimeUiValueProvider {
public:
    ModuleId moduleId() const override { return ModuleId::Alarm; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "alarms"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2560; }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::Command;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void loop() override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

private:
    // Une valeur Runtime UI par alarme, indexee par AlarmId et non par position
    // de slot. Les identifiants 1..3 des anciens masques ne sont pas reutilises :
    // un manifeste en cache leur associerait encore un type uint32.
    struct RuntimeUiAlarmEntry {
        uint8_t valueId;
        AlarmId alarmId;
        const char* key;
    };
    static const RuntimeUiAlarmEntry kRuntimeUiAlarms[9];

    struct AlarmSlot {
        bool used = false;
        AlarmId id = AlarmId::None;
        AlarmRegistration def{};
        AlarmCondFn condFn = nullptr;
        void* condCtx = nullptr;

        bool active = false;
        // Acquittement de l'occurrence active courante : remis a faux a chaque
        // nouveau declenchement et a chaque effacement, jamais persiste seul.
        bool acknowledged = false;
        // Alarme restauree depuis la NVS : active sans avoir ete evaluee depuis
        // le boot. Sert a distinguer « defaut vu avant coupure » de « defaut vu
        // maintenant » dans les logs.
        bool restored = false;
        AlarmCondState lastCond = AlarmCondState::Unknown;
        uint32_t onSinceMs = 0;
        uint32_t offSinceMs = 0;
        uint32_t activeSinceMs = 0;
        uint32_t lastChangeMs = 0;
        uint32_t lastNotifyMs = 0;
        uint32_t ackAtMs = 0;
        // Horodatage absolu du declenchement. 0 quand l'horloge n'etait pas encore
        // synchronisee : un millis() ne survit ni au rollover ni au redemarrage.
        uint32_t activeSinceEpoch = 0;
    };

    /** Nature d'une transition consignee au journal. */
    enum class LogEvent : uint8_t {
        Raised = 1,
        Cleared = 2,
        Reset = 3,
        Acked = 4,
        Restored = 5,
    };

    struct LogEntry {
        uint32_t epochSec;  //!< 0 si l'horloge n'etait pas synchronisee.
        uint32_t upMs;
        uint16_t id;
        uint8_t event;
        uint8_t lifecycle;
    };

    /** Etat persiste d'une alarme latchee encore active a la coupure. */
    struct PersistedLatch {
        uint16_t id;
        uint8_t flags;  //!< bit0 = acquittee.
        uint8_t reserved;
    };

    struct PersistedLatchBlob {
        uint16_t magic;
        uint8_t version;
        uint8_t count;
        PersistedLatch items[Limits::Alarm::MaxAlarms];
    };

    static constexpr uint16_t kLatchBlobMagic = 0xA1A2U;
    static constexpr uint8_t kLatchBlobVersion = 1U;
    static constexpr uint8_t kLatchFlagAcknowledged = 0x01U;
    static constexpr uint8_t kLogCapacity = 24U;

    static bool cmdList_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdLog_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdReset_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdResetAll_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAck_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAckAll_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool registerAlarmSvc_(const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx);
    bool registerAlarm_(const AlarmRegistration& def, AlarmCondFn condFn, void* condCtx);
    bool reset_(AlarmId id);
    uint8_t resetAll_();
    bool ack_(AlarmId id);
    uint8_t ackAll_();
    bool isActive_(AlarmId id) const;
    bool isResettable_(AlarmId id) const;
    bool isAcknowledged_(AlarmId id) const;
    AlarmLifecycle lifecycle_(AlarmId id) const;
    const char* codeOf_(AlarmId id) const;
    uint8_t activeCount_() const;
    uint8_t unackedCount_() const;
    AlarmSeverity highestSeverity_() const;
    AlarmSeverity highestUnackedSeverity_() const;
    AlarmSeverity highestSeverityFiltered_(bool unackedOnly) const;
    bool buildSnapshot_(char* out, size_t len) const;
    bool buildLog_(char* out, size_t len) const;
    void appendLog_(AlarmId id, LogEvent event);
    uint32_t nowEpoch_();
    void persistLatches_();
    void restoreLatches_();
    uint8_t listIds_(AlarmId* out, uint8_t max) const;
    bool buildAlarmState_(AlarmId id, char* out, size_t len) const;
    bool handleCmdReset_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handleCmdAck_(const CommandRequest& req, char* reply, size_t replyLen);
    void evaluateOnce_(uint32_t nowMs);

    int16_t findSlotById_(AlarmId id) const;
    int16_t findFreeSlot_() const;
    void emitAlarmEvent_(EventId id, AlarmId alarmId) const;
    void noteAlarmNotified_(AlarmId id, uint32_t nowMs);
    uint8_t takeDueAlarmReminderIds_(AlarmId* out, uint8_t max, uint32_t nowMs);
    static bool delayReached_(uint32_t sinceMs, uint32_t delayMs, uint32_t nowMs);
    static const char* condStateStr_(AlarmCondState s);
    void registerHaEntities_(ServiceRegistry& services);

    AlarmService alarmSvc_{
        ServiceBinding::bind<&AlarmModule::registerAlarmSvc_>,
        ServiceBinding::bind<&AlarmModule::reset_>,
        ServiceBinding::bind<&AlarmModule::resetAll_>,
        ServiceBinding::bind<&AlarmModule::ack_>,
        ServiceBinding::bind<&AlarmModule::ackAll_>,
        ServiceBinding::bind<&AlarmModule::isActive_>,
        ServiceBinding::bind<&AlarmModule::isResettable_>,
        ServiceBinding::bind<&AlarmModule::isAcknowledged_>,
        ServiceBinding::bind_or<&AlarmModule::lifecycle_, AlarmLifecycle::Unavailable>,
        ServiceBinding::bind_or<&AlarmModule::codeOf_, (const char*)nullptr>,
        ServiceBinding::bind<&AlarmModule::activeCount_>,
        ServiceBinding::bind<&AlarmModule::unackedCount_>,
        ServiceBinding::bind_or<&AlarmModule::highestSeverity_, AlarmSeverity::Info>,
        ServiceBinding::bind_or<&AlarmModule::highestUnackedSeverity_, AlarmSeverity::Info>,
        ServiceBinding::bind<&AlarmModule::buildSnapshot_>,
        ServiceBinding::bind<&AlarmModule::listIds_>,
        ServiceBinding::bind<&AlarmModule::buildAlarmState_>,
        this
    };

    const LogHubService* logHub_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const HAService* haSvc_ = nullptr;
    // Resolu paresseusement : TimeModule est enregistre apres AlarmModule et une
    // dependance dure reordonnerait tout le profil pour un simple horodatage.
    const TimeService* timeSvc_ = nullptr;
    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    bool haEntitiesRegistered_ = false;

    LogEntry log_[kLogCapacity]{};
    uint8_t logCount_ = 0U;
    uint8_t logHead_ = 0U;

    bool enabled_ = true;
    int32_t evalPeriodMsCfg_ = (int32_t)Limits::Alarm::DefaultEvalPeriodMs;

    ConfigVariable<bool,0> enabledVar_{
        NVS_KEY(NvsKeys::Alarm::Enabled), "enabled", "alarms", ConfigType::Bool,
        &enabled_, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> evalPeriodVar_{
        NVS_KEY(NvsKeys::Alarm::EvalPeriodMs), "eval_period_ms", "alarms", ConfigType::Int32,
        &evalPeriodMsCfg_, ConfigPersistence::Persistent, 0
    };

    mutable portMUX_TYPE slotsMux_ = portMUX_INITIALIZER_UNLOCKED;
    AlarmSlot slots_[Limits::Alarm::MaxAlarms]{};
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
};
