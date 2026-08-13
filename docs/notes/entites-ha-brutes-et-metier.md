# Home Assistant : entités « brutes » IO et entités métier

**État : constat, aucune modification.** Profil `Waveshare-ESP32-S3`.

Point de départ : dans HA on voit à la fois `sensor.pool_io_wat_tmp` / `pool_io_air_tmp`
(les deux températures métier) **et** `sensor.pool_io_temp1..4` (les quatre slots IO),
plus `sensor.pool_io_a08..a15` pour le moniteur de puissance.

## 1. Il y a deux chemins de découverte, indépendants

```
IOModule ──► PoolIoHa::registerDiscovery()      1 entité par slot IO publié
             (Domain/Pool/PoolIoHaDiscovery.cpp)   « ce que la carte mesure »

PoolLogic ─► onStart() → haSvc->addSensor()     1 entité par grandeur métier
             (PoolLogicLifecycle.cpp:723)          « ce que la piscine signifie »
```

Aucun des deux ne sait ce que l'autre publie. Le HAModule ne fait que collecter :
`addSensorEntry()` dédoublonne sur `(ownerId, objectSuffix)`, et comme les
propriétaires diffèrent (`io` contre `poollogic`), rien ne fusionne.

### Chemin IO — la règle est « tout slot défini est publié »

[PoolIoHaDiscovery.cpp:114](src/Domain/Pool/PoolIoHaDiscovery.cpp:114) :

```cpp
for (uint8_t i = 0; i < kAnalogHaSlots; ++i) {
    if (!ctx.io->analogSlotPublished(i)) continue;
    const PoolRoleSpec* role = domainRoleForIoSlot(*ctx.domain, analogInputSlot(i));
    ...
}
```

`analogSlotPublished_()` ([IOModule.cpp:845](src/Modules/IOModule/IOModule.cpp:845))
ne teste que trois choses : le module IO activé, le slot `used`, et un endpoint
présent. **Ni le binding, ni la présence physique du capteur.** Un slot défini
mais sans sonde produit quand même une entité — elle sort simplement
`unavailable` grâce au gabarit de disponibilité (`"available": false` dans
`rt/io/input/aNN`).

Le seul rôle du domaine est de **nommer** l'entité, pas de décider si elle existe :

| Slot a un rôle dans `kPoolRoles[]` ? | `object_id` | Nom |
|---|---|---|
| oui | `role->haObjectSuffix` (`io_ph`, `io_temp1`…) | `haName` / `displayName` |
| non | fallback `io_a%02u` | nom du slot en config (`endpointLabel`), sinon `A%02u` |

C'est exactement ce que montre ta capture : `sensor.pool_io_a08` nommé
« Puissance shunt mV » — le suffixe vient du fallback, le nom vient du nom de slot
NVS posé par l'auto-binding
([IoAnalogSlotDefaults.h:30](src/Modules/IOModule/IoAnalogSlotDefaults.h:30)).

## 2. Les 4 `io_temp1..4` : c'est voulu, et ce n'est pas un doublon

Ce n'est pas un oubli mais la conséquence directe de la refonte décrite dans
[temperatures-slots-generiques.md](temperatures-slots-generiques.md). Avant, la
couche IO décidait « eau » / « air », et l'inversion des deux sondes DS18B20 était
**incorrigeable sans reflasher**. Le principe retenu :

```
Sonde physique (ROM) → slot température 1..4 → rôle métier eau/air
  io/drivers/ds18b20/romN      a04..a07        poollogic/sensors/{wat,air}_temp_io_id
```

Les deux niveaux sont donc publiés parce qu'ils ne disent pas la même chose :

- `io_temp1..4` = **la sonde**, à son rang. Sert au diagnostic : identifier quelle
  ROM lit quoi, repérer une sonde muette, régler `wat_temp_io_id`.
- `io_wat_tmp` / `io_air_tmp` = **le rôle**, qui suit `wat_temp_io_id` /
  `air_temp_io_id`. Change de sonde sans changer d'entité HA. Les suffixes
  historiques ont été conservés volontairement pour ne casser aucun dashboard —
  ce sont désormais des entités `poollogic`, plus des entités `io`.

Deux différences concrètes entre `io_temp1` et `io_wat_tmp`, à sonde identique :

1. **Le topic.** `rt/io/input/a04` contre `rt/poollogic/temp` (`{"wat":…,"air":…}`).
2. **Le gel hors circulation.** `applySensorHoldBindings_()`
   ([PoolLogicControl.cpp:1078](src/Modules/PoolLogicModule/PoolLogicControl.cpp:1078))
   pose le hold sur `waterTempIoId_`, c'est-à-dire sur le **slot** désigné. Le gel
   s'applique donc au slot IO lui-même : la sonde eau est gelée aussi bien sous
   `io_temp1` que sous `io_wat_tmp`. La sonde air, elle, n'est jamais gelée.

Les quatre slots existent même sans sonde parce que le layout les bind d'usine aux
ports 120–123 ([WaveshareIoLayout.h:111](src/Profiles/Waveshare/WaveshareIoLayout.h:111)) :
`kAnalogRoleDefaults` couvre a00…a07. `io_temp3` / `io_temp4` sont donc présentes
et perpétuellement `unavailable` si tu n'as que deux sondes.

