# PoolLogicModule (`moduleId: poollogic`)

## Rôle

`PoolLogicModule` est l'orchestrateur métier principal de la piscine.  
Il:
- calcule et maintient la fenêtre quotidienne de filtration à partir de la température d'eau
- pilote les équipements via `PoolDeviceService` (filtration, pompe pH, pompe ORP/chlore liquide, robot, électrolyse, remplissage)
- applique les règles automatiques (modes, seuils, délais, sécurités)
- exécute la régulation **pH en dosage volumétrique par lots** (batch + temps de mélange) et la **désinfection liquide en PID temporel** (duty-cycle dans une fenêtre fixe)
- pilote l'oxygène actif liquide par volume calculé, sans sonde ORP, lorsque `disinfection_type=2`
- pilote un protocole de **chauffage assisté** (`heat_assist`) quand la température d'eau dépend de la filtration
- expose des snapshots runtime MQTT (`rt/poollogic/ph`, `rt/poollogic/orp`, `rt/poollogic/heat_assist`, `rt/poollogic/disinfection`)
- enregistre des commandes métier et des entités Home Assistant
- surveille la pression via `AlarmService`

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `time` (`TimeSchedulerService`)
- `io` (`IOServiceV2`)
- `pooldev` (`PoolDeviceService`)
- `ha` (`HAService`)
- `cmd` (`CommandService`)
- `alarms` (`AlarmService`)

## Affinité / cadence

- core: `1`
- task: `poollogic`
- loop: `200 ms` (`vTaskDelay(pdMS_TO_TICKS(200))`)

## Interfaces exposées

Services Core exposés:
- aucun service public direct

Interfaces runtime exposées:
- `IRuntimeSnapshotProvider`
  - `rt/poollogic/ph`
  - `rt/poollogic/orp`
  - `rt/poollogic/heat_assist`
  - `rt/poollogic/disinfection`

Ces snapshots sont routés vers MQTT via `MQTTModule::RuntimeProducer` (providers enregistrés dans le bootstrap de profil `FlowIO`), pas publiés directement par `PoolLogicModule`.

## Guide utilisateur: protocole chauffage assisté (`heat_assist`)

### Pourquoi ce protocole existe

Sur certaines installations, la température d'eau n'est fiable que si la pompe de filtration a tourné un peu.  
Le protocole `heat_assist` résout ce point: il fait d'abord un cycle court de filtration pour mesurer, puis décide si le chauffage doit réellement démarrer.

### Conditions d'activation

- `auto_mode` activé
- `heater_auto_mode` activé
- pas d'alarme pression PSI bloquante

Si une de ces conditions n'est pas remplie, `heat_assist` reste inactif.

### Fonctionnement pas à pas

1. Si la pompe est déjà en marche, le chauffage suit une hystérésis classique autour de la consigne.
2. Si la pompe est arrêtée en mode auto, le système lance une **filtration de sondage de 5 minutes**.
3. Cette phase de sondage est répétée toutes les **30 minutes** en régime normal, puis toutes les **20 minutes** après un cycle de chauffe terminé.
4. À la fin des 5 minutes, la température est lue.
5. Si l'eau est sous le seuil bas d'hystérésis, la pompe reste ON et le chauffage passe ON jusqu'au seuil haut d'hystérésis.
6. Quand le seuil haut est atteint, chauffage OFF, pompe OFF, puis reprise du cycle de sondage en cadence 5 minutes / 20 minutes.

### Exemples concrets

1. Eau froide le matin (pompe arrêtée): 08:00 sondage 5 min, 08:05 eau sous seuil bas donc chauffage ON + pompe ON, puis 10:10 seuil haut atteint donc chauffage OFF + pompe OFF. Ensuite le module passe en sondage périodique 5 min / 20 min.

2. Eau proche de la consigne (pompe arrêtée): sondage 5 min, mesure au-dessus du seuil bas, pas de chauffe, retour en attente jusqu'au prochain sondage.

3. Passage en manuel: si `auto_mode` est désactivé, `heat_assist` n'orchestre plus la chauffe automatique et la raison affichée passe à `MANUAL_MODE`.

### Lecture des motifs (`reason`) pour comprendre le comportement

- `DISABLED`: mode auto chauffage désactivé.
- `MANUAL_MODE`: mode auto global désactivé.
- `PSI_BLOCKED`: chauffage bloqué par la sécurité pression.
- `SETPOINT_INVALID`: consigne chauffage invalide.
- `TEMP_UNAVAILABLE`: température indisponible au moment de la décision.
- `PROBE_WAIT_30M`: attente avant le prochain sondage (cadence normale).
- `PROBE_WAIT_20M`: attente avant le prochain sondage (après cycle de chauffe).
- `PROBE_RUNNING`: cycle de sondage 5 min en cours.
- `HEATING`: chauffe active (pompe + chauffage).
- `IDLE_PUMP_ON`: pompe en marche mais pas de demande de chauffe.
- `SETPOINT_REACHED`: seuil haut atteint, arrêt chauffe/pompe effectué.

## Guide utilisateur: désinfection par oxygène actif liquide (`O2`)

### Principe

Le mode oxygène actif liquide est conçu pour les traitements qui ne peuvent pas être pilotés par la sonde ORP.  
Avec ce type de produit, la mesure ORP peut être fortement perturbée ou inhibée: Flow.io ne cherche donc pas à atteindre une consigne ORP et ne lance pas de PID chlore/brome.

