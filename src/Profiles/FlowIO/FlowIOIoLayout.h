#pragma once

#include "Board/FlowIODINBoard.h"
#include "Domain/DomainTypes.h"
#include "Domain/Pool/PoolIds.h"
#include "Domain/IoRoleDefaults.h"
#include "Modules/IOModule/IOModuleTypes.h"

namespace Profiles {
namespace FlowIO {
namespace IoLayout {

enum : PhysicalPortId {
    PortAdsInternal0 = 100, // ADS1115 interne, entree single-ended A0.
    PortAdsInternal1 = 101, // ADS1115 interne, entree single-ended A1.
    PortAdsInternal2 = 102, // ADS1115 interne, entree single-ended A2.
    PortAdsInternal3 = 103, // ADS1115 interne, entree single-ended A3.
    PortAdsExternal0 = 110, // ADS1115 externe, paire differentielle 0.
    PortAdsExternal1 = 111, // ADS1115 externe, paire differentielle 1.
    PortDsWater = 120, // DS18B20: sonde eau.
    PortDsAir = 121, // DS18B20: sonde air.
    PortSht40Temp = 130, // SHT40: temperature.
    PortSht40Humidity = 131, // SHT40: humidite.
    PortBmp280Temp = 132, // BMP280: temperature.
    PortBmp280Pressure = 133, // BMP280: pression.
    PortBme680Temp = 134, // BME680: temperature.
    PortBme680Humidity = 135, // BME680: humidite.
    PortBme680Pressure = 136, // BME680: pression.
    PortBme680Gas = 137, // BME680: resistance gaz.
    PortPowermonShuntMv = 143, // Moniteur puissance: tension shunt (mV).
    PortPowermonBusV = 144, // Moniteur puissance: tension bus (V).
    PortPowermonCurrentMa = 145, // Moniteur puissance: courant (mA).
    PortPowermonPowerMw = 146, // Moniteur puissance: puissance (mW).
    PortPowermonLoadV = 147, // Moniteur puissance: tension charge (V).
    PortPowermonTemp = 148, // Moniteur puissance: temperature (degC, INA228 seul).
    PortPowermonEnergy = 149, // Moniteur puissance: energie (Wh, INA228 seul).
    PortPowermonCharge = 150, // Moniteur puissance: charge (mAh, INA228 seul).
    PortDigitalIn1 = 200, // Entree digitale 1 (GPIO board).
    PortDigitalIn2 = 201, // Entree digitale 2 (GPIO board).
    PortDigitalIn3 = 202, // Entree digitale 3 (GPIO board).
    PortDigitalIn4 = 203, // Entree digitale 4 (GPIO board).
    PortRelay1 = 300, // Sortie relais 1.
    PortRelay2 = 301, // Sortie relais 2.
    PortRelay3 = 302, // Sortie relais 3.
    PortRelay4 = 303, // Sortie relais 4 (momentary).
    PortRelay5 = 304, // Sortie relais 5.
    PortRelay6 = 305, // Sortie relais 6.
    PortRelay7 = 306, // Sortie relais 7.
    PortRelay8 = 307, // Sortie relais 8.
    PortPcf0Bit0 = 400, // PCF8574 #0, bit 0.
    PortPcf0Bit1 = 401, // PCF8574 #0, bit 1.
    PortPcf0Bit2 = 402, // PCF8574 #0, bit 2.
    PortPcf0Bit3 = 403, // PCF8574 #0, bit 3.
    PortPcf0Bit4 = 404, // PCF8574 #0, bit 4.
    PortPcf0Bit5 = 405, // PCF8574 #0, bit 5.
    PortPcf0Bit6 = 406, // PCF8574 #0, bit 6.
    PortPcf0Bit7 = 407 // PCF8574 #0, bit 7.
};

inline constexpr IOBindingPortSpec kBindingPorts[] = {
    // {portId, backend, channel, flags, name}
    {PortAdsInternal0, IO_BACKEND_ADS1115_INT, 0, IO_PORT_DIR_IN, "ADS int A0"},
    {PortAdsInternal1, IO_BACKEND_ADS1115_INT, 1, IO_PORT_DIR_IN, "ADS int A1"},
    {PortAdsInternal2, IO_BACKEND_ADS1115_INT, 2, IO_PORT_DIR_IN, "ADS int A2"},
    {PortAdsInternal3, IO_BACKEND_ADS1115_INT, 3, IO_PORT_DIR_IN, "ADS int A3"},
    {PortAdsExternal0, IO_BACKEND_ADS1115_EXT_DIFF, 0, IO_PORT_DIR_IN, "ADS ext D0"},
    {PortAdsExternal1, IO_BACKEND_ADS1115_EXT_DIFF, 1, IO_PORT_DIR_IN, "ADS ext D1"},
    {PortDsWater, IO_BACKEND_DS18B20, 0, IO_PORT_DIR_IN, "DS18 eau"},  // Bus 0 (eau).
    {PortDsAir, IO_BACKEND_DS18B20, 1, IO_PORT_DIR_IN, "DS18 air"},    // Bus 1 (air).
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
    {PortDigitalIn1, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[8].pin, IO_PORT_DIR_IN, "DI Pin 1"},
    {PortDigitalIn2, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[9].pin, IO_PORT_DIR_IN, "DI Pin 2"},
    {PortDigitalIn3, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[10].pin, IO_PORT_DIR_IN, "DI Pin 3"},
    {PortDigitalIn4, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[11].pin, IO_PORT_DIR_IN, "DI Pin 4"},
    {PortRelay1, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[0].pin, IO_PORT_DIR_OUT, "Relay 1"},
    {PortRelay2, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[1].pin, IO_PORT_DIR_OUT, "Relay 2"},
    {PortRelay3, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[2].pin, IO_PORT_DIR_OUT, "Relay 3"},
    {PortRelay4, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[3].pin, IO_PORT_DIR_OUT, "Relay 4"},
    {PortRelay5, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[4].pin, IO_PORT_DIR_OUT, "Relay 5"},
    {PortRelay6, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[5].pin, IO_PORT_DIR_OUT, "Relay 6"},
    {PortRelay7, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[6].pin, IO_PORT_DIR_OUT, "Relay 7"},
    {PortRelay8, IO_BACKEND_GPIO, BoardProfiles::kFlowIODINv1IoPoints[7].pin, IO_PORT_DIR_OUT, "Relay 8"},
    {PortPcf0Bit0, IO_BACKEND_PCF8574, 0, IO_PORT_DIR_OUT, "PCF bit 0"},
    {PortPcf0Bit1, IO_BACKEND_PCF8574, 1, IO_PORT_DIR_OUT, "PCF bit 1"},
    {PortPcf0Bit2, IO_BACKEND_PCF8574, 2, IO_PORT_DIR_OUT, "PCF bit 2"},
    {PortPcf0Bit3, IO_BACKEND_PCF8574, 3, IO_PORT_DIR_OUT, "PCF bit 3"},
    {PortPcf0Bit4, IO_BACKEND_PCF8574, 4, IO_PORT_DIR_OUT, "PCF bit 4"},
    {PortPcf0Bit5, IO_BACKEND_PCF8574, 5, IO_PORT_DIR_OUT, "PCF bit 5"},
    {PortPcf0Bit6, IO_BACKEND_PCF8574, 6, IO_PORT_DIR_OUT, "PCF bit 6"},
    {PortPcf0Bit7, IO_BACKEND_PCF8574, 7, IO_PORT_DIR_OUT, "PCF bit 7"},
};

inline constexpr AnalogRoleDefault kAnalogRoleDefaults[] = {
    // {domainSlot, bindingPort, c0, c1, precision}
    {PoolIds::SensorOrp, (PhysicalPortId)FLOW_WIRDEF_IO_A0PORT, FLOW_WIRDEF_IO_A00, FLOW_WIRDEF_IO_A01, FLOW_WIRDEF_IO_A0P}, // ORP.
    {PoolIds::SensorPh, (PhysicalPortId)FLOW_WIRDEF_IO_A1PORT, FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P}, // pH.
    {PoolIds::SensorPsi, (PhysicalPortId)FLOW_WIRDEF_IO_A2PORT, FLOW_WIRDEF_IO_A20, FLOW_WIRDEF_IO_A21, FLOW_WIRDEF_IO_A2P}, // Pression.
    {PoolIds::SensorSpareAnalog, (PhysicalPortId)FLOW_WIRDEF_IO_A3PORT, FLOW_WIRDEF_IO_A30, FLOW_WIRDEF_IO_A31, FLOW_WIRDEF_IO_A3P}, // Entree analogique reservee.
    {PoolIds::SensorWaterTemp, (PhysicalPortId)FLOW_WIRDEF_IO_A4PORT, FLOW_WIRDEF_IO_A40, FLOW_WIRDEF_IO_A41, FLOW_WIRDEF_IO_A4P}, // Temperature eau.
    {PoolIds::SensorAirTemp, (PhysicalPortId)FLOW_WIRDEF_IO_A5PORT, FLOW_WIRDEF_IO_A50, FLOW_WIRDEF_IO_A51, FLOW_WIRDEF_IO_A5P}, // Temperature air.
};

inline constexpr DigitalInputRoleDefault kDigitalInputRoleDefaults[] = {
    // {domainSlot, bindingPort, mode, edgeMode, debounceUs}
    {PoolIds::SensorPoolLevel, PortDigitalIn1, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau piscine.
    {PoolIds::SensorPhLevel, PortDigitalIn2, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau pH.
    {PoolIds::SensorChlorineLevel, PortDigitalIn3, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau chlore.
    {PoolIds::SensorWaterCounter, PortDigitalIn4, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 100000U}, // Compteur impulsions eau (100 ms debounce).
};

inline constexpr DigitalOutputRoleDefault kDigitalOutputRoleDefaults[] = {
    // {domainSlot, bindingPort, activeHigh, retainOnWarmReboot, momentary, pulseMs}
    {PoolIds::ActuatorFiltrationPump, PortRelay1, false, true, false, 0U}, // Pompe filtration.
    {PoolIds::ActuatorPhPump, PortRelay2, false, false, false, 0U}, // Pompe pH.
    {PoolIds::ActuatorChlorinePump, PortRelay3, false, false, false, 0U}, // Pompe chlore.
    {PoolIds::ActuatorChlorineGenerator, PortRelay4, false, false, BoardProfiles::kFlowIODINv1IoPoints[3].momentary, BoardProfiles::kFlowIODINv1IoPoints[3].pulseMs}, // Electrolyseur; utilise limites board (momentary/pulseMs).
    {PoolIds::ActuatorRobot, PortRelay5, false, false, false, 0U}, // Robot.
    {PoolIds::ActuatorLights, PortRelay6, false, false, false, 0U}, // Eclairage.
    {PoolIds::ActuatorFillPump, PortRelay7, false, false, false, 0U}, // Pompe de remplissage.
    {PoolIds::ActuatorWaterHeater, PortRelay8, false, false, false, 0U}, // Chauffage.
};


}  // namespace IoLayout
}  // namespace FlowIO
}  // namespace Profiles
