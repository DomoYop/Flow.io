/**
 * @file PoolLogicScheduler.cpp
 * @brief Scheduler and filtration plan logic for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/PoolLogicModule/FiltrationWindow.h"

#include <cstring>
#include <cstdio>
#include <math.h>
#include <time.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

void PoolLogicModule::ensureDailySlot_()
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("time.scheduler service unavailable");
        return;
    }

    // The daily recompute slot is the long-lived scheduler anchor; it does not
    // directly change outputs, it only asks the loop to rebuild the plan.
    TimeSchedulerSlot recalc{};
    recalc.slot = SLOT_DAILY_RECALC;
    recalc.eventId = POOLLOGIC_EVENT_DAILY_RECALC;
    recalc.enabled = true;
    recalc.hasEnd = false;
    recalc.replayStartOnBoot = false;
    recalc.mode = TimeSchedulerMode::RecurringClock;
    recalc.weekdayMask = TIME_WEEKDAY_ALL;
    recalc.startHour = PoolDefaults::FiltrationPivotHour;
    recalc.startMinute = 0;
    recalc.endHour = 0;
    recalc.endMinute = 0;
    recalc.startEpochSec = 0;
    recalc.endEpochSec = 0;
    strncpy(recalc.label, "poollogic_daily_recalc", sizeof(recalc.label) - 1);
    recalc.label[sizeof(recalc.label) - 1] = '\0';

    if (!schedSvc_->setSlot(schedSvc_->ctx, &recalc)) {
        LOGW("Failed to set scheduler slot %u", (unsigned)SLOT_DAILY_RECALC);
    }
}

bool PoolLogicModule::computeFiltrationPlan_(float waterTemp, FiltrationPlanOutput& out) const
{
    // The actual deterministic formula stays isolated in FiltrationWindow.cpp;
    // this method only adapts module config into that pure helper.
    FiltrationPlanInput in{};
    in.waterTemp = waterTemp;
    in.poolVolumeM3 = o2PoolVolumeM3_;
    in.pumpFlowM3h = pumpFlowM3h_;
    for (uint8_t i = 0; i < FILTRATION_PLAN_MAX_WINDOWS; ++i) {
        in.windows[i].enabled = filtrWinEnabled_[i];
        in.windows[i].startMinute = filtrWinStart_[i];
        in.windows[i].stopMinute = filtrWinStop_[i];
        in.windows[i].priority = filtrWinPriority_[i];
    }
    return computeFiltrationPlan(in, out);
}

bool PoolLogicModule::currentFiltrationPlanActive_(const FiltrationPlanOutput& plan, bool& activeOut) const
{
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return false;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return false;

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    if (epoch < 1609459200ULL) return false;

    const time_t now = (time_t)epoch;
    struct tm localNow {};
    if (!localtime_r(&now, &localNow)) return false;

    const uint16_t minuteOfDay = (uint16_t)((localNow.tm_hour * 60) + localNow.tm_min);
    activeOut = isFiltrationPlanActiveAtMinute(plan, minuteOfDay);
    return true;
}

bool PoolLogicModule::applyFiltrationPlanSlots_(const FiltrationPlanOutput& plan)
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    // PoolLogic stores the computed plan back into the shared scheduler so
    // filtration state changes continue to arrive as regular scheduler events.
    // One slot per planned segment; unused slots are disabled.
    bool allOk = true;
    for (uint8_t i = 0; i < FILTRATION_PLAN_MAX_WINDOWS; ++i) {
        TimeSchedulerSlot window{};
        window.slot = (uint8_t)(SLOT_FILTR_WINDOW_BASE + i);
        window.eventId = POOLLOGIC_EVENT_FILTRATION_WINDOW;
        window.enabled = (i < plan.segmentCount);
        window.hasEnd = true;
        window.replayStartOnBoot = true;
        window.mode = TimeSchedulerMode::RecurringClock;
        window.weekdayMask = TIME_WEEKDAY_ALL;
        if (i < plan.segmentCount) {
            window.startHour = (uint8_t)(plan.segments[i].startMinute / 60u);
            window.startMinute = (uint8_t)(plan.segments[i].startMinute % 60u);
            window.endHour = (uint8_t)(plan.segments[i].stopMinute / 60u);
            window.endMinute = (uint8_t)(plan.segments[i].stopMinute % 60u);
        }
        window.startEpochSec = 0;
        window.endEpochSec = 0;
        snprintf(window.label, sizeof(window.label), "poollogic_filtr_%u", (unsigned)(i + 1));

        if (!schedSvc_->setSlot(schedSvc_->ctx, &window)) {
            LOGW("Failed to set filtration slot=%u", (unsigned)window.slot);
            allOk = false;
        }
    }
    if (!allOk) return false;

    bool windowActive = filtrationWindowActive_;
    if (!currentFiltrationPlanActive_(plan, windowActive) && schedSvc_->isActive) {
        // Keep PoolLogic deterministic during the short gap after setSlot(),
        // before TimeModule has rebuilt its active mask.
        windowActive = false;
        for (uint8_t i = 0; i < plan.segmentCount; ++i) {
            if (schedSvc_->isActive(schedSvc_->ctx, (uint8_t)(SLOT_FILTR_WINDOW_BASE + i))) {
                windowActive = true;
                break;
            }
        }
    }

    portENTER_CRITICAL(&pendingMux_);
    filtrationPlan_ = plan;
    filtrationWindowActive_ = windowActive;
    pendingFiltrationReconcile_ = true;
    portEXIT_CRITICAL(&pendingMux_);
    return true;
}

static void formatPlanSegments_(const FiltrationPlanOutput& plan, char* out, size_t len)
{
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < plan.segmentCount && used < len; ++i) {
        const int n = snprintf(out + used,
                               len - used,
                               "%s%02u:%02u-%02u:%02u",
                               (i > 0) ? ", " : "",
                               (unsigned)(plan.segments[i].startMinute / 60u),
                               (unsigned)(plan.segments[i].startMinute % 60u),
                               (unsigned)(plan.segments[i].stopMinute / 60u),
                               (unsigned)(plan.segments[i].stopMinute % 60u));
        if (n <= 0) break;
        used += (size_t)n;
    }
}

bool PoolLogicModule::recalcAndApplyFiltrationWindow_(uint8_t* startHourOut,
                                                      uint8_t* stopHourOut,
                                                      uint8_t* durationOut)
{
    if (!schedSvc_ || !schedSvc_->setSlot) {
        LOGW("No time.scheduler service available");
        return false;
    }

    float waterTemp = NAN;
    bool hasWaterTemp = false;
    if (ioSvc_ && ioSvc_->readAnalog) {
        hasWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp);
    }
    if (!ioSvc_ || !ioSvc_->readAnalog) {
        LOGW("No IOServiceV2 available for water temperature; using fallback filtration plan");
    } else if (!hasWaterTemp) {
        LOGW("Water temperature unavailable on ioId=%u; using fallback filtration plan", (unsigned)waterTempIoId_);
    }

    FiltrationPlanOutput plan{};
    if (!computeFiltrationPlan_(waterTemp, plan) || plan.segmentCount == 0) {
        LOGW("Filtration plan computation failed");
        return false;
    }

    if (!applyFiltrationPlanSlots_(plan)) return false;

    // Enveloppe du segment prioritaire publiee en heures pour les afficheurs
    // historiques (HMI/web) qui ne connaissent qu'une plage unique.
    const uint8_t startHour = (uint8_t)(plan.segments[0].startMinute / 60u);
    const uint8_t stopHour = (uint8_t)(((plan.segments[0].stopMinute + 59u) / 60u) % 24u);
    const uint8_t duration = (uint8_t)((plan.plannedMinutes + 30u) / 60u);

    bool startStored = false;
    bool stopStored = false;
    if (cfgStore_) {
        startStored = cfgStore_->set(calcStartVar_, startHour);
        stopStored = cfgStore_->set(calcStopVar_, stopHour);
        if (!startStored || !stopStored) {
            LOGW("Failed to persist calculated filtration window start=%u stop=%u",
                 (unsigned)startHour,
                 (unsigned)stopHour);
        }
    }
    if (!cfgStore_ || !startStored) filtrationCalcStart_ = startHour;
    if (!cfgStore_ || !stopStored) filtrationCalcStop_ = stopHour;

    // Liste complete des segments planifies (sortie pure Runtime), consommee par
    // le ruban 24 h de la page piscine. Vide si aucun segment.
    formatPlanSegments_(plan, filtrationCalcSegments_, sizeof(filtrationCalcSegments_));
    filtrationOptimalMin_ = plan.requiredRawMinutes;
    if (cfgStore_) {
        cfgStore_->set(filtrSegmentsVar_, filtrationCalcSegments_);
        cfgStore_->set(filtrOptimalVar_, filtrationOptimalMin_);
    }

    if (cfgMqttPub_) {
        // Recompute commands should always refresh MQTT cfg consumers, even if
        // computed values stayed identical and ConfigStore emitted no change.
        cfgMqttPub_->requestFullSync(MqttPublishPriority::Normal);
    }
    if (startHourOut) *startHourOut = startHour;
    if (stopHourOut) *stopHourOut = stopHour;
    if (durationOut) *durationOut = duration;

    char segments[96] = {0};
    formatPlanSegments_(plan, segments, sizeof(segments));

    if (hasWaterTemp) {
        LOGI("Filtration plan required=%umin planned=%umin water=%.2fC segments=%s",
             (unsigned)plan.requiredMinutes,
             (unsigned)plan.plannedMinutes,
             (double)waterTemp,
             segments);
        char detail[160] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "Température eau %.2f °C, besoin %u h %02u, plan %s.",
                 (double)waterTemp,
                 (unsigned)(plan.requiredMinutes / 60u),
                 (unsigned)(plan.requiredMinutes % 60u),
                 segments);
        emitActivity_(ActivityCode::PoolLogicFiltrationWindowCalculated,
                      ActivitySource::Scheduler,
                      ActivitySeverity::Info,
                      ActivityRole::Filtration,
                      ActivityState::None,
                      ActivityReason::Scheduler,
                      filtrationDeviceSlot_,
                      "Plan de filtration recalculé",
                      detail,
                      "schedule");
    } else {
        LOGI("Filtration plan fallback planned=%umin segments=%s",
             (unsigned)plan.plannedMinutes,
             segments);
        char detail[160] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "Température eau indisponible, plan de repli %s.",
                 segments);
        emitActivity_(ActivityCode::PoolLogicFiltrationWindowCalculated,
                      ActivitySource::Scheduler,
                      ActivitySeverity::Warning,
                      ActivityRole::Filtration,
                      ActivityState::None,
                      ActivityReason::Scheduler,
                      filtrationDeviceSlot_,
                      "Plan de filtration recalculé",
                      detail,
                      "schedule");
    }
    return true;
}
