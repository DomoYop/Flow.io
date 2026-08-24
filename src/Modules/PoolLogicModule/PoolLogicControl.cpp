/**
 * @file PoolLogicControl.cpp
 * @brief Control loop, device state, and alarm conditions for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
constexpr float kHeaterHysteresisC = 0.3f;
constexpr uint32_t kHeaterTempFreshMaxMs = 10UL * 60UL * 1000UL;
constexpr uint16_t kHeatAssistProbeRunSec = 5U * 60U;
constexpr uint16_t kHeatAssistIdleSlowSec = 30U * 60U;
constexpr uint16_t kHeatAssistIdleFastSec = 20U * 60U;
constexpr uint8_t kHeatAssistFlagProbeRunning = (1U << 0);
constexpr uint8_t kHeatAssistFlagHeatingActive = (1U << 1);
constexpr uint8_t kHeatAssistFlagFastCycle = (1U << 2);
constexpr uint32_t kO2PersistPeriodMs = 30000UL;
constexpr float kO2DoseEpsilonMl = 0.5f;
// Cadence de re-tentative d'une commande d'equipement non honoree.
constexpr uint32_t kDeviceRetryPeriodMs = 5000UL;
// Une cause de blocage dure typiquement des heures : on ne repete le log qu'a
// intervalle long, sinon le retry 5 s inonde la console.
constexpr uint32_t kBlockLogRepeatMs = 30UL * 60UL * 1000UL;
constexpr uint8_t kNoLoggedBlockReason = 0xFFU;

const char* poolDeviceSvcStatusStr_(PoolDeviceSvcStatus st)
{
    switch (st) {
        case POOLDEV_SVC_OK: return "ok";
        case POOLDEV_SVC_ERR_INVALID_ARG: return "invalid_arg";
        case POOLDEV_SVC_ERR_UNKNOWN_SLOT: return "unknown_slot";
        case POOLDEV_SVC_ERR_NOT_READY: return "not_ready";
        case POOLDEV_SVC_ERR_DISABLED: return "disabled";
        case POOLDEV_SVC_ERR_INTERLOCK: return "interlock";
        case POOLDEV_SVC_ERR_IO: return "io";
        case POOLDEV_SVC_ERR_MAX_UPTIME: return "max_uptime";
        case POOLDEV_SVC_ERR_WRITES_DISABLED: return "writes_disabled";
        default: return "unknown";
    }
}

const char* poolDeviceBlockReasonStr_(uint8_t reason)
{
    switch (reason) {
        case POOL_DEVICE_BLOCK_NONE: return "none";
        case POOL_DEVICE_BLOCK_DISABLED: return "disabled";
        case POOL_DEVICE_BLOCK_INTERLOCK: return "interlock";
        case POOL_DEVICE_BLOCK_IO_ERROR: return "io_error";
        case POOL_DEVICE_BLOCK_MAX_UPTIME: return "max_uptime";
        // Actionneur non lie a un port : etat normal pour le mode de desinfection
        // non utilise (voir kDigitalOutputRoleDefaults / binding_port).
        case POOL_DEVICE_BLOCK_UNBOUND: return "unbound";
        default: return "unknown";
    }
}

uint16_t o2WeekKeyFromDayKey_(uint16_t dayKey)
{
    return (uint16_t)(dayKey / 7U);
}
}  // namespace

/**
 * Compare au mode fige au demarrage, pas au reglage courant : les equipements,
 * les variables de configuration et les entites ont ete declares pour celui-la.
 * Basculer la strategie a chaud piloterait une pompe qui n'existe pas.
 */
bool PoolLogicModule::isDisinfectionType_(DisinfectionType type) const
{
    return bootDisinfectionType_ == (uint8_t)type;
}

const char* PoolLogicModule::disinfectionTypeStr_(uint8_t type)
{
    switch (type) {
        case DisinfectionChlorineBromine: return "chlorine_bromine";
        case DisinfectionSwg: return "swg";
        case DisinfectionActiveOxygen: return "active_oxygen";
        case DisinfectionDisabled: return "disabled";
        default: return "unknown";
    }
}

const char* PoolLogicModule::swgControlModeStr_(uint8_t mode)
{
    switch (mode) {
        case SwgControlOrp: return "orp";
        case SwgControlContinuous: return "continuous";
        default: return "unknown";
    }
}

const char* PoolLogicModule::o2ProtocolStateStr_(uint8_t state)
{
    switch (state) {
        case O2ProtocolIdle: return "idle";
        case O2ProtocolPending: return "pending";
        case O2ProtocolDosing: return "dosing";
        case O2ProtocolBlocked: return "blocked";
        default: return "unknown";
    }
}

const char* PoolLogicModule::o2BlockReasonStr_(uint8_t reason)
{
    switch (reason) {
        case O2BlockNone: return "none";
        case O2BlockInactive: return "inactive";
        case O2BlockTimeUnsynced: return "time_unsynced";
        case O2BlockPressure: return "pressure";
        case O2BlockTankLow: return "tank_low";
        case O2BlockFlowInvalid: return "flow_invalid";
        case O2BlockFiltrationWait: return "filtration_wait";
        case O2BlockPumpService: return "pump_service";
        case O2BlockPumpBlocked: return "pump_blocked";
        case O2BlockConfig: return "config";
        default: return "unknown";
    }
}

ActivityRole PoolLogicModule::activityRoleForDeviceSlot_(uint8_t deviceSlot) const
{
    if (deviceSlot == filtrationDeviceSlot_) return ActivityRole::Filtration;
    if (deviceSlot == swgDeviceSlot_) return ActivityRole::Swg;
    if (deviceSlot == robotDeviceSlot_) return ActivityRole::Robot;
    if (deviceSlot == fillingDeviceSlot_) return ActivityRole::Filling;
    if (deviceSlot == phPumpDeviceSlot_) return ActivityRole::Ph;
    if (deviceSlot == orpPumpDeviceSlot_) return ActivityRole::Disinfection;
    if (deviceSlot == heaterDeviceSlot_) return ActivityRole::Heater;
    return ActivityRole::None;
}

const char* PoolLogicModule::activityRoleLabel_(ActivityRole role) const
{
    switch (role) {
        case ActivityRole::Filtration: return "Filtration";
        case ActivityRole::Swg: return "Électrolyseur";
        case ActivityRole::Robot: return "Robot";
        case ActivityRole::Filling: return "Remplissage";
        case ActivityRole::Ph: return "Pompe pH";
        case ActivityRole::Disinfection: return "Désinfection";
        case ActivityRole::Heater: return "Chauffage";
        case ActivityRole::None:
        default: return "Équipement";
    }
}

void PoolLogicModule::emitActivity_(ActivityCode code,
                                    ActivitySource source,
                                    ActivitySeverity severity,
                                    ActivityRole role,
                                    ActivityState state,
                                    ActivityReason reason,
                                    uint8_t deviceSlot,
                                    const char* title,
                                    const char* detail,
                                    const char* icon) const
{
    if (!activityLogSvc_ || !activityLogSvc_->emit) return;
    ActivityEvent event{};
    event.code = (uint16_t)code;
    event.domain = (uint8_t)ActivityDomain::PoolLogic;
    event.source = (uint8_t)source;
    event.severity = (uint8_t)severity;
    event.role = (uint8_t)role;
    event.state = (uint8_t)state;
    event.reason = (uint8_t)reason;
    event.targetSlot = deviceSlot;
    snprintf(event.title, sizeof(event.title), "%s", title ? title : "PoolLogic");
    snprintf(event.detail, sizeof(event.detail), "%s", detail ? detail : "");
    snprintf(event.icon, sizeof(event.icon), "%s", icon ? icon : "pool");
    (void)activityLogSvc_->emit(activityLogSvc_->ctx, &event);
}

void PoolLogicModule::emitDeviceActivity_(bool requested,
                                          bool on,
                                          uint8_t deviceSlot,
                                          const char* label,
                                          ActivityReason reason) const
{
    const ActivityRole role = activityRoleForDeviceSlot_(deviceSlot);
    const char* roleLabel = activityRoleLabel_(role);
    const char* deviceLabel = (label && label[0] != '\0') ? label : roleLabel;
    char title[48] = {0};
    char detail[128] = {0};
    const char* icon = "power_settings_new";
    if (role == ActivityRole::Filtration) icon = "pool";
    else if (role == ActivityRole::Robot) icon = "cleaning_services";
    else if (role == ActivityRole::Ph || role == ActivityRole::Disinfection) icon = "science";
    else if (role == ActivityRole::Heater) icon = "heat_pump";
    else if (role == ActivityRole::Filling) icon = "water_drop";
    else if (role == ActivityRole::Swg) icon = "bolt";

    if (requested) {
        snprintf(title,
                 sizeof(title),
                 "%s %s demandé",
                 deviceLabel,
                 on ? "ON" : "OFF");
        snprintf(detail,
                 sizeof(detail),
                 "PoolLogic demande %s pour %s (slot %u).",
                 on ? "le démarrage" : "l'arrêt",
                 roleLabel,
                 (unsigned)deviceSlot);
        emitActivity_(on ? ActivityCode::PoolLogicDeviceStartRequested
                         : ActivityCode::PoolLogicDeviceStopRequested,
                      ActivitySource::Auto,
                      ActivitySeverity::Info,
                      role,
                      on ? ActivityState::RequestedOn : ActivityState::RequestedOff,
                      reason,
                      deviceSlot,
                      title,
                      detail,
                      icon);
        return;
    }

    snprintf(title,
             sizeof(title),
             "%s %s",
             deviceLabel,
             on ? "a démarré" : "s'est arrêté");
    snprintf(detail,
             sizeof(detail),
             "État réel observé pour %s (slot %u).",
             roleLabel,
             (unsigned)deviceSlot);
    emitActivity_(on ? ActivityCode::PoolLogicDeviceStarted : ActivityCode::PoolLogicDeviceStopped,
                  ActivitySource::Auto,
                  on ? ActivitySeverity::Success : ActivitySeverity::Info,
                  role,
                  on ? ActivityState::On : ActivityState::Off,
                  ActivityReason::None,
                  deviceSlot,
                  title,
                  detail,
                  icon);
}

void PoolLogicModule::emitAutoModeDisabledByManualActivity_(ActivityRole role,
                                                            uint8_t deviceSlot,
                                                            const char* autoLabel) const
{
    const char* roleLabel = activityRoleLabel_(role);
    const char* label = (autoLabel && autoLabel[0] != '\0') ? autoLabel : roleLabel;
    const char* icon = "settings";
    if (role == ActivityRole::Filtration) icon = "pool";
    else if (role == ActivityRole::Ph || role == ActivityRole::Disinfection) icon = "science";

    char title[48] = {0};
    char detail[128] = {0};
    snprintf(title, sizeof(title), "Mode auto %s désactivé", label);
    snprintf(detail,
             sizeof(detail),
             "Commande manuelle sur %s: l'automatisme a été désactivé.",
             roleLabel);
    emitActivity_(ActivityCode::SystemConfigChanged,
                  ActivitySource::Manual,
                  ActivitySeverity::Info,
                  role,
                  ActivityState::None,
                  ActivityReason::Manual,
                  deviceSlot,
                  title,
                  detail,
                  icon);
}

bool PoolLogicModule::readPoolDeviceFlowLh_(uint8_t deviceSlot, float& flowLhOut) const
{
    flowLhOut = 0.0f;
    if (!cfgStore_ || deviceSlot >= POOL_DEVICE_MAX) return false;

    char moduleName[16] = {0};
    snprintf(moduleName, sizeof(moduleName), "pdm/pd%u", (unsigned)deviceSlot);

    bool truncated = false;
    if (!cfgStore_->toJsonModule(moduleName,
                                 o2PoolDeviceJsonBuf_,
                                 sizeof(o2PoolDeviceJsonBuf_),
                                 &truncated) ||
        truncated) {
        return false;
    }

    const char* key = strstr(o2PoolDeviceJsonBuf_, "\"flow_l_h\":");
    if (!key) return false;
    key += 11;
    char* end = nullptr;
    const float flow = strtof(key, &end);
    if (end == key) return false;
    if (!std::isfinite(flow) || flow <= 0.0f) return false;
    flowLhOut = flow;
    return true;
}