À la place, Flow.io injecte un **volume calculé** d'oxygène actif à des jours et heures définis. Le volume dépend du bassin, du dosage produit choisi, de la température et d'un facteur manuel de charge.

### Ce qui change par rapport au chlore/brome

En mode `Chlore/Brome`, la pompe de désinfection est pilotée par le PID ORP: si l'ORP est sous la consigne, Flow.io injecte par fenêtres temporisées.

En mode `Oxygène actif`, la même sortie de pompe de désinfection est pilotée autrement:
- la sonde ORP n'est pas utilisée pour décider l'injection
- `dis_auto_mode` et les paramètres PID de désinfection liquide ne déclenchent pas la pompe
- la pompe injecte le volume O2 restant à doser (`pending_ml`)
- la filtration est demandée automatiquement quand une dose O2 est en attente

La régulation pH reste indépendante: le pH peut continuer à être régulé automatiquement si `ph_auto_mode=true`.

### Conditions pour que le protocole O2 fonctionne

Les conditions principales sont:

- `auto_mode=true`
- `disinfection_type=2` (`Oxygène actif`)
- heure système synchronisée
- pas de défaut pression PSI bloquant
- niveau de bidon désinfection OK (`chl_lvl_io_id`, réutilisé pour le bidon O2)
- débit de la pompe de désinfection configuré dans PoolDevice (`flow_l_h`)
- filtration en marche depuis au moins `min_filter_run_min`
- pompe de désinfection non bloquée par PoolDevice

PoolDevice conserve ses protections habituelles: appareil désactivé, interlock, erreur I/O et `max_uptime_day_s`. Si PoolDevice bloque la pompe, Flow.io ne force pas l'injection et affiche un blocage O2.

### Calcul de la dose

La dose hebdomadaire calculée est:

`dose_hebdo_ml = pool_volume_m3 / 10 * dose_ml_10m3_week * load_factor * facteur_temperature`

Exemple:
- bassin: `50 m3`
- dose produit: `500 ml / 10 m3 / semaine`
- facteur charge: `1.0`
- compensation température: `1.0`

Dose hebdomadaire calculée:

`50 / 10 * 500 * 1.0 * 1.0 = 2500 ml / semaine`

Si `split_count=2`, Flow.io prépare deux doses de `1250 ml`: une le lundi et une le jeudi.

### Compensation température

Si `temp_comp=true`, Flow.io ajuste la dose selon la température de l'eau:

- entre 18 °C et 24 °C: facteur `1.0`
- sous 18 °C: réduction progressive de 2% par °C, limitée à `0.75`
- au-dessus de 24 °C: augmentation progressive de 3% par °C, limitée à `1.50`

Si la température d'eau est indisponible, Flow.io utilise un facteur `1.0`. Le dosage reste donc possible, mais sans adaptation automatique à la température.

### Fractionnement dans la semaine

`split_count` indique combien d'injections sont prévues sur une semaine:

- `1`: lundi uniquement
- `2`: lundi et jeudi
- `3`: lundi, mercredi et vendredi

Le fractionnement réduit les gros volumes injectés en une seule fois et répartit mieux le traitement dans la semaine. Pour un bassin très sollicité, `2` ou `3` est généralement plus confortable qu'une dose unique.

### Heure de dosage

`main_hour` indique l'heure à partir de laquelle Flow.io peut créer la dose du jour.  
Par exemple, avec `main_hour=20`, une dose prévue le lundi ne sera créée qu'à partir de 20h00.

Si Flow.io redémarre après l'heure prévue, il peut reprendre le protocole:
- si la dose du jour n'a pas encore été faite, elle peut être créée
- si un volume était déjà en attente (`pending_ml`), il reprend ce volume
- si la dose a été terminée, `last_dose_day` empêche une deuxième injection le même jour

### Filtration avant injection

L'oxygène actif doit être injecté avec une eau en circulation.  
Quand une dose O2 est en attente, Flow.io demande donc la filtration, puis attend que la filtration soit réellement active depuis `min_filter_run_min` minutes.

Ce délai permet:
- d'éviter une injection dans une canalisation immobile
- de laisser la température et la circulation se stabiliser
- de limiter les injections juste au démarrage de la pompe

### Injection au volume

Flow.io calcule le volume injecté à partir du débit configuré dans PoolDevice:

`volume_injecte_ml = flow_l_h / 3600 * temps_ms`

Le paramètre important est donc `flow_l_h` du slot PoolDevice utilisé par la pompe de désinfection. Ce débit doit correspondre au débit réel de la pompe péristaltique. Une erreur de débit entraîne directement une erreur de volume injecté.

Exemple avec une pompe à `1.5 L/h`:
- débit = `1500 ml/h`
- soit environ `25 ml/min`
- une dose de `500 ml` demande environ `20 min` de pompe

### Paramètres à régler

Dans `poollogic/modes`:

- `disinfection_type`: choisir `Oxygène actif`
- `auto_mode`: doit être actif pour que Flow.io pilote le protocole

Dans `poollogic/o2`:

