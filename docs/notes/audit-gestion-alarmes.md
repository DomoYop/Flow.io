# Audit de la gestion des alarmes

**Statut : audit + refonte en 3 lots.** État relevé sur `main` au 11/08/2026, profil `Waveshare-ESP32-S3`.
Les trois lots décrits en §6 sont implémentés à la suite de cette note ; leur état est suivi en §7.

## 1. Carte du système

| Étage | Où | Quoi |
|---|---|---|
| Moteur | [AlarmModule.cpp](../../src/Modules/AlarmModule/AlarmModule.cpp) | 16 slots max (`Limits::Alarm::MaxAlarms`), évaluation toutes les 250 ms, machine à états latch/délais |
| Définitions | [PoolLogicLifecycle.cpp](../../src/Modules/PoolLogicModule/PoolLogicLifecycle.cpp) `onStart` | 9 alarmes enregistrées à la suite |
| Conditions | [PoolLogicControl.cpp](../../src/Modules/PoolLogicModule/PoolLogicControl.cpp) `cond*Static_` | 9 callbacks tri-état |
| Consommateurs | interlock filtration, buzzer, LED WS2812, MQTT, Home Assistant, 2 vues web | |

Sept consommateurs, mais surtout **six représentations différentes du même état** :

1. `rt/alarms/id<AlarmId>` — un topic par alarme (source de vérité) ;
2. `rt/alarms/m` — agrégat (compte + sévérité max) ;
3. `rt/alarms/p` — champ packé 5 bits/slot, 8 slots ;
4. masks Runtime UI 901/902/903 (`active_mask`, `resettable_mask`, `condition_mask`) ;
5. tuiles `alarm_slots` de l'API web (`/api/pool/dashboard`) ;
6. entités Home Assistant (binary_sensors + `alm_pack` + boutons).

L'information est simple ; c'est sa diffusion qui ne l'est pas. Les points 3, 4 et 5 sont des dérivées
de 1, et tous trois sont **indexés par position de slot**.

## 2. Le modèle d'état actuel

