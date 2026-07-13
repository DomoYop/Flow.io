# Refonte de la chaîne E/S — architecture cible

> Document d'architecture (juillet 2026). Complète l'état des lieux de
> [analyse-simplification-architecture.md](analyse-simplification-architecture.md).
> Décisions actées : implémentation par étapes compilables ; **pas de compatibilité NVS**
> (cartes reconfigurées après flash) ; **base commune Waveshare + FlowIO** (FlowIO migré,
> validé à la compilation seulement — son link est cassé en amont).

## Principe

La chaîne actuelle a 4 couches conceptuelles saines mais 6-7 tables/switch redondants.
La cible : **une seule table par couche, zéro duplication**, sans changer le contrat
`IOServiceV2`/`IoId` ni les identifiants exposés (jsonName `a07_c0`, module `io/input/a07`,
topics HA/MQTT, `binding_port` de l'API web).

```
Driver (backend HW)
  └─ kBackendTraits[backend]     ← 1 table constexpr : label, direction, analog, maxChannel
       └─ kBindingPorts[]        ← 1 table par carte : {portId, backend, channel, flags, name}
            └─ endpoint          ← NVS (généré) : nom, bindingPort, calibration/params
                 └─ kPoolRoles[] ← 1 table métier : rôle → slot + identité + HA
```

Couche runtime : seul `bindingPort` (+ calibration/params) reste reconfigurable en NVS —
c'était déjà le cas, mais la déclaration ne passe plus que par une table par niveau.

## A. Un seul enum matériel + table de traits

- `IoBackend` (`src/Core/Services/IIO.h`) reste **la** vérité matérielle (11 valeurs).
  **Supprimés** : `IOBindingPortKind` (14 valeurs) et `IOAnalogSource` public (8 valeurs).
- Nouveau `src/Modules/IOModule/IoBackendTraits.h` :

```cpp
enum IoPortDirMask : uint8_t { IO_PORT_DIR_IN = 1, IO_PORT_DIR_OUT = 2 };

struct IoBackendTraits {
    uint8_t     backend;            // IO_BACKEND_*
    const char* label;              // "ADS1115 int" (affichage)
    const char* kindLabel;          // "ads1115_internal" (API JSON, ex-waveshareIoPortKindLabel_)
    uint8_t     dirMask;            // IN / OUT / IN|OUT (GPIO)
    bool        analog;
    uint8_t     maxChannel;         // borne de validation du canal
    bool        configurableToggle; // pour IOServiceV2::backendInfo
};
inline constexpr IoBackendTraits kBackendTraits[] = { /* 11 entrées indexées par backend */ };
```

- `IOBindingPortSpec` devient `{portId, backend, channel, flags(dir), name}`.
  Le champ `name` ("DIN0", "EXIO1", "Relay 1"…) supprime les switch ordinal↔port des
  assemblies. **DS18** : `channel = busIndex` (0/1) ; le GPIO du bus vit déjà dans
  `IOModuleConfig.oneWire*Gpio` (l'ancien `param0`=47/48 de Waveshare était décoratif).
- La résolution port→(backend, canal) devient une lecture directe du spec validée par les
  traits. Supprimés : les 3 switch `resolve*Binding_` d'`IOModule.cpp` et leurs copies web
  (`waveshareIoPortBackendChannel_`, `waveshareIoPortKindLabel_`, `waveshareIoBackendLabel_`).
- Interne IOModule : `analogProviderIndex(backend, busIndex)` (privé) indexe
  `analogProviders_[8]` à la place de l'enum public.

## B. Fusion Definition/SlotConfig

Les paires `IO*Definition`/`IO*SlotConfig` sont fusionnées : une struct d'identité commune

```cpp
struct IOEndpointRegistration {
    IoId ioId; const char* name; PhysicalPortId defaultPort;
    /* callbacks valeur/counter + ctx */
};
```

et les `IO*SlotConfig` existantes comme défauts :
`defineAnalog / defineDigitalInput / defineDigitalOutput(const IOEndpointRegistration&, const *SlotConfig&)`.
`pulseMs` unifié en `uint16_t`.

## C. Persistance : ConfigVariable générées pour tous les slots

**Décision — pas de blob NVS par endpoint** : `read/writeRuntimeBlob` contournent
`toJson`/`applyJson`/EventBus, un schéma blob casserait l'export JSON, la page web de config
et les routes config MQTT. On garde des `ConfigVariable`, mais **générées uniformément**
(clés NVS comprises — la liberté NVS sert à ça).

- Nouveau `src/Modules/IOModule/IoSlotConfigVars.h` : générateur unique sur le pattern
  existant `ExtraAnalogConfigVars` (buffers possédés remplis par `snprintf`, placement-new
  en PSRAM au boot). 32 analog × 5 vars + 16 DI × 10 + 16 DO × 7 = 432 vars
  (budget `MaxConfigVars` = 768 → OK).
- **Invariants préservés** : jsonName (`a07_c0`, `binding_port`…), moduleName
  (`io/input/a07`, `io/output/d08`…), ordre d'enregistrement analog→DI→DO (ordre d'export
  JSON), attribution des `localBranchId` MQTT par slot, flux
  `persistCounterTotalIfNeeded_` → `persistFloatValue` pour les compteurs.
- Supprimés : ~250 lignes de membres ConfigVariable manuscrits + structs `Extra*ConfigVars`
  d'`IOModule.h`, ~290 clés slots de `include/Core/NvsKeys.h`, les planchers
  `*_CFG_STORAGE_SLOTS` et les `static_assert` legacy de `SystemLimits.h`.

## D. Table de rôles unifiée (Domain)

Nouveau `src/Domain/Pool/PoolRoles.h` :

```cpp
struct PoolRoleSpec {
    DomainSlotId roleId;
    IoSlotId     ioSlot;         // ex-kDomainIoSlots (avec le #if WAVESHARE de permutation DIN)
    uint8_t      runtimeIndex;   // index DataStore/RuntimeUI — ≠ ioSlotIndex (SensorPoolLevel:
                                 // runtimeIndex=6 mais ioSlot=digitalInputSlot(2) sur Waveshare)
    const char*  endpointId;     // "io_flt_pmp"
    const char*  displayName;    // "Filtration Pump"
    const char*  haObjectSuffix; // ex-kAnalogHaSpecs / kDigitalHaSpecs
    const char*  haIcon;
    const char*  haUnit;
};
inline constexpr PoolRoleSpec kPoolRoles[18] = { ... };
```

Remplace `kDomainSlots` + `kDomainIoSlots` + `kAnalogHaSpecs`/`kDigitalHaSpecs`.
`kPoolDevices` reste (sémantique équipement réellement distincte).

Défauts de brochage : structs `RolePortDefault` (analog/din/dout) définies **une fois** dans
`src/Domain/IoRolePortDefaults.h` ; les **données** restent par profil (ports, polarités
`activeHigh`, `momentary` diffèrent réellement entre Waveshare et FlowIO).

## E. Assembly + HA discovery partagés

Nouveaux `src/Domain/Pool/PoolIoAssembly.{h,cpp}` et `PoolIoHaDiscovery.{h,cpp}` :

```cpp
struct PoolIoProfileSpec {
    const IOBindingPortSpec* ports;         uint8_t portCount;
    const RolePortDefault*   roleDefaults;  uint8_t roleDefaultCount;
    const IOEndpointRegistration* extraInputs;  uint8_t extraInputCount;  // DIN libres
    const IOEndpointRegistration* extraOutputs; uint8_t extraOutputCount; // COMP01..08
};
```

Construction des endpoints depuis `kPoolRoles` + le spec de profil ; noms par défaut depuis
`port->name`. La machinerie HA discovery (heap one-shot + libération) est mutualisée via un
`PoolIoHaContext`. `WaveshareIoAssembly.cpp` (673 l.) et `FlowIOIoAssembly.cpp` (510 l.)
tombent à ~80 lignes chacune (tableaux extra + appels).

## Hors périmètre (justifié)

- **Interface driver uniforme de polling** : les 4 ticks (`tickFastAds_`, `tickSlowDs_`,
  `tickI2cAnalogs_`, `tickDigitalInputs_`) encodent des cadences et politiques d'erreur
  spécifiques (ADS rapide vs DS18 ≥750 ms) ; une table `{begin, poll, read}` coûterait des
  indirections et un risque de régression temporelle pour ~100 lignes. À réévaluer après
  stabilisation.
- **Contrat `IOServiceV2`/`IoId`** : inchangé, aucun besoin identifié.
- **Renumérotation des `PhysicalPortId`** (100/110/120…) : exposés dans l'API web, lisibles,
  aucun gain.

## Étapes et validation

Sept étapes, chacune compilable et committée — le détail (fichiers, volumes estimés,
risques) est dans le plan d'exécution. Validation par étape :
`pio run -e Waveshare-ESP32-S3` (+ relevé flash, marge initiale ~93 %) et
`pio run -e FlowIO` en n'acceptant que l'échec de link préexistant. À l'étape finale :
comparaison du JSON `/api/io/summary` et de l'export JSON de config avant/après.

## Points de vigilance

- DS18 2 bus : ROM cache NVS (`resolveDsBusAddress_`) et `resolveDs18Sensors_` inchangés.
- POWERMON : `maxChannel = 7` dans les traits ; l'inactivité des canaux 5-7 en INA226 reste
  gérée au runtime (ne pas la coder dans les traits).
- Compteurs d'impulsions : conserver l'init `configTotal + rawCount*c0` de
  `configureRuntime_`.
- `IO_SVC_MAX_ENDPOINTS = 40` : ne pas augmenter sans vérifier `IoCycleInfo`/snapshots.
- FlowIO sans PSRAM : les capacités réduites du board profile bornent le coût RAM du
  générateur de vars.
