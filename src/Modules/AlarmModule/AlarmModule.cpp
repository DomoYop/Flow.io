/**
 * @file AlarmModule.cpp
 * @brief Implementation file.
 */

#include "AlarmModule.h"

#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/ModuleId.h"
#include "Core/MqttTopics.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <new>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::AlarmModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint8_t kAlarmCfgProducerId = 46;
static constexpr uint8_t kAlarmCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kAlarmCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Alarms, kAlarmCfgBranch}, "alarms", "alarms", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
// Etat d'une alarme vu de Home Assistant : le payload de buildAlarmState_()
// porte "a" (active). Un binary_sensor par AlarmId remplace le decodage du
// champ packe cote Home Assistant, qui dependait de l'ordre d'enregistrement
// des slots.
static constexpr const char* kAlarmActiveValueTemplate =
    "{{ 'True' if value_json.a | int(0) == 1 else 'False' }}";

// L'agregat s'appuie sur rt/alarms/m, qui porte le nombre d'alarmes actives.
static constexpr HABinarySensorEntry kAlarmAnyActiveBinarySensor{
    "alarms",
    "alm_any",
    "Any Active Alarm",
    "rt/alarms/m",
    "{{ 'True' if value_json.a | int(0) > 0 else 'False' }}",
    "problem",
    nullptr,
    "mdi:alarm-multiple"
};

// Le suffixe de topic contient l'AlarmId en dur : les static_assert ci-dessous
// cassent le build si un identifiant change sans que la table suive.
static constexpr HABinarySensorEntry kAlarmBinarySensors[] = {
    {"alarms", "alm_pressure_low", "Low Pressure", "rt/alarms/id1000",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:gauge-low"},
    {"alarms", "alm_pressure_high", "High Pressure", "rt/alarms/id1001",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:gauge-full"},
    {"alarms", "alm_ph_tank_low", "pH Tank Low", "rt/alarms/id1002",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:flask-empty-off-outline"},
    {"alarms", "alm_chlorine_tank_low", "Chlorine Tank Low", "rt/alarms/id1003",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:beaker-alert-outline"},
    {"alarms", "alm_ph_pump_max_uptime", "pH Pump Max Uptime", "rt/alarms/id1004",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:timer-alert-outline"},
    {"alarms", "alm_chlorine_pump_max_uptime", "Chlorine Pump Max Uptime", "rt/alarms/id1005",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:timer-alert-outline"},
    {"alarms", "alm_water_level_low", "Pool Water Level Low", "rt/alarms/id1006",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:waves-arrow-down"},
    {"alarms", "alm_ph_dose_no_effect", "pH Dosing Has No Effect", "rt/alarms/id1007",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:water-alert-outline"},
    {"alarms", "alm_water_temp_unavailable", "Water Temperature Unavailable", "rt/alarms/id1008",
     kAlarmActiveValueTemplate, "problem", nullptr, "mdi:thermometer-alert"},
};

static_assert((uint16_t)AlarmId::PoolPressureLow == 1000, "rt/alarms/id1000 must stay PoolPressureLow");
static_assert((uint16_t)AlarmId::PoolPressureHigh == 1001, "rt/alarms/id1001 must stay PoolPressureHigh");
static_assert((uint16_t)AlarmId::PoolPhTankLow == 1002, "rt/alarms/id1002 must stay PoolPhTankLow");
static_assert((uint16_t)AlarmId::PoolChlorineTankLow == 1003, "rt/alarms/id1003 must stay PoolChlorineTankLow");
static_assert((uint16_t)AlarmId::PoolPhPumpMaxUptime == 1004, "rt/alarms/id1004 must stay PoolPhPumpMaxUptime");
static_assert((uint16_t)AlarmId::PoolChlorinePumpMaxUptime == 1005, "rt/alarms/id1005 must stay PoolChlorinePumpMaxUptime");
static_assert((uint16_t)AlarmId::PoolWaterLevelLow == 1006, "rt/alarms/id1006 must stay PoolWaterLevelLow");
static_assert((uint16_t)AlarmId::PoolPhDoseNoEffect == 1007, "rt/alarms/id1007 must stay PoolPhDoseNoEffect");
static_assert((uint16_t)AlarmId::PoolWaterTemperatureUnavailable == 1008,
              "rt/alarms/id1008 must stay PoolWaterTemperatureUnavailable");

// LogWarningSeen (1100) et LogErrorSeen (1101) ne sont volontairement pas
// declarees : LogAlarmSinkModule est exclu du build_src_filter de ce profil,
// donc ces alarmes ne sont jamais enregistrees et leur topic jamais publie.
// Une entite HA sans publication resterait indefiniment "unknown".

// Un bouton par alarme, identifie par son AlarmId : plus aucun numero de slot
// dans l'interface, donc plus aucune dependance a l'ordre d'enregistrement.
// « Acquitter » couvre les deux gestes utiles -- effacer si la cause a disparu,
// faire taire sinon -- ce qui evite un second bouton par alarme.
static constexpr HAButtonEntry kAlarmAckButtons[] = {
    {"alarms", "alm_ack_pressure_low", "Acknowledge Low Pressure", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1000}}", "diagnostic", "mdi:gauge-low"},
    {"alarms", "alm_ack_pressure_high", "Acknowledge High Pressure", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1001}}", "diagnostic", "mdi:gauge-full"},
    {"alarms", "alm_ack_ph_tank_low", "Acknowledge pH Tank Low", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1002}}", "diagnostic", "mdi:flask-empty-off-outline"},
    {"alarms", "alm_ack_chlorine_tank_low", "Acknowledge Chlorine Tank Low", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1003}}", "diagnostic", "mdi:beaker-alert-outline"},
    {"alarms", "alm_ack_ph_pump_max_uptime", "Acknowledge pH Pump Max Uptime", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1004}}", "diagnostic", "mdi:timer-alert-outline"},
    {"alarms", "alm_ack_chlorine_pump_max_uptime", "Acknowledge Chlorine Pump Max Uptime", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1005}}", "diagnostic", "mdi:timer-alert-outline"},
    {"alarms", "alm_ack_water_level_low", "Acknowledge Pool Water Level Low", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1006}}", "diagnostic", "mdi:waves-arrow-down"},
    {"alarms", "alm_ack_ph_dose_no_effect", "Acknowledge pH Dosing Has No Effect", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1007}}", "diagnostic", "mdi:water-alert-outline"},
    {"alarms", "alm_ack_water_temp_unavailable", "Acknowledge Water Temperature Unavailable", MqttTopics::SuffixCmd,
     "{\"cmd\":\"alarms.ack\",\"args\":{\"id\":1008}}", "diagnostic", "mdi:thermometer-alert"},
};

