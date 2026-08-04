/**
 * @file DosingController.cpp
 * @brief Implementation de la FSM de dosage volumetrique par lots.
 */

#include "Modules/PoolLogicModule/DosingController.h"

#include <math.h>

#include "Domain/Pool/PoolDefaults.h"

namespace {

bool finitePositive_(float v)
{
    return isfinite(v) && (v > 0.0f);
}

void enterPhase_(DosingState& st, uint8_t phase, uint8_t blockReason, uint32_t nowMs)
{
    st.phase = phase;
    st.blockReason = blockReason;
    st.phaseSinceMs = nowMs;
}

void clearBatch_(DosingState& st)
{
    st.batchTargetMl = 0.0f;
    st.batchDeliveredMl = 0.0f;
    st.batchStartValue = 0.0f;
    st.batchElapsedOnMs = 0;
    st.mixWaitMs = 0;
    st.mixElapsedMs = 0;
}

/** Ecart signe dans le sens du produit charge : > 0 => il faut doser. */
float signedNeed_(const DosingInput& in)
{
    return in.dosePlus ? (in.setpoint - in.measured) : (in.measured - in.setpoint);
}

float effectiveDeadband_(const DosingInput& in)
{
    return (isfinite(in.deadband) && in.deadband > 0.0f) ? in.deadband : 0.0f;
}

/** Volume minimal exploitable, impose par la granularite temporelle de la pompe. */
float minBatchMl_(const DosingInput& in)
{
    // L/h -> mL/s : /3,6. Sous DosingMinBatchSec de marche, le lot n'est pas
    // realisable proprement (temps de reponse du relais, amorcage).
    return (in.pumpFlowLPerHour / 3.6f) * (float)PoolDefaults::DosingMinBatchSec;
}

/** Mesure exploitable pour decider ou evaluer ? Renseigne la raison sinon. */
bool sampleUsable_(const DosingInput& in, uint8_t& reasonOut)
{
    if (!in.haveSample || !isfinite(in.measured)) {
        reasonOut = DOSING_BLOCK_NO_SAMPLE;
        return false;
    }
    if (in.sampleMaxAgeMs > 0U && in.sampleAgeMs > in.sampleMaxAgeMs) {
        reasonOut = DOSING_BLOCK_SAMPLE_STALE;
        return false;
    }
    // Hors plage de validite, la sonde est suspecte : on interdit le dosage
    // plutot que de doser au maximum.
    if (in.validMax > in.validMin && (in.measured < in.validMin || in.measured > in.validMax)) {
        reasonOut = DOSING_BLOCK_SAMPLE_RANGE;
        return false;
    }
    reasonOut = DOSING_BLOCK_NONE;
    return true;
}

/** Configuration exploitable pour un calcul de dose ? */
bool configUsable_(const DosingInput& in)
{
    return finitePositive_(in.poolVolumeM3) && finitePositive_(in.pumpFlowLPerHour) &&
           finitePositive_(in.gainMlPerM3PerStep) && finitePositive_(in.unitStep);
}

}  // namespace

float computeBatchDoseMl(const DosingInput& in, float effectiveError, uint8_t& limitReasonOut)
{
    limitReasonOut = DOSING_BLOCK_NONE;
    if (!isfinite(effectiveError) || !(effectiveError > 0.0f)) return 0.0f;
    if (!configUsable_(in)) {
        limitReasonOut = DOSING_BLOCK_CONFIG;
        return 0.0f;
    }

    float dose = in.gainMlPerM3PerStep * in.poolVolumeM3 * (effectiveError / in.unitStep);
    const float factor = (in.safetyFactor > 0.0f && in.safetyFactor <= 1.0f) ? in.safetyFactor
                                                                            : PoolDefaults::PhDoseFactor;
    dose *= factor;
    if (!isfinite(dose) || dose <= 0.0f) return 0.0f;

    if (in.maxBatchMl > 0.0f && dose > in.maxBatchMl) dose = in.maxBatchMl;

    // Le quota journalier est une securite volumetrique : il tronque le lot et,
    // s'il est epuise, l'interdit.
    if (in.maxDayMl > 0.0f) {
        const float dayLeft = in.maxDayMl - in.dosedTodayMl;
        if (dayLeft <= 0.0f) {
            limitReasonOut = DOSING_BLOCK_DAY_QUOTA;
            return 0.0f;
        }
        if (dose > dayLeft) {
            dose = dayLeft;
            limitReasonOut = DOSING_BLOCK_DAY_QUOTA;
        }
    }

    if (in.tankRemainingMl > 0.0f) {
        if (in.tankRemainingMl <= PoolDefaults::DosingMinTankMl) {
            limitReasonOut = DOSING_BLOCK_TANK_EMPTY;
            return 0.0f;
        }
        if (dose > in.tankRemainingMl) {
            dose = in.tankRemainingMl;
            limitReasonOut = DOSING_BLOCK_TANK_EMPTY;
        }
    }

    if (dose < minBatchMl_(in)) return 0.0f;
    return dose;
}

