# Rapport d'analyse — Domaine piscine

> Analyse du code des modules du domaine piscine (`Domain/Pool`, `PoolLogicModule`,
> `PoolDeviceModule`, alarmes pool, bindings IO) — juin 2026.
> Périmètre volontairement limité au domaine piscine ; les autres domaines
> (Micronova, Supervisor, FlowConnectDisplay) sont hors champ.

---

## 1. Synthèse

L'architecture du domaine piscine est **saine et au-dessus de la moyenne de ce
qu'on trouve dans les firmwares domotiques amateurs** : séparation nette en
couches (déclaration de domaine → couche actionneurs → orchestration métier),
sécurité prise au sérieux (interlocks réévalués en continu, gel des écritures au
boot, arbitrage « sécurité d'abord »), persistance prudente des compteurs, et un
début de stratégie de test sur la partie extraite en logique pure.

Le point faible principal est concentré en un seul endroit : **la boucle de
contrôle `runControlLoop_()` (~500 lignes)** qui mélange sept arbitrages
d'équipements dans un seul flux impératif, sans test. Tout le reste du rapport
découle plus ou moins de ce constat : la logique la plus critique du système est
celle qui est la moins testable.

| Axe | Évaluation |
|---|---|
| Architecture en couches | Très bonne |
| Sécurité / interlocks | Bonne, quelques cas limites |
| Lisibilité de la logique métier | Moyenne (boucle de contrôle monolithique) |
| Testabilité / couverture | Faible (1 seul test unitaire) |
| Persistance / robustesse reboot | Bonne |
| Couplages inter-modules | Bonne dans l'ensemble, 2 couplages fragiles par chaînes |

---

## 2. Architecture observée

```
                    ┌────────────────────────────────────────────┐
                    │  Domain/Pool (déclaratif, constexpr)        │
                    │  PoolDomain.h   rôles ↔ signaux carte       │
                    │  PoolBindings.h slots ↔ IoId ↔ noms         │
                    │  PoolDefaults.h constantes métier           │
                    └──────────────────┬─────────────────────────┘
                                       │ presets
   EventBus ◄──────────┐               ▼
   TimeScheduler ──────┤   ┌───────────────────────────┐
                       ├──►│ PoolLogicModule (cerveau)  │  tâche 200 ms, core 1
   AlarmModule ◄───────┤   │ - fenêtre filtration       │
   (7 alarmes pool)    │   │ - PID temporels pH/ORP     │
                       │   │ - heat-assist, SWG, O2     │
                       │   │ - arbitrage 7 équipements  │
                       │   └────────────┬──────────────┘
                       │                │ PoolDeviceService (desired/actual/meta)
                       │                ▼
                       │   ┌───────────────────────────┐
                       └──►│ PoolDeviceModule (muscles) │  tâche dédiée
                           │ - interlocks dependsOnMask │
                           │ - max uptime / jour        │
                           │ - compteurs j/sem/mois     │
                           │ - réservoirs (ml injectés) │
                           └────────────┬──────────────┘
                                        │ IOServiceV2 (IoId)
                                        ▼
                           ┌───────────────────────────┐
                           │ IOModule (E/S physiques)   │
                           └───────────────────────────┘
```

Points structurants vérifiés dans le code :

- **Le déclaratif est bien séparé** : [PoolDomain.h](../src/Domain/Pool/PoolDomain.h)
  ne contient que des tables `constexpr` (rôles, presets d'équipements, capteurs) ;
  changer le câblage ou les défauts ne touche pas la logique.
- **PoolDeviceModule est la seule porte vers les relais pool** : PoolLogic ne
  parle jamais à IOModule pour les sorties, uniquement via `PoolDeviceService`
  (`writeDesired`/`readActualOn`/`meta`). Les interlocks ne sont donc **pas
  contournables** par le HMI ou MQTT, qui passent par le même service.
- **Modèle « desired vs actual »** : PoolLogic exprime une intention,
  PoolDevice l'applique si les garde-fous le permettent, et PoolLogic resynchronise
  son FSM sur l'état réellement observé (`syncDeviceState_`). C'est le bon modèle.
- **Boot sûr** : les écritures actionneurs sont gelées (`writesEnabled_ = false`)
  jusqu'à ce que PoolLogic ait « adopté » l'état physique courant
  (`adoptBootDeviceState_`), ce qui évite un claquement de relais au reboot chaud.
- **La fenêtre de filtration est un helper pur** ([FiltrationWindow.cpp](../src/Modules/PoolLogicModule/FiltrationWindow.cpp))
  testé unitairement ([test_main.cpp](../test/test_poollogic_filtration_window/test_main.cpp)) —
  c'est exactement le pattern à généraliser.

---

## 3. Forces

1. **Hiérarchie de sécurité explicite et correcte** dans l'arbitrage filtration
   ([PoolLogicControl.cpp:910](../src/Modules/PoolLogicModule/PoolLogicControl.cpp)) :
   pression > mode manuel > maintien hors-gel > planning. Le hors-gel ne s'applique
   qu'au *maintien* (« une fois démarrée, ne jamais s'arrêter sous le seuil »),
   ce qui est physiquement le bon choix.
2. **Interlocks réévalués à chaque tick** côté PoolDevice
   ([PoolDeviceControl.cpp:538](../src/Modules/PoolDeviceModule/PoolDeviceControl.cpp)) :
   une écriture manuelle qui violerait `depends_on_mask` est reprise et coupée au
   tick suivant ; le `max_uptime` journalier coupe aussi un équipement déjà en marche.
3. **Persistance NVS disciplinée** : métriques throttlées (60 s en marche,
   immédiat à l'arrêt), protocole O2 throttlé à 30 s, rollover jour/semaine/mois
   par clés de période avec réconciliation au retour de l'heure — pas d'usure
   flash inutile, pas de perte de compteurs au reboot.
4. **Validation systématique de la config au chargement**
   (`onConfigLoaded`, [PoolLogicLifecycle.cpp:1070-1111](../src/Modules/PoolLogicModule/PoolLogicLifecycle.cpp)) :
   chaque valeur hors domaine est ramenée au défaut **et** repersistée, donc les
   boots suivants sont cohérents.
5. **Threading propre** : les callbacks d'événements ne font que latcher des
   intentions (`pendingDailyRecalc_`…) sous spinlock, le travail reste dans la
   tâche du module. Les commandes vers PoolDevice sont retentées à cadence
   bornée (5 s) au lieu de spammer.
6. **Fallback dégradé pensé** : sans AlarmService, PoolLogic garde un latch pression
   local conservateur ; sans heure synchronisée, une pompe retenue au boot reste
   en marche jusqu'à ce que le scheduler puisse décider.
7. **Découpage des modules en translation units** (Lifecycle / Control /
   Scheduler / Runtime / Commands) avec un header façade — navigation aisée
   malgré la taille.

---

## 4. Faiblesses et risques

Classés par sévérité décroissante. Les références sont précises pour faciliter
la reprise.

### F1 — `runControlLoop_()` : monolithe de ~500 lignes non testé — **sévérité haute**

[PoolLogicControl.cpp:740-1233](../src/Modules/PoolLogicModule/PoolLogicControl.cpp).
La fonction enchaîne : sync de 7 FSM, lecture de 8 capteurs, résolution alarmes,
armement PID, arbitrage filtration, robot (avec override manuel), SWG (2 modes),
chauffage (machine à états heat-assist complète), 2 PID temporels, protocole O2,
remplissage — avec des écritures croisées sur `filtrationDesired` depuis le
heat-assist et l'O2. Conséquences :

- aucune de ces décisions n'est testable isolément (1 seul test unitaire dans
  tout le domaine, sur la fenêtre de filtration) ;
- l'ordre des blocs **est** la sémantique (le heat-assist peut forcer
  `filtrationDesired`, l'O2 aussi, mais après lui) — cet ordre n'est ni
  documenté ni protégé par un test ;
- chaque évolution (vous venez d'ajouter O2 et heat-assist) augmente le risque
  de régression sur les arbitrages existants.

### F2 — État heat-assist packé à la main — **sévérité moyenne, bug latent**

[PoolLogicControl.cpp:874-908](../src/Modules/PoolLogicModule/PoolLogicControl.cpp).
L'état de la machine heat-assist vit dans `heatAssistTimingPacked_` (deux
compteurs 16 bits packés dans un `uint32_t`, accédés par lambdas à masques) et
`heatAssistFlags_` (bitfield). Deux problèmes :

- **Bug latent réel** : `lastProbeEndSec == 0` sert de sentinelle « jamais
  sondé », mais `nowSec = (nowMs / 1000) & 0xFFFF` repasse naturellement par 0
  toutes les ~18 h 12 min. Si une sonde se termine pile à cet instant, le
  prochain cycle croit qu'aucune sonde n'a jamais eu lieu et relance
  immédiatement (pompe + chauffage sondés trop tôt). Probabilité faible
  (fenêtre d'1 s), conséquence bénigne, mais c'est typiquement le bug
  impossible à diagnostiquer sur le terrain.
- Lisibilité : la FSM (Idle → Probe → Heating → Idle) est réelle mais
  invisible ; ses transitions sont dispersées dans des lambdas.

### F3 — Lecture du débit pompe par parsing de chaîne — **sévérité moyenne**

`readPoolDeviceFlowLh_()` ([PoolLogicControl.cpp:117-143](../src/Modules/PoolLogicModule/PoolLogicControl.cpp))
récupère `flow_l_h` en sérialisant le module de config `pdm/pdN` en JSON puis en
faisant un `strstr("\"flow_l_h\":")`. C'est fragile (un renommage de clé ou un
changement de format JSON casse silencieusement le protocole O2 → dose bloquée
`flow_invalid`) et coûteux (sérialisation JSON à chaque évaluation du protocole).
`PoolDeviceSvcMeta` existe déjà et est le bon endroit pour exposer le débit.

Même famille : `weekStartMondayFromConfig_()`
([PoolDeviceControl.cpp:213](../src/Modules/PoolDeviceModule/PoolDeviceControl.cpp))
parse le JSON du module `time` à chaque calcul de clés de période.

### F4 — Triple vérité sur l'état des équipements — **sévérité moyenne**

L'état on/off de chaque équipement existe en trois exemplaires synchronisés par
polling : `DeviceFsm` dans PoolLogic, `slots_[].actualOn` dans PoolDevice, et
`PoolDeviceRuntimeStateEntry` dans le DataStore. Le code gère bien la
réconciliation, mais cette redondance impose des béquilles — par exemple le
forçage `filtrationFsm_.lastDesired = !filtrationDesired` pour provoquer une
réécriture ([PoolLogicControl.cpp:1216-1221](../src/Modules/PoolLogicModule/PoolLogicControl.cpp)),
qui fonctionne mais documente le problème.

### F5 — PID temporels : gains non normalisés, anti-windup rudimentaire — **sévérité basse à moyenne (fonctionnel)**

[PoolLogicControl.cpp:631-717](../src/Modules/PoolLogicModule/PoolLogicControl.cpp) :

- La sortie est en **millisecondes de pompe par fenêtre**, donc les gains
  portent des valeurs absurdes à lire (`PhKp = 2 000 000`). Un gain exprimé en
  % de fenêtre par unité d'erreur serait auto-documenté et indépendant de
  `window_ms`.
- L'intégrale est remise à zéro dès que l'erreur passe ≤ 0 : avec `Ki = 0` par
  défaut ce n'est pas gênant (on est en P pur), mais si un utilisateur active
  `Ki`, le comportement intégral sera erratique (pas de vraie anti-windup par
  clamp, perte de l'historique à chaque croisement du setpoint).
- Pas de borne sur `integral` → si `Ki > 0` et erreur durable (sonde HS côté
  haut), l'intégrale croît sans limite jusqu'au plafond fenêtre. Le plafond
  fenêtre protège l'actionneur, mais le déstockage sera long.

### F6 — Latch pression en mode dégradé jamais réarmé — **à documenter ou corriger**

Dans le fallback sans AlarmService
([PoolLogicControl.cpp:816-831](../src/Modules/PoolLogicModule/PoolLogicControl.cpp)),
`pressureError_` se latch mais **aucun chemin ne le remet à false** (pas de reset
commande, pas de clear sur condition revenue). C'est défendable comme choix
conservateur (redémarrer = reset), mais ce n'est écrit nulle part, et sur la
cible S3 l'AlarmModule est présent donc ce chemin ne sert que si l'init échoue —
autant l'assumer explicitement par un commentaire + log.

### F7 — Remplissage sans hystérésis côté capteur — **sévérité basse**

`fillingDesired = poolLevelOn` ([PoolLogicControl.cpp:1206-1214](../src/Modules/PoolLogicModule/PoolLogicControl.cpp)) :
le `fillingMinOnSec_` (30 s) lisse l'arrêt, mais le **démarrage** suit
directement le capteur de niveau. Un capteur à flotteur qui clapote au seuil
fera cycler la pompe (un démarrage max toutes les 30 s). Il manque un délai de
confirmation au démarrage (ex. niveau bas confirmé pendant N secondes).

### F8 — Couverture de test quasi nulle sur la logique métier — **transversal**

Un seul test (`computeFiltrationWindowDeterministic`). Or les candidats sont
nombreux et déjà presque purs : `stepTemporalPid_`, `stepO2Protocol_`,
`o2TemperatureFactor_`/`computeO2WeeklyDoseMl_`, `isO2DoseDay_`, la FSM
heat-assist, l'arbitrage filtration. À noter : **aucun environnement `native`
n'existe dans platformio.ini**, donc même le test existant ne peut tourner que
sur cible — ce qui explique sans doute qu'il soit seul.

### F9 — Divers (mineur)

- [PoolBindings.h:54-76](../src/Domain/Pool/PoolBindings.h) : les deux tables
  répètent 8 et 10 lignes quasi identiques indexées à la main — un index erroné
  lors d'un ajout ne serait pas détecté (un `static_assert` de cohérence
  `kSensors[i].legacySlot == i` serait gratuit).
- `o2PoolDeviceJsonBuf_[160]` est `mutable` et membre permanent juste pour un
  parse temporaire — un buffer de pile suffirait.
- Le robot a trois sources de vérité d'intention (auto, override manuel sous
  spinlock, durée max) avec quatre sections critiques dans le même bloc —
  correct mais dense ; une mini-FSM robot serait plus claire.