// Entites de l'ancien modele par slot, republiees en pierre tombale (discovery
// vide) pour que Home Assistant les retire au lieu de les laisser orphelines.
// Supprimables apres une release : elles occupent une place de bouton chacune.
static constexpr HAButtonEntry kAlarmRetiredButtons[] = {
    {"alarms", "alm_reset_slot_0", "Reset Alarm Slot 0", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_1", "Reset Alarm Slot 1", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_2", "Reset Alarm Slot 2", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_3", "Reset Alarm Slot 3", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_4", "Reset Alarm Slot 4", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_5", "Reset Alarm Slot 5", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_6", "Reset Alarm Slot 6", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    {"alarms", "alm_reset_slot_7", "Reset Alarm Slot 7", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
    // Son payload_press etait double-echappe (HAModule applique deja jsonEscape),
    // donc ce bouton n'a jamais pu declencher la commande. Remplace par alm_ack_all.
    {"alarms", "alm_reset_all", "Reset Cleared Latched Alarms", MqttTopics::SuffixCmd, "{}", "diagnostic", nullptr, true},
};
}

static uint32_t clampEvalPeriodMs_(int32_t inMs)
{
    if (inMs < 25) return 25U;
    if (inMs > 5000) return 5000U;
    return (uint32_t)inMs;
}

static bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::Alarm::JsonCmdBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

bool AlarmModule::delayReached_(uint32_t sinceMs, uint32_t delayMs, uint32_t nowMs)
{
    if (delayMs == 0U) return true;
    if (sinceMs == 0U) return false;
    return (uint32_t)(nowMs - sinceMs) >= delayMs;
}

const char* AlarmModule::condStateStr_(AlarmCondState s)
{
    if (s == AlarmCondState::True) return "true";
    if (s == AlarmCondState::False) return "false";
    return "unknown";
}

void AlarmModule::emitAlarmEvent_(EventId id, AlarmId alarmId) const
{
    if (!eventBus_) return;
    const AlarmPayload payload{(uint16_t)alarmId};
    (void)eventBus_->post(id, &payload, sizeof(payload), ModuleId::Alarm);
}

uint32_t AlarmModule::nowEpoch_()
{
    if (!timeSvc_ && services_) timeSvc_ = services_->get<TimeService>(ServiceId::Time);
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return 0U;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return 0U;
    return (uint32_t)timeSvc_->epoch(timeSvc_->ctx);
}

// Journal circulaire des transitions. Sans lui, la seule trace d'une alarme
// retombee est une ligne de log serie que personne ne relit.
void AlarmModule::appendLog_(AlarmId id, LogEvent event)
{
    LogEntry entry{};
    entry.epochSec = nowEpoch_();
    entry.upMs = millis();
    entry.id = (uint16_t)id;
    entry.event = (uint8_t)event;
    entry.lifecycle = (uint8_t)lifecycle_(id);

    portENTER_CRITICAL(&slotsMux_);
    log_[logHead_] = entry;
    logHead_ = (uint8_t)((logHead_ + 1U) % kLogCapacity);
    if (logCount_ < kLogCapacity) ++logCount_;
    portEXIT_CRITICAL(&slotsMux_);
}

// Seules les alarmes latchees encore actives sont persistees : une alarme non
// latchee sera reevaluee en 250 ms, la persister n'apporterait rien.
void AlarmModule::persistLatches_()
{
    PersistedLatchBlob blob{};
    blob.magic = kLatchBlobMagic;
    blob.version = kLatchBlobVersion;

    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active || !s.def.latched) continue;
        if (blob.count >= (uint8_t)Limits::Alarm::MaxAlarms) break;
        blob.items[blob.count].id = (uint16_t)s.id;
        blob.items[blob.count].flags = s.acknowledged ? kLatchFlagAcknowledged : 0U;
        ++blob.count;
    }
    portEXIT_CRITICAL(&slotsMux_);

    if (cfgSvc_ && cfgSvc_->writeRuntimeBlobAsync) {
        (void)cfgSvc_->writeRuntimeBlobAsync(cfgSvc_->ctx, NvsKeys::Alarm::LatchBlob, &blob, sizeof(blob));
        return;
    }
    if (cfgStore_) {
        (void)cfgStore_->writeRuntimeBlob(NvsKeys::Alarm::LatchBlob, &blob, sizeof(blob));
    }
}

