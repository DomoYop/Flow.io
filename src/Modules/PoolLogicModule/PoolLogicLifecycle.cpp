/**
 * @file PoolLogicLifecycle.cpp
 * @brief Module lifecycle wiring for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/MqttTopics.h"
#include "Domain/DomainSpec.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cmath>
#include <cstring>
#include <new>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
// PoolLogic exposes one aggregated cfg/poollogic route plus the per-branch
// routes used by HA, MQTT config sync, and tooling.
static constexpr uint8_t kPoolLogicCfgProducerId = 44;
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
// Branche 1 = Bassin (ex-modes) : parametres transverses au bassin.
static constexpr uint8_t kCfgBranchBassin = 1;
static constexpr uint8_t kCfgBranchFiltration = 2;
static constexpr uint8_t kCfgBranchSensors = 3;
static constexpr uint8_t kCfgBranchSafety = 4;
// Branche 5 (ex-regulation) liberee : min_on/sample sont passes par boucle
// (pH / desinfection) et le delai d'armement a rejoint Bassin.
// Branche 7 = Desinfection : fusion des ex-branches chlorine (7), swg (8) et
// o2 (9), un seul slot logique de desinfection.
static constexpr uint8_t kCfgBranchPh = 6;
static constexpr uint8_t kCfgBranchDisinfection = 7;
// kCfgBranch 10 (ex-devices) libere : les slots role->PDM vivent desormais
// dans leur branche metier (filtration/desinfection/robot/refill/ph/heater).
static constexpr uint8_t kCfgBranchHeater = 11;
static constexpr uint8_t kCfgBranchRobot = 12;
static constexpr uint8_t kCfgBranchRefill = 13;
// Branche 14 = sous-branche Fenetres de Filtration : regroupe les 3 fenetres
// (actif/debut/fin/priorite) pour desengorger le menu Filtration.
static constexpr uint8_t kCfgBranchFiltrationWindows = 14;
static constexpr uint32_t kStartupActivityStabilizeMs = 3000U;
static constexpr uint32_t kStartupActivityMaxDelayMs = 30000U;
static constexpr uint64_t kActivityMinEpoch = 1609459200ULL;
static constexpr const char* kCfgModuleBassin = "poollogic/bassin";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleFiltrationWindows = "poollogic/filtration/fenetres";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
// Slot PoolDevice de la pompe de filtration : accueille ses caracteristiques
// materielles (debit) et celles de son circuit (seuils de pression).
static constexpr const char* kCfgModulePd0 = "pdm/pd0";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleDisinfection = "poollogic/disinfection";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";

enum : uint16_t {
    kCfgMsgBase = 1,
    kCfgMsgBassin = 2,
    kCfgMsgFiltration = 3,
    kCfgMsgSensors = 4,
    kCfgMsgSafety = 5,
    kCfgMsgPh = 7,
    kCfgMsgDisinfection = 8,
    kCfgMsgHeater = 12,
    kCfgMsgRobot = 13,
    kCfgMsgRefill = 14,
    kCfgMsgFiltrationWindows = 15,
};

static constexpr MqttConfigRouteProducer::Route kPoolLogicCfgRoutes[] = {
    {kCfgMsgBase,
     {(uint8_t)ConfigModuleId::PoolLogic, ConfigBranchRef::UnknownLocalBranch},
     nullptr,
     "",
     (uint8_t)MqttPublishPriority::Normal,
     &PoolLogicModule::buildCfgBaseStatic_,
     kPoolLogicCfgTopicBase},
    {kCfgMsgBassin,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchBassin},
     kCfgModuleBassin,
     "bassin",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgFiltration,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchFiltration},
     kCfgModuleFiltration,
     "filtration",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgFiltrationWindows,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchFiltrationWindows},
     kCfgModuleFiltrationWindows,
     "filtration/fenetres",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSensors,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSensors},
     kCfgModuleSensors,
     "sensors",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgSafety,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchSafety},
     kCfgModuleSafety,
     "safety",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgPh,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchPh},
     kCfgModulePh,
     "ph",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgDisinfection,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchDisinfection},
     kCfgModuleDisinfection,
     "disinfection",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgHeater,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchHeater},
     kCfgModuleHeater,
     "heater",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgRobot,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchRobot},
     kCfgModuleRobot,
     "robot",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
    {kCfgMsgRefill,
     {(uint8_t)ConfigModuleId::PoolLogic, kCfgBranchRefill},
     kCfgModuleRefill,
     "refill",
     (uint8_t)MqttPublishPriority::Normal,
     nullptr,
     kPoolLogicCfgTopicBase},
};
}

void PoolLogicModule::applyDomainDefaults(const DomainSpec& domain)
{
    if (const PoolLogicDefaultsSpec* d = domain.poolLogicDefaults) {
        // La plage historique du domaine devient la fenetre 1 du plan.
        filtrWinStart_[0] = (uint16_t)d->filtrationStartMinHour * 60u;
        filtrWinStop_[0] = (uint16_t)d->filtrationStopMaxHour * 60u;
        filtrationCalcStart_ = d->filtrationStartMinHour;
        filtrationCalcStop_ = d->filtrationStopMaxHour;
        pressureLowThreshold_ = d->pressureLow;
        pressureHighThreshold_ = d->pressureHigh;
        winterStartTempC_ = d->winterStartTempC;
        freezeHoldTempC_ = d->freezeHoldTempC;
        secureElectroTempC_ = d->secureElectroTempC;
        phSetpoint_ = d->phSetpoint;
        orpSetpoint_ = d->orpSetpoint;
        heaterSetpoint_ = d->heaterSetpoint;
        orpKp_ = d->orpKp;
        orpKi_ = d->orpKi;
        orpKd_ = d->orpKd;
        // Le PID temporel ne concerne plus que la desinfection : le pH est passe
        // au dosage volumetrique par lots, dont les defauts sont propres a
        // l'installation et restent des initialiseurs de membre.
        orpWindowMs_ = d->pidWindowMs;
        disMinOnMs_ = d->pidMinOnMs;
        disSampleMs_ = d->pidSampleMs;
        pressureStartupDelaySec_ = d->pressureStartupDelaySec;
        delayPidsMin_ = d->delayPidsMin;
        delayElectroMin_ = d->delayElectroMin;
        robotDelayMin_ = d->robotDelayMin;
        robotDurationMin_ = d->robotDurationMin;
        fillingMinOnSec_ = d->fillingMinOnSec;
        poolVolumeM3_ = d->poolVolumeM3;
        o2DoseMlPer10M3Week_ = d->o2DoseMlPer10M3Week;
        o2MainHour_ = d->o2MainHour;
        o2SplitCount_ = d->o2SplitCount;
        o2TempComp_ = d->o2TempComp;
        o2LoadFactor_ = d->o2LoadFactor;
        o2MinFilterRunMin_ = d->o2MinFilterRunMin;
    }

    // IoIds capteurs dérivés des bindings du domaine (remplace l'ancien #if par profil).
    const struct {
        DomainSlotId slot;
        IoId* target;
    } sensorSlots[] = {
        {PoolIds::SensorPh, &phIoId_},
        {PoolIds::SensorOrp, &orpIoId_},
        {PoolIds::SensorPressure, &pressureIoId_},
        // Defaut d'usine : sonde 1 = eau, sonde 2 = air. C'est un simple point
        // de depart, l'utilisateur reaffecte librement depuis la config.
        {PoolIds::SensorTemperature1, &waterTempIoId_},
        {PoolIds::SensorTemperature2, &airTempIoId_},
        {PoolIds::SensorPoolLevel, &levelIoId_},
        {PoolIds::SensorPhLevel, &phLevelIoId_},
        {PoolIds::SensorChlorineLevel, &chlorineLevelIoId_},
        {PoolIds::SensorFlowSwitch, &flowSwitchIoId_},
        {PoolIds::SensorCoverClosed, &coverClosedIoId_},
        // Les 2 sorties de report ne sont plus resolues ici : elles passent par
        // leur PoolDevice (DeviceFlowCopy / DeviceCoverReport).
    };
    for (const auto& s : sensorSlots) {
        const IoSlotId ioSlot = domainIoSlotForRole(domain, s.slot);
        if (ioSlot != IO_SLOT_INVALID) *s.target = ioIdFromSlot(ioSlot);
    }
}

void PoolLogicModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolLogic;
    cfgStore_ = &cfg;
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);

    // Runtime moduleName reassignment keeps the config tree grouped by branch
    // even though the variables are declared on a single facade class.
    enabledVar_.moduleName = kCfgModuleBassin;
    autoModeVar_.moduleName = kCfgModuleBassin;
    winterModeVar_.moduleName = kCfgModuleBassin;
    poolVolumeVar_.moduleName = kCfgModuleBassin;
    delayPidsVar_.moduleName = kCfgModuleBassin;
    phAutoModeVar_.moduleName = kCfgModulePh;
    orpAutoModeVar_.moduleName = kCfgModuleDisinfection;
    heaterAutoModeVar_.moduleName = kCfgModuleHeater;
    phDosePlusVar_.moduleName = kCfgModulePh;
    disinfectionTypeVar_.moduleName = kCfgModuleBassin;
    swgControlModeVar_.moduleName = kCfgModuleDisinfection;

    // Caracteristiques materielles de la pompe de filtration et de son
    // circuit : elles s affichent et se publient cote pdm/pd0, tout en
    // restant lues directement par PoolLogic (turnover, alarmes pression).
    pumpFlowVar_.moduleName = kCfgModulePd0;
    // Le ratio de cycles est un reglage de dimensionnement : il reste dans le
    // menu Filtration, aux cotes des sorties du plan.
    filtrCycleRatioVar_.moduleName = kCfgModuleFiltration;
    // Les fenetres sont regroupees dans une sous-branche dediee : le menu
    // Filtration ne garde que le dimensionnement et les sorties du plan.
    filtrWin1EnVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin1StartVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin1StopVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin1PrioVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin2EnVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin2StartVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin2StopVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin2PrioVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin3EnVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin3StartVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin3StopVar_.moduleName = kCfgModuleFiltrationWindows;
    filtrWin3PrioVar_.moduleName = kCfgModuleFiltrationWindows;
    calcStartVar_.moduleName = kCfgModuleFiltration;
    calcStopVar_.moduleName = kCfgModuleFiltration;
    filtrSegmentsVar_.moduleName = kCfgModuleFiltration;
    filtrOptimalVar_.moduleName = kCfgModuleFiltration;

    phIdVar_.moduleName = kCfgModuleSensors;
    orpIdVar_.moduleName = kCfgModuleSensors;
    pressureIdVar_.moduleName = kCfgModuleSensors;
    waterTempIdVar_.moduleName = kCfgModuleSensors;
    airTempIdVar_.moduleName = kCfgModuleSensors;
    levelIdVar_.moduleName = kCfgModuleSensors;
    phLevelIdVar_.moduleName = kCfgModuleSensors;
    chlorineLevelIdVar_.moduleName = kCfgModuleSensors;
    flowSwitchIdVar_.moduleName = kCfgModuleSensors;
    flowPresentVar_.moduleName = kCfgModuleSensors;
    coverClosedIdVar_.moduleName = kCfgModuleSensors;

    pressureLowVar_.moduleName = kCfgModulePd0;
    pressureHighVar_.moduleName = kCfgModulePd0;
    winterStartVar_.moduleName = kCfgModuleSafety;
    freezeHoldVar_.moduleName = kCfgModuleSafety;
    secureElectroVar_.moduleName = kCfgModuleDisinfection;
    phSetpointVar_.moduleName = kCfgModulePh;
    phDoseMlPerM3Var_.moduleName = kCfgModulePh;
    phDeadbandVar_.moduleName = kCfgModulePh;
    phDoseFactorVar_.moduleName = kCfgModulePh;
    phDoseMaxBatchVar_.moduleName = kCfgModulePh;
    phDoseMaxDayVar_.moduleName = kCfgModulePh;
    phValidMinVar_.moduleName = kCfgModulePh;
    phValidMaxVar_.moduleName = kCfgModulePh;
    phNoEffectDeltaVar_.moduleName = kCfgModulePh;
    phGainLearnedVar_.moduleName = kCfgModulePh;
    phMixWaitMinVar_.moduleName = kCfgModulePh;
    phSampleMaxAgeVar_.moduleName = kCfgModulePh;
    phNoEffectLotsVar_.moduleName = kCfgModulePh;
    phGainSamplesVar_.moduleName = kCfgModulePh;
    phLastDoseTsVar_.moduleName = kCfgModulePh;
    orpSetpointVar_.moduleName = kCfgModuleDisinfection;
    heaterSetpointVar_.moduleName = kCfgModuleHeater;
    orpKpVar_.moduleName = kCfgModuleDisinfection;
    orpKiVar_.moduleName = kCfgModuleDisinfection;
    orpKdVar_.moduleName = kCfgModuleDisinfection;
    orpWindowMsVar_.moduleName = kCfgModuleDisinfection;
    disMinOnMsVar_.moduleName = kCfgModuleDisinfection;
    disSampleMsVar_.moduleName = kCfgModuleDisinfection;

    pressureDelayVar_.moduleName = kCfgModulePd0;
    delayElectroVar_.moduleName = kCfgModuleDisinfection;
    robotDelayVar_.moduleName = kCfgModuleRobot;
    robotDurationVar_.moduleName = kCfgModuleRobot;
    fillingMinOnVar_.moduleName = kCfgModuleRefill;

    o2DoseVar_.moduleName = kCfgModuleDisinfection;
    o2MainHourVar_.moduleName = kCfgModuleDisinfection;
    o2SplitCountVar_.moduleName = kCfgModuleDisinfection;
    o2TempCompVar_.moduleName = kCfgModuleDisinfection;
    o2LoadFactorVar_.moduleName = kCfgModuleDisinfection;
    o2MinFilterRunVar_.moduleName = kCfgModuleDisinfection;
    o2ProtocolStateVar_.moduleName = kCfgModuleDisinfection;
    o2LastDoseDayVar_.moduleName = kCfgModuleDisinfection;
    o2WeeklyDoneVar_.moduleName = kCfgModuleDisinfection;
    o2PendingVar_.moduleName = kCfgModuleDisinfection;


    // Registration order mirrors the published config branches so init remains
    // easy to diff against the generated cfgdocs and MQTT routes.
    // Bassin : parametres transverses (identite du bassin, modes maitres,
    // methode de desinfection, delai d'armement des regulations).
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchBassin);
    cfg.registerVar(autoModeVar_, kCfgModuleId, kCfgBranchBassin);
    cfg.registerVar(winterModeVar_, kCfgModuleId, kCfgBranchBassin);
    cfg.registerVar(poolVolumeVar_, kCfgModuleId, kCfgBranchBassin);
    cfg.registerVar(disinfectionTypeVar_, kCfgModuleId, kCfgBranchBassin);
    cfg.registerVar(delayPidsVar_, kCfgModuleId, kCfgBranchBassin);

    cfg.registerVar(phAutoModeVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpAutoModeVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(heaterAutoModeVar_, kCfgModuleId, kCfgBranchHeater);
    cfg.registerVar(phDosePlusVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(swgControlModeVar_, kCfgModuleId, kCfgBranchDisinfection);

    cfg.registerVar(pumpFlowVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(filtrCycleRatioVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(filtrWin1EnVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin1StartVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin1StopVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin1PrioVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin2EnVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin2StartVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin2StopVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin2PrioVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin3EnVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin3StartVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin3StopVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(filtrWin3PrioVar_, kCfgModuleId, kCfgBranchFiltrationWindows);
    cfg.registerVar(calcStartVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(calcStopVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(filtrSegmentsVar_, kCfgModuleId, kCfgBranchFiltration);
    cfg.registerVar(filtrOptimalVar_, kCfgModuleId, kCfgBranchFiltration);

    cfg.registerVar(phIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(orpIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(pressureIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(waterTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(airTempIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(levelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(phLevelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(chlorineLevelIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(flowSwitchIdVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(flowPresentVar_, kCfgModuleId, kCfgBranchSensors);
    cfg.registerVar(coverClosedIdVar_, kCfgModuleId, kCfgBranchSensors);

    cfg.registerVar(pressureLowVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(pressureHighVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(winterStartVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(freezeHoldVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(secureElectroVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(phSetpointVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phDoseMlPerM3Var_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phDeadbandVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phDoseFactorVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phDoseMaxBatchVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phDoseMaxDayVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phValidMinVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phValidMaxVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phNoEffectDeltaVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phGainLearnedVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phMixWaitMinVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phSampleMaxAgeVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phNoEffectLotsVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phGainSamplesVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(phLastDoseTsVar_, kCfgModuleId, kCfgBranchPh);
    cfg.registerVar(orpSetpointVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(heaterSetpointVar_, kCfgModuleId, kCfgBranchHeater);
    cfg.registerVar(orpKpVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(orpKiVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(orpKdVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(orpWindowMsVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(disMinOnMsVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(disSampleMsVar_, kCfgModuleId, kCfgBranchDisinfection);

    cfg.registerVar(pressureDelayVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(delayElectroVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(robotDelayVar_, kCfgModuleId, kCfgBranchRobot);
    cfg.registerVar(robotDurationVar_, kCfgModuleId, kCfgBranchRobot);
    cfg.registerVar(fillingMinOnVar_, kCfgModuleId, kCfgBranchRefill);

    cfg.registerVar(o2DoseVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2MainHourVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2SplitCountVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2TempCompVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2LoadFactorVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2MinFilterRunVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2ProtocolStateVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2LastDoseDayVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2WeeklyDoneVar_, kCfgModuleId, kCfgBranchDisinfection);
    cfg.registerVar(o2PendingVar_, kCfgModuleId, kCfgBranchDisinfection);

    cfg.registerVar(flowCopyDelayVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(flowInterlockVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(sensorHoldVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(sensorHoldSettleVar_, kCfgModuleId, kCfgBranchSafety);
    cfg.registerVar(sensorHoldWatVar_, kCfgModuleId, kCfgBranchSafety);

    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    timeSvc_ = services.get<TimeService>(ServiceId::Time);
    schedSvc_ = services.get<TimeSchedulerService>(ServiceId::TimeScheduler);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    poolSvc_ = services.get<PoolDeviceService>(ServiceId::PoolDevice);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    const HAService* haSvc = services.get<HAService>(ServiceId::Ha);
    const CommandService* cmdSvc = services.get<CommandService>(ServiceId::Command);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    activityLogSvc_ = services.get<ActivityLogService>(ServiceId::ActivityLog);
    if (!ioSvc_) {
        LOGW("PoolLogic waiting for IOServiceV2");
    }
    if (!poolSvc_) {
        LOGW("PoolLogic waiting for PoolDeviceService");
    }
    // HA entities are still registered from the lifecycle layer because they
    // are part of startup wiring, not of the control algorithm itself.
    if (haSvc && haSvc->addSwitch) {
        const HASwitchEntry autoModeSwitch{
            "poollogic",
            "pl_modes_auto",
            "Pool Auto-regulation",
            "cfg/poollogic/bassin",
            "{% if value_json.auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/bassin\\\":{\\\"auto_mode\\\":true}}",
            "{\\\"poollogic/bassin\\\":{\\\"auto_mode\\\":false}}",
            "mdi:calendar-clock",
            "config"
        };
        const HASwitchEntry winterModeSwitch{
            "poollogic",
            "pl_modes_winter",
            "Winter Mode",
            "cfg/poollogic/bassin",
            "{% if value_json.winter_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/bassin\\\":{\\\"winter_mode\\\":true}}",
            "{\\\"poollogic/bassin\\\":{\\\"winter_mode\\\":false}}",
            "mdi:snowflake",
            "config"
        };
        const HASwitchEntry phAutoModeSwitch{
            "poollogic",
            "pl_ph_auto_mode",
            "pH Auto-regulation",
            "cfg/poollogic/ph",
            "{% if value_json.ph_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_auto_mode\\\":true}}",
            "{\\\"poollogic/ph\\\":{\\\"ph_auto_mode\\\":false}}",
            "mdi:beaker-check-outline",
            "config"
        };
        const HASwitchEntry orpAutoModeSwitch{
            "poollogic",
            "pl_dis_auto",
            "Orp Auto-regulation",
            "cfg/poollogic/disinfection",
            "{% if value_json.dis_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"dis_auto_mode\\\":true}}",
            "{\\\"poollogic/disinfection\\\":{\\\"dis_auto_mode\\\":false}}",
            "mdi:water-check-outline",
            "config"
        };
        const HASwitchEntry heaterAutoModeSwitch{
            "poollogic",
            "pl_heat_auto",
            "Heater Auto-regulation",
            "cfg/poollogic/heater",
            "{% if value_json.heater_auto_mode %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/heater\\\":{\\\"heater_auto_mode\\\":true}}",
            "{\\\"poollogic/heater\\\":{\\\"heater_auto_mode\\\":false}}",
            "mdi:radiator",
            "config"
        };
        const HASwitchEntry phDosePlusSwitch{
            "poollogic",
            "pl_ph_dose_plus",
            "pH Dosing uses pH+",
            "cfg/poollogic/ph",
            "{% if value_json.ph_dose_plus %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_dose_plus\\\":true}}",
            "{\\\"poollogic/ph\\\":{\\\"ph_dose_plus\\\":false}}",
            "mdi:beaker-plus-outline",
            "config"
        };
        const HASwitchEntry o2TempCompSwitch{
            "poollogic",
            "pl_o2_temp_comp",
            "O2 Temperature Compensation",
            "cfg/poollogic/disinfection",
            "{% if value_json.temp_comp %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"temp_comp\\\":true}}",
            "{\\\"poollogic/disinfection\\\":{\\\"temp_comp\\\":false}}",
            "mdi:thermometer-lines",
            "config"
        };
        const HASwitchEntry flowInterlockSwitch{
            "poollogic",
            "pl_flow_interlock",
            "Flow Safety Interlock",
            "cfg/poollogic/safety",
            "{% if value_json.flow_interlock %}ON{% else %}OFF{% endif %}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/safety\\\":{\\\"flow_interlock\\\":true}}",
            "{\\\"poollogic/safety\\\":{\\\"flow_interlock\\\":false}}",
            "mdi:water-alert",
            "config"
        };
        (void)haSvc->addSwitch(haSvc->ctx, &autoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &winterModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &phAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &orpAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &heaterAutoModeSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &phDosePlusSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &o2TempCompSwitch);
        (void)haSvc->addSwitch(haSvc->ctx, &flowInterlockSwitch);
    }
    if (haSvc && haSvc->addSelect) {
        static const char* kDisinfectionTypeStateTpl =
            R"({% set v = value_json.disinfection_type | int(0) %}{% if v == 1 %}Chlore/Brome{% elif v == 2 %}Electrolyse{% elif v == 3 %}Oxygène actif{% else %}Désactivé{% endif %})";
        static const char* kDisinfectionTypeCmdTpl =
            R"({% if value == 'Chlore/Brome' %}{\"poollogic/bassin\":{\"disinfection_type\":1}}{% elif value == 'Electrolyse' %}{\"poollogic/bassin\":{\"disinfection_type\":2}}{% elif value == 'Oxygène actif' %}{\"poollogic/bassin\":{\"disinfection_type\":3}}{% else %}{\"poollogic/bassin\":{\"disinfection_type\":0}}{% endif %})";
        const HASelectEntry disinfectionTypeSelect{
            "poollogic",
            "pl_modes_dis",
            "Disinfection Type",
            "cfg/poollogic/bassin",
            kDisinfectionTypeStateTpl,
            MqttTopics::SuffixCfgSet,
            kDisinfectionTypeCmdTpl,
            "[\"Désactivé\",\"Chlore/Brome\",\"Electrolyse\",\"Oxygène actif\"]",
            "mdi:water-check",
            "config"
        };
        static const char* kSwgControlModeStateTpl =
            R"({% set v = value_json.swg_control_mode | int(1) %}{% if v == 0 %}Suivi consigne ORP{% else %}Continu sur filtration{% endif %})";
        static const char* kSwgControlModeCmdTpl =
            R"({% if value == 'Suivi consigne ORP' %}{\"poollogic/disinfection\":{\"swg_control_mode\":0}}{% else %}{\"poollogic/disinfection\":{\"swg_control_mode\":1}}{% endif %})";
        const HASelectEntry swgControlModeSelect{
            "poollogic",
            "pl_swg_ctrl",
            "SWG Control Mode",
            "cfg/poollogic/disinfection",
            kSwgControlModeStateTpl,
            MqttTopics::SuffixCfgSet,
            kSwgControlModeCmdTpl,
            "[\"Suivi consigne ORP\",\"Continu sur filtration\"]",
            "mdi:flash",
            "config"
        };
        static const char* kO2MainHourStateTpl =
            R"({% set h = value_json.main_hour | int(12) %}{{ '%02d:00' | format(h) }})";
        static const char* kO2MainHourCmdTpl =
            R"({\"poollogic/disinfection\":{\"main_hour\":{{ value.split(':')[0] | int(12) }}}})";
        const HASelectEntry o2MainHourSelect{
            "poollogic",
            "pl_o2_hour",
            "O2 Dosing Hour",
            "cfg/poollogic/disinfection",
            kO2MainHourStateTpl,
            MqttTopics::SuffixCfgSet,
            kO2MainHourCmdTpl,
            "[\"00:00\",\"01:00\",\"02:00\",\"03:00\",\"04:00\",\"05:00\",\"06:00\",\"07:00\",\"08:00\",\"09:00\",\"10:00\",\"11:00\",\"12:00\",\"13:00\",\"14:00\",\"15:00\",\"16:00\",\"17:00\",\"18:00\",\"19:00\",\"20:00\",\"21:00\",\"22:00\",\"23:00\"]",
            "mdi:clock-time-four-outline",
            "config"
        };
        (void)haSvc->addSelect(haSvc->ctx, &disinfectionTypeSelect);
        (void)haSvc->addSelect(haSvc->ctx, &swgControlModeSelect);
        (void)haSvc->addSelect(haSvc->ctx, &o2MainHourSelect);
    }
    if (haSvc && haSvc->addSensor) {
        const HASensorEntry filtrationStart{
            "poollogic",
            "pl_flt_start",
            "Calculated Filtration Start",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_start_clc | int(0) }}",
            nullptr,
            "mdi:clock-start",
            "h"
        };
        const HASensorEntry filtrationStop{
            "poollogic",
            "pl_flt_stop",
            "Calculated Filtration Stop",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_stop_clc | int(0) }}",
            nullptr,
            "mdi:clock-end",
            "h"
        };
        static const char* kHeatAssistStatusFrTemplate =
            R"({% set st = value_json.ri | default('UNKNOWN', true) %}{% if st == 'DISABLED' %}Désactivé{% elif st == 'MANUAL_MODE' %}Mode manuel{% elif st == 'PRESSURE_BLOCKED' %}Pression bloquée{% elif st == 'SETPOINT_INVALID' %}Consigne invalide{% elif st == 'TEMP_UNAVAILABLE' %}Température indisponible{% elif st == 'PROBE_WAIT_30M' %}Attente sonde 30 min{% elif st == 'PROBE_WAIT_20M' %}Attente sonde 20 min{% elif st == 'PROBE_RUNNING' %}Sondage en cours{% elif st == 'HEATING' %}Chauffe active{% elif st == 'IDLE_PUMP_ON' %}Pompe active sans chauffe{% elif st == 'SETPOINT_REACHED' %}Consigne atteinte{% else %}Inconnu{% endif %})";
        const HASensorEntry heatAssistStatus{
            "poollogic",
            "pl_has_rsn",
            "Heat Assist Status",
            "rt/poollogic/heat_assist",
            kHeatAssistStatusFrTemplate,
            nullptr,
            "mdi:information-outline",
            nullptr,
            false,
            nullptr,
            true
        };
        static const char* kO2ProtocolStateTemplate =
            R"({% set st = value_json.o2.state | int(0) %}{% if st == 1 %}En attente{% elif st == 2 %}Dosage{% elif st == 3 %}Bloqué{% else %}Repos{% endif %})";
        const HASensorEntry o2ProtocolState{
            "poollogic",
            "pl_o2_state",
            "O2 Protocol State",
            "rt/poollogic/disinfection",
            kO2ProtocolStateTemplate,
            nullptr,
            "mdi:progress-clock",
            nullptr,
            false,
            nullptr,
            true
        };
        const HASensorEntry o2WeeklyDone{
            "poollogic",
            "pl_o2_done",
            "O2 Weekly Injected",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.done_ml | float(0) }}",
            "diagnostic",
            "mdi:bottle-tonic-plus-outline",
            "ml"
        };
        const HASensorEntry o2Pending{
            "poollogic",
            "pl_o2_pending",
            "O2 Pending Volume",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.pending_ml | float(0) }}",
            "diagnostic",
            "mdi:bottle-tonic-outline",
            "ml"
        };
        const HASensorEntry o2LastDoseDay{
            "poollogic",
            "pl_o2_last_day",
            "O2 Last Dose Day",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.last_day | int(0) }}",
            "diagnostic",
            "mdi:calendar-check-outline",
            nullptr
        };
        const HASensorEntry o2BlockReason{
            "poollogic",
            "pl_o2_block",
            "O2 Block Reason",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.block_s | default('none') }}",
            "diagnostic",
            "mdi:alert-circle-outline",
            nullptr,
            false,
            nullptr,
            true
        };
        const HASensorEntry o2PlannedDose{
            "poollogic",
            "pl_o2_plan",
            "O2 Planned Dose",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.plan_ml | float(0) }}",
            "diagnostic",
            "mdi:cup-water",
            "ml"
        };
        const HASensorEntry o2PumpFlow{
            "poollogic",
            "pl_o2_flow",
            "O2 Pump Flow",
            "rt/poollogic/disinfection",
            "{{ value_json.o2.flow_l_h | float(0) }}",
            "diagnostic",
            "mdi:pump",
            "L/h"
        };
        // Temperatures metier. Les suffixes historiques io_wat_tmp / io_air_tmp
        // sont conserves pour ne pas casser les tableaux de bord existants : ce
        // sont desormais des entites PoolLogic, qui suivent la sonde designee
        // par wat_temp_io_id / air_temp_io_id.
        const HASensorEntry waterTemperature{
            "poollogic",
            "io_wat_tmp",
            "Water Temperature",
            "rt/poollogic/temp",
            "{% if value_json.wat is number %}{{ value_json.wat | float | round(1) }}{% else %}unavailable{% endif %}",
            nullptr,
            "mdi:water-thermometer",
            "\xC2\xB0""C"
        };
        const HASensorEntry airTemperature{
            "poollogic",
            "io_air_tmp",
            "Air Temperature",
            "rt/poollogic/temp",
            "{% if value_json.air is number %}{{ value_json.air | float | round(1) }}{% else %}unavailable{% endif %}",
            nullptr,
            "mdi:thermometer",
            "\xC2\xB0""C"
        };
        (void)haSvc->addSensor(haSvc->ctx, &waterTemperature);
        (void)haSvc->addSensor(haSvc->ctx, &airTemperature);
        (void)haSvc->addSensor(haSvc->ctx, &filtrationStart);
        (void)haSvc->addSensor(haSvc->ctx, &filtrationStop);
        (void)haSvc->addSensor(haSvc->ctx, &heatAssistStatus);
        (void)haSvc->addSensor(haSvc->ctx, &o2ProtocolState);
        (void)haSvc->addSensor(haSvc->ctx, &o2WeeklyDone);
        (void)haSvc->addSensor(haSvc->ctx, &o2Pending);
        (void)haSvc->addSensor(haSvc->ctx, &o2LastDoseDay);
        (void)haSvc->addSensor(haSvc->ctx, &o2BlockReason);
        (void)haSvc->addSensor(haSvc->ctx, &o2PlannedDose);
        (void)haSvc->addSensor(haSvc->ctx, &o2PumpFlow);
    }
    if (haSvc && haSvc->addNumber) {
        const HANumberEntry pumpFlow{
            "poollogic",
            "pl_pump_flow",
            "Filtration Pump Flow",
            "cfg/pdm",
            "{{ value_json.pd0.pump_flow_m3h | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"pdm/pd0\\\":{\\\"pump_flow_m3h\\\":{{ value | float(0) }}}}",
            1.0f,
            40.0f,
            0.5f,
            "box",
            "config",
            "mdi:pump",
            "m3/h"
        };
        const HANumberEntry filtrCycleRatio{
            "poollogic",
            "pl_filtr_ratio",
            "Filtration Cycle Ratio",
            "cfg/poollogic/filtration",
            "{{ value_json.filtr_cycle_ratio | int(100) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/filtration\\\":{\\\"filtr_cycle_ratio\\\":{{ value | int(100) }}}}",
            50.0f,
            200.0f,
            5.0f,
            "slider",
            "config",
            "mdi:sync",
            "%"
        };
        const HANumberEntry delayPidsMin{
            "poollogic",
            "pl_reg_dly_pid",
            "Delay PIDs",
            "cfg/poollogic/bassin",
            "{{ value_json.dly_pid_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/bassin\\\":{\\\"dly_pid_min\\\":{{ value | int(0) }}}}",
            0.0f,
            30.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry delayElectroMin{
            "poollogic",
            "pl_swg_dly_elec",
            "Delay Chlorine Generator",
            "cfg/poollogic/disinfection",
            "{{ value_json.dly_electro_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"dly_electro_min\\\":{{ value | int(0) }}}}",
            0.0f,
            120.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-sand",
            "min"
        };
        const HANumberEntry fillMinUptime{
            "poollogic",
            "pl_refill_min_on",
            "Min Uptime Fill Pump",
            "cfg/poollogic/refill",
            "{{ ((value_json.fill_min_on_s | float(0)) / 60) | round(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/refill\\\":{\\\"fill_min_on_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
            0.0f,
            4.0f,
            0.5f,
            "box",
            "config",
            "mdi:timer-cog-outline",
            "mn"
        };
        const HANumberEntry phSetpoint{
            "poollogic",
            "pl_ph_setpoint",
            "pH Setpoint",
            "cfg/poollogic/ph",
            "{{ value_json.ph_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/ph\\\":{\\\"ph_setpoint\\\":{{ value | float(0) }}}}",
            6.0f,
            8.0f,
            0.01f,
            "slider",
            "config",
            "mdi:beaker-outline",
            nullptr
        };
        const HANumberEntry orpSetpoint{
            "poollogic",
            "pl_dis_setpoint",
            "Orp Setpoint",
            "cfg/poollogic/disinfection",
            "{{ value_json.dis_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"dis_setpoint\\\":{{ value | float(0) }}}}",
            300.0f,
            900.0f,
            1.0f,
            "slider",
            "config",
            "mdi:water-outline",
            "mV"
        };
        const HANumberEntry heaterSetpoint{
            "poollogic",
            "pl_heat_setpoint",
            "Water Heater Setpoint",
            "cfg/poollogic/heater",
            "{{ value_json.heater_setpoint | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/heater\\\":{\\\"heater_setpoint\\\":{{ value | float(0) }}}}",
            10.0f,
            35.0f,
            0.1f,
            "slider",
            "config",
            "mdi:thermometer-water",
            "C"
        };
        const HANumberEntry chlorineGeneratorMinTemp{
            "poollogic",
            "pl_swg_min_temp",
            "Min Temperature Chlorine Generator",
            "cfg/poollogic/disinfection",
            "{{ value_json.secure_elec_t | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"secure_elec_t\\\":{{ value | float(0) }}}}",
            5.0f,
            35.0f,
            0.1f,
            "slider",
            "config",
            "mdi:thermometer-alert",
            "C"
        };
        // Pierre tombale : le PID pH a laisse place au dosage par lots, la cle
        // ph_window_ms n'existe plus. L'entite pilotait donc une cle absente.
        // Champs inutilises par une pierre tombale, mais addNumberEntry() les
        // exige non nuls avant d'accepter l'entree. Supprimable apres une release.
        const HANumberEntry retiredPhWindow{
            "poollogic",
            "pl_ph_window",
            "pH PID Window Size",
            "cfg/poollogic/ph",
            "{{ 0 }}",
            MqttTopics::SuffixCfgSet,
            "{}",
            0.0f,
            0.0f,
            0.0f,
            "box",
            "config",
            nullptr,
            nullptr,
            true
        };
        const HANumberEntry orpWindowMin{
            "poollogic",
            "pl_dis_window",
            "Orp PID Window Size",
            "cfg/poollogic/disinfection",
            "{{ ((value_json.dis_window_ms | float(0)) / 60000) | round(0) | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"dis_window_ms\\\":{{ (value | float(0) * 60000) | round(0) | int(0) }}}}",
            1.0f,
            180.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timeline-clock-outline",
            "min"
        };
        const HANumberEntry pressureLowThreshold{
            "poollogic",
            "pl_safe_pressure_low",
            "Pressure Low Threshold",
            "cfg/pdm",
            "{{ value_json.pd0.pressure_low_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"pdm/pd0\\\":{\\\"pressure_low_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-low",
            "bar"
        };
        const HANumberEntry pressureHighThreshold{
            "poollogic",
            "pl_safe_pressure_high",
            "Pressure High Threshold",
            "cfg/pdm",
            "{{ value_json.pd0.pressure_high_th | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"pdm/pd0\\\":{\\\"pressure_high_th\\\":{{ value | float(0) }}}}",
            0.0f,
            5.0f,
            0.01f,
            "slider",
            "config",
            "mdi:gauge-full",
            "bar"
        };
        // Volume du bassin : parametre partage (besoin de filtration et doses O2),
        // porte par la branche Bassin. L'entite pointait cfg/poollogic/filtration,
        // ou la cle n'a jamais existe : elle affichait 0 et ses commandes etaient
        // ignorees en silence (patch sur une branche sans pool_volume_m3).
        const HANumberEntry poolVolume{
            "poollogic",
            "pl_pool_vol",
            "Pool Volume",
            "cfg/poollogic/bassin",
            "{{ value_json.pool_volume_m3 | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/bassin\\\":{\\\"pool_volume_m3\\\":{{ value | float(0) }}}}",
            1.0f,
            200.0f,
            0.1f,
            "box",
            "config",
            "mdi:pool",
            "m3"
        };
        // Pierre tombale de l'ancien object_id, dont le prefixe o2 datait du temps
        // ou le volume vivait dans la branche Oxygene actif. Supprimable apres une
        // release, comme retiredPhWindow.
        const HANumberEntry retiredO2PoolVolume{
            "poollogic",
            "pl_o2_vol",
            "Pool Volume",
            "cfg/poollogic/bassin",
            "{{ 0 }}",
            MqttTopics::SuffixCfgSet,
            "{}",
            0.0f,
            0.0f,
            0.0f,
            "box",
            "config",
            nullptr,
            nullptr,
            true
        };
        const HANumberEntry o2WeeklyDose{
            "poollogic",
            "pl_o2_dose",
            "O2 Weekly Dose per 10 m3",
            "cfg/poollogic/disinfection",
            "{{ value_json.dose_ml_10m3_week | float(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"dose_ml_10m3_week\\\":{{ value | float(0) }}}}",
            0.0f,
            5000.0f,
            10.0f,
            "box",
            "config",
            "mdi:cup-water",
            "ml"
        };
        const HANumberEntry o2SplitCount{
            "poollogic",
            "pl_o2_split",
            "O2 Weekly Split Count",
            "cfg/poollogic/disinfection",
            "{{ value_json.split_count | int(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"split_count\\\":{{ value | int(1) }}}}",
            1.0f,
            3.0f,
            1.0f,
            "box",
            "config",
            "mdi:call-split",
            nullptr
        };
        const HANumberEntry o2LoadFactor{
            "poollogic",
            "pl_o2_load",
            "O2 Load Factor",
            "cfg/poollogic/disinfection",
            "{{ value_json.load_factor | float(1) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"load_factor\\\":{{ value | float(1) }}}}",
            0.5f,
            2.0f,
            0.05f,
            "slider",
            "config",
            "mdi:scale-balance",
            nullptr
        };
        const HANumberEntry o2MinFilterRun{
            "poollogic",
            "pl_o2_min_flt",
            "O2 Min Filtration Runtime",
            "cfg/poollogic/disinfection",
            "{{ value_json.min_filter_run_min | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/disinfection\\\":{\\\"min_filter_run_min\\\":{{ value | int(0) }}}}",
            0.0f,
            240.0f,
            1.0f,
            "slider",
            "config",
            "mdi:timer-check-outline",
            "min"
        };
        (void)haSvc->addNumber(haSvc->ctx, &heaterSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &pumpFlow);
        (void)haSvc->addNumber(haSvc->ctx, &filtrCycleRatio);
        (void)haSvc->addNumber(haSvc->ctx, &delayPidsMin);
        (void)haSvc->addNumber(haSvc->ctx, &delayElectroMin);
        (void)haSvc->addNumber(haSvc->ctx, &fillMinUptime);
        (void)haSvc->addNumber(haSvc->ctx, &phSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &orpSetpoint);
        (void)haSvc->addNumber(haSvc->ctx, &chlorineGeneratorMinTemp);
        (void)haSvc->addNumber(haSvc->ctx, &orpWindowMin);
        (void)haSvc->addNumber(haSvc->ctx, &pressureLowThreshold);
        (void)haSvc->addNumber(haSvc->ctx, &pressureHighThreshold);
        (void)haSvc->addNumber(haSvc->ctx, &poolVolume);
        if (!haSvc->addNumber(haSvc->ctx, &retiredPhWindow)) {
            LOGW("HA tombstone registration failed: pl_ph_window");
        }
        if (!haSvc->addNumber(haSvc->ctx, &retiredO2PoolVolume)) {
            LOGW("HA tombstone registration failed: pl_o2_vol");
        }
        const HANumberEntry flowCopyDelay{
            "poollogic",
            "pl_flow_copy_delay",
            "Flow Copy Output Delay",
            "cfg/poollogic/safety",
            "{{ value_json.flow_copy_delay_s | int(0) }}",
            MqttTopics::SuffixCfgSet,
            "{\\\"poollogic/safety\\\":{\\\"flow_copy_delay_s\\\":{{ value | int(0) }}}}",
            0.0f,
            255.0f,
            1.0f,
            "box",
            "config",
            "mdi:timer-sand",
            "s"
        };
        (void)haSvc->addNumber(haSvc->ctx, &o2WeeklyDose);
        (void)haSvc->addNumber(haSvc->ctx, &o2SplitCount);
        (void)haSvc->addNumber(haSvc->ctx, &o2LoadFactor);
        (void)haSvc->addNumber(haSvc->ctx, &o2MinFilterRun);
        (void)haSvc->addNumber(haSvc->ctx, &flowCopyDelay);
    }
    if (haSvc && haSvc->addButton) {
        const HAButtonEntry filtrationRecalc{
            "poollogic",
            "pl_flt_recalc",
            "Recalculate Filtration Window",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"poollogic.filtration.recalc\\\"}",
            "config",
            "mdi:refresh"
        };
        (void)haSvc->addButton(haSvc->ctx, &filtrationRecalc);
    }
    if (haSvc && haSvc->addBinarySensor) {
        // Etat logique des 2 sorties indicatrices + interlock, publie sur le
        // snapshot runtime rt/poollogic/flow.
        const HABinarySensorEntry flowCopyOut{
            "poollogic", "pl_flow_copy_out", "Flow Copy Output",
            "rt/poollogic/flow",
            "{{ 'True' if value_json.flow_copy else 'False' }}",
            nullptr, nullptr, "mdi:waves-arrow-right", false
        };
        const HABinarySensorEntry coverOut{
            "poollogic", "pl_cover_out", "Cover Closed Output",
            "rt/poollogic/flow",
            "{{ 'True' if value_json.cover else 'False' }}",
            nullptr, nullptr, "mdi:window-shutter", false
        };
        const HABinarySensorEntry noFlow{
            "poollogic", "pl_no_flow", "No Flow (interlock)",
            "rt/poollogic/flow",
            "{{ 'True' if value_json.no_flow else 'False' }}",
            "problem", nullptr, "mdi:water-alert", false
        };
        // Sans cette entite, une courbe pH parfaitement plate pendant douze
        // heures serait indiscernable d'une sonde morte.
        const HABinarySensorEntry sensorHold{
            "poollogic", "pl_sensor_hold", "Readings Held (no flow)",
            "rt/poollogic/flow",
            "{{ 'True' if value_json.hold else 'False' }}",
            nullptr, nullptr, "mdi:snowflake", false
        };
        (void)haSvc->addBinarySensor(haSvc->ctx, &flowCopyOut);
        (void)haSvc->addBinarySensor(haSvc->ctx, &coverOut);
        (void)haSvc->addBinarySensor(haSvc->ctx, &noFlow);
        (void)haSvc->addBinarySensor(haSvc->ctx, &sensorHold);
    }
    if (cmdSvc && cmdSvc->registerHandler) {
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.filtration.write", &PoolLogicModule::cmdFiltrationWriteStatic_, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.filtration.recalc", &PoolLogicModule::cmdFiltrationRecalcStatic_, this);
        cmdSvc->registerHandler(cmdSvc->ctx, "poollogic.auto_mode.set", &PoolLogicModule::cmdAutoModeSetStatic_, this);
        static constexpr const char* kMqttControlCmds[] = {
            "poollogic.auto_mode.toggle",
            "poollogic.ph_auto_mode.set",
            "poollogic.ph_auto_mode.toggle",
            "poollogic.orp_auto_mode.set",
            "poollogic.orp_auto_mode.toggle",
            "poollogic.dis_auto_mode.set",
            "poollogic.dis_auto_mode.toggle",
            "poollogic.heater_auto_mode.set",
            "poollogic.heater_auto_mode.toggle",
            "poollogic.winter_mode.set",
            "poollogic.winter_mode.toggle",
            "poollogic.filtration.toggle",
            "poollogic.ph_pump.write",
            "poollogic.ph_pump.toggle",
            "poollogic.orp_pump.write",
            "poollogic.orp_pump.toggle",
            "poollogic.dis_pump.write",
            "poollogic.dis_pump.toggle",
            "poollogic.lights.write",
            "poollogic.lights.toggle",
            "poollogic.robot.write",
            "poollogic.robot.toggle",
            "poollogic.heater.write",
            "poollogic.heater.toggle",
            "poollogic.chlorine_generator.write",
            "poollogic.chlorine_generator.toggle"
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(kMqttControlCmds) / sizeof(kMqttControlCmds[0])); ++i) {
            cmdSvc->registerHandler(cmdSvc->ctx, kMqttControlCmds[i], &PoolLogicModule::cmdMqttControlStatic_, this);
        }
    }
    // PoolLogic owns the alarm definitions but delegates evaluation to the
    // shared alarm module through static condition callbacks.
    if (alarmSvc_ && alarmSvc_->registerAlarm) {
        const AlarmRegistration pressureLowAlarm{
            AlarmId::PoolPressureLow,
            AlarmSeverity::Alarm,
            true,
            2000,
            1000,
            60000,
            "pressure_low",
            "Low pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &pressureLowAlarm, &PoolLogicModule::condPressureLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPressureLow");
        }

        const AlarmRegistration pressureHighAlarm{
            AlarmId::PoolPressureHigh,
            AlarmSeverity::Critical,
            true,
            0,
            1000,
            60000,
            "pressure_high",
            "High pressure",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &pressureHighAlarm, &PoolLogicModule::condPressureHighStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPressureHigh");
        }

        const AlarmRegistration phTankLowAlarm{
            AlarmId::PoolPhTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "ph_tank_low",
            "pH tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phTankLowAlarm, &PoolLogicModule::condPhTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhTankLow");
        }

        const AlarmRegistration chlorineTankLowAlarm{
            AlarmId::PoolChlorineTankLow,
            AlarmSeverity::Alarm,
            false,
            500,
            1000,
            60000,
            "chlorine_tank_low",
            "Chlorine tank low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorineTankLowAlarm, &PoolLogicModule::condChlorineTankLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorineTankLow");
        }

        const AlarmRegistration phPumpMaxUptimeAlarm{
            AlarmId::PoolPhPumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "ph_pump_max_uptime",
            "pH pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phPumpMaxUptimeAlarm, &PoolLogicModule::condPhPumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhPumpMaxUptime");
        }

        const AlarmRegistration chlorinePumpMaxUptimeAlarm{
            AlarmId::PoolChlorinePumpMaxUptime,
            AlarmSeverity::Alarm,
            true,
            500,
            1000,
            60000,
            "chlorine_pump_uptime",
            "Chlorine pump max uptime reached",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &chlorinePumpMaxUptimeAlarm, &PoolLogicModule::condChlorinePumpMaxUptimeStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolChlorinePumpMaxUptime");
        }

        const AlarmRegistration waterLevelLowAlarm{
            AlarmId::PoolWaterLevelLow,
            AlarmSeverity::Alarm,
            false,
            60000,
            1000,
            60000,
            "pool_water_level_low",
            "Pool water level low",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &waterLevelLowAlarm, &PoolLogicModule::condWaterLevelLowStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolWaterLevelLow");
        }

        // Latchee : la cause est materielle et demande une intervention. Aucun
        // onDelay, la FSM a deja temporise N lots d'un turnover chacun.
        const AlarmRegistration phDoseNoEffectAlarm{
            AlarmId::PoolPhDoseNoEffect,
            AlarmSeverity::Alarm,
            true,
            0,
            60000,
            3600000,
            "ph_dose_no_effect",
            "pH dosing has no effect",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &phDoseNoEffectAlarm, &PoolLogicModule::condPhDoseNoEffectStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolPhDoseNoEffect");
        }

        // Non latchee : la disponibilite d'une sonde est un etat, pas un defaut
        // a acquitter. L'alarme retombe seule des que la mesure revient.
        // (La limite historique de 8 slots qui motivait aussi ce choix a disparu
        // avec le champ packe : chaque alarme a desormais son propre bouton.)
        // onDelay de 60 s en plus de la fenetre de fraicheur de 10 min de la
        // condition : une lecture 1-Wire qui rate ponctuellement, ou un premier
        // demarrage avant la premiere conversion, n'alarment pas.
        const AlarmRegistration waterTempUnavailableAlarm{
            AlarmId::PoolWaterTemperatureUnavailable,
            AlarmSeverity::Warning,
            false,
            60000,
            5000,
            600000,
            "water_temp_unavailable",
            "Water temperature unavailable",
            "poollogic"
        };
        if (!alarmSvc_->registerAlarm(alarmSvc_->ctx, &waterTempUnavailableAlarm, &PoolLogicModule::condWaterTempUnavailableStatic_, this)) {
            LOGW("PoolLogic failed to register AlarmId::PoolWaterTemperatureUnavailable");
        }
    } else {
        LOGW("PoolLogic running without alarm service");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolLogicModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolLogicModule::onEventStatic_, this);
    }

    if (!enabled_) {
        LOGI("PoolLogic disabled");
        return;
    }

    LOGI("PoolLogic ready");
    (void)cfgStore_;
}

void PoolLogicModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kPoolLogicCfgProducerId,
                               kPoolLogicCfgRoutes,
                               (uint8_t)(sizeof(kPoolLogicCfgRoutes) / sizeof(kPoolLogicCfgRoutes[0])),
                               services);
    }

    startupActivityPending_ = true;
    startupActivitySinceMs_ = millis();

    // Reamorce la copie de travail de la FSM avec le gain appris relu en NVS,
    // pour que la calibration reprenne ou elle s'etait arretee.
    resetPhDosingState_(startupActivitySinceMs_);

    if (!enabled_) return;

    if (disinfectionType_ > DisinfectionActiveOxygen) {
        disinfectionType_ = DisinfectionDisabled;
        if (cfgStore_) (void)cfgStore_->set(disinfectionTypeVar_, disinfectionType_);
    }
    if (swgControlMode_ > SwgControlContinuous) {
        swgControlMode_ = SwgControlContinuous;
        if (cfgStore_) (void)cfgStore_->set(swgControlModeVar_, swgControlMode_);
    }
    if (!std::isfinite(poolVolumeM3_) || poolVolumeM3_ <= 0.0f) {
        poolVolumeM3_ = PoolDefaults::PoolVolumeM3;
        if (cfgStore_) (void)cfgStore_->set(poolVolumeVar_, poolVolumeM3_);
    }
    if (!std::isfinite(o2DoseMlPer10M3Week_) || o2DoseMlPer10M3Week_ <= 0.0f) {
        o2DoseMlPer10M3Week_ = 500.0f;
        if (cfgStore_) (void)cfgStore_->set(o2DoseVar_, o2DoseMlPer10M3Week_);
    }
    if (o2MainHour_ > 23U) {
        o2MainHour_ = 20U;
        if (cfgStore_) (void)cfgStore_->set(o2MainHourVar_, o2MainHour_);
    }
    if (o2SplitCount_ == 0U || o2SplitCount_ > 3U) {
        o2SplitCount_ = 2U;
        if (cfgStore_) (void)cfgStore_->set(o2SplitCountVar_, o2SplitCount_);
    }
    if (!std::isfinite(o2LoadFactor_) || o2LoadFactor_ <= 0.0f) {
        o2LoadFactor_ = 1.0f;
        if (cfgStore_) (void)cfgStore_->set(o2LoadFactorVar_, o2LoadFactor_);
    }
    if (o2ProtocolState_ > O2ProtocolBlocked) {
        o2ProtocolState_ = O2ProtocolIdle;
        if (cfgStore_) (void)cfgStore_->set(o2ProtocolStateVar_, o2ProtocolState_);
    }
    if (!std::isfinite(o2WeeklyDoneMl_) || o2WeeklyDoneMl_ < 0.0f) {
        o2WeeklyDoneMl_ = 0.0f;
        if (cfgStore_) (void)cfgStore_->set(o2WeeklyDoneVar_, o2WeeklyDoneMl_);
    }
    if (!std::isfinite(o2PendingMl_) || o2PendingMl_ < 0.0f) {
        o2PendingMl_ = 0.0f;
        if (cfgStore_) (void)cfgStore_->set(o2PendingVar_, o2PendingMl_);
    }
    if (sensorHoldSettleSec_ > kSensorHoldSettleMaxSec) {
        LOGW("PoolLogic sensor hold settle %us > %us (heater probe window), clamped",
             (unsigned)sensorHoldSettleSec_,
             (unsigned)kSensorHoldSettleMaxSec);
        sensorHoldSettleSec_ = kSensorHoldSettleMaxSec;
        if (cfgStore_) (void)cfgStore_->set(sensorHoldSettleVar_, sensorHoldSettleSec_);
    }

    LOGI("PoolLogic pH dosing mode=%s", phDosePlus_ ? "pH+" : "pH-");
    LOGI("PoolLogic disinfection=%s swg_control=%s",
         disinfectionTypeStr_(disinfectionType_),
         swgControlModeStr_(swgControlMode_));
    updateDisinfectionDeviceSlot_();
    logDeviceSlotConfig_();

    // Masquage Home Assistant selon le type de desinfection et la presence des
    // equipements optionnels : les entites non pertinentes sont marquees absentes
    // (tombstone au boot). Cote arbre web, l'attribut visible_if des cfgdocs fait
    // le meme filtrage. Reconfiguration prise en compte au prochain redemarrage
    // (discovery one-shot).
    const HAService* haCfgSvc = services.get<HAService>(ServiceId::Ha);
    if (haCfgSvc && haCfgSvc->setEntityAbsent) {
        auto setAbs = [&](const char* suffix, bool absent) {
            (void)haCfgSvc->setEntityAbsent(haCfgSvc->ctx, "poollogic", suffix, absent);
        };
        const bool notChlorine = (disinfectionType_ != DisinfectionChlorineBromine);
        const bool notSwg = (disinfectionType_ != DisinfectionSwg);
        const bool notO2 = (disinfectionType_ != DisinfectionActiveOxygen);

        auto deviceEnabled = [&](uint8_t slot) -> bool {
            if (!poolSvc_ || !poolSvc_->meta) return true;
            PoolDeviceSvcMeta m{};
            if (poolSvc_->meta(poolSvc_->ctx, slot, &m) != POOLDEV_SVC_OK) return true;
            return m.used && m.enabled;
        };
        const bool heaterOff = !deviceEnabled(heaterDeviceSlot_);
        const bool fillOff = !deviceEnabled(fillingDeviceSlot_);

        // Chlore liquide (type 1) : PID ORP + fenetre.
        setAbs("pl_dis_auto", notChlorine);
        setAbs("pl_dis_window", notChlorine);
        // Consigne ORP partagee chlore liquide (1) et electrolyse-ORP (2).
        setAbs("pl_dis_setpoint", notChlorine && notSwg);
        // Electrolyse (type 2).
        setAbs("pl_swg_ctrl", notSwg);
        setAbs("pl_swg_dly_elec", notSwg);
        setAbs("pl_swg_min_temp", notSwg);
        // Oxygene actif (type 3).
        // pl_pool_vol absent de cette liste : le volume du bassin est un parametre
        // general (filtration + doses O2), il reste expose dans tous les modes.
        static const char* const kO2Suffixes[] = {
            "pl_o2_temp_comp", "pl_o2_hour", "pl_o2_state", "pl_o2_done",
            "pl_o2_pending", "pl_o2_last_day", "pl_o2_block", "pl_o2_plan",
            "pl_o2_flow", "pl_o2_dose", "pl_o2_split",
            "pl_o2_load", "pl_o2_min_flt"
        };
        for (const char* suffix : kO2Suffixes) setAbs(suffix, notO2);
        // Equipements optionnels.
        setAbs("pl_heat_auto", heaterOff);
        setAbs("pl_heat_setpoint", heaterOff);
        setAbs("pl_has_rsn", heaterOff);
        setAbs("pl_refill_min_on", fillOff);
    }

    ensureDailySlot_();

    if (schedSvc_ && schedSvc_->isActive) {
        bool active = false;
        for (uint8_t i = 0; i < FILTRATION_PLAN_MAX_WINDOWS; ++i) {
            if (schedSvc_->isActive(schedSvc_->ctx, (uint8_t)(SLOT_FILTR_WINDOW_BASE + i))) {
                active = true;
                break;
            }
        }
        filtrationWindowActive_ = active;
    }

    // Trigger one recompute on startup, after persisted config and scheduler
    // blob are fully loaded, so the window reflects the final restored state.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);
}

bool PoolLogicModule::activityTimeReady_() const
{
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return false;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return false;
    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    return epoch >= kActivityMinEpoch;
}

void PoolLogicModule::emitStartupActivityIfReady_(uint32_t nowMs)
{
    if (!startupActivityPending_) return;

    const uint32_t elapsedMs = (uint32_t)(nowMs - startupActivitySinceMs_);
    if (elapsedMs < kStartupActivityStabilizeMs) return;

    const bool timeReady = activityTimeReady_();
    if (!timeReady && elapsedMs < kStartupActivityMaxDelayMs) return;

    if (enabled_) {
        emitActivity_(ActivityCode::PoolLogicReady,
                      ActivitySource::System,
                      ActivitySeverity::Success,
                      ActivityRole::None,
                      ActivityState::None,
                      ActivityReason::None,
                      ACTIVITY_TARGET_NONE,
                      "PoolLogic est prêt",
                      "Les automatismes piscine sont initialisés.",
                      "pool");
    } else {
        emitActivity_(ActivityCode::PoolLogicDisabled,
                      ActivitySource::System,
                      ActivitySeverity::Info,
                      ActivityRole::None,
                      ActivityState::None,
                      ActivityReason::None,
                      ACTIVITY_TARGET_NONE,
                      "PoolLogic est désactivé",
                      "Les automatismes piscine ne pilotent pas les équipements.",
                      "toggle_off");
    }
    startupActivityPending_ = false;
}

void PoolLogicModule::loop()
{
    const uint32_t nowMs = millis();
    emitStartupActivityIfReady_(nowMs);

    if (!enabled_) {
        if (!bootControlReady_) {
            if (setPoolDeviceWritesEnabled_(true)) {
                bootControlReady_ = true;
                LOGI("PoolLogic disabled: pool device writes enabled after adoption");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    // The loop only consumes latched scheduler events; actual work stays in the
    // task context to avoid doing heavy operations from event callbacks.
    bool doRecalc = false;
    bool doDayReset = false;
    portENTER_CRITICAL(&pendingMux_);
    doRecalc = pendingDailyRecalc_;
    pendingDailyRecalc_ = false;
    doDayReset = pendingDayReset_;
    pendingDayReset_ = false;
    portEXIT_CRITICAL(&pendingMux_);

    if (doRecalc) {
        (void)recalcAndApplyFiltrationWindow_();
    }

    if (doDayReset) {
        cleaningDone_ = false;
        LOGI("Daily reset: cleaning_done=false");
    }

    if (!bootControlReady_) {
        adoptBootDeviceState_(nowMs);
        if (setPoolDeviceWritesEnabled_(true)) {
            bootControlReady_ = true;
            LOGI("PoolLogic boot adoption complete; pool device writes enabled");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    runControlLoop_(nowMs);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void PoolLogicModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolLogicModule*>(user)->onEvent_(e);
}

void PoolLogicModule::onEvent_(const Event& e)
{
    if (!enabled_) return;

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        // Les fenetres vivent dans leur propre sous-branche : sans elle ici, une
        // modification de plage n'aurait replanifie qu'au recalcul quotidien.
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            (p->localBranchId == kCfgBranchFiltration ||
             p->localBranchId == kCfgBranchFiltrationWindows)) {
            // Les cles calculees sont des sorties pures du plan : les re-appliquer
            // ici ecraserait les segments multi-fenetres (et bouclerait avec le
            // recalcul qui les ecrit).
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStart) != 0 &&
                strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrationCalcStop) != 0 &&
                strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrSegments) != 0 &&
                strcmp(p->nvsKey, NvsKeys::PoolLogic::FiltrOptimalMin) != 0) {
                portENTER_CRITICAL(&pendingMux_);
                pendingDailyRecalc_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchBassin &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::PoolVolumeM3) == 0) {
                // Le volume conditionne le besoin de filtration (turnover).
                portENTER_CRITICAL(&pendingMux_);
                pendingDailyRecalc_ = true;
                portEXIT_CRITICAL(&pendingMux_);
                // Il conditionne aussi la dose : le gain appris sur l'ancien
                // volume n'a plus de sens.
                resetPhLearnedGain_("pool_volume_m3 changed");
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::AutoMode) == 0 && autoMode_) {
                portENTER_CRITICAL(&pendingMux_);
                pendingFiltrationReconcile_ = true;
                portEXIT_CRITICAL(&pendingMux_);
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::DisinfectionType) == 0) {
                if (disinfectionType_ > DisinfectionActiveOxygen) disinfectionType_ = DisinfectionDisabled;
                // L'ancienne pompe est arretee avant la bascule : apres
                // updateDisinfectionDeviceSlot_ elle n'aurait plus de pilote et
                // resterait en marche.
                (void)forceDeviceStop_(orpPumpDeviceSlot_);
                (void)forceDeviceStop_(swgDeviceSlot_);
                updateDisinfectionDeviceSlot_();
                (void)forceDeviceStop_(orpPumpDeviceSlot_);
                if (disinfectionType_ == DisinfectionChlorineBromine && !orpAutoMode_ && cfgStore_) {
                    (void)cfgStore_->set(orpAutoModeVar_, true);
                    orpAutoMode_ = true;
                }
                // Resolution one-shot de la strategie : hors oxygene actif, le
                // protocole O2 n'est plus evalue par la boucle, on le remet donc
                // au repos ici (remplace le reset qui etait fait a chaque tour).
                if (disinfectionType_ != DisinfectionActiveOxygen) {
                    o2PendingMl_ = 0.0f;
                    o2LastProgressMs_ = 0;
                    o2ProtocolState_ = O2ProtocolIdle;
                    o2BlockReason_ = O2BlockInactive;
                    persistO2Protocol_(millis(), true);
                }
                resetTemporalPidState_(orpPidState_, millis());
                orpPidEnabled_ = false;
                LOGI("PoolLogic disinfection changed: %s", disinfectionTypeStr_(disinfectionType_));
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchPh &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::PhAutoMode) == 0 && phAutoMode_) {
                // Global business rule: entering pH auto starts from a safe stopped pump.
                if (!forceDeviceStop_(phPumpDeviceSlot_)) {
                    LOGW("PoolLogic failed to stop pH pump on ph_auto_mode enable (slot=%u)",
                         (unsigned)phPumpDeviceSlot_);
                }
                // Repartir d'une FSM propre efface aussi le latch "dosage sans
                // effet" : rebasculer le mode auto est la voie de deblocage.
                resetPhDosingState_(millis());
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::PhDoseMlPerM3) == 0) {
                // Le gain appris est reference au gain configure : changer la
                // reference invalide l'apprentissage precedent.
                resetPhLearnedGain_("ph_dose_ml_m3 changed");
            }
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchDisinfection &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::DisAutoMode) == 0 && orpAutoMode_) {
                // Global business rule: entering disinfection auto starts from a safe stopped pump.
                if (!forceDeviceStop_(orpPumpDeviceSlot_)) {
                    LOGW("PoolLogic failed to stop disinfection pump on dis_auto_mode enable (slot=%u)",
                         (unsigned)orpPumpDeviceSlot_);
                }
                resetTemporalPidState_(orpPidState_, millis());
            } else if (strcmp(p->nvsKey, NvsKeys::PoolLogic::SwgControlMode) == 0) {
                if (swgControlMode_ > SwgControlContinuous) swgControlMode_ = SwgControlContinuous;
                (void)forceDeviceStop_(swgDeviceSlot_);
                LOGI("PoolLogic SWG control changed: %s", swgControlModeStr_(swgControlMode_));
            }
            return;
        }
        // Le gel des mesures suit le role metier : un capteur rebinde sur un
        // autre slot doit emporter son gel avec lui, sans redemarrage.
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            (p->localBranchId == kCfgBranchSensors || p->localBranchId == kCfgBranchSafety)) {
            if (sensorHoldSettleSec_ > kSensorHoldSettleMaxSec) {
                LOGW("PoolLogic sensor hold settle %us > %us (heater probe window), clamped",
                     (unsigned)sensorHoldSettleSec_,
                     (unsigned)kSensorHoldSettleMaxSec);
                sensorHoldSettleSec_ = kSensorHoldSettleMaxSec;
                if (cfgStore_) (void)cfgStore_->set(sensorHoldSettleVar_, sensorHoldSettleSec_);
            }
            portENTER_CRITICAL(&pendingMux_);
            sensorHoldPending_ = true;
            portEXIT_CRITICAL(&pendingMux_);
            return;
        }
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic &&
            p->localBranchId == kCfgBranchHeater &&
            p->nvsKey) {
            if (strcmp(p->nvsKey, NvsKeys::PoolLogic::HeaterAutoMode) == 0 && heaterAutoMode_) {
                // Entering heater auto starts from a safe stopped heater relay.
                if (!forceDeviceStop_(heaterDeviceSlot_)) {
                    LOGW("PoolLogic failed to stop heater on heater_auto_mode enable (slot=%u)",
                         (unsigned)heaterDeviceSlot_);
                }
            }
            return;
        }
        return;
    }

    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    const SchedulerEdge edge = (SchedulerEdge)p->edge;

    if (p->eventId == POOLLOGIC_EVENT_DAILY_RECALC && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDailyRecalc_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    if (p->eventId == TIME_EVENT_SYS_DAY_START && edge == SchedulerEdge::Trigger) {
        portENTER_CRITICAL(&pendingMux_);
        pendingDayReset_ = true;
        portEXIT_CRITICAL(&pendingMux_);
        return;
    }

    // Scheduler edges only latch intent/state; the loop owns the control work.
    // Avec plusieurs segments, un front Stop peut coincider avec le Start du
    // suivant : on reevalue le plan complet plutot que de suivre le dernier front.
    if (p->eventId == POOLLOGIC_EVENT_FILTRATION_WINDOW) {
        FiltrationPlanOutput planCopy{};
        portENTER_CRITICAL(&pendingMux_);
        planCopy = filtrationPlan_;
        portEXIT_CRITICAL(&pendingMux_);

        bool active = (edge == SchedulerEdge::Start);
        if (planCopy.segmentCount > 0) {
            bool planActive = false;
            if (currentFiltrationPlanActive_(planCopy, planActive)) active = planActive;
        }
        portENTER_CRITICAL(&pendingMux_);
        filtrationWindowActive_ = active;
        portEXIT_CRITICAL(&pendingMux_);
    }
}

/**
 * La pompe de desinfection n'est pas la meme selon le mode : chlore liquide /
 * brome et oxygene actif sont deux appareils, avec leur propre debit, leur
 * propre bidon et leurs propres compteurs de consommation. Le slot est donc
 * derive de disinfection_type et non configure.
 */
