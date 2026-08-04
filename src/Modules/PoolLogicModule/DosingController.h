#pragma once
/**
 * @file DosingController.h
 * @brief FSM de dosage volumetrique par lots avec temps de melange (batch + rest).
 *
 * Le dosage chimique d'un bassin est un procede a grand retard pur : une dose
 * n'est visible par la sonde qu'apres un brassage complet (1 turnover = 3 a 8 h).
 * Un regulateur proportionnel dont la fenetre est plus courte que ce retard
 * redose plusieurs fois avant de voir l'effet de la premiere dose, donc surdose
 * structurellement. On calcule ici un VOLUME a partir du volume d'eau et de
 * l'ecart, on l'injecte, puis on attend une recirculation complete avant de
 * re-mesurer. Rien n'est decide pendant l'attente.
 *
 * Le controleur est agnostique de la grandeur regulee : `unitStep` porte le pas
 * de reference du gain (0,1 pH, ou 100 mV pour un ORP). Aucune horloge ici : le
 * module appelant fournit `nowMs`.
 */

#include <stdint.h>

/** @brief Phase de la machine a etats. */
enum DosingPhase : uint8_t {
    DOSING_PHASE_IDLE = 0,      // regulation non armee
    DOSING_PHASE_MEASURE = 1,   // attente d'une mesure valide et fraiche
    DOSING_PHASE_DOSING = 2,    // lot en cours d'injection
    DOSING_PHASE_MIXING = 3,    // recirculation ; rien ne se decide
    DOSING_PHASE_EVALUATE = 4,  // dose injectee vs effet mesure
    DOSING_PHASE_BLOCKED = 5,   // refus device, quota, bidon, mesure invalide
};

/** @brief Cause d'inaction ou de blocage, publiee telle quelle en runtime. */
enum DosingBlockReason : uint8_t {
    DOSING_BLOCK_NONE = 0,
    DOSING_BLOCK_DISABLED = 1,      // auto off / filtration off / hiver
    DOSING_BLOCK_NO_SAMPLE = 2,     // pas de mesure exploitable
    DOSING_BLOCK_SAMPLE_STALE = 3,  // mesure plus vieille que sampleMaxAgeMs
    DOSING_BLOCK_SAMPLE_RANGE = 4,  // hors [validMin, validMax] => sonde suspecte
    DOSING_BLOCK_IN_BAND = 5,       // ecart <= deadband (etat nominal, non bloquant)
    DOSING_BLOCK_WRONG_SIDE = 6,    // ecart du mauvais cote pour le produit charge
    DOSING_BLOCK_DAY_QUOTA = 7,     // plafond volumetrique journalier atteint
    DOSING_BLOCK_TANK_EMPTY = 8,    // bidon vide ou niveau bas
    DOSING_BLOCK_PUMP = 9,          // refus PoolDeviceService ou pompe incoherente
    DOSING_BLOCK_CONFIG = 10,       // volume / debit / gain invalides
    DOSING_BLOCK_NO_EFFECT = 11,    // N lots consecutifs sans effet mesurable
    DOSING_BLOCK_INTERLOCK = 12,    // pression, flowswitch
};

/** @brief Entree d'un pas : mesures, config, interlocks, retour actionneur. */
struct DosingInput {
    uint32_t nowMs = 0;

    // Armement et interlocks, resolus par le module appelant.
    bool regulationArmed = false;
    bool interlockBlocked = false;
    bool tankLow = false;
    bool circulating = false;  // filtration reellement en marche

    // Mesure.
    bool haveSample = false;
    float measured = 0.0f;
    uint32_t sampleAgeMs = 0xFFFFFFFFU;
    uint32_t sampleMaxAgeMs = 300000U;
    float setpoint = 7.4f;
    float validMin = 6.0f;
    float validMax = 8.5f;
    float deadband = 0.05f;
    bool dosePlus = false;  // true = produit correctif vers le haut (pH+)

    // Installation.
    float poolVolumeM3 = 0.0f;
    float filtrationFlowM3h = 0.0f;
    float pumpFlowLPerHour = 0.0f;