// Appelee apres l'enregistrement des alarmes (onConfigLoaded) : un latch de
// securite ne doit pas disparaitre parce que la carte a redemarre.
void AlarmModule::restoreLatches_()
{
    if (!cfgStore_) return;

    PersistedLatchBlob blob{};
    size_t actualLen = 0U;
    if (!cfgStore_->readRuntimeBlob(NvsKeys::Alarm::LatchBlob, &blob, sizeof(blob), &actualLen)) return;
    if (actualLen != sizeof(blob)) return;
    if (blob.magic != kLatchBlobMagic || blob.version != kLatchBlobVersion) return;
    if (blob.count == 0U) return;

    const uint32_t nowMs = millis();
    uint8_t restoredCount = 0U;
    AlarmId restoredIds[Limits::Alarm::MaxAlarms]{};

    for (uint8_t i = 0; i < blob.count && i < (uint8_t)Limits::Alarm::MaxAlarms; ++i) {
        const AlarmId id = (AlarmId)blob.items[i].id;
        const bool acked = (blob.items[i].flags & kLatchFlagAcknowledged) != 0U;

        portENTER_CRITICAL(&slotsMux_);
        const int16_t idx = findSlotById_(id);
        bool applied = false;
        if (idx >= 0) {
            AlarmSlot& s = slots_[(uint16_t)idx];
            // Une alarme dont la definition n'est plus latchee ne doit pas rester
            // bloquee : elle se remettra a jour seule a la premiere evaluation.
            if (s.def.latched && !s.active) {
                s.active = true;
                s.acknowledged = acked;
                s.restored = true;
                s.activeSinceMs = nowMs;
                s.lastChangeMs = nowMs;
                s.lastNotifyMs = nowMs;
                // La condition n'a pas encore ete evaluee : ni vraie ni fausse.
                s.lastCond = AlarmCondState::Unknown;
                applied = true;
            }
        }
        portEXIT_CRITICAL(&slotsMux_);

        if (applied) restoredIds[restoredCount++] = id;
    }

    for (uint8_t i = 0; i < restoredCount; ++i) {
        appendLog_(restoredIds[i], LogEvent::Restored);
    }
    if (restoredCount > 0U) {
        LOGI("Alarm latches restored count=%u", (unsigned)restoredCount);
    }
}

void AlarmModule::noteAlarmNotified_(AlarmId id, uint32_t nowMs)
{
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        AlarmSlot& s = slots_[(uint16_t)idx];
        s.lastNotifyMs = nowMs;
    }
    portEXIT_CRITICAL(&slotsMux_);
}

uint8_t AlarmModule::takeDueAlarmReminderIds_(AlarmId* out, uint8_t max, uint32_t nowMs)
{
    if (!out || max == 0U) return 0U;

    uint8_t count = 0U;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms && count < max; ++i) {
        AlarmSlot& s = slots_[i];
        if (!s.used || !s.active || s.lastCond != AlarmCondState::True) continue;
        // Acquittee : l'operateur a vu, le rappel periodique n'a plus d'objet.
        if (s.acknowledged) continue;
        const uint32_t minRepeatMs = s.def.minRepeatMs;
        if (minRepeatMs > 0U &&
            s.lastNotifyMs != 0U &&
            delayReached_(s.lastNotifyMs, minRepeatMs, nowMs)) {
            s.lastNotifyMs = nowMs;
            out[count++] = s.id;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

int16_t AlarmModule::findSlotById_(AlarmId id) const
{
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (!slots_[i].used) continue;
        if (slots_[i].id == id) return (int16_t)i;
    }
    return -1;
}

int16_t AlarmModule::findFreeSlot_() const
{
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (!slots_[i].used) return (int16_t)i;
    }
    return -1;
}

