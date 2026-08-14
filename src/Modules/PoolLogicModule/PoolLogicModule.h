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
#include "Modules/PoolLogicModule/DosingController.h"
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

    uint8_t dependencyCount() const override { return 8; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::EventBus;
        if (i == 2) return ModuleId::Time;
        if (i == 3) return ModuleId::Io;
        if (i == 4) return ModuleId::PoolDevice;
        if (i == 5) return ModuleId::Command;
        if (i == 6) return ModuleId::Alarm;
        // Publication de l'etat de dosage pH (PoolLogicRuntime).
        if (i == 7) return ModuleId::DataStore;
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
    /**
     * @brief Fige le mode de traitement lu dans les Preferences au démarrage.
     *
     * À appeler depuis le bootstrap de profil AVANT ModuleManager::initAll() :
     * c'est cette valeur, et non celle de la config courante, qui décide quelles
     * variables de configuration, quelles alarmes et quelles entités Home
     * Assistant sont déclarées — et que suivent les régulations.
     *
     * Le réglage reste modifiable à chaud (il alimente la liste déroulante),
     * mais le firmware continue de fonctionner sur le mode figé jusqu'au
     * redémarrage suivant. Le mode en service n'est pas republié : il se
     * constate, en regardant lequel des trois équipements de désinfection
     * existe réellement. C'est ce que fait l'interface pour signaler
     * « redémarrage requis ».
     */
    void setBootDisinfectionType(uint8_t type);
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
        RuntimeUiHeaterAutoMode = 5,
        // Temperatures metier : le slot IO lu suit wat_temp_io_id /
        // air_temp_io_id, pas une position figee dans la couche IO.
        RuntimeUiWaterTemp = 6,
        RuntimeUiAirTemp = 7,
        // Dosage pH volumetrique par lots.
        RuntimeUiPhDosePhase = 8,
        RuntimeUiPhDoseDayMl = 9,
        RuntimeUiPhGain = 10,
        // Suivi du lot en cours : ce que la FSM a decide, ce qu'elle a deja
        // injecte, ce qu'elle attend comme effet et pourquoi elle patiente.
        RuntimeUiPhBlockReason = 11,
        RuntimeUiPhMixRemainMin = 12,
        RuntimeUiPhBatchTargetMl = 13,
        RuntimeUiPhBatchDeliveredMl = 14,
        RuntimeUiPhExpectedDelta = 15,
        // Mesures figees faute de circulation : dit pourquoi pH et Redox sont
        // plats, sans quoi une sonde morte y ressemblerait trait pour trait.
        RuntimeUiSensorHold = 16,
    };

    // Le mode de traitement appartient au domaine (PoolIds::Disinfection) : il
    // est lu a froid par le bootstrap de profil, avant que ce module existe.
    // Les alias evitent de qualifier les ~40 usages internes.
    using DisinfectionType = PoolIds::Disinfection;
    static constexpr DisinfectionType DisinfectionDisabled = PoolIds::DisinfectionDisabled;
    static constexpr DisinfectionType DisinfectionChlorineBromine = PoolIds::DisinfectionChlorineBromine;
    static constexpr DisinfectionType DisinfectionSwg = PoolIds::DisinfectionSwg;
    static constexpr DisinfectionType DisinfectionActiveOxygen = PoolIds::DisinfectionActiveOxygen;

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
        // Suivi du refus d'ecriture : la consigne n'est latchee que lorsqu'elle
        // est acceptee, et le log de blocage est deduplique par raison.
        bool writeRejected = false;
        uint8_t lastBlockReason = 0;
        uint8_t loggedBlockReason = 0xFFU;  // 0xFF = aucune raison tracee
        uint32_t blockLoggedMs = 0;
    };

    // Resultat d'une commande d'equipement : distingue le refus d'un simple
    // "rien a faire", pour que la logique metier puisse suspendre son action.
    enum class DeviceWriteResult : uint8_t {
        NoDevice = 0,   // slot >= POOL_DEVICE_MAX : role sans appareil associe
        Unchanged = 1,  // aucune ecriture necessaire ce tour
        Written = 2,    // ecriture acceptee par PoolDeviceService
        Rejected = 3,   // ecriture refusee, voir fsm.lastBlockReason
    };

    struct TemporalPidState {
        bool initialized = false;
        bool sampleValid = false;
        bool lastDemandOn = false;
        bool windowLatched = false;  // outputOnMs fige pour la fenetre en cours
        uint32_t windowStartMs = 0;
        uint32_t lastComputeMs = 0;
        uint32_t sampleTsMs = 0;
        uint32_t outputOnMs = 0;
        uint32_t pendingOnMs = 0;  // consigne calculee, appliquee au debut de fenetre
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

    /**
     * Plafond du delai de reprise des mesures.
     *
     * La sonde de temperature du chauffage fait tourner la filtration 5 min et
     * decide a la fin : au-dela de cette borne, elle deciderait sur la valeur
     * figee de la veille. Meme raisonnement pour l'armement des regulations
     * (dly_pid_min, 5 min par defaut).
     */
    static constexpr uint16_t kSensorHoldSettleMaxSec = 240U;

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
    // disinfectionType_ est le miroir de la config : il suit la liste deroulante
    // et peut changer a chaud. bootDisinfectionType_ est le mode reellement en
    // service, fige par le bootstrap avant que les equipements, les variables et
    // les entites soient declares. Toute la logique consulte le second, via
    // isDisinfectionType_ ; le premier ne sert qu'a detecter l'ecart et a
    // demander un redemarrage.
    uint8_t disinfectionType_ = DisinfectionDisabled;
    uint8_t bootDisinfectionType_ = DisinfectionDisabled;
    uint8_t swgControlMode_ = SwgControlContinuous;

    // Schedule / filtration plan (turnover volumique + fenetres priorisees)
    float pumpFlowM3h_ = PoolDefaults::PumpFlowM3h;
    // Ratio utilisateur sur les cycles de renouvellement (%, 100 = courbe).
    uint8_t filtrCycleRatioPct_ = PoolDefaults::FiltrationCycleRatioPct;
    bool filtrWinEnabled_[FILTRATION_PLAN_MAX_WINDOWS] = {true, false, false};
    uint16_t filtrWinStart_[FILTRATION_PLAN_MAX_WINDOWS] = {
        PoolDefaults::FiltrWin1StartMinute, PoolDefaults::FiltrWin2StartMinute, 0};
    uint16_t filtrWinStop_[FILTRATION_PLAN_MAX_WINDOWS] = {
        PoolDefaults::FiltrWin1StopMinute, PoolDefaults::FiltrWin2StopMinute, 0};
    uint8_t filtrWinPriority_[FILTRATION_PLAN_MAX_WINDOWS] = {1, 2, 3};
    uint8_t filtrationCalcStart_ = PoolDefaults::FiltrationStartMinHour;
    uint8_t filtrationCalcStop_ = PoolDefaults::FiltrationStopMaxHour;
    // Segments planifies "HH:MM-HH:MM, ..." (sortie pure du plan, jamais en NVS).
    char filtrationCalcSegments_[64] = {0};
    // Duree optimale calculee (besoin journalier avant plafond capacite), minutes.
    uint16_t filtrationOptimalMin_ = 0;
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

    // Thresholds / delays
    float pressureLowThreshold_ = PoolDefaults::PressureLow;
    float pressureHighThreshold_ = PoolDefaults::PressureHigh;
    float winterStartTempC_ = PoolDefaults::WinterStartTempC;
    float freezeHoldTempC_ = PoolDefaults::FreezeHoldTempC;
    float secureElectroTempC_ = PoolDefaults::SecureElectroTempC;
    float phSetpoint_ = PoolDefaults::PhSetpoint;
    float orpSetpoint_ = PoolDefaults::OrpSetpoint;
    float heaterSetpoint_ = PoolDefaults::HeaterSetpoint;
    float orpKp_ = PoolDefaults::OrpKp;
    float orpKi_ = PoolDefaults::OrpKi;
    float orpKd_ = PoolDefaults::OrpKd;
    int32_t orpWindowMs_ = PoolDefaults::PidWindowMs;
    // Duree ON minimale et periode d'echantillonnage de la boucle ORP, qui reste
    // un PID temporel (le pH est passe au dosage volumetrique par lots).
    int32_t disMinOnMs_ = PoolDefaults::PidMinOnMs;
    int32_t disSampleMs_ = PoolDefaults::PidSampleMs;

    // Dosage pH volumetrique par lots. Le gain, la bande morte et les plafonds
    // sont exprimes dans les unites que l'utilisateur comprend (mL, pH), et
    // relies au volume du bassin et au debit reel de la pompe.
    float phDoseMlPerM3_ = PoolDefaults::PhDoseMlPerM3;
    float phDeadband_ = PoolDefaults::PhDeadband;
    float phDoseFactor_ = PoolDefaults::PhDoseFactor;
    float phDoseMaxBatchMl_ = PoolDefaults::PhDoseMaxBatchMl;
    float phDoseMaxDayMl_ = PoolDefaults::PhDoseMaxDayMl;
    float phValidMin_ = PoolDefaults::PhValidMin;
    float phValidMax_ = PoolDefaults::PhValidMax;
    float phNoEffectDelta_ = PoolDefaults::PhNoEffectDelta;
    float phGainLearned_ = 0.0f;
    uint16_t phMixWaitMin_ = PoolDefaults::PhMixWaitMin;
    uint16_t phSampleMaxAgeS_ = PoolDefaults::PhSampleMaxAgeSec;
    uint8_t phNoEffectBatches_ = PoolDefaults::PhNoEffectBatches;
    uint8_t phGainSamples_ = 0;
    int32_t phLastDoseTs_ = 0;  // epoch de fin du dernier lot, masque dans l'UI
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
    // Le capteur est-il reellement installe ? Une entree TOR libre est en
    // pull-up et lit « pas de debit » a vide : sans cette declaration, son etat
    // ne peut pas servir a decider quoi que ce soit. Activer l'interlock vaut
    // declaration -- on ne l'active pas sans capteur.
    bool flowPresent_ = false;

    // Gel des mesures en ligne hors circulation. Une sonde montee sur la
    // tuyauterie ne voit plus que l'eau immobile du porte-sondes des que la
    // pompe s'arrete : la mesure derive sans rien dire du bassin. Les
    // regulations sont deja protegees (armement dly_pid_min, interlock debit) ;
    // ce qui derive, c'est ce qui est publie -- Home Assistant, ecran, web.
    bool sensorHoldEnabled_ = PoolDefaults::SensorHold;
    uint16_t sensorHoldSettleSec_ = PoolDefaults::SensorHoldSettleSec;
    // La sonde de temperature d'eau peut etre en ligne (elle derive) ou
    // immergee dans le bassin (elle reste juste) : c'est un fait de montage,
    // pas une preference, d'ou le reglage separe.
    bool sensorHoldWaterTemp_ = PoolDefaults::SensorHoldWaterTemp;

    // Volume du bassin : transverse (filtration + doses O2), voir page Bassin.
    float poolVolumeM3_ = PoolDefaults::PoolVolumeM3;

    // Active oxygen phase-2 configuration and persisted protocol cursor.
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

    // Appareils pilotes. Une fonction piscine = un PoolDevice = une sortie
    // logique de meme index : le lien n'est plus un reglage, il est porte par
    // PoolIds::Device*. Ce que l'utilisateur choisit, c'est le relais physique
    // (io/output/dNN/binding_port), pas l'appareil.
    static constexpr uint8_t filtrationDeviceSlot_ = PoolIds::DeviceFiltrationPump;
    static constexpr uint8_t swgDeviceSlot_ = PoolIds::DeviceChlorineGenerator;
    static constexpr uint8_t robotDeviceSlot_ = PoolIds::DeviceRobot;
    static constexpr uint8_t fillingDeviceSlot_ = PoolIds::DeviceFillPump;
    static constexpr uint8_t phPumpDeviceSlot_ = PoolIds::DevicePhPump;
    static constexpr uint8_t heaterDeviceSlot_ = PoolIds::DeviceWaterHeater;
    // Seule exception : la pompe de desinfection depend du mode choisi
    // (chlore/brome ou oxygene actif), qui sont deux appareils distincts.
    uint8_t orpPumpDeviceSlot_ = PoolIds::DeviceChlorinePump;

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
    TemporalPidState orpPidState_{};

    // Dosage pH par lots : etat de la FSM (RAM seule, cf. reprise apres reboot
    // par phLastDoseTs_) et dernier resultat, publie en runtime.
    DosingState phDosingState_{};
    DosingOutput phDosingLast_{};
    uint32_t phDosingTsMs_ = 0;   // horodatage du dernier changement observable
    uint32_t phMetricsReadMs_ = 0;  // cadence de relecture des metriques PoolDevice
    float phPumpFlowLh_ = 0.0f;
    float phDosedTodayMl_ = 0.0f;
    float phTankRemainMl_ = 0.0f;
    bool phDosingResumeChecked_ = false;  // reprise post-boot deja tentee
    bool dosingConflictLogged_ = false;   // deduplication du log acide/chlore

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

    // Gel des mesures : dernier etat pousse a IOModule, et IoId marques (pour
    // les liberer si l'utilisateur rebinde un role sur un autre slot).
    static constexpr uint8_t kSensorHoldMax = 3;  // pH, ORP, temperature d'eau
    IoId sensorHoldIds_[kSensorHoldMax] = {IO_ID_INVALID, IO_ID_INVALID, IO_ID_INVALID};
    // Le registre IO n'est pas forcement construit quand la config est chargee :
    // un marquage refuse est rejoue au tick suivant plutot que perdu.
    bool sensorHoldPending_ = true;
    bool circulatingLast_ = false;
    bool circulatingKnown_ = false;

    portMUX_TYPE pendingMux_ = portMUX_INITIALIZER_UNLOCKED;

    ConfigVariable<bool,0> enabledVar_{NVS_KEY(NvsKeys::PoolLogic::Enabled), "enabled", "poollogic/bassin", ConfigType::Bool,
                                       &enabled_, ConfigPersistence::Persistent, 0};

    ConfigVariable<bool,0> autoModeVar_{NVS_KEY(NvsKeys::PoolLogic::AutoMode), "auto_mode", "poollogic/bassin", ConfigType::Bool,
                                        &autoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> winterModeVar_{NVS_KEY(NvsKeys::PoolLogic::WinterMode), "winter_mode", "poollogic/bassin", ConfigType::Bool,
                                          &winterMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::PhAutoMode), "ph_auto_mode", "poollogic/ph", ConfigType::Bool,
                                          &phAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> orpAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::DisAutoMode), "dis_auto_mode", "poollogic/disinfection", ConfigType::Bool,
                                           &orpAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> heaterAutoModeVar_{NVS_KEY(NvsKeys::PoolLogic::HeaterAutoMode), "heater_auto_mode", "poollogic/heater", ConfigType::Bool,
                                              &heaterAutoMode_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> phDosePlusVar_{NVS_KEY(NvsKeys::PoolLogic::PhDosePlus), "ph_dose_plus", "poollogic/ph", ConfigType::Bool,
                                          &phDosePlus_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> disinfectionTypeVar_{NVS_KEY(NvsKeys::PoolLogic::DisinfectionType), "disinfection_type", "poollogic/bassin", ConfigType::UInt8,
                                                   &disinfectionType_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> swgControlModeVar_{NVS_KEY(NvsKeys::PoolLogic::SwgControlMode), "swg_control_mode", "poollogic/disinfection", ConfigType::UInt8,
                                                 &swgControlMode_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> pumpFlowVar_{NVS_KEY(NvsKeys::PoolLogic::PumpFlowM3h), "pump_flow_m3h", "poollogic/filtration", ConfigType::Float,
                                         &pumpFlowM3h_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrCycleRatioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrCycleRatio), "filtr_cycle_ratio", "poollogic/filtration", ConfigType::UInt8,
                                                  &filtrCycleRatioPct_, ConfigPersistence::Persistent, 0};
    // Les 3 fenetres vivent dans la sous-branche "fenetres" pour alleger le
    // menu Filtration ; les cles NVS restent inchangees (pas de migration).
    ConfigVariable<bool,0> filtrWin1EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Enabled), "filtr_w1_en", "poollogic/filtration/fenetres", ConfigType::Bool,
                                           &filtrWinEnabled_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin1StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Start), "filtr_w1_start", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                  &filtrWinStart_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin1StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Stop), "filtr_w1_stop", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                 &filtrWinStop_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin1PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin1Priority), "filtr_w1_prio", "poollogic/filtration/fenetres", ConfigType::UInt8,
                                                &filtrWinPriority_[0], ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> filtrWin2EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Enabled), "filtr_w2_en", "poollogic/filtration/fenetres", ConfigType::Bool,
                                           &filtrWinEnabled_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin2StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Start), "filtr_w2_start", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                  &filtrWinStart_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin2StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Stop), "filtr_w2_stop", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                 &filtrWinStop_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin2PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin2Priority), "filtr_w2_prio", "poollogic/filtration/fenetres", ConfigType::UInt8,
                                                &filtrWinPriority_[1], ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> filtrWin3EnVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Enabled), "filtr_w3_en", "poollogic/filtration/fenetres", ConfigType::Bool,
                                           &filtrWinEnabled_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin3StartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Start), "filtr_w3_start", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                  &filtrWinStart_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> filtrWin3StopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Stop), "filtr_w3_stop", "poollogic/filtration/fenetres", ConfigType::UInt16,
                                                 &filtrWinStop_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> filtrWin3PrioVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrWin3Priority), "filtr_w3_prio", "poollogic/filtration/fenetres", ConfigType::UInt8,
                                                &filtrWinPriority_[2], ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStartVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStart), "filtr_start_clc", "poollogic/filtration", ConfigType::UInt8,
                                            &filtrationCalcStart_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> calcStopVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrationCalcStop), "filtr_stop_clc", "poollogic/filtration", ConfigType::UInt8,
                                           &filtrationCalcStop_, ConfigPersistence::Persistent, 0};
    ConfigVariable<char,0> filtrSegmentsVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrSegments), "filtr_segments", "poollogic/filtration", ConfigType::CharArray,
                                             filtrationCalcSegments_, ConfigPersistence::Runtime, sizeof(filtrationCalcSegments_)};
    ConfigVariable<uint16_t,0> filtrOptimalVar_{NVS_KEY(NvsKeys::PoolLogic::FiltrOptimalMin), "filtr_optimal_min", "poollogic/filtration", ConfigType::UInt16,
                                                &filtrationOptimalMin_, ConfigPersistence::Runtime, 0};

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
    ConfigVariable<IoId,0> chlorineLevelIdVar_{NVS_KEY(NvsKeys::PoolLogic::DisLevelIoId), "dis_lvl_io_id", "poollogic/sensors", ConfigType::UInt16,
                                                  &chlorineLevelIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> flowSwitchIdVar_{NVS_KEY(NvsKeys::PoolLogic::FlowSwitchIoId), "flow_io_id", "poollogic/sensors", ConfigType::UInt16,
                                               &flowSwitchIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<IoId,0> coverClosedIdVar_{NVS_KEY(NvsKeys::PoolLogic::CoverClosedIoId), "cover_io_id", "poollogic/sensors", ConfigType::UInt16,
                                                &coverClosedIoId_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> flowPresentVar_{NVS_KEY(NvsKeys::PoolLogic::FlowSwitchPresent), "flow_present", "poollogic/sensors", ConfigType::Bool,
                                           &flowPresent_, ConfigPersistence::Persistent, 0};

    ConfigVariable<float,0> pressureLowVar_{NVS_KEY(NvsKeys::PoolLogic::PressureLow), "pressure_low_th", "poollogic/safety", ConfigType::Float,
                                       &pressureLowThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> pressureHighVar_{NVS_KEY(NvsKeys::PoolLogic::PressureHigh), "pressure_high_th", "poollogic/safety", ConfigType::Float,
                                        &pressureHighThreshold_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> winterStartVar_{NVS_KEY(NvsKeys::PoolLogic::WinterStart), "winter_start_t", "poollogic/safety", ConfigType::Float,
                                            &winterStartTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> freezeHoldVar_{NVS_KEY(NvsKeys::PoolLogic::FreezeHold), "freeze_hold_t", "poollogic/safety", ConfigType::Float,
                                           &freezeHoldTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> secureElectroVar_{NVS_KEY(NvsKeys::PoolLogic::SecureElectro), "secure_elec_t", "poollogic/disinfection", ConfigType::Float,
                                              &secureElectroTempC_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::PhSetpoint), "ph_setpoint", "poollogic/ph", ConfigType::Float,
                                           &phSetpoint_, ConfigPersistence::Persistent, 0};
    // Dosage pH volumetrique par lots.
    ConfigVariable<float,0> phDoseMlPerM3Var_{NVS_KEY(NvsKeys::PoolLogic::PhDoseMlPerM3), "ph_dose_ml_m3", "poollogic/ph", ConfigType::Float,
                                              &phDoseMlPerM3_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phDeadbandVar_{NVS_KEY(NvsKeys::PoolLogic::PhDeadband), "ph_deadband", "poollogic/ph", ConfigType::Float,
                                           &phDeadband_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phDoseFactorVar_{NVS_KEY(NvsKeys::PoolLogic::PhDoseFactor), "ph_dose_factor", "poollogic/ph", ConfigType::Float,
                                             &phDoseFactor_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phDoseMaxBatchVar_{NVS_KEY(NvsKeys::PoolLogic::PhDoseMaxBatchMl), "ph_dose_max_batch", "poollogic/ph", ConfigType::Float,
                                               &phDoseMaxBatchMl_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phDoseMaxDayVar_{NVS_KEY(NvsKeys::PoolLogic::PhDoseMaxDayMl), "ph_dose_max_day", "poollogic/ph", ConfigType::Float,
                                             &phDoseMaxDayMl_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phValidMinVar_{NVS_KEY(NvsKeys::PoolLogic::PhValidMin), "ph_valid_min", "poollogic/ph", ConfigType::Float,
                                           &phValidMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phValidMaxVar_{NVS_KEY(NvsKeys::PoolLogic::PhValidMax), "ph_valid_max", "poollogic/ph", ConfigType::Float,
                                           &phValidMax_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phNoEffectDeltaVar_{NVS_KEY(NvsKeys::PoolLogic::PhNoEffectDelta), "ph_no_effect_dph", "poollogic/ph", ConfigType::Float,
                                                &phNoEffectDelta_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> phGainLearnedVar_{NVS_KEY(NvsKeys::PoolLogic::PhGainLearned), "ph_gain_learned", "poollogic/ph", ConfigType::Float,
                                              &phGainLearned_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> phMixWaitMinVar_{NVS_KEY(NvsKeys::PoolLogic::PhMixWaitMin), "ph_mix_wait_min", "poollogic/ph", ConfigType::UInt16,
                                                &phMixWaitMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> phSampleMaxAgeVar_{NVS_KEY(NvsKeys::PoolLogic::PhSampleMaxAge), "ph_sample_max_age", "poollogic/ph", ConfigType::UInt16,
                                                  &phSampleMaxAgeS_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> phNoEffectLotsVar_{NVS_KEY(NvsKeys::PoolLogic::PhNoEffectLots), "ph_no_effect_lots", "poollogic/ph", ConfigType::UInt8,
                                                 &phNoEffectBatches_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> phGainSamplesVar_{NVS_KEY(NvsKeys::PoolLogic::PhGainSamples), "ph_gain_samples", "poollogic/ph", ConfigType::UInt8,
                                                &phGainSamples_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> phLastDoseTsVar_{NVS_KEY(NvsKeys::PoolLogic::PhLastDoseTs), "ph_last_dose_ts", "poollogic/ph", ConfigType::Int32,
                                               &phLastDoseTs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::DisSetpoint), "dis_setpoint", "poollogic/disinfection", ConfigType::Float,
                                            &orpSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> heaterSetpointVar_{NVS_KEY(NvsKeys::PoolLogic::HeaterSetpoint), "heater_setpoint", "poollogic/heater", ConfigType::Float,
                                               &heaterSetpoint_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKpVar_{NVS_KEY(NvsKeys::PoolLogic::DisKp), "dis_kp", "poollogic/disinfection", ConfigType::Float,
                                      &orpKp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKiVar_{NVS_KEY(NvsKeys::PoolLogic::DisKi), "dis_ki", "poollogic/disinfection", ConfigType::Float,
                                      &orpKi_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> orpKdVar_{NVS_KEY(NvsKeys::PoolLogic::DisKd), "dis_kd", "poollogic/disinfection", ConfigType::Float,
                                      &orpKd_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> orpWindowMsVar_{NVS_KEY(NvsKeys::PoolLogic::DisWindowMs), "dis_window_ms", "poollogic/disinfection", ConfigType::Int32,
                                              &orpWindowMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> disMinOnMsVar_{NVS_KEY(NvsKeys::PoolLogic::DisMinOnMs), "dis_min_on_ms", "poollogic/disinfection", ConfigType::Int32,
                                             &disMinOnMs_, ConfigPersistence::Persistent, 0};
    ConfigVariable<int32_t,0> disSampleMsVar_{NVS_KEY(NvsKeys::PoolLogic::DisSampleMs), "dis_sample_ms", "poollogic/disinfection", ConfigType::Int32,
                                              &disSampleMs_, ConfigPersistence::Persistent, 0};

    ConfigVariable<uint8_t,0> pressureDelayVar_{NVS_KEY(NvsKeys::PoolLogic::PressureDelay), "pressure_start_dly_s", "poollogic/safety", ConfigType::UInt8,
                                           &pressureStartupDelaySec_, ConfigPersistence::Persistent, 0};
    // Delai d'armement des regulations apres demarrage filtration : temps
    // d'homogeneisation de l'eau et de stabilisation des sondes, commun pH/ORP.
    ConfigVariable<uint8_t,0> delayPidsVar_{NVS_KEY(NvsKeys::PoolLogic::DelayPids), "dly_pid_min", "poollogic/bassin", ConfigType::UInt8,
                                            &delayPidsMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> delayElectroVar_{NVS_KEY(NvsKeys::PoolLogic::DelayElectro), "dly_electro_min", "poollogic/disinfection", ConfigType::UInt8,
                                               &delayElectroMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDelayVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDelay), "robot_delay_min", "poollogic/robot", ConfigType::UInt8,
                                             &robotDelayMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> robotDurationVar_{NVS_KEY(NvsKeys::PoolLogic::RobotDuration), "robot_dur_min", "poollogic/robot", ConfigType::UInt8,
                                                &robotDurationMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> fillingMinOnVar_{NVS_KEY(NvsKeys::PoolLogic::FillingMinOn), "fill_min_on_s", "poollogic/refill", ConfigType::UInt8,
                                               &fillingMinOnSec_, ConfigPersistence::Persistent, 0};

    // Volume du bassin : caracteristique de l'installation, utilisee par le
    // besoin de filtration (renouvellement volumique) autant que par les doses
    // O2 -> parametre transverse, page Bassin.
    ConfigVariable<float,0> poolVolumeVar_{NVS_KEY(NvsKeys::PoolLogic::PoolVolumeM3), "pool_volume_m3", "poollogic/bassin", ConfigType::Float,
                                           &poolVolumeM3_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2DoseVar_{NVS_KEY(NvsKeys::PoolLogic::O2DoseMlPer10M3Week), "dose_ml_10m3_week", "poollogic/disinfection", ConfigType::Float,
                                       &o2DoseMlPer10M3Week_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2MainHourVar_{NVS_KEY(NvsKeys::PoolLogic::O2MainHour), "main_hour", "poollogic/disinfection", ConfigType::UInt8,
                                             &o2MainHour_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2SplitCountVar_{NVS_KEY(NvsKeys::PoolLogic::O2SplitCount), "split_count", "poollogic/disinfection", ConfigType::UInt8,
                                               &o2SplitCount_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> o2TempCompVar_{NVS_KEY(NvsKeys::PoolLogic::O2TempComp), "temp_comp", "poollogic/disinfection", ConfigType::Bool,
                                          &o2TempComp_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2LoadFactorVar_{NVS_KEY(NvsKeys::PoolLogic::O2LoadFactor), "load_factor", "poollogic/disinfection", ConfigType::Float,
                                             &o2LoadFactor_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2MinFilterRunVar_{NVS_KEY(NvsKeys::PoolLogic::O2MinFilterRunMin), "min_filter_run_min", "poollogic/disinfection", ConfigType::UInt8,
                                                 &o2MinFilterRunMin_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint8_t,0> o2ProtocolStateVar_{NVS_KEY(NvsKeys::PoolLogic::O2ProtocolState), "protocol_state", "poollogic/disinfection", ConfigType::UInt8,
                                                  &o2ProtocolState_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> o2LastDoseDayVar_{NVS_KEY(NvsKeys::PoolLogic::O2LastDoseDay), "last_dose_day", "poollogic/disinfection", ConfigType::UInt16,
                                                 &o2LastDoseDay_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2WeeklyDoneVar_{NVS_KEY(NvsKeys::PoolLogic::O2WeeklyDoneMl), "weekly_done_ml", "poollogic/disinfection", ConfigType::Float,
                                             &o2WeeklyDoneMl_, ConfigPersistence::Persistent, 0};
    ConfigVariable<float,0> o2PendingVar_{NVS_KEY(NvsKeys::PoolLogic::O2PendingMl), "pending_ml", "poollogic/disinfection", ConfigType::Float,
                                          &o2PendingMl_, ConfigPersistence::Persistent, 0};

    // Aiguillage role -> slot PoolDevice : chaque variable vit dans la branche
    // metier correspondante (les cles NVS pl_s* restent inchangees).

    ConfigVariable<uint8_t,0> flowCopyDelayVar_{NVS_KEY(NvsKeys::PoolLogic::FlowCopyDelay), "flow_copy_delay_s", "poollogic/safety", ConfigType::UInt8,
                                                &flowCopyDelaySec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> flowInterlockVar_{NVS_KEY(NvsKeys::PoolLogic::FlowInterlock), "flow_interlock", "poollogic/safety", ConfigType::Bool,
                                             &flowInterlockEnabled_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> sensorHoldVar_{NVS_KEY(NvsKeys::PoolLogic::SensorHold), "sensor_hold", "poollogic/safety", ConfigType::Bool,
                                          &sensorHoldEnabled_, ConfigPersistence::Persistent, 0};
    ConfigVariable<uint16_t,0> sensorHoldSettleVar_{NVS_KEY(NvsKeys::PoolLogic::SensorHoldSettle), "sensor_hold_settle_s", "poollogic/safety", ConfigType::UInt16,
                                                    &sensorHoldSettleSec_, ConfigPersistence::Persistent, 0};
    ConfigVariable<bool,0> sensorHoldWatVar_{NVS_KEY(NvsKeys::PoolLogic::SensorHoldWaterTemp), "sensor_hold_wat", "poollogic/safety", ConfigType::Bool,
                                             &sensorHoldWaterTemp_, ConfigPersistence::Persistent, 0};

    // Services and adapters
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    const TimeService* timeSvc_ = nullptr;
    const TimeSchedulerService* schedSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    const PoolDeviceService* poolSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const ActivityLogService* activityLogSvc_ = nullptr;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    // Lifecycle
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void resolveDisinfectionDeviceSlot_();
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
    static AlarmCondState condPhDoseNoEffectStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condWaterLevelLowStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condWaterTempUnavailableStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condPhPumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    static AlarmCondState condChlorinePumpMaxUptimeStatic_(void* ctx, uint32_t nowMs);
    AlarmCondState condPumpMaxUptime_(uint8_t deviceSlot) const;
    bool readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const;
    bool writeDeviceDesired_(uint8_t deviceSlot,
                             bool on,
                             PoolDeviceSvcStatus& statusOut,
                             uint8_t& blockReasonOut);
    bool forceDeviceStop_(uint8_t deviceSlot);
    bool setPoolDeviceWritesEnabled_(bool enabled);
    void syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut);
    void syncAllDeviceStates_(uint32_t nowMs);
    void adoptBootDeviceState_(uint32_t nowMs);
    uint32_t stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const;
    bool loadAnalogSensor_(IoId ioId, float& out, uint32_t* tsMsOut = nullptr) const;
    bool loadDigitalSensor_(IoId ioId, bool& out) const;
    /**
     * @brief Declare a IOModule les sondes a geler hors circulation.
     *
     * Le marquage suit le role metier : rebinder ph_io_id sur un autre slot
     * deplace le gel avec lui, d'ou la liberation des IoId precedents avant de
     * remarquer. A rejouer apres tout changement de `poollogic/sensors` ou des
     * reglages de gel.
     */
    void applySensorHoldBindings_();
    /**
     * @brief Publie l'etat hydraulique vers IOModule (front seulement).
     *
     * Pompe alimentee != eau qui circule : si un flowswitch est **declare
     * installe** (`flow_interlock`), c'est lui qui fait foi -- vanne fermee,
     * amorcage perdu. Sans cette declaration l'entree TOR est en pull-up et
     * lit « pas de debit » a vide : la suivre gelerait tout en permanence.
     */
    void updateSensorHold_(bool haveFlow, bool flowOn);
    /** @brief Vrai quand les mesures publiees sont figees (etat affichable). */
    bool sensorHoldActive_() const;
    void resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs);
    void stepTemporalPid_(TemporalPidState& st,
                          float input,
                          float setpoint,
                          float kp,
                          float ki,
                          float kd,
                          int32_t windowMsCfg,
                          int32_t minOnMsCfg,
                          int32_t sampleMsCfg,
                          bool positiveWhenInputHigh,
                          uint32_t nowMs,
                          bool& demandOnOut,
                          uint32_t& outputOnMsOut);
    // Dosage pH volumetrique par lots : adaptateurs minces autour du helper pur
    // DosingController (aucune logique de regulation ici).
    void refreshPhPumpMetrics_(uint32_t nowMs);
    void resumePhDosingAfterBoot_(uint32_t nowMs);
    void fillPhDosingInput_(DosingInput& in,
                            bool havePh,
                            float ph,
                            uint32_t phAgeMs,
                            uint32_t nowMs) const;
    void stepPhDosing_(bool havePh, float ph, uint32_t phAgeMs, uint32_t nowMs, bool& phPumpDesired);
    /** @brief Gain retenu pour le dosage : appris s'il existe, configure sinon. */
    float phEffectiveGainMlPerM3_() const;
    /** @brief Publie l'etat du dosage pH dans le DataStore (no-op sans DataStore). */
    void publishPhDosingRuntime_() const;
    /**
     * @brief Variation de pH attendue du lot en cours, signee selon le produit.
     *
     * Inverse de computeBatchDoseMl : le lot decide vaut gain x volume x
     * (ecart / unitStep) x facteur, plafonne lot/jour/bidon. Repartir du volume
     * reellement retenu -- et non de l'ecart mesure -- fait apparaitre l'effet
     * d'un plafonnement. false = pas de lot en cours ou config inexploitable.
     */
    bool phExpectedBatchDelta_(float& deltaOut) const;
    void persistPhDosingResult_(const DosingOutput& out, uint32_t nowMs);
    void resetPhLearnedGain_(const char* reason);
    void resetPhDosingState_(uint32_t nowMs);
    DeviceWriteResult applyDeviceControl_(uint8_t deviceSlot,
                                          const char* label,
                                          DeviceFsm& fsm,
                                          bool desired,
                                          uint32_t nowMs);
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
