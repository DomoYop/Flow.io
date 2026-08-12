#pragma once
/**
 * @file IAlarm.h
 * @brief Alarm engine service interface.
 */

#include <stddef.h>
#include <stdint.h>

#include "Core/AlarmIds.h"

/** Tri-state condition result returned by alarm callbacks. */
enum class AlarmCondState : uint8_t {
    False = 0,
    True = 1,
    Unknown = 2,
};

/**
 * Cycle de vie d'une alarme tel qu'expose aux interfaces (modele ANSI/ISA-18.2).
 * Etat unique derive de (condition, active, acquittee) : c'est la seule forme
 * publiee, a la place des anciens masques par position de slot.
 * Les valeurs sont figees : elles servent d'index d'enum Runtime UI.
 */
enum class AlarmLifecycle : uint8_t {
    Normal = 0,          //!< Rien a signaler.
    ActiveUnacked = 1,   //!< Active, pas encore acquittee : l'annonciation tourne.
    ActiveAcked = 2,     //!< Active et acquittee : visible, silencieuse.
    ClearedUnacked = 3,  //!< Cause disparue, latch encore en place : acquittement attendu.
    Unavailable = 4,     //!< Condition indeterminee (mesure absente ou perimee).
};

/** Callback used by modules to evaluate alarm conditions. */
using AlarmCondFn = AlarmCondState (*)(void* ctx, uint32_t nowMs);

/** Alarm definition registered by owner modules during init. */
struct AlarmRegistration {
    AlarmId id = AlarmId::None;
    AlarmSeverity severity = AlarmSeverity::Info;
    bool latched = false;
    uint32_t onDelayMs = 0;
    uint32_t offDelayMs = 0;
    uint32_t minRepeatMs = 0;
    char code[24] = {0};
    char title[48] = {0};
    char sourceModule[16] = {0};
};

/** Service contract exposed by AlarmModule. */
struct AlarmService {
    bool (*registerAlarm)(void* ctx, const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx);
    bool (*reset)(void* ctx, AlarmId id);
    uint8_t (*resetAll)(void* ctx);
    /**
     * Acquittement operateur : geste unique, toujours accepte sur une alarme active.
     * Si la condition est deja retombee, efface l'alarme (equivalent `reset`) ;
     * sinon la marque acquittee, ce qui coupe l'annonciation sans masquer l'etat.
     */
    bool (*ack)(void* ctx, AlarmId id);
    uint8_t (*ackAll)(void* ctx);
    bool (*isActive)(void* ctx, AlarmId id);
    bool (*isResettable)(void* ctx, AlarmId id);
    bool (*isAcknowledged)(void* ctx, AlarmId id);
    /** Etat de cycle de vie consolide : source unique des affichages. */
    AlarmLifecycle (*lifecycle)(void* ctx, AlarmId id);
    /**
     * Code stable de l'alarme ("pressure_low"), tel que declare a l'enregistrement.
     * Evite aux interfaces de fabriquer un identifiant a partir d'un numero de slot.
     * Retourne nullptr si l'alarme n'est pas enregistree.
     */
    const char* (*codeOf)(void* ctx, AlarmId id);
    uint8_t (*activeCount)(void* ctx);
    /** Alarmes actives que l'operateur n'a pas encore acquittees : pilote l'annonciation. */
    uint8_t (*unackedCount)(void* ctx);
    AlarmSeverity (*highestSeverity)(void* ctx);
    AlarmSeverity (*highestUnackedSeverity)(void* ctx);
    bool (*buildSnapshot)(void* ctx, char* out, size_t len);
    uint8_t (*listIds)(void* ctx, AlarmId* out, uint8_t max);
    bool (*buildAlarmState)(void* ctx, AlarmId id, char* out, size_t len);
    void* ctx;
};