- `pool_volume_m3`: volume du bassin en m3
- `dose_ml_10m3_week`: dose hebdomadaire du produit pour 10 m3
- `main_hour`: heure principale de dosage
- `split_count`: nombre d'injections par semaine
- `temp_comp`: active l'ajustement par température
- `load_factor`: multiplicateur manuel de charge
- `min_filter_run_min`: durée minimale de filtration avant injection
- `protocol_state`, `last_dose_day`, `weekly_done_ml`, `pending_ml`: curseurs internes persistants

Dans PoolDevice, sur le slot de la pompe de désinfection:

- `flow_l_h`: débit réel de la pompe
- `tank_cap_ml` et `tank_init_ml`: suivi du volume restant dans le bidon, si utilisé
- `max_uptime_day_s`: limite journalière de sécurité de la pompe
- `enabled`: autorise ou non le pilotage de la pompe
- `depends_on_mask`: interlock matériel, généralement filtration

### Facteur de charge (`load_factor`)

`load_factor` permet d'ajuster manuellement le dosage sans changer la dose produit de référence.

Exemples:
- `1.0`: dose nominale
- `1.2`: +20% pour forte fréquentation, eau chaude ou épisode difficile
- `0.8`: -20% pour bassin peu utilisé ou eau froide

Ce réglage ne remplace pas les recommandations du produit utilisé. Il sert à adapter la stratégie Flow.io au contexte réel du bassin.

### Etats et diagnostics

Le snapshot `rt/poollogic/disinfection` expose l'état O2:

- `o2.state_s=idle`: aucune dose à faire
- `o2.state_s=pending`: dose en attente, filtration ou conditions pas encore prêtes
- `o2.state_s=dosing`: injection en cours
- `o2.state_s=blocked`: protocole bloqué

Les raisons de blocage les plus utiles sont:

- `inactive`: mode O2 non actif ou mode auto désactivé
- `time_unsynced`: heure non synchronisée
- `psi`: défaut pression
- `tank_low`: bidon désinfection bas
- `flow_invalid`: débit pompe non configuré ou invalide
- `filtration_wait`: filtration pas encore prête
- `pump_service`: service PoolDevice indisponible
- `pump_blocked`: pompe bloquée par PoolDevice (`disabled`, interlock, I/O, max uptime)
- `config`: dose impossible à calculer

Home Assistant expose aussi les diagnostics O2:
- `O2 Protocol State`
- `O2 Block Reason`
- `O2 Weekly Injected`
- `O2 Pending Volume`
- `O2 Planned Dose`
- `O2 Pump Flow`
- `O2 Last Dose Day`

### Reprise après reboot

Le protocole O2 persiste les éléments nécessaires pour reprendre correctement:

- `pending_ml`: volume restant à injecter
- `weekly_done_ml`: volume déjà injecté depuis le début de semaine
- `last_dose_day`: dernier jour où une dose a été terminée
- `protocol_state`: état courant du protocole

Après redémarrage, Flow.io ne repart donc pas aveuglément de zéro. Si une dose était en attente, elle reste en attente. Si une dose a déjà été terminée le même jour, elle n'est pas répétée.

### Points d'attention

- L'O2 n'est pas régulé par une mesure en boucle fermée: le bon réglage du volume bassin, du dosage produit et du débit pompe est essentiel.
- Le bidon O2 utilise aujourd'hui l'entrée niveau désinfection (`chl_lvl_io_id`) et les alarmes associées au bidon chlore.
- `max_uptime_day_s` n'est pas modifié automatiquement quand on passe en O2. Il doit être vérifié pour être compatible avec les volumes à injecter et le débit pompe.
- Si la dose calculée nécessite plus de temps de pompe que le `max_uptime_day_s`, PoolDevice bloquera l'injection avant la fin et O2 restera avec un volume en attente.
- Les produits O2 ont souvent des recommandations spécifiques selon la marque, la température, la présence d'activateur, la fréquentation et les UV. `dose_ml_10m3_week` doit être aligné sur le produit réellement utilisé.

## Config / NVS

Module config: `poollogic`
Identité config: `moduleId = ConfigModuleId::PoolLogic`
Branches locales utilisées:
- `1`: `modes`
- `2`: `filtration`
- `3`: `sensors`
- `4`: `safety`
- `5`: `regulation`
- `6`: `ph`
- `7`: `chlorine`
- `8`: `swg`
- `9`: `o2`
- `10`: `devices`
- `11`: `heater`
- `12`: `robot`
- `13`: `refill`

Persistance: `ConfigStore` + `NvsKeys::PoolLogic::*`

### Paramètres modes et stratégie

- `enabled`
- `auto_mode`
- `winter_mode`
- `disinfection_type`
  - `0`: `Chlore/Brome`
  - `1`: `Electrolyse`
  - `2`: `Oxygène actif`
  - `3`: `Désactivé` (aucune désinfection automatique)

### Régulation pH (`poollogic/ph`)

- `ph_auto_mode`, `ph_dose_plus`, `ph_setpoint`
- Dosage : `ph_dose_ml_m3` (gain mL/m³ par 0,1 pH), `ph_deadband`, `ph_dose_factor`,
  `ph_mix_wait_min` (0 = turnover calculé), `ph_dose_max_batch`, `ph_dose_max_day`
- Validité de la mesure : `ph_valid_min`, `ph_valid_max`, `ph_sample_max_age`
- Détection d'inefficacité : `ph_no_effect_lots`, `ph_no_effect_dph`
- Auto-calibration (masqués / diagnostic) : `ph_gain_learned`, `ph_gain_samples`, `ph_last_dose_ts`

