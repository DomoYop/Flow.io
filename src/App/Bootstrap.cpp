#include "App/Bootstrap.h"

#include "App/BuildFlags.h"
#include "Core/Services/IHmi.h"
#include "Core/Services/ILogger.h"

#ifndef FLOW_ENABLE_BOOT_LOG_CAPTURE
#define FLOW_ENABLE_BOOT_LOG_CAPTURE 0
#endif

#if FLOW_BUILD_IS_FLOWIO
#include "Profiles/FlowIO/FlowIOProfile.h"
#endif

#if FLOW_BUILD_IS_FLOWIOS3
#include "Profiles/FlowIOS3/FlowIOS3Profile.h"
#endif

#if FLOW_BUILD_IS_SUPERVISOR
#include "Profiles/Supervisor/SupervisorProfile.h"
#endif

#if FLOW_BUILD_IS_FLOW_CONNECT_DISPLAY
#include "Profiles/FlowConnectDisplay/FlowConnectDisplayProfile.h"
#endif

#if FLOW_BUILD_IS_MICRONOVA
#include "Profiles/Micronova/MicronovaProfile.h"
#endif

namespace {

AppContext gContext{};
bool gStarted = false;
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
bool gBootLogCaptureCompleteMarked = false;

bool bootLogCaptureWaitsForHaDiscovery()
{
    return gContext.services.has(ServiceId::Ha);
}
#endif

const FirmwareProfile& resolveProfile()
{
#if FLOW_BUILD_IS_FLOWIO
    return Profiles::FlowIO::profile();
#elif FLOW_BUILD_IS_FLOWIOS3
    return Profiles::FlowIOS3::profile();
#elif FLOW_BUILD_IS_SUPERVISOR
    return Profiles::Supervisor::profile();
#elif FLOW_BUILD_IS_FLOW_CONNECT_DISPLAY
    return Profiles::FlowConnectDisplay::profile();
#elif FLOW_BUILD_IS_MICRONOVA
    return Profiles::Micronova::profile();
#else
#error "Unsupported build profile."
#endif
}

}  // namespace

namespace Bootstrap {

#if FLOW_ENABLE_BOOT_LOG_CAPTURE
void markBootLogCaptureCompleteIfReady()
{
    if (gBootLogCaptureCompleteMarked) return;
    if (!gContext.moduleManager.startupComplete()) return;
    if (bootLogCaptureWaitsForHaDiscovery()) return;

    markBootLogCaptureComplete();
    gBootLogCaptureCompleteMarked = true;
}
#endif

void run()
{
    if (gStarted) return;

    const FirmwareProfile& profile = resolveProfile();
    gContext.profile = &profile;
    gContext.board = profile.board;
    gContext.domain = profile.domain;
    gContext.identity = &profile.identity;
    gContext.supervisorRuntime = profile.supervisorRuntime;

    if (profile.setup) {
        profile.setup(gContext);
    }

    gContext.bootCompleted = true;
    if (const HmiService* hmi = gContext.services.get<HmiService>(ServiceId::Hmi)) {
        if (hmi->setBootComplete) hmi->setBootComplete(hmi->ctx);
    }
    gStarted = true;
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    markBootLogCaptureCompleteIfReady();
#endif
}

void loop()
{
    if (!gStarted) {
        run();
    }

    (void)gContext.moduleManager.tickStartup(gContext.registry, gContext.services);
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    markBootLogCaptureCompleteIfReady();
#endif

    if (gContext.profile && gContext.profile->loop) {
        gContext.profile->loop(gContext);
    }
}

AppContext& context()
{
    return gContext;
}

const FirmwareProfile& activeProfile()
{
    return resolveProfile();
}

}  // namespace Bootstrap
