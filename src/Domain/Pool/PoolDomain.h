#pragma once

#include "Domain/DomainSpec.h"
#include "Domain/Pool/PoolDefaults.h"
#include "Domain/Pool/PoolIds.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"

namespace PoolDomain {

// Une ligne par role metier : identite + slot IO + presentation Home
// Assistant. Le brochage d'usine (port physique, calibration, polarites)
// reste par profil dans les *IoLayout.h.
inline constexpr PoolRoleSpec kPoolRoles[] = {
    // {role, ioSlot, endpointId, displayName, haObjectSuffix, haName, haIcon, haUnit}
    {PoolIds::SensorOrp, analogInputSlot(0), "ORP", "ORP", "io_orp", nullptr, "mdi:flash", "mV"},
    {PoolIds::SensorPh, analogInputSlot(1), "pH", "pH", "io_ph", nullptr, "mdi:ph", ""},
    {PoolIds::SensorPressure, analogInputSlot(2), "Pressure", "Pressure", "io_pressure", nullptr, "mdi:gauge", "bar"},
    {PoolIds::SensorSpareAnalog, analogInputSlot(3), "Spare", "Spare", "io_spare", nullptr, "mdi:sine-wave", nullptr},
    {PoolIds::SensorWaterTemp, analogInputSlot(4), "Water Temperature", "Water Temperature", "io_wat_tmp", nullptr, "mdi:water-thermometer", "\xC2\xB0""C"},
    {PoolIds::SensorAirTemp, analogInputSlot(5), "Air Temperature", "Air Temperature", "io_air_tmp", nullptr, "mdi:thermometer", "\xC2\xB0""C"},
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    {PoolIds::SensorPoolLevel, digitalInputSlot(2), "Pool Level", "Pool Level", "io_pool_lvl", nullptr, "mdi:waves-arrow-up", nullptr},
    {PoolIds::SensorPhLevel, digitalInputSlot(0), "pH Level", "pH Level", "io_ph_lvl", nullptr, "mdi:flask-outline", nullptr},
    {PoolIds::SensorChlorineLevel, digitalInputSlot(1), "Chlorine Level", "Chlorine Level", "io_dis_lvl", "Disinfectant Level", "mdi:test-tube", nullptr},
    {PoolIds::SensorWaterCounter, digitalInputSlot(3), "Water Counter", "Water Counter", "io_wat_cnt", nullptr, "mdi:water-sync", "L"},
#else
    {PoolIds::SensorPoolLevel, digitalInputSlot(0), "Pool Level", "Pool Level", "io_pool_lvl", nullptr, "mdi:waves-arrow-up", nullptr},
    {PoolIds::SensorPhLevel, digitalInputSlot(1), "pH Level", "pH Level", "io_ph_lvl", nullptr, "mdi:flask-outline", nullptr},
    {PoolIds::SensorChlorineLevel, digitalInputSlot(2), "Chlorine Level", "Chlorine Level", "io_chl_lvl", nullptr, "mdi:test-tube", nullptr},
    {PoolIds::SensorWaterCounter, digitalInputSlot(3), "Water Counter", "Water Counter", "io_wat_cnt", nullptr, "mdi:water-sync", "L"},
#endif
    {PoolIds::ActuatorFiltrationPump, digitalOutputSlot(0), "io_flt_pmp", "Filtration Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorPhPump, digitalOutputSlot(1), "io_ph_pmp", "pH Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorChlorinePump, digitalOutputSlot(2), "io_chl_pmp", "Chlorine Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorRobot, digitalOutputSlot(3), "io_robot", "Robot", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorFillPump, digitalOutputSlot(4), "io_fill_pmp", "Fill Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorChlorineGenerator, digitalOutputSlot(5), "io_chl_gen", "Chlorine Generator", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorLights, digitalOutputSlot(6), "io_lights", "Lights", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorWaterHeater, digitalOutputSlot(7), "io_wat_htr", "Water Heater", nullptr, nullptr, nullptr, nullptr},
};

inline constexpr PoolDevicePreset kPoolDevices[] = {
    {PoolIds::DeviceFiltrationPump, PoolIds::ActuatorFiltrationPump, "io_flt_pmp", "Filtration Pump", "mdi:pool", POOL_DEVICE_FILTRATION, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DevicePhPump, PoolIds::ActuatorPhPump, "io_ph_pmp", "pH Pump", "mdi:beaker-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorinePump, PoolIds::ActuatorChlorinePump, "io_chl_pmp", "Chlorine Pump", "mdi:water-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceRobot, PoolIds::ActuatorRobot, "io_robot", "Robot", "mdi:robot-vacuum", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, PoolIds::DeviceFiltrationPump, 0},
    {PoolIds::DeviceFillPump, PoolIds::ActuatorFillPump, "io_fill_pmp", "Fill Pump", "mdi:waves-arrow-up", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID,
     PoolDefaults::FillPumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorineGenerator, PoolIds::ActuatorChlorineGenerator, "io_chl_gen", "Chlorine Generator", "mdi:flash", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     PoolIds::DeviceFiltrationPump, PoolDefaults::ChlorineGeneratorMaxUptimeDaySec},
    {PoolIds::DeviceLights, PoolIds::ActuatorLights, "io_lights", "Lights", "mdi:lightbulb", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DeviceWaterHeater, PoolIds::ActuatorWaterHeater, "io_wat_htr", "Water Heater", "mdi:water-boiler", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0},
};

inline constexpr DomainSpec kPoolDomain{
    "Pool",
    kPoolRoles,
    (uint8_t)(sizeof(kPoolRoles) / sizeof(kPoolRoles[0])),
    kPoolDevices,
    (uint8_t)(sizeof(kPoolDevices) / sizeof(kPoolDevices[0])),
    &PoolDefaults::kLogicDefaults,
    nullptr
};

}  // namespace PoolDomain