- `condPressureLowStatic_` retourne `False` quand le module est désactivé — OK, mais
  `Unknown` serait plus honnête sémantiquement pour l'affichage HA.

---

## 5. Pistes d'amélioration priorisées

### P1 — Rendre la logique critique testable (effort : moyen, gain : majeur)

1. **Créer un env PlatformIO `native`** pour les tests purs (`[env:native]`,
   `platform = native`), et y faire tourner le test existant. C'est le
   prérequis de tout le reste.
2. **Extraire les arbitrages de `runControlLoop_()` en helpers purs**, sur le
   modèle de `FiltrationWindow.{h,cpp}` : entrées = struct de mesures + état,
   sorties = struct de demandes. Dans l'ordre de valeur :
   - `HeatAssist.{h,cpp}` : la FSM chauffage complète (corrige F2 au passage
     en remplaçant le packing par une vraie struct avec `bool hasProbeEnded`) ;
   - `O2Protocol.{h,cpp}` : `stepO2Protocol_` est déjà presque pur (ses
     dépendances service peuvent devenir des paramètres) ;
   - `FiltrationArbiter.{h,cpp}` : la résolution
     sécurité/manuel/hors-gel/planning/demandes-externes en une fonction
     `resolve(demands) -> filtrationDesired` qui rend l'ordre des priorités
     explicite et testable.