uint32_t computeMixWaitMs(const DosingInput& in)
{
    uint32_t turnoverMs = 0U;
    if (finitePositive_(in.poolVolumeM3) && finitePositive_(in.filtrationFlowM3h)) {
        const float ms = (in.poolVolumeM3 / in.filtrationFlowM3h) * 3600000.0f;
        turnoverMs = (isfinite(ms) && ms > 0.0f && ms < 4.0e9f) ? (uint32_t)ms
                                                                : PoolDefaults::DosingMixWaitMaxMs;
    }

    // Le reglage utilisateur est un PLANCHER : il ne peut qu'allonger l'attente
    // calculee depuis le turnover.
    uint32_t mix = turnoverMs;
    const uint32_t cfgMs = (uint32_t)in.mixWaitMinCfg * 60000UL;
    if (cfgMs > mix) mix = cfgMs;
    if (mix < PoolDefaults::DosingMixWaitMinMs) mix = PoolDefaults::DosingMixWaitMinMs;
    if (mix > PoolDefaults::DosingMixWaitMaxMs) mix = PoolDefaults::DosingMixWaitMaxMs;
    return mix;
}

float computeObservedGain(float deliveredMl, float deltaValue, float poolVolumeM3, float unitStep)
{
    if (!isfinite(deliveredMl) || deliveredMl < PoolDefaults::DosingGainMinBatchMl) return 0.0f;
    if (!finitePositive_(poolVolumeM3) || !finitePositive_(unitStep)) return 0.0f;
    // Un delta du MAUVAIS SIGNE n'est pas une absence d'effet : c'est une
    // perturbation (pluie, baigneurs, electrolyse). L'echantillon est rejete.
    if (!isfinite(deltaValue) || deltaValue <= 0.0f) return 0.0f;
    if (deltaValue < PoolDefaults::PhNoEffectDelta) return 0.0f;

    const float denom = (deltaValue * poolVolumeM3) / unitStep;
    if (!finitePositive_(denom)) return 0.0f;
    const float gain = deliveredMl / denom;
    return isfinite(gain) ? gain : 0.0f;
}

float blendLearnedGain(float currentGain, float referenceGain, float observedGain, uint8_t sampleCount)
{
    if (!finitePositive_(referenceGain)) return currentGain;
    if (!finitePositive_(observedGain)) return currentGain;

    const float lo = referenceGain * (1.0f - PoolDefaults::DosingGainBandPct);
    const float hi = referenceGain * (1.0f + PoolDefaults::DosingGainBandPct);

    // Bornage AVANT integration : une observation aberrante ne peut pas tirer la
    // moyenne au-dela de la bande admise.
    float g = observedGain;
    if (g < lo) g = lo;
    if (g > hi) g = hi;

    const float base = (sampleCount == 0U || !finitePositive_(currentGain)) ? referenceGain : currentGain;
    // Denominateur croissant : moyenne arithmetique exacte sur les premiers
    // echantillons, puis moyenne exponentielle de constante DosingGainWindow.
    const uint8_t n = (sampleCount < PoolDefaults::DosingGainWindow) ? (uint8_t)(sampleCount + 1U)
                                                                    : PoolDefaults::DosingGainWindow;
    float blended = base + (g - base) / (float)n;
    if (!isfinite(blended)) return currentGain;
    if (blended < lo) blended = lo;
    if (blended > hi) blended = hi;
    return blended;
}

