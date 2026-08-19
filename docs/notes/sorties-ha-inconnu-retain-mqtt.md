# Sorties à l'état « inconnu » dans Home Assistant

**État : correctif appliqué le 18/08/2026** (retain sur les routes actionneur).
Profil `Waveshare-ESP32-S3`, firmware 4.3.5. La cause première — une liaison WiFi
marginale — n'est pas traitée par le correctif : voir §6.

Symptôme : toutes les sorties (switches d'équipement) apparaissent « inconnu »
dans Home Assistant, alors que l'appareil `poolbox` est disponible et que les
capteurs (pH, Redox, températures) affichent des valeurs correctes.

---

## 1. Ce que le relevé sur cible a établi

`GET /api/flow/status/domain?d=pool`, carte en service depuis ~71 min :

```json
"pool":{"has":true,"auto":true,"dis_live":2,"wat":27.6,"air":30.3,
        "ph":7.3,"orp":486,"fil":false,"php":false,"clp":null,"rbt":null}
"wifi":{"rdy":true,"ip":"10.10.50.90","rssi":-84,"hrss":true}
"mqtt":{"rdy":true,"srv":"10.10.20.20:1883","rxdrp":0,"prsf":0,"hndf":0,"ovr":0}
```

Trois lectures décisives :

| Champ | Valeur | Ce que ça prouve |
|---|---|---|
| `fil`, `php` | `false` (pas `null`) | Le firmware **connaît** l'état de ses sorties. Le DataStore n'est alimenté que si `runtimePublishable` est vrai ([PoolDeviceRuntime.cpp:397](../../src/Modules/PoolDeviceModule/PoolDeviceRuntime.cpp:397)) : aucune sortie n'est `unbound` ni `disabled`. |
| `clp` | `null` | **Normal.** `dis_live: 2` = `DisinfectionSwg` (électrolyse) : la pompe à chlore n'est pas déclarée dans ce mode. Comportement voulu du réglage à froid. |
| `rssi` | **-84 dBm** | Liaison marginale. En dessous de -80, la perte de paquets devient courante. |

`rxdrp`/`ovr` à 0 ne contredit rien : ces compteurs portent sur le trafic
**entrant**, pas sur les publications sortantes perdues en radio.

## 2. La cause

[RuntimeProducer.cpp](../../src/Modules/Network/MQTTModule/RuntimeProducer.cpp),
`buildMessage()`, publiait **toutes** les routes runtime en QoS 0 et sans
rétention :

```cpp
ctx.qos = 0;
ctx.retain = false;
```

Conséquence, sur une liaison qui perd des paquets :

- une **mesure** perdue se rattrape à l'acquisition suivante — d'où des capteurs
  corrects ;
- une **sortie** qui ne change pas d'état n'est jamais republiée. Rien ne la
  rattrape, et Home Assistant reste sur « inconnu » jusqu'au prochain
  basculement réel du relais. Une filtration à l'arrêt peut le rester des jours.

## 3. Pourquoi « depuis quelques jours » sans qu'aucune ligne n'ait bougé

Le `retain = false` date de `575ea98c` (09/03/2026, réécriture du transport MQTT
en job-based) et n'a jamais été modifié depuis ; les sorties y sont exposées
depuis `1d1f5752` (20/03/2026). **Le défaut est donc latent depuis cinq mois.**

Ce qui a changé n'est pas le firmware mais les **conditions radio** : le budget
d'erreur est passé sous le seuil où la perte devient probable à chaque
reconnexion. C'est ce qui explique un symptôme apparu « il y a quelques jours »
sans commit coupable — et la valeur `WIFI_TIMEOUT_MS` passée de 12 s à 45 s dans
l'arbre de travail témoigne de la même dégradation.

**Ne pas chercher de régression de code sur ce symptôme** : le relevé du §1
suffit à trancher entre « le firmware ne sait pas » et « le message n'arrive
pas ».

## 4. Pistes explorées et écartées

Conservées parce qu'elles sont plausibles et qu'elles reviendront :

| Piste | Pourquoi elle tombe |
|---|---|
| Régression du réglage de désinfection à froid (`8b185c5`) | Ne touche que `pd2`/`pd3`/`pd4`. `clp: null` est le comportement attendu en électrolyse. |
| Chaîne `b71c65f` (05/08, activation par câblage d'usine) + `8af34a3` (09/08, masquage HA des équipements désactivés) | Produirait des entités *supprimées* ou *indisponibles*, pas « inconnu ». Surtout : `fil`/`php` prouvent que les équipements sont liés et actifs. |
| Décalage des `IoId` par le passage à 24 entrées analogiques (`1061c64`) | `IO_ID_DO_BASE`/`DI`/`AI` valent 0/64/192, fixes ([IIO.h:17-21](../../src/Core/Services/IIO.h:17)) : la capacité analogique ne décale rien. |
| Interblocage du mutex PoolDevice (`buildStateSnapshot_` reprend le verrou via `slotRuntimePublishable_`) | Le mutex est **récursif** (`xSemaphoreCreateRecursiveMutexStatic`, [PoolDeviceLifecycle.cpp:56](../../src/Modules/PoolDeviceModule/PoolDeviceLifecycle.cpp:56)). Double prise volontaire et sûre. |
| Débordement de `MaxRuntimeRoutes` (112) | Pire cas ≈ 74 routes (6 PoolLogic + 44 IO + 24 PoolDevice). Aucune troncature. |
| Course au démarrage figeant `runtimePublishable` à faux | Réelle en théorie — `configureRuntime_` est one-shot et pose `runtimeReady_ = true` même sans endpoint résolu ([PoolDeviceRuntime.cpp:321](../../src/Modules/PoolDeviceModule/PoolDeviceRuntime.cpp:321)) — mais démentie par le relevé : le DataStore est alimenté. **À garder en tête** : le seul témoin est un `LOGD` (« Pool device %s sleeping »), invisible au niveau de log normal. |

## 5. Le correctif appliqué

Une ligne, dans `buildMessage()` :

```cpp
ctx.retain = (route.routeClass == RuntimeRouteClass::ActuatorImmediate);
```

Sont retenues les routes `ActuatorImmediate`, c'est-à-dire les états de sortie :
sorties digitales de l'IOModule ([IOModuleSnapshots.cpp:320](../../src/Modules/IOModule/IOModuleSnapshots.cpp:320))
et états — non métriques — des PoolDevice ([PoolDeviceRuntime.cpp:295](../../src/Modules/PoolDeviceModule/PoolDeviceRuntime.cpp:295)).

Les mesures (`NumericThrottled`) restent **volontairement** non retenues : une
température vieille d'une semaine ne doit pas apparaître comme actuelle, et elles
se rattrapent seules. Cette asymétrie est intentionnelle.

### Ce que le correctif ne fait pas

**Le retain ne rejoue rien vers un abonné déjà connecté.** Il n'agit qu'au moment
de l'abonnement :

- HA redémarre, se réabonne, ou recrée l'entité → sans retain : « inconnu ».
  **C'est le symptôme observé, et il est traité.**
- Paquet perdu pendant que HA est connecté → HA conserve l'ancienne valeur : état
  *périmé*, pas « inconnu ». **Non traité** ; seul QoS 1 le couvrirait (§6).

## 6. Effets de bord

**Topics orphelins retenus indéfiniment** — le seul qui demande une décision.
Changer de mode de désinfection fait cesser la publication de `pd2`/`pd3`/`pd4`,
mais leur dernier état reste dans le broker pour toujours : rien ne purge un
topic runtime retenu, contrairement à la discovery HA qui a sa pierre tombale
([HAModule.cpp:593](../../src/Modules/Network/HAModule/HAModule.cpp:593)). Idem
pour une sortie dont le binding change. HA n'est pas trompé (son entité est bien
supprimée), mais le broker accumule des états d'équipements disparus. Purge :

```bash
mosquitto_pub -h 10.10.20.20 -t 'flowio/<device>/rt/pdm/state/pd2' -r -n
```

**Fenêtre d'état périmé avant le testament** — sur un débranchement brutal, le
broker ne publie `{"online":false}` qu'à expiration du keepalive. Un abonné
arrivant dans cet intervalle reçoit l'état retenu comme s'il était frais. Borné,
puis l'`availability` reprend la main.

**Stockage broker** — négligeable : ~12 sorties IO + ~6 PoolDevice.

### Risques vérifiés et écartés

- **État faux affiché carte éteinte** : non. Le testament est retenu en QoS 1
  (`{"online":false}`, [MQTTTransport.cpp:82-85](../../src/Modules/Network/MQTTModule/MQTTTransport.cpp:82))
  et sert d'`availability` à toutes les entités ([HAModule.cpp:55](../../src/Modules/Network/HAModule/HAModule.cpp:55)).
  Carte hors ligne → `unavailable`, quel que soit l'état retenu.
- **Conflit avec la republication au boot** : non. `onConnected()` force toutes
  les routes ([RuntimeProducer.cpp:149](../../src/Modules/Network/MQTTModule/RuntimeProducer.cpp:149)),
  ce qui écrase les valeurs retenues.
- **Boucle de rétroaction** : non. Le firmware ne s'abonne pas à ses propres
  topics `rt/` — seulement `cmd` et `cfg/set`
  ([MQTTTransport.cpp:189-190](../../src/Modules/Network/MQTTModule/MQTTTransport.cpp:189)).
- **Autre consommateur cassé** : aucun sur ce profil. `rt/pdm/state/pdN` n'est lu
  que par la discovery HA ([PoolIoHaDiscovery.cpp:236](../../src/Domain/Pool/PoolIoHaDiscovery.cpp:236)).

## 7. Points ouverts

1. **Le RSSI à -84 dBm est la cause première.** Le correctif rend le symptôme
   invisible ; il ne fiabilise pas la liaison. Une liaison marginale dégradera
   aussi les commandes et les mesures. Répéteur ou repositionnement d'antenne.
2. **QoS 1 sur les routes `ActuatorImmediate`** — couvrirait la perte en cours de
   session, au prix d'un acquittement par publication et d'une pression accrue
   sur la file de jobs MQTT. À traiter en second temps, après avoir vérifié que
   le retain seul suffit.
3. **Purge des topics runtime orphelins** — aucun mécanisme aujourd'hui. Le
   patron existe pourtant à côté (pierre tombale de discovery).
4. **Le `LOGD` « Pool device sleeping » mériterait d'être un `LOGW`** : une
   sortie qui ne publiera jamais rien de tout le boot est un échec silencieux,
   du même genre que ceux que surveille `check_io_port_sync`.

## Vérification (à faire sur cible)

⚠️ Ne pas flasher sans avoir lu [ota-spiffs-gzip-bilan.md](ota-spiffs-gzip-bilan.md) :
les écritures flash corrompent le système de façon répétée depuis le 18/08/2026.

- Après flash, redémarrer Home Assistant seul (carte laissée en service) : les
  sorties doivent afficher leur état immédiatement, sans attendre un
  basculement de relais.
- Vérifier la rétention côté broker :
  `mosquitto_sub -h 10.10.20.20 -t 'flowio/+/rt/pdm/state/+' -v -W 2`
  doit rendre la main avec les états, sans qu'aucune sortie n'ait changé.
- Vérifier qu'une mesure n'est **pas** retenue (asymétrie voulue) :
  `mosquitto_sub -h 10.10.20.20 -t 'flowio/+/rt/io/input/+' -v -W 2`
  ne doit rien rendre tant qu'aucune acquisition n'a lieu.
- Couper l'alimentation de la carte : les entités doivent passer
  `unavailable` (testament), pas rester sur leur dernier état.