bool AlarmModule::registerAlarm_(const AlarmRegistration& def, AlarmCondFn condFn, void* condCtx)
{
    if (!condFn) return false;
    if (def.id == AlarmId::None) return false;
    if (def.code[0] == '\0') return false;
    if (def.title[0] == '\0') return false;

    bool ok = false;
    portENTER_CRITICAL(&slotsMux_);
    if (findSlotById_(def.id) >= 0) {
        ok = false;
    } else {
        const int16_t idx = findFreeSlot_();
        if (idx >= 0) {
            AlarmSlot& s = slots_[(uint16_t)idx];
            s = AlarmSlot{};
            s.used = true;
            s.id = def.id;
            s.def = def;
            s.condFn = condFn;
            s.condCtx = condCtx;
            ok = true;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    if (ok) {
        LOGI("Alarm registered id=%u code=%s", (unsigned)def.id, def.code);
    } else {
        LOGW("Alarm registration failed id=%u", (unsigned)def.id);
    }
    return ok;
}

bool AlarmModule::reset_(AlarmId id)
{
    bool postReset = false;
    bool warnConditionTrue = false;
    bool warnNotActive = false;
    bool warnNotLatched = false;
    char alarmCode[sizeof(slots_[0].def.code)] = {0};
    AlarmCondState resetCond = AlarmCondState::Unknown;
    uint32_t nowMs = millis();

    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        AlarmSlot& s = slots_[(uint16_t)idx];
        strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
        alarmCode[sizeof(alarmCode) - 1] = '\0';
        if (s.active && s.def.latched && s.lastCond == AlarmCondState::False) {
            resetCond = s.lastCond;
            s.active = false;
            s.acknowledged = false;
            s.ackAtMs = 0U;
            s.restored = false;
            s.activeSinceEpoch = 0U;
            s.offSinceMs = 0U;
            s.lastChangeMs = nowMs;
            s.lastNotifyMs = nowMs;
            postReset = true;
        } else {
            resetCond = s.lastCond;
            warnConditionTrue = s.active && s.def.latched && s.lastCond == AlarmCondState::True;
            warnNotActive = !s.active;
            warnNotLatched = !s.def.latched;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    if (postReset) {
        LOGD("Alarm reset request accepted id=%u code=%s cond=%s",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?",
             condStateStr_(resetCond));
        LOGI("Alarm reset id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        appendLog_(id, LogEvent::Reset);
        persistLatches_();
        emitAlarmEvent_(EventId::AlarmReset, id);
        emitAlarmEvent_(EventId::AlarmCleared, id);
    } else if (warnConditionTrue) {
        LOGW("Alarm reset denied id=%u code=%s cond=true active=1 latched=1",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?");
    } else if (warnNotActive || warnNotLatched) {
        LOGW("Alarm reset denied id=%u code=%s active=%u latched=%u cond=%s",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?",
             warnNotActive ? 0U : 1U,
             warnNotLatched ? 0U : 1U,
             condStateStr_(resetCond));
    }
    return postReset;
}

uint8_t AlarmModule::resetAll_()
{
    AlarmId pending[Limits::Alarm::MaxAlarms]{};
    uint8_t pendingCount = 0;

    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active || !s.def.latched || s.lastCond != AlarmCondState::False) continue;
        if (pendingCount < Limits::Alarm::MaxAlarms) {
            pending[pendingCount++] = s.id;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    uint8_t resetCount = 0;
    for (uint8_t i = 0; i < pendingCount; ++i) {
        if (reset_(pending[i])) ++resetCount;
    }
    return resetCount;
}

// Geste unique de l'operateur : « j'ai vu ». Toujours accepte sur une alarme
// active, contrairement a reset_() qui exige que la cause ait disparu. Si la
// condition est deja retombee, acquitter revient a effacer -- c'est ce que
// l'utilisateur attend d'un bouton unique par alarme.
bool AlarmModule::ack_(AlarmId id)
{
    bool clearedByReset = false;
    bool postAck = false;
    bool warnNotActive = false;
    char alarmCode[sizeof(slots_[0].def.code)] = {0};
    const uint32_t nowMs = millis();

    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        AlarmSlot& s = slots_[(uint16_t)idx];
        strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
        alarmCode[sizeof(alarmCode) - 1] = '\0';
        if (!s.active) {
            warnNotActive = true;
        } else if (s.def.latched && s.lastCond == AlarmCondState::False) {
            clearedByReset = true;
        } else if (!s.acknowledged) {
            s.acknowledged = true;
            s.ackAtMs = nowMs;
            s.lastNotifyMs = nowMs;
            postAck = true;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    // Hors section critique : reset_() reprend le verrou pour son compte.
    if (clearedByReset) return reset_(id);

    if (postAck) {
        LOGI("Alarm acknowledged id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        appendLog_(id, LogEvent::Acked);
        // L'acquittement doit survivre au redemarrage, sinon la carte se remet a
        // sonner apres une coupure pour un defaut deja vu.
        persistLatches_();
        emitAlarmEvent_(EventId::AlarmSilenceChanged, id);
        return true;
    }

    if (warnNotActive) {
        LOGW("Alarm ack denied id=%u code=%s active=0", (unsigned)id, alarmCode[0] ? alarmCode : "?");
    }
    return false;
}

uint8_t AlarmModule::ackAll_()
{
    AlarmId pending[Limits::Alarm::MaxAlarms]{};
    uint8_t pendingCount = 0;

    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active) continue;
        // Deja acquittee et cause toujours presente : rien a faire.
        if (s.acknowledged && !(s.def.latched && s.lastCond == AlarmCondState::False)) continue;
        if (pendingCount < Limits::Alarm::MaxAlarms) {
            pending[pendingCount++] = s.id;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    uint8_t ackCount = 0;
    for (uint8_t i = 0; i < pendingCount; ++i) {
        if (ack_(pending[i])) ++ackCount;
    }
    return ackCount;
}

bool AlarmModule::isActive_(AlarmId id) const
{
    bool out = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) out = slots_[(uint16_t)idx].active;
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

bool AlarmModule::isResettable_(AlarmId id) const
{
    bool out = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        const AlarmSlot& slot = slots_[(uint16_t)idx];
        out = slot.active && slot.def.latched && slot.lastCond == AlarmCondState::False;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

// Source unique des affichages : (condition, active, acquittee) -> un seul etat.
// Toute interface qui recompose ces trois dimensions elle-meme finit par diverger,
// c'est ce qui est arrive aux masques par slot et au champ packe.
AlarmLifecycle AlarmModule::lifecycle_(AlarmId id) const
{
    AlarmLifecycle out = AlarmLifecycle::Unavailable;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        const AlarmSlot& s = slots_[(uint16_t)idx];
        if (s.active) {
            if (s.def.latched && s.lastCond == AlarmCondState::False) {
                out = AlarmLifecycle::ClearedUnacked;
            } else {
                out = s.acknowledged ? AlarmLifecycle::ActiveAcked : AlarmLifecycle::ActiveUnacked;
            }
        } else {
            out = (s.lastCond == AlarmCondState::Unknown) ? AlarmLifecycle::Unavailable
                                                          : AlarmLifecycle::Normal;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

// Le buffer pointe appartient au slot et n'est plus ecrit apres enregistrement :
// un slot n'est jamais libere, le pointeur reste donc valide.
const char* AlarmModule::codeOf_(AlarmId id) const
{
    const char* out = nullptr;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) out = slots_[(uint16_t)idx].def.code;
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

bool AlarmModule::isAcknowledged_(AlarmId id) const
{
    bool out = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        const AlarmSlot& slot = slots_[(uint16_t)idx];
        out = slot.active && slot.acknowledged;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

uint8_t AlarmModule::activeCount_() const
{
    uint8_t count = 0;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (slots_[i].used && slots_[i].active) ++count;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

// Ce que doit suivre l'annonciation (buzzer, LED, notification) : une alarme
// acquittee reste active et visible, mais ne doit plus reclamer l'attention.
uint8_t AlarmModule::unackedCount_() const
{
    uint8_t count = 0;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (s.used && s.active && !s.acknowledged) ++count;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

AlarmSeverity AlarmModule::highestSeverityFiltered_(bool unackedOnly) const
{
    AlarmSeverity highest = AlarmSeverity::Info;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active) continue;
        if (unackedOnly && s.acknowledged) continue;
        if ((uint8_t)s.def.severity > (uint8_t)highest) highest = s.def.severity;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return highest;
}

AlarmSeverity AlarmModule::highestSeverity_() const
{
    return highestSeverityFiltered_(false);
}

AlarmSeverity AlarmModule::highestUnackedSeverity_() const
{
    return highestSeverityFiltered_(true);
}

bool AlarmModule::buildSnapshot_(char* out, size_t len) const
{
    if (!out || len == 0) return false;

    AlarmSlot snap[Limits::Alarm::MaxAlarms]{};
    portENTER_CRITICAL(&slotsMux_);
    memcpy(snap, slots_, sizeof(snap));
    portEXIT_CRITICAL(&slotsMux_);

    const uint8_t active = activeCount_();
    const uint8_t unacked = unackedCount_();
    const AlarmSeverity highest = highestSeverity_();

    int wrote = snprintf(
        out,
        len,
        "{\"ok\":true,\"active_count\":%u,\"unacked_count\":%u,\"highest_severity\":%u,\"alarms\":[",
        (unsigned)active,
        (unsigned)unacked,
        (unsigned)((uint8_t)highest));
    if (wrote <= 0 || (size_t)wrote >= len) return false;

    size_t pos = (size_t)wrote;
    bool first = true;
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = snap[i];
        if (!s.used || !s.active) continue;

        wrote = snprintf(
            out + pos,
            len - pos,
            "%s{\"id\":%u,\"code\":\"%s\",\"title\":\"%s\",\"active\":true,"
            "\"resettable\":%s,\"acknowledged\":%s,\"severity\":%u}",
            first ? "" : ",",
            (unsigned)s.id,
            s.def.code,
            s.def.title,
            (s.def.latched && s.lastCond == AlarmCondState::False) ? "true" : "false",
            s.acknowledged ? "true" : "false",
            (unsigned)((uint8_t)s.def.severity));
        if (wrote <= 0 || (size_t)wrote >= (len - pos)) return false;
        pos += (size_t)wrote;
        first = false;
    }

    if ((len - pos) < 3) return false;
    out[pos++] = ']';
    out[pos++] = '}';
    out[pos] = '\0';
    return true;
}

// Journal du plus recent au plus ancien, tronque des que le buffer de reponse
// est plein : la taille du buffer varie selon l'appelant (commande MQTT, HTTP).
bool AlarmModule::buildLog_(char* out, size_t len) const
{
    if (!out || len == 0) return false;

    LogEntry snap[kLogCapacity]{};
    uint8_t count = 0U;
    uint8_t head = 0U;
    portENTER_CRITICAL(&slotsMux_);
    memcpy(snap, log_, sizeof(snap));
    count = logCount_;
    head = logHead_;
    portEXIT_CRITICAL(&slotsMux_);

    int wrote = snprintf(out, len, "{\"ok\":true,\"count\":%u,\"entries\":[", (unsigned)count);
    if (wrote <= 0 || (size_t)wrote >= len) return false;
    size_t pos = (size_t)wrote;

    bool first = true;
    bool truncated = false;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t idx = (uint8_t)((head + kLogCapacity - 1U - i) % kLogCapacity);
        const LogEntry& e = snap[idx];

        char item[96] = {0};
        wrote = snprintf(item,
                         sizeof(item),
                         "%s{\"id\":%u,\"e\":%u,\"l\":%u,\"t\":%lu,\"ms\":%lu}",
                         first ? "" : ",",
                         (unsigned)e.id,
                         (unsigned)e.event,
                         (unsigned)e.lifecycle,
                         (unsigned long)e.epochSec,
                         (unsigned long)e.upMs);
        if (wrote <= 0) return false;
        // 16 octets de marge pour la fermeture et le drapeau de troncature.
        if (pos + (size_t)wrote + 16U >= len) {
            truncated = true;
            break;
        }
        memcpy(out + pos, item, (size_t)wrote);
        pos += (size_t)wrote;
        first = false;
    }

    wrote = snprintf(out + pos, len - pos, "],\"more\":%s}", truncated ? "true" : "false");
    return (wrote > 0) && ((size_t)wrote < (len - pos));
}

uint8_t AlarmModule::listIds_(AlarmId* out, uint8_t max) const
{
    if (!out || max == 0) return 0;
    uint8_t count = 0;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms && count < max; ++i) {
        if (!slots_[i].used) continue;
        out[count++] = slots_[i].id;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

bool AlarmModule::buildAlarmState_(AlarmId id, char* out, size_t len) const
{
    if (!out || len == 0) return false;

    AlarmSlot snap{};
    bool found = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        snap = slots_[(uint16_t)idx];
        found = true;
    }
    portEXIT_CRITICAL(&slotsMux_);
    if (!found) return false;

    // `l` porte l'etat consolide (AlarmLifecycle) ; a/r/k/c restent publies
    // separement pour les automatisations qui les consomment deja.
    // Le numero de slot n'est plus expose : il n'a jamais ete une identite.
    // `t` = epoch du declenchement (0 si l'horloge n'etait pas synchronisee),
    // `lc` = millis() conserve comme repli avant synchronisation NTP.
    const int wrote = snprintf(
        out,
        len,
        "{\"id\":%u,\"l\":%u,\"a\":%u,\"r\":%u,\"k\":%u,\"c\":%u,\"s\":%u,\"t\":%lu,\"lc\":%lu}",
        (unsigned)snap.id,
        (unsigned)((uint8_t)lifecycle_(id)),
        snap.active ? 1u : 0u,
        (snap.active && snap.def.latched && snap.lastCond == AlarmCondState::False) ? 1u : 0u,
        (snap.active && snap.acknowledged) ? 1u : 0u,
        (unsigned)((uint8_t)snap.lastCond),
        (unsigned)((uint8_t)snap.def.severity),
        (unsigned long)snap.activeSinceEpoch,
        (unsigned long)snap.lastChangeMs);
    return (wrote > 0) && ((size_t)wrote < len);
}

// L'ordre de cette table n'a aucune importance fonctionnelle : chaque entree porte
// son AlarmId. C'est precisement ce que les anciens masques ne garantissaient pas.
const AlarmModule::RuntimeUiAlarmEntry AlarmModule::kRuntimeUiAlarms[9] = {
    {11, AlarmId::PoolPressureLow, "alarms.pressure_low"},
    {12, AlarmId::PoolPressureHigh, "alarms.pressure_high"},
    {13, AlarmId::PoolPhTankLow, "alarms.ph_tank_low"},
    {14, AlarmId::PoolChlorineTankLow, "alarms.chlorine_tank_low"},
    {15, AlarmId::PoolPhPumpMaxUptime, "alarms.ph_pump_max_uptime"},
    {16, AlarmId::PoolChlorinePumpMaxUptime, "alarms.chlorine_pump_max_uptime"},
    {17, AlarmId::PoolWaterLevelLow, "alarms.water_level_low"},
    {18, AlarmId::PoolPhDoseNoEffect, "alarms.ph_dose_no_effect"},
    {19, AlarmId::PoolWaterTemperatureUnavailable, "alarms.water_temp_unavailable"},
};

bool AlarmModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    for (const RuntimeUiAlarmEntry& entry : kRuntimeUiAlarms) {
        if (entry.valueId != valueId) continue;
        const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
        return writer.writeU32(runtimeId, (uint32_t)(uint8_t)lifecycle_(entry.alarmId));
    }
    return false;
}

bool AlarmModule::registerAlarmSvc_(const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx)
{
    if (!def) return false;
    return registerAlarm_(*def, condFn, condCtx);
}

bool AlarmModule::cmdList_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    if (!self->buildSnapshot_(reply, replyLen)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InternalAckOverflow, "alarms.list")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    return true;
}

bool AlarmModule::cmdLog_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    if (!self->buildLog_(reply, replyLen)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InternalAckOverflow, "alarms.log")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    return true;
}

bool AlarmModule::handleCmdReset_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingArgs, "alarms.reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args.containsKey("id")) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingValue, "alarms.reset.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args["id"].is<uint16_t>() && !args["id"].is<uint32_t>() && !args["id"].is<int32_t>()) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InvalidEventId, "alarms.reset.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    const uint32_t idRaw = args["id"].as<uint32_t>();
    const AlarmId id = (AlarmId)((uint16_t)idRaw);
    if (!reset_(id)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "alarms.reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"id\":%u}", (unsigned)((uint16_t)id));
    return true;
}

bool AlarmModule::cmdReset_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    return self->handleCmdReset_(req, reply, replyLen);
}

bool AlarmModule::handleCmdAck_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingArgs, "alarms.ack")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args.containsKey("id")) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingValue, "alarms.ack.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args["id"].is<uint16_t>() && !args["id"].is<uint32_t>() && !args["id"].is<int32_t>()) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InvalidEventId, "alarms.ack.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    const uint32_t idRaw = args["id"].as<uint32_t>();
    const AlarmId id = (AlarmId)((uint16_t)idRaw);
    if (!ack_(id)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "alarms.ack")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"id\":%u}", (unsigned)((uint16_t)id));
    return true;
}

