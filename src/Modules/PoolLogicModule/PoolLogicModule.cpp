/**
 * @file PoolLogicModule.cpp
 * @brief Facade translation unit for PoolLogicModule.
 *
 * Architecture: PoolLogicModule keeps a single public facade and splits its
 * implementation across Lifecycle / Scheduler / Control / Runtime / Commands
 * translation units.
 */

#include "PoolLogicModule.h"

#if 0
// Config-doc generation compatibility anchor:
// the generator keys runtime moduleName aliases by translation-unit stem.
namespace {
static constexpr const char* kCfgModuleBassin = "poollogic/bassin";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleDisinfection = "poollogic/disinfection";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";
}

static void poolLogicCfgDocsAnchor_(PoolLogicModule& self)
{
    self.enabledVar_.moduleName = kCfgModuleBassin;
    self.autoModeVar_.moduleName = kCfgModuleBassin;
    self.winterModeVar_.moduleName = kCfgModuleBassin;
    self.phAutoModeVar_.moduleName = kCfgModulePh;
    self.orpAutoModeVar_.moduleName = kCfgModuleDisinfection;
    self.heaterAutoModeVar_.moduleName = kCfgModuleHeater;
    self.phDosePlusVar_.moduleName = kCfgModulePh;
    self.disinfectionTypeVar_.moduleName = kCfgModuleBassin;
    self.poolVolumeVar_.moduleName = kCfgModuleBassin;
    self.pumpFlowVar_.moduleName = kCfgModuleFiltration;
    self.filtrCycleRatioVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin1EnVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin1StartVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin1StopVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin1PrioVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin2EnVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin2StartVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin2StopVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin2PrioVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin3EnVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin3StartVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin3StopVar_.moduleName = kCfgModuleFiltration;
    self.filtrWin3PrioVar_.moduleName = kCfgModuleFiltration;
    self.calcStartVar_.moduleName = kCfgModuleFiltration;
    self.calcStopVar_.moduleName = kCfgModuleFiltration;
    self.filtrSegmentsVar_.moduleName = kCfgModuleFiltration;
    self.filtrOptimalVar_.moduleName = kCfgModuleFiltration;

    self.phIdVar_.moduleName = kCfgModuleSensors;
    self.orpIdVar_.moduleName = kCfgModuleSensors;
    self.pressureIdVar_.moduleName = kCfgModuleSensors;
    self.waterTempIdVar_.moduleName = kCfgModuleSensors;
    self.airTempIdVar_.moduleName = kCfgModuleSensors;
    self.levelIdVar_.moduleName = kCfgModuleSensors;
    self.phLevelIdVar_.moduleName = kCfgModuleSensors;
    self.chlorineLevelIdVar_.moduleName = kCfgModuleSensors;
    self.flowSwitchIdVar_.moduleName = kCfgModuleSensors;
    self.coverClosedIdVar_.moduleName = kCfgModuleSensors;

    self.pressureLowVar_.moduleName = kCfgModuleSafety;
    self.pressureHighVar_.moduleName = kCfgModuleSafety;
    self.winterStartVar_.moduleName = kCfgModuleSafety;
    self.freezeHoldVar_.moduleName = kCfgModuleSafety;
    self.secureElectroVar_.moduleName = kCfgModuleDisinfection;
    self.phSetpointVar_.moduleName = kCfgModulePh;
    self.phDoseMlPerM3Var_.moduleName = kCfgModulePh;
    self.phDeadbandVar_.moduleName = kCfgModulePh;
    self.phDoseFactorVar_.moduleName = kCfgModulePh;
    self.phDoseMaxBatchVar_.moduleName = kCfgModulePh;
    self.phDoseMaxDayVar_.moduleName = kCfgModulePh;
    self.phValidMinVar_.moduleName = kCfgModulePh;
    self.phValidMaxVar_.moduleName = kCfgModulePh;
    self.phNoEffectDeltaVar_.moduleName = kCfgModulePh;
    self.phGainLearnedVar_.moduleName = kCfgModulePh;
    self.phMixWaitMinVar_.moduleName = kCfgModulePh;
    self.phSampleMaxAgeVar_.moduleName = kCfgModulePh;
    self.phNoEffectLotsVar_.moduleName = kCfgModulePh;
    self.phGainSamplesVar_.moduleName = kCfgModulePh;
    self.phLastDoseTsVar_.moduleName = kCfgModulePh;
    self.orpSetpointVar_.moduleName = kCfgModuleDisinfection;
    self.heaterSetpointVar_.moduleName = kCfgModuleHeater;
    self.orpKpVar_.moduleName = kCfgModuleDisinfection;
    self.orpKiVar_.moduleName = kCfgModuleDisinfection;
    self.orpKdVar_.moduleName = kCfgModuleDisinfection;
    self.orpWindowMsVar_.moduleName = kCfgModuleDisinfection;
    self.disMinOnMsVar_.moduleName = kCfgModuleDisinfection;
    self.disSampleMsVar_.moduleName = kCfgModuleDisinfection;

    self.pressureDelayVar_.moduleName = kCfgModuleSafety;
    self.delayPidsVar_.moduleName = kCfgModuleBassin;
    self.delayElectroVar_.moduleName = kCfgModuleDisinfection;
    self.robotDelayVar_.moduleName = kCfgModuleRobot;
    self.robotDurationVar_.moduleName = kCfgModuleRobot;
    self.fillingMinOnVar_.moduleName = kCfgModuleRefill;

    self.filtrationDeviceVar_.moduleName = kCfgModuleFiltration;
    self.swgDeviceVar_.moduleName = kCfgModuleDisinfection;
    self.robotDeviceVar_.moduleName = kCfgModuleRobot;
    self.fillingDeviceVar_.moduleName = kCfgModuleRefill;
    self.phPumpDeviceVar_.moduleName = kCfgModulePh;
    self.orpPumpDeviceVar_.moduleName = kCfgModuleDisinfection;
    self.heaterDeviceVar_.moduleName = kCfgModuleHeater;
}
#endif
