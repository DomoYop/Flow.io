#pragma once
/**
 * @file PoolLogicModuleDataModel.h
 * @brief PoolLogic runtime data model contribution : etat de la FSM de dosage pH.
 *
 * Le dosage par lots decide un volume, l'injecte, puis attend une recirculation
 * complete avant de re-mesurer. Ce que l'utilisateur veut suivre pendant ce
 * cycle -- lot decide, lot deja injecte, attente restante, effet attendu -- vit
 * dans PoolLogicModule et n'etait publie que dans le snapshot MQTT. Le passer
 * par le DataStore le rend lisible par le serveur web (profil Waveshare), et
 * ouvre la voie a Home Assistant et a l'ecran.
 */

#include <stdint.h>

/** @brief Etat courant du dosage pH, reecrit a chaque pas de la FSM. */
struct PoolLogicPhDosingRuntimeData {
    /** @brief false tant que la FSM n'a pas tourne (boot, regulation desarmee). */
    bool valid = false;
    /** @brief DosingPhase : repos, mesure, injection, melange, evaluation, bloque. */
    uint8_t phase = 0;
    /** @brief DosingBlockReason : cause d'inaction, y compris non bloquante (bande morte). */
    uint8_t blockReason = 0;
    /** @brief Attente de melange en cours : hors de cette phase, mixRemainMs n'a pas de sens. */
    bool mixing = false;
    /** @brief Temps restant avant la prochaine decision (ms). */
    uint32_t mixRemainMs = 0;
    /** @brief Volume decide pour le lot en cours (mL), plafonds lot/jour/bidon appliques. */
    float batchTargetMl = 0.0f;
    /** @brief Volume deja injecte sur ce lot (mL). */
    float batchDeliveredMl = 0.0f;
    /** @brief false = aucun lot en cours ou configuration inexploitable. */
    bool haveExpectedDelta = false;
    /** @brief Variation de pH attendue du lot, signee selon le produit charge. */
    float expectedDelta = 0.0f;
    /** @brief Cumul injecte depuis minuit (mL), survit au reboot via le slot pompe. */
    float dosedTodayMl = 0.0f;
    /** @brief Gain retenu : appris s'il existe, configure sinon (mL/m3 par 0,1 pH). */
    float gainMlPerM3 = 0.0f;
};

// MODULE_DATA_MODEL: PoolLogicPhDosingRuntimeData poolPhDosing