// Metriques volumetriques de la pompe pH, relues a 1 Hz : elles ne bougent pas
// plus vite cote PoolDevice, inutile de les interroger au tick 200 ms.
void PoolLogicModule::refreshPhPumpMetrics_(uint32_t nowMs)
{
    if (phMetricsReadMs_ != 0U && (uint32_t)(nowMs - phMetricsReadMs_) < 1000UL) return;
    phMetricsReadMs_ = nowMs;

    if (!poolSvc_ || !poolSvc_->meta || phPumpDeviceSlot_ >= POOL_DEVICE_MAX) {
        phPumpFlowLh_ = 0.0f;
        phDosedTodayMl_ = 0.0f;
        phTankRemainMl_ = 0.0f;
        return;
    }

    PoolDeviceSvcMeta meta{};
    if (poolSvc_->meta(poolSvc_->ctx, phPumpDeviceSlot_, &meta) != POOLDEV_SVC_OK) {
        phPumpFlowLh_ = 0.0f;
        phDosedTodayMl_ = 0.0f;
        phTankRemainMl_ = 0.0f;
        return;
    }
    phPumpFlowLh_ = meta.flowLPerHour;
    // injectedMlDay est persiste dans le blob pdNrt : le quota journalier
    // survit donc a un redemarrage dans la journee.
    phDosedTodayMl_ = meta.injectedMlDay;
    phTankRemainMl_ = meta.tankRemainingMl;
}

// Reprise apres redemarrage : la FSM n'est pas persistee (millis() repart a 0),
// mais l'horodatage du dernier lot l'est. Sans cela, un reboot juste apres un
// lot ferait redoser immediatement, alors que le turnover n'a pas eu lieu.
void PoolLogicModule::resumePhDosingAfterBoot_(uint32_t nowMs)
{
    phDosingResumeChecked_ = true;
    if (phLastDoseTs_ <= 0) return;
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) {
        // Horloge non synchronisee : on ne peut pas dater le dernier lot, on
        // repart en mesure. Degradation explicite et sure.
        return;
    }

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    if (epoch <= (uint64_t)phLastDoseTs_) return;
    const uint64_t elapsedSec = epoch - (uint64_t)phLastDoseTs_;

    DosingInput probe{};
    fillPhDosingInput_(probe, false, 0.0f, 0xFFFFFFFFU, nowMs);
    const uint32_t mixWaitMs = computeMixWaitMs(probe);
    const uint64_t elapsedMs = elapsedSec * 1000ULL;
    if (elapsedMs >= (uint64_t)mixWaitMs) return;

    phDosingState_.phase = DOSING_PHASE_MIXING;
    phDosingState_.blockReason = DOSING_BLOCK_NONE;
    phDosingState_.phaseSinceMs = nowMs;
    phDosingState_.mixWaitMs = mixWaitMs;
    phDosingState_.mixElapsedMs = (uint32_t)elapsedMs;
    phDosingTsMs_ = nowMs;
    LOGI("pH dosing resumed in mixing: %lu s elapsed of %lu s",
         (unsigned long)elapsedSec,
         (unsigned long)(mixWaitMs / 1000UL));
}

void PoolLogicModule::fillPhDosingInput_(DosingInput& in,
                                         bool havePh,
                                         float ph,
                                         uint32_t phAgeMs,
                                         uint32_t nowMs) const
{
    in.nowMs = nowMs;
    // L'armement conserve la temporisation historique : filtration en marche
    // depuis delayPidsMin, hors mode hiver.
    in.regulationArmed = phPidEnabled_;
    in.interlockBlocked = hydraulicTrip_ || noFlowError_;
    in.tankLow = phTankLowError_;
    in.circulating = filtrationFsm_.on;

    in.haveSample = havePh;
    in.measured = ph;
    in.sampleAgeMs = phAgeMs;
    in.sampleMaxAgeMs = (uint32_t)phSampleMaxAgeS_ * 1000UL;
    in.setpoint = phSetpoint_;
    in.validMin = phValidMin_;
    in.validMax = phValidMax_;
    in.deadband = phDeadband_;
    in.dosePlus = phDosePlus_;

    in.poolVolumeM3 = poolVolumeM3_;
    in.filtrationFlowM3h = pumpFlowM3h_;
    in.pumpFlowLPerHour = phPumpFlowLh_;

    // Le gain appris prend le pas sur la valeur configuree une fois calibre.
    in.gainMlPerM3PerStep = phEffectiveGainMlPerM3_();
    in.referenceGain = phDoseMlPerM3_;
    in.unitStep = PoolDefaults::PhDoseUnitStep;
    in.safetyFactor = phDoseFactor_;
    in.maxBatchMl = phDoseMaxBatchMl_;
    in.maxDayMl = phDoseMaxDayMl_;
    in.dosedTodayMl = phDosedTodayMl_;
    in.tankRemainingMl = phTankRemainMl_;
    in.mixWaitMinCfg = phMixWaitMin_;

    in.pumpActualOn = phPumpFsm_.on;
    in.pumpWriteRejected = phPumpFsm_.writeRejected;

    in.noEffectThreshold = phNoEffectDelta_;
    in.noEffectBatches = phNoEffectBatches_;
}

void PoolLogicModule::stepPhDosing_(bool havePh,
                                    float ph,
                                    uint32_t phAgeMs,
                                    uint32_t nowMs,
                                    bool& phPumpDesired)
{
    refreshPhPumpMetrics_(nowMs);
    if (!phDosingResumeChecked_) resumePhDosingAfterBoot_(nowMs);

    DosingInput in{};
    fillPhDosingInput_(in, havePh, ph, phAgeMs, nowMs);

    DosingOutput out{};
    (void)stepDosingController(phDosingState_, in, out);
    phPumpDesired = out.pumpOn;

    if (out.phase != phDosingLast_.phase || out.blockReason != phDosingLast_.blockReason) {
        phDosingTsMs_ = nowMs;
    }
    phDosingLast_ = out;
    publishPhDosingRuntime_();

    if (out.batchCompleted) persistPhDosingResult_(out, nowMs);
}

// Remet la FSM de dosage au repos sans perdre l'apprentissage : le gain vit
// dans la config persistante, l'etat FSM n'en est qu'une copie de travail.
void PoolLogicModule::resetPhDosingState_(uint32_t nowMs)
{
    phDosingState_ = DosingState{};
    phDosingState_.learnedGain = phGainLearned_;
    phDosingState_.gainSampleCount = phGainSamples_;
    phDosingLast_ = DosingOutput{};
    phDosingTsMs_ = nowMs;
    publishPhDosingRuntime_();
}

// Le gain appris est exprime relativement au gain configure et au volume du
// bassin : si l'un des deux change, l'apprentissage precedent n'est plus
// reference au bon point et doit repartir de zero.
void PoolLogicModule::resetPhLearnedGain_(const char* reason)
{
    if (phGainLearned_ == 0.0f && phGainSamples_ == 0U) return;
    phGainLearned_ = 0.0f;
    phGainSamples_ = 0;
    phDosingState_.learnedGain = 0.0f;
    phDosingState_.gainSampleCount = 0;
    if (cfgStore_) {
        (void)cfgStore_->set(phGainLearnedVar_, phGainLearned_);
        (void)cfgStore_->set(phGainSamplesVar_, phGainSamples_);
    }
    LOGI("pH learned gain reset (%s)", reason ? reason : "");
}

// Ecrit une fois par lot termine, donc au plus toutes les quelques heures :
// l'usure NVS est negligeable et le gain reste diagnosticable dans cfg/.
void PoolLogicModule::persistPhDosingResult_(const DosingOutput& out, uint32_t nowMs)
{
    (void)nowMs;
    if (!cfgStore_) return;

    if (out.gainUpdated) {
        phGainLearned_ = out.learnedGain;
        phGainSamples_ = phDosingState_.gainSampleCount;
        (void)cfgStore_->set(phGainLearnedVar_, phGainLearned_);
        (void)cfgStore_->set(phGainSamplesVar_, phGainSamples_);
    }

    // Horodatage epoch de fin de lot : sert a reprendre le melange apres reboot.
    if (timeSvc_ && timeSvc_->isSynced && timeSvc_->epoch && timeSvc_->isSynced(timeSvc_->ctx)) {
        const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
        if (epoch > 0ULL && epoch < 0x7FFFFFFFULL) {
            phLastDoseTs_ = (int32_t)epoch;
            (void)cfgStore_->set(phLastDoseTsVar_, phLastDoseTs_);
        }
    }
}

bool PoolLogicModule::currentO2LocalTime_(uint16_t& dayKeyOut,
                                          uint16_t& weekKeyOut,
                                          uint8_t& weekDayMon0Out,
                                          uint8_t& hourOut) const
{
    dayKeyOut = 0;
    weekKeyOut = 0;
    weekDayMon0Out = 0;
    hourOut = 0;
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch || !timeSvc_->isSynced(timeSvc_->ctx)) {
        return false;
    }

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    if (epoch < 1609459200ULL) return false;
    const time_t now = (time_t)epoch;
    tm localNow{};
    if (!localtime_r(&now, &localNow)) return false;

    const uint64_t epochDay = epoch / 86400ULL;
    dayKeyOut = (uint16_t)(epochDay & 0xFFFFU);
    weekKeyOut = (uint16_t)((epochDay / 7ULL) & 0xFFFFU);
    weekDayMon0Out = (uint8_t)((localNow.tm_wday + 6) % 7);
    hourOut = (uint8_t)localNow.tm_hour;
    return true;
}

bool PoolLogicModule::isO2DoseDay_(uint8_t weekDayMon0) const
{
    if (o2SplitCount_ <= 1U) return weekDayMon0 == 0U;
    if (o2SplitCount_ == 2U) return weekDayMon0 == 0U || weekDayMon0 == 3U;
    return weekDayMon0 == 0U || weekDayMon0 == 2U || weekDayMon0 == 4U;
}

float PoolLogicModule::o2TemperatureFactor_(bool haveWaterTemp, float waterTemp) const
{
    if (!o2TempComp_ || !haveWaterTemp || !std::isfinite(waterTemp)) return 1.0f;
    if (waterTemp <= 18.0f) {
        const float factor = 1.0f - ((18.0f - waterTemp) * 0.02f);
        return (factor < 0.75f) ? 0.75f : factor;
    }
    if (waterTemp >= 24.0f) {
        const float factor = 1.0f + ((waterTemp - 24.0f) * 0.03f);
        return (factor > 1.50f) ? 1.50f : factor;
    }
    return 1.0f;
}

float PoolLogicModule::computeO2WeeklyDoseMl_(bool haveWaterTemp, float waterTemp) const
{
    if (!std::isfinite(poolVolumeM3_) || poolVolumeM3_ <= 0.0f ||
        !std::isfinite(o2DoseMlPer10M3Week_) || o2DoseMlPer10M3Week_ <= 0.0f ||
        !std::isfinite(o2LoadFactor_) || o2LoadFactor_ <= 0.0f) {
        return 0.0f;
    }

    const float base = (poolVolumeM3_ / 10.0f) * o2DoseMlPer10M3Week_;
    const float dose = base * o2LoadFactor_ * o2TemperatureFactor_(haveWaterTemp, waterTemp);
    if (!std::isfinite(dose) || dose <= 0.0f) return 0.0f;
    return dose;
}

