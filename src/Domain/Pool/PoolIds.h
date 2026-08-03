#pragma once

#include "Domain/DomainTypes.h"

namespace PoolIds {

enum DomainSlot : DomainSlotId {
    SensorOrp = 1,
    SensorPh = 2,
    SensorPressure = 3,
    SensorSpareAnalog = 4,
    // Sondes de temperature generiques. Le role metier (eau, air) est un
    // reglage de PoolLogic (wat_temp_io_id / air_temp_io_id), pas une identite
    // de slot : c'est ce qui rend l'affectation corrigeable sans reflasher.
    SensorTemperature1 = 5,
    SensorTemperature2 = 6,
    SensorPoolLevel = 7,
    SensorPhLevel = 8,
    SensorChlorineLevel = 9,
    SensorWaterCounter = 10,
    ActuatorFiltrationPump = 11,
    ActuatorPhPump = 12,
    ActuatorChlorinePump = 13,
    ActuatorRobot = 14,
    ActuatorFillPump = 15,
    ActuatorChlorineGenerator = 16,
    ActuatorLights = 17,
    ActuatorWaterHeater = 18,
    // Ajouts : entrees flowswitch (debit) + contact volet, et 2 sorties
    // indicatrices pilotees par PoolLogic (recopie temporisee, etat volet).
    SensorFlowSwitch = 19,
    SensorCoverClosed = 20,
    ActuatorFlowCopy = 21,
    ActuatorCoverClosed = 22,
    SensorTemperature3 = 23,
    SensorTemperature4 = 24
};

enum Device : PoolDeviceId {
    DeviceFiltrationPump = 0,
    DevicePhPump = 1,
    DeviceChlorinePump = 2,
    DeviceRobot = 3,
    DeviceFillPump = 4,
    DeviceChlorineGenerator = 5,
    DeviceLights = 6,
    DeviceWaterHeater = 7
};

constexpr uint8_t DeviceCount = 8;
constexpr uint8_t SensorCount = 14;
constexpr uint8_t DomainSlotCount = 24;

}  // namespace PoolIds
