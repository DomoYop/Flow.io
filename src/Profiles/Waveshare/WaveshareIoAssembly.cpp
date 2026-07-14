#include "Profiles/Waveshare/WaveshareIoAssembly.h"
#include "Profiles/Waveshare/WaveshareIoLayout.h"

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
#include "Profiles/Waveshare/WaveshareProfile.h"

namespace {

using Profiles::Waveshare::ModuleInstances;
namespace FlowIoLayout = Profiles::Waveshare::IoLayout;

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}

constexpr PoolIoExtraEndpoint kExtraDigitalInputs[] = {
    {(IoId)(IO_ID_DI_BASE + 0), FlowIoLayout::PortDin0},
    {(IoId)(IO_ID_DI_BASE + 1), FlowIoLayout::PortDin1},
    {(IoId)(IO_ID_DI_BASE + 2), FlowIoLayout::PortDin2},
    {(IoId)(IO_ID_DI_BASE + 3), FlowIoLayout::PortDin3},
    {(IoId)(IO_ID_DI_BASE + 4), FlowIoLayout::PortDin4},
    {(IoId)(IO_ID_DI_BASE + 5), FlowIoLayout::PortDin5},
    {(IoId)(IO_ID_DI_BASE + 6), FlowIoLayout::PortDin6},
    {(IoId)(IO_ID_DI_BASE + 7), FlowIoLayout::PortDin7},
};

#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
constexpr PoolIoExtraEndpoint kExtraDigitalOutputs[] = {
    {(IoId)(IO_ID_DO_BASE + 8), FlowIoLayout::PortMcpOut1},
    {(IoId)(IO_ID_DO_BASE + 9), FlowIoLayout::PortMcpOut2},
    {(IoId)(IO_ID_DO_BASE + 10), FlowIoLayout::PortMcpOut3},
    {(IoId)(IO_ID_DO_BASE + 11), FlowIoLayout::PortMcpOut4},
    {(IoId)(IO_ID_DO_BASE + 12), FlowIoLayout::PortMcpOut5},
    {(IoId)(IO_ID_DO_BASE + 13), FlowIoLayout::PortMcpOut6},
    {(IoId)(IO_ID_DO_BASE + 14), FlowIoLayout::PortMcpOut7},
    {(IoId)(IO_ID_DO_BASE + 15), FlowIoLayout::PortMcpOut8},
};
#endif

constexpr PoolIoProfileSpec kIoProfileSpec{
    FlowIoLayout::kBindingPorts,
    (uint8_t)(sizeof(FlowIoLayout::kBindingPorts) / sizeof(FlowIoLayout::kBindingPorts[0])),
    FlowIoLayout::kAnalogRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kAnalogRoleDefaults) / sizeof(FlowIoLayout::kAnalogRoleDefaults[0])),
    FlowIoLayout::kDigitalInputRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kDigitalInputRoleDefaults) / sizeof(FlowIoLayout::kDigitalInputRoleDefaults[0])),
    FlowIoLayout::kDigitalOutputRoleDefaults,
    (uint8_t)(sizeof(FlowIoLayout::kDigitalOutputRoleDefaults) / sizeof(FlowIoLayout::kDigitalOutputRoleDefaults[0])),
    kExtraDigitalInputs,
    (uint8_t)(sizeof(kExtraDigitalInputs) / sizeof(kExtraDigitalInputs[0])),
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    kExtraDigitalOutputs,
    (uint8_t)(sizeof(kExtraDigitalOutputs) / sizeof(kExtraDigitalOutputs[0])),
#else
    nullptr,
    0,
#endif
};

PoolIoHaContext haContext(ModuleInstances& modules, const AppContext* ctx)
{
    PoolIoHaContext haCtx{};
    haCtx.io = &modules.ioModule;
    haCtx.ha = modules.haService;
    haCtx.dataStore = modules.ioDataStore;
    haCtx.domain = ctx ? ctx->domain : &PoolDomain::kPoolDomain;
    return haCtx;
}

}  // namespace

namespace Profiles {
namespace Waveshare {

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

}  // namespace Waveshare
}  // namespace Profiles
