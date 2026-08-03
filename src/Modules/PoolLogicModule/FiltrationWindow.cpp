/**
 * @file FiltrationWindow.cpp
 * @brief Deterministic turnover-based filtration plan computation helper implementation.
 */

#include "Modules/PoolLogicModule/FiltrationWindow.h"
#include "Domain/Pool/PoolDefaults.h"
#include <math.h>

static constexpr uint16_t kMinutesPerDay = 1440;

static bool isValidWindow_(const FiltrationPlanWindow& w)
{
    if (!w.enabled) return false;
    if (w.startMinute >= kMinutesPerDay || w.stopMinute >= kMinutesPerDay) return false;
    return w.startMinute != w.stopMinute;
}

// Longueur en minutes, fenetres traversant minuit incluses.
static uint16_t windowLength_(const FiltrationPlanWindow& w)
{
    return (uint16_t)((w.stopMinute + kMinutesPerDay - w.startMinute) % kMinutesPerDay);
}

// Test [start, stop) avec passage de minuit quand stop < start.
static bool minuteInSegment_(uint16_t startMinute, uint16_t stopMinute, uint16_t minuteOfDay)
{
    if (startMinute == stopMinute) return false;
    return (stopMinute < startMinute)
        ? (minuteOfDay >= startMinute || minuteOfDay < stopMinute)
        : (minuteOfDay >= startMinute && minuteOfDay < stopMinute);
}

float filtrationCyclesForTemp(float tempC)
{
    const auto* curve = PoolDefaults::kFiltrationCyclesCurve;
    const uint8_t n = PoolDefaults::FiltrationCyclesCurveCount;

    if (tempC <= curve[0].tempC) return curve[0].cyclesPerDay;
    if (tempC >= curve[n - 1].tempC) return curve[n - 1].cyclesPerDay;

    for (uint8_t i = 1; i < n; ++i) {
        if (tempC < curve[i].tempC) {
            const float span = curve[i].tempC - curve[i - 1].tempC;
            const float t = (tempC - curve[i - 1].tempC) / span;
            return curve[i - 1].cyclesPerDay + t * (curve[i].cyclesPerDay - curve[i - 1].cyclesPerDay);
        }
    }
    return curve[n - 1].cyclesPerDay;
}

