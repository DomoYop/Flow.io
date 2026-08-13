# Type de désinfection : le passer en réglage « à froid »

**État : étude, aucune modification.** Profil `Waveshare-ESP32-S3`.

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

## Vérification (après implémentation)

- Compter les entités du device `poolbox` dans HA avant/après, en mode chlore :
  les 7 sensors O2 doivent avoir disparu, pas seulement être `unavailable`.
- `poollogic.dis_pump.write` puis redémarrage : le mode de désinfection doit
  être **inchangé**.
- Bascule chlore → O2 : avant redémarrage, le comportement reste chlore et le
  bandeau s'affiche ; après, l'inverse.
- Arbre de configuration : la branche `poollogic/disinfection` ne doit contenir
  que les champs du mode retenu.
