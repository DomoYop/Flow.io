# Du capteur à la mesure : comment se combinent carte, domaine et E/S

> **⚠ Page partiellement obsolète depuis la refonte E/S de juillet 2026**
> (voir [io-refonte-architecture.md](io-refonte-architecture.md)). Les concepts
> (4 couches, `bindingPort` seul reconfigurable au runtime) restent valables,
> mais les tables citées ont changé :
> - `kBindingPorts[]` porte désormais directement `{portId, backend, channel,
>   flags, name}` — plus de `kind` ni de switch de résolution (les capacités
>   par backend sont dans `src/Modules/IOModule/IoBackendTraits.h`).
> - `kDomainSlots[]` + `kDomainIoSlots[]` + les specs HA sont fusionnés en une
>   table unique `kPoolRoles[]` (`src/Domain/Pool/PoolDomain.h`).
> - Les défauts `kAnalogRoleDefaults`/`kDigital*RoleDefaults` restent par
>   profil, avec des structs partagées (`src/Domain/IoRoleDefaults.h`).
> - La construction des endpoints et la discovery HA sont mutualisées dans
>   `src/Domain/Pool/PoolIoAssembly.cpp` et `PoolIoHaDiscovery.cpp`.
> - Ajouter une sonde = 1 ligne dans `kPoolRoles` + 1 ligne de default par
>   profil ; ajouter un backend = 1 valeur d'enum + 1 ligne de traits + le
>   driver.

