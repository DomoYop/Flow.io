#pragma once

#include "App/FirmwareProfile.h"
#include "Core/SystemLimits.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/HMIModule/HMIModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IOModule.h"
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"
#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Network/HAModule/HAModule.h"
#include "Modules/Network/HmiUdpServerModule/HmiUdpServerModule.h"
#include "Modules/Network/I2CCfgServerModule/I2CCfgServerModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"
#include "Modules/PoolLogicModule/PoolLogicModule.h"
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"

class DataStore;
struct HAService;

namespace Profiles {
namespace FlowIO {

struct ModuleInstances {
    explicit ModuleInstances(const BoardSpec& board);

    WifiModule wifiModule;
    TimeModule timeModule{};
    I2CCfgServerModule i2cCfgServerModule;
    CommandModule commandModule{};
    ConfigStoreModule configStoreModule{};
    DataStoreModule dataStoreModule{};
    MQTTModule mqttModule{};
    HAModule haModule{};
    SystemModule systemModule{};
    SystemMonitorModule systemMonitorModule{};
    LogSerialSinkModule logSerialSinkModule{};
    LogDispatcherModule logDispatcherModule{};
    LogHubModule logHubModule{};
    EventBusModule eventBusModule{};
    AlarmModule alarmModule{};
    HmiUdpServerModule hmiUdpServerModule{};
    HMIModule hmiModule;
    IOModule ioModule;
    PoolDeviceModule poolDeviceModule{};
    PoolLogicModule poolLogicModule{};
    // Bus 1-Wire bit-bang de la carte. Un bus ne porte aucun role metier :
    // n'importe quelle sonde peut vivre sur n'importe lequel.
    OneWireBus oneWireBus1{19};
    OneWireBus oneWireBus2{18};
    DataStore* ioDataStore = nullptr;
    const HAService* haService = nullptr;
    char topicNetworkState[Limits::TopicBuf] = {0};
    char topicSystemState[Limits::TopicBuf] = {0};
};

ModuleInstances& moduleInstances();
const FirmwareProfile& profile();
void setupProfile(AppContext& ctx);
void loopProfile(AppContext& ctx);

}  // namespace FlowIO
}  // namespace Profiles
