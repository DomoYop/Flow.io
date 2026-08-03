# Températures : slots génériques 1..4 et rôle eau/air côté métier

**État : implémenté et compilé** (profil `Waveshare-ESP32-S3`).

## Le problème

Les deux sondes DS18B20 pouvaient apparaître inverties, et l'inversion était **impossible à corriger** sans reflasher. Le rôle « eau » / « air » était décidé dans la couche IO, à quatre endroits qui ne s'accordaient pas.

1. **L'attribution ignorait le bus physique.** `resolveDsSensor_()` prenait « la première ROM libre, tous bus confondus » : l'eau se servait d'abord, l'air ensuite. Malgré les commentaires « bus eau = GPIO47 / bus air = GPIO48 », une sonde seule câblée sur GPIO48 devenait « eau », et deux sondes sur le même bus étaient départagées par l'ordre de recherche 1-Wire.
2. **Le choix était figé en NVS** (blobs `io_dswrm` / `io_dsarm`) : intervertir physiquement les sondes ne changeait rien.
3. **Le champ ROM de la config n'était jamais persisté.** `resolveDsSensor_` remplissait `cfgData_.dsWaterRom` en RAM mais n'écrivait que le blob : le champ restait vide dans l'interface alors que l'affectation était verrouillée. L'utilisateur ne voyait ni ne pouvait corriger la décision.
4. **Les index DataStore codés en dur (4 = eau, 5 = air) étaient des index de *registre*, pas de *slot*.** Seuls les slots dont le binding est résolu entrent au registre : si `a03` perdait son binding, `a04` glissait à l'index 3 et `pool.water_temp` affichait l'air. MQTT et HA, indexés par slot, restaient corrects — d'où le symptôme « HA bon, UI web inversée ».

S'y ajoutait une **contention 1-Wire** : chaque `Ds18b20Driver` appelait `bus->request()`, qui est un broadcast. Deux sondes sur un même bus relançaient chacune une conversion et pouvaient lire un scratchpad non converti (85,0 °C, valeur de mise sous tension du DS18B20).

## Le principe retenu

La couche IO n'expose plus que des **sondes de température génériques et numérotées**. Le rôle métier vit dans PoolLogic.

```
Sonde physique (ROM)  →  slot temperature 1..4  →  rôle métier eau / air
   io/drivers/ds18b20/romN      a04..a07          poollogic/sensors/{wat,air}_temp_io_id
```

Une inversion se corrige désormais de deux façons, toutes deux sans reflasher :
- échanger les ROM entre `rom1` et `rom2` (on change quelle sonde physique alimente quel slot) ;
- ou changer `wat_temp_io_id` / `air_temp_io_id` (on change quel slot porte le rôle eau).

## Ce qui a changé

### Couche IO — 4 sondes génériques

| Élément | Avant | Après |
|---|---|---|
| Canaux DS18B20 (`IoBackendTraits`) | `maxChannel = 1` | `maxChannel = 3` |
| Ports de binding | 120, 121 | 120 à 123 (`PortOneWire1..4`, les deux profils) |
| Sources analogiques | `IO_SRC_DS18_WATER/AIR` | `IO_SRC_DS18_1..4`, `IO_SRC_COUNT` 8 → 10 |
| Pool de drivers | `dsDriverPool_[2]` | `dsDriverPool_[IO_DS18_SLOT_COUNT]` |
| Config ROM | `water_rom`, `air_rom` (`io_dswr`, `io_dsar`) | `rom1..rom4` (`io_dsr1..io_dsr4`) |
| Cache d'attribution | blobs `io_dswrm` / `io_dsarm` | **supprimés** — la ROM auto-attribuée est écrite dans la `ConfigVariable`, donc visible et modifiable |
| Noms de slot par défaut | « DS18B20 eau » / « DS18B20 air » | « Température 1..4 » |
| Bus des profils | `oneWireWater` / `oneWireAir` | `oneWireBus1` / `oneWireBus2` (un bus ne porte aucun rôle) |

`resolveDs18Sensors_()` est réécrite : un relevé unique des ROM par bus (`scanDs18Buses_`, car `getAddress()` relance une recherche 1-Wire complète à chaque appel), puis pour chaque slot — ROM configurée d'abord, sinon première ROM libre dans un ordre déterministe (bus 1 → bus 2 → DS2484), en excluant celles déjà prises par les slots de rang inférieur. `resolveDsBusAddress_()`, code mort issu de l'ancienne logique « une sonde par bus », est supprimée.

### Conversion 1-Wire arbitrée par le bus

`IOneWireBus::tickConversion(nowMs, pollMs, conversionWaitMs)` porte désormais la machine à états de conversion : une seule conversion en vol par bus, toutes les sondes lisent la même. `Ds18b20Driver` ne déclenche plus `request()` lui-même et ne lit qu'une fois par conversion ; `readSample()` renseigne `seq`/`hasSeq`, ce qui évite de recalculer le slot à chaque tick.

### Couche domaine

