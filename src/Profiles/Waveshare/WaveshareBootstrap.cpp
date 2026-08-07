#include "Profiles/Waveshare/WaveshareProfile.h"
#include "Profiles/Waveshare/WaveshareIoAssembly.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <string.h>

#include "App/AppContext.h"
#include "Board/BoardSpec.h"
#include "Board/BoardSerialMap.h"
#include "Core/ConfigMigrations.h"
#include "Core/DataStore/DataStore.h"
#include "Core/Log.h"
#include "Core/LogModuleIds.h"
#include "Core/NvsKeys.h"
#include "Core/SnprintfCheck.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include "Core/WokwiDefaultOverrides.h"
#include "Core/Services/IFlowCfg.h"
#include "Domain/IoRoleDefaults.h"
#include "Domain/Pool/PoolBehaviors.h"
#include "Domain/Pool/PoolIds.h"
#include "Profiles/Waveshare/WaveshareIoLayout.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

namespace {

using Profiles::Waveshare::ModuleInstances;

constexpr size_t kFlowIos3PsramMallocAlwaysInternalBytes = 128U;

const PoolDevicePreset* findPoolPresetById(const DomainSpec& domain, PoolDeviceId id)
{
    for (uint8_t i = 0; i < domain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = domain.poolDevices[i];
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Log::error((LogModuleId)LogModuleIdValue::Core, "setup failure: %s", step ? step : "unknown");
    if (!Log::hub()) {
        Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    }
    while (true) delay(1000);
}

bool mqttEnabledInPreferences(Preferences& prefs)
{
    return prefs.getBool(NvsKeys::Mqtt::Enabled, false);
}

bool buildNetworkSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = networkIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    const bool netReady = networkReady(*ds);
    const bool mqttOk = mqttReady(*ds);
    const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
    uint8_t mac[6] = {0};
    char macText[18] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(macText,
             sizeof(macText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)mac[0],
             (unsigned)mac[1],
             (unsigned)mac[2],
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);

    const int wrote = snprintf(out, len,
                               "{\"ready\":%s,\"ip\":\"%s\",\"sup_ip\":\"\",\"mac\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                               netReady ? "true" : "false",
                               ip,
                               macText,
                               rssi,
                               mqttOk ? "true" : "false",
                               (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    DataStore* ds = mqtt->dataStorePtr();
    const uint32_t rxDrop = ds ? mqttRxDrop(*ds) : 0U;
    const uint32_t parseFail = ds ? mqttParseFail(*ds) : 0U;
    const uint32_t handlerFail = ds ? mqttHandlerFail(*ds) : 0U;
    const uint32_t oversizeDrop = ds ? mqttOversizeDrop(*ds) : 0U;

    const int wrote = snprintf(
        out, len,
        "{\"upt_ms\":%llu,\"heap\":{\"free\":%lu,\"min_free\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"mqtt_rx\":{\"rx_drop\":%lu,\"oversize_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu},\"ts\":%lu}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)rxDrop,
        (unsigned long)oversizeDrop,
        (unsigned long)parseFail,
        (unsigned long)handlerFail,
        (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

void registerModules(AppContext& ctx, ModuleInstances& modules)
{
    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.activityLogModule);
#if FLOW_ENABLE_BOOT_LOG_CAPTURE
    ctx.moduleManager.add(&modules.bootLogCaptureModule);
#endif
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.hmiUdpServerModule);
    ctx.moduleManager.add(&modules.hmiModule);
    ctx.moduleManager.add(&modules.hmiBuzzerModule);
    ctx.moduleManager.add(&modules.alarmModule);
    ctx.moduleManager.add(&modules.ethernetModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.wifiProvisioningModule);
    ctx.moduleManager.add(&modules.webInterfaceModule);
    ctx.moduleManager.add(&modules.firmwareUpdateModule);
#if defined(FLOW_ENABLE_LOCAL_TFT_HMI) && (FLOW_ENABLE_LOCAL_TFT_HMI != 0)
    ctx.moduleManager.add(&modules.supervisorHMIModule);
#endif
#if defined(FLOW_ENABLE_TFT_S3) && (FLOW_ENABLE_TFT_S3 != 0)
    ctx.moduleManager.add(&modules.tftModuleS3);
#endif
    ctx.moduleManager.add(&modules.timeModule);
    ctx.moduleManager.add(&modules.mqttModule);
    if (mqttEnabledInPreferences(ctx.preferences)) {
        ctx.moduleManager.add(&modules.haModule);
    }
    ctx.moduleManager.add(&modules.systemModule);
    ctx.moduleManager.add(&modules.ioModule);
    ctx.moduleManager.add(&modules.poolLogicModule);
    ctx.moduleManager.add(&modules.poolDeviceModule);

    modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
    ctx.moduleManager.add(&modules.systemMonitorModule);
}

/**
 * Un appareil nait active si, et seulement si, le brochage d'usine lui donne un
 * relais. Les roles laisses volontairement non lies dans WaveshareIoLayout.h
 * (pompe desinfectant, electrolyseur) sortaient d'usine "actifs" alors qu'aucune
 * sortie ne les pilotait et que disinfection_type vaut Desactive : l'utilisateur
 * les active en meme temps qu'il choisit son mode de traitement.
 */
bool poolDeviceEnabledByFactoryWiring(const PoolDevicePreset& preset)
{
    const DigitalOutputRoleDefault* def =
        roleDefaultFor(Profiles::Waveshare::IoLayout::kDigitalOutputRoleDefaults, preset.commandSlot);
    return def != nullptr && def->bindingPort != IO_PORT_INVALID;
}

uint16_t dependsOnMaskForPreset(const DomainSpec& domain, const PoolDevicePreset& preset)
{
    if (preset.dependsOnDevice == POOL_DEVICE_INVALID) return 0;
    const PoolDevicePreset* dependency = findPoolPresetById(domain, preset.dependsOnDevice);
    if (!dependency) return 0;
    return (uint16_t)(1u << dependency->id);
}

void configurePoolDevices(const AppContext& ctx, ModuleInstances& modules)
{
    // Un PoolDevice n'est instancie que pour un role decrit par le domaine.
    // MaxPoolDevices n'est qu'un plafond de capacite : creer les slots
    // restants produisait des appareils generiques sans role, lies a des
    // sorties inexistantes ou deja pilotees en direct (recopie debit, volet),
    // et polluait l'arbre de configuration et les selecteurs de slot.
    for (uint8_t i = 0; i < Limits::Io::MaxPoolDevices; ++i) {
        const PoolDevicePreset* preset = findPoolPresetById(*ctx.domain, i);
        if (!preset) continue;

        // L'invariant pdN <-> dNN est verifie a la compilation par le
        // static_assert de PoolDomain.h : inutile de risquer ici une boucle
        // infinie de boot sur une table que le compilateur a deja validee.
        PoolDeviceDefinition def{};
        def.slot = i;
        def.ioSlot = digitalOutputSlot(i);
        def.enabled = poolDeviceEnabledByFactoryWiring(*preset);

        snprintf(def.label, sizeof(def.label), "%s", preset->displayName);
        def.commandSlot = preset->commandSlot;
        def.type = preset->poolDeviceType;
        def.flowLPerHour = preset->flowLPerHour;
        def.tankCapacityMl = preset->tankCapacityMl;
        def.tankInitialMl = preset->tankInitialMl;
        def.dependsOnMask = dependsOnMaskForPreset(*ctx.domain, *preset);
        def.maxUptimeDaySec = preset->maxUptimeDaySec;
        def.exposeHaSwitch = preset->exposeHaSwitch;
        def.externallyCommandable = preset->externallyCommandable;

        requireSetup(modules.poolDeviceModule.defineDevice(def), "define pool device");
    }
}

void postInit(AppContext& ctx, ModuleInstances& modules)
{
    const DataStoreService* dsSvc = ctx.services.get<DataStoreService>(ServiceId::DataStore);
    modules.ioDataStore = dsSvc ? dsSvc->store : nullptr;

    if (!mqttEnabledInPreferences(ctx.preferences)) {
        modules.haService = nullptr;
        return;
    }

    modules.mqttModule.formatTopic(modules.topicNetworkState, sizeof(modules.topicNetworkState), "rt/network/state");
    modules.mqttModule.formatTopic(modules.topicSystemState, sizeof(modules.topicSystemState), "rt/system/state");
    modules.mqttModule.addRuntimePublisher(modules.topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    modules.mqttModule.addRuntimePublisher(modules.topicSystemState, 60000, 0, false, buildSystemSnapshot);
    registerIoHomeAssistant(ctx, modules);
}

}  // namespace

namespace Profiles {
namespace Waveshare {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);

    if (psramFound()) {
        heap_caps_malloc_extmem_enable(kFlowIos3PsramMallocAlwaysInternalBytes);
        Board::SerialMap::logSerial().printf(
            "[waveshare] PSRAM malloc policy threshold=%u internal_free=%lu psram_free=%lu\r\n",
            (unsigned)kFlowIos3PsramMallocAlwaysInternalBytes,
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    ctx.registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);

    registerModules(ctx, modules);
    modules.hmiModule.setRemoteUdpServer(&modules.hmiUdpServerModule);
    configureIoModule(ctx, modules);
    configurePoolDevices(ctx, modules);
    modules.poolLogicModule.applyDomainDefaults(*ctx.domain);

    // Keep PoolLogic runtime snapshots first so HA-critical state (including
    // heat_assist reason) stays available even when runtime route capacity is reached.
    if (mqttEnabledInPreferences(ctx.preferences)) {
        requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolLogicModule), "register runtime provider poollogic");
        requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.ioModule), "register runtime provider io");
        requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.poolDeviceModule), "register runtime provider pooldev");
    }

    requireSetup(ctx.moduleManager.initAll(ctx.registry, ctx.services), "init modules");
    postInit(ctx, modules);
}

void loopProfile(AppContext&)
{
    ModuleInstances& modules = moduleInstances();
    if (modules.haService) {
        releaseIoHomeAssistantDiscoveryHeapIfDone(modules);
        refreshIoHomeAssistantIfNeeded(modules);
    }
    delay(20);
}

}  // namespace Waveshare
}  // namespace Profiles