Note que ce n'est pas propre aux températures : `io_ph`, `io_orp`, `io_pressure`
sont dans le même cas — le slot brut et la valeur métier PoolLogic coexistent
(`rt/io/input/a01` et `rt/poollogic/ph`). La température est juste l'endroit où le
doublon se voit, parce que le rôle est déplaçable et que les slots sont numérotés.

## 3. Les `io_a08..a15` : pas de rôle, donc pas de nom métier

L'auto-binding
([IOModuleAssembly.cpp:97](src/Modules/IOModule/IOModuleAssembly.cpp:97)) pose les
8 canaux POWERMON sur les premiers slots libres après a07, soit a08…a15 (8 canaux
⇒ INA228 ; en INA226 les canaux ≥ 5 sont sautés et il n'y en aurait que 5).

Aucun `PoolRoleSpec` ne couvre `analogInputSlot(8..15)` dans
[PoolDomain.h](src/Domain/Pool/PoolDomain.h) : fallback `io_aNN`, pas d'unité, pas
d'icône métier, `mdi:sine-wave` par défaut. C'est cohérent avec ton intuition — ces
mesures n'ont pas encore de sens piscine.

Si tu veux les habiller sans inventer de fonction métier, le moins cher est
d'ajouter des lignes `kPoolRoles[]` sur les slots 8..15 : tu récupères
`object_id`, nom, unité (`mV`, `V`, `mA`, `mW`, `Wh`, `mAh`) et icône, sans toucher
au reste de la chaîne. Ça ne crée aucune fonction, seulement une présentation. Le
coût réel est le renommage des entités existantes côté HA (`io_a08` → `io_pm_shunt`,
par ex.), donc à faire d'un coup ou pas du tout.

## 4. Le vrai risque n'est pas le bruit visuel, c'est le plafond

`MaxSensors = 48` sur Waveshare
([WaveshareBoard.h:119](src/Board/WaveshareBoard.h:119)), et
`addSensorEntry()` **renvoie `false` en silence** au-delà
([HAModule.cpp:355](src/Modules/Network/HAModule/HAModule.cpp:355)) — tous les
appelants font `(void)haSvc->addSensor(...)`. Aucun log, aucune alarme : l'entité
n'apparaît simplement jamais dans HA.

Décompte de la configuration actuelle (INA228 activé, 2 sondes DS18B20) :

| Source | Sensors |
|---|---|
| Slots analogiques IO (a00…a07 rôles + a08…a15 POWERMON) | 16 |
| Entrée digitale en mode compteur (Water Counter) | 1 |
| PoolDeviceModule (uptimes + niveaux de bidon) | ≤ 7 |
| PoolLogicModule | 12 |
| SystemMonitorModule | 4 |
| WifiModule | 4 |
| AlarmModule (pierre tombale `retiredPack`) | 1 |
| **Total** | **≈ 45 / 48** |

Il reste **3 places**. Activer SHT40 (2 canaux) passe à 47 ; activer BME680
(4 canaux) déborde et fait disparaître une entité sans le dire. C'est l'argument
le plus solide pour ne pas laisser la découverte IO publier tout par défaut, bien
plus que l'encombrement dans l'interface HA.

## 5. Options, si tu veux réduire

Rangées par rapport effet/risque. Aucune n'est appliquée.

1. **Marquer les slots IO bruts `entity_category: "diagnostic"`.** Une ligne dans
   `syncAnalogSensors` (le champ existe déjà dans `HASensorEntry`, PoolLogic s'en
   sert pour ses capteurs O2). Effet : HA les range hors des contrôles principaux,
   elles restent disponibles pour le diagnostic. **Ne libère aucune place** sur les
   48. C'est le geste le plus rentable pour le confort d'usage.
2. **Ne publier que les slots effectivement bindés.** `analogSlotPublished_()`
   accepte aujourd'hui un slot sans binding ; `analogRuntimeRoutePublished_()`,
   juste en dessous, fait déjà le test complet (binding résolu + driver activé).
   Basculer la découverte sur la seconde ferait disparaître `io_temp3` / `io_temp4`
   quand aucune sonde n'est déclarée. Attention : il faut alors publier une pierre
   tombale (`HASensorEntry::absent`, déjà supporté) pour les entités qui existaient,
   sinon elles restent orphelines dans HA.
3. **Sortir les slots portant un rôle déjà republié par le métier.** Techniquement
   possible mais je le déconseille : on perdrait le moyen de voir quelle sonde
   physique lit quoi, ce qui est précisément le problème que la refonte
   températures cherchait à résoudre.
4. **Relever `MaxSensors`.** Le tableau d'entités est en RAM (`ensureStorage_`),
   la flash n'est pas le frein. Solution de facilité si on ajoute des capteurs I2C,
   mais elle ne règle pas le fait que le débordement est muet.

Un correctif orthogonal et peu coûteux dans tous les cas : **loguer le refus** dans
`addSensorEntry` / `addBinarySensorEntry` quand la capacité est atteinte. Aujourd'hui
seul un comptage manuel des entités côté HA permet de s'en apercevoir.

## Vérification

- `rt/io/input/a04`…`a07` et `rt/poollogic/temp` sur MQTT : les valeurs doivent
  coïncider pour la sonde qui porte le rôle.
- Compter les entités du device `poolbox` dans HA : si le total des `sensor.` est
  bloqué à 48, une entité a été perdue en silence.
- Changer `poollogic/sensors/wat_temp_io_id` : `io_wat_tmp` doit suivre la nouvelle
  sonde, `io_temp1..4` ne doivent pas bouger.