void PoolLogicModule::setO2ProtocolState_(uint8_t state, uint8_t blockReason, uint32_t nowMs)
{
    if (state > O2ProtocolBlocked) state = O2ProtocolIdle;
    if (o2ProtocolState_ != state || o2BlockReason_ != blockReason) {
        o2ProtocolState_ = state;
        o2BlockReason_ = blockReason;
        o2LastPersistMs_ = 0;
        LOGI("O2 protocol state=%s block=%s pending=%.1f done=%.1f",
             o2ProtocolStateStr_(o2ProtocolState_),
             o2BlockReasonStr_(o2BlockReason_),
             (double)o2PendingMl_,
             (double)o2WeeklyDoneMl_);
        char detail[128] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "État=%s, blocage=%s, restant=%.1f ml, semaine=%.1f ml.",
                 o2ProtocolStateStr_(o2ProtocolState_),
                 o2BlockReasonStr_(o2BlockReason_),
                 (double)o2PendingMl_,
                 (double)o2WeeklyDoneMl_);
        if (isDisinfectionType_(DisinfectionActiveOxygen)) {
            emitActivity_(ActivityCode::PoolLogicO2StateChanged,
                          (blockReason == O2BlockNone) ? ActivitySource::Auto : ActivitySource::Safety,
                          (blockReason == O2BlockNone) ? ActivitySeverity::Info : ActivitySeverity::Warning,
                          ActivityRole::Disinfection,
                          ActivityState::None,
                          ActivityReason::O2,
                          orpPumpDeviceSlot_,
                          "Protocole oxygène actif mis à jour",
                          detail,
                          "science");
        }
    }
    persistO2Protocol_(nowMs, false);
}

void PoolLogicModule::persistO2Protocol_(uint32_t nowMs, bool force)
{
    if (!cfgStore_) return;
    if (!force && o2LastPersistMs_ != 0U && (uint32_t)(nowMs - o2LastPersistMs_) < kO2PersistPeriodMs) {
        return;
    }

    (void)cfgStore_->set(o2ProtocolStateVar_, o2ProtocolState_);
    (void)cfgStore_->set(o2LastDoseDayVar_, o2LastDoseDay_);
    (void)cfgStore_->set(o2WeeklyDoneVar_, o2WeeklyDoneMl_);
    (void)cfgStore_->set(o2PendingVar_, o2PendingMl_);
    o2LastPersistMs_ = nowMs ? nowMs : 1U;
}

bool PoolLogicModule::stepO2Protocol_(bool filtrationDesired,
                                      bool filtrationOn,
                                      uint32_t filtrationRunMin,
                                      bool haveWaterTemp,
                                      float waterTemp,
                                      bool pressureError,
                                      bool tankLow,
                                      uint32_t nowMs,
                                      bool& requestFiltrationOut,
                                      bool& pumpDesiredOut)
{
    requestFiltrationOut = false;
    pumpDesiredOut = false;

    if (!isDisinfectionType_(DisinfectionActiveOxygen) || !autoMode_) {
        o2LastProgressMs_ = 0;
        if (o2PendingMl_ <= kO2DoseEpsilonMl) {
            o2PendingMl_ = 0.0f;
            setO2ProtocolState_(O2ProtocolIdle, O2BlockInactive, nowMs);
        } else {
            setO2ProtocolState_(O2ProtocolPending, O2BlockInactive, nowMs);
        }
        return false;
    }

    uint16_t dayKey = 0;
    uint16_t weekKey = 0;
    uint8_t weekDayMon0 = 0;
    uint8_t hour = 0;
    if (!currentO2LocalTime_(dayKey, weekKey, weekDayMon0, hour)) {
        o2LastProgressMs_ = 0;
        if (o2PendingMl_ > kO2DoseEpsilonMl) requestFiltrationOut = true;
        setO2ProtocolState_(o2PendingMl_ > kO2DoseEpsilonMl ? O2ProtocolBlocked : O2ProtocolIdle,
                            O2BlockTimeUnsynced,
                            nowMs);
        return false;
    }

    if (o2LastDoseDay_ != 0U && o2WeekKeyFromDayKey_(o2LastDoseDay_) != weekKey && o2WeeklyDoneMl_ > 0.0f) {
        o2WeeklyDoneMl_ = 0.0f;
        persistO2Protocol_(nowMs, true);
    }

    const float weeklyDoseMl = computeO2WeeklyDoseMl_(haveWaterTemp, waterTemp);
    const uint8_t split = (o2SplitCount_ == 0U) ? 1U : ((o2SplitCount_ > 3U) ? 3U : o2SplitCount_);
    const float doseMl = (split > 0U) ? (weeklyDoseMl / (float)split) : weeklyDoseMl;
    o2LastPlannedDoseMl_ = doseMl;

    if (o2PendingMl_ <= kO2DoseEpsilonMl) {
        o2PendingMl_ = 0.0f;
        if (weeklyDoseMl <= kO2DoseEpsilonMl || doseMl <= kO2DoseEpsilonMl) {
            setO2ProtocolState_(O2ProtocolBlocked, O2BlockConfig, nowMs);
            return false;
        }
        const bool dueToday =
            isO2DoseDay_(weekDayMon0) &&
            hour >= o2MainHour_ &&
            o2LastDoseDay_ != dayKey &&
            o2WeeklyDoneMl_ < (weeklyDoseMl - kO2DoseEpsilonMl);
        if (dueToday) {
            const float remainingWeekMl = weeklyDoseMl - o2WeeklyDoneMl_;
            o2PendingMl_ = (remainingWeekMl < doseMl) ? remainingWeekMl : doseMl;
            if (o2PendingMl_ < 0.0f) o2PendingMl_ = 0.0f;
            o2LastProgressMs_ = 0;
            setO2ProtocolState_(O2ProtocolPending, O2BlockNone, nowMs);
            persistO2Protocol_(nowMs, true);
        }
    }

    if (o2PendingMl_ <= kO2DoseEpsilonMl) {
        o2PendingMl_ = 0.0f;
        setO2ProtocolState_(O2ProtocolIdle, O2BlockNone, nowMs);
        return false;
    }

    requestFiltrationOut = true;
    if (pressureError) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPressure, nowMs);
        return false;
    }
    if (tankLow) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockTankLow, nowMs);
        return false;
    }

    float flowLh = 0.0f;
    if (!readPoolDeviceFlowLh_(orpPumpDeviceSlot_, flowLh)) {
        o2LastFlowLh_ = 0.0f;
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockFlowInvalid, nowMs);
        return false;
    }
    o2LastFlowLh_ = flowLh;

    const bool filtrationReady = filtrationOn && filtrationDesired && filtrationRunMin >= o2MinFilterRunMin_;
    if (!filtrationReady) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolPending, O2BlockFiltrationWait, nowMs);
        return false;
    }

    PoolDeviceSvcMeta meta{};
    if (!poolSvc_ || !poolSvc_->meta || poolSvc_->meta(poolSvc_->ctx, orpPumpDeviceSlot_, &meta) != POOLDEV_SVC_OK) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPumpService, nowMs);
        return false;
    }
    if (meta.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME ||
        meta.blockReason == POOL_DEVICE_BLOCK_DISABLED ||
        meta.blockReason == POOL_DEVICE_BLOCK_INTERLOCK ||
        meta.blockReason == POOL_DEVICE_BLOCK_IO_ERROR) {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolBlocked, O2BlockPumpBlocked, nowMs);
        return false;
    }

    pumpDesiredOut = true;
    if (orpPumpFsm_.on) {
        const uint32_t deltaMs = (o2LastProgressMs_ == 0U) ? 0U : (nowMs - o2LastProgressMs_);
        o2LastProgressMs_ = nowMs ? nowMs : 1U;
        if (deltaMs > 0U) {
            const float injectedMl = (flowLh / 3600.0f) * (float)deltaMs;
            if (std::isfinite(injectedMl) && injectedMl > 0.0f) {
                o2PendingMl_ -= injectedMl;
                o2WeeklyDoneMl_ += injectedMl;
                if (o2PendingMl_ <= kO2DoseEpsilonMl) {
                    if (o2PendingMl_ < 0.0f) {
                        o2WeeklyDoneMl_ += o2PendingMl_;
                    }
                    o2PendingMl_ = 0.0f;
                    o2LastDoseDay_ = dayKey;
                    pumpDesiredOut = false;
                    o2LastProgressMs_ = 0;
                    setO2ProtocolState_(O2ProtocolIdle, O2BlockNone, nowMs);
                    persistO2Protocol_(nowMs, true);
                    return true;
                }
                setO2ProtocolState_(O2ProtocolDosing, O2BlockNone, nowMs);
                persistO2Protocol_(nowMs, false);
            }
        } else {
            setO2ProtocolState_(O2ProtocolDosing, O2BlockNone, nowMs);
        }
    } else {
        o2LastProgressMs_ = 0;
        setO2ProtocolState_(O2ProtocolPending, O2BlockNone, nowMs);
    }

    return pumpDesiredOut;
}

// Alarm conditions intentionally stay close to the control helpers because they
// read the same live IO/runtime state and should evolve together.
bool PoolLogicModule::readFlowConfirmed_() const
{
    if (!flowPresent_ && !flowInterlockEnabled_) return false;
    bool flowOn = false;
    if (!loadDigitalSensor_(flowSwitchIoId_, flowOn)) return false;
    return flowOn;
}

float PoolLogicModule::computeFoulingThreshold_(bool* clampedOut) const
{
    if (clampedOut) *clampedOut = false;
    // Reference non calibree : rien a comparer, l'alerte reste muette plutot que
    // de se declencher sur une valeur inventee.
    if (!(pressureRefBar_ > 0.0f)) return 0.0f;
    if (!(pressureFoulingDeltaBar_ > 0.0f)) return 0.0f;

    const float wanted = pressureRefBar_ + pressureFoulingDeltaBar_;
    const float ceiling = pressureHighThreshold_ - kFoulingTripMarginBar;
    if (!(ceiling > 0.0f)) return 0.0f;
    if (wanted <= ceiling) return wanted;

    if (clampedOut) *clampedOut = true;
    return ceiling;
}

