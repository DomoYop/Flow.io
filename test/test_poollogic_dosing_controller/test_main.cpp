#include <unity.h>
#include <math.h>

#include "Domain/Pool/PoolDefaults.h"
#include "Modules/PoolLogicModule/DosingController.h"

// Installation de reference de la note : 50 m3, filtration 10 m3/h (turnover
// 5 h), pompe doseuse 1,8 L/h (= 0,5 mL/s), gain 10 mL/m3 par 0,1 pH.
static DosingInput makeInput_(float measured)
{
    DosingInput in{};
    in.nowMs = 1000U;
    in.regulationArmed = true;
    in.circulating = true;
    in.haveSample = true;
    in.measured = measured;
    in.sampleAgeMs = 1000U;
    in.sampleMaxAgeMs = 300000U;
    in.setpoint = 7.4f;
    in.validMin = 6.0f;
    in.validMax = 8.5f;
    in.deadband = 0.05f;
    in.dosePlus = false;
    in.poolVolumeM3 = 50.0f;
    in.filtrationFlowM3h = 10.0f;
    in.pumpFlowLPerHour = 1.8f;
    in.gainMlPerM3PerStep = 10.0f;
    in.unitStep = 0.1f;
    in.safetyFactor = 0.5f;
    in.maxBatchMl = 250.0f;
    in.maxDayMl = 1500.0f;
    in.referenceGain = 10.0f;
    in.noEffectThreshold = 0.02f;
    in.noEffectBatches = 3;
    return in;
}

/** Avance l'horloge d'un pas et rejoue la FSM. */
static void advance_(DosingState& st, DosingInput& in, DosingOutput& out, uint32_t dtMs, bool pumpOn)
{
    in.nowMs += dtMs;
    in.pumpActualOn = pumpOn;
    (void)stepDosingController(st, in, out);
}

/** Amene la FSM en phase Dosing avec la pompe en marche. */
static void runToDosing_(DosingState& st, DosingInput& in, DosingOutput& out)
{
    (void)stepDosingController(st, in, out);  // Idle -> Measure
    advance_(st, in, out, 200U, false);       // Measure -> Dosing
    advance_(st, in, out, 200U, false);       // Dosing : pompe demandee
}

// --- Armement et robustesse ---------------------------------------------

void test_idle_when_not_armed()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.regulationArmed = false;
    DosingOutput out{};
    TEST_ASSERT_TRUE(stepDosingController(st, in, out));
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_IDLE, out.phase);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_DISABLED, out.blockReason);
}

void test_output_reset_each_call()
{
    DosingState st{};
    DosingInput in = makeInput_(7.42f);  // dans la bande morte
    DosingOutput out{};
    out.pumpOn = true;
    out.doseTargetMl = 999.0f;
    out.noEffectAlarm = true;
    out.gainUpdated = true;
    (void)stepDosingController(st, in, out);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out.doseTargetMl);
    TEST_ASSERT_FALSE(out.noEffectAlarm);
    TEST_ASSERT_FALSE(out.gainUpdated);
}

void test_invalid_config_returns_false()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.poolVolumeM3 = 0.0f;
    DosingOutput out{};
    TEST_ASSERT_FALSE(stepDosingController(st, in, out));
    TEST_ASSERT_TRUE(out.fallback);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_CONFIG, out.blockReason);
}

// --- Bande morte et sens de correction -----------------------------------

void test_in_band_no_dose()
{
    DosingState st{};
    DosingInput in = makeInput_(7.43f);  // ecart 0,03 < deadband 0,05
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    advance_(st, in, out, 200U, false);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_IN_BAND, out.blockReason);
}

void test_deadband_boundary()
{
    {
        DosingState st{};
        DosingInput in = makeInput_(7.45f);  // ecart exactement 0,05
        DosingOutput out{};
        (void)stepDosingController(st, in, out);
        advance_(st, in, out, 200U, false);
        TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_IN_BAND, out.blockReason);
    }
    {
        DosingState st{};
        DosingInput in = makeInput_(7.8f);  // ecart 0,40 : bien au-dela
        DosingOutput out{};
        runToDosing_(st, in, out);
        TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_DOSING, out.phase);
        TEST_ASSERT_TRUE(out.pumpOn);
    }
}