void PoolLogicModule::updateDisinfectionDeviceSlot_()
{
    const uint8_t previous = orpPumpDeviceSlot_;
    orpPumpDeviceSlot_ = (disinfectionType_ == DisinfectionActiveOxygen)
                             ? (uint8_t)PoolIds::DeviceO2Pump
                             : (uint8_t)PoolIds::DeviceChlorinePump;
    if (previous != orpPumpDeviceSlot_) {
        LOGI("PoolLogic disinfection pump slot=%u (type=%s)",
             (unsigned)orpPumpDeviceSlot_,
             disinfectionTypeStr_(disinfectionType_));
    }
}

void PoolLogicModule::logDeviceSlotConfig_() const
{
    LOGI("PoolLogic slots filtr=%u swg=%u robot=%u fill=%u ph=%u dis=%u heater=%u",
         (unsigned)filtrationDeviceSlot_,
         (unsigned)swgDeviceSlot_,
         (unsigned)robotDeviceSlot_,
         (unsigned)fillingDeviceSlot_,
         (unsigned)phPumpDeviceSlot_,
         (unsigned)orpPumpDeviceSlot_,
         (unsigned)heaterDeviceSlot_);

    logDeviceSlotBinding_("filtration", filtrationDeviceSlot_, 0);
    logDeviceSlotBinding_("swg", swgDeviceSlot_, -1);
    logDeviceSlotBinding_("robot", robotDeviceSlot_, -1);
    logDeviceSlotBinding_("filling", fillingDeviceSlot_, -1);
    logDeviceSlotBinding_("ph_pump", phPumpDeviceSlot_, 1);
    logDeviceSlotBinding_("dis_pump", orpPumpDeviceSlot_, 1);
    logDeviceSlotBinding_("heater", heaterDeviceSlot_, -1);
}

void PoolLogicModule::logDeviceSlotBinding_(const char* role, uint8_t slot, int8_t expectedType) const
{
    if (!poolSvc_ || !poolSvc_->meta) {
        LOGW("PoolLogic role=%s slot=%u PDM meta unavailable", role ? role : "?", (unsigned)slot);
        return;
    }

    PoolDeviceSvcMeta meta{};
    const PoolDeviceSvcStatus st = poolSvc_->meta(poolSvc_->ctx, slot, &meta);
    if (st != POOLDEV_SVC_OK) {
        LOGW("PoolLogic role=%s slot=%u PDM meta failed st=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)st);
        return;
    }

    LOGI("PoolLogic role=%s slot=%u pdm used=%u type=%u io=%u label=%s",
         role ? role : "?",
         (unsigned)slot,
         (unsigned)meta.used,
         (unsigned)meta.type,
         (unsigned)meta.ioId,
         meta.label[0] ? meta.label : "?");

    if (expectedType >= 0 && meta.type != (uint8_t)expectedType) {
        LOGW("PoolLogic role=%s slot=%u type mismatch expected=%u got=%u",
             role ? role : "?",
             (unsigned)slot,
             (unsigned)expectedType,
             (unsigned)meta.type);
    }
}
