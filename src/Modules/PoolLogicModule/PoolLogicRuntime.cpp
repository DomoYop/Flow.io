/**
 * @file PoolLogicRuntime.cpp
 * @brief Runtime and config publication helpers for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <stdio.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolLogicModule)
#include "Core/ModuleLog.h"

namespace {
/** Libelle de phase de la FSM de dosage, publie a cote du code numerique. */
const char* dosingPhaseStr_(uint8_t phase)
{
    switch (phase) {
        case DOSING_PHASE_IDLE: return "idle";
        case DOSING_PHASE_MEASURE: return "measure";
        case DOSING_PHASE_DOSING: return "dosing";
        case DOSING_PHASE_MIXING: return "mixing";
        case DOSING_PHASE_EVALUATE: return "evaluate";
        case DOSING_PHASE_BLOCKED: return "blocked";
        default: return "unknown";
    }
}

const char* dosingBlockReasonStr_(uint8_t reason)
{
    switch (reason) {
        case DOSING_BLOCK_NONE: return "none";
        case DOSING_BLOCK_DISABLED: return "disabled";
        case DOSING_BLOCK_NO_SAMPLE: return "no_sample";
        case DOSING_BLOCK_SAMPLE_STALE: return "sample_stale";
        case DOSING_BLOCK_SAMPLE_RANGE: return "sample_range";
        case DOSING_BLOCK_IN_BAND: return "in_band";
        case DOSING_BLOCK_WRONG_SIDE: return "wrong_side";
        case DOSING_BLOCK_DAY_QUOTA: return "day_quota";
        case DOSING_BLOCK_TANK_EMPTY: return "tank_empty";
        case DOSING_BLOCK_PUMP: return "pump";
        case DOSING_BLOCK_CONFIG: return "config";
        case DOSING_BLOCK_NO_EFFECT: return "no_effect";
        case DOSING_BLOCK_INTERLOCK: return "interlock";
        default: return "unknown";
    }
}

// The aggregated cfg payload republishes the same branch split used by the
// config routes so MQTT consumers can fetch one coherent snapshot.
static constexpr const char* kPoolLogicCfgTopicBase = "cfg/poollogic";
static constexpr const char* kCfgModuleBassin = "poollogic/bassin";
static constexpr const char* kCfgModuleFiltration = "poollogic/filtration";
static constexpr const char* kCfgModuleFiltrationWindows = "poollogic/filtration/fenetres";
static constexpr const char* kCfgModuleSensors = "poollogic/sensors";
static constexpr const char* kCfgModuleSafety = "poollogic/safety";
static constexpr const char* kCfgModulePh = "poollogic/ph";
static constexpr const char* kCfgModuleDisinfection = "poollogic/disinfection";
static constexpr const char* kCfgModuleHeater = "poollogic/heater";
static constexpr const char* kCfgModuleRobot = "poollogic/robot";
static constexpr const char* kCfgModuleRefill = "poollogic/refill";
}