void test_wrong_side_no_dose()
{
    DosingState st{};
    DosingInput in = makeInput_(7.1f);  // pH bas avec un produit pH-
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    advance_(st, in, out, 200U, false);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_WRONG_SIDE, out.blockReason);
}

// --- Calcul de dose -------------------------------------------------------

void test_dose_formula()
{
    // ecart 0,20 -> effectif 0,15 -> 10 * 50 * 1,5 = 750 mL, * 0,5 = 375 mL.
    DosingInput in = makeInput_(7.6f);
    in.maxBatchMl = 0.0f;  // plafond desactive pour isoler la formule
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 375.0f, computeBatchDoseMl(in, 0.15f, reason));
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_NONE, reason);
}

void test_dose_capped_by_max_batch()
{
    DosingInput in = makeInput_(7.6f);
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 250.0f, computeBatchDoseMl(in, 0.15f, reason));
}

void test_dose_capped_by_day_quota()
{
    DosingInput in = makeInput_(7.6f);
    in.dosedTodayMl = 1400.0f;
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, computeBatchDoseMl(in, 0.15f, reason));
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_DAY_QUOTA, reason);
}

void test_dose_zero_when_day_quota_exhausted()
{
    DosingInput in = makeInput_(7.6f);
    in.dosedTodayMl = 1500.0f;
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeBatchDoseMl(in, 0.15f, reason));
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_DAY_QUOTA, reason);
}

void test_dose_capped_by_tank()
{
    DosingInput in = makeInput_(7.6f);
    in.tankRemainingMl = 40.0f;
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 40.0f, computeBatchDoseMl(in, 0.15f, reason));
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_TANK_EMPTY, reason);
}

void test_dose_zero_when_tank_empty()
{
    DosingInput in = makeInput_(7.6f);
    in.tankRemainingMl = 2.0f;  // sous DosingMinTankMl
    uint8_t reason = 0xFFU;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeBatchDoseMl(in, 0.15f, reason));
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_TANK_EMPTY, reason);
}

void test_dose_below_min_batch_is_zero()
{
    // A 1,8 L/h, 10 s de pompe = 5 mL : en dessous, le lot n'est pas realisable.
    DosingInput in = makeInput_(7.46f);
    uint8_t reason = 0xFFU;
    // ecart effectif 0,001 -> 10 * 50 * 0,01 * 0,5 = 2,5 mL < 5 mL
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeBatchDoseMl(in, 0.001f, reason));
}

// --- Deroulement d'un lot -------------------------------------------------

void test_batch_completes_on_volume()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;  // a 0,5 mL/s -> 180 s de pompe
    DosingOutput out{};
    runToDosing_(st, in, out);
    TEST_ASSERT_TRUE(out.pumpOn);

    for (int i = 0; i < 180; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, out.doseDeliveredMl);
}

void test_batch_pauses_when_pump_off()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;
    DosingOutput out{};
    runToDosing_(st, in, out);

    // Un tick sur deux avec la pompe reellement a l'arret : meme volume, duree
    // doublee. Le lot ne se termine pas au temps mais au millilitre.
    for (int i = 0; i < 360; ++i) advance_(st, in, out, 1000U, (i % 2) == 0);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, out.doseDeliveredMl);
}

void test_pump_rejected_blocks_and_preserves_batch()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;
    DosingOutput out{};
    runToDosing_(st, in, out);

    for (int i = 0; i < 90; ++i) advance_(st, in, out, 1000U, true);  // ~45 mL
    const float delivered = out.doseDeliveredMl;
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 45.0f, delivered);

    in.pumpWriteRejected = true;
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_BLOCKED, out.phase);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_PUMP, out.blockReason);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, delivered, out.doseDeliveredMl);

    // Reprise : la dose deja injectee n'est ni perdue ni recomptee.
    in.pumpWriteRejected = false;
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_DOSING, out.phase);
    for (int i = 0; i < 90; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 90.0f, out.doseDeliveredMl);
}

void test_interlock_blocks_dosing()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    DosingOutput out{};
    runToDosing_(st, in, out);
    in.interlockBlocked = true;
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_BLOCKED, out.phase);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_INTERLOCK, out.blockReason);
    TEST_ASSERT_FALSE(out.pumpOn);
}

// --- Temps de melange -----------------------------------------------------

