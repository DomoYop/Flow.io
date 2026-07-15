/**
 * @file IOModule.cpp
 * @brief Implementation file.
 */

#include "IOModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::IOModule)
#include "Core/ModuleLog.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/IOModule/IoBackendTraits.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <new>
#include <stdlib.h>
#include <string.h>

IOModule::IOModule(const BoardSpec& board)
{
    applyBoardDefaults_(board);
}

void IOModule::applyBoardDefaults_(const BoardSpec& board)
{
    boardProfileName_ = board.name ? board.name : "unknown";
    const I2cBusSpec* ioBus = boardFindI2cBus(board, "io");
    if (!ioBus) return;
    boardDefaultI2cSda_ = ioBus->sdaPin;
    boardDefaultI2cScl_ = ioBus->sclPin;
    cfgData_.i2cSda = boardDefaultI2cSda_;
    cfgData_.i2cScl = boardDefaultI2cScl_;
}

void IOModule::logI2cConfigTrace_(const char* stage) const
{
    const char* sdaKeyActive = i2cSdaVar_.nvsKey ? i2cSdaVar_.nvsKey : "(null)";
    const char* sclKeyActive = i2cSclVar_.nvsKey ? i2cSclVar_.nvsKey : "(null)";

    Preferences prefs;
    bool opened = prefs.begin(NvsKeys::StorageNamespace, true);

    bool activeSdaExists = false;
    bool activeSclExists = false;
    bool legacySdaExists = false;
    bool legacySclExists = false;
    int activeSdaStored = 0;
    int activeSclStored = 0;
    int legacySdaStored = 0;
    int legacySclStored = 0;

    if (opened) {
        activeSdaExists = prefs.isKey(sdaKeyActive);
        activeSclExists = prefs.isKey(sclKeyActive);
        legacySdaExists = prefs.isKey(NvsKeys::Io::IO_SDA);
        legacySclExists = prefs.isKey(NvsKeys::Io::IO_SCL);
        activeSdaStored = prefs.getInt(sdaKeyActive, 0);
        activeSclStored = prefs.getInt(sclKeyActive, 0);
        legacySdaStored = prefs.getInt(NvsKeys::Io::IO_SDA, 0);
        legacySclStored = prefs.getInt(NvsKeys::Io::IO_SCL, 0);
        prefs.end();
    }

    const bool sdaValid = (cfgData_.i2cSda >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cSda);
    const bool sclValid = (cfgData_.i2cScl >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cScl);
    LOGI("io.i2c trace stage=%s board=%s defaults=(%ld,%ld) active_keys=(%s,%s) cfg=(%ld,%ld) valid=(%s,%s)",
         stage ? stage : "?",
         boardProfileName_ ? boardProfileName_ : "unknown",
         (long)boardDefaultI2cSda_,
         (long)boardDefaultI2cScl_,
         sdaKeyActive,
         sclKeyActive,
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl,
         sdaValid ? "true" : "false",
         sclValid ? "true" : "false");
    LOGI("io.i2c nvs ns=%s opened=%s active=(sda:%s/%d scl:%s/%d) legacy=(sda:%s/%d scl:%s/%d)",
         NvsKeys::StorageNamespace,
         opened ? "true" : "false",
         activeSdaExists ? "present" : "absent",
         activeSdaStored,
         activeSclExists ? "present" : "absent",
         activeSclStored,
         legacySdaExists ? "present" : "absent",
         legacySdaStored,
         legacySclExists ? "present" : "absent",
         legacySclStored);
}

namespace {
static constexpr uint8_t kIoCfgProducerId = 47;
static constexpr uint8_t kCfgBranchIo = 1;
static constexpr uint8_t kCfgBranchIoDebug = 2;
static constexpr uint8_t kCfgBranchIoA0 = 3;
static constexpr uint8_t kCfgBranchIoA1 = 4;
static constexpr uint8_t kCfgBranchIoA2 = 5;
static constexpr uint8_t kCfgBranchIoA3 = 6;
static constexpr uint8_t kCfgBranchIoA4 = 7;
static constexpr uint8_t kCfgBranchIoA5 = 8;
static constexpr uint8_t kCfgBranchIoA6 = 33;
static constexpr uint8_t kCfgBranchIoA7 = 34;
static constexpr uint8_t kCfgBranchIoA8 = 35;
static constexpr uint8_t kCfgBranchIoA9 = 36;
static constexpr uint8_t kCfgBranchIoA10 = 37;
static constexpr uint8_t kCfgBranchIoA11 = 38;
static constexpr uint8_t kCfgBranchIoA12 = 39;
static constexpr uint8_t kCfgBranchIoA13 = 40;
static constexpr uint8_t kCfgBranchIoA14 = 41;
static constexpr uint8_t kCfgBranchIoA15 = 42;
static constexpr uint8_t kCfgBranchIoA16 = 57;
static constexpr uint8_t kCfgBranchIoA17 = 58;
static constexpr uint8_t kCfgBranchIoA18 = 59;
static constexpr uint8_t kCfgBranchIoA19 = 60;
static constexpr uint8_t kCfgBranchIoA20 = 61;
static constexpr uint8_t kCfgBranchIoA21 = 62;
static constexpr uint8_t kCfgBranchIoA22 = 63;
static constexpr uint8_t kCfgBranchIoA23 = 64;
static constexpr uint8_t kCfgBranchIoA24 = 65;
static constexpr uint8_t kCfgBranchIoA25 = 66;
static constexpr uint8_t kCfgBranchIoA26 = 67;
static constexpr uint8_t kCfgBranchIoA27 = 68;
static constexpr uint8_t kCfgBranchIoA28 = 69;
static constexpr uint8_t kCfgBranchIoA29 = 70;
static constexpr uint8_t kCfgBranchIoA30 = 71;
static constexpr uint8_t kCfgBranchIoA31 = 72;
static constexpr uint8_t kCfgBranchIoD0 = 9;
static constexpr uint8_t kCfgBranchIoD1 = 10;
static constexpr uint8_t kCfgBranchIoD2 = 11;
static constexpr uint8_t kCfgBranchIoD3 = 12;
static constexpr uint8_t kCfgBranchIoD4 = 13;
static constexpr uint8_t kCfgBranchIoD5 = 14;
static constexpr uint8_t kCfgBranchIoD6 = 15;
static constexpr uint8_t kCfgBranchIoD7 = 16;
static constexpr uint8_t kCfgBranchIoD8 = 49;
static constexpr uint8_t kCfgBranchIoD9 = 50;
static constexpr uint8_t kCfgBranchIoD10 = 51;
static constexpr uint8_t kCfgBranchIoD11 = 52;
static constexpr uint8_t kCfgBranchIoD12 = 53;
static constexpr uint8_t kCfgBranchIoD13 = 54;
static constexpr uint8_t kCfgBranchIoD14 = 55;
static constexpr uint8_t kCfgBranchIoD15 = 56;

static constexpr uint8_t kCfgBranchIoI0 = 17;
static constexpr uint8_t kCfgBranchIoI1 = 18;
static constexpr uint8_t kCfgBranchIoI2 = 19;
static constexpr uint8_t kCfgBranchIoI3 = 20;
static constexpr uint8_t kCfgBranchIoI4 = 21;
static constexpr uint8_t kCfgBranchIoI5 = 44;
static constexpr uint8_t kCfgBranchIoI6 = 45;
static constexpr uint8_t kCfgBranchIoI7 = 46;
static constexpr uint8_t kCfgBranchIoBus = 22;
static constexpr uint8_t kCfgBranchIoDs18b20 = 23;
static constexpr uint8_t kCfgBranchIoGpio = 24;
static constexpr uint8_t kCfgBranchIoAds1115 = 25;
static constexpr uint8_t kCfgBranchIoAdsInt = 26;
static constexpr uint8_t kCfgBranchIoAdsExt = 27;
static constexpr uint8_t kCfgBranchIoPcf857x = 28;
static constexpr uint8_t kCfgBranchIoSht40 = 29;
static constexpr uint8_t kCfgBranchIoBmp280 = 30;
static constexpr uint8_t kCfgBranchIoBme680 = 31;
static constexpr uint8_t kCfgBranchIoMcp23017 = 48;
static constexpr uint8_t kCfgBranchIoDs2484 = 57;
static constexpr uint8_t kCfgBranchIo1Wire1 = 58;
static constexpr uint8_t kCfgBranchIo1Wire2 = 59;
static constexpr uint8_t kCfgBranchIoPowermon = 60;
static constexpr uint8_t kCfgBranchIoTca9554 = 73;
static constexpr PhysicalPortId kLegacyDisconnectedBindingPort = 65535U;
static constexpr char kLegacyCounterRuntimeKeyFmt[] = "ioi%02urt";

static constexpr uint8_t analogCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoA0 :
           (idx == 1U) ? kCfgBranchIoA1 :
           (idx == 2U) ? kCfgBranchIoA2 :
           (idx == 3U) ? kCfgBranchIoA3 :
           (idx == 4U) ? kCfgBranchIoA4 :
           (idx == 5U) ? kCfgBranchIoA5 :
           (idx == 6U) ? kCfgBranchIoA6 :
           (idx == 7U) ? kCfgBranchIoA7 :
           (idx == 8U) ? kCfgBranchIoA8 :
           (idx == 9U) ? kCfgBranchIoA9 :
           (idx == 10U) ? kCfgBranchIoA10 :
           (idx == 11U) ? kCfgBranchIoA11 :
           (idx == 12U) ? kCfgBranchIoA12 :
           (idx == 13U) ? kCfgBranchIoA13 :
           (idx == 14U) ? kCfgBranchIoA14 :
           (idx == 15U) ? kCfgBranchIoA15 :
           (idx == 16U) ? kCfgBranchIoA16 :
           (idx == 17U) ? kCfgBranchIoA17 :
           (idx == 18U) ? kCfgBranchIoA18 :
           (idx == 19U) ? kCfgBranchIoA19 :
           (idx == 20U) ? kCfgBranchIoA20 :
           (idx == 21U) ? kCfgBranchIoA21 :
           (idx == 22U) ? kCfgBranchIoA22 :
           (idx == 23U) ? kCfgBranchIoA23 :
           (idx == 24U) ? kCfgBranchIoA24 :
           (idx == 25U) ? kCfgBranchIoA25 :
           (idx == 26U) ? kCfgBranchIoA26 :
           (idx == 27U) ? kCfgBranchIoA27 :
           (idx == 28U) ? kCfgBranchIoA28 :
           (idx == 29U) ? kCfgBranchIoA29 :
           (idx == 30U) ? kCfgBranchIoA30 :
           (idx == 31U) ? kCfgBranchIoA31 :
                           ConfigBranchRef::UnknownLocalBranch;
}

