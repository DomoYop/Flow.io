# PoolDeviceModule (`moduleId: pooldev`)

## Rôle

Couche domaine actionneurs piscine:
- inventorie les 8 slots `pd0..pd7` et leur mapping I/O
- applique commandes désirées avec interlocks de dépendance
- applique une limite de runtime journalier par slot (`max_uptime_day_s`)
- synchronise état réel I/O et état désiré
- comptabilise runtime (jour/semaine/mois/total) et volumes injectés
- persiste les métriques runtime via les blobs runtime NVS du `ConfigStore`
- expose snapshots runtime pour publication MQTT

Type: module actif.

## Dépendances

- `loghub`
- `datastore`
- `cmd`
- `time`
- `io`
- `mqtt`
- `eventbus`
- `ha`

## Affinité / cadence

- core: 1
- task: `pooldev`
- loop: 200ms (avec retry init toutes 250ms tant que runtime non prêt)

## Services exposés

- `pooldev` -> `PoolDeviceService`
  - `count()`
  - `meta(slot, outMeta)`
  - `readActualOn(slot, outOn, outTsMs)`
  - `writeDesired(slot, on)`
  - `refillTank(slot, remainingMl)`

Codes retour: `POOLDEV_SVC_OK`, `...UNKNOWN_SLOT`, `...NOT_READY`, `...DISABLED`, `...INTERLOCK`, `...IO`.

## Services consommés

- `io` (`IOServiceV2`) pour lire/écrire sorties digitales
- `datastore` (`DataStoreService`) pour runtime partagé
- `eventbus` (`EventBus`) pour resets périodiques et resynchronisation
- `cmd` (`CommandService`) pour handlers commande
- `ha` (`HAService`) pour discovery capteurs/paramètres
- `config` (`ConfigStore`) via API module

## Config / NVS

Branches config (modèle 8/8):
- `moduleId = ConfigModuleId::PoolDevice`
- branches locales métier: `1..8` (`pdm/pd0..pd7`)

