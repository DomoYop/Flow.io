# Type de désinfection : le passer en réglage « à froid »

**État : implémenté (option B), le 14/08/2026.** Profil `Waveshare-ESP32-S3`,
appliqué aussi à `FlowIO`. Voir le §7 pour ce qui a été fait et les écarts par
rapport à l'étude. Les §1 à §6 sont l'étude d'origine, conservée telle quelle.

Question posée : `poollogic/bassin/disinfection_type` se règle aujourd'hui à chaud.
Le mode de désinfection dépend du matériel installé, pas d'un usage quotidien —
peut-on le figer au démarrage, et en profiter pour ne plus déclarer du tout ce
que le mode retenu n'utilise pas ?

---

## 1. Le « à chaud » est déjà une demi-mesure

Mettre un mode de désinfection en service demande deux réglages, pas un :

| Réglage | Effet | Prise en compte |
|---|---|---|
| `poollogic/bassin/disinfection_type` | choisit la stratégie | **immédiate** |
| `io/output/dNN/binding_port` | relie la fonction à un relais | **au prochain démarrage** |

Le second est explicitement documenté ainsi dans son aide :
« Le changement est pris en compte au prochain démarrage. » Et aucun des trois
actionneurs de désinfection n'est lié en sortie d'usine
([WaveshareIoLayout.h:138-140](src/Profiles/Waveshare/WaveshareIoLayout.h:138)) :

```cpp
{PoolIds::ActuatorChlorinePump,      IO_PORT_INVALID, ...}, // non liee par defaut
{PoolIds::ActuatorChlorineGenerator, IO_PORT_INVALID, ...}, // non liee par defaut
{PoolIds::ActuatorO2Pump,            IO_PORT_INVALID, ...}, // non liee par defaut
```

**Conséquence : une mise en service passe déjà obligatoirement par un
redémarrage.** Le caractère « à chaud » de `disinfection_type` ne fait donc
gagner aucune étape à l'utilisateur — il ne fait qu'entretenir un état
intermédiaire (mode choisi, relais pas encore actif) qui n'a pas d'utilité, et
du code pour le gérer.

## 2. Ce que le firmware fait déjà à froid

Quatre mécanismes du même genre sont déjà en place. Le chantier consiste à les
étendre, pas à en inventer un.

1. **Le patron de lecture NVS précoce existe.** `mqttEnabledInPreferences`
   ([WaveshareBootstrap.cpp:59](src/Profiles/Waveshare/WaveshareBootstrap.cpp:59))
   lit une clé NVS *avant* le ModuleManager ; si MQTT est désactivé, le HAModule
   n'est même pas ajouté. `ctx.preferences.begin()` a lieu ligne 266, avant
   `registerModules` et `configurePoolDevices` : lire `pl_dtype` au même endroit
   est trivial.
2. **Les PoolDevices sont déjà définis à froid.** `configurePoolDevices`
   ([WaveshareBootstrap.cpp:194](src/Profiles/Waveshare/WaveshareBootstrap.cpp:194))
   tourne avant tout chargement de config.
3. **Les variables de config d'un device suivent déjà son existence.**
   `PoolDeviceLifecycle.cpp:196` : `if (!s.used) continue;` — un device non
   défini n'enregistre aucune variable, et le type du device filtre déjà
   (débit et cuve seulement pour une péristaltique).
4. **Le masquage HA est déjà one-shot au boot.** `setEntityAbsent` selon
   `disinfectionType_` ([PoolLogicLifecycle.cpp:1439](src/Modules/PoolLogicModule/PoolLogicLifecycle.cpp:1439))
   et selon `deviceEnabled` ([PoolDeviceLifecycle.cpp:600](src/Modules/PoolDeviceModule/PoolDeviceLifecycle.cpp:600)),
   avec le commentaire « Reconfiguration prise en compte au prochain redémarrage ».