> Les paramètres PID `ph_kp` / `ph_ki` / `ph_kd` / `ph_window_ms` / `ph_min_on_ms` /
> `ph_sample_ms` **n'existent plus** : le pH n'est plus régulé par PID temporel.

### Désinfection chlore/brome liquide (`poollogic/chlorine`)

- `dis_auto_mode`
- `dis_setpoint`
- `dis_kp`, `dis_ki`, `dis_kd`
- `dis_window_ms`

### Régulation commune (`poollogic/regulation`)

- `dly_pid_min`
- `pid_min_on_ms`
- `pid_sample_ms`

### Paramètres électrolyse (`poollogic/swg`)

- `swg_control_mode`
- `secure_elec_t`
- `dly_electro_min`

### Paramètres oxygène actif (`poollogic/o2`)

- `pool_volume_m3`
- `dose_ml_10m3_week`
- `main_hour`
- `split_count`
- `temp_comp`
- `load_factor`
- `min_filter_run_min`
- `protocol_state` (persisté pour reprise après reboot)
- `last_dose_day` (persisté pour reprise après reboot)
- `weekly_done_ml` (persisté pour reprise après reboot)
- `pending_ml` (persisté pour reprise après reboot)

La dose O2 hebdomadaire calculée est:

`pool_volume_m3 / 10 * dose_ml_10m3_week * load_factor * facteur_temperature`

Le facteur température vaut `1.0` si `temp_comp=false` ou si la température eau est indisponible. Sinon il réduit la dose sous 18 °C (minimum `0.75`) et l'augmente au-dessus de 24 °C (maximum `1.50`).

Le protocole fractionne la dose selon `split_count`:
- `1`: lundi
- `2`: lundi et jeudi
- `3`: lundi, mercredi et vendredi

À partir de `main_hour`, si une dose est due, `pending_ml` est créé et persisté. Le protocole demande ensuite la filtration, attend `min_filter_run_min` minutes de filtration effective, puis pilote la pompe de désinfection par débit PoolDevice (`flow_l_h`) jusqu'à consommer le volume en attente.

Les curseurs `protocol_state`, `last_dose_day`, `weekly_done_ml` et `pending_ml` sont persistés pour reprendre correctement après reboot. La limite `max_uptime_day_s` de la pompe reste gérée par `PoolDeviceModule`; si elle bloque la pompe, le protocole O2 passe en état bloqué sans modifier cette configuration.

### Filtration (`poollogic/filtration`, calcul quotidien)

Besoin journalier = `volume × cycles(T°) × ratio ÷ débit`, réparti dans les fenêtres priorisées — voir [filtration-turnover-fenetres.md](../notes/filtration-turnover-fenetres.md).

- `filtr_cycle_ratio` (ratio sur les cycles de renouvellement, % ; défaut 100, plage 50–200)
- `filtr_start_clc` (calculé : début du segment prioritaire)
- `filtr_stop_clc` (calculé : fin du segment prioritaire)
- `filtr_segments` (calculé, runtime : tous les créneaux « HH:MM-HH:MM, … »)
- `filtr_optimal_min` (calculé, runtime : besoin optimal en minutes, avant plafonnement par la capacité des fenêtres)

Le débit `pump_flow_m3h` est enregistré ici mais publié sur `pdm/pd0` (caractéristique de la pompe) ; le volume `pool_volume_m3` vit dans `poollogic/bassin` et est partagé avec le dosage O2.

### Fenêtres de filtration (`poollogic/filtration/fenetres`)

Pour `{i}` de 1 à 3 : `filtr_w{i}_en`, `filtr_w{i}_start`, `filtr_w{i}_stop` (minutes depuis minuit ; `stop < start` traverse minuit), `filtr_w{i}_prio` (1 = remplie en premier).

### Bindings capteurs IO (`poollogic/sensors`)

- `ph_io_id`
- `dis_io_id`
- `pressure_io_id`
- `wat_temp_io_id`
- `air_temp_io_id`
- `pool_lvl_io_id`
- `ph_lvl_io_id`
- `chl_lvl_io_id`
- `flow_io_id` (entrée digitale flowswitch ; défaut = rôle domaine `SensorFlowSwitch`)
- `cover_io_id` (entrée digitale contact volet fermé ; défaut = rôle domaine `SensorCoverClosed`)

### Sécurités (`poollogic/safety`)

- `psi_low_th`
- `psi_high_th`
- `psi_start_dly_s`
- `winter_start_t`
- `freeze_hold_t`

### Robot (`poollogic/robot`)

- `robot_delay_min`
- `robot_dur_min`

### Remplissage (`poollogic/refill`)

- `fill_min_on_s`

### Chauffage (`poollogic/heater`)

- `heater_auto_mode`
- `heater_setpoint`

> Note : les 7 variables d'aiguillage rôle → slot (`filtr_slot`, `ph_pump_slot`,
> `dis_pump_slot`, `swg_slot`, `robot_slot`, `fill_slot`, `heater_slot`) et leurs clés NVS
> `pl_s*` ont été **supprimées**. Une fonction piscine est liée à un `PoolDevice` unique par
> `PoolIds::Device*` ; ce que l'utilisateur choisit est le relais physique, depuis la page
> `piscine/<fonction>`. Seul le slot de la pompe de désinfection reste dérivé, de
> `disinfection_type` (chlore/brome ou oxygène actif). Voir
> [refonte-fonctions-piscine-v2.md](../notes/refonte-fonctions-piscine-v2.md).