static constexpr uint8_t digitalInCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoI0 :
           (idx == 1U) ? kCfgBranchIoI1 :
           (idx == 2U) ? kCfgBranchIoI2 :
           (idx == 3U) ? kCfgBranchIoI3 :
           (idx == 4U) ? kCfgBranchIoI4 :
           (idx == 5U) ? kCfgBranchIoI5 :
           (idx == 6U) ? kCfgBranchIoI6 :
           (idx == 7U) ? kCfgBranchIoI7 :
                          ConfigBranchRef::UnknownLocalBranch;
}

static constexpr uint8_t digitalOutCfgBranch_(uint8_t idx)
{
    return (idx == 0U) ? kCfgBranchIoD0 :
           (idx == 1U) ? kCfgBranchIoD1 :
           (idx == 2U) ? kCfgBranchIoD2 :
           (idx == 3U) ? kCfgBranchIoD3 :
           (idx == 4U) ? kCfgBranchIoD4 :
           (idx == 5U) ? kCfgBranchIoD5 :
           (idx == 6U) ? kCfgBranchIoD6 :
           (idx == 7U) ? kCfgBranchIoD7 :
           (idx == 8U) ? kCfgBranchIoD8 :
           (idx == 9U) ? kCfgBranchIoD9 :
           (idx == 10U) ? kCfgBranchIoD10 :
           (idx == 11U) ? kCfgBranchIoD11 :
           (idx == 12U) ? kCfgBranchIoD12 :
           (idx == 13U) ? kCfgBranchIoD13 :
           (idx == 14U) ? kCfgBranchIoD14 :
           (idx == 15U) ? kCfgBranchIoD15 :
                           ConfigBranchRef::UnknownLocalBranch;
}

PhysicalPortId normalizeConfiguredBindingPort(PhysicalPortId port)
{
    return (port == kLegacyDisconnectedBindingPort) ? IO_PORT_INVALID : port;
}

#define FLOW_IO_ANALOG_ROUTE_ENTRY(ROUTE_ID, BRANCH_ID, SLOT_STR) \
    {ROUTE_ID, {(uint8_t)ConfigModuleId::Io, BRANCH_ID}, "io/input/a" SLOT_STR, "io/input/a" SLOT_STR, (uint8_t)MqttPublishPriority::Normal, nullptr}
#define FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(ROUTE_ID, BRANCH_ID, SLOT_STR) \
    {ROUTE_ID, {(uint8_t)ConfigModuleId::Io, BRANCH_ID}, "io/output/d" SLOT_STR, "io/output/d" SLOT_STR, (uint8_t)MqttPublishPriority::Normal, nullptr}