// Publie l'etat d'encrassement et apprend la pression de service quand elle
// n'est pas encore connue. Appelee a chaque pas de controle : tout y est
// recalcule a partir de la config, pour que l'interface reflete un changement de
// reglage sans attendre un evenement.
void PoolLogicModule::updatePressureReference_(bool havePressure, float pressure, uint32_t nowMs)
{
    bool clamped = false;
    const float threshold = computeFoulingThreshold_(&clamped);
    foulingThresholdBar_ = threshold;
    if (clamped && !foulingThresholdClamped_) {
        // Le trip mecanique passe devant l'alerte de lavage : sur une
        // installation a forte pression de service, l'arret d'urgence sonnerait
        // avant l'invitation a laver le filtre. Le dire une fois, avec les trois
        // valeurs, evite d'avoir a le deduire depuis l'interface.
        LOGW("Fouling threshold clamped to %.2f bar (ref %.2f + delta %.2f exceeds trip %.2f)",
             (double)threshold,
             (double)pressureRefBar_,
             (double)pressureFoulingDeltaBar_,
             (double)pressureHighThreshold_);
    }
    foulingThresholdClamped_ = clamped;

    // Chemin parcouru entre le filtre propre et le lavage. Non plafonne a 100 :
    // « 140 % » dit quelque chose qu'un plafond effacerait.
    if (havePressure && (threshold > pressureRefBar_)) {
        const float span = threshold - pressureRefBar_;
        const float pct = ((pressure - pressureRefBar_) / span) * 100.0f;
        foulingPct_ = (pct < 0.0f) ? 0.0f : pct;
    } else {
        foulingPct_ = 0.0f;
    }

    auto resetLearning = [this]() {
        pressureLearnStartMs_ = 0U;
        pressureLearnSum_ = 0.0f;
        pressureLearnCount_ = 0U;
    };

    // Une reference deja posee -- apprise ou saisie a la main -- n'est jamais
    // ecrasee. C'est ce qui rend le champ modifiable sans que l'apprentissage
    // vienne le reprendre au passage suivant.
    if (pressureRefBar_ > 0.0f) {
        resetLearning();
        return;
    }

    // Marche stable, debit confirme, aucune coupure : les trois conditions pour
    // qu'une pression instantanee vaille comme pression de service.
    const bool eligible = havePressure && filtrationFsm_.on && !hydraulicTrip_ &&
                          readFlowConfirmed_() &&
                          (stateUptimeSec_(filtrationFsm_, nowMs) >= (kPressureLearnRunMs / 1000UL));
    if (!eligible) {
        resetLearning();
        return;
    }

    if (pressureLearnStartMs_ == 0U) {
        pressureLearnStartMs_ = nowMs;
        pressureLearnSum_ = 0.0f;
        pressureLearnCount_ = 0U;
    }
    if (pressureLearnCount_ < 0xFFFFU) {
        pressureLearnSum_ += pressure;
        ++pressureLearnCount_;
    }
    if ((uint32_t)(nowMs - pressureLearnStartMs_) < kPressureLearnWindowMs) return;
    if (pressureLearnCount_ == 0U) return;

    const float learned = pressureLearnSum_ / (float)pressureLearnCount_;
    resetLearning();
    // Un capteur muet moyenne a zero : l'apprendre figerait une reference qui
    // desactive l'alerte pour toujours.
    if (!(learned > kPressureSensorFaultBar)) {
        LOGW("Pressure reference not learned: mean %.3f bar looks like a dead sensor", (double)learned);
        return;
    }

    pressureRefBar_ = learned;
    if (cfgStore_) {
        (void)cfgStore_->set(pressureRefVar_, pressureRefBar_);
    }
    LOGI("Pressure reference learned: %.2f bar (clean filter)", (double)pressureRefBar_);
}

// Une pression quasi nulle pendant que le flowswitch annonce du debit ne peut
// pas etre une pompe desamorcee : c'est la mesure qui manque. Sans flowswitch
// declare, le cas reste ambigu et on ne tranche pas -- c'est precisement
// l'ambiguite qui faisait couper la filtration a tort avant la refonte.
AlarmCondState PoolLogicModule::condPressureSensorFaultStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;
    const uint32_t runSec = self->stateUptimeSec_(self->filtrationFsm_, nowMs);
    if (runSec <= self->pressureStartupDelaySec_) return AlarmCondState::False;
    if (!self->readFlowConfirmed_()) return AlarmCondState::False;

    float pressure = 0.0f;
    if (!self->loadAnalogSensor_(self->pressureIoId_, pressure)) {
        return AlarmCondState::Unknown;
    }

    return (pressure < kPressureSensorFaultBar) ? AlarmCondState::True : AlarmCondState::False;
}

// Encrassement du filtre : seule lecture utile d'un manometre de filtration, et
// elle est relative. Le seuil absolu qui existait avant ne voulait rien dire
// d'une installation a l'autre.
AlarmCondState PoolLogicModule::condFilterFoulingStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;
    const uint32_t runSec = self->stateUptimeSec_(self->filtrationFsm_, nowMs);
    if (runSec <= self->pressureStartupDelaySec_) return AlarmCondState::False;

    const float threshold = self->computeFoulingThreshold_();
    if (!(threshold > 0.0f)) return AlarmCondState::False;

    float pressure = 0.0f;
    if (!self->loadAnalogSensor_(self->pressureIoId_, pressure)) {
        return AlarmCondState::Unknown;
    }

    return (pressure > threshold) ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPressureHighStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;

    float pressure = 0.0f;
    if (!self->loadAnalogSensor_(self->pressureIoId_, pressure)) {
        return AlarmCondState::Unknown;
    }

    return (pressure > self->pressureHighThreshold_) ? AlarmCondState::True : AlarmCondState::False;
}

// Marche a sec : le flowswitch mesure ce que la pression basse ne faisait que
// deviner. Meme temporisation que la pression -- c'est le temps d'amorcage de la
// pompe, pas un reglage propre au capteur de debit.
AlarmCondState PoolLogicModule::condNoFlowStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->flowInterlockEnabled_) return AlarmCondState::False;
    if (!self->filtrationFsm_.on) return AlarmCondState::False;
    const uint32_t runSec = self->stateUptimeSec_(self->filtrationFsm_, nowMs);
    if (runSec <= self->pressureStartupDelaySec_) return AlarmCondState::False;

    bool flowOn = false;
    if (!self->loadDigitalSensor_(self->flowSwitchIoId_, flowOn)) {
        return AlarmCondState::Unknown;
    }

    return flowOn ? AlarmCondState::False : AlarmCondState::True;
}

AlarmCondState PoolLogicModule::condPhTankLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->phLevelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    return low ? AlarmCondState::True : AlarmCondState::False;
}

// Le comptage des lots sans effet est fait par la FSM de dosage ; la condition
// ne fait que refleter son latch.
AlarmCondState PoolLogicModule::condPhDoseNoEffectStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    if (!self->phAutoMode_) return AlarmCondState::False;
    if (self->phPumpDeviceSlot_ >= POOL_DEVICE_MAX) return AlarmCondState::False;
    if (self->phNoEffectBatches_ == 0U) return AlarmCondState::False;
    return (self->phDosingState_.noEffectCount >= self->phNoEffectBatches_) ? AlarmCondState::True
                                                                           : AlarmCondState::False;
}

// Le bidon de desinfectant n'existe que pour les modes de dosage liquide
// (chlore/brome, oxygene actif). Le mode etant fige au demarrage, l'alarme n'est
// simplement pas enregistree ailleurs : plus besoin d'un etat « inapplicable »
// renvoye a chaque evaluation.
AlarmCondState PoolLogicModule::condChlorineTankLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->chlorineLevelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    return low ? AlarmCondState::True : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condWaterLevelLowStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;

    bool low = false;
    if (!self->loadDigitalSensor_(self->levelIoId_, low)) {
        return AlarmCondState::Unknown;
    }
    // Harmonized digital level semantics: true means "problem detected".
    return low ? AlarmCondState::True : AlarmCondState::False;
}

// Alignee sur kHeaterTempFreshMaxMs : la condition devient vraie exactement
// quand la mesure cesse d'etre utilisable pour les deux consommateurs qui en
// dependent -- le chauffage la coupe et pose HeatAssistReason::TempUnavailable,
// FiltrationWindow bascule sur son plan de repli (capacite maximale des
// fenetres). Ces deux replis existaient deja mais n'etaient traces que dans les
// logs serie : l'alarme les rend visibles (Home Assistant, ecran, historique).
//
// Une lecture qui echoue, une valeur non finie ou un horodatage absent comptent
// comme indisponibles : le but est de signaler une sonde d'eau muette, que la
// cause soit une panne, un debranchement ou un endpoint mal configure.
AlarmCondState PoolLogicModule::condWaterTempUnavailableStatic_(void* ctx, uint32_t nowMs)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self || !self->enabled_) return AlarmCondState::False;
    // Service IO pas encore resolu au demarrage : ni vrai ni faux, on ne
    // declenche pas les temporisations de l'alarme.
    if (!self->ioSvc_ || !self->ioSvc_->readAnalog) return AlarmCondState::Unknown;

    float waterTemp = 0.0f;
    uint32_t tsMs = 0U;
    if (!self->loadAnalogSensor_(self->waterTempIoId_, waterTemp, &tsMs)) {
        return AlarmCondState::True;
    }
    if (!std::isfinite(waterTemp) || tsMs == 0U) return AlarmCondState::True;
    return ((uint32_t)(nowMs - tsMs) > kHeaterTempFreshMaxMs) ? AlarmCondState::True
                                                              : AlarmCondState::False;
}

AlarmCondState PoolLogicModule::condPhPumpMaxUptimeStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->condPumpMaxUptime_(self->phPumpDeviceSlot_) : AlarmCondState::Unknown;
}

// Meme raison que ci-dessus : hors dosage liquide la pompe de desinfection
// n'est pas definie, et son alarme d'uptime n'est pas enregistree.
AlarmCondState PoolLogicModule::condChlorinePumpMaxUptimeStatic_(void* ctx, uint32_t)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    if (!self) return AlarmCondState::Unknown;
    return self->condPumpMaxUptime_(self->orpPumpDeviceSlot_);
}

AlarmCondState PoolLogicModule::condPumpMaxUptime_(uint8_t deviceSlot) const
{
    // Aucun appareil associe : pas d'uptime a surveiller.
    if (deviceSlot >= POOL_DEVICE_MAX) return AlarmCondState::False;
    if (!poolSvc_ || !poolSvc_->meta) return AlarmCondState::Unknown;

    PoolDeviceSvcMeta meta{};
    const PoolDeviceSvcStatus st = poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta);
    if (st == POOLDEV_SVC_OK) {
        return (meta.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME) ? AlarmCondState::True : AlarmCondState::False;
    }
    if (st == POOLDEV_SVC_ERR_DISABLED) return AlarmCondState::False;
    return AlarmCondState::Unknown;
}

bool PoolLogicModule::readDeviceActualOn_(uint8_t deviceSlot, bool& onOut) const
{
    // Role sans appareil associe ("aucun PDM") : rien a lire, sans erreur.
    if (deviceSlot >= POOL_DEVICE_MAX) return false;
    if (!poolSvc_ || !poolSvc_->readActualOn) return false;
    uint8_t on = 0;
    if (poolSvc_->readActualOn(poolSvc_->ctx, deviceSlot, &on, nullptr) != POOLDEV_SVC_OK) return false;
    onOut = (on != 0U);
    return true;
}

// N'emet aucun log : la deduplication appartient a applyDeviceControl_, seul
// detenteur de l'etat de blocage par equipement.
bool PoolLogicModule::writeDeviceDesired_(uint8_t deviceSlot,
                                          bool on,
                                          PoolDeviceSvcStatus& statusOut,
                                          uint8_t& blockReasonOut)
{
    statusOut = POOLDEV_SVC_OK;
    blockReasonOut = POOL_DEVICE_BLOCK_NONE;

    // Role sans appareil associe : commande ignoree silencieusement.
    if (deviceSlot >= POOL_DEVICE_MAX) {
        statusOut = POOLDEV_SVC_ERR_UNKNOWN_SLOT;
        return false;
    }
    if (!poolSvc_ || !poolSvc_->writeDesired) {
        statusOut = POOLDEV_SVC_ERR_NOT_READY;
        return false;
    }

    statusOut = poolSvc_->writeDesired(poolSvc_->ctx, deviceSlot, on ? 1U : 0U);
    if (statusOut == POOLDEV_SVC_OK) return true;

    PoolDeviceSvcMeta meta{};
    if (poolSvc_->meta && (poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta) == POOLDEV_SVC_OK)) {
        blockReasonOut = meta.blockReason;
    }
    return false;
}

// Arret immediat hors boucle de controle (changement de mode, de type de
// desinfection...) : le statut detaille n'interesse pas l'appelant.
bool PoolLogicModule::forceDeviceStop_(uint8_t deviceSlot)
{
    PoolDeviceSvcStatus st = POOLDEV_SVC_OK;
    uint8_t block = POOL_DEVICE_BLOCK_NONE;
    return writeDeviceDesired_(deviceSlot, false, st, block);
}

