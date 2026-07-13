#pragma once

#include "Domain/Pool/PoolIds.h"
#include "Modules/IOModule/IOModuleTypes.h"

namespace Profiles {
namespace Waveshare {
namespace IoLayout {

enum : PhysicalPortId {
    PortAdsInternal0    = 100, // ADS1115 interne, entree single-ended A0.
    PortAdsInternal1    = 101, // ADS1115 interne, entree single-ended A1.
    PortAdsInternal2    = 102, // ADS1115 interne, entree single-ended A2.
    PortAdsInternal3    = 103, // ADS1115 interne, entree single-ended A3.
    PortAdsExternal0    = 110, // ADS1115 externe, paire differentielle 0.
    PortAdsExternal1    = 111, // ADS1115 externe, paire differentielle 1.
    PortOneWire1        = 120, // DS18B20 bus 1.
    PortOneWire2        = 121, // DS18B20 bus 2.
    PortSht40Temp       = 130, // SHT40: temperature.
    PortSht40Humidity   = 131, // SHT40: humidite.
    PortBmp280Temp      = 132, // BMP280: temperature.
    PortBmp280Pressure  = 133, // BMP280: pression.
    PortBme680Temp      = 134, // BME680: temperature.
    PortBme680Humidity  = 135, // BME680: humidite.
    PortBme680Pressure  = 136, // BME680: pression.
    PortBme680Gas       = 137, // BME680: resistance gaz.
    PortPowermonShuntMv   = 143, // Moniteur puissance: tension shunt (mV).
    PortPowermonBusV      = 144, // Moniteur puissance: tension bus (V).
    PortPowermonCurrentMa = 145, // Moniteur puissance: courant (mA).
    PortPowermonPowerMw   = 146, // Moniteur puissance: puissance (mW).
    PortPowermonLoadV     = 147, // Moniteur puissance: tension charge (V).
    PortPowermonTemp      = 148, // Moniteur puissance: temperature (degC, INA228 seul).
    PortPowermonEnergy    = 149, // Moniteur puissance: energie (Wh, INA228 seul).
    PortPowermonCharge    = 150, // Moniteur puissance: charge (mAh, INA228 seul).
    PortDin0            = 200, // DIN0.
    PortDin1            = 201, // DIN1.
    PortDin2            = 202, // DIN2.
    PortDin3            = 203, // DIN3.
    PortDin4            = 204, // DIN4.
    PortDin5            = 205, // DIN5.
    PortDin6            = 206, // DIN6.
    PortDin7            = 207, // DIN7.
    PortExio1           = 300, // TCA9554 sortie bit 0.
    PortExio2           = 301, // TCA9554 sortie bit 1.
    PortExio3           = 302, // TCA9554 sortie bit 2.
    PortExio4           = 303, // TCA9554 sortie bit 3.
    PortExio5           = 304, // TCA9554 sortie bit 4.
    PortExio6           = 305, // TCA9554 sortie bit 5.
    PortExio7           = 306, // TCA9554 sortie bit 6.
    PortExio8           = 307, // TCA9554 sortie bit 7.
    PortMcpOut1         = 400, // MCP23017 sortie bit 0.
    PortMcpOut2         = 401, // MCP23017 sortie bit 1.
    PortMcpOut3         = 402, // MCP23017 sortie bit 2.
    PortMcpOut4         = 403, // MCP23017 sortie bit 3.
    PortMcpOut5         = 404, // MCP23017 sortie bit 4.
    PortMcpOut6         = 405, // MCP23017 sortie bit 5.
    PortMcpOut7         = 406, // MCP23017 sortie bit 6.
    PortMcpOut8         = 407, // MCP23017 sortie bit 7.
    PortMcpOut9         = 408, // MCP23017 sortie bit 8.
    PortMcpOut10        = 409, // MCP23017 sortie bit 9.
    PortMcpOut11        = 410, // MCP23017 sortie bit 10.
    PortMcpOut12        = 411, // MCP23017 sortie bit 11.
    PortMcpOut13        = 412, // MCP23017 sortie bit 12.
    PortMcpOut14        = 413, // MCP23017 sortie bit 13.
    PortMcpOut15        = 414, // MCP23017 sortie bit 14.
    PortMcpOut16        = 415  // MCP23017 sortie bit 15.
};

inline constexpr IOBindingPortSpec kBindingPorts[] = {
    // {portId, backend, channel, flags, name}
    {PortAdsInternal0, IO_BACKEND_ADS1115_INT, 0, IO_PORT_DIR_IN, "ADS int A0"},
    {PortAdsInternal1, IO_BACKEND_ADS1115_INT, 1, IO_PORT_DIR_IN, "ADS int A1"},
    {PortAdsInternal2, IO_BACKEND_ADS1115_INT, 2, IO_PORT_DIR_IN, "ADS int A2"},
    {PortAdsInternal3, IO_BACKEND_ADS1115_INT, 3, IO_PORT_DIR_IN, "ADS int A3"},
    {PortAdsExternal0, IO_BACKEND_ADS1115_EXT_DIFF, 0, IO_PORT_DIR_IN, "ADS ext D0"},
    {PortAdsExternal1, IO_BACKEND_ADS1115_EXT_DIFF, 1, IO_PORT_DIR_IN, "ADS ext D1"},
    {PortOneWire1, IO_BACKEND_DS18B20, 0, IO_PORT_DIR_IN, "1-Wire 1"},        // Bus eau (GPIO via config oneWire1Gpio).
    {PortOneWire2, IO_BACKEND_DS18B20, 1, IO_PORT_DIR_IN, "1-Wire 2"},        // Bus air (GPIO via config oneWire2Gpio).
    {PortSht40Temp, IO_BACKEND_SHT40, 0, IO_PORT_DIR_IN, "SHT40 temp"},
    {PortSht40Humidity, IO_BACKEND_SHT40, 1, IO_PORT_DIR_IN, "SHT40 hum"},
    {PortBmp280Temp, IO_BACKEND_BMP280, 0, IO_PORT_DIR_IN, "BMP280 temp"},
    {PortBmp280Pressure, IO_BACKEND_BMP280, 1, IO_PORT_DIR_IN, "BMP280 press"},
    {PortBme680Temp, IO_BACKEND_BME680, 0, IO_PORT_DIR_IN, "BME680 temp"},
    {PortBme680Humidity, IO_BACKEND_BME680, 1, IO_PORT_DIR_IN, "BME680 hum"},
    {PortBme680Pressure, IO_BACKEND_BME680, 2, IO_PORT_DIR_IN, "BME680 press"},
    {PortBme680Gas, IO_BACKEND_BME680, 3, IO_PORT_DIR_IN, "BME680 gas"},
    {PortPowermonShuntMv, IO_BACKEND_POWERMON, 0, IO_PORT_DIR_IN, "PM shunt mV"},
    {PortPowermonBusV, IO_BACKEND_POWERMON, 1, IO_PORT_DIR_IN, "PM bus V"},
    {PortPowermonCurrentMa, IO_BACKEND_POWERMON, 2, IO_PORT_DIR_IN, "PM curr mA"},
    {PortPowermonPowerMw, IO_BACKEND_POWERMON, 3, IO_PORT_DIR_IN, "PM power mW"},
    {PortPowermonLoadV, IO_BACKEND_POWERMON, 4, IO_PORT_DIR_IN, "PM load V"},
    {PortPowermonTemp, IO_BACKEND_POWERMON, 5, IO_PORT_DIR_IN, "PM temp"},     // INA228 seul.
    {PortPowermonEnergy, IO_BACKEND_POWERMON, 6, IO_PORT_DIR_IN, "PM energy"}, // INA228 seul.
    {PortPowermonCharge, IO_BACKEND_POWERMON, 7, IO_PORT_DIR_IN, "PM charge"}, // INA228 seul.
    {PortDin0, IO_BACKEND_GPIO, 4, IO_PORT_DIR_IN, "DIN0"},
    {PortDin1, IO_BACKEND_GPIO, 5, IO_PORT_DIR_IN, "DIN1"},
    {PortDin2, IO_BACKEND_GPIO, 6, IO_PORT_DIR_IN, "DIN2"},
    {PortDin3, IO_BACKEND_GPIO, 7, IO_PORT_DIR_IN, "DIN3"},
    {PortDin4, IO_BACKEND_GPIO, 8, IO_PORT_DIR_IN, "DIN4"},
    {PortDin5, IO_BACKEND_GPIO, 9, IO_PORT_DIR_IN, "DIN5"},
    {PortDin6, IO_BACKEND_GPIO, 10, IO_PORT_DIR_IN, "DIN6"},
    {PortDin7, IO_BACKEND_GPIO, 11, IO_PORT_DIR_IN, "DIN7"},
    {PortExio1, IO_BACKEND_TCA9554, 0, IO_PORT_DIR_OUT, "EXIO1"},
    {PortExio2, IO_BACKEND_TCA9554, 1, IO_PORT_DIR_OUT, "EXIO2"},
    {PortExio3, IO_BACKEND_TCA9554, 2, IO_PORT_DIR_OUT, "EXIO3"},
    {PortExio4, IO_BACKEND_TCA9554, 3, IO_PORT_DIR_OUT, "EXIO4"},
    {PortExio5, IO_BACKEND_TCA9554, 4, IO_PORT_DIR_OUT, "EXIO5"},
    {PortExio6, IO_BACKEND_TCA9554, 5, IO_PORT_DIR_OUT, "EXIO6"},
    {PortExio7, IO_BACKEND_TCA9554, 6, IO_PORT_DIR_OUT, "EXIO7"},
    {PortExio8, IO_BACKEND_TCA9554, 7, IO_PORT_DIR_OUT, "EXIO8"},
    {PortMcpOut1, IO_BACKEND_MCP23017, 0, IO_PORT_DIR_OUT, "COMP01"},
    {PortMcpOut2, IO_BACKEND_MCP23017, 1, IO_PORT_DIR_OUT, "COMP02"},
    {PortMcpOut3, IO_BACKEND_MCP23017, 2, IO_PORT_DIR_OUT, "COMP03"},
    {PortMcpOut4, IO_BACKEND_MCP23017, 3, IO_PORT_DIR_OUT, "COMP04"},
    {PortMcpOut5, IO_BACKEND_MCP23017, 4, IO_PORT_DIR_OUT, "COMP05"},
    {PortMcpOut6, IO_BACKEND_MCP23017, 5, IO_PORT_DIR_OUT, "COMP06"},
    {PortMcpOut7, IO_BACKEND_MCP23017, 6, IO_PORT_DIR_OUT, "COMP07"},
    {PortMcpOut8, IO_BACKEND_MCP23017, 7, IO_PORT_DIR_OUT, "COMP08"},
    {PortMcpOut9, IO_BACKEND_MCP23017, 8, IO_PORT_DIR_OUT, "MCP OUT9"},
    {PortMcpOut10, IO_BACKEND_MCP23017, 9, IO_PORT_DIR_OUT, "MCP OUT10"},
    {PortMcpOut11, IO_BACKEND_MCP23017, 10, IO_PORT_DIR_OUT, "MCP OUT11"},
    {PortMcpOut12, IO_BACKEND_MCP23017, 11, IO_PORT_DIR_OUT, "MCP OUT12"},
    {PortMcpOut13, IO_BACKEND_MCP23017, 12, IO_PORT_DIR_OUT, "MCP OUT13"},
    {PortMcpOut14, IO_BACKEND_MCP23017, 13, IO_PORT_DIR_OUT, "MCP OUT14"},
    {PortMcpOut15, IO_BACKEND_MCP23017, 14, IO_PORT_DIR_OUT, "MCP OUT15"},
    {PortMcpOut16, IO_BACKEND_MCP23017, 15, IO_PORT_DIR_OUT, "MCP OUT16"},
};

constexpr PhysicalPortId analogPortFromLegacy(uint8_t source, uint8_t channel)
{
    switch (source) {
        case IO_SRC_ADS_INTERNAL_SINGLE:
            return (channel == 0U) ? PortAdsInternal0 :
                   (channel == 1U) ? PortAdsInternal1 :
                   (channel == 2U) ? PortAdsInternal2 :
                                     PortAdsInternal3;
        case IO_SRC_ADS_EXTERNAL_DIFF:
            return (channel == 0U) ? PortAdsExternal0 : PortAdsExternal1;
        case IO_SRC_DS18_WATER:
            return PortOneWire1;
        case IO_SRC_DS18_AIR:
            return PortOneWire2;
        default:
            return IO_PORT_INVALID;
    }
}

struct AnalogRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de la sonde.
    PhysicalPortId bindingPort; // Port physique associe.
    float c0; // Coefficient de calibration offset/intercept.
    float c1; // Coefficient de calibration gain/slope.
    int32_t precision; // Precision d'affichage (nb de decimales).
};

inline constexpr AnalogRoleDefault kAnalogRoleDefaults[] = {
    // {domainSlot, bindingPort, c0, c1, precision}
    {PoolIds::SensorOrp,        analogPortFromLegacy(FLOW_WIRDEF_IO_A0S, FLOW_WIRDEF_IO_A0C), FLOW_WIRDEF_IO_A00, FLOW_WIRDEF_IO_A01, FLOW_WIRDEF_IO_A0P}, // ORP.
    {PoolIds::SensorPh,         analogPortFromLegacy(FLOW_WIRDEF_IO_A1S, FLOW_WIRDEF_IO_A1C), FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P}, // pH.
    {PoolIds::SensorPsi,        analogPortFromLegacy(FLOW_WIRDEF_IO_A2S, FLOW_WIRDEF_IO_A2C), FLOW_WIRDEF_IO_A20, FLOW_WIRDEF_IO_A21, FLOW_WIRDEF_IO_A2P}, // Pression.
    {PoolIds::SensorSpareAnalog,analogPortFromLegacy(FLOW_WIRDEF_IO_A3S, FLOW_WIRDEF_IO_A3C), FLOW_WIRDEF_IO_A30, FLOW_WIRDEF_IO_A31, FLOW_WIRDEF_IO_A3P}, // Entree analogique reservee.
    {PoolIds::SensorWaterTemp,  analogPortFromLegacy(FLOW_WIRDEF_IO_A4S, FLOW_WIRDEF_IO_A4C), FLOW_WIRDEF_IO_A40, FLOW_WIRDEF_IO_A41, FLOW_WIRDEF_IO_A4P}, // Temperature eau.
    {PoolIds::SensorAirTemp,    analogPortFromLegacy(FLOW_WIRDEF_IO_A5S, FLOW_WIRDEF_IO_A5C), FLOW_WIRDEF_IO_A50, FLOW_WIRDEF_IO_A51, FLOW_WIRDEF_IO_A5P}, // Temperature air.
};

struct DigitalInputRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de l'entree.
    PhysicalPortId bindingPort; // Port physique associe.
    uint8_t mode; // Mode de lecture (etat/counter).
    uint8_t edgeMode; // Type de front pris en compte.
    uint32_t debounceUs; // Debounce en microsecondes.
};

inline constexpr DigitalInputRoleDefault kDigitalInputRoleDefaults[] = {
    // {role, bindingPort, mode, edgeMode, debounceUs}
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    {PoolIds::SensorPoolLevel,      PortDin2, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau piscine (GPIO6).
    {PoolIds::SensorPhLevel,        PortDin0, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau pH (GPIO4).
    {PoolIds::SensorChlorineLevel,  PortDin1, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau desinfectant (GPIO5).
    {PoolIds::SensorWaterCounter,   PortDin3, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 100000U}, // Compteur impulsions eau (GPIO7, 100 ms debounce).
#else
    {PoolIds::SensorPoolLevel,      PortDin0, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau piscine.
    {PoolIds::SensorPhLevel,        PortDin1, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau pH.
    {PoolIds::SensorChlorineLevel,  PortDin2, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau chlore.
    {PoolIds::SensorWaterCounter,   PortDin3, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 100000U}, // Compteur impulsions eau (100 ms debounce).
#endif
};

struct DigitalOutputRoleDefault {
    DomainSlotId domainSlot; // Besoin fonctionnel de la sortie.
    PhysicalPortId bindingPort; // Port physique associe.
    bool activeHigh; // Polarite de commande logique.
    bool retainOnWarmReboot; // Conserve le latch expander sur reboot ESP32 chaud.
    bool momentary; // True si sortie impulsionnelle.
    uint16_t pulseMs; // Duree d'impulsion en ms.
};

inline constexpr DigitalOutputRoleDefault kDigitalOutputRoleDefaults[] = {
    // {domainSlot, bindingPort, activeHigh, retainOnWarmReboot, momentary, pulseMs}
    {PoolIds::ActuatorFiltrationPump,   PortExio1, true, true,  false, 0U}, // Pompe filtration.
    {PoolIds::ActuatorPhPump,           PortExio2, true, false, false, 0U}, // Pompe pH.
    {PoolIds::ActuatorChlorinePump,     PortExio3, true, false, false, 0U}, // Pompe chlore.
    {PoolIds::ActuatorRobot,            PortExio4, true, false, false, 0U}, // Robot.
    {PoolIds::ActuatorFillPump,         PortExio5, true, false, false, 0U}, // Pompe de remplissage.
    {PoolIds::ActuatorChlorineGenerator,PortExio6, true, false, false, 0U}, // Electrolyseur.
    {PoolIds::ActuatorLights,           PortExio7, true, false, false, 0U}, // Eclairage.
    {PoolIds::ActuatorWaterHeater,      PortExio8, true, false, false, 0U}, // Chauffage.
};

inline constexpr const AnalogRoleDefault* analogDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const AnalogRoleDefault& entry : kAnalogRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalInputRoleDefault* digitalInputDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const DigitalInputRoleDefault& entry : kDigitalInputRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalOutputRoleDefault* digitalOutputDefaultForDomainSlot(DomainSlotId domainSlot)
{
    for (const DigitalOutputRoleDefault& entry : kDigitalOutputRoleDefaults) {
        if (entry.domainSlot == domainSlot) return &entry;
    }
    return nullptr;
}

}  // namespace IoLayout
}  // namespace Waveshare
}  // namespace Profiles
