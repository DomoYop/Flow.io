#pragma once
/**
 * @file IoAnalogSlotDefaults.h
 * @brief Nom et precision par defaut d'un slot analogique auto-provisionne,
 *        indexes par (backend, canal).
 *
 * Le sens d'un canal est fixe par la puce (POWERMON canal 0 = tension shunt,
 * SHT40 canal 1 = humidite...), donc cette table est independante de la carte.
 * Elle sert a l'auto-binding : quand un driver analogique a toggle est active,
 * ses ports encore libres remplissent des slots de config libres avec ces
 * valeurs lisibles (cf. IOModule::autoBindEnabledAnalogDrivers_).
 *
 * Les noms restent <= 23 octets (buffer IOAnalogSlotConfig::name = 24 avec NUL),
 * accents UTF-8 compris.
 */

#include <stdint.h>

#include "Modules/IOModule/IOModuleTypes.h"

struct IoAnalogSlotDefault {
    uint8_t backend;   // IO_BACKEND_*
    uint8_t channel;   // Canal logique du backend.
    const char* name;  // Nom affiche par defaut.
    int32_t precision; // Nombre de decimales par defaut.
};

inline constexpr IoAnalogSlotDefault kAnalogSlotDefaults[] = {
    // POWERMON (INA22x) — canaux 5-7 (temp/energie/charge) fournis par l'INA228 seul.
    {IO_BACKEND_POWERMON, 0, "Puissance shunt mV", 2},
    {IO_BACKEND_POWERMON, 1, "Puissance bus V", 2},
    {IO_BACKEND_POWERMON, 2, "Puissance courant mA", 1},
    {IO_BACKEND_POWERMON, 3, "Puissance mW", 0},
    {IO_BACKEND_POWERMON, 4, "Puissance sortie V", 2},
    {IO_BACKEND_POWERMON, 5, "Puissance temp °C", 1},
    {IO_BACKEND_POWERMON, 6, "Puissance énergie Wh", 2},
    {IO_BACKEND_POWERMON, 7, "Puissance charge mAh", 1},
    // DS18B20 (sondes 1-Wire) — convention bus eau / bus air.
    {IO_BACKEND_DS18B20, 0, "DS18B20 eau", 1},
    {IO_BACKEND_DS18B20, 1, "DS18B20 air", 1},
    // SHT40.
    {IO_BACKEND_SHT40, 0, "SHT40 température", 1},
    {IO_BACKEND_SHT40, 1, "SHT40 humidité", 0},
    // BMP280.
    {IO_BACKEND_BMP280, 0, "BMP280 température", 1},
    {IO_BACKEND_BMP280, 1, "BMP280 pression", 1},
    // BME680.
    {IO_BACKEND_BME680, 0, "BME680 température", 1},
    {IO_BACKEND_BME680, 1, "BME680 humidité", 0},
    {IO_BACKEND_BME680, 2, "BME680 pression", 1},
    {IO_BACKEND_BME680, 3, "BME680 gaz", 0},
};

/** Retourne le defaut lisible pour (backend, canal), ou nullptr si absent. */
constexpr const IoAnalogSlotDefault* analogSlotDefault(uint8_t backend, uint8_t channel)
{
    for (const IoAnalogSlotDefault& d : kAnalogSlotDefaults) {
        if (d.backend == backend && d.channel == channel) return &d;
    }
    return nullptr;
}