bool PoolLogicModule::setPoolDeviceWritesEnabled_(bool enabled)
{
    if (!poolSvc_ || !poolSvc_->setWritesEnabled) return false;
    const PoolDeviceSvcStatus st = poolSvc_->setWritesEnabled(poolSvc_->ctx, enabled ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        LOGW("pooldev.setWritesEnabled failed enabled=%u st=%u(%s)",
             enabled ? 1u : 0u,
             (unsigned)st,
             poolDeviceSvcStatusStr_(st));
        return false;
    }
    return true;
}

// Device FSM synchronization is edge-oriented: it preserves the current state
// and only emits transitions when the observed hardware state changes.
void PoolLogicModule::syncDeviceState_(uint8_t deviceSlot, DeviceFsm& fsm, uint32_t nowMs, bool& turnedOnOut, bool& turnedOffOut)
{
    turnedOnOut = false;
    turnedOffOut = false;
    if (deviceSlot >= POOL_DEVICE_MAX) return;  // role sans appareil associe

    bool actualOn = false;
    if (!readDeviceActualOn_(deviceSlot, actualOn)) {
        return;
    }

    if (!fsm.known) {
        fsm.known = true;
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
        return;
    }

    if (fsm.on != actualOn) {
        turnedOnOut = (!fsm.on && actualOn);
        turnedOffOut = (fsm.on && !actualOn);
        fsm.on = actualOn;
        fsm.stateSinceMs = nowMs;
        PoolDeviceSvcMeta meta{};
        const char* label = nullptr;
        if (poolSvc_ && poolSvc_->meta &&
            poolSvc_->meta(poolSvc_->ctx, deviceSlot, &meta) == POOLDEV_SVC_OK &&
            meta.label[0] != '\0') {
            label = meta.label;
        }
        emitDeviceActivity_(false, actualOn, deviceSlot, label, ActivityReason::None);
    }
}

