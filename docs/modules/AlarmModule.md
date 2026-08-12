# AlarmModule (`moduleId: alarms`)

## Rôle

Moteur d'alarmes central:
- enregistrement des définitions d'alarmes (`AlarmRegistration`)
- évaluation cyclique de conditions (`AlarmCondFn`)
- latching, acquittement, reset manuel, délais ON/OFF, snapshots
- journal circulaire des transitions et persistance des latchs
- émission d'événements de cycle de vie d'alarme

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `cmd`

`time` et `cfg` sont résolus **paresseusement** (pas de dépendance déclarée) :
`TimeModule` est enregistré après `AlarmModule` dans les profils, et une
dépendance dure réordonnerait tout le profil pour un simple horodatage.

## Affinité / cadence

- core: 1
- task: `alarms`
- période d'évaluation: config `eval_period_ms` clampée [25..5000] ms

## Services exposés

- `alarms` -> `AlarmService`
  - `registerAlarm`, `reset`, `resetAll`, `ack`, `ackAll`
  - `isActive`, `isResettable`, `isAcknowledged`, `lifecycle`, `codeOf`
  - `activeCount`, `unackedCount`, `highestSeverity`, `highestUnackedSeverity`
  - `buildSnapshot`, `listIds`, `buildAlarmState`

## Services consommés

- `eventbus` (émission événements)
- `cmd` (commandes)
- `time` (horodatage absolu, facultatif)
- `cfg` (persistance des latchs, facultatif)

## Config / NVS

Module config `alarms` (`moduleId = ConfigModuleId::Alarms`, branche locale `1`):
- `enabled`
- `eval_period_ms`

Blob runtime (hors arbre de config) :
- `al_lat` : alarmes **latchées encore actives** à la coupure, avec leur drapeau
  d'acquittement. Relu et reposé dans `onConfigLoaded`, c'est-à-dire après les
  `init()` de tous les modules, donc après l'enregistrement des alarmes.

## Commandes

- `alarms.list`
- `alarms.log` (journal, du plus récent au plus ancien, tronqué à la taille du buffer de réponse)
- `alarms.ack` (args `{id}`)
- `alarms.ack_all`
- `alarms.reset` (args `{id}`)
- `alarms.reset_all`

## Modèle d'état d'une alarme

Chaque slot garde notamment:
- `lastCond`: `False` / `True` / `Unknown`
- `active`: état d'alarme actif
- `acknowledged`: l'opérateur a vu l'occurrence active courante
- timers `onSinceMs` / `offSinceMs`, `activeSinceEpoch`

Transitions:
- `cond=True` pendant `onDelayMs` -> `active=true`, `acknowledged=false`
- `cond=False`:
  - si non latched -> clear après `offDelayMs`
  - si latched -> reste active jusqu'à `ack` ou `reset`
- `cond=Unknown` -> pas de transition (timers annulés, état conservé)

### Cycle de vie exposé (`AlarmLifecycle`)

État consolidé, seule forme publiée aux interfaces (modèle ANSI/ISA-18.2) :

| Valeur | Nom | Sens |
|---:|---|---|
| 0 | `Normal` | rien à signaler |
| 1 | `ActiveUnacked` | active, non acquittée : l'annonciation tourne |
| 2 | `ActiveAcked` | active et acquittée : visible, silencieuse |
| 3 | `ClearedUnacked` | cause disparue, latch en place : acquittement attendu |
| 4 | `Unavailable` | condition indéterminée (mesure absente ou périmée) |

Aucune interface ne doit recomposer cet état à partir de `a`/`r`/`c` : c'est ce
que faisaient les anciens masques par slot, et ils divergeaient.

## Sémantique acquittement / latch / reset

- **`ack`** est le geste unique de l'opérateur, toujours accepté sur une alarme
  active : il efface l'alarme si la condition est déjà retombée, sinon il la
  marque acquittée. C'est ce que déclenche le bouton Home Assistant par alarme.