MqttBuildResult PoolLogicModule::buildCfgBaseStatic_(void* ctx, uint16_t, MqttBuildContext& buildCtx)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(ctx);
    return self ? self->buildCfgBase_(buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult PoolLogicModule::buildCfgBase_(MqttBuildContext& buildCtx)
{
    if (!cfgStore_) return MqttBuildResult::RetryLater;
    if (!buildCtx.topic || buildCtx.topicCapacity == 0U || !buildCtx.payload || buildCtx.payloadCapacity == 0U) {
        return MqttBuildResult::PermanentError;
    }
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::RetryLater;

    char relativeTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    size_t topicLen = 0U;
    if (!MqttConfigRouteProducer::buildRelativeTopic(relativeTopic,
                                                     sizeof(relativeTopic),
                                                     kPoolLogicCfgTopicBase,
                                                     "",
                                                     topicLen)) {
        return MqttBuildResult::PermanentError;
    }
    mqttSvc_->formatTopic(mqttSvc_->ctx, relativeTopic, buildCtx.topic, buildCtx.topicCapacity);
    if (buildCtx.topic[0] == '\0') return MqttBuildResult::PermanentError;
    topicLen = strnlen(buildCtx.topic, buildCtx.topicCapacity);

    struct Entry {
        const char* key;
        const char* moduleName;
    };
    static constexpr Entry kEntries[] = {
        {"bassin", kCfgModuleBassin},
        {"filtration", kCfgModuleFiltration},
        // Sous-branche des fenetres : cle a plat pour rester accessible en
        // notation pointee cote consommateurs (value_json.fenetres.*).
        {"fenetres", kCfgModuleFiltrationWindows},
        {"sensors", kCfgModuleSensors},
        {"safety", kCfgModuleSafety},
        {"ph", kCfgModulePh},
        {"disinfection", kCfgModuleDisinfection},
        {"heater", kCfgModuleHeater},
        {"robot", kCfgModuleRobot},
        {"refill", kCfgModuleRefill},
    };

    buildCtx.payload[0] = '{';
    buildCtx.payload[1] = '\0';
    size_t pos = 1U;
    bool any = false;
    bool truncatedPayload = false;

    // Rebuild the aggregate JSON from per-branch module snapshots so the
    // payload stays a pure projection of the config store.
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        char moduleJson[640] = {0};
        bool truncatedModule = false;
        const bool hasAny = cfgStore_->toJsonModule(kEntries[i].moduleName,
                                                    moduleJson,
                                                    sizeof(moduleJson),
                                                    &truncatedModule);
        if (truncatedModule) {
            truncatedPayload = true;
            break;
        }
        if (!hasAny) continue;

        const int w = snprintf(buildCtx.payload + pos,
                               buildCtx.payloadCapacity - pos,
                               "%s\"%s\":%s",
                               any ? "," : "",
                               kEntries[i].key,
                               moduleJson);
        if (!(w > 0 && (size_t)w < (buildCtx.payloadCapacity - pos))) {
            truncatedPayload = true;
            break;
        }
        pos += (size_t)w;
        any = true;
    }

    if (truncatedPayload || pos + 2U > buildCtx.payloadCapacity) {
        if (!writeErrorJson(buildCtx.payload, buildCtx.payloadCapacity, ErrorCode::CfgTruncated, "cfg/poollogic")) {
            snprintf(buildCtx.payload, buildCtx.payloadCapacity, "{\"ok\":false}");
        }
        buildCtx.topicLen = (uint16_t)topicLen;
        buildCtx.payloadLen = (uint16_t)strnlen(buildCtx.payload, buildCtx.payloadCapacity);
        buildCtx.qos = 1;
        buildCtx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (!any) {
        LOGW("cfg base skipped: no data for %s", kPoolLogicCfgTopicBase);
        return MqttBuildResult::NoLongerNeeded;
    }

    buildCtx.payload[pos++] = '}';
    buildCtx.payload[pos] = '\0';
    buildCtx.topicLen = (uint16_t)topicLen;
    buildCtx.payloadLen = (uint16_t)pos;
    buildCtx.qos = 1;
    buildCtx.retain = true;
    return MqttBuildResult::Ready;
}

uint8_t PoolLogicModule::runtimeSnapshotCount() const
{
    return 6;
}

float PoolLogicModule::phEffectiveGainMlPerM3_() const
{
    return (phGainLearned_ > 0.0f) ? phGainLearned_ : phDoseMlPerM3_;
}

void PoolLogicModule::publishPhDosingRuntime_() const
{
    if (!dataStore_) return;

    PoolLogicPhDosingRuntimeData out{};
    out.valid = phDosingState_.tickValid;
    out.phase = phDosingLast_.phase;
    // Mode auto coupe : la FSM ne tourne plus et sa derniere sortie a ete remise a
    // zero, donc elle porte DOSING_BLOCK_NONE. Annoncer "aucune cause" serait faux :
    // la cause est le desarmement lui-meme, et c'est precisement ce que l'ecran doit
    // dire au moment ou plus rien ne se passe.
    out.blockReason = phAutoMode_ ? phDosingLast_.blockReason : (uint8_t)DOSING_BLOCK_DISABLED;
    // Hors phase de melange, le decompte n'a pas de sens : le consommateur doit
    // pouvoir distinguer "0 minute restante" de "pas d'attente en cours".
    out.mixing = (phDosingLast_.phase == DOSING_PHASE_MIXING);
    out.mixRemainMs = out.mixing ? phDosingLast_.mixRemainMs : 0U;
    out.batchTargetMl = phDosingLast_.doseTargetMl;
    out.batchDeliveredMl = phDosingLast_.doseDeliveredMl;
    out.haveExpectedDelta = phExpectedBatchDelta_(out.expectedDelta);
    out.dosedTodayMl = phDosedTodayMl_;
    out.gainMlPerM3 = phEffectiveGainMlPerM3_();
    (void)setPoolPhDosingRuntime(*dataStore_, out);
}

void PoolLogicModule::publishPressureRuntime_() const
{
    if (!dataStore_) return;

    PoolLogicPressureRuntimeData out{};
    out.referenceBar = pressureRefBar_;
    out.foulingThresholdBar = foulingThresholdBar_;
    out.foulingPct = foulingPct_;
    out.calibrated = (pressureRefBar_ > 0.0f);
    out.thresholdClamped = foulingThresholdClamped_;
    out.learning = (pressureLearnStartMs_ != 0U);
    (void)setPoolPressureRuntime(*dataStore_, out);
}

bool PoolLogicModule::phExpectedBatchDelta_(float& deltaOut) const
{
    deltaOut = 0.0f;
    const float target = phDosingLast_.doseTargetMl;
    if (!std::isfinite(target) || target <= 0.0f) return false;

    const float gain = phEffectiveGainMlPerM3_();
    const float unitStep = PoolDefaults::PhDoseUnitStep;
    if (!std::isfinite(gain) || gain <= 0.0f) return false;
    if (!std::isfinite(poolVolumeM3_) || poolVolumeM3_ <= 0.0f) return false;
    if (!std::isfinite(unitStep) || unitStep <= 0.0f) return false;

    const float magnitude = (target / (gain * poolVolumeM3_)) * unitStep;
    if (!std::isfinite(magnitude)) return false;
    // pH+ remonte la mesure, pH- la fait baisser : le signe dit dans quel sens
    // lire l'effet, sans avoir a connaitre le produit charge.
    deltaOut = phDosePlus_ ? magnitude : -magnitude;
    return true;
}

bool PoolLogicModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);

    switch (valueId) {
        case RuntimeUiAutoMode:
            return writer.writeBool(runtimeId, autoMode_);
        case RuntimeUiWinterMode:
            return writer.writeBool(runtimeId, winterMode_);
        case RuntimeUiPhAutoMode:
            return writer.writeBool(runtimeId, phAutoMode_);
        case RuntimeUiOrpAutoMode:
            return writer.writeBool(runtimeId, orpAutoMode_);
        case RuntimeUiHeaterAutoMode:
            return writer.writeBool(runtimeId, heaterAutoMode_);
        case RuntimeUiWaterTemp:
        case RuntimeUiAirTemp: {
            const IoId ioId = (valueId == RuntimeUiWaterTemp) ? waterTempIoId_ : airTempIoId_;
            float value = 0.0f;
            if (!loadAnalogSensor_(ioId, value)) return writer.writeUnavailable(runtimeId);
            return writer.writeF32(runtimeId, value);
        }
        case RuntimeUiPhDosePhase:
            return writer.writeEnum(runtimeId, phDosingLast_.phase);
        case RuntimeUiPhDoseDayMl:
            return writer.writeF32(runtimeId, phDosedTodayMl_);
        case RuntimeUiPhGain:
            return writer.writeF32(runtimeId, phEffectiveGainMlPerM3_());
        case RuntimeUiPhBlockReason:
            return writer.writeEnum(runtimeId, phDosingLast_.blockReason);
        case RuntimeUiPhMixRemainMin:
            // Le decompte n'avance que si la filtration brasse : hors phase de
            // melange il n'y a pas d'attente a annoncer, donc rien a afficher.
            if (phDosingLast_.phase != DOSING_PHASE_MIXING) return writer.writeUnavailable(runtimeId);
            return writer.writeF32(runtimeId, (float)phDosingLast_.mixRemainMs / 60000.0f);
        case RuntimeUiPhBatchTargetMl:
            return writer.writeF32(runtimeId, phDosingLast_.doseTargetMl);
        case RuntimeUiPhBatchDeliveredMl:
            return writer.writeF32(runtimeId, phDosingLast_.doseDeliveredMl);
        case RuntimeUiSensorHold:
            return writer.writeBool(runtimeId, sensorHoldActive_());
        case RuntimeUiPhExpectedDelta: {
            float delta = 0.0f;
            if (!phExpectedBatchDelta_(delta)) return writer.writeUnavailable(runtimeId);
            return writer.writeF32(runtimeId, delta);
        }
        default:
            return false;
    }
}