bool computeFiltrationPlan(const FiltrationPlanInput& in, FiltrationPlanOutput& out)
{
    out = FiltrationPlanOutput{};

    // Tri des fenetres valides par priorite croissante (indice comme egalite).
    uint8_t order[FILTRATION_PLAN_MAX_WINDOWS];
    uint8_t validCount = 0;
    uint16_t totalCapacity = 0;
    for (uint8_t i = 0; i < FILTRATION_PLAN_MAX_WINDOWS; ++i) {
        if (!isValidWindow_(in.windows[i])) continue;
        order[validCount++] = i;
        totalCapacity = (uint16_t)(totalCapacity + windowLength_(in.windows[i]));
    }
    for (uint8_t a = 1; a < validCount; ++a) {
        const uint8_t idx = order[a];
        uint8_t b = a;
        while (b > 0 && in.windows[order[b - 1]].priority > in.windows[idx].priority) {
            order[b] = order[b - 1];
            --b;
        }
        order[b] = idx;
    }

    const bool inputValid = isfinite(in.waterTemp) && in.poolVolumeM3 > 0.0f && in.pumpFlowM3h > 0.0f;
    out.fallback = !inputValid;

    // Aucune fenetre exploitable : segment de secours de duree minimale
    // centre sur le pivot solaire, pour ne jamais laisser l'eau sans brassage.
    if (validCount == 0) {
        const uint16_t take = PoolDefaults::FiltrationMinTotalMinutes;
        const uint16_t pivot = (uint16_t)PoolDefaults::FiltrationPivotHour * 60u;
        const uint16_t segStart = (uint16_t)((pivot + kMinutesPerDay - take / 2u) % kMinutesPerDay);
        out.segments[0].startMinute = segStart;
        out.segments[0].stopMinute = (uint16_t)((segStart + take) % kMinutesPerDay);
        out.segmentCount = 1;
        out.requiredMinutes = take;
        out.requiredRawMinutes = take;
        out.plannedMinutes = take;
        out.fallback = true;
        return true;
    }

    uint16_t required = totalCapacity;
    uint16_t requiredRaw = totalCapacity;
    if (inputValid) {
        // Ratio utilisateur sur le nombre de cycles vises (100 % = courbe de
        // reference) ; borne pour rester dans un domaine physiquement tenable.
        uint8_t ratioPct = in.cycleRatioPct;
        if (ratioPct < PoolDefaults::FiltrationCycleRatioMinPct) ratioPct = PoolDefaults::FiltrationCycleRatioMinPct;
        if (ratioPct > PoolDefaults::FiltrationCycleRatioMaxPct) ratioPct = PoolDefaults::FiltrationCycleRatioMaxPct;
        const float cycles = filtrationCyclesForTemp(in.waterTemp) * ((float)ratioPct / 100.0f);
        const float hours = (in.poolVolumeM3 * cycles) / in.pumpFlowM3h;
        long minutes = lroundf(hours * 60.0f);
        if (minutes < (long)PoolDefaults::FiltrationMinTotalMinutes) {
            minutes = (long)PoolDefaults::FiltrationMinTotalMinutes;
        }
        requiredRaw = (uint16_t)minutes;  // duree optimale, avant plafond capacite
        if (minutes > (long)totalCapacity) minutes = (long)totalCapacity;
        required = (uint16_t)minutes;
    }
    out.requiredMinutes = required;
    out.requiredRawMinutes = requiredRaw;

    // Allocation par priorite : chaque fenetre recoit ce qu'il reste, segment
    // centre dans la fenetre. Les reliquats sous la duree minimale de segment
    // sont arrondis vers le haut (jamais de cycle pompe trop court).
    uint16_t remaining = required;
    for (uint8_t a = 0; a < validCount && remaining > 0; ++a) {
        const FiltrationPlanWindow& w = in.windows[order[a]];
        const uint16_t len = windowLength_(w);
        uint16_t take = (remaining < len) ? remaining : len;
        if (take < PoolDefaults::FiltrationMinSegmentMinutes) {
            if (len < PoolDefaults::FiltrationMinSegmentMinutes) continue;
            take = PoolDefaults::FiltrationMinSegmentMinutes;
        }
        // Segment cale sur le debut de la fenetre (filtration au plus tot :
        // attaque les heures creuses des leur ouverture).
        const uint16_t segStart = w.startMinute;
        FiltrationPlanSegment& seg = out.segments[out.segmentCount++];
        seg.startMinute = segStart;
        seg.stopMinute = (uint16_t)((segStart + take) % kMinutesPerDay);
        out.plannedMinutes = (uint16_t)(out.plannedMinutes + take);
        remaining = (remaining > take) ? (uint16_t)(remaining - take) : 0;
    }

    // Besoin entierement sous la duree minimale de segment : forcer au moins
    // un segment dans la fenetre la plus prioritaire.
    if (out.segmentCount == 0) {
        const FiltrationPlanWindow& w = in.windows[order[0]];
        const uint16_t len = windowLength_(w);
        const uint16_t take = (required < len) ? required : len;
        const uint16_t segStart = w.startMinute;
        out.segments[0].startMinute = segStart;
        out.segments[0].stopMinute = (uint16_t)((segStart + take) % kMinutesPerDay);
        out.segmentCount = 1;
        out.plannedMinutes = take;
    }
    return true;
}

bool isFiltrationPlanActiveAtMinute(const FiltrationPlanOutput& plan, uint16_t minuteOfDay)
{
    const uint16_t m = (minuteOfDay < kMinutesPerDay) ? minuteOfDay : (uint16_t)(kMinutesPerDay - 1);
    for (uint8_t i = 0; i < plan.segmentCount && i < FILTRATION_PLAN_MAX_WINDOWS; ++i) {
        if (minuteInSegment_(plan.segments[i].startMinute, plan.segments[i].stopMinute, m)) return true;
    }
    return false;
}
