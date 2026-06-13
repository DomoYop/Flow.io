/**
 * @file PoolDeviceModule.cpp
 * @brief Facade translation unit for PoolDeviceModule.
 *
 * Architecture: PoolDeviceModule keeps a single public facade and splits its
 * implementation across Lifecycle / Control / Runtime / Commands translation
 * units.
 */

#include "PoolDeviceModule.h"

#if 0
// Config-doc generation compatibility anchor:
// the generator keys dynamic jsonName/moduleName aliases by translation-unit stem.
static void poolDeviceCfgDocsAnchor_(PoolDeviceModule& self)
{
    self.cfgEnabledVar_[0].jsonName = "enabled";
    self.cfgDependsVar_[0].jsonName = "depends_on_mask";
    self.cfgFlowVar_[0].jsonName = "flow_l_h";
    self.cfgTankCapVar_[0].jsonName = "tank_cap_ml";
    self.cfgTankInitVar_[0].jsonName = "tank_init_ml";
    self.cfgMaxUptimeVar_[0].jsonName = "max_uptime_day_s";

    self.cfgEnabledVar_[0].moduleName = "pdm/pd0";
    self.cfgEnabledVar_[1].moduleName = "pdm/pd1";
    self.cfgEnabledVar_[2].moduleName = "pdm/pd2";
    self.cfgEnabledVar_[3].moduleName = "pdm/pd3";
    self.cfgEnabledVar_[4].moduleName = "pdm/pd4";
    self.cfgEnabledVar_[5].moduleName = "pdm/pd5";
    self.cfgEnabledVar_[6].moduleName = "pdm/pd6";
    self.cfgEnabledVar_[7].moduleName = "pdm/pd7";

    self.cfgDependsVar_[0].moduleName = "pdm/pd0";
    self.cfgDependsVar_[1].moduleName = "pdm/pd1";
    self.cfgDependsVar_[2].moduleName = "pdm/pd2";
    self.cfgDependsVar_[3].moduleName = "pdm/pd3";
    self.cfgDependsVar_[4].moduleName = "pdm/pd4";
    self.cfgDependsVar_[5].moduleName = "pdm/pd5";
    self.cfgDependsVar_[6].moduleName = "pdm/pd6";
    self.cfgDependsVar_[7].moduleName = "pdm/pd7";

    self.cfgFlowVar_[0].moduleName = "pdm/pd0";
    self.cfgFlowVar_[1].moduleName = "pdm/pd1";
    self.cfgFlowVar_[2].moduleName = "pdm/pd2";
    self.cfgFlowVar_[3].moduleName = "pdm/pd3";
    self.cfgFlowVar_[4].moduleName = "pdm/pd4";
    self.cfgFlowVar_[5].moduleName = "pdm/pd5";
    self.cfgFlowVar_[6].moduleName = "pdm/pd6";
    self.cfgFlowVar_[7].moduleName = "pdm/pd7";

    self.cfgTankCapVar_[0].moduleName = "pdm/pd0";
    self.cfgTankCapVar_[1].moduleName = "pdm/pd1";
    self.cfgTankCapVar_[2].moduleName = "pdm/pd2";
    self.cfgTankCapVar_[3].moduleName = "pdm/pd3";
    self.cfgTankCapVar_[4].moduleName = "pdm/pd4";
    self.cfgTankCapVar_[5].moduleName = "pdm/pd5";
    self.cfgTankCapVar_[6].moduleName = "pdm/pd6";
    self.cfgTankCapVar_[7].moduleName = "pdm/pd7";

    self.cfgTankInitVar_[0].moduleName = "pdm/pd0";
    self.cfgTankInitVar_[1].moduleName = "pdm/pd1";
    self.cfgTankInitVar_[2].moduleName = "pdm/pd2";
    self.cfgTankInitVar_[3].moduleName = "pdm/pd3";
    self.cfgTankInitVar_[4].moduleName = "pdm/pd4";
    self.cfgTankInitVar_[5].moduleName = "pdm/pd5";
    self.cfgTankInitVar_[6].moduleName = "pdm/pd6";
    self.cfgTankInitVar_[7].moduleName = "pdm/pd7";

    self.cfgMaxUptimeVar_[0].moduleName = "pdm/pd0";
    self.cfgMaxUptimeVar_[1].moduleName = "pdm/pd1";
    self.cfgMaxUptimeVar_[2].moduleName = "pdm/pd2";
    self.cfgMaxUptimeVar_[3].moduleName = "pdm/pd3";
    self.cfgMaxUptimeVar_[4].moduleName = "pdm/pd4";
    self.cfgMaxUptimeVar_[5].moduleName = "pdm/pd5";
    self.cfgMaxUptimeVar_[6].moduleName = "pdm/pd6";
    self.cfgMaxUptimeVar_[7].moduleName = "pdm/pd7";

}
#endif