## Commandes

Enregistrées via `CommandService`:

Transport MQTT (`<base>/<device>/cmd`) :
- payload attendu: `{"cmd":"<nom_commande>","args":{...}}`

Commandes modes:
- `poollogic.auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `auto_mode`
- `poollogic.auto_mode.toggle`
  - inverse `auto_mode`
- `poollogic.ph_auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `ph_auto_mode`
- `poollogic.ph_auto_mode.toggle`
  - inverse `ph_auto_mode`
- `poollogic.dis_auto_mode.set` / alias `poollogic.orp_auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `dis_auto_mode`
- `poollogic.dis_auto_mode.toggle` / alias `poollogic.orp_auto_mode.toggle`
  - inverse `dis_auto_mode`
- `poollogic.winter_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `winter_mode`
- `poollogic.winter_mode.toggle`
  - inverse `winter_mode`

Commandes actionneurs:
- `poollogic.filtration.write`
  - args: `{"value":true|false}`
  - force `auto_mode=false`
  - écrit l'état désiré du slot filtration via `pooldev.writeDesired`
- `poollogic.filtration.toggle`
  - inverse l'état réel filtration
  - force `auto_mode=false`
- `poollogic.ph_pump.write`
  - args: `{"value":true|false}`
  - écrit la consigne de pompe pH
  - si `value=true`, force `ph_auto_mode=false` (aligné avec `pooldevice.write`)
- `poollogic.ph_pump.toggle`
  - inverse l'état réel pompe pH
  - si passage ON, force `ph_auto_mode=false`
- `poollogic.dis_pump.write` / alias `poollogic.orp_pump.write`
  - args: `{"value":true|false}`
  - écrit la consigne de pompe ORP/chlore liquide
  - si `value=true`, force `disinfection_type=3` (`Désactivé`, aligné avec `pooldevice.write`)
- `poollogic.dis_pump.toggle` / alias `poollogic.orp_pump.toggle`
  - inverse l'état réel pompe ORP/chlore liquide
  - si passage ON, force `disinfection_type=3` (`Désactivé`)
- `poollogic.light.write` / `poollogic.lights.write`
  - args: `{"value":true|false}`
  - écrit la consigne éclairage
- `poollogic.light.toggle` / `poollogic.lights.toggle`
  - inverse l'état réel éclairage
- `poollogic.robot.write`
  - args: `{"value":true|false}`
  - écrit la consigne robot
- `poollogic.robot.toggle`
  - inverse l'état réel robot
- `poollogic.heater.write`
  - args: `{"value":true|false}`
  - écrit la consigne chauffage
- `poollogic.heater.toggle`
  - inverse l'état réel chauffage
- `poollogic.chlorine_generator.write` / `poollogic.swg.write`
  - args: `{"value":true|false}`
  - écrit la consigne électrolyseur (SWG)
- `poollogic.chlorine_generator.toggle` / `poollogic.swg.toggle`
  - inverse l'état réel électrolyseur (SWG)

Commande utilitaire:
- `poollogic.filtration.recalc`
  - met en file une recomputation de la fenêtre
  - traitement asynchrone dans la loop

Les réponses d'erreur suivent `ErrorCode` (`MissingArgs`, `MissingValue`, `NotReady`, `Disabled`, `InterlockBlocked`, etc.).

## EventBus et Scheduler

### Abonnements EventBus

- `EventId::SchedulerEventTriggered`
  - `POOLLOGIC_EVENT_DAILY_RECALC` + `Trigger` -> queue recalc de filtration
  - `TIME_EVENT_SYS_DAY_START` + `Trigger` -> reset quotidien interne (`cleaningDone_ = false`)
  - `POOLLOGIC_EVENT_FILTRATION_WINDOW` + `Start/Stop` -> mise à jour `filtrationWindowActive_`

### Slots scheduler gérés

- slot `3` (`POOLLOGIC_EVENT_DAILY_RECALC`)
  - rappel quotidien à 15:00
- slot `4` (`POOLLOGIC_EVENT_FILTRATION_WINDOW`)
  - fenêtre start/stop calculée dynamiquement
  - `replayStartOnBoot=true` pour reconstruire l'état après reboot

## Algorithme de contrôle

### Entrées runtime

- capteurs analogiques: pH, ORP, PSI, température eau, température air
- capteur digital: niveau bassin
- états actionneurs: lecture `pooldev.readActualOn(...)`
- états alarmes PSI: via `alarmSvc->isActive(...)`

Convention logique des capteurs digitaux de niveau (règle harmonisée):
- `true` (`ON`) = problème détecté (niveau bas / défaut)
- `false` (`OFF`) = état normal (pas de problème)
- cette convention est appliquée à `pool_lvl_io_id`, `ph_lvl_io_id`, `chl_lvl_io_id`

### Priorité des règles (filtration)

