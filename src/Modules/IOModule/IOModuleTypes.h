#pragma once
/**
 * @file IOModuleTypes.h
 * @brief Public POD types used to describe IO topology without runtime allocation.
 */

#include <stdint.h>

#include <ADS1X15.h>  // FLOW_MODDEF_IO_AGAI (defaults generes) reference ADS1X15_GAIN_*.

#include "Core/Services/IIO.h"
#include "Core/WokwiDefaultOverrides.h"

typedef uint16_t BindingPointId;
typedef BindingPointId PhysicalPortId;
constexpr BindingPointId BINDING_POINT_NONE = 0u;
constexpr PhysicalPortId IO_PORT_INVALID = BINDING_POINT_NONE;

struct IOModuleConfig {
    bool enabled = FLOW_WIRDEF_IO_EN;
    int32_t i2cSda = FLOW_WIRDEF_IO_SDA;
    int32_t i2cScl = FLOW_WIRDEF_IO_SCL;
    int32_t adsPollMs = FLOW_MODDEF_IO_ADS;
    int32_t dsPollMs = FLOW_MODDEF_IO_DS;
    int32_t digitalPollMs = FLOW_MODDEF_IO_DIN;
    uint8_t adsInternalAddr = FLOW_WIRDEF_IO_AIAD;
    uint8_t adsExternalAddr = FLOW_WIRDEF_IO_AEAD;
    int32_t adsGain = FLOW_MODDEF_IO_AGAI;
    int32_t adsRate = FLOW_MODDEF_IO_ARAT;
    bool sht40Enabled = false;
    uint8_t sht40Address = 0x44;
    int32_t sht40PollMs = 2000;
    bool bmp280Enabled = false;
    uint8_t bmp280Address = 0x76;
    int32_t bmp280PollMs = 1000;
    bool bme680Enabled = false;
    uint8_t bme680Address = 0x77;
    int32_t bme680PollMs = 2000;
    // Moniteur de puissance unifie (INA226 ou INA228). Le modele choisit la puce
    // physique ; les grandeurs temperature/energie/charge restent inactives en 226.
    bool powermonEnabled = false;
    uint8_t powermonModel = 228;  // 226 ou 228.
    uint8_t powermonAddress = 0x40;
    int32_t powermonPollMs = 500;
    float powermonShuntOhms = 0.1f;
    bool pcfEnabled = FLOW_WIRDEF_IO_PCFEN;
    uint8_t pcfAddress = FLOW_WIRDEF_IO_PCFAD;
    uint8_t pcfMaskDefault = FLOW_WIRDEF_IO_PCFMK;
    bool pcfActiveLow = FLOW_WIRDEF_IO_PCFAL;
    bool mcp23017Enabled = true;
    uint8_t mcp23017Address = 0x21;
    // DS2484 I2C-to-1-Wire bridge.
    bool ds2484Enabled = false;
    uint8_t ds2484Address = 0x18;
    int32_t ds2484PollMs = 2000;
    // GPIO bit-bang 1-Wire buses (gpio < 0 means "use board default pin").
    bool oneWire1Enabled = true;
    int32_t oneWire1Gpio = -1;
    int32_t oneWire1PollMs = 2000;
    bool oneWire2Enabled = true;
    int32_t oneWire2Gpio = -1;
    int32_t oneWire2PollMs = 2000;
    // DS18B20 sensor->temperature assignment by ROM (hex "AA:BB:..."; empty = auto).
    char dsWaterRom[24] = {0};
    char dsAirRom[24] = {0};
    bool traceEnabled = FLOW_MODDEF_IO_TREN;
    int32_t tracePeriodMs = FLOW_MODDEF_IO_TRMS;
};

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

/** Direction bitmask of a binding port. */
enum IoPortDirMask : uint8_t {
    IO_PORT_DIR_IN = 0x01,
    IO_PORT_DIR_OUT = 0x02
};

/**
 * One physical binding point. The backend (IO_BACKEND_*) is the single
 * hardware identity; `channel` is the GPIO pin, expander bit, sensor channel
 * or 1-Wire bus index (DS18B20: 0 = water bus, 1 = air bus). Backend
 * capabilities (direction, channel range, labels) live in kBackendTraits.
 */
struct IOBindingPortSpec {
    PhysicalPortId portId = IO_PORT_INVALID;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t channel = 0;
    uint8_t flags = 0;          // IoPortDirMask bits.
    const char* name = nullptr; // Default display name ("DIN0", "EXIO1"...).
};

typedef void (*IOAnalogValueCallback)(void* ctx, float value);
typedef void (*IODigitalValueCallback)(void* ctx, bool value);
typedef void (*IODigitalCounterValueCallback)(void* ctx, int32_t value);

enum IODigitalPullMode : uint8_t {
    IO_PULL_NONE = 0,
    IO_PULL_UP = 1,
    IO_PULL_DOWN = 2
};

enum IODigitalInputMode : uint8_t {
    IO_DIGITAL_INPUT_STATE = 0,
    IO_DIGITAL_INPUT_COUNTER = 1
};

enum IODigitalEdgeMode : uint8_t {
    IO_EDGE_FALLING = 0,
    IO_EDGE_RISING = 1,
    IO_EDGE_BOTH = 2
};

enum class IOOutputStartupPolicy : uint8_t {
    ApplyInitial = 0,
    PreserveHardwareState = 1
};

struct IOAnalogDefinition {
    char id[24] = {0};
    /** Required explicit AI id in [IO_ID_AI_BASE..IO_ID_AI_BASE+MAX_ANALOG_ENDPOINTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    IOAnalogValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
};

struct IOAnalogSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
};

struct IODigitalOutputDefinition {
    char id[24] = {0};
    /** Required explicit DO id in [IO_ID_DO_BASE..IO_ID_DO_BASE+MAX_DIGITAL_OUTPUTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = false;
    bool initialOn = false;
    IOOutputStartupPolicy startupPolicy = IOOutputStartupPolicy::ApplyInitial;
    bool retainOnWarmReboot = false;
    bool momentary = false;
    uint16_t pulseMs = 500;
};

struct IODigitalOutputSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = false;
    bool initialOn = false;
    IOOutputStartupPolicy startupPolicy = IOOutputStartupPolicy::ApplyInitial;
    bool retainOnWarmReboot = false;
    bool momentary = false;
    int32_t pulseMs = 500;
};

struct IODigitalInputSlotConfig {
    char name[24] = {0};
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint8_t edgeMode = IO_EDGE_RISING;
    int32_t counterDebounceUs = 0;
    float c0 = 1.0f;
    float counterTotal = 0.0f;
    int32_t precision = 0;
};

struct IODigitalInputDefinition {
    char id[24] = {0};
    /** Required explicit DI id in [IO_ID_DI_BASE..IO_ID_DI_BASE+MAX_DIGITAL_INPUTS). */
    IoId ioId = IO_ID_INVALID;
    PhysicalPortId bindingPort = IO_PORT_INVALID;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint8_t edgeMode = IO_EDGE_RISING;
    uint32_t counterDebounceUs = 0;
    IODigitalValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
    IODigitalCounterValueCallback onCounterChanged = nullptr;
    void* onCounterCtx = nullptr;
};
