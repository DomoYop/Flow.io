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
    SensorTemperature4 = 24,
    // Desinfection a l'oxygene actif : pompe distincte de la pompe chlore, car
    // debit, bidon et compteurs de consommation ne sont pas les memes.
    ActuatorO2Pump = 25,
    ActuatorAux1 = 26
};

// Une fonction piscine = un Device = une sortie logique dNN de meme index.
// L'ordre de cet enum est donc aussi l'ordre des sorties : le modifier deplace
// les cles NVS pdN* et impose un effacement.
enum Device : PoolDeviceId {
    DeviceFiltrationPump = 0,
    DevicePhPump = 1,
    DeviceChlorinePump = 2,        // Desinfection : chlore liquide / brome.
    DeviceChlorineGenerator = 3,   // Desinfection : electrolyseur au sel.
    DeviceO2Pump = 4,              // Desinfection : oxygene actif.
    DeviceFillPump = 5,
    DeviceLights = 6,
    DeviceWaterHeater = 7,
    DeviceRobot = 8,
    DeviceFlowCopy = 9,            // Recopie temporisee du debit (sortie de report).
    DeviceCoverReport = 10,        // Report d'etat du volet (sortie de report).
    DeviceAux1 = 11
};

/**
 * Mode de traitement de l'eau. C'est un choix de materiel installe (pompe
 * doseuse, electrolyseur au sel, bidon d'oxygene actif), pas un reglage d'usage
 * courant : il est lu dans les Preferences au demarrage du profil et decide
 * quels equipements, quelles variables de configuration et quelles entites
 * Home Assistant existent. Il vit donc dans le domaine et non dans PoolLogic,
 * qui n'en est qu'un consommateur parmi d'autres (bootstrap compris).
 *
 * Voir docs/notes/desinfection-reglage-a-froid.md.
 */
enum Disinfection : uint8_t {
    DisinfectionDisabled = 0,
    DisinfectionChlorineBromine = 1,
    DisinfectionSwg = 2,
    DisinfectionActiveOxygen = 3,
};

constexpr uint8_t DeviceCount = 12;
constexpr uint8_t SensorCount = 14;
constexpr uint8_t DomainSlotCount = 26;

}  // namespace PoolIds