Ce qui resterait à chaud aujourd'hui, et disparaîtrait : le handler
`onConfigChanged` de `DisinfectionType`
([PoolLogicLifecycle.cpp:1648-1673](src/Modules/PoolLogicModule/PoolLogicLifecycle.cpp:1648)) —
arrêt de l'ancienne pompe avant bascule, `updateDisinfectionDeviceSlot_`, arrêt
de la nouvelle, forçage de `dis_auto_mode`, remise au repos du protocole O2,
reset du PID ORP. Environ 25 lignes de transition d'état, plus
`updateDisinfectionDeviceSlot_` qui devient une résolution unique au boot.

## 3. Le gain réel : le masquage HA actuel ne libère aucune capacité

C'est le point important, et il n'est pas visuel.

`setEntityAbsent` **n'annule pas la déclaration** : l'entrée occupe toujours sa
place dans le tableau et publie une pierre tombale
([HAModule.cpp:1148](src/Modules/Network/HAModule/HAModule.cpp:1148)).
Les 19 entités PoolLogic liées au mode sont donc créées **dans tous les cas**,
puis 16 d'entre elles sont marquées absentes :

| Mode | Entités | Détail |
|---|---:|---|
| Chlore liquide | 3 | 1 switch (`pl_dis_auto`), 2 numbers (`pl_dis_window`, `pl_dis_setpoint`) |
| Électrolyse | 3 | 1 select (`pl_swg_ctrl`), 2 numbers (`pl_swg_dly_elec`, `pl_swg_min_temp`) |
| Oxygène actif | 13 | **7 sensors**, 4 numbers, 1 switch, 1 select |
| **Total déclaré** | **19** | 7 sensors, 8 numbers, 2 switches, 2 selects |

