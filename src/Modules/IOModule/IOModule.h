#pragma once
/**
 * @file IOModule.h
 * @brief Unified IO module with endpoint registry and scheduler.
 */

#include "Board/BoardSpec.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/RuntimeUi.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IODrivers/Bme680Driver.h"
#include "Modules/IOModule/IODrivers/Bmp280Driver.h"
#include "Modules/IOModule/IOBus/Ds2484Bus.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IODrivers/Ds18b20Driver.h"
#include "Modules/IOModule/IODrivers/GpioDriver.h"
#include "Modules/IOModule/IODrivers/Ina226Driver.h"
#include "Modules/IOModule/IODrivers/Ina228Driver.h"
#include "Modules/IOModule/IODrivers/Mcp23017BitDriver.h"
#include "Modules/IOModule/IODrivers/Mcp23017Driver.h"
#include "Modules/IOModule/IODrivers/PcntCounterDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574BitDriver.h"
#include "Modules/IOModule/IODrivers/Pcf8574Driver.h"
#include "Modules/IOModule/IODrivers/Sht40Driver.h"
#include "Modules/IOModule/IODrivers/Tca9554BitDriver.h"
#include "Modules/IOModule/IODrivers/Tca9554Driver.h"
#include "Modules/IOModule/IOEndpoints/AnalogSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalActuatorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/DigitalSensorEndpoint.h"
#include "Modules/IOModule/IOEndpoints/Pcf8574MaskEndpoint.h"
#include "Modules/IOModule/IOEndpoints/RunningMedianAverageFloat.h"
#include "Modules/IOModule/IOModuleDataModel.h"
#include "Modules/IOModule/IOModuleTypes.h"
#include "Modules/IOModule/IOProviders/IOProviders.h"
#include "Modules/IOModule/IORegistry/IORegistry.h"
#include "Modules/IOModule/IOScheduler/IOScheduler.h"
#include <stdio.h>

class DataStore;
class OneWireBus;

/**
 * Internal index of the analog provider pool (one provider per physical
 * device). Derived from the binding port (backend + DS18 bus index); not part
 * of the public topology types.
 */
enum IOAnalogSource : uint8_t {
    IO_SRC_ADS_INTERNAL_SINGLE = 0,
    IO_SRC_ADS_EXTERNAL_DIFF = 1,
    IO_SRC_DS18_WATER = 2,
    IO_SRC_DS18_AIR = 3,
    IO_SRC_SHT40 = 4,
    IO_SRC_BMP280 = 5,
    IO_SRC_BME680 = 6,
    IO_SRC_POWERMON = 7,
    IO_SRC_COUNT = 8
};

constexpr uint8_t IO_ANALOG_SOURCE_INVALID = 0xFFu;

class IOModule : public Module, public IRuntimeSnapshotProvider, public IRuntimeUiValueProvider {
public:
    IOModule() = default;
    explicit IOModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::Io; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }
    const char* taskName() const override { return "io"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2560; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    UBaseType_t taskStackCaps() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        return Module::taskStackCaps();
#endif
    }

#if defined(FLOW_PROFILE_SUPERVISOR)
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::ConfigStore;
        return ModuleId::Unknown;
    }
#else
    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::ConfigStore;
        return ModuleId::Unknown;
    }
#endif

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::IoStartDelayMs; }

    void setOneWireBuses(OneWireBus* gpio1, OneWireBus* gpio2);
    void setBindingPorts(const IOBindingPortSpec* ports, uint8_t count);
    bool defineAnalogInput(const IOAnalogDefinition& def);
    bool applyAnalogInputDefaults(const IOAnalogDefinition& def);
    bool defineDigitalInput(const IODigitalInputDefinition& def);
    bool defineDigitalOutput(const IODigitalOutputDefinition& def);
    const char* analogSlotName(uint8_t idx) const;
    bool analogSlotUsed(uint8_t idx) const;
    bool analogSlotPublished(uint8_t idx) const;
    bool digitalInputSlotUsed(uint8_t logicalIdx) const;
    bool digitalInputSlotPublished(uint8_t logicalIdx) const;
    uint8_t digitalInputValueType(uint8_t logicalIdx) const;
    int32_t digitalInputPrecision(uint8_t logicalIdx) const;
    bool digitalOutputSlotUsed(uint8_t logicalIdx) const;
    bool digitalOutputSlotWritable(uint8_t logicalIdx) const;
    int32_t analogPrecision(uint8_t idx) const;
    uint32_t takeAnalogConfigDirtyMask();
    const char* endpointLabel(const char* endpointId) const;
    bool buildInputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildOutputSnapshot(char* out, size_t len, uint32_t& maxTsOut) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

    IORegistry& registry() { return registry_; }

