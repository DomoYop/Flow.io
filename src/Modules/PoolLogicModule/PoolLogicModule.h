#pragma once
/**
 * @file PoolLogicModule.h
 * @brief Pool business orchestration based on scheduler windows and sensor conditions.
 *
 * Public facade only. The implementation is split across Lifecycle / Scheduler /
 * Control / Runtime / Commands translation units.
 */

#include "Core/Module.h"
#include "Core/RuntimeUi.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolDefaults.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/PoolLogicModule/FiltrationWindow.h"

/** @brief Event ids owned by PoolLogicModule. */
constexpr uint16_t POOLLOGIC_EVENT_DAILY_RECALC = 0x2101;
constexpr uint16_t POOLLOGIC_EVENT_FILTRATION_WINDOW = 0x2102;

struct DomainSpec;

class PoolLogicModule : public Module, public IRuntimeSnapshotProvider, public IRuntimeUiValueProvider {
public:
    ModuleId moduleId() const override { return ModuleId::PoolLogic; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "poollogic"; }
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 7; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::Time;
        if (i == 3) return ModuleId::Io;
        if (i == 4) return ModuleId::PoolDevice;
        if (i == 5) return ModuleId::Command;
        if (i == 6) return ModuleId::Alarm;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    /**
     * @brief Injecte les défauts métier du profil (spec + IoIds capteurs du domaine).
     *
     * À appeler depuis le bootstrap de profil AVANT ModuleManager::initAll() :
     * les valeurs injectées deviennent les défauts que ConfigStore::loadPersistent()
     * conserve tant que la clé NVS correspondante n'existe pas.
     */
    void applyDomainDefaults(const DomainSpec& domain);
    void loop() override;
    uint16_t taskStackSize() const override { return 4096; }
    uint32_t startDelayMs() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 8000U;
#else
        return Limits::Boot::PoolLogicStartDelayMs;
#endif
    }
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;
    static MqttBuildResult buildCfgBaseStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);