`SensorWaterTemp` / `SensorAirTemp` deviennent `SensorTemperature1..4` sur `analogInputSlot(4..7)`, suffixes HA `io_temp1..4`. `DomainSlotCount` passe de 22 à 24 (valeur documentaire : elle n'est référencée nulle part et ne dimensionne aucun tableau).

`kAnalogHaSlots` passe de 17 à `Limits::Io::MaxAnalogEndpoints` (32) : le plafond historique tronquait silencieusement les capteurs auto-provisionnés dès qu'on activait POWERMON + BME680 + SHT40.

### Couche métier

`wat_temp_io_id` / `air_temp_io_id` (module `poollogic/sensors`) deviennent le **seul** endroit qui décide du rôle. Ils existaient déjà, avec leur sélecteur UI (`enum_set: flowio_logical_input_analog`, réétiqueté dynamiquement par `app.js` avec le nom réel de chaque endpoint) ; seuls leurs défauts ont été recalés sur `SensorTemperature1` / `SensorTemperature2`.

Les valeurs RuntimeUI `pool.water_temp` / `pool.air_temp` migrent d'IOModule vers PoolLogicModule. `RuntimeUiId = moduleNumeric × 100 + valueId`, donc **les ids passent de 2201/2202 à 2406/2407** ; les clés textuelles sont inchangées. Consommateurs mis à jour : `WebInterfaceServer.cpp`, `data/webinterface/app.js` (`calibrationSensorDefs`), les textes de `TFTModuleS3`.

⚠️ Les tuiles de dashboard Supervisor persistent un RuntimeUiId en NVS (`ic_d%u_rt`) : une tuile réglée sur 2201 doit être repointée sur 2406.

### Home Assistant

Les 4 slots IO publient `io_temp1..4`. PoolLogic republie **les mêmes suffixes historiques** `io_wat_tmp` / `io_air_tmp` depuis un nouveau snapshot `rt/poollogic/temp` (`{"wat":…,"air":…}`, `null` quand la sonde est absente → entité `unavailable`). Aucun tableau de bord HA existant n'est cassé, aucune entité orpheline.

### Index DataStore résolus par IoId

`IOEndpointRuntime` porte désormais l'`ioId` du slot occupant la case, publié par `IOModule::publishEndpointIoIds_()` une fois le registre figé. Nouveaux accesseurs dans `IORuntime.h` : `ioEndpointFloatByIoId` / `…BoolByIoId` / `…IntByIoId` / `ioEndpointIndexByIoId`. Tous les littéraux (0, 1, 2, 4, 5, 9) de `IOModuleSnapshots.cpp`, `WebInterfaceServer.cpp` et `I2CCfgServerModule.cpp` sont remplacés. `DATAKEY_IO_BASE + idx` et `runtimeSnapshotAffectsKey` restent indexés par registre, inchangés.

## Points de vigilance

- **L'effacement NVS est obligatoire**, pas optionnel. Une NVS contenant `io_a06bp` / `io_a07bp` auto-bindés à SHT40/BMP280 empêche les ports 122/123 de se lier à a06/a07 : les sondes partent sur d'autres slots, mal nommées, **sans aucun message d'erreur**.
- Les backends I2C optionnels (tous désactivés d'usine) s'auto-bindent désormais à partir de **a08** au lieu de a06 — décalage de nommage uniquement.
- `analogProviderSourceForSpec_` **borne** le canal DS18B20 : un canal hors plage, venu d'un profil ou de la config NVS, déborderait le pool de providers.
- `Ds18b20Driver` conserve le pointeur d'identifiant passé au constructeur : les ids de driver doivent rester des littéraux statiques.
- Le nom d'un slot tient dans 24 octets, accents UTF-8 compris (`IOAnalogSlotConfig::name`).
- Pas de migration NVS sur cette branche : la rupture des clés `io_dswr` / `io_dsar` est assumée.

## Vérification

Build `Waveshare-ESP32-S3` SUCCESS — RAM 34,7 %, flash 47,1 % (1 973 554 / 4 194 304 o).

`Supervisor` et `FlowIO` restent en échec de build pour des raisons **préexistantes** et indépendantes (dérive du core Arduino-ESP32 : `ledcAttach`, `xTaskCreatePinnedToCoreWithCaps`, `NetworkEvents.h`, `RuntimeData::pool`) — vérifié à l'identique sur un worktree du commit de référence.

À contrôler sur matériel :

1. Série : `Analog map a04…a07 binding_port=120…123`, `DS18B20 detectee bus=… rom=…`, `DS18B20 romN auto-affectee`.
2. Intervertir physiquement deux sondes → les valeurs suivent la ROM, pas le bus. Échanger `rom1` et `rom2` dans la config → échange immédiat.
3. Mettre `io/input/a03/binding_port` à « Non connecté », rebooter → `pool.water_temp` reste sur la bonne sonde (ce scénario échouait avant).
4. MQTT : `rt/io/input/a04`…`a07`, `rt/poollogic/temp`.
5. HA : `io_temp1..4` découverts, `io_wat_tmp` / `io_air_tmp` toujours présents et suivant le choix métier. Vérifier qu'on reste sous `MaxSensors = 48`.
6. Aucun test unitaire ne couvre l'IO ; le seul répertoire Unity (`test/test_poollogic_filtration_window`) n'a pas d'environnement PlatformIO hôte déclaré et n'a pas pu être exécuté.