À quoi s'ajoutent, pour les deux fonctions de désinfection non retenues :
les entités de leurs PoolDevices (6 pour la pompe chlore `pd2`, 3 pour
l'électrolyseur `pd3` — voir `kSlotEntities`), leurs variables de config
(6 pour une péristaltique, 4 pour un relais), et les deux alarmes de
désinfection avec leur binary_sensor et leur bouton d'acquittement.

**Ce que ça change concrètement** : la note
[entites-ha-brutes-et-metier.md](entites-ha-brutes-et-metier.md) chiffre
l'occupation à **≈ 45 / 48 sensors**, avec un débordement muet
(`addSensorEntry` renvoie `false` sans log) — activer BME680 fait disparaître
une entité sans le dire. Les 7 sensors O2 déclarés-puis-masqués sont
exactement la marge qui manque. En mode froid, un utilisateur en chlore
liquide ou en électrolyse les récupère.

Côté configuration, la branche `poollogic/disinfection` compte 21 champs :

| | champs |
|---|---:|
| chlore liquide (`dis_*`) | 8 |
| électrolyse (`swg_*`, `secure_elec_t`, `dly_electro_min`) | 3 |
| oxygène actif | 10 |

Selon le mode, **11 à 21 champs** quittent l'arbre de configuration — et
disparaissent réellement, au lieu d'être seulement repliés par `visible_if`
(qui ne couvre aujourd'hui que 2 champs de `poollogic/sensors`).

## 4. Les quatre pièges, dont un bloquant

### 4.1 Un démarrage manuel de pompe efface le type — bloquant

[PoolLogicCommands.cpp:311-327](src/Modules/PoolLogicModule/PoolLogicCommands.cpp:311) :
`poollogic.dis_pump.write` (démarrage manuel de la pompe de désinfection, atteint
depuis l'écran local via [HMIModule.cpp:2266](src/Modules/HMIModule/HMIModule.cpp:2266))
écrit `disinfection_type = 0` en NVS.

```cpp
snprintf(patch, sizeof(patch),
         "{\"poollogic/bassin\":{\"disinfection_type\":%u}}", DisinfectionDisabled);
```

Aujourd'hui c'est « le démarrage manuel coupe l'automatisme ». En froid, ce
serait **la perte de la configuration matérielle** : au redémarrage suivant, la
pompe, ses réglages et ses entités auraient disparu parce que l'utilisateur a
démarré sa pompe à la main une fois. À corriger d'abord, indépendamment du reste :
la clé à remettre à zéro est `dis_auto_mode`, pas `disinfection_type` — c'est le
même geste que pour le pH, qui vise bien `ph_auto_mode`.

### 4.2 Aucun mécanisme « redémarrage requis » dans l'interface

Rien n'existe : ni bandeau, ni indicateur. L'utilisateur changerait le mode et ne
verrait **rien** se produire jusqu'au redémarrage — le pire mode d'échec
possible, et celui que la convention actuelle (une phrase dans le texte d'aide)
ne couvre qu'à moitié.

Le remède est peu coûteux : geler la valeur lue au boot dans un
`bootDisinfectionType_`, exposer une valeur Runtime UI booléenne quand elle
diffère de la valeur de config courante, et afficher un bandeau avec un bouton
de redémarrage — l'endpoint `POST /api/system/reboot`
([WebInterfaceServer.cpp:7200](src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp:7200))
existe déjà. Ce bandeau servirait aussi aux bindings de relais, qui souffrent
aujourd'hui du même silence.

### 4.3 Commandes visant un équipement non déclaré

Un PoolDevice non défini fait répondre `POOLDEV_SVC_ERR_UNKNOWN_SLOT` aux
commandes MQTT/HA le visant. C'est correct et déjà géré par le service
(`ErrorCode::UnknownSlot`), mais il faut vérifier que rien côté UI ne suppose que
les 12 fonctions existent toujours — l'invariant `pdN ⇔ dNN` de la refonte v2
est un `static_assert` sur la table compile-time, il n'est pas affecté.

### 4.4 Sauvegarde de configuration partielle

`exportJson` n'exporte que les variables enregistrées : une sauvegarde faite en
mode chlore ne contiendrait pas les réglages O2. Sans importance en usage normal
(la NVS n'est pas effacée, rebasculer restaure tout), mais une restauration après
effacement NVS ne ramènerait que le mode courant. À mentionner dans l'aide.

## 5. Options

### A — Statu quo

Rien à faire. Les 19 entités HA restent déclarées, la marge sensors reste à 3
places, l'arbre de configuration garde ses 21 champs de désinfection.

### B — Froid complet (recommandé)

`disinfection_type` lu dans les Preferences au démarrage du profil, puis :
définition conditionnelle des PoolDevices, enregistrement conditionnel des
variables de config PoolLogic, des entités HA et des deux alarmes de
désinfection. Suppression du handler à chaud, du `setEntityAbsent` par mode
(devenu sans objet), et bandeau « redémarrage requis ».

Points de code concernés :

| Fichier | Nature |
|---|---|
| `WaveshareBootstrap.cpp` | lecture précoce `pl_dtype` + `configurePoolDevices` conditionnel |
| `PoolLogicLifecycle.cpp` | `registerVar` conditionnels, `registerAlarm` conditionnels, suppression du handler à chaud et des `setEntityAbsent` par mode |
| `PoolLogicCommands.cpp` | correctif §4.1 |
| `PoolLogicControl.cpp` | `condInapplicable_` devient inutile pour les deux alarmes de désinfection |
| `app.js` + Runtime UI | bandeau « redémarrage requis » |
| catalogues i18n | mention « pris en compte au prochain démarrage » |

Effort : une demi-journée à une journée, plus les essais sur cible — les quatre
modes, chacun avec un redémarrage, plus une bascule dans chaque sens.

### C — Correctif §4.1 seul

Le piège du démarrage manuel qui efface le type est un défaut réel **aujourd'hui
déjà** : il fait perdre le mode de désinfection sans prévenir, simplement de
façon moins visible qu'en froid. Il vaut d'être corrigé même si le reste est
abandonné. Effort : quelques lignes.

## 6. Recommandation

**B, en commençant par C.** Le raisonnement décisif n'est pas le confort visuel
mais le §3 : le masquage HA actuel donne l'illusion du ménage sans rien libérer,
et la marge de 3 sensors est déjà le point de rupture connu du profil. Le §1
enlève le principal argument contre : le redémarrage est déjà obligatoire pour
mettre un mode en service, on ne dégrade donc pas l'expérience — on aligne le
réglage sur ce qui se passe réellement.

La condition de réussite est le §4.2 : sans un signal visible « redémarrage
requis », le froid transforme un réglage sans effet apparent en support
téléphonique.

## 7. Ce qui a été implémenté (14/08/2026)

Option B complète, en commençant par le correctif §4.1. Build
`Waveshare-ESP32-S3` vert (flash 47,6 %), gates `i18n`, `io-port-sync`,
`firmware-size` et `cppcheck` rejoués localement sans alerte.

### 7.1 Le mode vit désormais dans le domaine

`PoolIds::Disinfection` ([PoolIds.h](../../src/Domain/Pool/PoolIds.h)) remplace
l'enum privé de `PoolLogicModule`, qui n'en garde que des alias. Le bootstrap de
profil en a besoin **avant** que le module existe : le laisser dans le module
aurait demandé de rendre l'enum public sans que la couche soit la bonne.

`disinfectionTypeInPreferences()` lit `pl_dtype` juste après `runMigrations`, sur
le patron de `mqttEnabledInPreferences`. Une valeur hors enum retombe sur
Désactivé. La valeur est passée à `configurePoolDevices()`, qui saute les
équipements des deux modes non retenus, puis à
`PoolLogicModule::setBootDisinfectionType()`.

Fait sur **les deux** profils qui portent PoolLogic (`Waveshare` et `FlowIO`).
L'étude ne visait que Waveshare, mais sans l'appel côté FlowIO le mode y serait
resté silencieusement figé sur « Désactivé ».

### 7.2 Deux valeurs, une seule fait foi

`disinfectionType_` reste le miroir de la config (il suit la liste déroulante) ;
`bootDisinfectionType_` est le mode en service. **`isDisinfectionType_()` consulte
le second** : c'était le seul point d'entrée de la logique de contrôle, la bascule
a donc tenu en une ligne. Les champs `dt`/`dts` des snapshots MQTT décrivent eux
aussi le mode en service.

Sont conditionnés par le mode figé, dans `init()` (donc avant tout chargement de
config) :

| | chlore | électrolyse | O2 |
|---|---|---|---|
| variables de config | `dis_auto_mode`, PID Redox (6) | `swg_control_mode`, `secure_elec_t`, `dly_electro_min` | les 10 `o2*` |
| `dis_setpoint` | oui | oui (consigne partagée) | non |
| entités HA | 1 switch, 1 number | 1 select, 2 numbers | 7 sensors, 1 switch, 1 select, 4 numbers |
| alarmes | bidon + uptime pompe | — | bidon + uptime pompe |

Le select `pl_modes_dis` (le choix lui-même) et les pierres tombales
(`pl_ph_window`, `pl_o2_vol`) restent déclarés dans tous les cas.

Supprimés, devenus sans objet : le handler à chaud de `DisinfectionType`
(remplacé par une simple trace de l'écart), les `setEntityAbsent` par mode, et
`condInapplicable_` avec les deux tests qui l'appelaient.
`updateDisinfectionDeviceSlot_` devient `resolveDisinfectionDeviceSlot_`, appelée
une fois.

### 7.3 Le mode en service ne se publie pas, il se constate

L'étude proposait une valeur Runtime UI booléenne. Elle n'était pas praticable
telle quelle : sur Waveshare, `/api/runtime/values` ne lit que le DataStore et le
ConfigStore, jamais `writeRuntimeUiValue`
([runtime-ui-double-chemin-waveshare.md](runtime-ui-double-chemin-waveshare.md)).

Première tentative, abandonnée : republier la valeur figée dans une variable de
configuration `poollogic/bassin/dis_type_live`. Elle fonctionnait, mais mettait
dans un arbre de **réglages** une valeur qui n'en est pas un — deux lignes
voisines disant la même chose, dont une en lecture seule qu'il fallait masquer.
Un `hidden: true` n'aurait fait que cacher le problème.

Retenu : **le mode en service se déduit des équipements réellement définis.**
`waveshareLivePoolDisinfectionType_` regarde lequel de `pd2` / `pd3` / `pd4` est
présent dans le DataStore (`PoolDeviceRuntimeStateEntry::valid`, vrai pour les
seuls slots définis) et publie `dis_live` dans
`/api/flow/status/domain?d=pool`, que l'UI sait déjà consommer avec cache.

C'est plus juste qu'une recopie : ce qui compte pour l'utilisateur n'est pas un
`uint8_t` mais quel équipement existe. `pd0` (filtration, toujours défini) sert
de témoin de disponibilité — sans lui, on ne saurait pas distinguer « PoolDevice
n'a pas fini d'initialiser » de « désinfection désactivée », et le bandeau
clignoterait au démarrage. Absence de `dis_live` = aucun bandeau.

Le bandeau s'affiche dans le sélecteur de traitement de la page Piscine
(`poolConfigFillRebootNotice`), rempli en différé comme les métriques runtime,
avec un bouton qui appelle l'endpoint de redémarrage existant. L'écran TFT fait
la même déduction depuis le DataStore.

Les `visible_if` de `dis_io_id` / `dis_lvl_io_id` restent volontairement sur
`disinfection_type`, c'est-à-dire sur le **choix** : ces deux réglages de sonde
sont enregistrés dans tous les cas (les conditionner ferait perdre leur valeur
en mode désactivé), et pouvoir préparer l'affectation des sondes avant de
redémarrer est légitime.

### 7.4 Deux défauts trouvés en chemin

Le §4.1 décrivait `poollogic.dis_pump.write`. Il existait une **seconde** porte,
plus dommageable : `pooldevice.write`
([PoolDeviceCommands.cpp](../../src/Modules/PoolDeviceModule/PoolDeviceCommands.cpp)),
atteinte depuis Home Assistant et MQTT, écrivait `disinfection_type = 3` — un
démarrage manuel de la pompe à chlore basculait donc toute la piscine en oxygène
actif. Les deux chemins visent désormais `dis_auto_mode`. Au passage, les
lectures de `poollogic/ph/ph_pump_slot` et `poollogic/disinfection/dis_pump_slot`
y étaient mortes depuis la refonte des fonctions piscine (clés supprimées) : le
slot vient de la table de domaine, et la pompe de désinfection peut être celle du
chlore ou celle de l'oxygène actif.

Second défaut, dans `waveshareEnsurePoolMode_` : le commentaire
« type == Disabled == 3 » datait d'avant le réordonnancement de l'enum. La tuile
« désinfection auto » était donc masquée en **oxygène actif** et affichée en
**désinfection désactivée**, exactement à l'envers. Le test disparaît : la seule
présence de la clé `dis_auto_mode` répond désormais à la question.

### 7.5 Pas d'effacement NVS

`pl_dtype` est inchangée et aucune clé n'est ajoutée. Les clés des modes non
retenus restent en NVS sans être lues : rebasculer sur un mode restaure ses
réglages tels qu'ils étaient. Seule une restauration **après** effacement NVS ne
ramènerait que le mode courant (§4.4), ce que dit maintenant l'aide du champ.

## Vérification (à faire sur cible)

- Compter les entités du device `poolbox` dans HA avant/après, en mode chlore :
  les 7 sensors O2 doivent avoir disparu, pas seulement être `unavailable`.
- `poollogic.dis_pump.write` **et** `pooldevice.write` sur le slot pd2, puis
  redémarrage : le mode de désinfection doit être **inchangé**, et seul
  `dis_auto_mode` doit être retombé.
- Bascule chlore → O2 : avant redémarrage, le comportement reste chlore et le
  bandeau s'affiche ; après, l'inverse.
- Arbre de configuration : la branche `poollogic/disinfection` ne doit contenir
  que les champs du mode retenu.
- Revenir au mode précédent et redémarrer : les réglages d'origine doivent être
  retrouvés intacts (validation du §7.5).