private:
    enum RuntimeUiValueId : uint8_t {
        RuntimeUiAutoMode = 1,
        RuntimeUiWinterMode = 2,
        RuntimeUiPhAutoMode = 3,
        RuntimeUiOrpAutoMode = 4,
    };

    enum DisinfectionType : uint8_t {
        DisinfectionChlorineBromine = 0,
        DisinfectionSwg = 1,
        DisinfectionActiveOxygen = 2,
        DisinfectionDisabled = 3,
    };

    enum SwgControlMode : uint8_t {
        SwgControlOrp = 0,
        SwgControlContinuous = 1,
    };

    enum O2ProtocolState : uint8_t {
        O2ProtocolIdle = 0,
        O2ProtocolPending = 1,
        O2ProtocolDosing = 2,
        O2ProtocolBlocked = 3,
    };

    enum O2BlockReason : uint8_t {
        O2BlockNone = 0,
        O2BlockInactive = 1,
        O2BlockTimeUnsynced = 2,
        O2BlockPressure = 3,
        O2BlockTankLow = 4,
        O2BlockFlowInvalid = 5,
        O2BlockFiltrationWait = 6,
        O2BlockPumpService = 7,
        O2BlockPumpBlocked = 8,
        O2BlockConfig = 9,
    };

    struct DeviceFsm {
        bool known = false;
        bool on = false;
        bool lastDesired = false;
        uint32_t stateSinceMs = 0;
        uint32_t lastCmdMs = 0;
    };

    struct TemporalPidState {
        bool initialized = false;
        bool sampleValid = false;
        bool lastDemandOn = false;
        uint32_t windowStartMs = 0;
        uint32_t lastComputeMs = 0;
        uint32_t sampleTsMs = 0;
        uint32_t outputOnMs = 0;
        uint32_t runtimeTsMs = 0;
        float sampleInput = 0.0f;
        float sampleSetpoint = 0.0f;
        float sampleError = 0.0f;
        float integral = 0.0f;
        float prevError = 0.0f;
        float lastError = 0.0f;
    };

    enum class HeatAssistReason : uint8_t {
        Disabled = 0,
        ManualMode,
        PressureBlocked,
        SetpointInvalid,
        TempUnavailable,
        ProbeWait30m,
        ProbeWait20m,
        ProbeRunning,
        Heating,
        IdlePumpOn,
        SetpointReached,
    };

    static constexpr uint8_t SLOT_DAILY_RECALC = 3;
    static constexpr uint8_t SLOT_FILTR_WINDOW_BASE = 4;  // slots 4..6, un par segment planifie

    // Filet générique si applyDomainDefaults() n'est pas appelé ; les vrais défauts
    // sont injectés par le bootstrap depuis DomainSpec::domainIoSlotBindings.
    static constexpr IoId IO_ID_PH_DEFAULT = ioIdFromSlot(analogInputSlot(1));
    static constexpr IoId IO_ID_ORP_DEFAULT = ioIdFromSlot(analogInputSlot(0));
    static constexpr IoId IO_ID_PRESSURE_DEFAULT = ioIdFromSlot(analogInputSlot(2));
    static constexpr IoId IO_ID_WATER_TEMP_DEFAULT = ioIdFromSlot(analogInputSlot(4));
    static constexpr IoId IO_ID_AIR_TEMP_DEFAULT = ioIdFromSlot(analogInputSlot(5));
    static constexpr IoId IO_ID_LEVEL_DEFAULT = ioIdFromSlot(digitalInputSlot(0));
    static constexpr IoId IO_ID_PH_LEVEL_DEFAULT = ioIdFromSlot(digitalInputSlot(1));
    static constexpr IoId IO_ID_CHLORINE_LEVEL_DEFAULT = ioIdFromSlot(digitalInputSlot(2));

    // State and configuration storage
    bool enabled_ = false;

    // Modes
    bool autoMode_ = false;
    bool winterMode_ = false;
    bool phAutoMode_ = false;
    bool orpAutoMode_ = false;
    bool heaterAutoMode_ = false;
    bool phDosePlus_ = false;
    uint8_t disinfectionType_ = DisinfectionChlorineBromine;
    uint8_t swgControlMode_ = SwgControlContinuous;

    // Schedule / filtration plan (turnover volumique + fenetres priorisees)
    float pumpFlowM3h_ = PoolDefaults::PumpFlowM3h;
    bool filtrWinEnabled_[FILTRATION_PLAN_MAX_WINDOWS] = {true, false, false};
    uint16_t filtrWinStart_[FILTRATION_PLAN_MAX_WINDOWS] = {
        PoolDefaults::FiltrWin1StartMinute, PoolDefaults::FiltrWin2StartMinute, 0};
    uint16_t filtrWinStop_[FILTRATION_PLAN_MAX_WINDOWS] = {
        PoolDefaults::FiltrWin1StopMinute, PoolDefaults::FiltrWin2StopMinute, 0};
    uint8_t filtrWinPriority_[FILTRATION_PLAN_MAX_WINDOWS] = {1, 2, 3};
    uint8_t filtrationCalcStart_ = PoolDefaults::FiltrationStartMinHour;
    uint8_t filtrationCalcStop_ = PoolDefaults::FiltrationStopMaxHour;
    FiltrationPlanOutput filtrationPlan_{};  // dernier plan applique (garde par pendingMux_)

    // Sensor IO ids for IOServiceV2 reads.
    IoId phIoId_ = IO_ID_PH_DEFAULT;
    IoId orpIoId_ = IO_ID_ORP_DEFAULT;
    IoId pressureIoId_ = IO_ID_PRESSURE_DEFAULT;
    IoId waterTempIoId_ = IO_ID_WATER_TEMP_DEFAULT;
    IoId airTempIoId_ = IO_ID_AIR_TEMP_DEFAULT;
    IoId levelIoId_ = IO_ID_LEVEL_DEFAULT;
    IoId phLevelIoId_ = IO_ID_PH_LEVEL_DEFAULT;
    IoId chlorineLevelIoId_ = IO_ID_CHLORINE_LEVEL_DEFAULT;
    // Flowswitch/volet : entrees exposees en config (poollogic/sensors) avec
    // defaut domaine pose par applyDomainDefaults avant loadPersistent ; les
    // sorties indicatrices (recopie temporisee, etat volet) restent runtime,
    // leur port physique est reconfigurable via la page E/S (bindingPort).
    IoId flowSwitchIoId_ = IO_ID_INVALID;
    IoId coverClosedIoId_ = IO_ID_INVALID;
    IoId outFlowCopyIoId_ = IO_ID_INVALID;
    IoId outCoverIoId_ = IO_ID_INVALID;

    // Thresholds / delays
    float pressureLowThreshold_ = PoolDefaults::PressureLow;
    float pressureHighThreshold_ = PoolDefaults::PressureHigh;
    float winterStartTempC_ = PoolDefaults::WinterStartTempC;
    float freezeHoldTempC_ = PoolDefaults::FreezeHoldTempC;
    float secureElectroTempC_ = PoolDefaults::SecureElectroTempC;
    float phSetpoint_ = PoolDefaults::PhSetpoint;
    float orpSetpoint_ = PoolDefaults::OrpSetpoint;
    float heaterSetpoint_ = PoolDefaults::HeaterSetpoint;
    float phKp_ = PoolDefaults::PhKp;
    float phKi_ = PoolDefaults::PhKi;
    float phKd_ = PoolDefaults::PhKd;
    float orpKp_ = PoolDefaults::OrpKp;
    float orpKi_ = PoolDefaults::OrpKi;
    float orpKd_ = PoolDefaults::OrpKd;
    int32_t phWindowMs_ = PoolDefaults::PidWindowMs;
    int32_t orpWindowMs_ = PoolDefaults::PidWindowMs;
    int32_t pidMinOnMs_ = PoolDefaults::PidMinOnMs;
    int32_t pidSampleMs_ = PoolDefaults::PidSampleMs;
    uint8_t pressureStartupDelaySec_ = PoolDefaults::PressureStartupDelaySec;
    uint8_t delayPidsMin_ = PoolDefaults::DelayPidsMin;
    uint8_t delayElectroMin_ = PoolDefaults::DelayElectroMin;
    uint8_t robotDelayMin_ = PoolDefaults::RobotDelayMin;
    uint8_t robotDurationMin_ = PoolDefaults::RobotDurationMin;
    uint8_t fillingMinOnSec_ = PoolDefaults::FillingMinOnSec;
    // Delai d'activation (s) de la recopie flowswitch ; interlock securite debit.
    // Interlock desactive par defaut (opt-in) : sans flowswitch cable, DIN4 en
    // pull-up lit "pas de debit" et bloquerait le dosage. A activer une fois le
    // capteur installe (switch HA / config poollogic/safety flow_interlock).
    uint8_t flowCopyDelaySec_ = 30;
    bool flowInterlockEnabled_ = false;

    // Active oxygen phase-2 configuration and persisted protocol cursor.
    float o2PoolVolumeM3_ = PoolDefaults::O2PoolVolumeM3;
    float o2DoseMlPer10M3Week_ = PoolDefaults::O2DoseMlPer10M3Week;
    uint8_t o2MainHour_ = PoolDefaults::O2MainHour;
    uint8_t o2SplitCount_ = PoolDefaults::O2SplitCount;
    bool o2TempComp_ = PoolDefaults::O2TempComp;
    float o2LoadFactor_ = PoolDefaults::O2LoadFactor;
    uint8_t o2MinFilterRunMin_ = PoolDefaults::O2MinFilterRunMin;
    uint8_t o2ProtocolState_ = O2ProtocolIdle;
    uint16_t o2LastDoseDay_ = 0;
    float o2WeeklyDoneMl_ = 0.0f;
    float o2PendingMl_ = 0.0f;
    uint8_t o2BlockReason_ = O2BlockNone;
    float o2LastPlannedDoseMl_ = 0.0f;
    float o2LastFlowLh_ = 0.0f;
    uint32_t o2LastProgressMs_ = 0;
    uint32_t o2LastPersistMs_ = 0;
    mutable char o2PoolDeviceJsonBuf_[160] = {0};

    // Controlled pool devices
    uint8_t filtrationDeviceSlot_ = PoolIds::DeviceFiltrationPump;
    uint8_t swgDeviceSlot_ = PoolIds::DeviceChlorineGenerator;
    uint8_t robotDeviceSlot_ = PoolIds::DeviceRobot;
    uint8_t fillingDeviceSlot_ = PoolIds::DeviceFillPump;
    uint8_t phPumpDeviceSlot_ = PoolIds::DevicePhPump;
    uint8_t orpPumpDeviceSlot_ = PoolIds::DeviceChlorinePump;
    uint8_t heaterDeviceSlot_ = PoolIds::DeviceWaterHeater;

    // Runtime flags
    DeviceFsm filtrationFsm_{};
    DeviceFsm swgFsm_{};
    DeviceFsm robotFsm_{};
    DeviceFsm fillingFsm_{};
    DeviceFsm phPumpFsm_{};
    DeviceFsm orpPumpFsm_{};
    DeviceFsm heaterFsm_{};
    uint32_t heatAssistTimingPacked_ = 0;
    uint8_t heatAssistFlags_ = 0;
    HeatAssistReason heatAssistReason_ = HeatAssistReason::Disabled;
    TemporalPidState phPidState_{};
    TemporalPidState orpPidState_{};

    bool filtrationWindowActive_ = false;
    bool pendingDailyRecalc_ = false;
    bool pendingDayReset_ = false;
    bool pendingFiltrationReconcile_ = false;
    bool bootControlReady_ = false;
    bool startupActivityPending_ = false;
    uint32_t startupActivitySinceMs_ = 0;

    bool pressureError_ = false;
    bool phTankLowError_ = false;
    bool chlorineTankLowError_ = false;
    bool cleaningDone_ = false;
    bool robotManualOverride_ = false;
    bool robotManualDesired_ = false;
    bool phPidEnabled_ = false;
    bool orpPidEnabled_ = false;

    // Etat runtime flowswitch / sorties indicatrices.
    uint32_t flowSwitchOnSinceMs_ = 0;
    bool flowSwitchLast_ = false;
    bool flowCopyOutState_ = false;
    bool coverClosedState_ = false;
    bool noFlowError_ = false;

    portMUX_TYPE pendingMux_ = portMUX_INITIALIZER_UNLOCKED;

    ConfigVariable<bool,0> enabledVar_{NVS_KEY(NvsKeys::PoolLogic::Enabled), "enabled", "poollogic/modes", ConfigType::Bool,
                                       &enabled_, ConfigPersistence::Persistent, 0};

    ConfigVariable<bool,0> autoModeVar_{NVS_KEY(NvsKeys::PoolLogic::AutoMode), "auto_mode", "poollogic/modes", ConfigType::Bool,
                                        &autoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> winterModeVar_{NVS_KEY(NvsKeys::PoolLogic::WinterMode), "winter_mode", "poollogic/modes", ConfigType::Bool,
                                          &winterMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::PhAutoMode), "ph_auto_mode", "poollogic/ph", ConfigType::Bool,
                                          &phAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> orpAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::OrpAutoMode), "dis_auto_mode", "poollogic/chlorine", ConfigType::Bool,
                                           &orpAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> heaterAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::HeaterAutoMode), "heater_auto_mode", "poollogic/heater", ConfigType::Bool,
                                              &heaterAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phDosePlusVar_{NVS_KEY(NvsKeys::PoolLogic::PhDosePlus), "ph_dose_plus", "poollogic/ph", ConfigType::Bool,
                                          &phDosePlus_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> disinfectionTypeVar_{NVS_KEY(NvsKeys::PoolLogic::DisinfectionType), "disinfection_type", "poollogic/modes", ConfigType::UInt8,
                                                   &disinfectionType_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> swgControlModeVar_{NVS_KEY(NvsKeys::PoolLogic::SwgControlMode), "swg_control_mode", "poollogic/swg", ConfigType::UInt8,
                                                 &swgControlMode_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> pumpFlowVar_{NVS_KEY(NvsKeys::PoolLogic::PumpFlowM3h), "pump_flow_m3h", "poollogic/filtration", ConfigType::Float,
                                         &pumpFlowM3h_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> filtrWin1EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Enabled), "filtr_w1_en", "poollogic/filtration", ConfigType::Bool,
                                           &filtrWinEnabled_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin1StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Start), "filtr_w1_start", "poollogic/filtration", ConfigType::UInt16,
                                                  &filtrWinStart_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin1StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Stop), "filtr_w1_stop", "poollogic/filtration", ConfigType::UInt16,
                                                 &filtrWinStop_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin1PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Priority), "filtr_w1_prio", "poollogic/filtration", ConfigType::UInt8,
                                                &filtrWinPriority_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> filtrWin2EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Enabled), "filtr_w2_en", "poollogic/filtration", ConfigType::Bool,
                                           &filtrWinEnabled_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin2StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Start), "filtr_w2_start", "poollogic/filtration", ConfigType::UInt16,
                                                  &filtrWinStart_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin2StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Stop), "filtr_w2_stop", "poollogic/filtration", ConfigType::UInt16,
                                                 &filtrWinStop_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin2PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Priority), "filtr_w2_prio", "poollogic/filtration", ConfigType::UInt8,
                                                &filtrWinPriority_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> filtrWin3EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Enabled), "filtr_w3_en", "poollogic/filtration", ConfigType::Bool,
                                           &filtrWinEnabled_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin3StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Start), "filtr_w3_start", "poollogic/filtration", ConfigType::UInt16,
                                                  &filtrWinStart_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin3StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Stop), "filtr_w3_stop", "poollogic/filtration", ConfigType::UInt16,
                                                 &filtrWinStop_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin3PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Priority), "filtr_w3_prio", "poollogic/filtration", ConfigType::UInt8,
                                                &filtrWinPriority_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStart), "filtr_start_clc", "poollogic/filtration", ConfigType::UInt8,
                                            &filtrationCalcStart_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStop), "filtr_stop_clc", "poollogic/filtration", ConfigType::UInt8,
                                           &filtrationCalcStop_, ConfigPersistence::Persistent, 0};

    ConfigVariable<IoId,0> phIdVar_{NVS_KEY(NvsKeys::PoolLogic::PhIoId), "ph_io_id", "poollogic/sensors", ConfigType::UInt16,
                                       &phIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> orpIdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpIoId), "dis_io_id", "poollogic/sensors", ConfigType::UInt16,
                                        &orpIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> pressureIdVar_{NVS_KEY(NvsKeys::PoolLogic::PressureIoId), "pressure_io_id", "poollogic/sensors", ConfigType::UInt16,
                                        &pressureIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> waterTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::WaterTempIoId), "wat_temp_io_id", "poollogic/sensors", ConfigType::UInt16,
                                              &waterTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> airTempIdVar_{NVS_KEY(NvsKeys::PoolLogic::AirTempIoId), "air_temp_io_id", "poollogic/sensors", ConfigType::UInt16,
                                            &airTempIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> levelIdVar_{NVS_KEY(NvsKeys::PoolLogic::LevelIoId), "pool_lvl_io_id", "poollogic/sensors", ConfigType::UInt16,
                                          &levelIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> phLevelIdVar_{NVS_KEY(NvsKeys::PoolLogic::PhLevelIoId), "ph_lvl_io_id", "poollogic/sensors", ConfigType::UInt16,
                                            &phLevelIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> chlorineLevelIdVar_{NVS_KEY(NvsKeys::PoolLogic::ChlorineLevelIoId), "chl_lvl_io_id", "poollogic/sensors", ConfigType::UInt16,
                                                  &chlorineLevelIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> flowSwitchIdVar_{NVS_KEY(NvsKeys::PoolLogic::FlowSwitchIoId), "flow_io_id", "poollogic/sensors", ConfigType::UInt16,
                                               &flowSwitchIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> coverClosedIdVar_{NVS_KEY(NvsKeys::PoolLogic::CoverClosedIoId), "cover_io_id", "poollogic/sensors", ConfigType::UInt16,
                                                &coverClosedIoId_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> pressureLowVar_{NVS_KEY(NvsKeys::PoolLogic::PressureLow), "pressure_low_th", "poollogic/safety", ConfigType::Float,
                                       &pressureLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> pressureHighVar_{NVS_KEY(NvsKeys::PoolLogic::PressureHigh), "pressure_high_th", "poollogic/safety", ConfigType::Float,
                                        &pressureHighThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> winterStartVar_{NVS_KEY(NvsKeys::PoolLogic::WinterStart), "winter_start_t", "poollogic/safety", ConfigType::Float,
                                            &winterStartTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> freezeHoldVar_{NVS_KEY(NvsKeys::PoolLogic::FreezeHold), "freeze_hold_t", "poollogic/safety", ConfigType::Float,
                                           &freezeHoldTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> secureElectroVar_{NVS_KEY(NvsKeys::PoolLogic::SecureElectro), "secure_elec_t", "poollogic/swg", ConfigType::Float,
                                              &secureElectroTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::PhSetpoint), "ph_setpoint", "poollogic/ph", ConfigType::Float,
                                           &phSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::OrpSetpoint), "dis_setpoint", "poollogic/chlorine", ConfigType::Float,
                                            &orpSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> heaterSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::HeaterSetpoint), "heater_setpoint", "poollogic/heater", ConfigType::Float,
                                               &heaterSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKpVar_{NVS_KEY(NvsKeys::PoolLogic::PhKp), "ph_kp", "poollogic/ph", ConfigType::Float,
                                     &phKp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKiVar_{NVS_KEY(NvsKeys::PoolLogic::PhKi), "ph_ki", "poollogic/ph", ConfigType::Float,
                                     &phKi_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phKdVar_{NVS_KEY(NvsKeys::PoolLogic::PhKd), "ph_kd", "poollogic/ph", ConfigType::Float,
                                     &phKd_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKpVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKp), "dis_kp", "poollogic/chlorine", ConfigType::Float,
                                      &orpKp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKiVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKi), "dis_ki", "poollogic/chlorine", ConfigType::Float,
                                      &orpKi_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKdVar_{NVS_KEY(NvsKeys::PoolLogic::OrpKd), "dis_kd", "poollogic/chlorine", ConfigType::Float,
                                      &orpKd_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> phWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::PhWindowMs), "ph_window_ms", "poollogic/ph", ConfigType::Int32,
                                             &phWindowMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> orpWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::OrpWindowMs), "dis_window_ms", "poollogic/chlorine", ConfigType::Int32,
                                              &orpWindowMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> pidMinOnMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidMinOnMs), "pid_min_on_ms", "poollogic/regulation", ConfigType::Int32,
                                             &pidMinOnMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> pidSampleMsVar_{NVS_KEY(NvsKeys::PoolLogic::PidSampleMs), "pid_sample_ms", "poollogic/regulation", ConfigType::Int32,
                                              &pidSampleMs_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> pressureDelayVar_{NVS_KEY(NvsKeys::PoolLogic::PressureDelay), "pressure_start_dly_s", "poollogic/safety", ConfigType::UInt8,
                                           &pressureStartupDelaySec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayPidsVar_{NVS_KEY(NvsKeys::PoolLogic::DelayPids), "dly_pid_min", "poollogic/regulation", ConfigType::UInt8,
                                            &delayPidsMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayElectroVar_{NVS_KEY(NvsKeys::PoolLogic::DelayElectro), "dly_electro_min", "poollogic/swg", ConfigType::UInt8,
                                               &delayElectroMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDelayVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDelay), "robot_delay_min", "poollogic/robot", ConfigType::UInt8,
                                             &robotDelayMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDurationVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDuration), "robot_dur_min", "poollogic/robot", ConfigType::UInt8,
                                                &robotDurationMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingMinOnVar_{NVS_KEY(NvsKeys::PoolLogic::FillingMinOn), "fill_min_on_s", "poollogic/refill", ConfigType::UInt8,
                                               &fillingMinOnSec_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> o2PoolVolumeVar_{NVS_KEY(NvsKeys::PoolLogic::O2PoolVolumeM3), "pool_volume_m3", "poollogic/o2", ConfigType::Float,
                                             &o2PoolVolumeM3_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2DoseVar_{NVS_KEY(NvsKeys::PoolLogic::O2DoseMlPer10M3Week), "dose_ml_10m3_week", "poollogic/o2", ConfigType::Float,
                                       &o2DoseMlPer10M3Week_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2MainHourVar_{NVS_KEY(NvsKeys::PoolLogic::O2MainHour), "main_hour", "poollogic/o2", ConfigType::UInt8,
                                             &o2MainHour_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2SplitCountVar_{NVS_KEY(NvsKeys::PoolLogic::O2SplitCount), "split_count", "poollogic/o2", ConfigType::UInt8,
                                               &o2SplitCount_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> o2TempCompVar_{NVS_KEY(NvsKeys::PoolLogic::O2TempComp), "temp_comp", "poollogic/o2", ConfigType::Bool,
                                          &o2TempComp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2LoadFactorVar_{NVS_KEY(NvsKeys::PoolLogic::O2LoadFactor), "load_factor", "poollogic/o2", ConfigType::Float,
                                             &o2LoadFactor_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2MinFilterRunVar_{NVS_KEY(NvsKeys::PoolLogic::O2MinFilterRunMin), "min_filter_run_min", "poollogic/o2", ConfigType::UInt8,
                                                 &o2MinFilterRunMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2ProtocolStateVar_{NVS_KEY(NvsKeys::PoolLogic::O2ProtocolState), "protocol_state", "poollogic/o2", ConfigType::UInt8,
                                                  &o2ProtocolState_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> o2LastDoseDayVar_{NVS_KEY(NvsKeys::PoolLogic::O2LastDoseDay), "last_dose_day", "poollogic/o2", ConfigType::UInt16,
                                                 &o2LastDoseDay_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2WeeklyDoneVar_{NVS_KEY(NvsKeys::PoolLogic::O2WeeklyDoneMl), "weekly_done_ml", "poollogic/o2", ConfigType::Float,
                                             &o2WeeklyDoneMl_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2PendingVar_{NVS_KEY(NvsKeys::PoolLogic::O2PendingMl), "pending_ml", "poollogic/o2", ConfigType::Float,
                                          &o2PendingMl_, ConfigPersistence::Persistent, 0};

    // Aiguillage role -> slot PoolDevice : chaque variable vit dans la branche
    // metier correspondante (les cles NVS pl_s* restent inchangees).
    ConfigVariable<uint8_t,0> filtrationDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationSlot), "filtr_slot", "poollogic/filtration", ConfigType::UInt8,
                                                   &filtrationDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> swgDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::SwgSlot), "swg_slot", "poollogic/swg", ConfigType::UInt8,
                                            &swgDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::RobotSlot), "robot_slot", "poollogic/robot", ConfigType::UInt8,
                                              &robotDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::FillingSlot), "fill_slot", "poollogic/refill", ConfigType::UInt8,
                                                &fillingDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> phPumpDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::PhPumpSlot), "ph_pump_slot", "poollogic/ph", ConfigType::UInt8,
                                               &phPumpDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> orpPumpDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::OrpPumpSlot), "dis_pump_slot", "poollogic/chlorine", ConfigType::UInt8,
                                                &orpPumpDeviceSlot_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> heaterDeviceVar_{NVS_KEY(NvsKeys::PoolLogic::HeaterSlot), "heater_slot", "poollogic/heater", ConfigType::UInt8,
                                               &heaterDeviceSlot_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> flowCopyDelayVar_{NVS_KEY(NvsKeys::PoolLogic::FlowCopyDelay), "flow_copy_delay_s", "poollogic/safety", ConfigType::UInt8,
                                                &flowCopyDelaySec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> flowInterlockVar_{NVS_KEY(NvsKeys::PoolLogic::FlowInterlock), "flow_interlock", "poollogic/safety", ConfigType::Bool,
                                             &flowInterlockEnabled_, ConfigPersistence::Persistent, 0};

    // Services and adapters
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const TimeSchedulerService* schedSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const PoolDeviceService* poolSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const ActivityLogService* activityLogSvc_ = nullptr;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    // Lifecycle
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void normalizeDeviceSlots_();
    void logDeviceSlotConfig_() const;
    void logDeviceSlotBinding_(const char* role, uint8_t slot, int8_t expectedType) const;
    bool activityTimeReady_() const;
    void emitStartupActivityIfReady_(uint32_t nowMs);

    // Scheduler
    void ensureDailySlot_();
    bool applyFiltrationPlanSlots_(const FiltrationPlanOutput& plan);
    bool currentFiltrationPlanActive_(const FiltrationPlanOutput& plan, bool& activeOut) const;
    bool computeFiltrationPlan_(float waterTemp, FiltrationPlanOutput& out) const;
    bool recalcAndApplyFiltrationWindow_(uint8_t* startHourOut = nullptr,
                                         uint8_t* stopHourOut = nullptr,
                                         uint8_t* durationOut = nullptr);

    // Control
    static AlarmCondState condPressureLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPressureHighStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPhTankLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condChlorineTankLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condWaterLevelLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPhPumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condChlorinePumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    AlarmCondState condPumpMaxUptime_(uint8_t deviceSlot) const;
    bool readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const;
    bool writeDeviceDesired_(uint8_t deviceSlot, bool on);
    bool setPoolDeviceWritesEnabled_(bool enabled);
    void syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut);
    void syncAllDeviceStates_(uint32_t nowMs);
    void adoptBootDeviceState_(uint32_t nowMs);
    uint32_t stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const;
    bool loadAnalogSensor_(IoId ioId, float& out, uint32_t* tsMsOut = nullptr) const;
    bool loadDigitalSensor_(IoId ioId, bool& out) const;
    void resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs);
    bool stepTemporalPid_(TemporalPidState& st,
                          float input,
                          float setpoint,
                          float kp,
                          float ki,
                          float kd,
                          int32_t windowMsCfg,
                          bool positiveWhenInputHigh,
                          uint32_t nowMs,
                          bool& demandOnOut,
                          uint32_t& outputOnMsOut);
    void applyDeviceControl_(uint8_t deviceSlot, const char* label, DeviceFsm& fsm, bool desired, uint32_t nowMs);
    void runControlLoop_(uint32_t nowMs);
    ActivityRole activityRoleForDeviceSlot_(uint8_t deviceSlot) const;
    const char* activityRoleLabel_(ActivityRole role) const;
    void emitActivity_(ActivityCode code,
                       ActivitySource source,
                       ActivitySeverity severity,
                       ActivityRole role,
                       ActivityState state,
                       ActivityReason reason,
                       uint8_t deviceSlot,
                       const char* title,
                       const char* detail,
                       const char* icon) const;
    void emitDeviceActivity_(bool requested,
                             bool on,
                             uint8_t deviceSlot,
                             const char* label,
                             ActivityReason reason) const;
    void emitAutoModeDisabledByManualActivity_(ActivityRole role, uint8_t deviceSlot, const char* autoLabel) const;
    bool isDisinfectionType_(DisinfectionType type) const;
    bool readPoolDeviceFlowLh_(uint8_t deviceSlot, float& flowLhOut) const;
    bool currentO2LocalTime_(uint16_t& dayKeyOut,
                             uint16_t& weekKeyOut,
                             uint8_t& weekDayMon0Out,
                             uint8_t& hourOut) const;
    bool isO2DoseDay_(uint8_t weekDayMon0) const;
    float o2TemperatureFactor_(bool haveWaterTemp, float waterTemp) const;
    float computeO2WeeklyDoseMl_(bool haveWaterTemp, float waterTemp) const;
    void setO2ProtocolState_(uint8_t state, uint8_t blockReason, uint32_t nowMs);
    void persistO2Protocol_(uint32_t nowMs, bool force);
    bool stepO2Protocol_(bool filtrationDesired,
                         bool filtrationOn,
                         uint32_t filtrationRunMin,
                         bool haveWaterTemp,
                         float waterTemp,
                         bool pressureError,
                         bool tankLow,
                         uint32_t nowMs,
                         bool& requestFiltrationOut,
                         bool& pumpDesiredOut);
    static const char* disinfectionTypeStr_(uint8_t type);
    static const char* swgControlModeStr_(uint8_t mode);
    static const char* o2ProtocolStateStr_(uint8_t state);
    static const char* o2BlockReasonStr_(uint8_t reason);

    // Runtime
    MqttBuildResult buildCfgBase_(MqttBuildContext& buildCtx);

    // Commands
    static bool cmdFiltrationWriteStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFiltrationRecalcStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdAutoModeSetStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdMqttControlStatic_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdFiltrationWrite_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdFiltrationRecalc_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdAutoModeSet_(const CommandRequest& req, char* reply, size_t replyLen);
    bool cmdMqttControl_(const CommandRequest& req, char* reply, size_t replyLen);
};
