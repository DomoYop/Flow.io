# AlarmModule (`moduleId: alarms`)

## Rôle

Moteur d'alarmes central:
- enregistrement des définitions d'alarmes (`AlarmRegistration`)
- évaluation cyclique de conditions (`AlarmCondFn`)
- latching, reset manuel, délais ON/OFF, snapshots
- émission d'événements de cycle de vie d'alarme

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `cmd`

## Affinité / cadence

- core: 1
- task: `alarms`
- période d'évaluation: config `eval_period_ms` clampée [25..5000] ms

## Services exposés

- `alarms` -> `AlarmService`
  - `registerAlarm`, `reset`, `resetAll`
  - `isActive`, `isResettable`, `activeCount`, `highestSeverity`
  - `buildSnapshot`, `listIds`, `buildAlarmState`, `buildPacked`

## Services consommés

- `eventbus` (émission événements)
- `cmd` (commandes)

## Config / NVS

Module config `alarms` (`moduleId = ConfigModuleId::Alarms`, branche locale `1`):
- `enabled`
- `eval_period_ms`

## Commandes

- `alarms.list`
- `alarms.reset` (args `{id}`)
- `alarms.reset_slot` (args `{slot}` -> résolution `slot -> id`)
- `alarms.reset_all`

## Modèle d'état d'une alarme

Chaque slot garde notamment:
- `lastCond`: `False` / `True` / `Unknown`
- `active`: état d'alarme actif
- timers `onSinceMs` / `offSinceMs`

Transitions:
- `cond=True` pendant `onDelayMs` -> `active=true`
- `cond=False`:
  - si non latched -> clear après `offDelayMs`
  - si latched -> reste active jusqu'à commande `reset`
- `cond=Unknown` -> pas de transition (timers annulés, état conservé)

## Sémantique latch / reset

- `latched=false`: clear automatique quand la condition retombe
- `latched=true`: clear nécessite `condition=false` puis une commande `reset`
- `reset` n'est accepté que si l'alarme est encore active et que sa condition est déjà redevenue fausse
- `reset_all` ne touche que les alarmes latched actives actuellement réarmables

Conséquence opérationnelle importante:
- une alarme latched peut rester `active=true` même si la condition est redevenue fausse (tant qu'elle n'est pas reset)
- c'est voulu pour garder la trace d'un défaut de sécurité jusqu'à réarmement manuel

## EventBus

Émissions:
- `AlarmConditionChanged`
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`

Abonnements:
- aucun

## DataStore / MQTT

Aucun accès direct au DataStore.
MQTT consomme les événements alarmes via `MQTTModule`.

Topics runtime publiés par `MQTTModule` (`MQTTProducers.cpp`) :

| Topic | Contenu | Retain |
|---|---|---|
| `rt/alarms/id<AlarmId>` | `buildAlarmState()` : `a` (actif), `r` (réarmable), `c` (condition), `s` (sévérité) | oui |
| `rt/alarms/m` | nombre d'alarmes actives et sévérité la plus haute | oui |
| `rt/alarms/p` | `buildPacked()` : 5 bits par slot, 8 premiers slots | non |

Les deux premiers sont retenus parce qu'ils alimentent des entités Home
Assistant : ils ne sont republiés que sur événement d'alarme ou à la
reconnexion MQTT (`enqueueAlarmFullSync_`), donc sans retain une entité reste
`unknown` après un redémarrage de Home Assistant.

## Home Assistant

Le module déclare en MQTT Discovery :
- un `binary_sensor` par alarme enregistrée (`alm_pressure_low`,
  `alm_pressure_high`, `alm_ph_tank_low`, `alm_chlorine_tank_low`,
  `alm_ph_pump_max_uptime`, `alm_chlorine_pump_max_uptime`,
  `alm_water_level_low`, `alm_ph_dose_no_effect`,
  `alm_water_temp_unavailable`), chacun lié au topic
  `rt/alarms/id<AlarmId>` de son identifiant, `device_class: problem` ;
- `alm_any`, agrégat lié à `rt/alarms/m` ;
- `alm_pack`, capteur du champ packé ;
- `alm_reset_all` et un bouton `alm_reset_slot_<0..7>`.

Le suffixe de topic contient l'`AlarmId` en dur ; des `static_assert` dans
`AlarmModule.cpp` cassent le build si un identifiant change sans que la table
suive. `LogWarningSeen` (1100) et `LogErrorSeen` (1101) ne sont volontairement
pas déclarées : `LogAlarmSinkModule` est hors du `build_src_filter` du profil
Waveshare, donc ces alarmes ne sont jamais enregistrées.

Côté Home Assistant, [home_assistant_alarm_helpers_fio53.yaml](../integration/home_assistant_alarm_helpers_fio53.yaml)
n'aliase plus que ces entités pour l'état actif ; seuls les aspects `réarmable`
et `condition`, non exposés en discovery, restent décodés depuis `alm_pack` et
dépendent donc de l'ordre d'enregistrement des slots.

## Notifications et anti-spam

Les transitions utiles sont publiées immédiatement sur l'EventBus:
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`
- `AlarmConditionChanged` quand la condition change réellement

`minRepeatMs` ne sert plus à retarder ces transitions. Il ne sert qu'aux rappels
périodiques `AlarmConditionChanged` tant qu'une alarme reste active avec
`condition=true`.

## Alarmes définies actuellement

Le module est générique. Dans le projet actuel, `PoolLogicModule` enregistre
huit alarmes, dans cet ordre (qui fixe l'ordre des slots) :
- `AlarmId::PoolPressureLow` (1000)
- `AlarmId::PoolPressureHigh` (1001)
- `AlarmId::PoolPhTankLow` (1002)
- `AlarmId::PoolChlorineTankLow` (1003)
- `AlarmId::PoolPhPumpMaxUptime` (1004)
- `AlarmId::PoolChlorinePumpMaxUptime` (1005)
- `AlarmId::PoolWaterLevelLow` (1006)
- `AlarmId::PoolPhDoseNoEffect` (1007)
- `AlarmId::PoolWaterTemperatureUnavailable` (1008)

Attention : `buildPacked()` ne couvre que les **8 premiers slots**, et les boutons
Home Assistant `alm_reset_slot_*` vont de 0 à 7. Une neuvième alarme est donc
absente du champ packé et n'a pas de bouton de reset dédié ; elle ne doit pas
être latched, sinon elle ne serait acquittable que par `alarms.reset` avec son
identifiant.

Pour `PoolLogic`, quatre d'entre elles servent d'interlock sécurité
(`PoolPressureLow`, `PoolPressureHigh`, `PoolPhTankLow`,
`PoolChlorineTankLow`) :
- `PoolLogic` lit `isActive()` sur ces IDs
- si l'une est active, la filtration est forcée OFF (auto et manuel)
