#include <unity.h>
#include <math.h>

#include "Modules/PoolLogicModule/FiltrationWindow.h"

static FiltrationPlanInput makeInput_(float temp, float volume, float flow)
{
    FiltrationPlanInput in{};
    in.waterTemp = temp;
    in.poolVolumeM3 = volume;
    in.pumpFlowM3h = flow;
    in.windows[0] = {true, 480, 1380, 1};  // 08:00-23:00
    return in;
}

void test_cycles_curve_interpolation()
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, filtrationCyclesForTemp(5.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, filtrationCyclesForTemp(10.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, filtrationCyclesForTemp(22.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f, filtrationCyclesForTemp(23.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, filtrationCyclesForTemp(28.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, filtrationCyclesForTemp(35.0f));
}

void test_single_window_centered_segment()
{
    // 48 m3 * 1.0 cycle / 8 m3/h = 6 h = 360 min, centre dans 08:00-23:00.
    FiltrationPlanInput in = makeInput_(22.0f, 48.0f, 8.0f);
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_FALSE(out.fallback);
    TEST_ASSERT_EQUAL_UINT8(1, out.segmentCount);
    TEST_ASSERT_EQUAL_UINT16(360, out.requiredMinutes);
    TEST_ASSERT_EQUAL_UINT16(360, out.plannedMinutes);
    TEST_ASSERT_EQUAL_UINT16(750, out.segments[0].startMinute);   // 12:30
    TEST_ASSERT_EQUAL_UINT16(1110, out.segments[0].stopMinute);   // 18:30
}

void test_priority_fills_off_peak_window_first()
{
    // Fenetre HC 23:30-07:30 (480 min) prio 1, fenetre jour prio 2.
    // 48 m3 * 2.0 cycles / 8 m3/h = 12 h = 720 min.
    FiltrationPlanInput in = makeInput_(28.0f, 48.0f, 8.0f);
    in.windows[0].priority = 2;
    in.windows[1] = {true, 1410, 450, 1};
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_EQUAL_UINT8(2, out.segmentCount);
    TEST_ASSERT_EQUAL_UINT16(720, out.requiredMinutes);
    TEST_ASSERT_EQUAL_UINT16(720, out.plannedMinutes);
    // Segment 0 = fenetre HC remplie en entier (traverse minuit).
    TEST_ASSERT_EQUAL_UINT16(1410, out.segments[0].startMinute);
    TEST_ASSERT_EQUAL_UINT16(450, out.segments[0].stopMinute);
    // Reliquat 240 min centre dans la fenetre jour (900 min).
    TEST_ASSERT_EQUAL_UINT16(810, out.segments[1].startMinute);
    TEST_ASSERT_EQUAL_UINT16(1050, out.segments[1].stopMinute);
}

void test_nan_temperature_uses_full_windows()
{
    FiltrationPlanInput in = makeInput_(NAN, 48.0f, 8.0f);
    in.windows[1] = {true, 1410, 450, 2};
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_TRUE(out.fallback);
    TEST_ASSERT_EQUAL_UINT8(2, out.segmentCount);
    TEST_ASSERT_EQUAL_UINT16(900 + 480, out.plannedMinutes);
    TEST_ASSERT_EQUAL_UINT16(480, out.segments[0].startMinute);
    TEST_ASSERT_EQUAL_UINT16(1380, out.segments[0].stopMinute);
}

void test_minimum_total_duration_clamp()
{
    // 20 m3 * 0.25 / 10 m3/h = 30 min -> borne a 120 min.
    FiltrationPlanInput in = makeInput_(8.0f, 20.0f, 10.0f);
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_EQUAL_UINT16(120, out.requiredMinutes);
    TEST_ASSERT_EQUAL_UINT16(120, out.plannedMinutes);
}

void test_no_valid_window_emergency_plan()
{
    FiltrationPlanInput in = makeInput_(22.0f, 48.0f, 8.0f);
    in.windows[0].enabled = false;
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_TRUE(out.fallback);
    TEST_ASSERT_EQUAL_UINT8(1, out.segmentCount);
    // 2 h centrees sur le pivot 15:00 -> 14:00-16:00.
    TEST_ASSERT_EQUAL_UINT16(840, out.segments[0].startMinute);
    TEST_ASSERT_EQUAL_UINT16(960, out.segments[0].stopMinute);
}

void test_plan_active_wraps_midnight()
{
    FiltrationPlanOutput plan{};
    plan.segmentCount = 1;
    plan.segments[0] = {1410, 450};
    TEST_ASSERT_TRUE(isFiltrationPlanActiveAtMinute(plan, 1439));
    TEST_ASSERT_TRUE(isFiltrationPlanActiveAtMinute(plan, 0));
    TEST_ASSERT_TRUE(isFiltrationPlanActiveAtMinute(plan, 449));
    TEST_ASSERT_FALSE(isFiltrationPlanActiveAtMinute(plan, 450));
    TEST_ASSERT_FALSE(isFiltrationPlanActiveAtMinute(plan, 1409));
}

void test_small_remainder_rounds_up_to_min_segment()
{
    // Besoin = 910 min : fenetre 1 (900) pleine + reliquat 10 min arrondi a 30.
    // 48.53 m3 pour tomber sur ~910 : on force plutot via volume exact.
    // 910 min = 15.1667 h ; a 1.0 cycle et 8 m3/h il faut 121.33 m3.
    FiltrationPlanInput in = makeInput_(22.0f, 121.333f, 8.0f);
    in.windows[1] = {true, 1410, 450, 2};
    FiltrationPlanOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationPlan(in, out));
    TEST_ASSERT_EQUAL_UINT8(2, out.segmentCount);
    TEST_ASSERT_EQUAL_UINT16(900, (uint16_t)(out.segments[0].stopMinute - out.segments[0].startMinute));
    // Reliquat 10 min -> segment force a 30 min minimum.
    TEST_ASSERT_EQUAL_UINT16(930, out.plannedMinutes);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_cycles_curve_interpolation);
    RUN_TEST(test_single_window_centered_segment);
    RUN_TEST(test_priority_fills_off_peak_window_first);
    RUN_TEST(test_nan_temperature_uses_full_windows);
    RUN_TEST(test_minimum_total_duration_clamp);
    RUN_TEST(test_no_valid_window_emergency_plan);
    RUN_TEST(test_plan_active_wraps_midnight);
    RUN_TEST(test_small_remainder_rounds_up_to_min_segment);
    return UNITY_END();
}