3. **Écrire les tests des invariants de sécurité** en premier : « pression error ⇒
   filtration coupée même en manuel », « hors-gel ⇒ pas d'arrêt », « O2 ne force
   pas la filtration si pression », « robot jamais sans filtration ». Ce sont eux qui
   protègent la piscine et le matériel.

### P2 — Supprimer les couplages fragiles (effort : faible, gain : robustesse)

4. **Exposer `flowLPerHour` dans `PoolDeviceSvcMeta`** et supprimer
   `readPoolDeviceFlowLh_()`/`o2PoolDeviceJsonBuf_` (corrige F3).
5. **Mettre en cache `week_start_mon`** dans PoolDeviceModule (lecture au
   `onConfigLoaded` + invalidation sur `ConfigChanged`) au lieu du parse JSON
   par appel.
6. **`static_assert` de cohérence** sur les tables de
   [PoolBindings.h](../src/Domain/Pool/PoolBindings.h) (slots == index).

### P3 — Améliorations fonctionnelles (effort : variable, à discuter)

7. **PID** : exprimer les gains en % de fenêtre/unité d'erreur (avec migration
   des valeurs persistées), clamper l'intégrale (anti-windup réel), et exposer
   le duty effectif (%) dans le snapshot runtime `rt/poollogic/ph|orp` pour
   diagnostiquer le réglage depuis l'interface web.
