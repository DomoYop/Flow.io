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
static constexpr const char* kCfgModuleModes = "poollogic/modes";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
static constexpr const char* kCfgModuleRegulation = "poollogic/regulation";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleChlorine = "poollogic/chlorine";
static constexpr const char* kCfgModuleSwg = "poollogic/swg";
static constexpr const char* kCfgModuleO2 = "poollogic/o2";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";
}

static void poolLogicCfgDocsAnchor_(PoolLogicModule& self)
{
    self.enabledVar_.moduleName = kCfgModuleModes;
    self.autoModeVar_.moduleName = kCfgModuleModes;
    self.winterModeVar_.moduleName = kCfgModuleModes;
    self.phAutoModeVar_.moduleName = kCfgModulePh;
    self.orpAutoModeVar_.moduleName = kCfgModuleChlorine;
    self.heaterAutoModeVar_.moduleName = kCfgModuleHeater;
    self.phDosePlusVar_.moduleName = kCfgModulePh;
    self.disinfectionTypeVar_.moduleName = kCfgModuleModes;
    self.pumpFlowVar_.moduleName = kCfgModuleFiltration;
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
    self.secureElectroVar_.moduleName = kCfgModuleSwg;
    self.phSetpointVar_.moduleName = kCfgModulePh;
    self.orpSetpointVar_.moduleName = kCfgModuleChlorine;
    self.heaterSetpointVar_.moduleName = kCfgModuleHeater;
    self.phKpVar_.moduleName = kCfgModulePh;
    self.phKiVar_.moduleName = kCfgModulePh;
    self.phKdVar_.moduleName = kCfgModulePh;
    self.orpKpVar_.moduleName = kCfgModuleChlorine;
    self.orpKiVar_.moduleName = kCfgModuleChlorine;
    self.orpKdVar_.moduleName = kCfgModuleChlorine;
    self.phWindowMsVar_.moduleName = kCfgModulePh;
    self.orpWindowMsVar_.moduleName = kCfgModuleChlorine;
    self.pidMinOnMsVar_.moduleName = kCfgModuleRegulation;
    self.pidSampleMsVar_.moduleName = kCfgModuleRegulation;

    self.pressureDelayVar_.moduleName = kCfgModuleSafety;
    self.delayPidsVar_.moduleName = kCfgModuleRegulation;
    self.delayElectroVar_.moduleName = kCfgModuleSwg;
    self.robotDelayVar_.moduleName = kCfgModuleRobot;
    self.robotDurationVar_.moduleName = kCfgModuleRobot;
    self.fillingMinOnVar_.moduleName = kCfgModuleRefill;

    self.filtrationDeviceVar_.moduleName = kCfgModuleFiltration;
    self.swgDeviceVar_.moduleName = kCfgModuleSwg;
    self.robotDeviceVar_.moduleName = kCfgModuleRobot;
    self.fillingDeviceVar_.moduleName = kCfgModuleRefill;
    self.phPumpDeviceVar_.moduleName = kCfgModulePh;
    self.orpPumpDeviceVar_.moduleName = kCfgModuleChlorine;
    self.heaterDeviceVar_.moduleName = kCfgModuleHeater;
}
#endif