    // Dosage.
    float gainMlPerM3PerStep = 10.0f;  // mL de produit par m3 pour `unitStep`
    float unitStep = 0.1f;             // pas de reference du gain (0,1 pH)
    float safetyFactor = 0.5f;         // viser une fraction de l'ecart par lot
    float maxBatchMl = 250.0f;
    float maxDayMl = 1500.0f;
    float dosedTodayMl = 0.0f;     // injectedMlDay du slot pompe (survit au reboot)
    float tankRemainingMl = 0.0f;  // <= 0 => plafond bidon non applique

    uint16_t mixWaitMinCfg = 0;  // minutes ; 0 => turnover pur

    // Retour actionneur. La raison de refus detaillee reste cote module : ici
    // seul compte le fait que la commande n'a pas ete honoree.
    bool pumpActualOn = false;
    bool pumpWriteRejected = false;

    // Auto-calibration du gain.
    float referenceGain = 10.0f;  // gain configure : borne le gain appris
    float noEffectThreshold = 0.02f;
    uint8_t noEffectBatches = 3;
};

/** @brief Etat persistant en RAM, propriete du module appelant. */
struct DosingState {
    uint8_t phase = DOSING_PHASE_IDLE;
    uint8_t blockReason = DOSING_BLOCK_NONE;
    uint32_t phaseSinceMs = 0;
    uint32_t lastTickMs = 0;  // integration volumetrique et decompte du melange
    bool tickValid = false;

    float batchTargetMl = 0.0f;
    float batchDeliveredMl = 0.0f;
    float batchStartValue = 0.0f;   // mesure au demarrage du lot
    uint32_t batchElapsedOnMs = 0;  // temps pompe ON cumule (garde-fou timeout)

    uint32_t mixWaitMs = 0;
    uint32_t mixElapsedMs = 0;  // n'avance QUE si `circulating`

    float learnedGain = 0.0f;  // 0 => pas encore appris
    float lastObservedGain = 0.0f;
    float lastDeltaValue = 0.0f;
    uint8_t gainSampleCount = 0;
    uint8_t noEffectCount = 0;
    uint32_t batchCount = 0;
};

/** @brief Sortie d'un pas, entierement reecrite a chaque appel. */
struct DosingOutput {
    bool pumpOn = false;
    uint8_t phase = DOSING_PHASE_IDLE;
    uint8_t blockReason = DOSING_BLOCK_NONE;
    float doseTargetMl = 0.0f;
    float doseDeliveredMl = 0.0f;
    float error = 0.0f;  // ecart signe brut, dans le sens du produit charge
    uint32_t mixWaitMs = 0;
    uint32_t mixRemainMs = 0;
    float learnedGain = 0.0f;
    bool gainUpdated = false;     // front : ce pas a recale le gain
    bool noEffectAlarm = false;   // seuil noEffectBatches atteint
    bool batchCompleted = false;  // front : un lot vient de se terminer
    bool fallback = false;        // entree degradee : sortie sure, rien de decide
};

/** @brief Volume d'un lot (mL), plafonne lot / jour / bidon. 0 = rien a doser. */
float computeBatchDoseMl(const DosingInput& in, float effectiveError, uint8_t& limitReasonOut);

/** @brief Duree de melange (ms) = max(config, 1 turnover), bornee [10 min, 8 h]. */
uint32_t computeMixWaitMs(const DosingInput& in);

/** @brief Gain observe (mL/m3 par `unitStep`). 0 si l'echantillon est inexploitable. */
float computeObservedGain(float deliveredMl, float deltaValue, float poolVolumeM3, float unitStep);

/** @brief Moyenne glissante bornee a +/-50 % du gain de reference (fenetre 8). */
float blendLearnedGain(float currentGain, float referenceGain, float observedGain, uint8_t sampleCount);

/** @brief Un pas de FSM. false = entree inexploitable, pompe forcee a l'arret. */
bool stepDosingController(DosingState& st, const DosingInput& in, DosingOutput& out);
