#pragma once
/**
 * @file IoBackendTraits.h
 * @brief Single compile-time description of every hardware backend (IO_BACKEND_*).
 *
 * This table is the only place that knows what a backend supports: direction,
 * analog capability, channel range, display labels and whether the driver has
 * a config on/off toggle. IOModule, the web interface and profile assemblies
 * all read it, so adding a backend is one enum value + one row here.
 */

#include <stdint.h>

#include "Modules/IOModule/IOModuleTypes.h"

struct IoBackendTraits {
    uint8_t backend;         // IO_BACKEND_*
    const char* label;       // Short display label ("ADS1115 int").
    const char* kindLabel;   // Stable JSON identifier ("ads1115_internal").
    uint8_t dirMask;         // Supported directions (IO_PORT_DIR_*).
    bool analog;             // Provides analog samples.
    uint8_t maxChannel;      // Highest accepted channel (inclusive).
    bool configurableToggle; // Driver has an on/off config toggle (backendInfo).
};

inline constexpr IoBackendTraits kBackendTraits[] = {
    // {backend, label, kindLabel, dirMask, analog, maxChannel, configurableToggle}
    {IO_BACKEND_GPIO,             "GPIO",        "gpio",                  IO_PORT_DIR_IN | IO_PORT_DIR_OUT, false, 48U, false},
    {IO_BACKEND_PCF8574,          "PCF8574",     "pcf8574_output",        IO_PORT_DIR_OUT,                  false, 7U,  true},
    {IO_BACKEND_ADS1115_INT,      "ADS1115 int", "ads1115_internal",      IO_PORT_DIR_IN,                   true,  3U,  false},
    {IO_BACKEND_ADS1115_EXT_DIFF, "ADS1115 ext", "ads1115_external_diff", IO_PORT_DIR_IN,                   true,  1U,  false},
    {IO_BACKEND_DS18B20,          "DS18B20",     "ds18b20",               IO_PORT_DIR_IN,                   true,  1U,  true},
    {IO_BACKEND_SHT40,            "SHT40",       "sht40",                 IO_PORT_DIR_IN,                   true,  1U,  true},
    {IO_BACKEND_BMP280,           "BMP280",      "bmp280",                IO_PORT_DIR_IN,                   true,  1U,  true},
    {IO_BACKEND_BME680,           "BME680",      "bme680",                IO_PORT_DIR_IN,                   true,  3U,  true},
    {IO_BACKEND_POWERMON,         "INA22x",      "powermon",              IO_PORT_DIR_IN,                   true,  7U,  true},
    {IO_BACKEND_TCA9554,          "TCA9554",     "tca9554_output",        IO_PORT_DIR_OUT,                  false, 7U,  false},
    {IO_BACKEND_MCP23017,         "MCP23017",    "mcp23017_output",       IO_PORT_DIR_OUT,                  false, 15U, true},
};

constexpr const IoBackendTraits* backendTraits(uint8_t backend)
{
    for (const IoBackendTraits& traits : kBackendTraits) {
        if (traits.backend == backend) return &traits;
    }
    return nullptr;
}

constexpr const char* ioBackendLabel(uint8_t backend)
{
    const IoBackendTraits* traits = backendTraits(backend);
    return traits ? traits->label : "unknown";
}

/**
 * Per-port kind identifier for the web API. GPIO and DS18B20 keep their
 * historical direction/bus-specific labels; every other backend has one label.
 */
constexpr const char* ioPortKindLabel(const IOBindingPortSpec& spec)
{
    if (spec.backend == IO_BACKEND_GPIO) {
        return (spec.flags & IO_PORT_DIR_OUT) ? "gpio_output" : "gpio_input";
    }
    if (spec.backend == IO_BACKEND_DS18B20) {
        return (spec.channel == 0U) ? "ds18b20_water" : "ds18b20_air";
    }
    const IoBackendTraits* traits = backendTraits(spec.backend);
    return traits ? traits->kindLabel : "none";
}

/** True when the port targets a valid backend/channel/direction combination. */
constexpr bool ioPortSpecValid(const IOBindingPortSpec& spec)
{
    const IoBackendTraits* traits = backendTraits(spec.backend);
    if (!traits) return false;
    if ((spec.flags & traits->dirMask) == 0U) return false;
    return spec.channel <= traits->maxChannel;
}