- `latched=false`: clear automatique quand la condition retombe
- `latched=true`: clear nécessite `condition=false` puis `ack` ou `reset`
- `reset` n'est accepté que si l'alarme est encore active et que sa condition est
  déjà redevenue fausse (comportement historique, conservé pour l'API)
- `reset_all` ne touche que les alarmes latched actives actuellement réarmables ;
  `ack_all` couvre en plus les alarmes dont la cause persiste

Conséquences opérationnelles:
- une alarme latched peut rester `active=true` même si la condition est redevenue
  fausse (tant qu'elle n'est pas acquittée) — c'est voulu
- un acquittement coupe l'annonciation (buzzer) et les rappels périodiques, sans
  masquer l'état
- un acquittement survit au redémarrage : sinon la carte se remettrait à sonner
  après une coupure pour un défaut déjà vu

## EventBus

Émissions:
- `AlarmConditionChanged`
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`
- `AlarmSilenceChanged` (acquittement)

Abonnements:
- aucun

## DataStore / MQTT

Aucun accès direct au DataStore.
MQTT consomme les événements alarmes via `MQTTModule`.

Topics runtime publiés par `MQTTModule` (`MQTTProducers.cpp`) :

| Topic | Contenu | Retain |
|---|---|---|
| `rt/alarms/id<AlarmId>` | `buildAlarmState()` : `l` (cycle de vie), `a`, `r`, `k` (acquittée), `c`, `s`, `t` (epoch du déclenchement), `lc` | oui |
| `rt/alarms/m` | `a` alarmes actives, `u` non acquittées, `h` sévérité max | oui |

Les deux sont retenus parce qu'ils alimentent des entités Home Assistant : ils ne
sont republiés que sur événement d'alarme ou à la reconnexion MQTT
(`enqueueAlarmFullSync_`), donc sans retain une entité reste `unknown` après un
redémarrage de Home Assistant.

`rt/alarms/p` (champ packé 5 bits par slot) a été **supprimé** : il indexait les
alarmes par position d'enregistrement et plafonnait à 8.

## Runtime UI

Une valeur par alarme (`valueId` 11..19, soit `RuntimeUiId` 911..919), de type
`enum` affichée en badge, portant l'`AlarmLifecycle`. La table
`AlarmModule::kRuntimeUiAlarms` associe `valueId` -> `AlarmId` ; côté serveur web
Waveshare, `kWaveshareAlarmRuntimeValues` en est le miroir (le manifeste généré
n'est pas lisible depuis le serveur). **Ajouter une alarme demande de compléter
les deux tables**, sinon la valeur répond « indisponible » en silence — voir
[runtime-ui-double-chemin-waveshare.md](../notes/runtime-ui-double-chemin-waveshare.md).

## Home Assistant

Le module déclare en MQTT Discovery :
- un `binary_sensor` par alarme enregistrée (`alm_pressure_low`,
  `alm_pressure_high`, `alm_ph_tank_low`, `alm_chlorine_tank_low`,
  `alm_ph_pump_max_uptime`, `alm_chlorine_pump_max_uptime`,
  `alm_water_level_low`, `alm_ph_dose_no_effect`,
  `alm_water_temp_unavailable`), chacun lié au topic
  `rt/alarms/id<AlarmId>` de son identifiant, `device_class: problem` ;
- `alm_any`, agrégat lié à `rt/alarms/m` ;
- un bouton d'acquittement par alarme (`alm_ack_<code>`) et `alm_ack_all`.

Le suffixe de topic et le payload contiennent l'`AlarmId` en dur ; des
`static_assert` dans `AlarmModule.cpp` cassent le build si un identifiant change
sans que les tables suivent. `LogWarningSeen` (1100) et `LogErrorSeen` (1101) ne
sont volontairement pas déclarées : `LogAlarmSinkModule` est hors du
`build_src_filter` du profil Waveshare, donc ces alarmes ne sont jamais
enregistrées.

Les entités de l'ancien modèle par slot (`alm_reset_slot_0..7`, `alm_reset_all`,
`alm_pack`) sont republiées en **pierre tombale** (discovery vide) pour que Home
Assistant les retire. Elles occupent une place chacune dans les capacités
`HaCapacitySpec` et sont supprimables après une release.

Le payload d'un bouton doit être du **JSON brut** : `HAModule::publishButton`
applique déjà `jsonEscape()`. L'ancien `alm_reset_all` contenait des `\"`
littéraux, donc partait doublement échappé et n'a jamais pu déclencher sa
commande.

Côté Home Assistant, [home_assistant_alarm_mqtt_pool.yaml](../integration/home_assistant_alarm_mqtt_pool.yaml)
déclare un capteur d'état par alarme (attributs `a`/`r`/`k`/`c`/`s`/`t`), et
[home_assistant_alarm_helpers_pool.yaml](../integration/home_assistant_alarm_helpers_pool.yaml)
n'en fait plus que des alias et des dérivations : plus aucune dépendance à
l'ordre d'enregistrement des slots.

## Journal

Anneau de 24 transitions en RAM (`{epoch, uptime ms, id, event, lifecycle}`),
exposé par `alarms.log`. Événements : `Raised`, `Cleared`, `Reset`, `Acked`,
`Restored` (latch reposé au démarrage). L'epoch vaut 0 tant que l'horloge n'est
pas synchronisée ; l'uptime sert alors de repère relatif.

## Notifications et anti-spam

Les transitions utiles sont publiées immédiatement sur l'EventBus:
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`
- `AlarmSilenceChanged`
- `AlarmConditionChanged` quand la condition change réellement

`minRepeatMs` ne sert plus à retarder ces transitions. Il ne sert qu'aux rappels
périodiques `AlarmConditionChanged` tant qu'une alarme reste active, avec
`condition=true` **et non acquittée**.

## Alarmes définies actuellement

Le module est générique. Dans le projet actuel, `PoolLogicModule` enregistre
neuf alarmes :
- `AlarmId::PoolPressureLow` (1000)
- `AlarmId::PoolPressureHigh` (1001)
- `AlarmId::PoolPhTankLow` (1002)
- `AlarmId::PoolChlorineTankLow` (1003)
- `AlarmId::PoolPhPumpMaxUptime` (1004)
- `AlarmId::PoolChlorinePumpMaxUptime` (1005)
- `AlarmId::PoolWaterLevelLow` (1006)
- `AlarmId::PoolPhDoseNoEffect` (1007)
- `AlarmId::PoolWaterTemperatureUnavailable` (1008)

L'ordre d'enregistrement n'a plus d'effet observable : identifiants, libellés,
boutons et valeurs Runtime UI sont tous indexés par `AlarmId`.

Pour `PoolLogic`, quatre d'entre elles servent d'interlock sécurité
(`PoolPressureLow`, `PoolPressureHigh`, `PoolPhTankLow`,
`PoolChlorineTankLow`) :
- `PoolLogic` lit `isActive()` sur ces IDs
- si l'une est active, la filtration est forcée OFF (auto et manuel)
- l'acquittement ne lève pas l'interlock : il coupe l'annonciation, pas la sécurité