void PoolLogicModule::syncAllDeviceStates_(uint32_t nowMs)
{
    bool unusedStart = false;
    bool unusedStop = false;
    syncDeviceState_(filtrationDeviceSlot_, filtrationFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(robotDeviceSlot_, robotFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(swgDeviceSlot_, swgFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(fillingDeviceSlot_, fillingFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(phPumpDeviceSlot_, phPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(orpPumpDeviceSlot_, orpPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(heaterDeviceSlot_, heaterFsm_, nowMs, unusedStart, unusedStop);
}

void PoolLogicModule::adoptBootDeviceState_(uint32_t nowMs)
{
    syncAllDeviceStates_(nowMs);
    filtrationFsm_.lastDesired = filtrationFsm_.on;
    robotFsm_.lastDesired = robotFsm_.on;
    swgFsm_.lastDesired = swgFsm_.on;
    fillingFsm_.lastDesired = fillingFsm_.on;
    phPumpFsm_.lastDesired = phPumpFsm_.on;
    orpPumpFsm_.lastDesired = orpPumpFsm_.on;
    heaterFsm_.lastDesired = heaterFsm_.on;
}

uint32_t PoolLogicModule::stateUptimeSec_(const DeviceFsm& fsm, uint32_t nowMs) const
{
    if (!fsm.known || !fsm.on) return 0;
    return (uint32_t)((nowMs - fsm.stateSinceMs) / 1000UL);
}

bool PoolLogicModule::loadAnalogSensor_(IoId ioId, float& out, uint32_t* tsMsOut) const
{
    if (!ioSvc_ || !ioSvc_->readAnalog) return false;
    uint32_t tsMs = 0U;
    if (ioSvc_->readAnalog(ioSvc_->ctx, ioId, &out, &tsMs, nullptr) != IO_OK) return false;
    if (tsMsOut) *tsMsOut = tsMs;
    return true;
}

bool PoolLogicModule::loadDigitalSensor_(IoId ioId, bool& out) const
{
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    uint8_t on = 0;
    if (ioSvc_->readDigital(ioSvc_->ctx, ioId, &on, nullptr, nullptr) != IO_OK) return false;
    out = (on != 0U);
    return true;
}

void PoolLogicModule::applySensorHoldBindings_()
{
    auto retryLater = [this]() {
        portENTER_CRITICAL(&pendingMux_);
        sensorHoldPending_ = true;
        portEXIT_CRITICAL(&pendingMux_);
    };

    if (!ioSvc_ || !ioSvc_->setAnalogHold) {
        retryLater();
        return;
    }

    // Pousse a chaque rejeu (demarrage et ConfigChanged sur sensors/safety) :
    // le reglage doit prendre effet pompe en marche, sans attendre le prochain
    // front de circulation.
    if (ioSvc_->setAnalogHoldRefAge) {
        (void)ioSvc_->setAnalogHoldRefAge(ioSvc_->ctx,
                                          sensorHoldEnabled_ ? sensorHoldRefAgeSec_ : 0U);
    }

    IoId wanted[kSensorHoldMax] = {IO_ID_INVALID, IO_ID_INVALID, IO_ID_INVALID};
    if (sensorHoldEnabled_) {
        // pH et ORP sont toujours en ligne : leur sonde est dans le
        // porte-sondes, jamais dans le bassin. La pression reste libre -- a
        // l'arret, 0 bar est une information vraie.
        wanted[0] = phIoId_;
        wanted[1] = orpIoId_;
        if (sensorHoldWaterTemp_) wanted[2] = waterTempIoId_;
    }

    for (uint8_t i = 0; i < kSensorHoldMax; ++i) {
        const IoId previous = sensorHoldIds_[i];
        if (previous == wanted[i]) continue;
        if (previous != IO_ID_INVALID) {
            (void)ioSvc_->setAnalogHold(ioSvc_->ctx, previous, 0U);
        }
        if (wanted[i] != IO_ID_INVALID &&
            ioSvc_->setAnalogHold(ioSvc_->ctx, wanted[i], 1U) != IO_OK) {
            // Registre IO pas encore construit : reessayer au tick suivant.
            retryLater();
            continue;
        }
        sensorHoldIds_[i] = wanted[i];
    }
}

void PoolLogicModule::updateSensorHold_(bool haveFlow, bool flowOn)
{
    bool pending = false;
    portENTER_CRITICAL(&pendingMux_);
    pending = sensorHoldPending_;
    sensorHoldPending_ = false;
    portEXIT_CRITICAL(&pendingMux_);
    if (pending) applySensorHoldBindings_();

    if (!ioSvc_ || !ioSvc_->setCirculating) return;

    // Flowswitch declare installe : il decide seul. C'est une mesure physique du
    // debit, la l'etat du relais n'est qu'une intention -- une pompe forcee a la
    // main, hors du firmware, donne quand meme des mesures valides.
    //
    // Non declare : on retombe sur la mise en route de la pompe. Une entree TOR
    // libre est en pull-up et lit « pas de debit » en permanence ; la suivre
    // sans declaration gelait les mesures pour toujours, filtration comprise.
    // Activer l'interlock vaut declaration : on ne l'active pas sans capteur.
    const bool flowDeclared = (flowPresent_ || flowInterlockEnabled_) && haveFlow;
    const bool circulating = flowDeclared ? flowOn : filtrationFsm_.on;
    if (circulatingKnown_ && circulating == circulatingLast_) return;
    circulatingKnown_ = true;
    circulatingLast_ = circulating;

    // Filtration en marche mais toujours gele : c'est le flowswitch qui parle.
    // Le tracer evite d'avoir a deduire la cause depuis l'interface.
    if (!circulating && filtrationFsm_.on) {
        LOGW("Sensor hold kept while filtration runs: flowswitch reports no flow");
    }
    (void)ioSvc_->setCirculating(ioSvc_->ctx,
                                 circulating ? 1U : 0U,
                                 sensorHoldSettleSec_);
}

bool PoolLogicModule::sensorHoldActive_() const
{
    // Etat constate, pas intention : c'est IOModule qui tient la temporisation
    // de reprise, la relire evite d'en entretenir une deuxieme ici.
    return dataStore_ && ioAnyEndpointHeld(*dataStore_);
}

void PoolLogicModule::resetTemporalPidState_(TemporalPidState& st, uint32_t nowMs)
{
    st.initialized = false;
    st.sampleValid = false;
    st.lastDemandOn = false;
    st.windowLatched = false;
    st.windowStartMs = nowMs;
    st.lastComputeMs = nowMs;
    st.sampleTsMs = 0;
    st.outputOnMs = 0;
    st.pendingOnMs = 0;
    st.sampleInput = 0.0f;
    st.sampleSetpoint = 0.0f;
    st.sampleError = 0.0f;
    st.integral = 0.0f;
    st.prevError = 0.0f;
    st.lastError = 0.0f;
    st.runtimeTsMs = nowMs;
}

void PoolLogicModule::stepTemporalPid_(TemporalPidState& st,
                                       float input,
                                       float setpoint,
                                       float kp,
                                       float ki,
                                       float kd,
                                       int32_t windowMsCfg,
                                       int32_t minOnMsCfg,
                                       int32_t sampleMsCfg,
                                       bool positiveWhenInputHigh,
                                       uint32_t nowMs,
                                       bool& demandOnOut,
                                       uint32_t& outputOnMsOut)
{
    // The PID output is converted into a time-on window so peristaltic pumps
    // can be driven with coarse duty-cycle control instead of a raw analog value.
    // Fenetre, duree ON minimale et echantillonnage sont propres a chaque boucle.
    const uint32_t windowMs = (windowMsCfg > 1000) ? (uint32_t)windowMsCfg : 1000U;
    const uint32_t sampleMs = (sampleMsCfg > 100) ? (uint32_t)sampleMsCfg : 100U;
    const uint32_t minOnMs = (minOnMsCfg > 0) ? (uint32_t)minOnMsCfg : 0U;

    if (!st.initialized) {
        st.initialized = true;
        st.windowStartMs = nowMs;
        // Force un calcul des le premier tick : sans cela, la consigne ne
        // deviendrait effective qu'a la fin de la premiere fenetre (jusqu'a 1 h
        // de temps mort apres chaque demarrage de filtration).
        st.lastComputeMs = nowMs - sampleMs;
        st.sampleValid = false;
        st.sampleTsMs = 0;
        st.sampleInput = 0.0f;
        st.sampleSetpoint = 0.0f;
        st.sampleError = 0.0f;
        st.integral = 0.0f;
        st.prevError = 0.0f;
        st.lastError = 0.0f;
        st.outputOnMs = 0;
        st.pendingOnMs = 0;
        st.windowLatched = false;
        st.lastDemandOn = false;
        st.runtimeTsMs = nowMs;
    }

    if ((uint32_t)(nowMs - st.lastComputeMs) >= sampleMs) {
        const uint32_t dtMs = nowMs - st.lastComputeMs;
        const float dtSec = (dtMs > 0U) ? ((float)dtMs / 1000.0f) : 0.0f;
        st.lastComputeMs = nowMs;

        const float error = positiveWhenInputHigh ? (input - setpoint) : (setpoint - input);
        st.lastError = error;

        float outputMs = 0.0f;
        if (error > 0.0f && std::isfinite(error)) {
            if (ki != 0.0f && dtSec > 0.0f) {
                st.integral += error * dtSec;
            } else {
                st.integral = 0.0f;
            }
            const float deriv = (dtSec > 0.0f) ? ((error - st.prevError) / dtSec) : 0.0f;
            outputMs = (kp * error) + (ki * st.integral) + (kd * deriv);
            if (!std::isfinite(outputMs) || outputMs < 0.0f) outputMs = 0.0f;
            if (outputMs > (float)windowMs) outputMs = (float)windowMs;
        } else {
            st.integral = 0.0f;
            outputMs = 0.0f;
        }
        st.prevError = error;
        st.sampleValid = true;
        st.sampleInput = input;
        st.sampleSetpoint = setpoint;
        st.sampleError = error;
        st.sampleTsMs = nowMs;
        st.runtimeTsMs = nowMs;

        uint32_t outMs = (uint32_t)(outputMs + 0.5f);
        if (outMs < minOnMs) outMs = 0U;
        if (outMs > windowMs) outMs = windowMs;
        // La consigne calculee n'est pas appliquee immediatement : elle attend
        // le prochain debut de fenetre (cf. latch ci-dessous).
        st.pendingOnMs = outMs;
    }

    // Avance de fenetre par pas entiers : conserve la phase malgre les retards
    // d'ordonnancement.
    bool windowRolled = false;
    while ((uint32_t)(nowMs - st.windowStartMs) >= windowMs) {
        st.windowStartMs += windowMs;
        windowRolled = true;
    }

    // Seul point ou outputOnMs change : la duree ON reste figee pour toute la
    // fenetre en cours, sinon la pompe peut se rallumer en milieu de fenetre
    // apres s'etre arretee.
    if (windowRolled || !st.windowLatched) {
        st.outputOnMs = st.pendingOnMs;
        st.windowLatched = true;
        st.runtimeTsMs = nowMs;
    }

    const uint32_t elapsedMs = nowMs - st.windowStartMs;
    const bool demandOn = (st.outputOnMs > 0U) && (elapsedMs < st.outputOnMs);
    if (demandOn != st.lastDemandOn) {
        st.lastDemandOn = demandOn;
        st.runtimeTsMs = nowMs;
    }

    demandOnOut = demandOn;
    outputOnMsOut = st.outputOnMs;
}

PoolLogicModule::DeviceWriteResult PoolLogicModule::applyDeviceControl_(uint8_t deviceSlot,
                                                                       const char* label,
                                                                       DeviceFsm& fsm,
                                                                       bool desired,
                                                                       uint32_t nowMs)
{
    if (deviceSlot >= POOL_DEVICE_MAX) return DeviceWriteResult::NoDevice;  // role sans appareil

    const bool desiredChanged = (desired != fsm.lastDesired);
    const bool retryDue = ((uint32_t)(nowMs - fsm.lastCmdMs) >= kDeviceRetryPeriodMs);
    // When the actual state does not follow the requested state, retry at a
    // bounded cadence instead of spamming the downstream pool-device service.
    const bool needRetry = (fsm.known && (fsm.on != desired) && retryDue);
    // Sur refus, la consigne n'est pas latchee : desiredChanged resterait vrai a
    // chaque tour. On borne donc la re-tentative a la meme cadence que le retry.
    const bool attempt = (desiredChanged && (!fsm.writeRejected || retryDue)) || needRetry;

    if (!attempt) {
        fsm.lastDesired = desired;
        return DeviceWriteResult::Unchanged;
    }

    PoolDeviceSvcStatus st = POOLDEV_SVC_OK;
    uint8_t block = POOL_DEVICE_BLOCK_NONE;
    const bool ok = writeDeviceDesired_(deviceSlot, desired, st, block);
    fsm.lastCmdMs = nowMs;
    fsm.lastBlockReason = block;

    if (ok) {
        LOGI("%s %s", desired ? "Start" : "Stop", label ? label : "Pool Device");
        if (desiredChanged) {
            emitDeviceActivity_(true, desired, deviceSlot, label, ActivityReason::Auto);
        }
        if (fsm.loggedBlockReason != kNoLoggedBlockReason) {
            LOGI("pooldev.writeDesired recovered slot=%u prev_block=%u(%s)",
                 (unsigned)deviceSlot,
                 (unsigned)fsm.loggedBlockReason,
                 poolDeviceBlockReasonStr_(fsm.loggedBlockReason));
            fsm.loggedBlockReason = kNoLoggedBlockReason;
        }
        fsm.writeRejected = false;
        fsm.lastDesired = desired;  // latch uniquement sur succes
        return DeviceWriteResult::Written;
    }

    // Echec : la consigne reste "non honoree" pour la logique metier, et le log
    // n'est reemis que si la raison change ou apres kBlockLogRepeatMs.
    fsm.writeRejected = true;
    const bool reasonChanged = (fsm.loggedBlockReason != block);
    const bool repeatDue = ((uint32_t)(nowMs - fsm.blockLoggedMs) >= kBlockLogRepeatMs);
    if (reasonChanged || repeatDue) {
        LOGW("pooldev.writeDesired blocked slot=%u desired=%u st=%u(%s) block=%u(%s)",
             (unsigned)deviceSlot,
             desired ? 1u : 0u,
             (unsigned)st,
             poolDeviceSvcStatusStr_(st),
             (unsigned)block,
             poolDeviceBlockReasonStr_(block));
        fsm.loggedBlockReason = block;
        fsm.blockLoggedMs = nowMs;
    }
    return DeviceWriteResult::Rejected;
}

void PoolLogicModule::runControlLoop_(uint32_t nowMs)
{
    // The loop always starts by refreshing observed actuator states so all
    // subsequent decisions are based on the latest physical feedback.
    bool filtrationStarted = false;
    bool filtrationStopped = false;
    bool robotStopped = false;
    bool unusedStart = false;
    bool unusedStop = false;

    syncDeviceState_(filtrationDeviceSlot_, filtrationFsm_, nowMs, filtrationStarted, filtrationStopped);
    syncDeviceState_(robotDeviceSlot_, robotFsm_, nowMs, unusedStart, robotStopped);
    syncDeviceState_(swgDeviceSlot_, swgFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(fillingDeviceSlot_, fillingFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(phPumpDeviceSlot_, phPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(orpPumpDeviceSlot_, orpPumpFsm_, nowMs, unusedStart, unusedStop);
    syncDeviceState_(heaterDeviceSlot_, heaterFsm_, nowMs, unusedStart, unusedStop);

    if (filtrationStarted) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(orpPidState_, nowMs);
    }
    if (filtrationStopped) {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
        resetTemporalPidState_(orpPidState_, nowMs);
        // Filtration arretee : un melange en cours n'a plus de sens, et un lot
        // entame ne peut pas reprendre sur une eau non brassee.
        resetPhDosingState_(nowMs);
        portENTER_CRITICAL(&pendingMux_);
        robotManualOverride_ = false;
        robotManualDesired_ = false;
        portEXIT_CRITICAL(&pendingMux_);
    }
    if (robotStopped) {
        cleaningDone_ = true;
        portENTER_CRITICAL(&pendingMux_);
        if (robotManualOverride_ && !robotManualDesired_) {
            robotManualOverride_ = false;
        }
        portEXIT_CRITICAL(&pendingMux_);
    }

    float pressure = 0.0f;
    float ph = 0.0f;
    float waterTemp = 0.0f;
    float airTemp = 0.0f;
    float orp = 0.0f;
    bool poolLevelOn = false;
    bool phTankLow = false;
    bool chlorineTankLow = false;

    const bool havePressure = loadAnalogSensor_(pressureIoId_, pressure);
    // La mesure pH est datee : la FSM de dosage refuse de decider sur un
    // echantillon perime (ph_sample_max_age).
    uint32_t phTsMs = 0U;
    const bool havePh = loadAnalogSensor_(phIoId_, ph, &phTsMs);
    const uint32_t phAgeMs =
        (havePh && phTsMs != 0U) ? (uint32_t)(nowMs - phTsMs) : 0xFFFFFFFFU;
    uint32_t waterTempTsMs = 0U;
    const bool haveWaterTemp = loadAnalogSensor_(waterTempIoId_, waterTemp, &waterTempTsMs);
    const bool waterTempFresh =
        haveWaterTemp &&
        (waterTempTsMs != 0U) &&
        ((uint32_t)(nowMs - waterTempTsMs) <= kHeaterTempFreshMaxMs);
    const bool haveAirTemp = loadAnalogSensor_(airTempIoId_, airTemp);
    const bool haveOrp = loadAnalogSensor_(orpIoId_, orp);
    const bool haveLevel = loadDigitalSensor_(levelIoId_, poolLevelOn);
    const bool havePhTankLow = loadDigitalSensor_(phLevelIoId_, phTankLow);
    const bool haveChlorineTankLow = loadDigitalSensor_(chlorineLevelIoId_, chlorineTankLow);

    // Prefer centralized alarm state when available; otherwise fall back to a
    // local safety latch so standalone behavior remains conservative.
    //
    // Deux coupures hydrauliques seulement : la surpression mecanique et le
    // manque de debit. La pression basse n'en fait plus partie -- elle signalait
    // une pompe desamorcee faute de capteur de debit, role que le flowswitch
    // tient directement et sans ambiguite.
    const bool phTankLowBefore = phTankLowError_;
    const bool pressureTripBefore = pressureTrip_;
    if (alarmSvc_ && alarmSvc_->isActive) {
        pressureTrip_ = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPressureHigh);
        noFlowTrip_ = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolNoFlow);
        phTankLowError_ = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolPhTankLow);
        chlorineTankLowError_ = alarmSvc_->isActive(alarmSvc_->ctx, AlarmId::PoolChlorineTankLow);
    } else {
        // Mode degrade : seule la surpression est refaite en local. L'encrassement
        // et le diagnostic capteur sont informatifs -- les dupliquer ici n'aurait
        // protege personne.
        phTankLowError_ = havePhTankLow && phTankLow;
        chlorineTankLowError_ = haveChlorineTankLow && chlorineTankLow;
        noFlowTrip_ = false;
        if (filtrationFsm_.on && havePressure && (pressure > pressureHighThreshold_) && !pressureTrip_) {
            pressureTrip_ = true;
            LOGW("Pressure trip latched locally (pressure=%.3f high=%.3f)",
                 (double)pressure,
                 (double)pressureHighThreshold_);
            char detail[128] = {0};
            snprintf(detail,
                     sizeof(detail),
                     "Pression %.3f bar au-dessus de la limite %.3f bar.",
                     (double)pressure,
                     (double)pressureHighThreshold_);
            emitActivity_(ActivityCode::PoolLogicSafetyPressureLatched,
                          ActivitySource::Safety,
                          ActivitySeverity::Warning,
                          ActivityRole::Filtration,
                          ActivityState::None,
                          ActivityReason::Safety,
                          filtrationDeviceSlot_,
                          "Sécurité pression piscine activée",
                          detail,
                          "warning");
        }
    }

    // Re-trips consecutifs : une vanne restee fermee refait tripper la pompe a
    // chaque rearmement. Au-dela du seuil on cesse de rejouer le cycle et on le
    // dit -- c'est le garde-fou qui manquait a l'ancienne alarme de pression.
    if (pressureTrip_ && !pressureTripBefore) {
        if (pressureRetripCount_ < 0xFFU) ++pressureRetripCount_;
        if (!pressureTripLocked_ && pressureRetripCount_ >= kPressureRetripLock) {
            pressureTripLocked_ = true;
            LOGW("Pressure trip locked after %u consecutive re-trips: check valves and filter",
                 (unsigned)pressureRetripCount_);
        }
    }
    // Une marche saine efface le compteur : le verrou ne doit pas survivre a un
    // probleme reellement resolu.
    if (!pressureTrip_ && filtrationFsm_.on &&
        (stateUptimeSec_(filtrationFsm_, nowMs) > pressureStartupDelaySec_) &&
        (pressureRetripCount_ != 0U || pressureTripLocked_)) {
        pressureRetripCount_ = 0U;
        pressureTripLocked_ = false;
    }

    hydraulicTrip_ = pressureTrip_ || noFlowTrip_;

    updatePressureReference_(havePressure, pressure, nowMs);
    publishPressureRuntime_();

    // Bidon pH reapprovisionne : c'est la cause n°1 d'un dosage sans effet et le
    // geste naturel de l'utilisateur. Le front descendant leve donc le latch,
    // sans exiger de commande dediee.
    if (phTankLowBefore && !phTankLowError_ && phDosingState_.noEffectCount != 0U) {
        if (phDosingState_.blockReason == DOSING_BLOCK_NO_EFFECT) {
            resetPhDosingState_(nowMs);
        } else {
            phDosingState_.noEffectCount = 0;
            phDosingTsMs_ = nowMs;
        }
        LOGI("pH dosing no-effect latch cleared (tank refilled)");
    }

    // PID regulation is armed only after filtration has been stable long enough
    // to avoid reacting to startup transients.
    if (filtrationFsm_.on && !winterMode_) {
        const uint32_t runMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;

        if (phAutoMode_ && !phPidEnabled_ && runMin >= delayPidsMin_) {
            phPidEnabled_ = true;
            LOGI("Activate pH regulation (delay=%umin)", (unsigned)runMin);
            emitActivity_(ActivityCode::PoolLogicPhRegulationEnabled,
                          ActivitySource::Pid,
                          ActivitySeverity::Info,
                          ActivityRole::Ph,
                          ActivityState::None,
                          ActivityReason::Pid,
                          phPumpDeviceSlot_,
                          "Régulation pH activée",
                          "La filtration est stable, le PID pH peut piloter la pompe.",
                          "science");
        }
        if (orpAutoMode_ && isDisinfectionType_(DisinfectionChlorineBromine) &&
            !orpPidEnabled_ && runMin >= delayPidsMin_) {
            orpPidEnabled_ = true;
            LOGI("Activate ORP regulation (delay=%umin)", (unsigned)runMin);
            emitActivity_(ActivityCode::PoolLogicOrpRegulationEnabled,
                          ActivitySource::Pid,
                          ActivitySeverity::Info,
                          ActivityRole::Disinfection,
                          ActivityState::None,
                          ActivityReason::Pid,
                          orpPumpDeviceSlot_,
                          "Régulation ORP activée",
                          "La filtration est stable, le PID ORP peut piloter la désinfection.",
                          "science");
        }
    } else {
        phPidEnabled_ = false;
        orpPidEnabled_ = false;
    }
    // Manual forcing relies on *_auto_mode=false. In that case, PID must not
    // keep a stale enabled state that could override manual requests.
    if (!phAutoMode_ && phPidEnabled_) {
        phPidEnabled_ = false;
    }
    if (!orpAutoMode_ && orpPidEnabled_) {
        orpPidEnabled_ = false;
    }

    bool windowActive = false;
    bool forceFiltrationReconcile = false;
    FiltrationPlanOutput planCopy{};
    portENTER_CRITICAL(&pendingMux_);
    windowActive = filtrationWindowActive_;
    forceFiltrationReconcile = pendingFiltrationReconcile_;
    pendingFiltrationReconcile_ = false;
    planCopy = filtrationPlan_;
    portEXIT_CRITICAL(&pendingMux_);

    bool clockWindowActive = false;
    if (planCopy.segmentCount > 0 && currentFiltrationPlanActive_(planCopy, clockWindowActive)) {
        if (clockWindowActive != windowActive || forceFiltrationReconcile) {
            windowActive = clockWindowActive;
            portENTER_CRITICAL(&pendingMux_);
            filtrationWindowActive_ = windowActive;
            portEXIT_CRITICAL(&pendingMux_);
        }
    } else if (forceFiltrationReconcile && schedSvc_ && schedSvc_->isActive) {
        windowActive = false;
        for (uint8_t i = 0; i < planCopy.segmentCount; ++i) {
            if (schedSvc_->isActive(schedSvc_->ctx, (uint8_t)(SLOT_FILTR_WINDOW_BASE + i))) {
                windowActive = true;
                break;
            }
        }
        portENTER_CRITICAL(&pendingMux_);
        filtrationWindowActive_ = windowActive;
        portEXIT_CRITICAL(&pendingMux_);
    }

    auto hasHeatAssistFlag = [&](uint8_t flag) -> bool {
        return (heatAssistFlags_ & flag) != 0U;
    };

    auto setHeatAssistFlag = [&](uint8_t flag, bool enabled) {
        if (enabled) heatAssistFlags_ = (uint8_t)(heatAssistFlags_ | flag);
        else heatAssistFlags_ = (uint8_t)(heatAssistFlags_ & (uint8_t)(~flag));
    };

    auto setHeatAssistReason = [&](HeatAssistReason reason) {
        heatAssistReason_ = reason;
    };

    auto resetHeatAssistSession = [&]() {
        setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
        setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0xFFFF0000UL);
    };

    const uint16_t nowSec = (uint16_t)((nowMs / 1000UL) & 0xFFFFU);
    auto getProbeStartSec = [&]() -> uint16_t {
        return (uint16_t)(heatAssistTimingPacked_ & 0xFFFFU);
    };
    auto getLastProbeEndSec = [&]() -> uint16_t {
        return (uint16_t)((heatAssistTimingPacked_ >> 16) & 0xFFFFU);
    };
    auto setProbeStartSec = [&](uint16_t sec) {
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0xFFFF0000UL) | (uint32_t)sec;
    };
    auto setLastProbeEndSec = [&](uint16_t sec) {
        heatAssistTimingPacked_ = (heatAssistTimingPacked_ & 0x0000FFFFUL) | (((uint32_t)sec) << 16);
    };
    auto elapsedSecSince = [&](uint16_t pastSec) -> uint16_t {
        return (uint16_t)(nowSec - pastSec);
    };

    // Filtration arbitration intentionally applies safety, then manual mode,
    // then automatic scheduling/winter logic in that order.
    bool filtrationDesiredBase = filtrationFsm_.on;
    if (hydraulicTrip_) {
        // Safety first: surpression mecanique et manque de debit arretent la
        // pompe, y compris en mode manuel. Ce sont les deux seules causes -- une
        // pression basse ne coupe plus rien.
        filtrationDesiredBase = false;
    } else if (!autoMode_) {
        // Legacy-like manual mode: when auto_mode is off, keep filtration fully manual.
        filtrationDesiredBase = filtrationFsm_.on;
    } else {
        const bool timeSynced = timeSvc_ && timeSvc_->isSynced && timeSvc_->isSynced(timeSvc_->ctx);
        if (filtrationFsm_.on && !timeSynced) {
            // During a warm reboot, keep a retained pump running until the
            // scheduler can make a reliable clock-based decision.
            filtrationDesiredBase = true;
        } else if (filtrationFsm_.on && haveAirTemp && airTemp <= freezeHoldTempC_) {
            // Freeze hold: once running, never stop under freeze-hold threshold.
            filtrationDesiredBase = true;
        } else {
            const bool scheduleDemand = windowActive;
            const bool winterDemand = winterMode_ && haveAirTemp && (airTemp < winterStartTempC_);
            filtrationDesiredBase = (scheduleDemand || winterDemand);
        }
    }
    bool filtrationDesired = filtrationDesiredBase;

    // Robot and SWG remain derived outputs in auto mode. A manual robot request
    // is still arbitrated here so PoolLogic, not the HMI or PoolDeviceModule,
    // owns interlocks and duration limits.
    bool robotDesired = robotFsm_.on;
    bool robotManualOverride = false;
    bool robotManualDesired = false;
    portENTER_CRITICAL(&pendingMux_);
    robotManualOverride = robotManualOverride_;
    robotManualDesired = robotManualDesired_;
    portEXIT_CRITICAL(&pendingMux_);
    if (autoMode_) {
        robotDesired = false;
        if (filtrationFsm_.on && !cleaningDone_) {
            const uint32_t filtrationRunMin = stateUptimeSec_(filtrationFsm_, nowMs) / 60U;
            if (filtrationRunMin >= robotDelayMin_) robotDesired = true;
        }
        if (robotFsm_.on) {
            const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
            if (robotRunMin >= robotDurationMin_) robotDesired = false;
        }
        if (!filtrationFsm_.on) robotDesired = false;
    }
    if (robotManualOverride) {
        if (!filtrationFsm_.on) {
            robotDesired = false;
            portENTER_CRITICAL(&pendingMux_);
            robotManualOverride_ = false;
            robotManualDesired_ = false;
            portEXIT_CRITICAL(&pendingMux_);
        } else {
            robotDesired = robotManualDesired;
            if (robotManualDesired && robotFsm_.on) {
                const uint32_t robotRunMin = stateUptimeSec_(robotFsm_, nowMs) / 60U;
                if (robotRunMin >= robotDurationMin_) {
                    robotDesired = false;
                    cleaningDone_ = true;
                    portENTER_CRITICAL(&pendingMux_);
                    robotManualOverride_ = false;
                    robotManualDesired_ = false;
                    portEXIT_CRITICAL(&pendingMux_);
                }
            }
            if (!robotManualDesired && !robotFsm_.on) {
                portENTER_CRITICAL(&pendingMux_);
                robotManualOverride_ = false;
                portEXIT_CRITICAL(&pendingMux_);
            }
        }
    }

    bool swgDesired = swgFsm_.on;
    if (autoMode_) {
        swgDesired = false;
        if (isDisinfectionType_(DisinfectionSwg) && filtrationFsm_.on) {
            if (swgControlMode_ == SwgControlOrp) {
                if (swgFsm_.on) {
                    swgDesired = haveOrp && (orp <= orpSetpoint_);
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady && haveOrp && (orp <= (orpSetpoint_ * 0.9f));
                }
            } else {
                if (swgFsm_.on) {
                    swgDesired = true;
                } else {
                    const bool startReady =
                        haveWaterTemp &&
                        (waterTemp >= secureElectroTempC_) &&
                        ((stateUptimeSec_(filtrationFsm_, nowMs) / 60U) >= delayElectroMin_);
                    swgDesired = startReady;
                }
            }
        }
    }

    bool heaterDesired = heaterFsm_.on;
    if (!heaterAutoMode_) {
        resetHeatAssistSession();
        setHeatAssistFlag(kHeatAssistFlagFastCycle, false);
        setLastProbeEndSec(0U);
        setHeatAssistReason(HeatAssistReason::Disabled);
    } else if (!autoMode_) {
        resetHeatAssistSession();
        setHeatAssistReason(HeatAssistReason::ManualMode);
    } else if (hydraulicTrip_) {
        // Un rechauffeur alimente sans debit est le cas le plus dangereux du
        // lot : le manque de debit le coupe desormais aussi, alors qu'il n'etait
        // couvert que par ricochet de la pression.
        resetHeatAssistSession();
        heaterDesired = false;
        setHeatAssistReason(HeatAssistReason::HydraulicBlocked);
    } else if (!std::isfinite(heaterSetpoint_)) {
        resetHeatAssistSession();
        heaterDesired = false;
        setHeatAssistReason(HeatAssistReason::SetpointInvalid);
    } else {
        const float heaterStartThreshold = heaterSetpoint_ - kHeaterHysteresisC;
        const float heaterStopThreshold = heaterSetpoint_ + kHeaterHysteresisC;
        const bool fastCycle = hasHeatAssistFlag(kHeatAssistFlagFastCycle);
        const uint16_t idleSec = fastCycle ? kHeatAssistIdleFastSec : kHeatAssistIdleSlowSec;

        auto startProbe = [&]() {
            setHeatAssistFlag(kHeatAssistFlagProbeRunning, true);
            setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
            setProbeStartSec(nowSec);
            filtrationDesired = true;
            heaterDesired = false;
            setHeatAssistReason(HeatAssistReason::ProbeRunning);
        };

        if (hasHeatAssistFlag(kHeatAssistFlagHeatingActive)) {
            filtrationDesired = true;
            if (!waterTempFresh) {
                heaterDesired = false;
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else if (waterTemp >= heaterStopThreshold) {
                setHeatAssistFlag(kHeatAssistFlagHeatingActive, false);
                setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
                setProbeStartSec(0U);
                setLastProbeEndSec(nowSec);
                setHeatAssistFlag(kHeatAssistFlagFastCycle, true);
                heaterDesired = false;
                filtrationDesired = false;
                setHeatAssistReason(HeatAssistReason::SetpointReached);
            } else {
                heaterDesired = true;
                setHeatAssistReason(HeatAssistReason::Heating);
            }
        } else if (hasHeatAssistFlag(kHeatAssistFlagProbeRunning)) {
            filtrationDesired = true;
            heaterDesired = false;
            const uint16_t probeElapsedSec = elapsedSecSince(getProbeStartSec());
            if (probeElapsedSec >= kHeatAssistProbeRunSec) {
                setHeatAssistFlag(kHeatAssistFlagProbeRunning, false);
                setProbeStartSec(0U);
                if (!waterTempFresh) {
                    filtrationDesired = filtrationDesiredBase;
                    setLastProbeEndSec(nowSec);
                    setHeatAssistReason(HeatAssistReason::TempUnavailable);
                } else if (waterTemp <= heaterStartThreshold) {
                    setHeatAssistFlag(kHeatAssistFlagHeatingActive, true);
                    filtrationDesired = true;
                    heaterDesired = true;
                    setHeatAssistReason(HeatAssistReason::Heating);
                } else {
                    setLastProbeEndSec(nowSec);
                    filtrationDesired = filtrationDesiredBase;
                    setHeatAssistReason(fastCycle ? HeatAssistReason::ProbeWait20m
                                                  : HeatAssistReason::ProbeWait30m);
                }
            } else {
                setHeatAssistReason(HeatAssistReason::ProbeRunning);
            }
        } else if (filtrationFsm_.on || filtrationDesiredBase) {
            filtrationDesired = filtrationDesiredBase;
            if (!waterTempFresh) {
                heaterDesired = false;
                setHeatAssistReason(HeatAssistReason::TempUnavailable);
            } else {
                if (heaterFsm_.on) {
                    heaterDesired = (waterTemp < heaterStopThreshold);
                } else {
                    heaterDesired = (waterTemp <= heaterStartThreshold);
                }
                setHeatAssistReason(heaterDesired ? HeatAssistReason::Heating
                                                  : HeatAssistReason::IdlePumpOn);
            }
        } else {
            heaterDesired = false;
            filtrationDesired = false;
            const uint16_t lastProbeEndSec = getLastProbeEndSec();
            const bool dueForProbe =
                (lastProbeEndSec == 0U) || (elapsedSecSince(lastProbeEndSec) >= idleSec);
            if (dueForProbe) {
                startProbe();
            } else {
                setHeatAssistReason(fastCycle ? HeatAssistReason::ProbeWait20m
                                              : HeatAssistReason::ProbeWait30m);
            }
        }
    }

    // --- Flowswitch : recopie temporisee, etat volet, et interlock securite ---
    bool flowOn = false;
    const bool haveFlow = loadDigitalSensor_(flowSwitchIoId_, flowOn);
    if (flowOn && !flowSwitchLast_) {
        // Front montant du debit : demarre le compte a rebours d'activation.
        flowSwitchOnSinceMs_ = nowMs;
    }
    flowSwitchLast_ = flowOn;
    // Recopie a l'activation temporisee (delai configurable), retombee immediate.
    bool flowCopyOut = false;
    if (flowOn) {
        const uint32_t elapsedMs = (uint32_t)(nowMs - flowSwitchOnSinceMs_);
        flowCopyOut = elapsedMs >= ((uint32_t)flowCopyDelaySec_ * 1000UL);
    }
    flowCopyOutState_ = flowCopyOut;

    bool coverClosed = false;
    const bool haveCover = loadDigitalSensor_(coverClosedIoId_, coverClosed);
    coverClosedState_ = haveCover && coverClosed;

    // Ecriture des 2 sorties de report via leur PoolDevice, et non plus en
    // direct sur l'IO : la fonction gagne ainsi son switch d'activation, son
    // relais configurable et son comptage d'heures, comme les autres. La
    // temporisation reste calculee ici (flowCopyDelaySec_), donc onDelaySec de
    // ces deux appareils doit rester a 0 pour ne pas temporiser deux fois.
    // Ecriture no-op si la fonction n'est liee a aucun relais.
    if (poolSvc_ && poolSvc_->writeDesired) {
        (void)poolSvc_->writeDesired(poolSvc_->ctx, PoolIds::DeviceFlowCopy, flowCopyOutState_ ? 1U : 0U);
        (void)poolSvc_->writeDesired(poolSvc_->ctx, PoolIds::DeviceCoverReport, coverClosedState_ ? 1U : 0U);
    }

    // Interlock securite : plus de debit => coupe dosage pH/chlore et electrolyse.
    // N'agit que si un flowswitch est present (haveFlow) et l'interlock active.
    const bool noFlow = haveFlow && !flowOn;
    const bool interlockActive = flowInterlockEnabled_ && noFlow;
    if (interlockActive && !noFlowError_) {
        LOGW("Flow interlock: no flow detected, blocking pH/chlorine/electrolysis");
    }
    noFlowError_ = interlockActive;

    // Gel des mesures en ligne : meme lecture du debit que l'interlock, mais
    // sans condition d'activation -- le gel ne coupe rien, il empeche seulement
    // de publier la derive du porte-sondes.
    updateSensorHold_(haveFlow, flowOn);

    // Chemical dosing is computed last because it depends on the resolved
    // filtration state, alarm state, and sensor freshness.
    bool phPumpDesired = phPumpFsm_.on;
    bool orpPumpDesired = orpPumpFsm_.on;
    // Le pH est regule par dosage volumetrique par lots : la FSM tourne des que
    // le mode auto est actif, y compris filtration a l'arret, ou elle se met
    // d'elle-meme au repos (regulationArmed) sans perdre un melange en cours.
    if (phAutoMode_) {
        stepPhDosing_(havePh, ph, phAgeMs, nowMs, phPumpDesired);
    } else {
        // Mode auto coupe : la FSM se tait, mais le volume injecte du jour, le gain
        // effectif et la cause d'inaction restent des faits exacts. Les publier ici
        // aussi evite de les figer sur la photo prise au boot -- sinon un dosage
        // manuel, ou la remise a zero de minuit, ne remonterait jamais a l'ecran.
        // Le cout est nul quand rien ne bouge : setPoolPhDosingRuntime compare avant
        // d'ecrire, et refreshPhPumpMetrics_ ne relit le slot qu'une fois par seconde.
        refreshPhPumpMetrics_(nowMs);
        if (phDosingState_.phase != DOSING_PHASE_IDLE) {
            resetPhDosingState_(nowMs);
        } else {
            publishPhDosingRuntime_();
        }
    }

    // La desinfection liquide conserve le PID temporel par fenetre.
    const bool orpAllowed = orpAutoMode_ && filtrationDesired && orpPidEnabled_ && haveOrp &&
                            isDisinfectionType_(DisinfectionChlorineBromine) && !hydraulicTrip_ &&
                            !chlorineTankLowError_;
    if (orpAllowed) {
        uint32_t outMs = 0;
        stepTemporalPid_(orpPidState_,
                         orp,
                         orpSetpoint_,
                         orpKp_,
                         orpKi_,
                         orpKd_,
                         orpWindowMs_,
                         disMinOnMs_,
                         disSampleMs_,
                         false,
                         nowMs,
                         orpPumpDesired,
                         outMs);
    } else if (orpPidState_.initialized || orpPidState_.outputOnMs != 0U || orpPidState_.lastDemandOn) {
        resetTemporalPidState_(orpPidState_, nowMs);
    }

    // Le protocole oxygene actif n'est evalue que dans son mode : hors O2, aucun
    // appel (donc aucun set() NVS ni recalcul inutile). L'etat O2 est remis au
    // repos une seule fois au changement de mode (onEvent_/DisinfectionType).
    if (isDisinfectionType_(DisinfectionActiveOxygen)) {
        bool o2RequestFiltration = false;
        bool o2PumpDesired = false;
        (void)stepO2Protocol_(filtrationDesired,
                              filtrationFsm_.on,
                              stateUptimeSec_(filtrationFsm_, nowMs) / 60U,
                              haveWaterTemp,
                              waterTemp,
                              hydraulicTrip_,
                              chlorineTankLowError_,
                              nowMs,
                              o2RequestFiltration,
                              o2PumpDesired);
        if (o2RequestFiltration && !hydraulicTrip_) {
            filtrationDesired = true;
        }
        orpPumpDesired = o2PumpDesired;
    }

    bool fillingDesired = false;
    if (haveLevel) {
        if (!fillingFsm_.on) {
            fillingDesired = poolLevelOn;
        } else {
            const bool minUpReached = stateUptimeSec_(fillingFsm_, nowMs) >= fillingMinOnSec_;
            fillingDesired = poolLevelOn || !minUpReached;
        }
    }

    if (forceFiltrationReconcile) {
        // Auto mode changes arrive through ConfigChanged, so force one immediate
        // reconciliation in the loop regardless of the previous manual path.
        filtrationFsm_.lastDesired = !filtrationDesired;
        filtrationFsm_.lastCmdMs = 0U;
    }

    // Ceinture-bretelles : l'interlock est deja une entree de la FSM de dosage,
    // ce forcage garantit l'arret meme si une branche l'avait ignore.
    if (noFlowError_) {
        phPumpDesired = false;
        orpPumpDesired = false;
        swgDesired = false;
    }

    // Non-simultaneite acide / chlore liquide : leur melange degage du chlore
    // gazeux. Priorite au pH, dont depend l'efficacite du chlore, et dont le lot
    // ne dure que quelques minutes par turnover.
    if (phPumpDesired && orpPumpDesired && phPumpDeviceSlot_ != orpPumpDeviceSlot_ &&
        isDisinfectionType_(DisinfectionChlorineBromine)) {
        orpPumpDesired = false;
        if (!dosingConflictLogged_) {
            LOGW("Dosing conflict: chlorine pump held off while pH batch is running");
            dosingConflictLogged_ = true;
        }
    } else {
        dosingConflictLogged_ = false;
    }

    (void)applyDeviceControl_(filtrationDeviceSlot_, "Filtration Pump", filtrationFsm_, filtrationDesired, nowMs);
    // Le resultat de la pompe pH est consomme au tour suivant par la FSM de
    // dosage, via phPumpFsm_.writeRejected / lastBlockReason.
    (void)applyDeviceControl_(phPumpDeviceSlot_, "pH Pump", phPumpFsm_, phPumpDesired, nowMs);
    // Un seul actionneur de desinfection est pilote selon le mode actif : la
    // pompe (chlore liquide / oxygene actif) OU l'electrolyseur. Le device du
    // mode non choisi n'est plus commande chaque tour ; il est deja mis OFF au
    // boot (defaut Desactive) et a chaque changement de mode (onEvent_).
    if (isDisinfectionType_(DisinfectionChlorineBromine) || isDisinfectionType_(DisinfectionActiveOxygen)) {
        (void)applyDeviceControl_(orpPumpDeviceSlot_,
                                  isDisinfectionType_(DisinfectionActiveOxygen) ? "O2 Pump" : "Chlorine Pump",
                                  orpPumpFsm_,
                                  orpPumpDesired,
                                  nowMs);
    } else if (isDisinfectionType_(DisinfectionSwg)) {
        (void)applyDeviceControl_(swgDeviceSlot_, "SWG Pump", swgFsm_, swgDesired, nowMs);
    }
    (void)applyDeviceControl_(robotDeviceSlot_, "Robot Pump", robotFsm_, robotDesired, nowMs);
    (void)applyDeviceControl_(heaterDeviceSlot_, "Water Heater", heaterFsm_, heaterDesired, nowMs);
    (void)applyDeviceControl_(fillingDeviceSlot_, "Filling Pump", fillingFsm_, fillingDesired, nowMs);
}
