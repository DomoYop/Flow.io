#pragma once
/**
 * @file AlarmIds.h
 * @brief Central stable alarm identifier repository.
 */

#include <stdint.h>

/** Stable alarm identifiers exposed across modules, MQTT, and automations. */
enum class AlarmId : uint16_t {
    None = 0,

    // PoolLogic domain
    // 1000 portait « pression basse », un detecteur de debit par defaut herite de
    // PoolMaster (pas de flowswitch la-bas). Le flowswitch tient desormais ce
    // role : ce qu'une pression quasi nulle signale alors qu'un debit est
    // confirme, c'est un capteur muet, pas une pompe desamorcee.
    PoolPressureSensorFault = 1000,
    PoolPressureHigh = 1001,
    PoolPhTankLow = 1002,
    PoolChlorineTankLow = 1003,
    PoolPhPumpMaxUptime = 1004,
    PoolChlorinePumpMaxUptime = 1005,
    PoolWaterLevelLow = 1006,
    // Dosage pH sans effet mesurable : bidon vide, tuyau perce ou sonde figee.
    PoolPhDoseNoEffect = 1007,
    // Sonde d'eau muette ou perimee : le chauffage se coupe et la filtration
    // bascule sur son plan de repli. Sans cette alarme, la degradation n'est
    // visible que dans les logs serie.
    PoolWaterTemperatureUnavailable = 1008,
    // Encrassement du filtre : mesure relative a la pression de service filtre
    // propre (pressure_ref). Purement informative, ne coupe rien.
    PoolFilterFouling = 1009,
    // Manque de debit confirme par le flowswitch, pompe en marche. Coupe la
    // pompe : marche a sec.
    PoolNoFlow = 1010,

    // Log pipeline domain
    LogWarningSeen = 1100,
    LogErrorSeen = 1101,
};

/** Alarm severity used for prioritization and summaries. */
enum class AlarmSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Alarm = 2,
    Critical = 3,
};