Ordre de décision appliqué à chaque cycle (`200 ms`):
1. état réel et capteurs relus (`syncDeviceState_`, IO analog/digital)
2. statut sécurité PSI recalculé (`psiError_`)
3. si `psiError_==true` -> **filtration forcée OFF**, même en manuel
4. sinon:
   - mode manuel (`auto_mode=false`): consigne manuelle conservée
   - mode auto: décision fenêtre scheduler / hiver / freeze-hold
5. actionnement via `PoolDeviceService::writeDesired` (avec interlocks `PoolDeviceModule`)

### Pilotage filtration / robot / SWG / remplissage

Logique principale:
- filtration:
  - sécurité PSI prioritaire: coupe sur erreur PSI (auto **et** manuel)
  - en auto: suit fenêtre scheduler, mode hiver et freeze-hold
  - en manuel (`auto_mode=false`): `PoolLogic` n'impose pas de demande auto hors sécurité PSI
- robot:
  - démarre après `robot_delay_min` de filtration
  - s'arrête après `robot_dur_min`
  - un cycle/jour (`cleaningDone_`)
- électrolyse:
  - nécessite filtration active, température mini (`secure_elec_t`) et délai (`dly_electro_min`)
  - active uniquement si `disinfection_type == 1`
  - en mode `swg_control_mode == 0`, asservissement ORP avec hystérésis implicite (`<= 100%` pour maintenir, `<= 90%` pour démarrer)
  - en mode `swg_control_mode == 1`, marche continue pendant la filtration autorisée
- remplissage:
  - démarre si `Pool Level` est actif (`pool_lvl_io_id == true`)
  - respecte un minimum de marche `fill_min_on_s`

### Alarmes pression PSI

- `AlarmId::PoolPsiLow`
  - latched
  - délai ON `2000 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition active seulement si filtration ON et `runSec > psi_start_dly_s`
- `AlarmId::PoolPsiHigh`
  - latched
  - sévérité critique
  - délai ON `0 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition active seulement si filtration ON

### Alarmes niveau cuves dosage

