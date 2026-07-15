#include "Profiles/FlowIO/FlowIOIoAssembly.h"
#include "Profiles/FlowIO/FlowIOIoLayout.h"

#include <Arduino.h>
#include <stdint.h>

#include "App/AppContext.h"
#include "Board/BoardSerialMap.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/Services/Services.h"
#include "Domain/Pool/PoolDomain.h"
#include "Domain/Pool/PoolIoAssembly.h"
#include "Domain/Pool/PoolIoHaDiscovery.h"
#include "Profiles/FlowIO/FlowIOProfile.h"

namespace {

using Profiles::FlowIO::ModuleInstances;
namespace FlowIoLayout = Profiles::FlowIO::IoLayout;

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}

constexpr PoolIoProfileSpec kIoProfileSpec{
    FlowIoLayout::kBindingPorts,
    (uint8_t)(sizeof(FlowIoLayout::kBindingPorts) / sizeof(FlowIoLayout::kBindingPorts[0])),
    FlowIoLayout::kAnalogRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kAnalogRoleDefaults) / sizeof(FlowIoLayout::kAnalogRoleDefaults[0])),
    FlowIoLayout::kDigitalInputRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kDigitalInputRoleDefaults) / sizeof(FlowIoLayout::kDigitalInputRoleDefaults[0])),
    FlowIoLayout::kDigitalOutputRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kDigitalOutputRoleDefaults) / sizeof(FlowIoLayout::kDigitalOutputRoleDefaults[0])),
    nullptr,
    0,
    nullptr,
    0,
};

PoolIoHaContext haContext(ModuleInstances& modules, const AppContext* ctx)
{
    PoolIoHaContext haCtx{};
    haCtx.io = &modules.ioModule;
    haCtx.ha = modules.haService;
    haCtx.dataStore = modules.ioDataStore;
    haCtx.domain = ctx ? ctx->domain : &PoolDomain::kPoolDomain;
    haCtx.poolDevice = &modules.poolDeviceModule;
    return haCtx;
}

}  // namespace

namespace Profiles {
namespace FlowIO {

void configureIoModule(const AppContext& ctx, ModuleInstances& modules)
{
    requireSetup(ctx.domain != nullptr, "missing domain spec");
    modules.ioModule.setOneWireBuses(&modules.oneWireWater, &modules.oneWireAir);
    requireSetup(PoolIo::configure(*ctx.domain, modules.ioModule, kIoProfileSpec), "pool io configure");
}

void registerIoHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    modules.haService = ctx.services.get<HAService>(ServiceId::Ha);
    if (!modules.haService) return;
    PoolIoHa::registerDiscovery(haContext(modules, &ctx));
}

void refreshIoHomeAssistantIfNeeded(ModuleInstances& modules)
{
    if (!modules.haService) return;
    PoolIoHa::refreshIfNeeded(haContext(modules, nullptr));
}

void releaseIoHomeAssistantDiscoveryHeapIfDone(ModuleInstances& modules)
{
    PoolIoHa::releaseDiscoveryHeapIfDone(haContext(modules, nullptr));
}

}  // namespace FlowIO
}  // namespace Profiles