void test_mix_wait_turnover()
{
    DosingInput in = makeInput_(7.8f);  // 50 m3 / 10 m3/h = 5 h
    TEST_ASSERT_EQUAL_UINT32(5UL * 3600UL * 1000UL, computeMixWaitMs(in));
}

void test_mix_wait_config_is_floor()
{
    DosingInput in = makeInput_(7.8f);
    in.mixWaitMinCfg = 360;  // 6 h > turnover 5 h
    TEST_ASSERT_EQUAL_UINT32(6UL * 3600UL * 1000UL, computeMixWaitMs(in));
    in.mixWaitMinCfg = 60;  // 1 h < turnover 5 h : le turnover l'emporte
    TEST_ASSERT_EQUAL_UINT32(5UL * 3600UL * 1000UL, computeMixWaitMs(in));
}

void test_mix_wait_floor_and_ceiling()
{
    DosingInput in = makeInput_(7.8f);
    in.poolVolumeM3 = 1.0f;
    in.filtrationFlowM3h = 100.0f;  // turnover 36 s
    TEST_ASSERT_EQUAL_UINT32(PoolDefaults::DosingMixWaitMinMs, computeMixWaitMs(in));

    in.poolVolumeM3 = 500.0f;
    in.filtrationFlowM3h = 1.0f;  // turnover 500 h
    TEST_ASSERT_EQUAL_UINT32(PoolDefaults::DosingMixWaitMaxMs, computeMixWaitMs(in));
}

void test_mixing_pauses_without_circulation()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;
    DosingOutput out{};
    runToDosing_(st, in, out);
    for (int i = 0; i < 180; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);

    const uint32_t remainBefore = out.mixRemainMs;
    in.circulating = false;
    for (int i = 0; i < 600; ++i) advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT32(remainBefore, out.mixRemainMs);

    in.circulating = true;
    for (int i = 0; i < 600; ++i) advance_(st, in, out, 1000U, false);
    TEST_ASSERT_TRUE(out.mixRemainMs < remainBefore);
}

void test_nothing_decided_during_mixing()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;
    DosingOutput out{};
    runToDosing_(st, in, out);
    for (int i = 0; i < 180; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);

    in.measured = 8.4f;  // ecart enorme : la pompe doit rester a l'arret
    for (int i = 0; i < 1000; ++i) {
        advance_(st, in, out, 1000U, false);
        TEST_ASSERT_FALSE(out.pumpOn);
    }
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);
}

// --- Validite de la mesure ------------------------------------------------

void test_stale_sample_blocks()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.sampleAgeMs = 600000U;  // 10 min > 5 min
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    advance_(st, in, out, 200U, false);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_SAMPLE_STALE, out.blockReason);
}

void test_out_of_range_sample_blocks()
{
    DosingState st{};
    DosingInput in = makeInput_(9.2f);  // hors [6,0 ; 8,5] : sonde suspecte
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    advance_(st, in, out, 200U, false);
    TEST_ASSERT_FALSE(out.pumpOn);  // jamais de dosage maximal
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_SAMPLE_RANGE, out.blockReason);
}

void test_missing_sample_blocks()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.haveSample = false;
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    advance_(st, in, out, 200U, false);
    TEST_ASSERT_FALSE(out.pumpOn);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_NO_SAMPLE, out.blockReason);
}

// --- Auto-calibration du gain --------------------------------------------

void test_observed_gain_formula()
{
    // 250 mL dans 50 m3 pour -0,05 pH -> 250 / (0,05 * 50 / 0,1) = 10,0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, computeObservedGain(250.0f, 0.05f, 50.0f, 0.1f));
}

void test_observed_gain_rejects_wrong_sign()
{
    // pH monte apres un pH- : perturbation, pas un echantillon de gain.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeObservedGain(250.0f, -0.05f, 50.0f, 0.1f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeObservedGain(250.0f, 0.0f, 50.0f, 0.1f));
}

void test_observed_gain_rejects_small_batch()
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, computeObservedGain(10.0f, 0.05f, 50.0f, 0.1f));
}

void test_gain_clamped_to_50pct()
{
    // Observation aberrante : bornee a [0,5 ; 1,5] x reference avant integration.
    const float high = blendLearnedGain(10.0f, 10.0f, 500.0f, 0);
    TEST_ASSERT_TRUE(high <= 15.0f);
    const float low = blendLearnedGain(10.0f, 10.0f, 0.01f, 0);
    TEST_ASSERT_TRUE(low >= 5.0f);
}