const char* PoolLogicModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    if (idx == 0) return "rt/poollogic/ph";
    if (idx == 1) return "rt/poollogic/orp";
    if (idx == 2) return "rt/poollogic/heat_assist";
    if (idx == 3) return "rt/poollogic/disinfection";
    if (idx == 4) return "rt/poollogic/flow";
    // Temperatures metier : la sonde publiee suit wat_temp_io_id /
    // air_temp_io_id, ce qui rend les entites HA independantes du slot IO.
    if (idx == 5) return "rt/poollogic/temp";
    return nullptr;
}

RuntimeRouteClass PoolLogicModule::runtimeSnapshotClass(uint8_t idx) const
{
    (void)idx;
    return RuntimeRouteClass::NumericThrottled;
}

bool PoolLogicModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    if (idx > 5) return false;
    if (key >= DATAKEY_IO_BASE && key < (DataKey)(DATAKEY_IO_BASE + IO_MAX_ENDPOINTS)) return true;
    if (key >= DATAKEY_POOL_DEVICE_STATE_BASE &&
        key < (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + POOL_DEVICE_MAX)) return true;
    return false;
}

bool PoolLogicModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;

    const uint32_t nowMs = millis();
    if (idx == 5) {
        // Temperatures metier. Une sonde absente sort en null : cote Home
        // Assistant l'entite passe "unavailable" plutot que de figer 0 degC.
        float waterTemp = 0.0f;
        float airTemp = 0.0f;
        const bool haveWater = loadAnalogSensor_(waterTempIoId_, waterTemp);
        const bool haveAir = loadAnalogSensor_(airTempIoId_, airTemp);

        char waterBuf[16] = "null";
        char airBuf[16] = "null";
        if (haveWater) snprintf(waterBuf, sizeof(waterBuf), "%.1f", (double)waterTemp);
        if (haveAir) snprintf(airBuf, sizeof(airBuf), "%.1f", (double)airTemp);

        const int wrote = snprintf(out, len, "{\"wat\":%s,\"air\":%s,\"t\":%lu}",
                                   waterBuf, airBuf, (unsigned long)nowMs);
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    if (idx == 3) {
        // Le sous-objet O2 n'est emis qu'en mode oxygene actif : hors O2 il serait
        // fige et inerte, donc du trafic MQTT inutile. Payload compact sinon.
        //
        // Les champs dt/dts decrivent le mode *en service*, celui fige au
        // demarrage : un mode choisi mais pas encore applique n'a aucun effet sur
        // ces snapshots. Le reglage lui-meme reste lisible sur cfg/poollogic/bassin.
        int wrote = 0;
        if (bootDisinfectionType_ == DisinfectionActiveOxygen) {
            wrote = snprintf(out,
                             len,
                             "{\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\","
                             "\"o2\":{\"state\":%u,\"state_s\":\"%s\",\"block\":%u,\"block_s\":\"%s\","
                             "\"last_day\":%u,\"done_ml\":%.1f,\"pending_ml\":%.1f,"
                             "\"plan_ml\":%.1f,\"flow_l_h\":%.2f},"
                             "\"t\":%lu}",
                             (unsigned)bootDisinfectionType_,
                             disinfectionTypeStr_(bootDisinfectionType_),
                             (unsigned)swgControlMode_,
                             swgControlModeStr_(swgControlMode_),
                             (unsigned)o2ProtocolState_,
                             o2ProtocolStateStr_(o2ProtocolState_),
                             (unsigned)o2BlockReason_,
                             o2BlockReasonStr_(o2BlockReason_),
                             (unsigned)o2LastDoseDay_,
                             (double)o2WeeklyDoneMl_,
                             (double)o2PendingMl_,
                             (double)o2LastPlannedDoseMl_,
                             (double)o2LastFlowLh_,
                             (unsigned long)nowMs);
        } else {
            wrote = snprintf(out,
                             len,
                             "{\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\",\"t\":%lu}",
                             (unsigned)bootDisinfectionType_,
                             disinfectionTypeStr_(bootDisinfectionType_),
                             (unsigned)swgControlMode_,
                             swgControlModeStr_(swgControlMode_),
                             (unsigned long)nowMs);
        }
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    if (idx == 2) {
        constexpr uint8_t kHeatAssistFlagProbeRunning = (1U << 0);
        constexpr uint8_t kHeatAssistFlagHeatingActive = (1U << 1);
        constexpr uint8_t kHeatAssistFlagFastCycle = (1U << 2);
        const bool probeRunning = (heatAssistFlags_ & kHeatAssistFlagProbeRunning) != 0U;
        const bool heatingActive = (heatAssistFlags_ & kHeatAssistFlagHeatingActive) != 0U;
        const bool fastCycle = (heatAssistFlags_ & kHeatAssistFlagFastCycle) != 0U;
        const uint16_t nowSec = (uint16_t)((nowMs / 1000UL) & 0xFFFFU);
        const uint16_t probeStartSec = (uint16_t)(heatAssistTimingPacked_ & 0xFFFFU);
        const uint16_t lastProbeEndSec = (uint16_t)((heatAssistTimingPacked_ >> 16) & 0xFFFFU);
        const auto reasonToStr = [this]() -> const char* {
            switch (heatAssistReason_) {
                case HeatAssistReason::Disabled: return "DISABLED";
                case HeatAssistReason::ManualMode: return "MANUAL_MODE";
                case HeatAssistReason::HydraulicBlocked: return "HYDRAULIC_BLOCKED";
                case HeatAssistReason::SetpointInvalid: return "SETPOINT_INVALID";
                case HeatAssistReason::TempUnavailable: return "TEMP_UNAVAILABLE";
                case HeatAssistReason::ProbeWait30m: return "PROBE_WAIT_30M";
                case HeatAssistReason::ProbeWait20m: return "PROBE_WAIT_20M";
                case HeatAssistReason::ProbeRunning: return "PROBE_RUNNING";
                case HeatAssistReason::Heating: return "HEATING";
                case HeatAssistReason::IdlePumpOn: return "IDLE_PUMP_ON";
                case HeatAssistReason::SetpointReached: return "SETPOINT_REACHED";
                default: return "UNKNOWN";
            }
        };
        const uint32_t idleIntervalMin = fastCycle ? 20U : 30U;
        const uint32_t idleIntervalSec = idleIntervalMin * 60U;
        const uint32_t probeRemainMs = probeRunning
                                           ? ((((uint16_t)(nowSec - probeStartSec)) >= 5U * 60U)
                                                  ? 0U
                                                  : ((5U * 60U) - (uint32_t)((uint16_t)(nowSec - probeStartSec))) * 1000U)
                                           : 0U;
        const uint32_t idleElapsedSec = (lastProbeEndSec == 0U) ? idleIntervalSec : (uint32_t)((uint16_t)(nowSec - lastProbeEndSec));
        const uint32_t idleRemainMs = probeRunning
                                          ? 0U
                                          : ((idleElapsedSec >= idleIntervalSec) ? 0U : (idleIntervalSec - idleElapsedSec) * 1000U);
        const int wrote = snprintf(out,
                                   len,
                                   "{\"en\":%s,\"pr\":%s,\"ha\":%s,\"fc\":%s,"
                                   "\"ri\":\"%s\",\"ivm\":%lu,\"prm\":%lu,\"irm\":%lu,\"t\":%lu}",
                                   (autoMode_ && heaterAutoMode_) ? "true" : "false",
                                   probeRunning ? "true" : "false",
                                   heatingActive ? "true" : "false",
                                   fastCycle ? "true" : "false",
                                   reasonToStr(),
                                   (unsigned long)idleIntervalMin,
                                   (unsigned long)probeRemainMs,
                                   (unsigned long)idleRemainMs,
                                   (unsigned long)nowMs);
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    if (idx == 4) {
        // Etat des sorties indicatrices flowswitch : recopie temporisee, volet,
        // et interlock securite (no_flow). Consomme par les binary_sensor HA.
        // `hold` s'y ajoute : c'est la meme question physique (l'eau circule-t-elle),
        // et il dit pourquoi les courbes pH/Redox sont plates.
        // L'encrassement du filtre s'y ajoute : c'est encore de l'hydraulique, et
        // publier un snapshot de plus couterait une route MQTT pour trois nombres.
        // `fouling_pct` vaut -1 tant que la reference n'est pas calibree, ce qu'un
        // 0 % ne saurait pas dire -- Home Assistant le traduit en « unavailable ».
        const int wrote = snprintf(out,
                                   len,
                                   "{\"flow_copy\":%s,\"cover\":%s,\"no_flow\":%s,\"hold\":%s,"
                                   "\"delay_s\":%u,\"p_ref\":%.2f,\"p_wash\":%.2f,"
                                   "\"fouling_pct\":%.0f,\"t\":%lu}",
                                   flowCopyOutState_ ? "true" : "false",
                                   coverClosedState_ ? "true" : "false",
                                   noFlowError_ ? "true" : "false",
                                   sensorHoldActive_() ? "true" : "false",
                                   (unsigned)flowCopyDelaySec_,
                                   (double)pressureRefBar_,
                                   (double)foulingThresholdBar_,
                                   (pressureRefBar_ > 0.0f) ? (double)foulingPct_ : -1.0,
                                   (unsigned long)nowMs);
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = nowMs ? nowMs : 1U;
        return true;
    }

    // Le pH est regule par dosage volumetrique par lots : son snapshot decrit la
    // FSM (phase, lot en cours, melange, gain appris) et non un PID temporel.
    if (idx == 0) {
        const DosingOutput& d = phDosingLast_;
        const bool haveSample = phDosingState_.tickValid && std::isfinite(d.error);
        int wrote = 0;
        if (haveSample) {
            wrote = snprintf(
                out, len,
                "{\"i\":\"ph\",\"in\":%.3f,\"sp\":%.3f,\"er\":%.3f,\"db\":%.3f,"
                "\"en\":%s,\"dm\":%s,\"ac\":%s,"
                "\"ph\":%u,\"phs\":\"%s\",\"blk\":%u,\"blks\":\"%s\","
                "\"tgt_ml\":%.1f,\"del_ml\":%.1f,"
                "\"mix_ms\":%lu,\"mix_rem_ms\":%lu,"
                "\"gain\":%.2f,\"gn\":%u,\"noeff\":%u,"
                "\"day_ml\":%.1f,\"tank_ml\":%.1f,\"flow_l_h\":%.2f,"
                "\"dt\":%u,\"dts\":\"%s\",\"t\":%lu}",
                (double)(phSetpoint_ + (phDosePlus_ ? -d.error : d.error)),
                (double)phSetpoint_,
                (double)d.error,
                (double)phDeadband_,
                phPidEnabled_ ? "true" : "false",
                d.pumpOn ? "true" : "false",
                phPumpFsm_.on ? "true" : "false",
                (unsigned)d.phase,
                dosingPhaseStr_(d.phase),
                (unsigned)d.blockReason,
                dosingBlockReasonStr_(d.blockReason),
                (double)d.doseTargetMl,
                (double)d.doseDeliveredMl,
                (unsigned long)d.mixWaitMs,
                (unsigned long)d.mixRemainMs,
                (double)phEffectiveGainMlPerM3_(),
                (unsigned)phGainSamples_,
                (unsigned)phDosingState_.noEffectCount,
                (double)phDosedTodayMl_,
                (double)phTankRemainMl_,
                (double)phPumpFlowLh_,
                (unsigned)bootDisinfectionType_,
                disinfectionTypeStr_(bootDisinfectionType_),
                (unsigned long)nowMs
            );
        } else {
            wrote = snprintf(
                out, len,
                "{\"i\":\"ph\",\"in\":null,\"sp\":%.3f,\"er\":null,\"db\":%.3f,"
                "\"en\":%s,\"dm\":false,\"ac\":%s,"
                "\"ph\":%u,\"phs\":\"%s\",\"blk\":%u,\"blks\":\"%s\","
                "\"gain\":%.2f,\"gn\":%u,"
                "\"day_ml\":%.1f,\"tank_ml\":%.1f,\"flow_l_h\":%.2f,"
                "\"dt\":%u,\"dts\":\"%s\",\"t\":%lu}",
                (double)phSetpoint_,
                (double)phDeadband_,
                phPidEnabled_ ? "true" : "false",
                phPumpFsm_.on ? "true" : "false",
                (unsigned)d.phase,
                dosingPhaseStr_(d.phase),
                (unsigned)d.blockReason,
                dosingBlockReasonStr_(d.blockReason),
                (double)phEffectiveGainMlPerM3_(),
                (unsigned)phGainSamples_,
                (double)phDosedTodayMl_,
                (double)phTankRemainMl_,
                (double)phPumpFlowLh_,
                (unsigned)bootDisinfectionType_,
                disinfectionTypeStr_(bootDisinfectionType_),
                (unsigned long)nowMs
            );
        }
        if (wrote < 0 || (size_t)wrote >= len) return false;
        maxTsOut = phDosingTsMs_ ? phDosingTsMs_ : 1U;
        return true;
    }

    if (idx != 1) return false;

    // Desinfection liquide : PID temporel inchange.
    const TemporalPidState& st = orpPidState_;
    const DeviceFsm& pumpFsm = orpPumpFsm_;
    const uint32_t windowMs = (orpWindowMs_ > 1000) ? (uint32_t)orpWindowMs_ : 1000U;

    uint32_t elapsedMs = 0;
    if (st.initialized) {
        const uint32_t rawElapsed = nowMs - st.windowStartMs;
        elapsedMs = (rawElapsed < windowMs) ? rawElapsed : windowMs;
    }

    // Runtime snapshots expose the last PID sample plus the derived on-window
    // so MQTT/UI consumers can inspect why dosing is currently active or idle.
    int wrote = 0;
    if (st.sampleValid) {
        wrote = snprintf(
            out, len,
            "{\"i\":\"orp\",\"in\":%.3f,\"sp\":%.3f,\"er\":%.3f,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":%lu,"
            "\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\",\"t\":%lu}",
            (double)st.sampleInput,
            (double)st.sampleSetpoint,
            (double)st.sampleError,
            orpPidEnabled_ ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)orpKp_,
            (double)orpKi_,
            (double)orpKd_,
            (long)orpWindowMs_,
            (long)disSampleMs_,
            (long)disMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned long)st.sampleTsMs,
            (unsigned)bootDisinfectionType_,
            disinfectionTypeStr_(bootDisinfectionType_),
            (unsigned)swgControlMode_,
            swgControlModeStr_(swgControlMode_),
            (unsigned long)nowMs
        );
    } else {
        wrote = snprintf(
            out, len,
            "{\"i\":\"orp\",\"in\":null,\"sp\":%.3f,\"er\":null,"
            "\"en\":%s,\"dm\":%s,\"ac\":%s,"
            "\"kp\":%.6f,\"ki\":%.6f,\"kd\":%.6f,"
            "\"w\":%ld,\"sm\":%ld,\"mo\":%ld,"
            "\"on\":%lu,\"we\":%lu,\"ct\":0,"
            "\"dt\":%u,\"dts\":\"%s\",\"swgm\":%u,\"swgms\":\"%s\",\"t\":%lu}",
            (double)orpSetpoint_,
            orpPidEnabled_ ? "true" : "false",
            st.lastDemandOn ? "true" : "false",
            pumpFsm.on ? "true" : "false",
            (double)orpKp_,
            (double)orpKi_,
            (double)orpKd_,
            (long)orpWindowMs_,
            (long)disSampleMs_,
            (long)disMinOnMs_,
            (unsigned long)st.outputOnMs,
            (unsigned long)elapsedMs,
            (unsigned)bootDisinfectionType_,
            disinfectionTypeStr_(bootDisinfectionType_),
            (unsigned)swgControlMode_,
            swgControlModeStr_(swgControlMode_),
            (unsigned long)nowMs
        );
    }
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = st.runtimeTsMs ? st.runtimeTsMs : 1U;
    return true;
}