Trois notions stockées — `condition` (False / True / **Unknown**), `active`, `latched` (statique, fixé à
l'enregistrement) — d'où deux dérivées, `resettable` et le champ packé. La règle du reset est la clé :

> `reset` n'est accepté **que si** l'alarme est active, latched, **et que la condition est déjà retombée**.

Autrement dit : **il n'y a pas d'acquittement, seulement un effacement conditionnel**. Tant que la cause
persiste, aucun bouton ne fait quoi que ce soit, et [HMIBuzzerModule.cpp](../../src/Modules/HMIBuzzerModule/HMIBuzzerModule.cpp)
rebipe **toutes les 8 s** (`kAlarmRepeatMs`) sans autre échappatoire que couper le buzzer entièrement.

Signe que le manque avait été identifié : `EventId::AlarmSilenceChanged = 413` existe, `MQTTModule` y est
abonné — et **personne ne l'émet**.

## 3. Constats

### 3.1 Structurel — le `slot` est un index d'allocation traité comme une identité

`registerAlarm_` prend le premier slot libre (`findFreeSlot_`) ; l'ordre ne tient qu'à la séquence d'appels
de `PoolLogic`. En dépendent :

- les 5 bits par slot de `buildPacked_` ;
- les 8 boutons Home Assistant `alm_reset_slot_<0..7>` et la commande `alarms.reset_slot` ;
- les libellés i18n `runtimeui.poollogic.alarms.flag00..flag08` ;
- les trois masks Runtime UI.

Rien ne casse le build si l'ordre change : les libellés glissent en silence et un bouton acquitte l'alarme
du voisin. La donnée possède pourtant déjà une clé stable — `AlarmId`, garanti par des `static_assert`.

Corollaires de la limite à 8 slots packés : la 9ᵉ alarme (`PoolWaterTemperatureUnavailable`, 1008) est
absente du pack et n'a aucun bouton de reset ; elle a dû être déclarée non latched pour rester acquittable.

### 3.2 Fonctionnel

| Manque | Conséquence concrète |
|---|---|
| Pas d'acquittement | Buzzer toutes les 8 s tant que la cause dure, aucun moyen de le faire taire |
| Aucune persistance | Les slots repartent à zéro au boot : un reboot vaut acquittement d'un latch de sécurité |
| Pas d'horodatage réel | `lc` est un `millis()` (rollover 49,7 j, incomparable après reboot) alors que `TimeService` existe |
| Pas d'hystérésis | Seuils analogiques secs (`pressure < seuil`) ; seuls le latch et l'`offDelay` masquent le battement |
| Aucun journal | Traçabilité uniquement via les logs série / LogHub |
| Sévérité quasi inerte | Un seul consommateur la lit (le buzzer, `Critical` vs le reste) ; Info/Warning/Alarm sont indistinguables partout ailleurs |

### 3.3 Dette et pièges

- **`alm_reset_all` est inopérant.** Son `payloadPress` contient des `\"` littéraux alors que
  `HAModule::publishButton` applique déjà `jsonEscape()` : le payload part doublement échappé et le
  parseur de commandes le rejette. Les boutons `alm_reset_slot_*`, eux, utilisent du JSON brut — correct.
- **`latched` ment.** [WebInterfaceServer.cpp](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp)
  `waveshareReadAlarmDashboardSlotState_` fait `out.latched = doc["a"]` : le champ JSON nommé `latched`
  porte en réalité l'état *actif*. L'affichage est juste, le nom est faux, et le vrai `r` n'est jamais
  exposé par ce chemin.
- **Config fantôme.** Les tuiles d'alarme du dashboard web se configurent via `tft/s3/alarms/slotNN`,
  clés appartenant à `TFTModuleS3` — module **exclu du `build_src_filter` Waveshare**. Elles ne sont donc
  jamais enregistrées, `toJsonModule` échoue et les défauts en dur s'appliquent toujours. Mais les cfgdocs
  de l'image SPIFFS les exposent quand même : 8 × 4 variables réglables sans le moindre effet.
- **Trois jeux de libellés non liés** : tokens i18n positionnels, `name` anglais en dur dans la discovery
  HA, libellés français en dur dans le C++ du serveur web (`kWaveshareAlarmDashboardDefaultLabels`).
- **Aucun test natif** du moteur, alors que latch / délais / tri-état est exactement ce que
  `pio test -e native` sait couvrir sans matériel.

## 4. La façon habituelle de traiter des alarmes

Référence : ANSI/ISA-18.2 (gestion des alarmes de procédé) et le guide EEMUA 191. Cinq principes,
tous transposables à une piscine.

1. **Deux booléens orthogonaux, pas trois notions.** `condition` (le défaut est présent) × `acknowledged`
   (l'humain a vu). D'où le cycle de vie standard :

   ```
   Normal ──cond──> Active non acquittée ──ack──> Active acquittée
      ^                     │                            │
      │                     │ cond retombe               │ cond retombe
      │                     v                            v
      └──ack── Retombée non acquittée <───────────────  Normal
   ```

   Le couple `latched` / `resettable` actuel est un cas particulier de ce modèle, avec l'acquittement
   fusionné dans l'effacement — c'est précisément ce qui rend la sémantique difficile à énoncer.
2. **Acquitter ≠ effacer.** L'acquittement est *toujours* possible et coupe l'annonciation ; l'alarme reste
   visible tant que la cause persiste. L'effacement, lui, exige le retour à la normale.
3. **La priorité définit la réponse attendue**, pas une couleur : délai de réaction, qui est notifié, quoi
   est bloqué. Trois niveaux suffisent ; si deux priorités déclenchent la même chose, il n'y en a qu'une.
4. **Anti-chattering par hystérésis + délais**, pas par délais seuls.
5. **Un identifiant stable, un horodatage absolu, un journal.** Les agrégats se calculent **chez le
   consommateur** — Home Assistant sait très bien faire un « une alarme quelconque » sur un groupe.

## 5. L'écart, ligne à ligne

| Standard | flow.io avant refonte |
|---|---|
| `condition` + `ack` | `condition` + `active` + `latched` → `resettable` dérivé |
| Acquittement toujours possible | Reset refusé tant que la cause persiste |
| Identité = clé stable | Identité de fait = position d'enregistrement |
| Horodatage absolu + journal | `millis()`, pas de journal |
| 1 représentation, agrégats chez le client | 6 représentations, agrégats embarqués |
| Priorité → réponse différenciée | Sévérité lue par un seul consommateur, sur 2 niveaux |

## 6. Cible : `{ AlarmId, condition, active, acknowledged, sinceEpoch }`

### Lot 1 — acquittement

- `AlarmSlot.acknowledged` + `ackAtMs`, effacé automatiquement à chaque nouveau `raise` et à l'effacement.
- Service : `ack(id)`, `ackAll()`, `unackedCount()`, `highestUnackedSeverity()`.
- Commandes `alarms.ack` / `alarms.ack_all`, `EventId::AlarmSilenceChanged` enfin émis.
- Le buzzer se tait sur acquittement (il pilote sur les alarmes **non acquittées**).
- Champ `k` dans `rt/alarms/id<N>`, champ `u` dans `rt/alarms/m`.
- Correction du `payloadPress` de `alm_reset_all`.

### Lot 2 — fin de l'identité positionnelle

- Suppression de `rt/alarms/p`, `buildPacked`, `AlarmMsgPack`, entité `alm_pack`.
- Suppression de `alarms.reset_slot`, `slotAlarmId_` et des 8 boutons `alm_reset_slot_*`.
- **Un bouton par alarme**, identifié par `AlarmId` : « acquitter », dont la sémantique couvre les deux cas
  (condition retombée → efface ; condition présente → acquitte). Plus `alm_reset_all` conservé.
- Suppression des masks 901/902/903 et des tokens positionnels `flagNN` ; une valeur Runtime UI **par
  alarme**, portant l'état du cycle de vie §4 sous forme d'enum traduit.
- Suppression du chemin `alarm_slots` et de sa config fantôme `tft/s3/alarms/*`.

### Lot 3 — mémoire

- Horodatage epoch via `TimeService` (`since` dans l'état publié), `millis()` conservé en repli avant
  synchronisation NTP.
- Journal circulaire des transitions, exposé en HTTP et en commande.
- Persistance NVS des latchs actifs, restaurés à l'enregistrement des alarmes au boot.

## 7. Suivi

| Lot | Statut |
|---|---|
| 1 — acquittement | **implémenté** |
| 2 — fin du positionnel | **implémenté** |
| 3 — mémoire | **implémenté** |

Écarts et décisions prises en cours de route :

- **Les boutons `alm_reset_slot_*` ont été retirés dès le lot 1**, et non au lot 2 :
  faire coexister 9 boutons d'acquittement avec 8 boutons de slot dépassait la
  capacité `HaCapacitySpec.buttons`, dont le débordement est silencieux.
  Cette capacité passe de 24 à 32 sur Waveshare et FlowIODIN, le temps que les
  pierres tombales de nettoyage Home Assistant soient retirées.
- **Un seul bouton par alarme**, « acquitter », au lieu d'un bouton acquitter +
  un bouton effacer : `ack` couvre les deux gestes selon l'état de la condition.
- **`alarms.reset` reste exposé** en commande (API/MQTT) : seul `alarms.reset_slot`
  disparaît.
- **`codeOf()` ajouté au service** : `/api/status/compact` fabriquait des codes
  `alarm_<position+1>` à partir du numéro de slot, dernière trace du positionnel.
- **La carte « Alarmes piscine en cours » de la page Piscine était inerte** :
  `poolConfigRenderAlarms([])` recevait une liste vide en dur depuis son
  introduction. Elle est désormais alimentée par la liste réelle.
- **Le CSS des tuiles d'alarme par slot** (`.status-alarm-slot*`, `.status-flag-*`)
  a été retiré, ainsi que le mode d'affichage `flags` d'`app.js`, devenu sans
  utilisateur.

Points volontairement **hors périmètre**, à traiter séparément si besoin :

- l'hystérésis sur les seuils analogiques (touche `PoolLogic`, pas le moteur d'alarmes) ;
- la différenciation Info/Warning/Alarm chez les consommateurs (question produit : que doit faire de plus
  une alarme « critique » ?) ;
- les tests natifs du moteur ;
- les cfgdocs `tft/s3/alarms/*` toujours présents dans l'image SPIFFS : le
  générateur de cfgdocs n'est pas filtré par profil, ce qui concerne **tous** les
  modules hors `build_src_filter`, pas seulement les alarmes. Plus personne ne lit
  ces clés côté serveur web, mais elles restent affichées dans l'arbre de config.

## 8. Vérification sur cible

Non fait : les trois lots compilent (`pio run -e Waveshare-ESP32-S3`, flash 47,5 %,
RAM 35,0 %) mais n'ont pas été flashés. À vérifier au premier essai :

1. `alarms.ack` sur une alarme active dont la cause persiste → buzzer silencieux,
   `k:1` dans `rt/alarms/id<N>`, état Runtime UI à 2.
2. Même alarme dont la cause a disparu → l'alarme s'efface (état 0).
3. Coupure d'alimentation avec un latch actif → alarme toujours active au boot,
   entrée `Restored` dans `alarms.log`.
4. Page Piscine : la carte d'alarmes doit apparaître quand une alarme est active.
5. Home Assistant : disparition des entités `alm_reset_slot_*`, `alm_reset_all`
   et `alm_pack`, apparition des `alm_ack_*`.

Points volontairement **hors périmètre** de la refonte, à traiter séparément si besoin :

- l'hystérésis sur les seuils analogiques (touche `PoolLogic`, pas le moteur d'alarmes) ;
- la différenciation Info/Warning/Alarm chez les consommateurs (question produit : que doit faire de plus
  une alarme « critique » ?) ;
- les tests natifs du moteur.