8. **Remplissage** : délai de confirmation au démarrage (ex. 10 s de niveau bas
   continu avant `fillingDesired = true`) pour absorber le clapot (F7).
9. **Tableau des FSM** : remplacer les 7 paires membre/slot
   (`filtrationFsm_`+`filtrationDeviceSlot_`, …) par une table
   `{ slot, fsm, label, role }` itérable — réduit `syncAllDeviceStates_`,
   `adoptBootDeviceState_` et les 7 appels `applyDeviceControl_` à des boucles.
10. **Documenter l'ordre d'arbitrage** dans
    [flowio-poollogic-business.md](integration/flowio-poollogic-business.md) :
    qui peut forcer la filtration (heat-assist, O2), dans quel ordre, et
    pourquoi — aujourd'hui cette connaissance n'existe que dans l'ordre des
    lignes de `runControlLoop_()`.

### Suggestion de séquencement

| Étape | Contenu | Risque de régression |
|---|---|---|
| 1 | Env `native` + tests sur l'existant pur (FiltrationWindow, O2 helpers) | Nul (aucun code produit modifié) |
| 2 | P2.4 et P2.5 (couplages) | Faible, testable sur carte |
| 3 | Extraction HeatAssist + tests (corrige le bug F2) | Moyen, mais couvert par les tests écrits en 1 |
| 4 | Extraction O2Protocol puis FiltrationArbiter + tests d'invariants | Moyen |
| 5 | P3 au fil de l'eau | Variable |