bool AlarmModule::cmdAck_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    return self->handleCmdAck_(req, reply, replyLen);
}

bool AlarmModule::cmdAckAll_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    const uint8_t ackCount = self->ackAll_();
    snprintf(reply, replyLen, "{\"ok\":true,\"acked\":%u}", (unsigned)ackCount);
    return true;
}

bool AlarmModule::cmdResetAll_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    const uint8_t resetCount = self->resetAll_();
    snprintf(reply, replyLen, "{\"ok\":true,\"reset\":%u}", (unsigned)resetCount);
    return true;
}

void AlarmModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Alarms;
    constexpr uint8_t kCfgBranchId = kAlarmCfgBranch;
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(evalPeriodVar_, kCfgModuleId, kCfgBranchId);

    services_ = &services;
    cfgStore_ = &cfg;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const EventBusService* eb = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eb ? eb->bus : nullptr;
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    haSvc_ = services.get<HAService>(ServiceId::Ha);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);

    if (!services.add(ServiceId::Alarm, &alarmSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Alarm));
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.list", &AlarmModule::cmdList_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.log", &AlarmModule::cmdLog_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.reset", &AlarmModule::cmdReset_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.reset_all", &AlarmModule::cmdResetAll_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.ack", &AlarmModule::cmdAck_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.ack_all", &AlarmModule::cmdAckAll_, this);
    }

    LOGI("Alarm service registered");
    (void)logHub_;
}

void AlarmModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>(ServiceId::Ha);
    if (!haSvc_) return;

    bool registeredAny = false;

    if (haSvc_->addSensor) {
        // Pierre tombale : le champ packe a ete remplace par un binary_sensor et
        // un bouton d'acquittement par alarme. Supprimable apres une release.
        const HASensorEntry retiredPack{
            "alarms",
            "alm_pack",
            "Alarms Pack",
            "rt/alarms/m",
            // Champs inutilises par une pierre tombale, mais addSensorEntry() les
            // exige non nuls avant d'accepter l'entree.
            "{{ 0 }}",
            "diagnostic",
            nullptr,
            nullptr,
            false,
            nullptr,
            false,
            true
        };
        if (!haSvc_->addSensor(haSvc_->ctx, &retiredPack)) {
            LOGW("HA tombstone registration failed: alm_pack");
        }
    }

    if (haSvc_->addBinarySensor) {
        if (haSvc_->addBinarySensor(haSvc_->ctx, &kAlarmAnyActiveBinarySensor)) {
            registeredAny = true;
        } else {
            LOGW("HA registration failed: %s", kAlarmAnyActiveBinarySensor.objectSuffix);
        }

        for (uint8_t i = 0; i < (uint8_t)(sizeof(kAlarmBinarySensors) / sizeof(kAlarmBinarySensors[0])); ++i) {
            if (haSvc_->addBinarySensor(haSvc_->ctx, &kAlarmBinarySensors[i])) {
                registeredAny = true;
            } else {
                LOGW("HA registration failed: %s", kAlarmBinarySensors[i].objectSuffix);
            }
        }
    }

    if (haSvc_->addButton) {
        // Payload en JSON brut : publishButton() applique jsonEscape() avant de
        // l'inserer dans le message de discovery.
        const HAButtonEntry ackAll{
            "alarms",
            "alm_ack_all",
            "Acknowledge All Alarms",
            MqttTopics::SuffixCmd,
            "{\"cmd\":\"alarms.ack_all\"}",
            "diagnostic",
            "mdi:bell-check"
        };
        if (haSvc_->addButton(haSvc_->ctx, &ackAll)) {
            registeredAny = true;
        } else {
            LOGW("HA registration failed: alm_ack_all");
        }

        for (uint8_t i = 0; i < (uint8_t)(sizeof(kAlarmAckButtons) / sizeof(kAlarmAckButtons[0])); ++i) {
            if (haSvc_->addButton(haSvc_->ctx, &kAlarmAckButtons[i])) {
                registeredAny = true;
            } else {
                LOGW("HA registration failed: %s", kAlarmAckButtons[i].objectSuffix);
            }
        }

        for (uint8_t i = 0; i < (uint8_t)(sizeof(kAlarmRetiredButtons) / sizeof(kAlarmRetiredButtons[0])); ++i) {
            if (!haSvc_->addButton(haSvc_->ctx, &kAlarmRetiredButtons[i])) {
                LOGW("HA tombstone registration failed: %s", kAlarmRetiredButtons[i].objectSuffix);
            }
        }
    }

    haEntitiesRegistered_ = registeredAny;
}

void AlarmModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kAlarmCfgProducerId,
                               kAlarmCfgRoutes,
                               (uint8_t)(sizeof(kAlarmCfgRoutes) / sizeof(kAlarmCfgRoutes[0])),
                               services);
    }
    evalPeriodMsCfg_ = (int32_t)clampEvalPeriodMs_(evalPeriodMsCfg_);
    // Tous les init() ont eu lieu : les alarmes des modules metier sont
    // enregistrees, leurs latchs d'avant coupure peuvent etre reposes.
    restoreLatches_();
    registerHaEntities_(services);
}

void AlarmModule::evaluateOnce_(uint32_t nowMs)
{
    // Lu une fois par passe : nowEpoch_() interroge le service de temps.
    const uint32_t epochNow = nowEpoch_();
    AlarmId dueNotifyIds[Limits::Alarm::MaxAlarms]{};
    const uint8_t dueNotifyCount = takeDueAlarmReminderIds_(dueNotifyIds, (uint8_t)Limits::Alarm::MaxAlarms, nowMs);
    for (uint8_t i = 0; i < dueNotifyCount; ++i) {
        emitAlarmEvent_(EventId::AlarmConditionChanged, dueNotifyIds[i]);
    }

    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        AlarmId id = AlarmId::None;
        AlarmCondFn condFn = nullptr;
        void* condCtx = nullptr;
        bool used = false;

        portENTER_CRITICAL(&slotsMux_);
        if (slots_[i].used) {
            used = true;
            id = slots_[i].id;
            condFn = slots_[i].condFn;
            condCtx = slots_[i].condCtx;
        }
        portEXIT_CRITICAL(&slotsMux_);

        if (!used || !condFn) continue;

        const AlarmCondState cond = condFn(condCtx, nowMs);
        bool postRaised = false;
        bool postCleared = false;
        bool postCondTrue = false;
        bool postCondFalse = false;
        char alarmCode[sizeof(slots_[0].def.code)] = {0};

        portENTER_CRITICAL(&slotsMux_);
        AlarmSlot& s = slots_[i];
        if (s.used && s.id == id && s.condFn == condFn && s.condCtx == condCtx) {
            const AlarmCondState prevCond = s.lastCond;
            if (prevCond != cond) {
                if (cond == AlarmCondState::True) {
                    postCondTrue = true;
                    strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                    alarmCode[sizeof(alarmCode) - 1] = '\0';
                } else if (cond == AlarmCondState::False) {
                    postCondFalse = true;
                    strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                    alarmCode[sizeof(alarmCode) - 1] = '\0';
                }
            }
            s.lastCond = cond;

            if (cond == AlarmCondState::True) {
                s.offSinceMs = 0U;
                if (!s.active) {
                    if (s.onSinceMs == 0U) s.onSinceMs = nowMs;
                    if (delayReached_(s.onSinceMs, s.def.onDelayMs, nowMs)) {
                        s.active = true;
                        // Nouvelle occurrence : l'acquittement precedent ne vaut
                        // plus, l'annonciation doit repartir.
                        s.acknowledged = false;
                        s.ackAtMs = 0U;
                        s.restored = false;
                        s.activeSinceEpoch = epochNow;
                        s.activeSinceMs = nowMs;
                        s.lastChangeMs = nowMs;
                        s.onSinceMs = 0U;
                        postRaised = true;
                        strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                        alarmCode[sizeof(alarmCode) - 1] = '\0';
                    }
                } else {
                    s.onSinceMs = 0U;
                }
            } else if (cond == AlarmCondState::False) {
                s.onSinceMs = 0U;
                if (s.active) {
                    if (!s.def.latched) {
                        if (s.offSinceMs == 0U) s.offSinceMs = nowMs;
                        if (delayReached_(s.offSinceMs, s.def.offDelayMs, nowMs)) {
                            s.active = false;
                            s.acknowledged = false;
                            s.ackAtMs = 0U;
                            s.restored = false;
                            s.activeSinceEpoch = 0U;
                            s.offSinceMs = 0U;
                            s.lastChangeMs = nowMs;
                            postCleared = true;
                            strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                            alarmCode[sizeof(alarmCode) - 1] = '\0';
                        }
                    } else {
                        s.offSinceMs = 0U;
                    }
                } else {
                    s.offSinceMs = 0U;
                }
            } else {
                // Unknown sensor/state: cancel transition timers, keep stable alarm state.
                s.onSinceMs = 0U;
                s.offSinceMs = 0U;
            }
        }
        portEXIT_CRITICAL(&slotsMux_);

        if (postCondTrue) {
            LOGD("Alarm cond=true id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        }
        if (postCondFalse) {
            LOGD("Alarm cond=false id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        }
        if (postRaised) {
            LOGI("Alarm raised id=%u code=%s cond=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?", condStateStr_(cond));
        }
        if (postCleared) {
            LOGI("Alarm cleared id=%u code=%s cond=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?", condStateStr_(cond));
        }

        if (postRaised) {
            noteAlarmNotified_(id, nowMs);
            appendLog_(id, LogEvent::Raised);
            persistLatches_();
            emitAlarmEvent_(EventId::AlarmRaised, id);
        } else if (postCleared) {
            noteAlarmNotified_(id, nowMs);
            appendLog_(id, LogEvent::Cleared);
            persistLatches_();
            emitAlarmEvent_(EventId::AlarmCleared, id);
        } else if (postCondTrue || postCondFalse) {
            noteAlarmNotified_(id, nowMs);
            emitAlarmEvent_(EventId::AlarmConditionChanged, id);
        }
    }
}

void AlarmModule::loop()
{
    if (!enabled_) {
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    evaluateOnce_(millis());
    vTaskDelay(pdMS_TO_TICKS(clampEvalPeriodMs_(evalPeriodMsCfg_)));
}