Cette page répond à une question précise : **où et comment décide-t-on quel capteur
physique alimente quelle mesure** (pH, ORP, température d'eau…) sur flow.io ?

La réponse tient en une distinction : la **carte** ne fait que *dimensionner* (combien
d'E/S), tandis que le **rattachement capteur → mesure** se joue en 4 couches, dont une
seule est réellement reconfigurable à l'exécution.

Les exemples ci-dessous portent sur le profil **Waveshare ESP32-S3**
(`FLOW_BOARD_WAVESHARE_ESP32_S3`), source de vérité runtime actuelle.

---

## 1. La carte : des *capacités*, pas du câblage

Dans [`src/Board/WaveshareBoard.h`](../../src/Board/WaveshareBoard.h) :

```cpp
IoCapacitySpec kWaveshareESP32S3IoCapacity{32, 8, 16, 32, 8, 16};
```

Ces valeurs ne disent **rien** sur « quel capteur fait quoi ». Ce sont uniquement des
**tailles de tableaux statiques** (compile-time), dans l'ordre :

| Champ | Valeur | Rôle |
|---|---:|---|
| `analogEndpoints` | 24 | nb max d'entrées analogiques physiques (= pire cas des ports déclarés : 8 rôles + 8 POWERMON + SHT40 + BMP280 + BME680) |
| `digitalInputs` | 8 | nb max d'entrées TOR |
| `digitalOutputs` | 16 | nb max de sorties TOR |
| `analogConfigSlots` | 24 | nb max de *slots de config* analogiques (endpoints nommés/calibrés) |
| `digitalInputConfigSlots` | 8 | idem entrées TOR |
| `digitalOutputConfigSlots` | 16 | idem sorties TOR |

De même, `MqttCapacitySpec`, `HaCapacitySpec`, les specs UART/I2C… sont des **plafonds**
qui bornent des tableaux. C'est du *« combien »*, jamais du *« qui fait quoi »*.

> ⚠️ Dépasser une capacité n'est **pas** une erreur de build : c'est une **troncature
> silencieuse** (cf. [CLAUDE.md](../../CLAUDE.md) « Capacités statiques »).

---

## 2. Les 4 couches E/S

C'est ici que se joue le rattachement capteur → mesure. On lit la chaîne de gauche à droite :

```
DRIVER            →  BINDING PORT        →  IO SLOT (endpoint)     →  DOMAIN SLOT
(backend HW)         (canal physique)       (nommé, calibré)          (rôle métier)
IO_BACKEND_ADS…      PortAdsExternal0       analogInputSlot(1)        SensorPh
```

### Couche A — Driver (backend matériel)

`IoBackend` dans [`src/Core/Services/IIO.h`](../../src/Core/Services/IIO.h) :

`GPIO=0, PCF8574=1, ADS1115_INT=2, ADS1115_EXT_DIFF=3, DS18B20=4, SHT40=5, BMP280=6,
BME680=7, POWERMON=8, TCA9554=9, MCP23017=10`.

C'est *comment* le firmware parle au silicium (INA226/INA228 unifiés sous `POWERMON`).

### Couche B — Binding port (le canal physique)

Dans [`src/Profiles/Waveshare/WaveshareIoLayout.h`](../../src/Profiles/Waveshare/WaveshareIoLayout.h),
le tableau `kBindingPorts[]` liste chaque canal atteignable :

```cpp
{ PortAdsExternal0, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 0 /*param0 = canal*/, 0 }
```

Le champ **`kind`** est la liaison *driver ↔ port câblée en dur* : le `switch(kind)` de
`IOModule::resolveAnalogBinding_` / `resolveDigitalBinding_`
([`src/Modules/IOModule/IOModule.cpp`](../../src/Modules/IOModule/IOModule.cpp)) traduit
`kind → (backend, channel)`. **C'est ce qui n'est pas reconfigurable au runtime** : un port
ADS restera ADS. (La table est dupliquée côté web dans `waveshareIoPortBackendChannel_`.)

### Couche C — IO slot / endpoint (l'objet configurable)

Un endpoint = une entrée **nommée**, **calibrée** (`c0`/`c1`), rattachée à **un** binding
port via son champ `bindingPort`. Cette valeur est **persistée en NVS** (Config Store,
clés type `IO_A*BP`). C'est le **premier maillon modifiable par l'utilisateur** :
« l'endpoint analogique n°1 lit le port `PortAdsExternal0` ».

### Couche D — Domain slot (le rôle métier)

Dans [`src/Domain/Pool/PoolDomain.h`](../../src/Domain/Pool/PoolDomain.h), `kDomainSlots`
définit les **mesures comme des rôles**, et `kDomainIoSlots` fige **« quelle mesure = quel
slot »** :

```cpp
{PoolIds::SensorOrp,         analogInputSlot(0)},
{PoolIds::SensorPh,          analogInputSlot(1)},
{PoolIds::SensorPressure,    analogInputSlot(2)},
{PoolIds::SensorSpareAnalog, analogInputSlot(3)},
{PoolIds::SensorTemperature1,analogInputSlot(4)},
{PoolIds::SensorTemperature2,analogInputSlot(5)},
```

Le pH est **toujours** le slot analogique 1. Ce mapping rôle → slot est **en dur**.

Exception depuis la refonte des températures : les 4 slots de température sont **génériques**, et le rôle métier eau/air est un réglage de PoolLogic (`wat_temp_io_id` / `air_temp_io_id`), pas une identité de slot — voir [temperatures-slots-generiques.md](temperatures-slots-generiques.md).

---

## 3. Où se décide concrètement « ce capteur → cette mesure »

Il y a **deux** endroits, à **deux moments** :

### (a) Valeur d'usine, à la compilation

`kAnalogRoleDefaults[]` dans
[`WaveshareIoLayout.h`](../../src/Profiles/Waveshare/WaveshareIoLayout.h) donne le **binding
port par défaut** de chaque rôle :

```cpp
{PoolIds::SensorPh, analogPortFromLegacy(FLOW_WIRDEF_IO_A1S, FLOW_WIRDEF_IO_A1C),
 FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P}, // pH
```

`WaveshareIoAssembly.cpp` applique ces défauts au boot pour **créer les endpoints**.
(Équivalents pour les E/S TOR : `kDigitalInputRoleDefaults` et `kDigitalOutputRoleDefaults`,
p. ex. `ActuatorFillPump → PortExio5`.)

### (b) Valeur effective, à l'exécution

Le champ `bindingPort` de l'endpoint dans le **Config Store / NVS**. C'est le **seul maillon
reconfigurable** : on peut décider que le pH ne lit plus `PortAdsExternal0` mais
`PortAdsInternal2`, et ajuster la calibration `c0`/`c1`. On **ne peut pas** changer *quel slot
est le pH* (couche D, figée) ni *quel driver sert un port* (`kind`, figé).

### Chaîne complète (exemple pH)

```
SensorPh (rôle, PoolDomain)
   └→ analogInputSlot(1)                        kDomainIoSlots  — mesure→slot, figé
        └→ endpoint.bindingPort = PortAdsExternal0
                                                 kAnalogRoleDefaults (défaut) → NVS (effectif)
             └→ kind = ADS_EXTERNAL_DIFF, canal 0
                                                 kBindingPorts   — câblé en dur
                  └→ IO_BACKEND_ADS1115_EXT_DIFF
                                                 resolveAnalogBinding_
```

---

## 4. Récapitulatif

| Question | Défini où | Modifiable ? |
|---|---|---|
| Combien d'E/S / MQTT / HA ? | `IoCapacitySpec` & co. ([`WaveshareBoard.h`](../../src/Board/WaveshareBoard.h)) | Non (compile-time) |
| Quel driver pour quel port ? | `kind` de `kBindingPorts[]` + `switch` de `resolveAnalogBinding_` | Non (en dur) |
| Quelle mesure = quel slot ? | `kDomainSlots` / `kDomainIoSlots` ([`PoolDomain.h`](../../src/Domain/Pool/PoolDomain.h)) | Non (en dur) |
| **Quel capteur physique alimente une mesure ?** | `endpoint.bindingPort` — défaut dans `kAnalogRoleDefaults`, valeur courante en **NVS** | **Oui** (config) |

En une phrase : **« quel capteur pour quelle mesure » = le `bindingPort` de l'endpoint
associé au rôle**, dont la valeur d'usine vit dans `kAnalogRoleDefaults`
([`WaveshareIoLayout.h`](../../src/Profiles/Waveshare/WaveshareIoLayout.h)) et la valeur
effective en NVS / Config Store.

---

## Un cinquième index, à ne pas confondre avec les autres

Le DataStore ajoute un index de plus, qui ne fait partie d'aucune des 4 couches :
la position dans `IORuntimeData::endpoints[]`. C'est un **index de registre**,
donné par l'ordre d'insertion dans `IORegistry` — et seuls les slots dont le
binding est résolu y entrent (`configureAnalogSlots_` fait `continue` sur les
autres). Un slot analogique sans binding décale donc **tous les suivants** :
avec `a00` non bindé, `a01` occupe la case 0.

Toute lecture ou écriture doit donc résoudre par `IoId`
(`ioEndpointIndexByIoId`, `endpointIndexFromId_`), jamais par l'index de slot.
`forceAnalogSnapshotPublish_` ne le faisait pas : un changement de précision
d'affichage écrivait la valeur du slot dans la case d'un autre endpoint, qui la
republiait vers Home Assistant jusqu'à sa propre acquisition suivante
(**corrigé**).

## Voir aussi

- [Structure des profils, cartes, domaines et bootstrap](../core/profiles-board-domain-app.md)
- [IOModule](../modules/IOModule.md)
- [Adapter le projet à un autre domaine](../integration/adaptation-domaine.md)