static constexpr MqttConfigRouteProducer::Route kIoCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Io, kCfgBranchIo}, "io", "io", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {2, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDebug}, "io/debug", "io/debug", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_ANALOG_ROUTE_ENTRY(3, kCfgBranchIoA0, "00"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(4, kCfgBranchIoA1, "01"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(5, kCfgBranchIoA2, "02"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(6, kCfgBranchIoA3, "03"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(7, kCfgBranchIoA4, "04"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(8, kCfgBranchIoA5, "05"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(9, kCfgBranchIoD0, "00"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(10, kCfgBranchIoD1, "01"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(11, kCfgBranchIoD2, "02"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(12, kCfgBranchIoD3, "03"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(13, kCfgBranchIoD4, "04"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(14, kCfgBranchIoD5, "05"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(15, kCfgBranchIoD6, "06"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(16, kCfgBranchIoD7, "07"),
    {17, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI0}, "io/input/i00", "io/input/i00", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {18, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI1}, "io/input/i01", "io/input/i01", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {19, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI2}, "io/input/i02", "io/input/i02", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {20, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI3}, "io/input/i03", "io/input/i03", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {21, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI4}, "io/input/i04", "io/input/i04", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {44, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI5}, "io/input/i05", "io/input/i05", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {45, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI6}, "io/input/i06", "io/input/i06", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {46, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoI7}, "io/input/i07", "io/input/i07", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {22, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBus}, "io/drivers/bus", "io/drivers/bus", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {23, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDs18b20}, "io/drivers/ds18b20", "io/drivers/ds18b20", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {24, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoGpio}, "io/drivers/gpio", "io/drivers/gpio", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {25, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAds1115}, "io/drivers/ads1115", "io/drivers/ads1115", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {26, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAdsInt}, "io/drivers/ads1115_int", "io/drivers/ads1115_int", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {27, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoAdsExt}, "io/drivers/ads1115_ext", "io/drivers/ads1115_ext", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {28, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoPcf857x}, "io/drivers/pcf857x", "io/drivers/pcf857x", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {29, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoSht40}, "io/drivers/sht40", "io/drivers/sht40", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {30, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBmp280}, "io/drivers/bmp280", "io/drivers/bmp280", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {31, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoBme680}, "io/drivers/bme680", "io/drivers/bme680", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {43, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoPowermon}, "io/drivers/powermon", "io/drivers/powermon", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_ANALOG_ROUTE_ENTRY(33, kCfgBranchIoA6, "06"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(34, kCfgBranchIoA7, "07"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(35, kCfgBranchIoA8, "08"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(36, kCfgBranchIoA9, "09"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(37, kCfgBranchIoA10, "10"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(38, kCfgBranchIoA11, "11"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(39, kCfgBranchIoA12, "12"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(40, kCfgBranchIoA13, "13"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(41, kCfgBranchIoA14, "14"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(42, kCfgBranchIoA15, "15"),
    {48, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoMcp23017}, "io/drivers/mcp23017", "io/drivers/mcp23017", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {73, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoTca9554}, "io/drivers/tca9554", "io/drivers/tca9554", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {57, {(uint8_t)ConfigModuleId::Io, kCfgBranchIoDs2484}, "io/drivers/ds2484", "io/drivers/ds2484", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {58, {(uint8_t)ConfigModuleId::Io, kCfgBranchIo1Wire1}, "io/drivers/1wire_int1", "io/drivers/1wire_int1", (uint8_t)MqttPublishPriority::Normal, nullptr},
    {59, {(uint8_t)ConfigModuleId::Io, kCfgBranchIo1Wire2}, "io/drivers/1wire_int2", "io/drivers/1wire_int2", (uint8_t)MqttPublishPriority::Normal, nullptr},
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(49, kCfgBranchIoD8, "08"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(50, kCfgBranchIoD9, "09"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(51, kCfgBranchIoD10, "10"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(52, kCfgBranchIoD11, "11"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(53, kCfgBranchIoD12, "12"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(54, kCfgBranchIoD13, "13"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(55, kCfgBranchIoD14, "14"),
    FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY(56, kCfgBranchIoD15, "15"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(57, kCfgBranchIoA16, "16"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(58, kCfgBranchIoA17, "17"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(59, kCfgBranchIoA18, "18"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(60, kCfgBranchIoA19, "19"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(61, kCfgBranchIoA20, "20"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(62, kCfgBranchIoA21, "21"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(63, kCfgBranchIoA22, "22"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(64, kCfgBranchIoA23, "23"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(65, kCfgBranchIoA24, "24"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(66, kCfgBranchIoA25, "25"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(67, kCfgBranchIoA26, "26"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(68, kCfgBranchIoA27, "27"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(69, kCfgBranchIoA28, "28"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(70, kCfgBranchIoA29, "29"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(71, kCfgBranchIoA30, "30"),
    FLOW_IO_ANALOG_ROUTE_ENTRY(72, kCfgBranchIoA31, "31"),
};
#undef FLOW_IO_ANALOG_ROUTE_ENTRY
#undef FLOW_IO_DIGITAL_OUTPUT_ROUTE_ENTRY
static_assert((sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])) <= MqttConfigRouteProducer::MaxRoutes,
              "IOModule config routes exceed MqttConfigRouteProducer capacity");
}

template <typename T>
T* allocPsramArray_(size_t count)
{
    void* mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(T) * count, MALLOC_CAP_8BIT);
    if (!mem) return nullptr;

    T* out = static_cast<T*>(mem);
    for (size_t i = 0; i < count; ++i) {
        new (&out[i]) T();
    }
    return out;
}

void* allocPsramBytes_(size_t bytes)
{
    void* mem = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    return mem;
}

void IOModule::setOneWireBuses(OneWireBus* gpio1, OneWireBus* gpio2)
{
    oneWireGpio1_ = gpio1;
    oneWireGpio2_ = gpio2;
}

void IOModule::setBindingPorts(const IOBindingPortSpec* ports, uint8_t count)
{
    bindingPorts_ = ports;
    bindingPortCount_ = count;
}

bool IOModule::ensureSlotConfigVars_()
{
    if (slotCfgVars_) return true;
    void* mem = heap_caps_malloc(sizeof(IoSlotConfigVars), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(IoSlotConfigVars), MALLOC_CAP_8BIT);
    if (!mem) return false;
    slotCfgVars_ = new (mem) IoSlotConfigVars(analogCfg_, digitalInCfg_, digitalCfg_);
    return true;
}

bool IOModule::ensureScalableStorage_()
{
    if (analogSlots_ && digitalSlots_ && digitalSensorEndpointPool_ &&
        digitalActuatorEndpointPool_ && gpioDriverPool_) {
        return true;
    }

    if (!analogSlots_) analogSlots_ = allocPsramArray_<AnalogSlot>(MAX_ANALOG_ENDPOINTS);
    if (!digitalSlots_) digitalSlots_ = allocPsramArray_<DigitalSlot>(MAX_DIGITAL_SLOTS);
    if (!digitalSensorEndpointPool_) {
        digitalSensorEndpointPool_ = static_cast<uint8_t (*)[sizeof(DigitalSensorEndpoint)]>(
            allocPsramBytes_(MAX_DIGITAL_INPUTS * sizeof(DigitalSensorEndpoint))
        );
    }
    if (!digitalActuatorEndpointPool_) {
        digitalActuatorEndpointPool_ = static_cast<uint8_t (*)[sizeof(DigitalActuatorEndpoint)]>(
            allocPsramBytes_(MAX_DIGITAL_OUTPUTS * sizeof(DigitalActuatorEndpoint))
        );
    }
    if (!gpioDriverPool_) {
        gpioDriverPool_ = static_cast<uint8_t (*)[sizeof(GpioDriver)]>(
            allocPsramBytes_(MAX_DIGITAL_SLOTS * sizeof(GpioDriver))
        );
    }

    const bool ok = analogSlots_ && digitalSlots_ && digitalSensorEndpointPool_ &&
                    digitalActuatorEndpointPool_ && gpioDriverPool_;
    if (ok) {
        LOGI("I/O scalable storage ready analog=%u digital_slots=%u digital_in=%u digital_out=%u",
             (unsigned)MAX_ANALOG_ENDPOINTS,
             (unsigned)MAX_DIGITAL_SLOTS,
             (unsigned)MAX_DIGITAL_INPUTS,
             (unsigned)MAX_DIGITAL_OUTPUTS);
    } else {
        LOGE("I/O scalable storage allocation failed");
    }
    return ok;
}

bool IOModule::ensureDigitalCounterConfigState_()
{
    if (digitalCounterLastConfigTotals_) return true;
    digitalCounterLastConfigTotals_ = static_cast<float*>(
        heap_caps_calloc(MAX_DIGITAL_INPUTS, sizeof(float), MALLOC_CAP_8BIT)
    );
    return digitalCounterLastConfigTotals_ != nullptr;
}

bool IOModule::ensureLastCycleState_()
{
    if (lastCycle_) return true;
    lastCycle_ = static_cast<IoCycleInfo*>(
        heap_caps_calloc(1, sizeof(IoCycleInfo), MALLOC_CAP_8BIT)
    );
    return lastCycle_ != nullptr;
}

bool IOModule::ensureAnalogPrecisionState_()
{
    if (analogPrecisionLast_) return true;
    analogPrecisionLast_ = static_cast<int32_t*>(
        heap_caps_calloc(ANALOG_CFG_SLOTS, sizeof(int32_t), MALLOC_CAP_8BIT)
    );
    return analogPrecisionLast_ != nullptr;
}

bool IOModule::defineAnalogInput(const IOEndpointRegistration& reg, const IOAnalogSlotConfig& defaults)
{
    if (!ensureScalableStorage_()) return false;
    if (reg.id[0] == '\0') return false;
    if (reg.ioId == IO_ID_INVALID) return false;
    if (reg.ioId < IO_ID_AI_BASE || reg.ioId >= IO_ID_AI_MAX) return false;

    const uint8_t analogIdx = (uint8_t)(reg.ioId - IO_ID_AI_BASE);
    if (analogSlots_[analogIdx].used) return false;

    AnalogSlot& slot = analogSlots_[analogIdx];
    slot.used = true;
    slot.ioId = reg.ioId;
    strncpy(slot.id, reg.id, sizeof(slot.id) - 1);
    slot.id[sizeof(slot.id) - 1] = '\0';
    slot.cfg = defaults;
    slot.onValueChanged = reg.onAnalogValueChanged;
    slot.onValueCtx = reg.onAnalogValueCtx;

    if (analogIdx < ANALOG_CFG_SLOTS) {
        analogCfg_[analogIdx] = defaults;
        strncpy(analogCfg_[analogIdx].name, reg.id, sizeof(analogCfg_[analogIdx].name) - 1);
        analogCfg_[analogIdx].name[sizeof(analogCfg_[analogIdx].name) - 1] = '\0';
    }

    return true;
}

bool IOModule::applyAnalogInputDefaults(const IOEndpointRegistration& reg, const IOAnalogSlotConfig& defaults)
{
    if (!ensureScalableStorage_()) return false;
    if (reg.id[0] == '\0') return false;
    if (reg.ioId == IO_ID_INVALID) return false;
    if (reg.ioId < IO_ID_AI_BASE || reg.ioId >= IO_ID_AI_MAX) return false;

    const uint8_t analogIdx = (uint8_t)(reg.ioId - IO_ID_AI_BASE);
    AnalogSlot& slot = analogSlots_[analogIdx];
    if (!slot.used) return false;

    strncpy(slot.id, reg.id, sizeof(slot.id) - 1);
    slot.id[sizeof(slot.id) - 1] = '\0';
    slot.cfg = defaults;
    slot.onValueChanged = reg.onAnalogValueChanged;
    slot.onValueCtx = reg.onAnalogValueCtx;

    if (analogIdx < ANALOG_CFG_SLOTS) {
        analogCfg_[analogIdx] = defaults;
        strncpy(analogCfg_[analogIdx].name, reg.id, sizeof(analogCfg_[analogIdx].name) - 1);
        analogCfg_[analogIdx].name[sizeof(analogCfg_[analogIdx].name) - 1] = '\0';
    }

    return true;
}

bool IOModule::digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.kind != kind) continue;
        if (s.logicalIdx != logicalIdx) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

bool IOModule::findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        const DigitalSlot& s = digitalSlots_[i];
        if (!s.used) continue;
        if (s.ioId != id) continue;
        slotIdxOut = i;
        return true;
    }
    return false;
}

ConfigVariable<float,0>* IOModule::counterTotalVar_(uint8_t logicalIdx)
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return nullptr;
    if (!slotCfgVars_ && !ensureSlotConfigVars_()) return nullptr;
    if (logicalIdx >= IoSlotConfigVars::DigitalInSlots) return nullptr;
    return &slotCfgVars_->din[logicalIdx].counterTotal;
}

float* IOModule::counterConfigTotalState_(uint8_t logicalIdx)
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return nullptr;
    if (!digitalCounterLastConfigTotals_ && !ensureDigitalCounterConfigState_()) return nullptr;
    return &digitalCounterLastConfigTotals_[logicalIdx];
}

void IOModule::eraseLegacyCounterPersistedTotal_(uint8_t logicalIdx)
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return;

    char key[16];
    snprintf(key, sizeof(key), kLegacyCounterRuntimeKeyFmt, (unsigned)logicalIdx);
    if (cfgSvc_ && cfgSvc_->eraseKeyAsync) {
        (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, key);
    }
}

void IOModule::beginIoCycle_(uint32_t nowMs)
{
    if (!ensureLastCycleState_()) return;
    ++lastCycle_->seq;
    lastCycle_->tsMs = nowMs;
    lastCycle_->changedCount = 0;
}

void IOModule::markIoCycleChanged_(IoId id)
{
    if (id == IO_ID_INVALID) return;
    if (!ensureLastCycleState_()) return;

    for (uint8_t i = 0; i < lastCycle_->changedCount; ++i) {
        if (lastCycle_->changedIds[i] == id) return;
    }

    if (lastCycle_->changedCount >= IO_MAX_CHANGED_IDS) return;
    lastCycle_->changedIds[lastCycle_->changedCount++] = id;
}

bool IOModule::defineDigitalInput(const IOEndpointRegistration& reg, const IODigitalInputSlotConfig& defaults)
{
    if (!ensureScalableStorage_()) return false;
    if (reg.id[0] == '\0') return false;
    if (reg.ioId == IO_ID_INVALID) return false;
    if (reg.ioId < IO_ID_DI_BASE || reg.ioId >= IO_ID_DI_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(reg.ioId - IO_ID_DI_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_INPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = reg.ioId;
        s.kind = DIGITAL_SLOT_INPUT;
        s.logicalIdx = logicalIdx;
        strncpy(s.id, reg.id, sizeof(s.id) - 1);
        s.id[sizeof(s.id) - 1] = '\0';
        s.inCfg = defaults;
        s.onValueChanged = reg.onDigitalValueChanged;
        s.onValueCtx = reg.onDigitalValueCtx;
        s.onCounterChanged = reg.onCounterChanged;
        s.onCounterCtx = reg.onCounterCtx;
        s.owner = this;
        if (logicalIdx < MAX_DIGITAL_INPUTS) {
            digitalInCfg_[logicalIdx] = defaults;
            strncpy(digitalInCfg_[logicalIdx].name, reg.id, sizeof(digitalInCfg_[logicalIdx].name) - 1);
            digitalInCfg_[logicalIdx].name[sizeof(digitalInCfg_[logicalIdx].name) - 1] = '\0';
        }
        return true;
    }

    return false;
}

bool IOModule::defineDigitalOutput(const IOEndpointRegistration& reg, const IODigitalOutputSlotConfig& defaults)
{
    if (!ensureScalableStorage_()) return false;
    if (reg.id[0] == '\0') return false;
    if (reg.ioId == IO_ID_INVALID) return false;
    if (reg.ioId < IO_ID_DO_BASE || reg.ioId >= IO_ID_DO_MAX) return false;

    const uint8_t logicalIdx = (uint8_t)(reg.ioId - IO_ID_DO_BASE);
    if (digitalLogicalUsed_(DIGITAL_SLOT_OUTPUT, logicalIdx)) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (s.used) continue;
        s.used = true;
        s.ioId = reg.ioId;
        s.kind = DIGITAL_SLOT_OUTPUT;
        s.logicalIdx = logicalIdx;
        strncpy(s.id, reg.id, sizeof(s.id) - 1);
        s.id[sizeof(s.id) - 1] = '\0';
        s.outCfg = defaults;
        s.owner = this;

        if (logicalIdx < DIGITAL_CFG_SLOTS) {
            const uint8_t cfgIdx = logicalIdx;
            digitalCfg_[cfgIdx] = defaults;
            strncpy(digitalCfg_[cfgIdx].name, reg.id, sizeof(digitalCfg_[cfgIdx].name) - 1);
            digitalCfg_[cfgIdx].name[sizeof(digitalCfg_[cfgIdx].name) - 1] = '\0';
        }
        return true;
    }

    return false;
}

const char* IOModule::analogSlotName(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return nullptr;
    if (!analogSlots_[idx].used) return nullptr;
    if (analogSlots_[idx].id[0] == '\0') return nullptr;
    return analogSlots_[idx].id;
}

bool IOModule::analogSlotUsed(uint8_t idx) const
{
    return idx < MAX_ANALOG_ENDPOINTS && analogSlots_[idx].used;
}

bool IOModule::analogSlotPublished(uint8_t idx) const
{
    return analogSlotPublished_(idx);
}

bool IOModule::digitalInputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_INPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx);
}

bool IOModule::digitalInputSlotPublished(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return false;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx)) return false;

    const DigitalSlot& s = digitalSlots_[slotIdx];
    return s.used && s.kind == DIGITAL_SLOT_INPUT && s.endpoint;
}

uint8_t IOModule::digitalInputValueType(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return IO_VAL_BOOL;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_INPUT, logicalIdx, slotIdx)) return IO_VAL_BOOL;
    const DigitalSlot& s = digitalSlots_[slotIdx];
    return (s.inCfg.mode == IO_DIGITAL_INPUT_COUNTER) ? IO_VAL_FLOAT : IO_VAL_BOOL;
}

int32_t IOModule::digitalInputPrecision(uint8_t logicalIdx) const
{
    if (logicalIdx >= MAX_DIGITAL_INPUTS) return 0;
    return sanitizeAnalogPrecision_(digitalInCfg_[logicalIdx].precision);
}

bool IOModule::digitalOutputSlotUsed(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    return logicalIdx < MAX_DIGITAL_OUTPUTS && findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logicalIdx, slotIdx);
}

bool IOModule::digitalOutputSlotWritable(uint8_t logicalIdx) const
{
    uint8_t slotIdx = 0xFF;
    if (logicalIdx >= MAX_DIGITAL_OUTPUTS) return false;
    if (!findDigitalSlotByLogical_(DIGITAL_SLOT_OUTPUT, logicalIdx, slotIdx)) return false;

    const DigitalSlot& s = digitalSlots_[slotIdx];
    return s.used && s.kind == DIGITAL_SLOT_OUTPUT && s.provider.isBound();
}

int32_t IOModule::analogPrecision(uint8_t idx) const
{
    if (idx >= ANALOG_CFG_SLOTS) return 0;
    return sanitizeAnalogPrecision_(analogCfg_[idx].precision);
}

uint32_t IOModule::takeAnalogConfigDirtyMask()
{
    const uint32_t mask = analogConfigDirtyMask_;
    analogConfigDirtyMask_ = 0;
    return mask;
}

bool IOModule::tickFastAds_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_ADS_INTERNAL_SINGLE].tick(nowMs);
    self->analogProviders_[IO_SRC_ADS_EXTERNAL_DIFF].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_ADS_INTERNAL_SINGLE || src == IO_SRC_ADS_EXTERNAL_DIFF) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickSlowDs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_DS18_WATER].tick(nowMs);
    self->analogProviders_[IO_SRC_DS18_AIR].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_DS18_WATER || src == IO_SRC_DS18_AIR) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickI2cAnalogs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    self->analogProviders_[IO_SRC_SHT40].tick(nowMs);
    self->analogProviders_[IO_SRC_BMP280].tick(nowMs);
    self->analogProviders_[IO_SRC_BME680].tick(nowMs);
    self->analogProviders_[IO_SRC_POWERMON].tick(nowMs);

    for (uint8_t i = 0; i < MAX_ANALOG_ENDPOINTS; ++i) {
        if (!self->analogSlots_[i].used) continue;
        const uint8_t src = self->analogSlots_[i].source;
        if (src == IO_SRC_SHT40 || src == IO_SRC_BMP280 || src == IO_SRC_BME680 || src == IO_SRC_POWERMON) {
            self->processAnalogDefinition_(i, nowMs);
        }
    }
    return true;
}

bool IOModule::tickDigitalInputs_(void* ctx, uint32_t nowMs)
{
    IOModule* self = static_cast<IOModule*>(ctx);
    if (!self || !self->runtimeReady_) return false;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        if (!self->digitalSlots_[i].used) continue;
        if (self->digitalSlots_[i].kind != DIGITAL_SLOT_INPUT) continue;
        (void)self->processDigitalInputDefinition_(i, nowMs);
    }
    self->pollPulseOutputs_(nowMs);
    return true;
}

const IOAnalogProvider* IOModule::analogProviderForSource_(uint8_t source) const
{
    // Kernel-side routing stays on compact source ids; runtime setup binds one provider per physical device.
    return (source < IO_SRC_COUNT) ? &analogProviders_[source] : nullptr;
}

bool IOModule::resolveConfiguredAnalogSource_(uint8_t idx, uint8_t& sourceOut) const
{
    if (idx >= ANALOG_CFG_SLOTS) return false;
    if (!analogSlots_[idx].used) return false;

    uint8_t channel = 0U;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveAnalogBinding_(analogCfg_[idx].bindingPort, source, channel, backend)) return false;

    sourceOut = source;
    return true;
}

bool IOModule::analogSourceRequiresDriverEnable_(uint8_t source) const
{
    return source == IO_SRC_SHT40 ||
           source == IO_SRC_BMP280 ||
           source == IO_SRC_BME680 ||
           source == IO_SRC_POWERMON;
}

bool IOModule::analogSourceDriverEnabled_(uint8_t source) const
{
    switch (source) {
        case IO_SRC_SHT40:
            return cfgData_.sht40Enabled;
        case IO_SRC_BMP280:
            return cfgData_.bmp280Enabled;
        case IO_SRC_BME680:
            return cfgData_.bme680Enabled;
        case IO_SRC_POWERMON:
            return cfgData_.powermonEnabled;
        default:
            return true;
    }
}

bool IOModule::analogSlotPublished_(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    return cfgData_.enabled && analogSlots_[idx].used && analogSlots_[idx].endpoint;
}

bool IOModule::analogRuntimeRoutePublished_(uint8_t idx) const
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    if (!cfgData_.enabled || !analogSlots_[idx].used) return false;

    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveConfiguredAnalogSource_(idx, source)) return false;

    if (!analogSourceRequiresDriverEnable_(source)) return true;
    return analogSourceDriverEnabled_(source);
}

bool IOModule::digitalRuntimeRoutePublished_(uint8_t slotIdx) const
{
    if (slotIdx >= MAX_DIGITAL_SLOTS) return false;
    const DigitalSlot& slot = digitalSlots_[slotIdx];
    if (!cfgData_.enabled || !slot.used) return false;

    if (slot.kind == DIGITAL_SLOT_INPUT) {
        if (slot.logicalIdx >= MAX_DIGITAL_INPUTS) return false;
        return digitalInCfg_[slot.logicalIdx].bindingPort != IO_PORT_INVALID;
    }
    if (slot.kind == DIGITAL_SLOT_OUTPUT) {
        if (slot.logicalIdx >= DIGITAL_CFG_SLOTS) return false;
        return digitalCfg_[slot.logicalIdx].bindingPort != IO_PORT_INVALID;
    }
    return false;
}

bool IOModule::analogSlotUsesUndefinedInvalidValue_(uint8_t idx) const
{
    uint8_t source = IO_ANALOG_SOURCE_INVALID;
    if (!resolveConfiguredAnalogSource_(idx, source)) return false;
    return analogSourceRequiresDriverEnable_(source) && analogSourceDriverEnabled_(source);
}

void IOModule::invalidateAnalogSlot_(AnalogSlot& slot, uint32_t nowMs)
{
    if (!slot.endpoint) return;
    if (!slot.lastRoundedValid) return;

    slot.endpoint->update(slot.lastRounded, false, nowMs);
    slot.lastRoundedValid = false;

    if (dataStore_) {
        uint8_t rtIdx = 0;
        if (endpointIndexFromId_(slot.id, rtIdx)) {
            (void)setIoEndpointInvalid(*dataStore_, rtIdx, IO_VALUE_FLOAT, nowMs);
        }
    }
    markIoCycleChanged_(slot.ioId);
}

bool IOModule::processAnalogDefinition_(uint8_t idx, uint32_t nowMs)
{
    if (idx >= MAX_ANALOG_ENDPOINTS) return false;
    AnalogSlot& slot = analogSlots_[idx];
    if (!slot.used || !slot.endpoint) return false;

    const IOAnalogProvider* provider = analogProviderForSource_(slot.source);
    if (!provider || !provider->isBound()) {
        invalidateAnalogSlot_(slot, nowMs);
        return false;
    }

    IOAnalogSample sample{};
    const uint8_t readChannel =
        (slot.source == IO_SRC_DS18_WATER || slot.source == IO_SRC_DS18_AIR) ? 0U : slot.channel;
    if (!provider->readSample(readChannel, sample)) {
        invalidateAnalogSlot_(slot, nowMs);
        return false;
    }
    float raw = sample.value;
    int16_t rawBinary = sample.raw;
    uint32_t sampleSeq = sample.seq;
    bool hasSampleSeq = sample.hasSeq;

    // Providers expose an optional sequence so multi-channel sensors only update endpoints on fresh acquisitions.
    if (hasSampleSeq) {
        if (slot.lastSampleSeqValid && sampleSeq == slot.lastSampleSeq) return false;
        slot.lastSampleSeq = sampleSeq;
        slot.lastSampleSeqValid = true;
    }

    float filtered = slot.median.update(raw);
    float calibrated = (slot.cfg.c0 * filtered) + slot.cfg.c1;
    float rounded = ioRoundToPrecision(calibrated, slot.cfg.precision);

    // Trace pH/ORP/Pressure calculation chain with configurable periodic ticker.
    bool isAdsSource = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ||
                       (slot.source == IO_SRC_ADS_EXTERNAL_DIFF);
    if (cfgData_.traceEnabled && isAdsSource && idx < 3) {
        uint32_t periodMs =
            (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : Limits::IoTracePeriodMs;
        uint32_t& lastMs = analogCalcLogLastMs_[idx];
        if (lastMs == 0U || (uint32_t)(nowMs - lastMs) >= periodMs) {
            const char* sensor = (idx == 0) ? "ORP" : ((idx == 1) ? "pH" : "Pressure");
            const char sourceMark = (slot.source == IO_SRC_ADS_INTERNAL_SINGLE) ? 'I' : 'E';
            LOGD("Calc %c %-3s raw_bin=%7d raw_V=%10.6f median_V=%10.6f coeff=%9.3f rounded=%9.3f",
                 sourceMark,
                 sensor,
                 (int)rawBinary,
                 (double)raw,
                 (double)filtered,
                 (double)calibrated,
                 (double)rounded);
            lastMs = nowMs;
        }
    }

    slot.endpoint->update(rounded, true, nowMs);

    if (!slot.lastRoundedValid || rounded != slot.lastRounded) {
        slot.lastRounded = rounded;
        slot.lastRoundedValid = true;
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(slot.id, rtIdx)) {
                (void)setIoEndpointFloat(*dataStore_, rtIdx, rounded, nowMs);
            }
        }
        markIoCycleChanged_(slot.ioId);
        if (slot.onValueChanged) {
            slot.onValueChanged(slot.onValueCtx, rounded);
        }
    }

    return true;
}

bool IOModule::processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs)
{
    if (slotIdx >= MAX_DIGITAL_SLOTS) return false;
    DigitalSlot& slot = digitalSlots_[slotIdx];
    if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT || !slot.endpoint) return false;
    if (slot.endpoint->type() != IO_EP_DIGITAL_SENSOR) return false;

    DigitalSensorEndpoint* inputEp = static_cast<DigitalSensorEndpoint*>(slot.endpoint);

    if (slot.inCfg.mode == IO_DIGITAL_INPUT_COUNTER) {
        if (!slot.provider.isBound()) return false;
        IDigitalCounterDriver* counterDriver = static_cast<IDigitalCounterDriver*>(slot.provider.ctx);
        if (!counterDriver) return false;

        const IODigitalInputSlotConfig* cfg = (slot.logicalIdx < MAX_DIGITAL_INPUTS) ? &digitalInCfg_[slot.logicalIdx] : nullptr;
        const float c0 = cfg ? cfg->c0 : 1.0f;
        const int32_t precision = sanitizeAnalogPrecision_(cfg ? cfg->precision : 0);

        int32_t rawCount = 0;
        if (!counterDriver->readCount(rawCount)) {
            if (slot.lastValid) {
                const float invalidValue = ioRoundToPrecision(slot.counterScaledTotal, precision);
                inputEp->updateFloat(invalidValue, false, nowMs);
                slot.lastValid = false;
            }
            return false;
        }

        float* lastConfigTotal = counterConfigTotalState_(slot.logicalIdx);
        if (cfg && lastConfigTotal && *lastConfigTotal != cfg->counterTotal) {
            slot.counterScaledTotal = cfg->counterTotal;
            slot.counterLastPersistedTotal = cfg->counterTotal;
            *lastConfigTotal = cfg->counterTotal;
            slot.counterLastRawCount = rawCount;
            slot.counterLastFlushedRawCount = rawCount;
            slot.counterLastPersistMs = nowMs;
        }

        const int32_t delta = rawCount - slot.counterLastRawCount;
        if (delta > 0) {
            slot.counterScaledTotal += ((float)delta * c0);
            slot.counterLastRawCount = rawCount;
            if (cfgData_.traceEnabled) {
                const float tracedScaledValue = ioRoundToPrecision(slot.counterScaledTotal, precision);
                LOGI("Counter pulse i%02u io=%u raw=%ld delta=%ld total=%.3f",
                     (unsigned)slot.logicalIdx,
                     (unsigned)slot.ioId,
                     (long)rawCount,
                     (long)delta,
                     (double)tracedScaledValue);
            }
        } else if (delta < 0) {
            if (cfgData_.traceEnabled) {
                LOGW("Counter raw reset i%02u io=%u raw=%ld prev_raw=%ld",
                     (unsigned)slot.logicalIdx,
                     (unsigned)slot.ioId,
                     (long)rawCount,
                     (long)slot.counterLastRawCount);
            }
            slot.counterLastRawCount = rawCount;
            slot.counterLastFlushedRawCount = rawCount;
        }
        (void)persistCounterTotalIfNeeded_(slot, rawCount, nowMs);

        const float scaledValue = ioRoundToPrecision(slot.counterScaledTotal, precision);

        IOEndpointValue prev{};
        const bool hasPrev = inputEp->read(prev) && prev.valid && prev.valueType == IO_EP_VALUE_FLOAT;
        const bool changed = (!slot.lastValid) || (delta != 0) || !hasPrev || (prev.v.f != scaledValue);
        if (changed) {
            inputEp->updateFloat(scaledValue, true, nowMs);
            slot.lastValid = true;
            if (dataStore_) {
                uint8_t rtIdx = 0;
                if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                    (void)setIoEndpointFloat(*dataStore_, rtIdx, scaledValue, nowMs);
                }
            }
            markIoCycleChanged_(slot.ioId);
        }
        return true;
    }

    if (!slot.provider.isBound()) return false;

    bool on = false;
    if (!slot.provider.read(on)) {
        // Transition to invalid only once; avoid timestamp churn while input remains unreadable.
        if (slot.lastValid) {
            inputEp->update(false, false, nowMs);
            slot.lastValid = false;
        }
        return false;
    }

    const bool changed = (!slot.lastValid) || (slot.lastValue != on);
    if (changed) {
        inputEp->update(on, true, nowMs);
        slot.lastValue = on;
        slot.lastValid = true;
        if (dataStore_) {
            uint8_t rtIdx = 0;
            if (endpointIndexFromId_(slot.endpointId, rtIdx)) {
                (void)setIoEndpointBool(*dataStore_, rtIdx, on, nowMs);
            }
        }
        markIoCycleChanged_(slot.ioId);
        if (slot.onValueChanged) {
            slot.onValueChanged(slot.onValueCtx, on);
        }
    }

    return true;
}

void IOModule::traceDigitalCounters_(uint32_t nowMs)
{
    if (!cfgData_.traceEnabled || !runtimeReady_) return;
    if (counterTraceLastMs_ != 0U && (uint32_t)(nowMs - counterTraceLastMs_) < 1000U) return;
    counterTraceLastMs_ = nowMs;

    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& slot = digitalSlots_[i];
        if (!slot.used || slot.kind != DIGITAL_SLOT_INPUT) continue;
        if (slot.inCfg.mode != IO_DIGITAL_INPUT_COUNTER) continue;
        if (!slot.provider.isBound()) continue;

        IDigitalCounterDriver* counterDriver = static_cast<IDigitalCounterDriver*>(slot.provider.ctx);
        if (!counterDriver) continue;

        IODigitalCounterDebugStats stats{};
        if (!counterDriver->readDebugStats(stats)) continue;
        LOGD("Counter dbg i%02u pin=%u accepted=%ld raw_hw=%lu polls=%lu dropped_db=%lu active_high=%u edge_mode=%u",
             (unsigned)slot.logicalIdx,
             (unsigned)stats.pin,
             (long)stats.pulseCount,
             (unsigned long)stats.irqCalls,
             (unsigned long)stats.transitions,
             (unsigned long)stats.ignoredDebounce,
             (unsigned)stats.activeHigh,
             (unsigned)stats.edgeMode);

    }
}

int32_t IOModule::sanitizeAnalogPrecision_(int32_t precision) const
{
    if (precision < 0) return 0;
    if (precision > 6) return 6;
    return precision;
}

void IOModule::forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs)
{
    if (analogIdx >= MAX_ANALOG_ENDPOINTS) return;
    AnalogSlot& slot = analogSlots_[analogIdx];
    if (!slot.used || !slot.endpoint) return;

    IOEndpointValue v{};
    if (!slot.endpoint->read(v) || !v.valid || v.valueType != IO_EP_VALUE_FLOAT) return;

    float republished = ioRoundToPrecision(v.v.f, slot.cfg.precision);
    slot.endpoint->update(republished, true, nowMs);
    if (dataStore_) {
        (void)setIoEndpointFloat(*dataStore_, analogIdx, republished, nowMs);
    }
}

void IOModule::refreshAnalogConfigState_()
{
    if (!ensureAnalogPrecisionState_()) return;

    // `c0/c1` live in the logical slot, not in the shared provider. Keeping them
    // synced here makes the next acquired sample use the new calibration without reboot.
    if (runtimeReady_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if (i >= MAX_ANALOG_ENDPOINTS) continue;
            if (!analogSlots_[i].used) continue;
            analogSlots_[i].cfg.c0 = analogCfg_[i].c0;
            analogSlots_[i].cfg.c1 = analogCfg_[i].c1;
        }
    }

    if (!analogPrecisionLastInit_) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
            analogPrecisionLast_[i] = p;
        }
        analogPrecisionLastInit_ = true;
        return;
    }

    bool changed = false;
    uint32_t changedMask = 0;
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        int32_t p = sanitizeAnalogPrecision_(analogCfg_[i].precision);
        if (analogPrecisionLast_[i] == p) continue;
        analogPrecisionLast_[i] = p;
        if (runtimeReady_ && i < MAX_ANALOG_ENDPOINTS && analogSlots_[i].used) {
            analogSlots_[i].cfg.precision = p;
        }
        changedMask |= (uint32_t)(1u << i);
        changed = true;
    }

    if (changed) {
        LOGI("Input precision changed -> publish runtime snapshot");
        const uint32_t nowMs = millis();
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            if ((changedMask & (uint32_t)(1u << i)) == 0) continue;
            forceAnalogSnapshotPublish_(i, nowMs);
        }
        analogConfigDirtyMask_ |= changedMask;
    }
}

bool IOModule::persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t rawCount, uint32_t nowMs)
{
    static constexpr int32_t kCounterPersistPulseDelta = 32;
    static constexpr uint32_t kCounterPersistPeriodMs = 180000U;

    if (slot.kind != DIGITAL_SLOT_INPUT || slot.inCfg.mode != IO_DIGITAL_INPUT_COUNTER) return false;
    if (slot.counterScaledTotal == slot.counterLastPersistedTotal) return false;

    bool shouldPersist = false;
    if (rawCount >= slot.counterLastFlushedRawCount &&
        (rawCount - slot.counterLastFlushedRawCount) >= kCounterPersistPulseDelta) {
        shouldPersist = true;
    }
    if (!shouldPersist &&
        slot.counterLastPersistMs != 0U &&
        (uint32_t)(nowMs - slot.counterLastPersistMs) >= kCounterPersistPeriodMs) {
        shouldPersist = true;
    }
    if (!shouldPersist) return false;

    ConfigVariable<float,0>* totalVar = counterTotalVar_(slot.logicalIdx);
    if (!totalVar) return false;

    if (cfgSvc_ && cfgSvc_->persistFloatAsync) {
        if (!totalVar->value || !totalVar->nvsKey) return false;
        if (!cfgSvc_->persistFloatAsync(cfgSvc_->ctx,
                                        totalVar->nvsKey,
                                        slot.counterScaledTotal,
                                        totalVar->moduleName,
                                        totalVar->moduleId,
                                        totalVar->localBranchId)) {
            return false;
        }
        *(totalVar->value) = slot.counterScaledTotal;
        totalVar->notify();
    } else {
        return false;
    }
    if (float* lastConfigTotal = counterConfigTotalState_(slot.logicalIdx)) {
        *lastConfigTotal = slot.counterScaledTotal;
    }
    slot.counterLastPersistedTotal = slot.counterScaledTotal;
    slot.counterLastFlushedRawCount = rawCount;
    slot.counterLastPersistMs = nowMs;
    return true;
}

void IOModule::pollPulseOutputs_(uint32_t nowMs)
{
    for (uint8_t i = 0; i < MAX_DIGITAL_SLOTS; ++i) {
        DigitalSlot& s = digitalSlots_[i];
        if (!s.used || s.kind != DIGITAL_SLOT_OUTPUT) continue;
        if (!s.outCfg.momentary || !s.pulseArmed || !s.provider.isBound()) continue;
        if ((int32_t)(nowMs - s.pulseDeadlineMs) < 0) continue;
        (void)s.provider.write(false);
        s.pulseArmed = false;
    }
}

bool IOModule::writeDigitalOut_(void* ctx, bool on)
{
    IOModule::DigitalSlot* s = static_cast<IOModule::DigitalSlot*>(ctx);
    if (!s || !s->provider.isBound()) return false;
    if (!s->used || s->kind != DIGITAL_SLOT_OUTPUT) return false;

    if (!s->outCfg.momentary) {
        bool ok = s->provider.write(on);
        if (ok && s->owner) s->owner->markIoCycleChanged_(s->ioId);
        return ok;
    }

    // Momentary outputs always generate a physical pulse on each command.
    if (!s->provider.write(true)) return false;
    uint32_t pulse = (s->outCfg.pulseMs == 0) ? 500u : (uint32_t)s->outCfg.pulseMs;
    const uint32_t nowMs = millis();
    s->pulseDeadlineMs = nowMs + pulse;
    s->pulseArmed = true;
    if (s->owner) s->owner->markIoCycleChanged_(s->ioId);
    return true;
}

bool IOModule::endpointIndexFromId_(const char* id, uint8_t& idxOut) const
{
    if (!id || id[0] == '\0') return false;
    for (uint8_t i = 0; i < registry_.count(); ++i) {
        IOEndpoint* ep = registry_.at(i);
        if (!ep || !ep->id()) continue;
        if (strcmp(ep->id(), id) != 0) continue;
        idxOut = i;
        return true;
    }
    return false;
}

void IOModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Io;
    if (!ensureScalableStorage_()) return;

    cfgStore_ = &cfg;
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    if (!services.add(ServiceId::Io, &ioSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Io));
    }
    if (!services.add(ServiceId::StatusLeds, &statusLedsSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::StatusLeds));
    }

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchIo);
    cfg.registerVar(i2cSdaVar_, kCfgModuleId, kCfgBranchIoBus);
    cfg.registerVar(i2cSclVar_, kCfgModuleId, kCfgBranchIoBus);
    cfg.registerVar(adsPollVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(dsPollVar_, kCfgModuleId, kCfgBranchIoDs18b20);
    cfg.registerVar(digitalPollVar_, kCfgModuleId, kCfgBranchIoGpio);
    cfg.registerVar(adsInternalAddrVar_, kCfgModuleId, kCfgBranchIoAdsInt);
    cfg.registerVar(adsExternalAddrVar_, kCfgModuleId, kCfgBranchIoAdsExt);
    cfg.registerVar(adsGainVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(adsRateVar_, kCfgModuleId, kCfgBranchIoAds1115);
    cfg.registerVar(sht40EnabledVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(sht40AddressVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(sht40PollVar_, kCfgModuleId, kCfgBranchIoSht40);
    cfg.registerVar(bmp280EnabledVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bmp280AddressVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bmp280PollVar_, kCfgModuleId, kCfgBranchIoBmp280);
    cfg.registerVar(bme680EnabledVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(bme680AddressVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(bme680PollVar_, kCfgModuleId, kCfgBranchIoBme680);
    cfg.registerVar(powermonEnabledVar_, kCfgModuleId, kCfgBranchIoPowermon);
    cfg.registerVar(powermonModelVar_, kCfgModuleId, kCfgBranchIoPowermon);
    cfg.registerVar(powermonAddressVar_, kCfgModuleId, kCfgBranchIoPowermon);
    cfg.registerVar(powermonPollVar_, kCfgModuleId, kCfgBranchIoPowermon);
    cfg.registerVar(powermonShuntOhmsVar_, kCfgModuleId, kCfgBranchIoPowermon);
    cfg.registerVar(pcfEnabledVar_, kCfgModuleId, kCfgBranchIoPcf857x);
    cfg.registerVar(pcfAddressVar_, kCfgModuleId, kCfgBranchIoPcf857x);
    cfg.registerVar(pcfMaskDefaultVar_, kCfgModuleId, kCfgBranchIoPcf857x);
    cfg.registerVar(pcfActiveLowVar_, kCfgModuleId, kCfgBranchIoPcf857x);
    cfg.registerVar(mcp23017EnabledVar_, kCfgModuleId, kCfgBranchIoMcp23017);
    cfg.registerVar(mcp23017AddressVar_, kCfgModuleId, kCfgBranchIoMcp23017);
    cfg.registerVar(tca9554EnabledVar_, kCfgModuleId, kCfgBranchIoTca9554);
    cfg.registerVar(tca9554AddressVar_, kCfgModuleId, kCfgBranchIoTca9554);
    cfg.registerVar(ds2484EnabledVar_, kCfgModuleId, kCfgBranchIoDs2484);
    cfg.registerVar(ds2484AddressVar_, kCfgModuleId, kCfgBranchIoDs2484);
    cfg.registerVar(ds2484PollVar_, kCfgModuleId, kCfgBranchIoDs2484);
    cfg.registerVar(oneWire1EnabledVar_, kCfgModuleId, kCfgBranchIo1Wire1);
    cfg.registerVar(oneWire1GpioVar_, kCfgModuleId, kCfgBranchIo1Wire1);
    cfg.registerVar(oneWire1PollVar_, kCfgModuleId, kCfgBranchIo1Wire1);
    cfg.registerVar(oneWire2EnabledVar_, kCfgModuleId, kCfgBranchIo1Wire2);
    cfg.registerVar(oneWire2GpioVar_, kCfgModuleId, kCfgBranchIo1Wire2);
    cfg.registerVar(oneWire2PollVar_, kCfgModuleId, kCfgBranchIo1Wire2);
    cfg.registerVar(dsWaterRomVar_, kCfgModuleId, kCfgBranchIoDs18b20);
    cfg.registerVar(dsAirRomVar_, kCfgModuleId, kCfgBranchIoDs18b20);
    cfg.registerVar(traceEnabledVar_, kCfgModuleId, kCfgBranchIoDebug);
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchIoDebug);

    if (ensureSlotConfigVars_()) {
        slotCfgVars_->registerAll(cfg, kCfgModuleId, analogCfgBranch_, digitalInCfgBranch_, digitalOutCfgBranch_);
    } else {
        LOGE("failed to allocate IO slot config vars");
    }

    LOGI("I/O config registered");
    if (ensureAnalogPrecisionState_()) {
        for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
            analogPrecisionLast_[i] = sanitizeAnalogPrecision_(analogCfg_[i].precision);
        }
        analogPrecisionLastInit_ = true;
    } else {
        analogPrecisionLastInit_ = false;
        LOGE("failed to allocate analog precision state");
    }
    analogConfigDirtyMask_ = 0;

    (void)logHub_;
}

void IOModule::onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    for (uint8_t i = 0; i < ANALOG_CFG_SLOTS; ++i) {
        analogCfg_[i].bindingPort = normalizeConfiguredBindingPort(analogCfg_[i].bindingPort);
    }
    for (uint8_t i = 0; i < DIGITAL_INPUT_CFG_SLOTS; ++i) {
        digitalInCfg_[i].bindingPort = normalizeConfiguredBindingPort(digitalInCfg_[i].bindingPort);
    }
    for (uint8_t i = 0; i < DIGITAL_CFG_SLOTS; ++i) {
        digitalCfg_[i].bindingPort = normalizeConfiguredBindingPort(digitalCfg_[i].bindingPort);
    }
#if defined(FLOW_PROFILE_WAVESHARE)
    // On Waveshare, TCA9554 is the only path to drive digital outputs (EXIO1-8);
    // it cannot be an optional, user-disableable driver like PCF8574/MCP23017.
    cfgData_.tca9554Enabled = true;
#endif
    const bool sdaValid = (cfgData_.i2cSda >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cSda);
    const bool sclValid = (cfgData_.i2cScl >= 0) && digitalPinIsValid((uint8_t)cfgData_.i2cScl);
    if (!sdaValid || !sclValid) {
        LOGW("io.i2c invalid persisted pins sda=%ld scl=%ld, fallback to board defaults sda=%ld scl=%ld",
             (long)cfgData_.i2cSda,
             (long)cfgData_.i2cScl,
             (long)boardDefaultI2cSda_,
             (long)boardDefaultI2cScl_);
        if (cfgSvc_ && cfgSvc_->eraseKeyAsync) {
            (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, i2cSdaVar_.nvsKey);
            (void)cfgSvc_->eraseKeyAsync(cfgSvc_->ctx, i2cSclVar_.nvsKey);
        }
        cfgData_.i2cSda = boardDefaultI2cSda_;
        cfgData_.i2cScl = boardDefaultI2cScl_;
    }
    logI2cConfigTrace_("onConfigLoaded");
    if (!cfgMqttPubConfigured_) {
        cfgMqttPub_.configure(this,
                              kIoCfgProducerId,
                              kIoCfgRoutes,
                              (uint8_t)(sizeof(kIoCfgRoutes) / sizeof(kIoCfgRoutes[0])),
                              services);
        cfgMqttPubConfigured_ = true;
    }

#if defined(FLOW_PROFILE_MICRONOVA)
    LOGI("io.onConfigLoaded deferred runtime init enabled=%s i2c_sda=%ld i2c_scl=%ld",
         cfgData_.enabled ? "true" : "false",
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl);
#else
    configureRuntimeAfterConfig_();
#endif
}

void IOModule::onStart(ConfigStore& cfg, ServiceRegistry& services)
{
    (void)cfg;
    (void)services;
#if defined(FLOW_PROFILE_MICRONOVA)
    configureRuntimeAfterConfig_();
#endif
}

void IOModule::configureRuntimeAfterConfig_()
{
    if (runtimeInitAttempted_) {
        LOGD("io runtime init already attempted");
        return;
    }

    LOGI("io.runtime init begin enabled=%s i2c_sda=%ld i2c_scl=%ld runtimeReady=%s",
         cfgData_.enabled ? "true" : "false",
         (long)cfgData_.i2cSda,
         (long)cfgData_.i2cScl,
         runtimeReady_ ? "true" : "false");
    logI2cConfigTrace_("runtimeInit");

    runtimeInitAttempted_ = true;
    if (cfgData_.enabled) {
        runtimeReady_ = configureRuntime_();
        if (!runtimeReady_) {
            LOGW("Runtime init failed; no runtime allocations will be attempted later");
        } else {
            LOGI("io.runtime configured");
        }
    } else {
        runtimeReady_ = false;
        LOGI("io.runtime init skipped (disabled)");
    }
}

void IOModule::loop()
{
    const IoStatus st = ioTick_(millis());
    if (st != IO_OK) {
        if (!cfgData_.enabled || !runtimeReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