void test_gain_converges_to_observation()
{
    // Repetee, une observation constante tire la moyenne vers elle.
    float gain = 0.0f;
    for (uint8_t i = 0; i < 8; ++i) gain = blendLearnedGain(gain, 10.0f, 14.0f, i);
    TEST_ASSERT_TRUE(gain > 11.0f);
    TEST_ASSERT_TRUE(gain <= 15.0f);
}

void test_evaluate_updates_gain()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 250.0f;
    DosingOutput out{};
    runToDosing_(st, in, out);
    for (int i = 0; i < 500; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);

    // Fin du brassage, puis une mesure montrant l'effet obtenu.
    for (int i = 0; i < 5 * 3600; ++i) advance_(st, in, out, 1000U, false);
    in.measured = 7.75f;  // -0,05 pH pour 250 mL -> gain observe 10,0
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_TRUE(out.gainUpdated);
    TEST_ASSERT_TRUE(out.batchCompleted);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 10.0f, out.learnedGain);
    TEST_ASSERT_EQUAL_UINT8(0, st.noEffectCount);
}

// --- Detection "dosage sans effet" ---------------------------------------

/** Joue un cycle complet lot + brassage, en imposant la mesure d'evaluation. */
static void runFullBatch_(DosingState& st, DosingInput& in, DosingOutput& out, float measuredAfter)
{
    while (out.phase != DOSING_PHASE_MIXING) {
        advance_(st, in, out, 1000U, out.phase == DOSING_PHASE_DOSING);
        if (out.phase == DOSING_PHASE_BLOCKED) return;
    }
    for (int i = 0; i < 5 * 3600 + 10; ++i) advance_(st, in, out, 1000U, false);
    in.measured = measuredAfter;
    advance_(st, in, out, 1000U, false);
}

void test_no_effect_alarm_after_n_batches()
{
    DosingState st{};
    DosingInput in = makeInput_(7.9f);
    DosingOutput out{};
    (void)stepDosingController(st, in, out);

    // Trois lots consecutifs sans correction mesurable (bidon vide : le pH
    // derive meme legerement a la hausse).
    runFullBatch_(st, in, out, 7.90f);
    TEST_ASSERT_EQUAL_UINT8(1, st.noEffectCount);
    runFullBatch_(st, in, out, 7.91f);
    TEST_ASSERT_EQUAL_UINT8(2, st.noEffectCount);
    runFullBatch_(st, in, out, 7.92f);
    TEST_ASSERT_EQUAL_UINT8(3, st.noEffectCount);
    TEST_ASSERT_TRUE(out.noEffectAlarm);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_BLOCKED, out.phase);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_NO_EFFECT, out.blockReason);
}

void test_no_effect_counter_resets_on_effect()
{
    DosingState st{};
    DosingInput in = makeInput_(7.9f);
    DosingOutput out{};
    (void)stepDosingController(st, in, out);

    runFullBatch_(st, in, out, 7.90f);
    TEST_ASSERT_EQUAL_UINT8(1, st.noEffectCount);
    runFullBatch_(st, in, out, 7.80f);  // -0,10 pH : effet net
    TEST_ASSERT_EQUAL_UINT8(0, st.noEffectCount);
}

void test_no_effect_latched_until_disarm()
{
    DosingState st{};
    DosingInput in = makeInput_(7.9f);
    DosingOutput out{};
    (void)stepDosingController(st, in, out);
    runFullBatch_(st, in, out, 7.90f);
    runFullBatch_(st, in, out, 7.91f);
    runFullBatch_(st, in, out, 7.92f);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_NO_EFFECT, out.blockReason);

    // Le latch resiste au temps et a une mesure redevenue normale.
    for (int i = 0; i < 1000; ++i) advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_BLOCKED, out.phase);
    TEST_ASSERT_EQUAL_UINT8(DOSING_BLOCK_NO_EFFECT, out.blockReason);

    // Seul le desarmement le leve.
    in.regulationArmed = false;
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_IDLE, out.phase);
    in.regulationArmed = true;
    advance_(st, in, out, 1000U, false);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MEASURE, out.phase);
    TEST_ASSERT_EQUAL_UINT8(0, st.noEffectCount);
}

// --- Convergence et robustesse horloge -----------------------------------