private:
    struct AnalogSlot;
    struct DigitalSlot;

    enum RuntimeUiValueId : uint8_t {
        RuntimeUiWaterTemp = 1,
        RuntimeUiAirTemp = 2,
        RuntimeUiPh = 3,
        RuntimeUiOrp = 4,
        RuntimeUiWaterCounter = 5,
        RuntimeUiPsi = 6,
        RuntimeUiBmp280Temp = 7,
        RuntimeUiBme680Temp = 8,
        RuntimeUiBmp280Pressure = 9,
        RuntimeUiSht40Temperature = 10,
        RuntimeUiSht40Humidity = 11,
        RuntimeUiBme680Humidity = 12,
        RuntimeUiBme680Pressure = 13,
        RuntimeUiBme680Gaz = 14,
        RuntimeUiPowermonVoltage = 15,
        RuntimeUiPowermonCurrent = 16,
        RuntimeUiPowermonPower = 17,
        RuntimeUiPowermonTemperature = 18,
        RuntimeUiPowermonEnergy = 19,
        RuntimeUiPowermonCharge = 20,
    };

    static bool tickFastAds_(void* ctx, uint32_t nowMs);
    static bool tickSlowDs_(void* ctx, uint32_t nowMs);
    static bool tickI2cAnalogs_(void* ctx, uint32_t nowMs);
    static bool tickDigitalInputs_(void* ctx, uint32_t nowMs);

    uint8_t ioCount_() const;
    IoStatus ioIdAt_(uint8_t index, IoId* outId) const;
    IoStatus ioMeta_(IoId id, IoEndpointMeta* outMeta) const;
    IoStatus ioReadValue_(IoId id, IoValue* outValue) const;
    IoStatus ioReadDigital_(IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioWriteDigital_(IoId id, uint8_t on, uint32_t tsMs);
    IoStatus ioReadAnalog_(IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq) const;
    IoStatus ioTick_(uint32_t nowMs);
    IoStatus ioLastCycle_(IoCycleInfo* outCycle) const;
    IoStatus ioSensorStatus_(IoId id, IoSensorStatus* outStatus) const;
    IoStatus ioListInvalidSensors_(IoId* outIds, uint8_t maxIds, uint8_t* outCount) const;
    IoStatus ioBackendInfo_(uint8_t backend, uint8_t* outEnabled, uint8_t* outConfigurable) const;

    bool setLedMask_(uint8_t mask, uint32_t tsMs);
    bool turnLedOn_(uint8_t bit, uint32_t tsMs);
    bool turnLedOff_(uint8_t bit, uint32_t tsMs);
    bool getLedMask_(uint8_t& mask) const;
    bool getLedMaskSvc_(uint8_t* mask) const;
    uint8_t pcfPhysicalFromLogical_(uint8_t logicalMask) const;
    uint8_t pcfLogicalFromPhysical_(uint8_t physicalMask) const;

    bool configureRuntime_();
    const IOBindingPortSpec* bindingPortSpec_(PhysicalPortId portId) const;
    bool resolveAnalogBinding_(PhysicalPortId portId, uint8_t& sourceOut, uint8_t& channelOut, uint8_t& backendOut) const;
    bool resolveDigitalInputBinding_(PhysicalPortId portId, uint8_t& pinOut, uint8_t& backendOut, uint8_t& channelOut) const;
    bool resolveDigitalOutputBinding_(PhysicalPortId portId,
                                      uint8_t& pinOut,
                                      uint8_t& backendOut,
                                      uint8_t& channelOut,
                                      bool& usesPcfOut,
                                      bool& usesTcaOut,
                                      bool& usesMcpOut) const;
    bool resolveDsBusAddress_(OneWireBus* bus, const char* runtimeKey, uint8_t outAddr[8]);
    void resolveDs18Sensors_();
    bool resolveDsSensor_(IOneWireBus** buses, uint8_t nBuses, char* romCfg, size_t romCfgLen,
                          const char* nvsKey, const uint8_t* excludeAddr,
                          IOneWireBus** busOut, uint8_t outAddr[8]);
    uint32_t dsPollForBus_(const IOneWireBus* bus) const;
    static bool parseDs18Address_(const char* str, uint8_t out[8]);
    bool runtimeSnapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& routeTypeOut, uint8_t& slotIdxOut) const;
    bool buildEndpointSnapshot_(IOEndpoint* ep, char* out, size_t len, uint32_t& maxTsOut, bool invalidAsUndefined = false) const;
    bool buildGroupSnapshot_(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut) const;
    const IOAnalogProvider* analogProviderForSource_(uint8_t source) const;
    bool writeAnalogProviderRuntimeValue_(RuntimeUiId runtimeId, uint8_t source, uint8_t channel, IRuntimeUiWriter& writer) const;
    bool resolveConfiguredAnalogSource_(uint8_t idx, uint8_t& sourceOut) const;
    bool analogSourceRequiresDriverEnable_(uint8_t source) const;
    bool analogSourceDriverEnabled_(uint8_t source) const;
    bool analogRuntimeRoutePublished_(uint8_t idx) const;
    bool digitalRuntimeRoutePublished_(uint8_t slotIdx) const;
    bool analogSlotPublished_(uint8_t idx) const;
    bool analogSlotUsesUndefinedInvalidValue_(uint8_t idx) const;
    void invalidateAnalogSlot_(AnalogSlot& slot, uint32_t nowMs);
    bool processAnalogDefinition_(uint8_t idx, uint32_t nowMs);
    bool processDigitalInputDefinition_(uint8_t slotIdx, uint32_t nowMs);
    int32_t sanitizeAnalogPrecision_(int32_t precision) const;
    void forceAnalogSnapshotPublish_(uint8_t analogIdx, uint32_t nowMs);
    void refreshAnalogConfigState_();
    bool ensureAnalogPrecisionState_();
    bool ensureExtraAnalogCfgVars_();
    bool ensureDigitalInputModeCfgVars_();
    bool ensureExtraDigitalOutputCfgVars_();
    bool ensureDigitalCounterCfgVars_();
    bool ensureScalableStorage_();
    bool ensureDigitalCounterConfigState_();
    bool ensureLastCycleState_();
    bool endpointIndexFromId_(const char* id, uint8_t& idxOut) const;
    void configureRuntimeAfterConfig_();
    bool digitalLogicalUsed_(uint8_t kind, uint8_t logicalIdx) const;
    bool findDigitalSlotByLogical_(uint8_t kind, uint8_t logicalIdx, uint8_t& slotIdxOut) const;
    bool findDigitalSlotByIoId_(IoId id, uint8_t& slotIdxOut) const;
    ConfigVariable<float,0>* counterTotalVar_(uint8_t logicalIdx);
    float* counterConfigTotalState_(uint8_t logicalIdx);
    void eraseLegacyCounterPersistedTotal_(uint8_t logicalIdx);
    bool persistCounterTotalIfNeeded_(DigitalSlot& slot, int32_t rawCount, uint32_t nowMs);
    void traceDigitalCounters_(uint32_t nowMs);
    void beginIoCycle_(uint32_t nowMs);
    void markIoCycleChanged_(IoId id);
    void logI2cConfigTrace_(const char* stage) const;
    static bool writeDigitalOut_(void* ctx, bool on);
    void applyBoardDefaults_(const BoardSpec& board);
    void pollPulseOutputs_(uint32_t nowMs);
    AnalogSensorEndpoint* allocAnalogEndpoint_(const char* endpointId);
    DigitalSensorEndpoint* allocDigitalSensorEndpoint_(const char* endpointId, uint8_t valueType);
    DigitalActuatorEndpoint* allocDigitalActuatorEndpoint_(const char* endpointId, DigitalWriteFn writeFn, void* writeCtx);
    IDigitalCounterDriver* allocGpioDriver_(const char* driverId,
                                            uint8_t pin,
                                            bool output,
                                            bool activeHigh,
                                            uint8_t inputPullMode = GpioDriver::PullNone,
                                            bool counterEnabled = false,
                                            uint8_t edgeMode = IO_EDGE_RISING,
                                            uint32_t counterDebounceUs = 0);
    IAnalogSourceDriver* allocAdsDriver_(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg);
    IAnalogSourceDriver* allocDsDriver_(const char* driverId, IOneWireBus* bus, const uint8_t address[8], const Ds18b20DriverConfig& cfg);
    IAnalogSourceDriver* allocSht40Driver_(const char* driverId, I2CBus* bus, const Sht40DriverConfig& cfg);
    IAnalogSourceDriver* allocBmp280Driver_(const char* driverId, I2CBus* bus, const Bmp280DriverConfig& cfg);
    IAnalogSourceDriver* allocBme680Driver_(const char* driverId, I2CBus* bus, const Bme680DriverConfig& cfg);
    IAnalogSourceDriver* allocIna226Driver_(const char* driverId, I2CBus* bus, const Ina226DriverConfig& cfg);
    IAnalogSourceDriver* allocIna228Driver_(const char* driverId, I2CBus* bus, const Ina228DriverConfig& cfg);
    IDigitalPinDriver* allocPcfBitDriver_(const char* driverId, Pcf8574Driver* parent, uint8_t bit, bool activeHigh);
    IDigitalPinDriver* allocTcaBitDriver_(const char* driverId, Tca9554Driver* parent, uint8_t bit, bool activeHigh);
    IDigitalPinDriver* allocMcpBitDriver_(const char* driverId, Mcp23017Driver* parent, uint8_t bit, bool activeHigh);
    IMaskOutputDriver* allocPcfDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    IMaskOutputDriver* allocTcaDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    Mcp23017Driver* allocMcpDriver_(const char* driverId, I2CBus* bus, uint8_t address);
    Pcf8574MaskEndpoint* allocMaskEndpoint_(const char* endpointId, MaskWriteFn writeFn, MaskReadFn readFn, void* fnCtx);

    static constexpr uint8_t MAX_ANALOG_ENDPOINTS = Limits::Io::MaxAnalogEndpoints;
    static constexpr uint8_t MAX_DIGITAL_INPUTS = Limits::Io::MaxDigitalInputs;
    static constexpr uint8_t MAX_DIGITAL_OUTPUTS = Limits::Io::MaxDigitalOutputs;
    static constexpr uint8_t MAX_DIGITAL_SLOTS = MAX_DIGITAL_INPUTS + MAX_DIGITAL_OUTPUTS;
    static constexpr uint8_t ANALOG_CFG_SLOTS = Limits::Io::AnalogConfigSlots;
    static constexpr uint8_t DIGITAL_INPUT_CFG_SLOTS = Limits::Io::DigitalInputConfigSlots;
    static constexpr uint8_t DIGITAL_CFG_SLOTS = Limits::Io::DigitalOutputConfigSlots;
    static constexpr uint8_t ANALOG_CFG_STORAGE_SLOTS = (ANALOG_CFG_SLOTS < 17U) ? 17U : ANALOG_CFG_SLOTS;
    static constexpr uint8_t DIGITAL_INPUT_CFG_STORAGE_SLOTS =
        (DIGITAL_INPUT_CFG_SLOTS < 8U) ? 8U : DIGITAL_INPUT_CFG_SLOTS;
    static constexpr uint8_t DIGITAL_CFG_STORAGE_SLOTS = (DIGITAL_CFG_SLOTS < 16U) ? 16U : DIGITAL_CFG_SLOTS;
    static_assert(ANALOG_CFG_SLOTS <= 32U, "ExtraAnalogConfigVars currently supports analog slots a00..a31");
    /** End-exclusive upper bounds for each static id range. */
    static constexpr IoId IO_ID_DO_MAX = IO_ID_DO_BASE + MAX_DIGITAL_OUTPUTS;
    static constexpr IoId IO_ID_DI_MAX = IO_ID_DI_BASE + MAX_DIGITAL_INPUTS;
    static constexpr IoId IO_ID_AI_MAX = IO_ID_AI_BASE + MAX_ANALOG_ENDPOINTS;

    struct AnalogSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOAnalogDefinition def{};
        // `source` identifies the shared physical driver; `channel` selects one logical measurement.
        uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
        uint8_t channel = 0;
        uint8_t backend = IO_BACKEND_ADS1115_INT;
        AnalogSensorEndpoint* endpoint = nullptr;
        RunningMedianAverageFloat median{11, 5};
        bool lastSampleSeqValid = false;
        uint32_t lastSampleSeq = 0;
        bool lastRoundedValid = false;
        float lastRounded = 0.0f;
    };
    enum DigitalSlotKind : uint8_t {
        DIGITAL_SLOT_INPUT = 0,
        DIGITAL_SLOT_OUTPUT = 1
    };
    struct DigitalSlot {
        bool used = false;
        IoId ioId = IO_ID_INVALID;
        IOModule* owner = nullptr;
        uint8_t kind = DIGITAL_SLOT_INPUT;
        uint8_t logicalIdx = 0;
        char endpointId[8] = {0};
        IODigitalInputDefinition inDef{};
        IODigitalOutputDefinition outDef{};
        uint8_t backend = IO_BACKEND_GPIO;
        uint8_t channel = 0;
        IODigitalProvider provider{};
        IOEndpoint* endpoint = nullptr;
        bool pulseArmed = false;
        uint32_t pulseDeadlineMs = 0;
        bool lastValid = false;
        bool lastValue = false;
        float counterScaledTotal = 0.0f;
        float counterLastPersistedTotal = 0.0f;
        int32_t counterLastRawCount = 0;
        int32_t counterLastFlushedRawCount = 0;
        uint32_t counterLastPersistMs = 0;
    };

    struct ExtraAnalogConfigVars {
        static constexpr uint8_t FIRST_SLOT = 6U;
        static constexpr uint8_t SLOT_COUNT = 26U; // a06..a31

        struct SlotVars {
            char nameJson[9] = {0};
            char c0Json[8] = {0};
            char c1Json[8] = {0};
            char precJson[10] = {0};
            char moduleName[13] = {0};
            ConfigVariable<char,0> nameVar_;
            ConfigVariable<PhysicalPortId,0> bindingVar_;
            ConfigVariable<float,0> c0Var_;
            ConfigVariable<float,0> c1Var_;
            ConfigVariable<int32_t,0> precVar_;
        };

        SlotVars slots[SLOT_COUNT]{};

        explicit ExtraAnalogConfigVars(IOAnalogSlotConfig* analogCfg)
        {
            static constexpr const char* kNameKeys[SLOT_COUNT] = {
                NVS_KEY(NvsKeys::Io::IO_A6NM), NVS_KEY(NvsKeys::Io::IO_A7NM), NVS_KEY(NvsKeys::Io::IO_A8NM), NVS_KEY(NvsKeys::Io::IO_A9NM),
                NVS_KEY(NvsKeys::Io::IO_A10NM), NVS_KEY(NvsKeys::Io::IO_A11NM), NVS_KEY(NvsKeys::Io::IO_A12NM), NVS_KEY(NvsKeys::Io::IO_A13NM),
                NVS_KEY(NvsKeys::Io::IO_A14NM), NVS_KEY(NvsKeys::Io::IO_A15NM), NVS_KEY(NvsKeys::Io::IO_A16NM), NVS_KEY(NvsKeys::Io::IO_A17NM),
                NVS_KEY(NvsKeys::Io::IO_A18NM), NVS_KEY(NvsKeys::Io::IO_A19NM), NVS_KEY(NvsKeys::Io::IO_A20NM), NVS_KEY(NvsKeys::Io::IO_A21NM),
                NVS_KEY(NvsKeys::Io::IO_A22NM), NVS_KEY(NvsKeys::Io::IO_A23NM), NVS_KEY(NvsKeys::Io::IO_A24NM), NVS_KEY(NvsKeys::Io::IO_A25NM),
                NVS_KEY(NvsKeys::Io::IO_A26NM), NVS_KEY(NvsKeys::Io::IO_A27NM), NVS_KEY(NvsKeys::Io::IO_A28NM), NVS_KEY(NvsKeys::Io::IO_A29NM),
                NVS_KEY(NvsKeys::Io::IO_A30NM), NVS_KEY(NvsKeys::Io::IO_A31NM)
            };
            static constexpr const char* kBindingKeys[SLOT_COUNT] = {
                NVS_KEY(NvsKeys::Io::IO_A6BP), NVS_KEY(NvsKeys::Io::IO_A7BP), NVS_KEY(NvsKeys::Io::IO_A8BP), NVS_KEY(NvsKeys::Io::IO_A9BP),
                NVS_KEY(NvsKeys::Io::IO_A10BP), NVS_KEY(NvsKeys::Io::IO_A11BP), NVS_KEY(NvsKeys::Io::IO_A12BP), NVS_KEY(NvsKeys::Io::IO_A13BP),
                NVS_KEY(NvsKeys::Io::IO_A14BP), NVS_KEY(NvsKeys::Io::IO_A15BP), NVS_KEY(NvsKeys::Io::IO_A16BP), NVS_KEY(NvsKeys::Io::IO_A17BP),
                NVS_KEY(NvsKeys::Io::IO_A18BP), NVS_KEY(NvsKeys::Io::IO_A19BP), NVS_KEY(NvsKeys::Io::IO_A20BP), NVS_KEY(NvsKeys::Io::IO_A21BP),
                NVS_KEY(NvsKeys::Io::IO_A22BP), NVS_KEY(NvsKeys::Io::IO_A23BP), NVS_KEY(NvsKeys::Io::IO_A24BP), NVS_KEY(NvsKeys::Io::IO_A25BP),
                NVS_KEY(NvsKeys::Io::IO_A26BP), NVS_KEY(NvsKeys::Io::IO_A27BP), NVS_KEY(NvsKeys::Io::IO_A28BP), NVS_KEY(NvsKeys::Io::IO_A29BP),
                NVS_KEY(NvsKeys::Io::IO_A30BP), NVS_KEY(NvsKeys::Io::IO_A31BP)
            };
            static constexpr const char* kC0Keys[SLOT_COUNT] = {
                NVS_KEY(NvsKeys::Io::IO_A60), NVS_KEY(NvsKeys::Io::IO_A70), NVS_KEY(NvsKeys::Io::IO_A80), NVS_KEY(NvsKeys::Io::IO_A90),
                NVS_KEY(NvsKeys::Io::IO_A100), NVS_KEY(NvsKeys::Io::IO_A110), NVS_KEY(NvsKeys::Io::IO_A120), NVS_KEY(NvsKeys::Io::IO_A130),
                NVS_KEY(NvsKeys::Io::IO_A140), NVS_KEY(NvsKeys::Io::IO_A150), NVS_KEY(NvsKeys::Io::IO_A160), NVS_KEY(NvsKeys::Io::IO_A170),
                NVS_KEY(NvsKeys::Io::IO_A180), NVS_KEY(NvsKeys::Io::IO_A190), NVS_KEY(NvsKeys::Io::IO_A200), NVS_KEY(NvsKeys::Io::IO_A210),
                NVS_KEY(NvsKeys::Io::IO_A220), NVS_KEY(NvsKeys::Io::IO_A230), NVS_KEY(NvsKeys::Io::IO_A240), NVS_KEY(NvsKeys::Io::IO_A250),
                NVS_KEY(NvsKeys::Io::IO_A260), NVS_KEY(NvsKeys::Io::IO_A270), NVS_KEY(NvsKeys::Io::IO_A280), NVS_KEY(NvsKeys::Io::IO_A290),
                NVS_KEY(NvsKeys::Io::IO_A300), NVS_KEY(NvsKeys::Io::IO_A310)
            };
            static constexpr const char* kC1Keys[SLOT_COUNT] = {
                NVS_KEY(NvsKeys::Io::IO_A61), NVS_KEY(NvsKeys::Io::IO_A71), NVS_KEY(NvsKeys::Io::IO_A81), NVS_KEY(NvsKeys::Io::IO_A91),
                NVS_KEY(NvsKeys::Io::IO_A101), NVS_KEY(NvsKeys::Io::IO_A111), NVS_KEY(NvsKeys::Io::IO_A121), NVS_KEY(NvsKeys::Io::IO_A131),
                NVS_KEY(NvsKeys::Io::IO_A141), NVS_KEY(NvsKeys::Io::IO_A151), NVS_KEY(NvsKeys::Io::IO_A161), NVS_KEY(NvsKeys::Io::IO_A171),
                NVS_KEY(NvsKeys::Io::IO_A181), NVS_KEY(NvsKeys::Io::IO_A191), NVS_KEY(NvsKeys::Io::IO_A201), NVS_KEY(NvsKeys::Io::IO_A211),
                NVS_KEY(NvsKeys::Io::IO_A221), NVS_KEY(NvsKeys::Io::IO_A231), NVS_KEY(NvsKeys::Io::IO_A241), NVS_KEY(NvsKeys::Io::IO_A251),
                NVS_KEY(NvsKeys::Io::IO_A261), NVS_KEY(NvsKeys::Io::IO_A271), NVS_KEY(NvsKeys::Io::IO_A281), NVS_KEY(NvsKeys::Io::IO_A291),
                NVS_KEY(NvsKeys::Io::IO_A301), NVS_KEY(NvsKeys::Io::IO_A311)
            };
            static constexpr const char* kPrecKeys[SLOT_COUNT] = {
                NVS_KEY(NvsKeys::Io::IO_A6P), NVS_KEY(NvsKeys::Io::IO_A7P), NVS_KEY(NvsKeys::Io::IO_A8P), NVS_KEY(NvsKeys::Io::IO_A9P),
                NVS_KEY(NvsKeys::Io::IO_A10P), NVS_KEY(NvsKeys::Io::IO_A11P), NVS_KEY(NvsKeys::Io::IO_A12P), NVS_KEY(NvsKeys::Io::IO_A13P),
                NVS_KEY(NvsKeys::Io::IO_A14P), NVS_KEY(NvsKeys::Io::IO_A15P), NVS_KEY(NvsKeys::Io::IO_A16P), NVS_KEY(NvsKeys::Io::IO_A17P),
                NVS_KEY(NvsKeys::Io::IO_A18P), NVS_KEY(NvsKeys::Io::IO_A19P), NVS_KEY(NvsKeys::Io::IO_A20P), NVS_KEY(NvsKeys::Io::IO_A21P),
                NVS_KEY(NvsKeys::Io::IO_A22P), NVS_KEY(NvsKeys::Io::IO_A23P), NVS_KEY(NvsKeys::Io::IO_A24P), NVS_KEY(NvsKeys::Io::IO_A25P),
                NVS_KEY(NvsKeys::Io::IO_A26P), NVS_KEY(NvsKeys::Io::IO_A27P), NVS_KEY(NvsKeys::Io::IO_A28P), NVS_KEY(NvsKeys::Io::IO_A29P),
                NVS_KEY(NvsKeys::Io::IO_A30P), NVS_KEY(NvsKeys::Io::IO_A31P)
            };

            for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
                const uint8_t slot = (uint8_t)(FIRST_SLOT + i);
                SlotVars& vars = slots[i];
                snprintf(vars.nameJson, sizeof(vars.nameJson), "a%02u_name", (unsigned)slot);
                snprintf(vars.c0Json, sizeof(vars.c0Json), "a%02u_c0", (unsigned)slot);
                snprintf(vars.c1Json, sizeof(vars.c1Json), "a%02u_c1", (unsigned)slot);
                snprintf(vars.precJson, sizeof(vars.precJson), "a%02u_prec", (unsigned)slot);
                snprintf(vars.moduleName, sizeof(vars.moduleName), "io/input/a%02u", (unsigned)slot);

                vars.nameVar_ = {kNameKeys[i], vars.nameJson, vars.moduleName, ConfigType::CharArray, (char*)analogCfg[slot].name, ConfigPersistence::Persistent, sizeof(analogCfg[slot].name)};
                vars.bindingVar_ = {kBindingKeys[i], "binding_port", vars.moduleName, ConfigType::UInt16, &analogCfg[slot].bindingPort, ConfigPersistence::Persistent, 0};
                vars.c0Var_ = {kC0Keys[i], vars.c0Json, vars.moduleName, ConfigType::Float, &analogCfg[slot].c0, ConfigPersistence::Persistent, 0};
                vars.c1Var_ = {kC1Keys[i], vars.c1Json, vars.moduleName, ConfigType::Float, &analogCfg[slot].c1, ConfigPersistence::Persistent, 0};
                vars.precVar_ = {kPrecKeys[i], vars.precJson, vars.moduleName, ConfigType::Int32, &analogCfg[slot].precision, ConfigPersistence::Persistent, 0};
            }
        }
    };

    struct ExtraDigitalCounterConfigVars {
        ConfigVariable<float,0> i0TotalVar_;
        ConfigVariable<float,0> i1TotalVar_;
        ConfigVariable<float,0> i2TotalVar_;
        ConfigVariable<float,0> i3TotalVar_;
        ConfigVariable<float,0> i4TotalVar_;
        ConfigVariable<float,0> i5TotalVar_;
        ConfigVariable<float,0> i6TotalVar_;
        ConfigVariable<float,0> i7TotalVar_;

        explicit ExtraDigitalCounterConfigVars(IODigitalInputSlotConfig* digitalCfg)
            : i0TotalVar_{NVS_KEY(NvsKeys::Io::IO_I0CT), "counter_total", "io/input/i00", ConfigType::Float, &digitalCfg[0].counterTotal, ConfigPersistence::Persistent, 0},
              i1TotalVar_{NVS_KEY(NvsKeys::Io::IO_I1CT), "counter_total", "io/input/i01", ConfigType::Float, &digitalCfg[1].counterTotal, ConfigPersistence::Persistent, 0},
              i2TotalVar_{NVS_KEY(NvsKeys::Io::IO_I2CT), "counter_total", "io/input/i02", ConfigType::Float, &digitalCfg[2].counterTotal, ConfigPersistence::Persistent, 0},
              i3TotalVar_{NVS_KEY(NvsKeys::Io::IO_I3CT), "counter_total", "io/input/i03", ConfigType::Float, &digitalCfg[3].counterTotal, ConfigPersistence::Persistent, 0},
              i4TotalVar_{NVS_KEY(NvsKeys::Io::IO_I4CT), "counter_total", "io/input/i04", ConfigType::Float, &digitalCfg[4].counterTotal, ConfigPersistence::Persistent, 0},
              i5TotalVar_{NVS_KEY(NvsKeys::Io::IO_I5CT), "counter_total", "io/input/i05", ConfigType::Float, &digitalCfg[5].counterTotal, ConfigPersistence::Persistent, 0},
              i6TotalVar_{NVS_KEY(NvsKeys::Io::IO_I6CT), "counter_total", "io/input/i06", ConfigType::Float, &digitalCfg[6].counterTotal, ConfigPersistence::Persistent, 0},
              i7TotalVar_{NVS_KEY(NvsKeys::Io::IO_I7CT), "counter_total", "io/input/i07", ConfigType::Float, &digitalCfg[7].counterTotal, ConfigPersistence::Persistent, 0}
        {
        }
    };

    struct ExtraDigitalInputModeConfigVars {
        ConfigVariable<uint8_t,0> i0ModeVar_;
        ConfigVariable<uint8_t,0> i1ModeVar_;
        ConfigVariable<uint8_t,0> i2ModeVar_;
        ConfigVariable<uint8_t,0> i3ModeVar_;
        ConfigVariable<uint8_t,0> i4ModeVar_;
        ConfigVariable<uint8_t,0> i5ModeVar_;
        ConfigVariable<uint8_t,0> i6ModeVar_;
        ConfigVariable<uint8_t,0> i7ModeVar_;

        explicit ExtraDigitalInputModeConfigVars(IODigitalInputSlotConfig* digitalCfg)
            : i0ModeVar_{NVS_KEY(NvsKeys::Io::IO_I0MD), "mode", "io/input/i00", ConfigType::UInt8, &digitalCfg[0].mode, ConfigPersistence::Persistent, 0},
              i1ModeVar_{NVS_KEY(NvsKeys::Io::IO_I1MD), "mode", "io/input/i01", ConfigType::UInt8, &digitalCfg[1].mode, ConfigPersistence::Persistent, 0},
              i2ModeVar_{NVS_KEY(NvsKeys::Io::IO_I2MD), "mode", "io/input/i02", ConfigType::UInt8, &digitalCfg[2].mode, ConfigPersistence::Persistent, 0},
              i3ModeVar_{NVS_KEY(NvsKeys::Io::IO_I3MD), "mode", "io/input/i03", ConfigType::UInt8, &digitalCfg[3].mode, ConfigPersistence::Persistent, 0},
              i4ModeVar_{NVS_KEY(NvsKeys::Io::IO_I4MD), "mode", "io/input/i04", ConfigType::UInt8, &digitalCfg[4].mode, ConfigPersistence::Persistent, 0},
              i5ModeVar_{NVS_KEY(NvsKeys::Io::IO_I5MD), "mode", "io/input/i05", ConfigType::UInt8, &digitalCfg[5].mode, ConfigPersistence::Persistent, 0},
              i6ModeVar_{NVS_KEY(NvsKeys::Io::IO_I6MD), "mode", "io/input/i06", ConfigType::UInt8, &digitalCfg[6].mode, ConfigPersistence::Persistent, 0},
              i7ModeVar_{NVS_KEY(NvsKeys::Io::IO_I7MD), "mode", "io/input/i07", ConfigType::UInt8, &digitalCfg[7].mode, ConfigPersistence::Persistent, 0}
        {
        }
    };

    struct ExtraDigitalOutputConfigVars {
#define FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(INDEX) \
        ConfigVariable<char,0> d##INDEX##NameVar_; \
        ConfigVariable<PhysicalPortId,0> d##INDEX##BindingVar_; \
        ConfigVariable<bool,0> d##INDEX##ActiveHighVar_; \
        ConfigVariable<bool,0> d##INDEX##InitialOnVar_; \
        ConfigVariable<bool,0> d##INDEX##RetainWarmVar_; \
        ConfigVariable<bool,0> d##INDEX##MomentaryVar_; \
        ConfigVariable<int32_t,0> d##INDEX##PulseVar_;
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(8)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(9)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(10)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(11)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(12)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(13)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(14)
        FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS(15)
#undef FLOW_IO_EXTRA_DIGITAL_OUTPUT_MEMBERS

        explicit ExtraDigitalOutputConfigVars(IODigitalOutputSlotConfig* digitalCfg)
            : d8NameVar_{NVS_KEY(NvsKeys::Io::IO_D8NM),"d08_name","io/output/d08",ConfigType::CharArray,(char*)digitalCfg[8].name,ConfigPersistence::Persistent,sizeof(digitalCfg[8].name)},
              d8BindingVar_{NVS_KEY(NvsKeys::Io::IO_D8BP),"binding_port","io/output/d08",ConfigType::UInt16,&digitalCfg[8].bindingPort,ConfigPersistence::Persistent,0},
              d8ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D8AH),"d08_active_high","io/output/d08",ConfigType::Bool,&digitalCfg[8].activeHigh,ConfigPersistence::Persistent,0},
              d8InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D8IN),"d08_initial_on","io/output/d08",ConfigType::Bool,&digitalCfg[8].initialOn,ConfigPersistence::Persistent,0},
              d8RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D8RT),"retain_on_warm_reboot","io/output/d08",ConfigType::Bool,&digitalCfg[8].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d8MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D8MO),"d08_momentary","io/output/d08",ConfigType::Bool,&digitalCfg[8].momentary,ConfigPersistence::Persistent,0},
              d8PulseVar_{NVS_KEY(NvsKeys::Io::IO_D8PM),"d08_pulse_ms","io/output/d08",ConfigType::Int32,&digitalCfg[8].pulseMs,ConfigPersistence::Persistent,0},
              d9NameVar_{NVS_KEY(NvsKeys::Io::IO_D9NM),"d09_name","io/output/d09",ConfigType::CharArray,(char*)digitalCfg[9].name,ConfigPersistence::Persistent,sizeof(digitalCfg[9].name)},
              d9BindingVar_{NVS_KEY(NvsKeys::Io::IO_D9BP),"binding_port","io/output/d09",ConfigType::UInt16,&digitalCfg[9].bindingPort,ConfigPersistence::Persistent,0},
              d9ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D9AH),"d09_active_high","io/output/d09",ConfigType::Bool,&digitalCfg[9].activeHigh,ConfigPersistence::Persistent,0},
              d9InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D9IN),"d09_initial_on","io/output/d09",ConfigType::Bool,&digitalCfg[9].initialOn,ConfigPersistence::Persistent,0},
              d9RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D9RT),"retain_on_warm_reboot","io/output/d09",ConfigType::Bool,&digitalCfg[9].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d9MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D9MO),"d09_momentary","io/output/d09",ConfigType::Bool,&digitalCfg[9].momentary,ConfigPersistence::Persistent,0},
              d9PulseVar_{NVS_KEY(NvsKeys::Io::IO_D9PM),"d09_pulse_ms","io/output/d09",ConfigType::Int32,&digitalCfg[9].pulseMs,ConfigPersistence::Persistent,0},
              d10NameVar_{NVS_KEY(NvsKeys::Io::IO_D10NM),"d10_name","io/output/d10",ConfigType::CharArray,(char*)digitalCfg[10].name,ConfigPersistence::Persistent,sizeof(digitalCfg[10].name)},
              d10BindingVar_{NVS_KEY(NvsKeys::Io::IO_D10BP),"binding_port","io/output/d10",ConfigType::UInt16,&digitalCfg[10].bindingPort,ConfigPersistence::Persistent,0},
              d10ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D10AH),"d10_active_high","io/output/d10",ConfigType::Bool,&digitalCfg[10].activeHigh,ConfigPersistence::Persistent,0},
              d10InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D10IN),"d10_initial_on","io/output/d10",ConfigType::Bool,&digitalCfg[10].initialOn,ConfigPersistence::Persistent,0},
              d10RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D10RT),"retain_on_warm_reboot","io/output/d10",ConfigType::Bool,&digitalCfg[10].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d10MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D10MO),"d10_momentary","io/output/d10",ConfigType::Bool,&digitalCfg[10].momentary,ConfigPersistence::Persistent,0},
              d10PulseVar_{NVS_KEY(NvsKeys::Io::IO_D10PM),"d10_pulse_ms","io/output/d10",ConfigType::Int32,&digitalCfg[10].pulseMs,ConfigPersistence::Persistent,0},
              d11NameVar_{NVS_KEY(NvsKeys::Io::IO_D11NM),"d11_name","io/output/d11",ConfigType::CharArray,(char*)digitalCfg[11].name,ConfigPersistence::Persistent,sizeof(digitalCfg[11].name)},
              d11BindingVar_{NVS_KEY(NvsKeys::Io::IO_D11BP),"binding_port","io/output/d11",ConfigType::UInt16,&digitalCfg[11].bindingPort,ConfigPersistence::Persistent,0},
              d11ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D11AH),"d11_active_high","io/output/d11",ConfigType::Bool,&digitalCfg[11].activeHigh,ConfigPersistence::Persistent,0},
              d11InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D11IN),"d11_initial_on","io/output/d11",ConfigType::Bool,&digitalCfg[11].initialOn,ConfigPersistence::Persistent,0},
              d11RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D11RT),"retain_on_warm_reboot","io/output/d11",ConfigType::Bool,&digitalCfg[11].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d11MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D11MO),"d11_momentary","io/output/d11",ConfigType::Bool,&digitalCfg[11].momentary,ConfigPersistence::Persistent,0},
              d11PulseVar_{NVS_KEY(NvsKeys::Io::IO_D11PM),"d11_pulse_ms","io/output/d11",ConfigType::Int32,&digitalCfg[11].pulseMs,ConfigPersistence::Persistent,0},
              d12NameVar_{NVS_KEY(NvsKeys::Io::IO_D12NM),"d12_name","io/output/d12",ConfigType::CharArray,(char*)digitalCfg[12].name,ConfigPersistence::Persistent,sizeof(digitalCfg[12].name)},
              d12BindingVar_{NVS_KEY(NvsKeys::Io::IO_D12BP),"binding_port","io/output/d12",ConfigType::UInt16,&digitalCfg[12].bindingPort,ConfigPersistence::Persistent,0},
              d12ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D12AH),"d12_active_high","io/output/d12",ConfigType::Bool,&digitalCfg[12].activeHigh,ConfigPersistence::Persistent,0},
              d12InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D12IN),"d12_initial_on","io/output/d12",ConfigType::Bool,&digitalCfg[12].initialOn,ConfigPersistence::Persistent,0},
              d12RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D12RT),"retain_on_warm_reboot","io/output/d12",ConfigType::Bool,&digitalCfg[12].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d12MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D12MO),"d12_momentary","io/output/d12",ConfigType::Bool,&digitalCfg[12].momentary,ConfigPersistence::Persistent,0},
              d12PulseVar_{NVS_KEY(NvsKeys::Io::IO_D12PM),"d12_pulse_ms","io/output/d12",ConfigType::Int32,&digitalCfg[12].pulseMs,ConfigPersistence::Persistent,0},
              d13NameVar_{NVS_KEY(NvsKeys::Io::IO_D13NM),"d13_name","io/output/d13",ConfigType::CharArray,(char*)digitalCfg[13].name,ConfigPersistence::Persistent,sizeof(digitalCfg[13].name)},
              d13BindingVar_{NVS_KEY(NvsKeys::Io::IO_D13BP),"binding_port","io/output/d13",ConfigType::UInt16,&digitalCfg[13].bindingPort,ConfigPersistence::Persistent,0},
              d13ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D13AH),"d13_active_high","io/output/d13",ConfigType::Bool,&digitalCfg[13].activeHigh,ConfigPersistence::Persistent,0},
              d13InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D13IN),"d13_initial_on","io/output/d13",ConfigType::Bool,&digitalCfg[13].initialOn,ConfigPersistence::Persistent,0},
              d13RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D13RT),"retain_on_warm_reboot","io/output/d13",ConfigType::Bool,&digitalCfg[13].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d13MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D13MO),"d13_momentary","io/output/d13",ConfigType::Bool,&digitalCfg[13].momentary,ConfigPersistence::Persistent,0},
              d13PulseVar_{NVS_KEY(NvsKeys::Io::IO_D13PM),"d13_pulse_ms","io/output/d13",ConfigType::Int32,&digitalCfg[13].pulseMs,ConfigPersistence::Persistent,0},
              d14NameVar_{NVS_KEY(NvsKeys::Io::IO_D14NM),"d14_name","io/output/d14",ConfigType::CharArray,(char*)digitalCfg[14].name,ConfigPersistence::Persistent,sizeof(digitalCfg[14].name)},
              d14BindingVar_{NVS_KEY(NvsKeys::Io::IO_D14BP),"binding_port","io/output/d14",ConfigType::UInt16,&digitalCfg[14].bindingPort,ConfigPersistence::Persistent,0},
              d14ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D14AH),"d14_active_high","io/output/d14",ConfigType::Bool,&digitalCfg[14].activeHigh,ConfigPersistence::Persistent,0},
              d14InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D14IN),"d14_initial_on","io/output/d14",ConfigType::Bool,&digitalCfg[14].initialOn,ConfigPersistence::Persistent,0},
              d14RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D14RT),"retain_on_warm_reboot","io/output/d14",ConfigType::Bool,&digitalCfg[14].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d14MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D14MO),"d14_momentary","io/output/d14",ConfigType::Bool,&digitalCfg[14].momentary,ConfigPersistence::Persistent,0},
              d14PulseVar_{NVS_KEY(NvsKeys::Io::IO_D14PM),"d14_pulse_ms","io/output/d14",ConfigType::Int32,&digitalCfg[14].pulseMs,ConfigPersistence::Persistent,0},
              d15NameVar_{NVS_KEY(NvsKeys::Io::IO_D15NM),"d15_name","io/output/d15",ConfigType::CharArray,(char*)digitalCfg[15].name,ConfigPersistence::Persistent,sizeof(digitalCfg[15].name)},
              d15BindingVar_{NVS_KEY(NvsKeys::Io::IO_D15BP),"binding_port","io/output/d15",ConfigType::UInt16,&digitalCfg[15].bindingPort,ConfigPersistence::Persistent,0},
              d15ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D15AH),"d15_active_high","io/output/d15",ConfigType::Bool,&digitalCfg[15].activeHigh,ConfigPersistence::Persistent,0},
              d15InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D15IN),"d15_initial_on","io/output/d15",ConfigType::Bool,&digitalCfg[15].initialOn,ConfigPersistence::Persistent,0},
              d15RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D15RT),"retain_on_warm_reboot","io/output/d15",ConfigType::Bool,&digitalCfg[15].retainOnWarmReboot,ConfigPersistence::Persistent,0},
              d15MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D15MO),"d15_momentary","io/output/d15",ConfigType::Bool,&digitalCfg[15].momentary,ConfigPersistence::Persistent,0},
              d15PulseVar_{NVS_KEY(NvsKeys::Io::IO_D15PM),"d15_pulse_ms","io/output/d15",ConfigType::Int32,&digitalCfg[15].pulseMs,ConfigPersistence::Persistent,0}
        {
        }
    };

    IOModuleConfig cfgData_{};
    IOAnalogSlotConfig analogCfg_[ANALOG_CFG_STORAGE_SLOTS]{};
    IODigitalInputSlotConfig digitalInCfg_[DIGITAL_INPUT_CFG_STORAGE_SLOTS]{};
    IODigitalOutputSlotConfig digitalCfg_[DIGITAL_CFG_STORAGE_SLOTS]{};
    const IOBindingPortSpec* bindingPorts_ = nullptr;
    uint8_t bindingPortCount_ = 0;

    ConfigStore* cfgStore_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    DataStore* dataStore_ = nullptr;
    MqttConfigRouteProducer cfgMqttPub_{};
    bool cfgMqttPubConfigured_ = false;

    IORegistry registry_{};
    IOScheduler scheduler_{};
    I2CBus i2cBus_{};
    Ds2484Bus ds2484Bus_{&i2cBus_};

    // GPIO bit-bang 1-Wire buses provided by the profile (board pins).
    OneWireBus* oneWireGpio1_ = nullptr;
    OneWireBus* oneWireGpio2_ = nullptr;
    // Resolved bus carrying each DS18B20 sensor (any of the enabled buses).
    IOneWireBus* oneWireWater_ = nullptr;
    IOneWireBus* oneWireAir_ = nullptr;
    uint8_t oneWireWaterAddr_[8] = {0};
    uint8_t oneWireAirAddr_[8] = {0};
    bool oneWireWaterAddrValid_ = false;
    bool oneWireAirAddrValid_ = false;

    IOAnalogProvider analogProviders_[IO_SRC_COUNT]{};
    IOMaskProvider ledMaskProvider_{};
    Pcf8574MaskEndpoint* ledMaskEp_ = nullptr;
    Pcf8574Driver* pcfDriver_ = nullptr;
    Tca9554Driver* tcaDriver_ = nullptr;
    Mcp23017Driver* mcpDriver_ = nullptr;
    IOServiceV2 ioSvc_{
        ServiceBinding::bind<&IOModule::ioCount_>,
        ServiceBinding::bind<&IOModule::ioIdAt_>,
        ServiceBinding::bind<&IOModule::ioMeta_>,
        ServiceBinding::bind<&IOModule::ioReadValue_>,
        ServiceBinding::bind<&IOModule::ioReadDigital_>,
        ServiceBinding::bind<&IOModule::ioWriteDigital_>,
        ServiceBinding::bind<&IOModule::ioReadAnalog_>,
        ServiceBinding::bind<&IOModule::ioTick_>,
        ServiceBinding::bind<&IOModule::ioLastCycle_>,
        ServiceBinding::bind<&IOModule::ioSensorStatus_>,
        ServiceBinding::bind<&IOModule::ioListInvalidSensors_>,
        ServiceBinding::bind<&IOModule::ioBackendInfo_>,
        this
    };
    StatusLedsService statusLedsSvc_{
        ServiceBinding::bind<&IOModule::setLedMask_>,
        ServiceBinding::bind<&IOModule::getLedMaskSvc_>,
        this
    };
    bool pcfLastEnabled_ = false;
    uint8_t pcfLogicalMask_ = 0;
    bool pcfLogicalValid_ = false;
    IoCycleInfo* lastCycle_ = nullptr;

    AnalogSlot* analogSlots_ = nullptr;
    DigitalSlot* digitalSlots_ = nullptr;
    AnalogSensorEndpoint* analogEndpointPool_ = nullptr;
    uint8_t (*digitalSensorEndpointPool_)[sizeof(DigitalSensorEndpoint)] = nullptr;
    uint8_t (*digitalActuatorEndpointPool_)[sizeof(DigitalActuatorEndpoint)] = nullptr;
    uint8_t (*gpioDriverPool_)[sizeof(GpioDriver)] = nullptr;
    alignas(PcntCounterDriver) uint8_t gpioCounterDriverPool_[MAX_DIGITAL_INPUTS][sizeof(PcntCounterDriver)]{};
    alignas(Ads1115Driver) uint8_t adsDriverPool_[2][sizeof(Ads1115Driver)]{};
    alignas(Ds18b20Driver) uint8_t dsDriverPool_[2][sizeof(Ds18b20Driver)]{};
    alignas(Sht40Driver) uint8_t sht40DriverPool_[1][sizeof(Sht40Driver)]{};
    alignas(Bmp280Driver) uint8_t bmp280DriverPool_[1][sizeof(Bmp280Driver)]{};
    alignas(Bme680Driver) uint8_t bme680DriverPool_[1][sizeof(Bme680Driver)]{};
    alignas(Ina226Driver) uint8_t ina226DriverPool_[1][sizeof(Ina226Driver)]{};
    alignas(Ina228Driver) uint8_t ina228DriverPool_[1][sizeof(Ina228Driver)]{};
    alignas(Pcf8574Driver) uint8_t pcfDriverPool_[1][sizeof(Pcf8574Driver)]{};
    alignas(Tca9554Driver) uint8_t tcaDriverPool_[1][sizeof(Tca9554Driver)]{};
    alignas(Mcp23017Driver) uint8_t mcpDriverPool_[1][sizeof(Mcp23017Driver)]{};
    alignas(Pcf8574MaskEndpoint) uint8_t maskEndpointPool_[1][sizeof(Pcf8574MaskEndpoint)]{};
    uint8_t analogEndpointPoolUsed_ = 0;
    uint8_t digitalSensorEndpointPoolUsed_ = 0;
    uint8_t digitalActuatorEndpointPoolUsed_ = 0;
    uint8_t gpioDriverPoolUsed_ = 0;
    uint8_t gpioCounterDriverPoolUsed_ = 0;
    uint8_t pcfBitDriverPoolUsed_ = 0;
    uint8_t tcaBitDriverPoolUsed_ = 0;
    uint8_t mcpBitDriverPoolUsed_ = 0;
    uint8_t adsDriverPoolUsed_ = 0;
    uint8_t dsDriverPoolUsed_ = 0;
    uint8_t sht40DriverPoolUsed_ = 0;
    uint8_t bmp280DriverPoolUsed_ = 0;
    uint8_t bme680DriverPoolUsed_ = 0;
    uint8_t ina226DriverPoolUsed_ = 0;
    uint8_t ina228DriverPoolUsed_ = 0;
    uint8_t pcfDriverPoolUsed_ = 0;
    uint8_t tcaDriverPoolUsed_ = 0;
    uint8_t mcpDriverPoolUsed_ = 0;
    uint8_t maskEndpointPoolUsed_ = 0;
    bool runtimeReady_ = false;
    bool runtimeInitAttempted_ = false;
    int32_t boardDefaultI2cSda_ = FLOW_WIRDEF_IO_SDA;
    int32_t boardDefaultI2cScl_ = FLOW_WIRDEF_IO_SCL;
    const char* boardProfileName_ = "unknown";
    bool pcfEnableNeedsReinitWarned_ = false;
    uint32_t counterTraceLastMs_ = 0;
    uint32_t analogCalcLogLastMs_[3]{0, 0, 0};
    int32_t* analogPrecisionLast_ = nullptr;
    float* digitalCounterLastConfigTotals_ = nullptr;
    bool analogPrecisionLastInit_ = false;
    uint32_t analogConfigDirtyMask_ = 0;
    ExtraAnalogConfigVars* extraAnalogCfgVars_ = nullptr;
    ExtraDigitalInputModeConfigVars* extraDigitalInputModeCfgVars_ = nullptr;
    ExtraDigitalCounterConfigVars* extraDigitalCounterCfgVars_ = nullptr;
    ExtraDigitalOutputConfigVars* extraDigitalOutputCfgVars_ = nullptr;

    ConfigVariable<bool,0> enabledVar_ { NVS_KEY(NvsKeys::Io::IO_EN),"enabled","io",ConfigType::Bool,&cfgData_.enabled,ConfigPersistence::Persistent,0 };
