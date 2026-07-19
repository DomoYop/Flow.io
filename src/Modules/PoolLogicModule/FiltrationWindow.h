#pragma once
/**
 * @file FiltrationWindow.h
 * @brief Deterministic turnover-based filtration plan computation helper.
 *
 * Besoin journalier = volume bassin (m3) * cycles(T) / debit pompe (m3/h),
 * distribue dans des fenetres horaires priorisees (une fenetre peut traverser
 * minuit, ex. heures creuses 23:30-07:30).
 */

#include <stdint.h>

constexpr uint8_t FILTRATION_PLAN_MAX_WINDOWS = 3;

/** @brief Fenetre autorisee configurable (minutes depuis minuit locales). */
struct FiltrationPlanWindow {
    bool enabled = false;
    uint16_t startMinute = 0;  // 0..1439
    uint16_t stopMinute = 0;   // 0..1439 ; stop == start => vide, stop < start => traverse minuit
    uint8_t priority = 1;      // 1 = remplie en premier
};

struct FiltrationPlanInput {
    float waterTemp = 0.0f;     // NaN/inf => plan de repli (fenetres actives en entier)
    float poolVolumeM3 = 0.0f;  // <= 0 => plan de repli
    float pumpFlowM3h = 0.0f;   // <= 0 => plan de repli
    FiltrationPlanWindow windows[FILTRATION_PLAN_MAX_WINDOWS];
};

/** @brief Segment planifie ; stop < start => traverse minuit. */
struct FiltrationPlanSegment {
    uint16_t startMinute = 0;
    uint16_t stopMinute = 0;
};

struct FiltrationPlanOutput {
    // Segments ordonnes par priorite d'allocation (segments[0] = fenetre la
    // plus prioritaire retenue).
    FiltrationPlanSegment segments[FILTRATION_PLAN_MAX_WINDOWS];
    uint8_t segmentCount = 0;
    uint16_t requiredMinutes = 0;  // besoin journalier calcule
    uint16_t plannedMinutes = 0;   // somme des segments retenus
    bool fallback = false;         // entree invalide => plan de repli
};

/** @brief Cycles de renouvellement/jour pour une temperature d'eau donnee. */
float filtrationCyclesForTemp(float tempC);

bool computeFiltrationPlan(const FiltrationPlanInput& in, FiltrationPlanOutput& out);

bool isFiltrationPlanActiveAtMinute(const FiltrationPlanOutput& plan, uint16_t minuteOfDay);