Clés persistantes (format):
- `pd%uen` -> `enabled`
- `pd%udp` -> `depends_on_mask`
- `pd%uflh` -> `flow_l_h`
- `pd%utc` -> `tank_cap_ml`
- `pd%uti` -> `tank_init_ml`
- `pd%umu` -> `max_uptime_day_s`
- `pd%urt` -> blob NVS runtime interne (non exposé dans l'arbre config)

Format du blob runtime persistant:
- `v1,<running_day_ms>,<running_week_ms>,<running_month_ms>,<running_total_ms>,<inj_day_ml>,<inj_week_ml>,<inj_month_ml>,<inj_total_ml>,<tank_ml>,<day_key>,<week_key>,<month_key>`

## Commandes

Enregistrées:
- `pooldevice.write`
- `pool.write` (compat legacy)
  - args: `{"slot":N,"value":true|false|0|1}`
  - applique `writeDesired` avec contrôles interlock/enable/io
- `pool.refill`
  - args: `{"slot":N,"remaining_ml":1234.0}` (`remaining_ml` optionnel)
  - met à jour niveau cuve suivi et force commit runtime/persist
- `pooldevice.uptime.reset`
- `pool.uptime.reset`
  - args: `{"slot":N}`
  - remet à zéro les compteurs jour/semaine/mois (runtime + volume injecté) du slot ciblé
- `pooldevice.uptime.reset_all`
- `pool.uptime.reset_all`
  - sans args
  - remet à zéro les compteurs jour/semaine/mois de tous les slots actifs

## EventBus

Abonnements:
- `EventId::SchedulerEventTriggered`
  - `TIME_EVENT_SYS_DAY_START` -> reset compteurs jour
  - `TIME_EVENT_SYS_WEEK_START` -> reset semaine
  - `TIME_EVENT_SYS_MONTH_START` -> reset mois
- `EventId::DataChanged`
  - sur `DATAKEY_TIME_READY` à `true` -> demande reconcile de période
- `EventId::ConfigChanged`
  - branche `moduleId=ConfigModuleId::Time` (ex: `week_start_mon`) -> reconcile de période

Publications:
- aucune publication EventBus directe

## DataStore

Écritures via `PoolDeviceRuntime.h`:
- état: `setPoolDeviceRuntimeState(ds, slot, ...)`
  - clé `DataKeys::PoolDeviceStateBase + slot` (`80..87`)
- métriques: `setPoolDeviceRuntimeMetrics(ds, slot, ...)`
  - clé `DataKeys::PoolDeviceMetricsBase + slot` (`88..95`)

Données runtime:
- state: `enabled`, `desiredOn`, `actualOn`, `blockReason`, `tsMs`
- metrics: `runningSecDay/Week/Month/Total`, `injectedMl*`, `tankRemainingMl`, `tsMs`

## Persistance runtime

Objectif:
- conserver les runtimes à travers reboot
- reset uniquement sur changement de période (jour/semaine/mois)

Mécanisme:
- chargement au boot (`onConfigLoaded`) depuis le blob runtime NVS
- reconcile de période basé horloge locale + config `time.week_start_mon`
- reset ciblé quand marqueur période (`dayKey/weekKey/monthKey`) change
- persistance conditionnelle:
  - immédiate si arrêt équipement ou reset période ou refill
  - sinon max toutes les `60s` pendant fonctionnement (`RUNTIME_PERSIST_INTERVAL_MS`)

## Scheduler / période

Le module maintient des clés:
- `dayKey = YYYYMMDD`
- `weekKey = date locale du début de semaine`
- `monthKey = YYYYMM`

`currentPeriodKeys_()` n'est valide que si:
- `timeReady == true`
- epoch >= `2021-01-01` (`MIN_VALID_EPOCH_SEC`)

Si l'heure n'est pas prête: reconcile replanifié.

## Contrôle des dépendances et limites (interlocks)

Chaque slot peut dépendre d'autres slots (`dependsOnMask`):
- au démarrage d'un slot, tous les slots dépendance doivent être `actualOn`
- si dépendance tombe, arrêt forcé + `blockReason=interlock`

Codes `blockReason` (`PoolDeviceRuntimeBlockReason`):
- `0` = `none`
- `1` = `disabled`
- `2` = `interlock`
- `3` = `io_error`
- `4` = `max_uptime`

Note diagnostic:
- dans les logs "start blocked", `reason=4` signifie que la limite `max_uptime_day_s` du slot est atteinte.

### Limite de runtime journalier (`max_uptime_day_s`)

- portée: par slot `pdm/pdN`
- unité: secondes
- `0` = illimité
- si la limite est atteinte:
  - le slot est coupé immédiatement s'il est ON
  - les demandes de démarrage sont refusées
  - `blockReason` passe à `max_uptime`
- le blocage est levé automatiquement au reset journalier (`runningMsDay`)

## Snapshots runtime (MQTT indirect)

Le module implémente `IRuntimeSnapshotProvider`:
- `runtimeSnapshotCount()` -> `2 * nb_slots_actifs`
- `runtimeSnapshotSuffix(idx)`:
  - `rt/pdm/state/pdN`
  - `rt/pdm/metrics/pdN`
- `buildRuntimeSnapshot(idx, ...)` -> JSON complet state/metrics

Publication assurée par le producteur runtime intégré à `MQTTModule` (`RuntimeProducer`), via jobs `(producerId,messageId)`.

## Publication config MQTT (`cfg/*`)

Publication autoportée via `MqttConfigRouteProducer` local au module:
- agrégat métier: `cfg/pdm`
- détail métier par slot: `cfg/pdm/pdN`

Le module garde localement:
- le mapping changement config -> `messageId`
- le mapping `messageId` -> topic relatif
- le build éventuel custom de payload

## Home Assistant

Entités créées (suffixe d'`object_id`, slot piloté) :

| Suffixe | Slot | Type | Source / commande |
|---|---|---|---|
| `pd_flt_upt_mn` | `pd0` | sensor | `rt/pdm/metrics/pd0` |
| `pd_ph_pmp_upt` | `pd1` | sensor | `rt/pdm/metrics/pd1` |
| `pd_ph_tnk_rem` | `pd1` | sensor | `rt/pdm/metrics/pd1` (`remaining_ml -> L`) |
| `pd_chl_pmp_upt` | `pd2` | sensor | `rt/pdm/metrics/pd2` |
| `pd_chl_tnk_rem` | `pd2` | sensor | `rt/pdm/metrics/pd2` (`remaining_ml -> L`) |
| `pd_chl_gen_upt` | `pd3` | sensor | `rt/pdm/metrics/pd3` |
| `pd_fill_upt_mn` | `pd5` | sensor | `rt/pdm/metrics/pd5` |
| `pd1_flow` / `pd2_flow` | `pd1` / `pd2` | number | `cfg/pdm/pdN` -> `cfg/set` |
| `pd0_max_upt` | `pd0` | number (min) | `cfg/pdm/pd0` -> `cfg/set` |
| `pd1_max_upt` / `pd2_max_upt` | `pd1` / `pd2` | number (min) | `cfg/pdm/pdN` -> `cfg/set` |
| `pd4_max_upt` | `pd5` | number (min) | `cfg/pdm/pd5` -> `cfg/set` |
| `pd5_max_upt` | `pd3` | number (min) | `cfg/pdm/pd3` -> `cfg/set` |
| `pd_refill_ph` | `pd1` | button | `{"cmd":"pool.refill","args":{"slot":1}}` |
| `pd_refill_chl` | `pd2` | button | `{"cmd":"pool.refill","args":{"slot":2}}` |
| `pd_reset_upt_flt` | `pd0` | button | `{"cmd":"pool.uptime.reset","args":{"slot":0}}` |
| `pd_reset_upt_ph` | `pd1` | button | `{"cmd":"pool.uptime.reset","args":{"slot":1}}` |
| `pd_reset_upt_chl` | `pd2` | button | `{"cmd":"pool.uptime.reset","args":{"slot":2}}` |
| `pd_reset_upt_chl_gen` | `pd3` | button | `{"cmd":"pool.uptime.reset","args":{"slot":3}}` |
| `pd_reset_upt_fill` | `pd5` | button | `{"cmd":"pool.uptime.reset","args":{"slot":5}}` |
| `pd_reset_upt_all` | — | button | `{"cmd":"pool.uptime.reset_all"}` |

Les suffixes `pd4_max_upt` et `pd5_max_upt` datent de la numérotation d'avant
[refonte-fonctions-piscine-v2](../notes/refonte-fonctions-piscine-v2.md) et ne correspondent
plus au slot qu'ils pilotent. Ils sont conservés tels quels pour ne pas recréer les entités
côté Home Assistant ; seuls les topics et les payloads ont été corrigés.

Il n'y a pas d'entité de débit pour la filtration : `flow_l_h` n'est enregistré que pour les
pompes péristaltiques, et la filtration expose `pump_flow_m3h` via l'entité PoolLogic
`pl_pump_flow`.

### Masquage selon les équipements activés

`syncHaEntityVisibility_()` marque absentes (tombstone) toutes les entités du tableau
ci-dessus dont le `pdN` est désactivé en configuration. L'appel a lieu dans
`onConfigLoaded()` et non dans `init()` : `enabled` vient de la NVS, chargée entre les deux.
Tester `slots_[i].used` ne suffit pas — ce champ vaut `true` pour les 12 fonctions déclarées
par le profil, indépendamment de la configuration utilisateur.

La reconfiguration est prise en compte au redémarrage suivant (discovery one-shot), comme
pour le masquage PoolLogic et le tombstone des switchs dans `PoolIoHaDiscovery`.

## Initialisation des slots dans le profil

Les slots sont définis via `defineDevice()` dans `configurePoolDevices()`
(`src/Profiles/Waveshare/WaveshareBootstrap.cpp`, `src/Profiles/FlowIO/FlowIOBootstrap.cpp`),
à partir des presets `PoolDomain::kPoolDevices`.

Une fonction piscine = un `PoolDevice` = la sortie logique `dNN` de **même index**. Cet
invariant est vérifié à la compilation par le `static_assert` de `PoolDomain.h` ; le type
métier vient du preset et n'est pas exposé en configuration. Le seul maillon reconfigurable
est le dernier : `pdN -> dNN -> binding_port -> relais physique`.

Table Waveshare (profil de référence). Sur FlowIO, `pd4` et `pd9..pd11` ne sont pas déclarés.

| Slot | Fonction | Sortie IO | Port d'usine | Dépend de | Particularités |
| --- | --- | --- | --- | --- | --- |
| `pd0` | filtration | `d00` | `PortExio1` | aucune | pilotée par `PoolLogic` (turnover) |
| `pd1` | régulation pH | `d01` | `PortExio2` | `pd0` | péristaltique, cuve suivie, uptime max `90 min/j` |
| `pd2` | désinfection chlore/brome | `d02` | non lié | `pd0` | péristaltique, cuve suivie, uptime max `90 min/j` |
| `pd3` | désinfection électrolyseur | `d03` | non lié | `pd0` | relais standard, uptime max `600 min/j` |
| `pd4` | désinfection oxygène actif | `d04` | non lié | `pd0` | péristaltique, cuve et compteurs séparés du chlore |
| `pd5` | remplissage automatique | `d05` | `PortExio5` | aucune | relais standard, uptime max borné |
| `pd6` | éclairage | `d06` | `PortExio7` | aucune | relais standard |
| `pd7` | chauffage | `d07` | `PortExio8` | aucune | relais standard |
| `pd8` | robot de nettoyage | `d08` | `PortExio4` | `pd0` | relais standard |
| `pd9` | recopie temporisée du débit | `d09` | non lié | aucune | sortie de report : pas de switch HA, non commandable |
| `pd10` | report d'état du volet | `d10` | non lié | aucune | sortie de report : pas de switch HA, non commandable |
| `pd11` | sortie auxiliaire 1 | `d11` | non lié | aucune | relais standard, sans automatisme |

Seuls **8 relais physiques** existent (EXIO1..EXIO8) : les fonctions non liées sont inertes,
ce qui est le comportement voulu pour les trois modes de désinfection, exclusifs entre eux.