void test_convergence_geometric()
{
    // Boucle fermee simulee : le bassin repond selon le gain reel (10 mL/m3 par
    // 0,1 pH). On verifie une convergence monotone et SANS depassement.
    DosingState st{};
    DosingInput in = makeInput_(7.9f);
    DosingOutput out{};
    (void)stepDosingController(st, in, out);

    float ph = 7.9f;
    int batches = 0;
    for (; batches < 12; ++batches) {
        in.measured = ph;
        while (out.phase != DOSING_PHASE_MIXING && out.phase != DOSING_PHASE_BLOCKED &&
               out.blockReason != DOSING_BLOCK_IN_BAND) {
            advance_(st, in, out, 1000U, out.phase == DOSING_PHASE_DOSING);
        }
        if (out.blockReason == DOSING_BLOCK_IN_BAND) break;
        if (out.phase == DOSING_PHASE_BLOCKED) break;

        // Effet du lot : delta = dose / (gain * volume / unitStep)
        const float delta = out.doseDeliveredMl / (10.0f * 50.0f / 0.1f);
        const float next = ph - delta;
        TEST_ASSERT_TRUE(next < ph);            // progression monotone
        TEST_ASSERT_TRUE(next >= in.setpoint);  // jamais de depassement
        ph = next;

        for (int i = 0; i < 5 * 3600 + 10; ++i) advance_(st, in, out, 1000U, false);
        in.measured = ph;
        advance_(st, in, out, 1000U, false);
    }
    TEST_ASSERT_TRUE(batches < 10);
    TEST_ASSERT_TRUE(ph <= in.setpoint + in.deadband + 0.001f);
}

void test_millis_wraparound()
{
    DosingState st{};
    DosingInput in = makeInput_(7.8f);
    in.maxBatchMl = 90.0f;
    in.nowMs = 0xFFFFF000UL;  // ~4 s avant le rollover de millis()
    DosingOutput out{};
    runToDosing_(st, in, out);
    for (int i = 0; i < 180; ++i) advance_(st, in, out, 1000U, true);
    TEST_ASSERT_EQUAL_UINT8(DOSING_PHASE_MIXING, out.phase);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, out.doseDeliveredMl);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_when_not_armed);
    RUN_TEST(test_output_reset_each_call);
    RUN_TEST(test_invalid_config_returns_false);
    RUN_TEST(test_in_band_no_dose);
    RUN_TEST(test_deadband_boundary);
    RUN_TEST(test_wrong_side_no_dose);
    RUN_TEST(test_dose_formula);
    RUN_TEST(test_dose_capped_by_max_batch);
    RUN_TEST(test_dose_capped_by_day_quota);
    RUN_TEST(test_dose_zero_when_day_quota_exhausted);
    RUN_TEST(test_dose_capped_by_tank);
    RUN_TEST(test_dose_zero_when_tank_empty);
    RUN_TEST(test_dose_below_min_batch_is_zero);
    RUN_TEST(test_batch_completes_on_volume);
    RUN_TEST(test_batch_pauses_when_pump_off);
    RUN_TEST(test_pump_rejected_blocks_and_preserves_batch);
    RUN_TEST(test_interlock_blocks_dosing);
    RUN_TEST(test_mix_wait_turnover);
    RUN_TEST(test_mix_wait_config_is_floor);
    RUN_TEST(test_mix_wait_floor_and_ceiling);
    RUN_TEST(test_mixing_pauses_without_circulation);
    RUN_TEST(test_nothing_decided_during_mixing);
    RUN_TEST(test_stale_sample_blocks);
    RUN_TEST(test_out_of_range_sample_blocks);
    RUN_TEST(test_missing_sample_blocks);
    RUN_TEST(test_observed_gain_formula);
    RUN_TEST(test_observed_gain_rejects_wrong_sign);
    RUN_TEST(test_observed_gain_rejects_small_batch);
    RUN_TEST(test_gain_clamped_to_50pct);
    RUN_TEST(test_gain_converges_to_observation);
    RUN_TEST(test_evaluate_updates_gain);
    RUN_TEST(test_no_effect_alarm_after_n_batches);
    RUN_TEST(test_no_effect_counter_resets_on_effect);
    RUN_TEST(test_no_effect_latched_until_disarm);
    RUN_TEST(test_convergence_geometric);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