- `AlarmId::PoolWaterLevelLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `pool_lvl_io_id == true`
- `AlarmId::PoolPhTankLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `ph_lvl_io_id == true`
- `AlarmId::PoolChlorineTankLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `chl_lvl_io_id == true`

### Réarmement PSI

- source de vérité en nominal: `alarmSvc->isActive(PoolPsiLow|PoolPsiHigh)`
- tant qu'une alarme PSI latched reste `active`, `psiError_` reste vrai et la filtration est bloquée
- si la condition est redevenue fausse, un `reset` manuel est alors autorisé pour clear l'alarme
- si la filtration est redémarrée alors que la pression reste anormale:
  - `psi_high` peut reraiser immédiatement
  - `psi_low` reraisera après `psi_start_dly_s` (délai de démarrage)

### Mode dégradé sans `AlarmService`

Si le service alarmes est indisponible, `PoolLogic` applique un latch local PSI minimal:
- détection locale `psi < low` (après délai de démarrage) ou `psi > high`
- `psiError_` passe à vrai
- pas de clear automatique local (mode dégradé conservatif)

## Régulation pH : dosage volumétrique par lots

Le dosage d'un bassin est un procédé à **grand retard pur** : une dose n'est visible par la sonde
qu'après un brassage complet (1 turnover = 3 à 8 h). Un régulateur proportionnel dont la fenêtre est
plus courte que ce retard redose plusieurs fois avant de voir l'effet de la première dose, donc
surdose structurellement. Le pH utilise donc un contrôleur par lots : calculer un **volume**,
l'injecter, puis **attendre** avant de re-mesurer.

La logique est isolée dans le helper pur
[DosingController.h](../../src/Modules/PoolLogicModule/DosingController.h) (aucune dépendance
Arduino/ESP, aucune horloge interne), testé en natif par `test/test_poollogic_dosing_controller`.
`PoolLogicModule` ne fait que remplir la structure d'entrée et appliquer la sortie.

### Machine à états

| Phase | Pompe | Sortie |
|---|---|---|
| `idle` | OFF | régulation non armée |
| `measure` | OFF | attend une mesure valide et fraîche, puis calcule la dose du lot |
| `dosing` | **ON** | injecte jusqu'au millilitre visé |
| `mixing` | OFF | **rien ne se décide** ; un écart même important ne redémarre pas la pompe |
| `evaluate` | OFF | compare le ΔpH obtenu à la dose injectée, recale le gain |
| `blocked` | OFF | refus device, interlock, bidon, quota, ou dosage sans effet |

L'armement reprend la temporisation historique : filtration en marche depuis `dly_pid_min`, hors mode
hiver, `ph_auto_mode` actif. Les autres conditions (mesure disponible, pression, niveau de bidon,
interlock débit) sont des **entrées du contrôleur** et produisent une phase et une raison observables
plutôt qu'un arrêt silencieux.

### Calcul de la dose

```
effectif = |écart| - ph_deadband
dose_ml  = gain × volume_m3 × (effectif / 0,1) × ph_dose_factor
plafonds : ph_dose_max_batch, (ph_dose_max_day - injecté du jour), niveau du bidon
plancher : 10 s de pompe (granularité) — en dessous, aucune dose
```

`ph_dose_factor` = 0,5 vise la moitié de l'écart par lot : combiné à l'attente de mélange, la
convergence est géométrique et **sans dépassement** — ce qu'un intégrateur ne sait pas produire sur un
procédé à retard pur.

Exemple (50 m³, gain 10, pompe 1,8 L/h, plafond 250 mL) : écart 0,10 pH → 125 mL ≈ 4 min de pompe ;
écart 0,20 pH → 250 mL (plafonné) ≈ 8 min, suivies de 5 h de mélange.

### Temps de mélange

```
mix_wait = clamp(max(ph_mix_wait_min, volume_m3 / débit_filtration_m3h), 10 min, 8 h)
```

`ph_mix_wait_min = 0` donne le turnover pur ; une valeur non nulle sert de **plancher**. Le décompte
n'avance **que si la filtration tourne réellement** : sinon un arrêt ferait « passer » le turnover
sans qu'un seul m³ ait été brassé, et le lot suivant serait décidé sur une eau non homogène.

### Auto-calibration du gain

À la fin de chaque cycle :

```
gain_observé = dose_injectée_ml / (ΔpH × volume_m3 / 0,1)
```

L'échantillon est rejeté si la dose est inférieure à 50 mL, si le ΔpH est sous le bruit de sonde, ou
s'il est **du mauvais signe** (une perturbation — orage, fréquentation — fausserait la calibration).
Les échantillons retenus alimentent une moyenne glissante bornée à ±50 % de `ph_dose_ml_m3`, sur une
fenêtre de 8 lots. Le résultat est persisté dans `ph_gain_learned`, remis à zéro si `ph_dose_ml_m3` ou
`pool_volume_m3` change.

Un lot dont la dose est significative mais sans correction mesurable incrémente un compteur ; après
`ph_no_effect_lots` lots consécutifs, l'alarme **`ph_dose_no_effect`** (`AlarmId` 1007, latchée) est
levée et la FSM passe en `blocked`. Le latch est levé par le réapprovisionnement du bidon (front
descendant du niveau bas) ou une bascule de `ph_auto_mode`.

### Reprise après redémarrage

La FSM n'est **pas** persistée : `millis()` repart à zéro, et un blocage persisté serait plus dangereux
qu'un redémarrage en mesure. Ce qui doit survivre survit ailleurs — `injectedMlDay` et le niveau de
bidon sont dans le blob `pdNrt` de `PoolDeviceModule`, donc un reboot ne contourne ni le quota
journalier ni le bidon vide.

Seule exception : `ph_last_dose_ts` (epoch, masqué), écrit à chaque fin de lot. Au premier armement
après un boot, si l'horloge est synchronisée et que le turnover n'est pas écoulé, la FSM redémarre
directement en `mixing` avec le temps déjà passé. Sans cela, un reboot juste après un lot ferait
redoser sur une eau non brassée.

### Sécurités

- **Bande morte** `ph_deadband` : jamais de dosage sur le bruit de sonde.
- **Plage de validité** `ph_valid_min` / `ph_valid_max` : hors plage, la sonde est suspecte et le
  dosage est **interdit**, jamais maximal.
- **Fraîcheur** `ph_sample_max_age` : une mesure périmée suspend le dosage.
- **Quota volumétrique** `ph_dose_max_day`, adossé au compteur persistant de la pompe.
- **Non-simultanéité acide / chlore liquide** : leur mélange dégage du chlore gazeux ; priorité au pH,
  dont dépend l'efficacité du chlore.
- `max_uptime_day_s` du slot PoolDevice reste une sécurité de dernier recours (défaut 90 min).

## Régulation PID temporelle (désinfection liquide)

La désinfection liquide conserve un PID temporel (`TemporalPidState`), inchangé.

### Activation

Le mode PID est autorisé seulement si:
- filtration en marche
- pas de mode hiver
- délai de stabilisation atteint (`dly_pid_min`)
- mode auto de la boucle activé (`dis_auto_mode`)

Le calcul et la commande sont ensuite conditionnés par:
- capteur disponible (`dis_io_id`)
- pas de défaut pression latched (`pressureError_==false`)
- pas de niveau bas cuve actif (`chlorineTankLowError_==false`)
- `disinfection_type == 1` (en mode électrolyse ou oxygène actif, la régulation ORP liquide est inhibée)

### Convention d'erreur

- `error = dis_setpoint - orp_input`
- injection chlore liquide seulement si `error > 0`

### Calcul périodique

À chaque `dis_sample_ms` (par défaut `30000 ms`):
- intégrale:
  - accumulée si `Ki != 0`
  - remise à zéro si `Ki == 0` ou `error <= 0`
- dérivée:
  - `dE/dt` entre l'échantillon précédent et courant
- sortie:
  - `u = Kp*e + Ki*I + Kd*D`
  - clampée sur `[0, window_ms]`
  - filtrée par seuil minimal: si `u < dis_min_on_ms`, alors `0`

### Fenêtre temporelle (PWM temporel)

La boucle a une fenêtre cyclique fixe (`dis_window_ms`):
- la fenêtre est avancée par pas de `window_ms`
- `output_on_ms` est **figé pour toute la fenêtre en cours** : une consigne recalculée en cours de
  route n'entre en vigueur qu'au début de la fenêtre suivante, sinon la pompe pourrait se rallumer en
  milieu de fenêtre après s'être arrêtée
- demande pompe:
  - ON si `elapsed_in_window < output_on_ms`
  - OFF sinon

### Reset état PID

Si les conditions d'autorisation ne sont plus remplies:
- sortie forcée à `0`
- demande OFF
- reset de l'état interne (fenêtre, intégrale, erreur, échantillons)

### Actionnement

Le module n'écrit pas directement les GPIO.  
Il passe par `PoolDeviceService::writeDesired` sur:
- slot pH (`POOL_IO_SLOT_PH_PUMP`)
- slot ORP/chlore liquide (`POOL_IO_SLOT_CHLORINE_PUMP`)

Les interlocks `PoolDeviceModule` restent appliqués.

Codes de blocage renvoyés par `PoolDevice` (`blockReason`):
- `0` = `none`
- `1` = `disabled`
- `2` = `interlock`
- `3` = `io_error`
- `4` = `max_uptime` (limite journalière `max_uptime_day_s` atteinte)

## Runtime MQTT (`rt/poollogic/*`)

Snapshots publiés (via `RuntimeProducer` du `MQTTModule`):
- `rt/poollogic/ph`
- `rt/poollogic/orp`
- `rt/poollogic/heat_assist`
- `rt/poollogic/disinfection`

Payload (champs principaux):
- `id`: `ph` ou `orp`
- `input`: valeur **échantillonnée lors du dernier compute PID** (pas la mesure live instantanée)
- `setpoint`: setpoint utilisé lors du dernier compute
- `error`: erreur utilisée lors du dernier compute
- `compute_ts`: timestamp du dernier compute PID
- `enabled`: boucle autorisée côté logique
- `demand`: demande ON/OFF calculée dans la fenêtre
- `actual`: état ON/OFF réel lu sur le device
- `kp`, `ki`, `kd`
- `window_ms`, `sample_ms`, `min_on_ms`
- `output_on_ms`
- `window_elapsed_ms`
- `disinfection_type`
- `swg_control_mode`
- `ts`: timestamp snapshot

Sémantique importante:
- `input/setpoint/error` sont des valeurs latched au compute PID
- `rt/io/input/*` reste la source des mesures live brutes

### Snapshot `rt/poollogic/heat_assist` (lecture orientée usage)

- `en`: protocole autorisé (`auto_mode` + `heater_auto_mode`)
- `pr`: sondage 5 min en cours
- `ha`: chauffe active
- `fc`: cadence rapide active (5 min / 20 min)
- `ri`: motif courant brut (code interne)
- `ivm`: intervalle d'attente courant en minutes (20 ou 30)
- `prm`: temps restant du sondage courant (ms)
- `irm`: temps restant avant prochain sondage (ms)

### Snapshot `rt/poollogic/disinfection`

- `dt`: type numérique (`0` chlore/brome, `1` électrolyse, `2` oxygène actif)
- `dts`: libellé machine du type
- `swgm`: mode SWG numérique (`0` suivi ORP, `1` continu filtration)
- `swgms`: libellé machine du mode SWG
- `o2.state`, `o2.state_s`: état protocole O2 (`idle`, `pending`, `dosing`, `blocked`)
- `o2.block`, `o2.block_s`: raison de blocage O2 (`inactive`, `time_unsynced`, `psi`, `tank_low`, `flow_invalid`, `filtration_wait`, `pump_service`, `pump_blocked`, `config`)
- `o2.last_day`, `o2.done_ml`, `o2.pending_ml`: curseur O2 persistant
- `o2.plan_ml`, `o2.flow_l_h`: dose élémentaire calculée et débit pompe utilisé

## Config MQTT (`cfg/poollogic*`)

Publication autoportée via `MqttConfigRouteProducer` local:
- `cfg/poollogic` (agrégat base)
- `cfg/poollogic/modes`
- `cfg/poollogic/filtration`
- `cfg/poollogic/sensors`
- `cfg/poollogic/safety`
- `cfg/poollogic/regulation`
- `cfg/poollogic/ph`
- `cfg/poollogic/chlorine`
- `cfg/poollogic/swg`
- `cfg/poollogic/o2`
- `cfg/poollogic/heater`
- `cfg/poollogic/robot`
- `cfg/poollogic/refill`

Le mapping `ConfigChanged -> messageId -> topic` est local au module.

## Home Assistant

Entités enregistrées par `PoolLogicModule`:
- switches:
  - `pool_auto_mode` (`Pool Auto-regulation`)
  - `pool_winter_mode`
  - `pool_ph_auto_mode` (`pH Auto-regulation`)
  - `pool_dis_auto_mode` (`Disinfection Auto-regulation`)
- sensors:
  - `calculated_filtration_start`
  - `calculated_filtration_stop`
  - `heat_assist_status`
- numbers (section configuration):
  - `dly_pid_min`
  - `ph_setpoint`
  - `dis_setpoint`
  - `ph_pid_window_min` (conversion vers `ph_window_ms`)
  - `dis_pid_window_min` (conversion vers `dis_window_ms`)
  - `psi_low_threshold`
  - `psi_high_threshold`
- button:
  - `filtration_recalc` -> `{"cmd":"poollogic.filtration.recalc"}`

## DataStore / EventStore

- pas d'écriture directe `DataStore` par `PoolLogicModule`
- interactions runtime via services (`IOServiceV2`, `PoolDeviceService`, `AlarmService`)
- pas d'`EventStore` persistant côté `PoolLogic` (événements runtime via `EventBus`, config persistante via `ConfigStore`)