#if defined(FLOW_BOARD_FLOWIO_S3)
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA_S3),"sda","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL_S3),"scl","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
#else
    ConfigVariable<int32_t,0> i2cSdaVar_ { NVS_KEY(NvsKeys::Io::IO_SDA),"sda","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cSda,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> i2cSclVar_ { NVS_KEY(NvsKeys::Io::IO_SCL),"scl","io/drivers/bus",ConfigType::Int32,&cfgData_.i2cScl,ConfigPersistence::Persistent,0 };
#endif
    ConfigVariable<int32_t,0> adsPollVar_ { NVS_KEY(NvsKeys::Io::IO_ADS),"poll_ms","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> dsPollVar_ { NVS_KEY(NvsKeys::Io::IO_DS),"poll_ms","io/drivers/ds18b20",ConfigType::Int32,&cfgData_.dsPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> digitalPollVar_ { NVS_KEY(NvsKeys::Io::IO_DIN),"poll_ms","io/drivers/gpio",ConfigType::Int32,&cfgData_.digitalPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsInternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AIAD),"address","io/drivers/ads1115_int",ConfigType::UInt8,&cfgData_.adsInternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> adsExternalAddrVar_ { NVS_KEY(NvsKeys::Io::IO_AEAD),"address","io/drivers/ads1115_ext",ConfigType::UInt8,&cfgData_.adsExternalAddr,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsGainVar_ { NVS_KEY(NvsKeys::Io::IO_AGAI),"gain","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsGain,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> adsRateVar_ { NVS_KEY(NvsKeys::Io::IO_ARAT),"rate","io/drivers/ads1115",ConfigType::Int32,&cfgData_.adsRate,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> sht40EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_SHTEN),"enabled","io/drivers/sht40",ConfigType::Bool,&cfgData_.sht40Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> sht40AddressVar_ { NVS_KEY(NvsKeys::Io::IO_SHTAD),"address","io/drivers/sht40",ConfigType::UInt8,&cfgData_.sht40Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> sht40PollVar_ { NVS_KEY(NvsKeys::Io::IO_SHTPL),"poll_ms","io/drivers/sht40",ConfigType::Int32,&cfgData_.sht40PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bmp280EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMPEN),"enabled","io/drivers/bmp280",ConfigType::Bool,&cfgData_.bmp280Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bmp280AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMPAD),"address","io/drivers/bmp280",ConfigType::UInt8,&cfgData_.bmp280Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bmp280PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMPPL),"poll_ms","io/drivers/bmp280",ConfigType::Int32,&cfgData_.bmp280PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> bme680EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_BMEEN),"enabled","io/drivers/bme680",ConfigType::Bool,&cfgData_.bme680Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> bme680AddressVar_ { NVS_KEY(NvsKeys::Io::IO_BMEAD),"address","io/drivers/bme680",ConfigType::UInt8,&cfgData_.bme680Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> bme680PollVar_ { NVS_KEY(NvsKeys::Io::IO_BMEPL),"poll_ms","io/drivers/bme680",ConfigType::Int32,&cfgData_.bme680PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> powermonEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_PMEN),"enabled","io/drivers/powermon",ConfigType::Bool,&cfgData_.powermonEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> powermonModelVar_ { NVS_KEY(NvsKeys::Io::IO_PMMD),"model","io/drivers/powermon",ConfigType::UInt8,&cfgData_.powermonModel,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> powermonAddressVar_ { NVS_KEY(NvsKeys::Io::IO_PMAD),"address","io/drivers/powermon",ConfigType::UInt8,&cfgData_.powermonAddress,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> powermonPollVar_ { NVS_KEY(NvsKeys::Io::IO_PMPL),"poll_ms","io/drivers/powermon",ConfigType::Int32,&cfgData_.powermonPollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<float,0> powermonShuntOhmsVar_ { NVS_KEY(NvsKeys::Io::IO_PMSH),"shunt_ohms","io/drivers/powermon",ConfigType::Float,&cfgData_.powermonShuntOhms,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_PCFEN),"enabled","io/drivers/pcf857x",ConfigType::Bool,&cfgData_.pcfEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfAddressVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAD),"address","io/drivers/pcf857x",ConfigType::UInt8,&cfgData_.pcfAddress,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> pcfMaskDefaultVar_ { NVS_KEY(NvsKeys::Io::IO_PCFMK),"mask_default","io/drivers/pcf857x",ConfigType::UInt8,&cfgData_.pcfMaskDefault,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> pcfActiveLowVar_ { NVS_KEY(NvsKeys::Io::IO_PCFAL),"active_low","io/drivers/pcf857x",ConfigType::Bool,&cfgData_.pcfActiveLow,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> mcp23017EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_MCPEN),"enabled","io/drivers/mcp23017",ConfigType::Bool,&cfgData_.mcp23017Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> mcp23017AddressVar_ { NVS_KEY(NvsKeys::Io::IO_MCPAD),"address","io/drivers/mcp23017",ConfigType::UInt8,&cfgData_.mcp23017Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> ds2484EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_DS24EN),"enabled","io/drivers/ds2484",ConfigType::Bool,&cfgData_.ds2484Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<uint8_t,0> ds2484AddressVar_ { NVS_KEY(NvsKeys::Io::IO_DS24AD),"address","io/drivers/ds2484",ConfigType::UInt8,&cfgData_.ds2484Address,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> ds2484PollVar_ { NVS_KEY(NvsKeys::Io::IO_DS24PL),"poll_ms","io/drivers/ds2484",ConfigType::Int32,&cfgData_.ds2484PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> oneWire1EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_OW1EN),"enabled","io/drivers/1wire_int1",ConfigType::Bool,&cfgData_.oneWire1Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> oneWire1GpioVar_ { NVS_KEY(NvsKeys::Io::IO_OW1GP),"gpio","io/drivers/1wire_int1",ConfigType::Int32,&cfgData_.oneWire1Gpio,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> oneWire1PollVar_ { NVS_KEY(NvsKeys::Io::IO_OW1PL),"poll_ms","io/drivers/1wire_int1",ConfigType::Int32,&cfgData_.oneWire1PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<bool,0> oneWire2EnabledVar_ { NVS_KEY(NvsKeys::Io::IO_OW2EN),"enabled","io/drivers/1wire_int2",ConfigType::Bool,&cfgData_.oneWire2Enabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> oneWire2GpioVar_ { NVS_KEY(NvsKeys::Io::IO_OW2GP),"gpio","io/drivers/1wire_int2",ConfigType::Int32,&cfgData_.oneWire2Gpio,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> oneWire2PollVar_ { NVS_KEY(NvsKeys::Io::IO_OW2PL),"poll_ms","io/drivers/1wire_int2",ConfigType::Int32,&cfgData_.oneWire2PollMs,ConfigPersistence::Persistent,0 };
    ConfigVariable<char,0> dsWaterRomVar_ { NVS_KEY(NvsKeys::Io::IO_DSWR),"water_rom","io/drivers/ds18b20",ConfigType::CharArray,(char*)cfgData_.dsWaterRom,ConfigPersistence::Persistent,sizeof(cfgData_.dsWaterRom) };
    ConfigVariable<char,0> dsAirRomVar_ { NVS_KEY(NvsKeys::Io::IO_DSAR),"air_rom","io/drivers/ds18b20",ConfigType::CharArray,(char*)cfgData_.dsAirRom,ConfigPersistence::Persistent,sizeof(cfgData_.dsAirRom) };
    ConfigVariable<bool,0> traceEnabledVar_ { NVS_KEY(NvsKeys::Io::IO_TREN),"trace_enabled","io/debug",ConfigType::Bool,&cfgData_.traceEnabled,ConfigPersistence::Persistent,0 };
    ConfigVariable<int32_t,0> tracePeriodVar_ { NVS_KEY(NvsKeys::Io::IO_TRMS),"trace_period_ms","io/debug",ConfigType::Int32,&cfgData_.tracePeriodMs,ConfigPersistence::Persistent,0 };

#define FLOW_IO_ANALOG_CFG_DECL(INDEX, SLOT_STR, KEYNM, KEYBP, KEYC0, KEYC1, KEYP) \
    ConfigVariable<char,0> a##INDEX##NameVar_{NVS_KEY(NvsKeys::Io::KEYNM),"a" SLOT_STR "_name","io/input/a" SLOT_STR,ConfigType::CharArray,(char*)analogCfg_[INDEX].name,ConfigPersistence::Persistent,sizeof(analogCfg_[INDEX].name)}; \
    ConfigVariable<PhysicalPortId,0> a##INDEX##BindingVar_{NVS_KEY(NvsKeys::Io::KEYBP),"binding_port","io/input/a" SLOT_STR,ConfigType::UInt16,&analogCfg_[INDEX].bindingPort,ConfigPersistence::Persistent,0}; \
    ConfigVariable<float,0> a##INDEX##C0Var_{NVS_KEY(NvsKeys::Io::KEYC0),"a" SLOT_STR "_c0","io/input/a" SLOT_STR,ConfigType::Float,&analogCfg_[INDEX].c0,ConfigPersistence::Persistent,0}; \
    ConfigVariable<float,0> a##INDEX##C1Var_{NVS_KEY(NvsKeys::Io::KEYC1),"a" SLOT_STR "_c1","io/input/a" SLOT_STR,ConfigType::Float,&analogCfg_[INDEX].c1,ConfigPersistence::Persistent,0}; \
    ConfigVariable<int32_t,0> a##INDEX##PrecVar_{NVS_KEY(NvsKeys::Io::KEYP),"a" SLOT_STR "_prec","io/input/a" SLOT_STR,ConfigType::Int32,&analogCfg_[INDEX].precision,ConfigPersistence::Persistent,0};

    FLOW_IO_ANALOG_CFG_DECL(0, "00", IO_A0NM, IO_A0BP, IO_A00, IO_A01, IO_A0P)
    FLOW_IO_ANALOG_CFG_DECL(1, "01", IO_A1NM, IO_A1BP, IO_A10, IO_A11, IO_A1P)
    FLOW_IO_ANALOG_CFG_DECL(2, "02", IO_A2NM, IO_A2BP, IO_A20, IO_A21, IO_A2P)
    FLOW_IO_ANALOG_CFG_DECL(3, "03", IO_A3NM, IO_A3BP, IO_A30, IO_A31, IO_A3P)
    FLOW_IO_ANALOG_CFG_DECL(4, "04", IO_A4NM, IO_A4BP, IO_A40, IO_A41, IO_A4P)
    FLOW_IO_ANALOG_CFG_DECL(5, "05", IO_A5NM, IO_A5BP, IO_A50, IO_A51, IO_A5P)
#undef FLOW_IO_ANALOG_CFG_DECL

    ConfigVariable<char,0> i0NameVar_{NVS_KEY(NvsKeys::Io::IO_I0NM),"i00_name","io/input/i00",ConfigType::CharArray,(char*)digitalInCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[0].name)};
    ConfigVariable<PhysicalPortId,0> i0BindingVar_{NVS_KEY(NvsKeys::Io::IO_I0BP),"binding_port","io/input/i00",ConfigType::UInt16,&digitalInCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I0AH),"i00_active_high","io/input/i00",ConfigType::Bool,&digitalInCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i0PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I0PU),"i00_pull_mode","io/input/i00",ConfigType::UInt8,&digitalInCfg_[0].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i0EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I0ED),"edge_mode","io/input/i00",ConfigType::UInt8,&digitalInCfg_[0].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i0CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I0DB),"counter_debounce_us","io/input/i00",ConfigType::Int32,&digitalInCfg_[0].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i0C0Var_{NVS_KEY(NvsKeys::Io::IO_I0C0),"i00_c0","io/input/i00",ConfigType::Float,&digitalInCfg_[0].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i0PrecVar_{NVS_KEY(NvsKeys::Io::IO_I0P),"i00_prec","io/input/i00",ConfigType::Int32,&digitalInCfg_[0].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i1NameVar_{NVS_KEY(NvsKeys::Io::IO_I1NM),"i01_name","io/input/i01",ConfigType::CharArray,(char*)digitalInCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[1].name)};
    ConfigVariable<PhysicalPortId,0> i1BindingVar_{NVS_KEY(NvsKeys::Io::IO_I1BP),"binding_port","io/input/i01",ConfigType::UInt16,&digitalInCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I1AH),"i01_active_high","io/input/i01",ConfigType::Bool,&digitalInCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i1PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I1PU),"i01_pull_mode","io/input/i01",ConfigType::UInt8,&digitalInCfg_[1].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i1EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I1ED),"edge_mode","io/input/i01",ConfigType::UInt8,&digitalInCfg_[1].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i1CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I1DB),"counter_debounce_us","io/input/i01",ConfigType::Int32,&digitalInCfg_[1].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i1C0Var_{NVS_KEY(NvsKeys::Io::IO_I1C0),"i01_c0","io/input/i01",ConfigType::Float,&digitalInCfg_[1].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i1PrecVar_{NVS_KEY(NvsKeys::Io::IO_I1P),"i01_prec","io/input/i01",ConfigType::Int32,&digitalInCfg_[1].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i2NameVar_{NVS_KEY(NvsKeys::Io::IO_I2NM),"i02_name","io/input/i02",ConfigType::CharArray,(char*)digitalInCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[2].name)};
    ConfigVariable<PhysicalPortId,0> i2BindingVar_{NVS_KEY(NvsKeys::Io::IO_I2BP),"binding_port","io/input/i02",ConfigType::UInt16,&digitalInCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I2AH),"i02_active_high","io/input/i02",ConfigType::Bool,&digitalInCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i2PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I2PU),"i02_pull_mode","io/input/i02",ConfigType::UInt8,&digitalInCfg_[2].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i2EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I2ED),"edge_mode","io/input/i02",ConfigType::UInt8,&digitalInCfg_[2].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i2CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I2DB),"counter_debounce_us","io/input/i02",ConfigType::Int32,&digitalInCfg_[2].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i2C0Var_{NVS_KEY(NvsKeys::Io::IO_I2C0),"i02_c0","io/input/i02",ConfigType::Float,&digitalInCfg_[2].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i2PrecVar_{NVS_KEY(NvsKeys::Io::IO_I2P),"i02_prec","io/input/i02",ConfigType::Int32,&digitalInCfg_[2].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i3NameVar_{NVS_KEY(NvsKeys::Io::IO_I3NM),"i03_name","io/input/i03",ConfigType::CharArray,(char*)digitalInCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[3].name)};
    ConfigVariable<PhysicalPortId,0> i3BindingVar_{NVS_KEY(NvsKeys::Io::IO_I3BP),"binding_port","io/input/i03",ConfigType::UInt16,&digitalInCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I3AH),"i03_active_high","io/input/i03",ConfigType::Bool,&digitalInCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i3PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I3PU),"i03_pull_mode","io/input/i03",ConfigType::UInt8,&digitalInCfg_[3].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i3EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I3ED),"edge_mode","io/input/i03",ConfigType::UInt8,&digitalInCfg_[3].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i3CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I3DB),"counter_debounce_us","io/input/i03",ConfigType::Int32,&digitalInCfg_[3].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i3C0Var_{NVS_KEY(NvsKeys::Io::IO_I3C0),"i03_c0","io/input/i03",ConfigType::Float,&digitalInCfg_[3].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i3PrecVar_{NVS_KEY(NvsKeys::Io::IO_I3P),"i03_prec","io/input/i03",ConfigType::Int32,&digitalInCfg_[3].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i4NameVar_{NVS_KEY(NvsKeys::Io::IO_I4NM),"i04_name","io/input/i04",ConfigType::CharArray,(char*)digitalInCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[4].name)};
    ConfigVariable<PhysicalPortId,0> i4BindingVar_{NVS_KEY(NvsKeys::Io::IO_I4BP),"binding_port","io/input/i04",ConfigType::UInt16,&digitalInCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I4AH),"i04_active_high","io/input/i04",ConfigType::Bool,&digitalInCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i4PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I4PU),"i04_pull_mode","io/input/i04",ConfigType::UInt8,&digitalInCfg_[4].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i4EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I4ED),"edge_mode","io/input/i04",ConfigType::UInt8,&digitalInCfg_[4].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i4CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I4DB),"counter_debounce_us","io/input/i04",ConfigType::Int32,&digitalInCfg_[4].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i4C0Var_{NVS_KEY(NvsKeys::Io::IO_I4C0),"i04_c0","io/input/i04",ConfigType::Float,&digitalInCfg_[4].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i4PrecVar_{NVS_KEY(NvsKeys::Io::IO_I4P),"i04_prec","io/input/i04",ConfigType::Int32,&digitalInCfg_[4].precision,ConfigPersistence::Persistent,0};
    ConfigVariable<char,0> i5NameVar_{NVS_KEY(NvsKeys::Io::IO_I5NM),"i05_name","io/input/i05",ConfigType::CharArray,(char*)digitalInCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[5].name)};
    ConfigVariable<PhysicalPortId,0> i5BindingVar_{NVS_KEY(NvsKeys::Io::IO_I5BP),"binding_port","io/input/i05",ConfigType::UInt16,&digitalInCfg_[5].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i5ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I5AH),"i05_active_high","io/input/i05",ConfigType::Bool,&digitalInCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i5PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I5PU),"i05_pull_mode","io/input/i05",ConfigType::UInt8,&digitalInCfg_[5].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i5EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I5ED),"edge_mode","io/input/i05",ConfigType::UInt8,&digitalInCfg_[5].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i5CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I5DB),"counter_debounce_us","io/input/i05",ConfigType::Int32,&digitalInCfg_[5].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i5C0Var_{NVS_KEY(NvsKeys::Io::IO_I5C0),"i05_c0","io/input/i05",ConfigType::Float,&digitalInCfg_[5].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i5PrecVar_{NVS_KEY(NvsKeys::Io::IO_I5P),"i05_prec","io/input/i05",ConfigType::Int32,&digitalInCfg_[5].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i6NameVar_{NVS_KEY(NvsKeys::Io::IO_I6NM),"i06_name","io/input/i06",ConfigType::CharArray,(char*)digitalInCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[6].name)};
    ConfigVariable<PhysicalPortId,0> i6BindingVar_{NVS_KEY(NvsKeys::Io::IO_I6BP),"binding_port","io/input/i06",ConfigType::UInt16,&digitalInCfg_[6].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i6ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I6AH),"i06_active_high","io/input/i06",ConfigType::Bool,&digitalInCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i6PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I6PU),"i06_pull_mode","io/input/i06",ConfigType::UInt8,&digitalInCfg_[6].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i6EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I6ED),"edge_mode","io/input/i06",ConfigType::UInt8,&digitalInCfg_[6].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i6CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I6DB),"counter_debounce_us","io/input/i06",ConfigType::Int32,&digitalInCfg_[6].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i6C0Var_{NVS_KEY(NvsKeys::Io::IO_I6C0),"i06_c0","io/input/i06",ConfigType::Float,&digitalInCfg_[6].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i6PrecVar_{NVS_KEY(NvsKeys::Io::IO_I6P),"i06_prec","io/input/i06",ConfigType::Int32,&digitalInCfg_[6].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> i7NameVar_{NVS_KEY(NvsKeys::Io::IO_I7NM),"i07_name","io/input/i07",ConfigType::CharArray,(char*)digitalInCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalInCfg_[7].name)};
    ConfigVariable<PhysicalPortId,0> i7BindingVar_{NVS_KEY(NvsKeys::Io::IO_I7BP),"binding_port","io/input/i07",ConfigType::UInt16,&digitalInCfg_[7].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> i7ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_I7AH),"i07_active_high","io/input/i07",ConfigType::Bool,&digitalInCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i7PullModeVar_{NVS_KEY(NvsKeys::Io::IO_I7PU),"i07_pull_mode","io/input/i07",ConfigType::UInt8,&digitalInCfg_[7].pullMode,ConfigPersistence::Persistent,0};
    ConfigVariable<uint8_t,0> i7EdgeModeVar_{NVS_KEY(NvsKeys::Io::IO_I7ED),"edge_mode","io/input/i07",ConfigType::UInt8,&digitalInCfg_[7].edgeMode,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i7CounterDebounceVar_{NVS_KEY(NvsKeys::Io::IO_I7DB),"counter_debounce_us","io/input/i07",ConfigType::Int32,&digitalInCfg_[7].counterDebounceUs,ConfigPersistence::Persistent,0};
    ConfigVariable<float,0> i7C0Var_{NVS_KEY(NvsKeys::Io::IO_I7C0),"i07_c0","io/input/i07",ConfigType::Float,&digitalInCfg_[7].c0,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> i7PrecVar_{NVS_KEY(NvsKeys::Io::IO_I7P),"i07_prec","io/input/i07",ConfigType::Int32,&digitalInCfg_[7].precision,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d0NameVar_{NVS_KEY(NvsKeys::Io::IO_D0NM),"d00_name","io/output/d00",ConfigType::CharArray,(char*)digitalCfg_[0].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[0].name)};
    ConfigVariable<PhysicalPortId,0> d0BindingVar_{NVS_KEY(NvsKeys::Io::IO_D0BP),"binding_port","io/output/d00",ConfigType::UInt16,&digitalCfg_[0].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D0AH),"d00_active_high","io/output/d00",ConfigType::Bool,&digitalCfg_[0].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D0IN),"d00_initial_on","io/output/d00",ConfigType::Bool,&digitalCfg_[0].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D0RT),"retain_on_warm_reboot","io/output/d00",ConfigType::Bool,&digitalCfg_[0].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d0MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D0MO),"d00_momentary","io/output/d00",ConfigType::Bool,&digitalCfg_[0].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d0PulseVar_{NVS_KEY(NvsKeys::Io::IO_D0PM),"d00_pulse_ms","io/output/d00",ConfigType::Int32,&digitalCfg_[0].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d1NameVar_{NVS_KEY(NvsKeys::Io::IO_D1NM),"d01_name","io/output/d01",ConfigType::CharArray,(char*)digitalCfg_[1].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[1].name)};
    ConfigVariable<PhysicalPortId,0> d1BindingVar_{NVS_KEY(NvsKeys::Io::IO_D1BP),"binding_port","io/output/d01",ConfigType::UInt16,&digitalCfg_[1].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D1AH),"d01_active_high","io/output/d01",ConfigType::Bool,&digitalCfg_[1].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D1IN),"d01_initial_on","io/output/d01",ConfigType::Bool,&digitalCfg_[1].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D1RT),"retain_on_warm_reboot","io/output/d01",ConfigType::Bool,&digitalCfg_[1].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d1MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D1MO),"d01_momentary","io/output/d01",ConfigType::Bool,&digitalCfg_[1].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d1PulseVar_{NVS_KEY(NvsKeys::Io::IO_D1PM),"d01_pulse_ms","io/output/d01",ConfigType::Int32,&digitalCfg_[1].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d2NameVar_{NVS_KEY(NvsKeys::Io::IO_D2NM),"d02_name","io/output/d02",ConfigType::CharArray,(char*)digitalCfg_[2].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[2].name)};
    ConfigVariable<PhysicalPortId,0> d2BindingVar_{NVS_KEY(NvsKeys::Io::IO_D2BP),"binding_port","io/output/d02",ConfigType::UInt16,&digitalCfg_[2].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D2AH),"d02_active_high","io/output/d02",ConfigType::Bool,&digitalCfg_[2].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D2IN),"d02_initial_on","io/output/d02",ConfigType::Bool,&digitalCfg_[2].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D2RT),"retain_on_warm_reboot","io/output/d02",ConfigType::Bool,&digitalCfg_[2].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d2MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D2MO),"d02_momentary","io/output/d02",ConfigType::Bool,&digitalCfg_[2].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d2PulseVar_{NVS_KEY(NvsKeys::Io::IO_D2PM),"d02_pulse_ms","io/output/d02",ConfigType::Int32,&digitalCfg_[2].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d3NameVar_{NVS_KEY(NvsKeys::Io::IO_D3NM),"d03_name","io/output/d03",ConfigType::CharArray,(char*)digitalCfg_[3].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[3].name)};
    ConfigVariable<PhysicalPortId,0> d3BindingVar_{NVS_KEY(NvsKeys::Io::IO_D3BP),"binding_port","io/output/d03",ConfigType::UInt16,&digitalCfg_[3].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D3AH),"d03_active_high","io/output/d03",ConfigType::Bool,&digitalCfg_[3].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D3IN),"d03_initial_on","io/output/d03",ConfigType::Bool,&digitalCfg_[3].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D3RT),"retain_on_warm_reboot","io/output/d03",ConfigType::Bool,&digitalCfg_[3].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d3MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D3MO),"d03_momentary","io/output/d03",ConfigType::Bool,&digitalCfg_[3].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d3PulseVar_{NVS_KEY(NvsKeys::Io::IO_D3PM),"d03_pulse_ms","io/output/d03",ConfigType::Int32,&digitalCfg_[3].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d4NameVar_{NVS_KEY(NvsKeys::Io::IO_D4NM),"d04_name","io/output/d04",ConfigType::CharArray,(char*)digitalCfg_[4].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[4].name)};
    ConfigVariable<PhysicalPortId,0> d4BindingVar_{NVS_KEY(NvsKeys::Io::IO_D4BP),"binding_port","io/output/d04",ConfigType::UInt16,&digitalCfg_[4].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D4AH),"d04_active_high","io/output/d04",ConfigType::Bool,&digitalCfg_[4].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D4IN),"d04_initial_on","io/output/d04",ConfigType::Bool,&digitalCfg_[4].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D4RT),"retain_on_warm_reboot","io/output/d04",ConfigType::Bool,&digitalCfg_[4].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d4MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D4MO),"d04_momentary","io/output/d04",ConfigType::Bool,&digitalCfg_[4].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d4PulseVar_{NVS_KEY(NvsKeys::Io::IO_D4PM),"d04_pulse_ms","io/output/d04",ConfigType::Int32,&digitalCfg_[4].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d5NameVar_{NVS_KEY(NvsKeys::Io::IO_D5NM),"d05_name","io/output/d05",ConfigType::CharArray,(char*)digitalCfg_[5].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[5].name)};
    ConfigVariable<PhysicalPortId,0> d5BindingVar_{NVS_KEY(NvsKeys::Io::IO_D5BP),"binding_port","io/output/d05",ConfigType::UInt16,&digitalCfg_[5].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D5AH),"d05_active_high","io/output/d05",ConfigType::Bool,&digitalCfg_[5].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D5IN),"d05_initial_on","io/output/d05",ConfigType::Bool,&digitalCfg_[5].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D5RT),"retain_on_warm_reboot","io/output/d05",ConfigType::Bool,&digitalCfg_[5].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d5MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D5MO),"d05_momentary","io/output/d05",ConfigType::Bool,&digitalCfg_[5].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d5PulseVar_{NVS_KEY(NvsKeys::Io::IO_D5PM),"d05_pulse_ms","io/output/d05",ConfigType::Int32,&digitalCfg_[5].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d6NameVar_{NVS_KEY(NvsKeys::Io::IO_D6NM),"d06_name","io/output/d06",ConfigType::CharArray,(char*)digitalCfg_[6].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[6].name)};
    ConfigVariable<PhysicalPortId,0> d6BindingVar_{NVS_KEY(NvsKeys::Io::IO_D6BP),"binding_port","io/output/d06",ConfigType::UInt16,&digitalCfg_[6].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D6AH),"d06_active_high","io/output/d06",ConfigType::Bool,&digitalCfg_[6].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D6IN),"d06_initial_on","io/output/d06",ConfigType::Bool,&digitalCfg_[6].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D6RT),"retain_on_warm_reboot","io/output/d06",ConfigType::Bool,&digitalCfg_[6].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d6MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D6MO),"d06_momentary","io/output/d06",ConfigType::Bool,&digitalCfg_[6].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d6PulseVar_{NVS_KEY(NvsKeys::Io::IO_D6PM),"d06_pulse_ms","io/output/d06",ConfigType::Int32,&digitalCfg_[6].pulseMs,ConfigPersistence::Persistent,0};

    ConfigVariable<char,0> d7NameVar_{NVS_KEY(NvsKeys::Io::IO_D7NM),"d07_name","io/output/d07",ConfigType::CharArray,(char*)digitalCfg_[7].name,ConfigPersistence::Persistent,sizeof(digitalCfg_[7].name)};
    ConfigVariable<PhysicalPortId,0> d7BindingVar_{NVS_KEY(NvsKeys::Io::IO_D7BP),"binding_port","io/output/d07",ConfigType::UInt16,&digitalCfg_[7].bindingPort,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7ActiveHighVar_{NVS_KEY(NvsKeys::Io::IO_D7AH),"d07_active_high","io/output/d07",ConfigType::Bool,&digitalCfg_[7].activeHigh,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7InitialOnVar_{NVS_KEY(NvsKeys::Io::IO_D7IN),"d07_initial_on","io/output/d07",ConfigType::Bool,&digitalCfg_[7].initialOn,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7RetainWarmVar_{NVS_KEY(NvsKeys::Io::IO_D7RT),"retain_on_warm_reboot","io/output/d07",ConfigType::Bool,&digitalCfg_[7].retainOnWarmReboot,ConfigPersistence::Persistent,0};
    ConfigVariable<bool,0> d7MomentaryVar_{NVS_KEY(NvsKeys::Io::IO_D7MO),"d07_momentary","io/output/d07",ConfigType::Bool,&digitalCfg_[7].momentary,ConfigPersistence::Persistent,0};
    ConfigVariable<int32_t,0> d7PulseVar_{NVS_KEY(NvsKeys::Io::IO_D7PM),"d07_pulse_ms","io/output/d07",ConfigType::Int32,&digitalCfg_[7].pulseMs,ConfigPersistence::Persistent,0};
};