bool stepDosingController(DosingState& st, const DosingInput& in, DosingOutput& out)
{
    out = DosingOutput{};

    // Integration temporelle commune : dtMs en arithmetique non signee, donc
    // correct au wraparound de millis() (49,7 jours).
    const uint32_t dtMs = st.tickValid ? (uint32_t)(in.nowMs - st.lastTickMs) : 0U;
    st.lastTickMs = in.nowMs;
    st.tickValid = true;

    if (in.pumpActualOn && dtMs > 0U) {
        // L/h / 3600 = mL/ms, meme conversion que la comptabilite PoolDevice.
        st.batchDeliveredMl += (in.pumpFlowLPerHour / 3600.0f) * (float)dtMs;
        st.batchElapsedOnMs += dtMs;
    }
    // Le decompte du melange n'avance que si l'eau circule reellement : sinon un
    // arret de filtration ferait "passer" le turnover sans aucun brassage.
    if (in.circulating && dtMs > 0U && st.phase == DOSING_PHASE_MIXING) {
        st.mixElapsedMs += dtMs;
    }

    // Desarmement : retour a Idle depuis n'importe quelle phase, y compris le
    // blocage latche NO_EFFECT.
    if (!in.regulationArmed) {
        if (st.phase != DOSING_PHASE_IDLE) {
            clearBatch_(st);
            st.noEffectCount = 0;
            enterPhase_(st, DOSING_PHASE_IDLE, DOSING_BLOCK_DISABLED, in.nowMs);
        } else {
            st.blockReason = DOSING_BLOCK_DISABLED;
        }
    }

    const bool configOk = configUsable_(in);

    switch (st.phase) {
        case DOSING_PHASE_IDLE:
            if (in.regulationArmed) {
                clearBatch_(st);
                enterPhase_(st, DOSING_PHASE_MEASURE, DOSING_BLOCK_NONE, in.nowMs);
            }
            break;

        case DOSING_PHASE_MEASURE: {
            if (in.interlockBlocked) {
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_INTERLOCK, in.nowMs);
                break;
            }
            if (in.tankLow) {
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_TANK_EMPTY, in.nowMs);
                break;
            }
            if (!configOk) {
                st.blockReason = DOSING_BLOCK_CONFIG;
                break;
            }
            uint8_t sampleReason = DOSING_BLOCK_NONE;
            if (!sampleUsable_(in, sampleReason)) {
                st.blockReason = sampleReason;
                break;
            }

            const float need = signedNeed_(in);
            const float effective = need - effectiveDeadband_(in);
            if (!(effective > 0.0f)) {
                // Ecart du mauvais cote = le produit charge ne peut rien corriger
                // (il faudrait le correctif oppose) ; dans la bande = etat nominal.
                st.blockReason = (need < -effectiveDeadband_(in)) ? DOSING_BLOCK_WRONG_SIDE
                                                                 : DOSING_BLOCK_IN_BAND;
                break;
            }

            uint8_t limitReason = DOSING_BLOCK_NONE;
            const float dose = computeBatchDoseMl(in, effective, limitReason);
            if (!(dose > 0.0f)) {
                st.blockReason = (limitReason != DOSING_BLOCK_NONE) ? limitReason : DOSING_BLOCK_IN_BAND;
                break;
            }

            st.batchTargetMl = dose;
            st.batchDeliveredMl = 0.0f;
            st.batchElapsedOnMs = 0;
            st.batchStartValue = in.measured;
            st.mixWaitMs = computeMixWaitMs(in);
            st.mixElapsedMs = 0;
            enterPhase_(st, DOSING_PHASE_DOSING, limitReason, in.nowMs);
            break;
        }

        case DOSING_PHASE_DOSING: {
            if (in.pumpWriteRejected) {
                // Le lot est suspendu, pas perdu : batchDeliveredMl est conserve.
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_PUMP, in.nowMs);
                break;
            }
            if (in.interlockBlocked) {
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_INTERLOCK, in.nowMs);
                break;
            }
            if (in.tankLow) {
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_TANK_EMPTY, in.nowMs);
                break;
            }
            if (st.batchDeliveredMl >= st.batchTargetMl) {
                st.batchCount++;
                st.mixWaitMs = computeMixWaitMs(in);
                st.mixElapsedMs = 0;
                enterPhase_(st, DOSING_PHASE_MIXING, DOSING_BLOCK_NONE, in.nowMs);
                break;
            }
            out.pumpOn = true;
            break;
        }

        case DOSING_PHASE_MIXING:
            // Rien ne se decide ici : c'est le coeur de la correction. Meme un
            // ecart important ne redemarre pas la pompe avant la fin du brassage.
            if (st.mixElapsedMs >= st.mixWaitMs) {
                enterPhase_(st, DOSING_PHASE_EVALUATE, DOSING_BLOCK_NONE, in.nowMs);
            }
            break;

        case DOSING_PHASE_EVALUATE: {
            uint8_t sampleReason = DOSING_BLOCK_NONE;
            if (!sampleUsable_(in, sampleReason)) {
                st.blockReason = sampleReason;
                break;
            }

            // Delta oriente dans le sens de la correction attendue.
            const float delta = in.dosePlus ? (in.measured - st.batchStartValue)
                                            : (st.batchStartValue - in.measured);
            st.lastDeltaValue = delta;

            const float observed =
                computeObservedGain(st.batchDeliveredMl, delta, in.poolVolumeM3, in.unitStep);
            if (observed > 0.0f) {
                st.lastObservedGain = observed;
                st.learnedGain = blendLearnedGain(st.learnedGain, in.referenceGain, observed,
                                                  st.gainSampleCount);
                if (st.gainSampleCount < 0xFFU) st.gainSampleCount++;
                out.gainUpdated = true;
                st.noEffectCount = 0;
            } else if (isfinite(delta) && delta < in.noEffectThreshold &&
                       st.batchDeliveredMl >= PoolDefaults::DosingGainMinBatchMl) {
                // Dose significative sans correction mesurable : bidon vide,
                // tuyau perce ou sonde figee. Un delta negatif compte aussi --
                // avec un bidon vide la derive naturelle fait remonter le pH,
                // et l'alarme n'est levee qu'apres N lots consecutifs, ce qu'une
                // perturbation ponctuelle (orage, frequentation) ne produit pas.
                if (st.noEffectCount < 0xFFU) st.noEffectCount++;
            }

            out.batchCompleted = true;
            if (in.noEffectBatches > 0U && st.noEffectCount >= in.noEffectBatches) {
                out.noEffectAlarm = true;
                clearBatch_(st);
                enterPhase_(st, DOSING_PHASE_BLOCKED, DOSING_BLOCK_NO_EFFECT, in.nowMs);
                break;
            }
            clearBatch_(st);
            enterPhase_(st, DOSING_PHASE_MEASURE, DOSING_BLOCK_NONE, in.nowMs);
            break;
        }

        case DOSING_PHASE_BLOCKED: {
            // NO_EFFECT est latche : seul le desarmement (traite plus haut) le leve.
            if (st.blockReason == DOSING_BLOCK_NO_EFFECT) break;

            const bool causeGone =
                !in.interlockBlocked &&
                !(in.tankLow && st.blockReason == DOSING_BLOCK_TANK_EMPTY) &&
                !(in.pumpWriteRejected && st.blockReason == DOSING_BLOCK_PUMP);
            if (!causeGone) break;

            // Un lot entame reprend la ou il s'etait arrete : la dose deja
            // injectee n'est ni perdue ni comptee deux fois.
            if (st.batchTargetMl > 0.0f && st.batchDeliveredMl < st.batchTargetMl) {
                enterPhase_(st, DOSING_PHASE_DOSING, DOSING_BLOCK_NONE, in.nowMs);
            } else {
                clearBatch_(st);
                enterPhase_(st, DOSING_PHASE_MEASURE, DOSING_BLOCK_NONE, in.nowMs);
            }
            break;
        }

        default:
            enterPhase_(st, DOSING_PHASE_IDLE, DOSING_BLOCK_NONE, in.nowMs);
            break;
    }

    // Une entree inexploitable ne doit jamais laisser la pompe en marche.
    if (!configOk && in.regulationArmed) {
        out.pumpOn = false;
        out.fallback = true;
        if (st.blockReason == DOSING_BLOCK_NONE) st.blockReason = DOSING_BLOCK_CONFIG;
    }

    out.phase = st.phase;
    out.blockReason = st.blockReason;
    out.doseTargetMl = st.batchTargetMl;
    out.doseDeliveredMl = st.batchDeliveredMl;
    out.error = isfinite(in.measured) ? signedNeed_(in) : 0.0f;
    out.mixWaitMs = st.mixWaitMs;
    out.mixRemainMs = (st.mixWaitMs > st.mixElapsedMs) ? (st.mixWaitMs - st.mixElapsedMs) : 0U;
    out.learnedGain = st.learnedGain;

    return !out.fallback;
}