---

## 6. Annexe — Inventaire et métriques

| Fichier | Lignes | Rôle |
|---|---|---|
| `Domain/Pool/PoolDomain.h` | 66 | Tables déclaratives rôles/presets |
| `Domain/Pool/PoolBindings.h` | 95 | Slots ↔ IoId ↔ noms |
| `Domain/Pool/PoolDefaults.h` | 73 | Constantes métier |
| `PoolLogicModule.h` | 474 | Façade + ~60 ConfigVariable |
| `PoolLogicControl.cpp` | 1128 | Boucle de contrôle, PID, O2, alarmes |
| `PoolLogicLifecycle.cpp` | 1332 | Init, config, événements, normalisation |
| `PoolLogicCommands.cpp` | 444 | Commandes (filtration, modes, MQTT) |
| `PoolLogicScheduler.cpp` | 175 | Slots scheduler + fenêtre |
| `PoolLogicRuntime.cpp` | 326 | Snapshots MQTT/runtime UI |
| `FiltrationWindow.{h,cpp}` | 83 | Helper pur testé ✔ |
| `PoolDeviceControl.cpp` | 667 | Service, interlocks, compteurs |
| `PoolDeviceLifecycle.cpp` | 492 | Config par device, enregistrement |
| `PoolDeviceRuntime.cpp` | 469 | Snapshots état/métriques |
| `PoolDeviceCommands.cpp` | 305 | write/refill/reset uptime |
| Tests | 1 fichier | `computeFiltrationWindowDeterministic` uniquement |

Alarmes pool enregistrées (7) : `PoolPressureLow`, `PoolPressureHigh`, `PoolPhTankLow`,
`PoolChlorineTankLow`, `PoolPhPumpMaxUptime`, `PoolChlorinePumpMaxUptime`,
`PoolWaterLevelLow` — toutes avec conditions évaluées par l'AlarmModule central
(anti-rebond on/off, latch, reset commandable).

Équipements gérés (8 slots) : filtration, pompe pH, pompe chlore/O2, robot,
pompe remplissage, électrolyseur (SWG), éclairage, réchauffeur — les slots
éclairage n'ont pas de logique dans PoolLogic (commande directe via
PoolDevice), ce qui est cohérent.
