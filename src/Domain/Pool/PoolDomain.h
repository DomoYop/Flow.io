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
    // Sondes 1-Wire generiques. Les entites metier eau/air sont publiees a part
    // par PoolLogic, d'apres wat_temp_io_id / air_temp_io_id.
    {PoolIds::SensorTemperature1, analogInputSlot(4), "Temperature 1", "Temperature 1", "io_temp1", nullptr, "mdi:thermometer", "\xC2\xB0""C"},
    {PoolIds::SensorTemperature2, analogInputSlot(5), "Temperature 2", "Temperature 2", "io_temp2", nullptr, "mdi:thermometer", "\xC2\xB0""C"},
    {PoolIds::SensorTemperature3, analogInputSlot(6), "Temperature 3", "Temperature 3", "io_temp3", nullptr, "mdi:thermometer", "\xC2\xB0""C"},
    {PoolIds::SensorTemperature4, analogInputSlot(7), "Temperature 4", "Temperature 4", "io_temp4", nullptr, "mdi:thermometer", "\xC2\xB0""C"},
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
    // Sorties : l'index du slot est celui de la fonction piscine (PoolIds::Device*).
    // Le slot 4 (oxygene actif) et les slots 9..11 n'existent que sur Waveshare ;
    // FlowIO garde donc les memes index pour les 8 fonctions qu'il porte.
    {PoolIds::ActuatorFiltrationPump, digitalOutputSlot(0), "io_flt_pmp", "Filtration Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorPhPump, digitalOutputSlot(1), "io_ph_pmp", "pH Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorChlorinePump, digitalOutputSlot(2), "io_chl_pmp", "Chlorine Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorChlorineGenerator, digitalOutputSlot(3), "io_chl_gen", "Chlorine Generator", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorFillPump, digitalOutputSlot(5), "io_fill_pmp", "Fill Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorLights, digitalOutputSlot(6), "io_lights", "Lights", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorWaterHeater, digitalOutputSlot(7), "io_wat_htr", "Water Heater", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorRobot, digitalOutputSlot(8), "io_robot", "Robot", nullptr, nullptr, nullptr, nullptr},
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    // Flowswitch (debit) + contact volet, et 2 sorties indicatrices pilotees
    // par PoolLogic (recopie temporisee, etat volet). Waveshare seulement :
    // FlowIO n'a ni les DIN ni les slots de sortie correspondants.
    {PoolIds::SensorFlowSwitch, digitalInputSlot(4), "Flow Switch", "Flow Switch", "io_flowsw", nullptr, "mdi:waves-arrow-right", nullptr},
    {PoolIds::SensorCoverClosed, digitalInputSlot(5), "Cover Closed", "Cover Closed", "io_cover", nullptr, "mdi:window-shutter", nullptr},
    {PoolIds::ActuatorO2Pump, digitalOutputSlot(4), "io_o2_pmp", "Active Oxygen Pump", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorFlowCopy, digitalOutputSlot(9), "io_flow_cpy", "Flow Copy Output", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorCoverClosed, digitalOutputSlot(10), "io_cover_out", "Cover Output", nullptr, nullptr, nullptr, nullptr},
    {PoolIds::ActuatorAux1, digitalOutputSlot(11), "io_aux1", "Auxiliary 1", nullptr, nullptr, nullptr, nullptr},
#endif
};

// Une ligne par fonction piscine. L'ordre suit PoolIds::Device*, qui est aussi
// l'index de la sortie logique dNN pilotee.
inline constexpr PoolDevicePreset kPoolDevices[] = {
    {PoolIds::DeviceFiltrationPump, PoolIds::ActuatorFiltrationPump, "io_flt_pmp", "Filtration Pump", "mdi:pool", POOL_DEVICE_FILTRATION, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DevicePhPump, PoolIds::ActuatorPhPump, "io_ph_pmp", "pH Pump", "mdi:beaker-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorinePump, PoolIds::ActuatorChlorinePump, "io_chl_pmp", "Chlorine Pump", "mdi:water-outline", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {PoolIds::DeviceChlorineGenerator, PoolIds::ActuatorChlorineGenerator, "io_chl_gen", "Chlorine Generator", "mdi:flash", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     PoolIds::DeviceFiltrationPump, PoolDefaults::ChlorineGeneratorMaxUptimeDaySec},
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    // Pompe oxygene actif : pompe distincte de la pompe chlore, pour garder un
    // debit, un bidon et des compteurs de consommation separes.
    {PoolIds::DeviceO2Pump, PoolIds::ActuatorO2Pump, "io_o2_pmp", "Active Oxygen Pump", "mdi:water-percent", POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, PoolIds::DeviceFiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
#endif
    {PoolIds::DeviceFillPump, PoolIds::ActuatorFillPump, "io_fill_pmp", "Fill Pump", "mdi:waves-arrow-up", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID,
     PoolDefaults::FillPumpMaxUptimeDaySec},
    {PoolIds::DeviceLights, PoolIds::ActuatorLights, "io_lights", "Lights", "mdi:lightbulb", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, POOL_DEVICE_INVALID, 0},
    {PoolIds::DeviceWaterHeater, PoolIds::ActuatorWaterHeater, "io_wat_htr", "Water Heater", "mdi:water-boiler", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0},
    {PoolIds::DeviceRobot, PoolIds::ActuatorRobot, "io_robot", "Robot", "mdi:robot-vacuum", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, PoolIds::DeviceFiltrationPump, 0},
#if defined(FLOW_BOARD_WAVESHARE_ESP32_S3)
    // Sorties de report : pilotees par PoolLogic, jamais commandees de
    // l'exterieur. Une commande MQTT/HA serait ecrasee au tick suivant, donc
    // elles ne publient pas de switch Home Assistant.
    {PoolIds::DeviceFlowCopy, PoolIds::ActuatorFlowCopy, "io_flow_cpy", "Flow Copy Output", "mdi:waves-arrow-right", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0, false, false},
    {PoolIds::DeviceCoverReport, PoolIds::ActuatorCoverClosed, "io_cover_out", "Cover Output", "mdi:window-shutter", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0, false, false},
    {PoolIds::DeviceAux1, PoolIds::ActuatorAux1, "io_aux1", "Auxiliary 1", "mdi:toggle-switch-outline", POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     POOL_DEVICE_INVALID, 0},
#endif
};

namespace detail {

constexpr IoSlotId ioSlotForRole(DomainSlotId role)
{
    for (const PoolRoleSpec& spec : kPoolRoles) {
        if (spec.id == role) return spec.ioSlot;
    }
    return IO_SLOT_INVALID;
}

/**
 * Invariant du modele : une fonction piscine pilote la sortie logique portant
 * son propre index (pdN <-> dNN). Il etait verifie au boot par requireSetup,
 * c'est-a-dire par une boucle infinie qui briquait la carte avec pour seule
 * trace un log serie. Le verifier ici en fait une erreur de compilation.
 */
constexpr bool poolDevicesDriveTheirOwnOutput()
{
    for (const PoolDevicePreset& device : kPoolDevices) {
        if (ioSlotForRole(device.commandSlot) != digitalOutputSlot(device.id)) return false;
    }
    return true;
}

}  // namespace detail

static_assert(detail::poolDevicesDriveTheirOwnOutput(),
              "chaque PoolDevice doit piloter la sortie digitale de son propre index (pdN <-> dNN)");

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
