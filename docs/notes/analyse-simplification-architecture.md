# Analyse de simplification de l'architecture — état des lieux et recommandations

> Note d'analyse (juillet 2026). **Aucun refactor appliqué** : ce document est un état des lieux
> chiffré de la complexité du code, avec des recommandations hiérarchisées pour plus tard.
> Périmètre décidé : les profils autres que Waveshare sont conservés tels quels.

## TL;DR

L'architecture de fond est **saine et intentionnelle** : micro-noyau de modules avec séquenceur
de démarrage non bloquant, services C sans vtable/RTTI, stores statiques sans allocation
dynamique, chaîne E/S en couches dont une seule (le `bindingPort` en NVS) porte la
reconfigurabilité runtime. La complexité *ressentie* vient surtout de **duplications
accumulées** — tables redondantes de la chaîne E/S, doubles implémentations module/web,
quasi-fork Waveshare↔FlowIO, deux styles de trampolines de services — et non d'un défaut
de conception. Les recommandations en fin de document s'attaquent à cet accidentel sans
toucher au design.

## 1. Vue d'ensemble chiffrée

`src/` = **340 fichiers `.cpp`/`.h`, ~80 240 lignes** :

| Zone | Fichiers | Lignes | Part |
|---|---|---|---|
| `src/Modules` | 205 | 66 203 | 82,5 % |
| `src/Core` | 77 | 8 483 | 10,6 % |
| `src/Profiles` | 28 | 3 416 | 4,3 % |
| `src/Board` | 10 | 1 283 | 1,6 % |
| `src/Domain` | 13 | 527 | 0,7 % |
| `src/App` | 6 | 314 | 0,4 % |

Top 5 des plus gros fichiers :

| Lignes | Fichier |
|---|---|
| 7 462 | `src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp` |
| 3 687 | `src/Modules/IOModule/IOModule.cpp` |
| 3 031 | `src/Modules/HMIModule/HMIModule.cpp` |
| 2 447 | `src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.cpp` |
| 2 414 | `src/Modules/Network/TimeModule/TimeModule.cpp` |

Autres ordres de grandeur : **36 modules** concrets (25 `Module` actifs + 11 `ModulePassive`),
**22 `ServiceId`**, **5 profils** compilables, **8 environnements** PlatformIO, **~3 240 lignes**
de scripts Python de génération, MQTTModule ≈ 3 700 lignes réparties sur 15 fichiers (le module
le plus lourd hors web).

## 2. Chaîne E/S : 4 couches conceptuelles, 6-7 tables en pratique

C'est le cœur de la sensation de complexité. La chaîne documentée
(`docs/notes/io-mapping-capteur-mesure.md`) compte 4 couches :
`Driver → Binding port → IO slot/endpoint → Domain slot`. En pratique, une mesure traverse
**6 tables/switch distincts**, et un actionneur une 7e (`kPoolDevices[]`).

Exemple de la sonde pH (profil Waveshare) :

```
ADS1115 externe (HW)                              Ads1115Driver
  → port 110 (kind=ADS_EXTERNAL_DIFF, param0=0)   kBindingPorts[]           (figé)
  → kind → backend/canal                          resolveAnalogBinding_     (switch figé)
  → endpoint a01 : bindingPort=110 + c0/c1        kAnalogRoleDefaults[] + NVS (seule partie runtime)
  → SensorPh ↔ analogInputSlot(1)                 kDomainIoSlots[]          (figé)
  → "pH", runtimeIndex 1                          kDomainSlots[]            (figé)
  → PoolLogic
```

### Redondances constatées

1. **Double implémentation de la résolution port → (backend, canal)** :
   `IOModule::resolveAnalogBinding_` ([IOModule.cpp:2169](../../src/Modules/IOModule/IOModule.cpp))
   et `resolveDigitalOutputBinding_` (l. 2233) sont réimplémentées à l'identique dans
   `waveshareIoPortBackendChannel_`
   ([WebInterfaceServer.cpp:2894](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp)),
   plus un switch de libellés `waveshareIoPortKindLabel_` (l. 2861). Tout nouveau backend doit
   être ajouté aux deux (trois) endroits — dérive silencieuse garantie à terme.

2. **Trois tables keyées sur le même rôle métier** encodant des fragments du même mapping :
   - `kDomainSlots[].runtimeIndex` ([PoolDomain.h:10-29](../../src/Domain/Pool/PoolDomain.h)) ;
   - `kDomainIoSlots[]` (PoolDomain.h:31-57) — redonne le même index de slot ;
   - `kAnalogRoleDefaults[]` / `kDigital*RoleDefaults[]` (`src/Profiles/<Profil>/*IoLayout.h`)
     — le bindingPort d'usine, **dupliqué par profil**.
   Le pH est ainsi « slot analogique 1 » à trois endroits qu'il faut garder cohérents à la main.

3. **Trois enums parallèles pour le même matériel** :
   `IoBackend` (11 valeurs, [IIO.h:55-66](../../src/Core/Services/IIO.h)),
   `IOAnalogSource` (8 valeurs, [IOModuleTypes.h:68-77](../../src/Modules/IOModule/IOModuleTypes.h)),
   `IOBindingPortKind` (14 valeurs, IOModuleTypes.h:82-96). Un backend analogique existe dans
   les trois ; les switch de résolution ne font que traduire l'un en l'autre.

4. **Structs quasi-jumelles** : `IOAnalogDefinition` vs `IOAnalogSlotConfig`,
   `IODigitalOutputDefinition` vs `IODigitalOutputSlotConfig` (IOModuleTypes.h:132-174).

5. **ConfigVar mixte main/macro** : dans [IOModule.h](../../src/Modules/IOModule/IOModule.h),
   les slots analogiques 6-31 et sorties 8-15 sont générés par macro, mais les slots 0-5 et
   sorties 0-7 sont déclarés champ par champ (~250 lignes énumérées à la main). Le split est
   historique, pas fonctionnel.

6. **Switch ordinal↔port faits main** (`digitalInputOrdinalFromPort`,
   `waveshareCompOutputPort`… dans `WaveshareIoAssembly.cpp:174-233`) qui ré-encodent une
   information déjà présente dans `kBindingPorts[]`.

### Coût d'ajout mesuré

- **Nouvelle sonde analogique** (backend existant) : ~6 fichiers à toucher
  (`PoolIds.h`, 2 tables de `PoolDomain.h`, `*IoLayout.h` **× 2 profils**, `kAnalogHaSpecs`,
  consommateur métier).
- **Nouveau backend matériel** : ~10 endroits, dont les 3 enums, les 2 (3) switch de
  résolution, `kBindingPorts[]` × profils, le driver et son wiring dans `IOModule.cpp`.

### Ce qui est justifié (à ne pas « simplifier »)

- Le **`bindingPort` persisté en NVS** : c'est la seule couche runtime, et c'est elle qui
  permet de rebrancher une sonde sur un autre port + recalibrer sans reflasher. À préserver.
- L'**indirection kind → backend/canal** : nécessaire pour que plusieurs endpoints partagent
  un même provider physique (un ADS1115, un INA22x).
- Les **capacités compile-time** (`IoCapacitySpec`, `MqttCapacitySpec`) : dimensionnement
  statique cohérent avec la contrainte no-heap ESP32.
- L'unification récente INA226/INA228 → `POWERMON` va exactement dans le bon sens.

## 3. Core : bon design, boilerplate évitable

- **`ServiceRegistry::get<T>` est un `reinterpret_cast` aveugle**
  ([ServiceRegistry.h:26](../../src/Core/ServiceRegistry.h)) : rien ne lie un `ServiceId` au
  type de sa struct. `get<WifiService>(ServiceId::Mqtt)` compile sans broncher. C'est le point
  faible structurel du pattern, alors que l'enum promettait un typage fort.
- **Deux styles de trampolines coexistent** : le moderne `ServiceBinding::bind<&Method>`
  (`src/Core/ServiceBinding.h`, ~140 usages, garde `nullptr` centralisée) et l'ancien
  `static_cast<T*>(ctx)` écrit à la main dans **18 fichiers** (`IOProviders.h`,
  `PoolLogicControl.cpp`, `MQTTProducers.cpp`, `BootLogCaptureModule.cpp`…). Chaque cast manuel
  est un UB silencieux si la mauvaise instance est branchée.
- **ConfigStore** (~1 240 l., le plus gros bloc Core) : le même `switch(ConfigType)` à 7 cas
  avec casts `void*` est répété ~4 fois (`set`, `loadPersistentVar`, `put_`, `toJson`) —
  duplication classique d'un variant maison. Double représentation
  `ConfigVariable<T>`/`ConfigMeta` de la même donnée.
- **ModuleManager** : tri topologique de Kahn en O(n²) qui n'exploite pas l'index
  `modulesById[]` pourtant présent ; trois tableaux parallèles (`modules[]`, `modulesById[]`,
  `ordered[]`) ; `startupStartedMask_` en `uint32_t` = **limite silencieuse à 32 modules**
  (on est à 36 modules déclarés, tous profils confondus — à surveiller par profil) ;
  `startModule_` = 133 lignes mêlant séquencement, FreeRTOS et logging heap dupliqué.
- **EventBus** : le cœur subscribe/post/dispatch tient en ~100 lignes ; ~70 % des 461 lignes
  du `.cpp` sont des compteurs/télémétrie/warnings throttlés. Utile en debug embarqué, mais
  ça noie la logique.
- **MQTTModule** : le système de jobs (priorités, retry/backoff, dédup) répond à de vrais
  besoins ; l'accidentel est ailleurs — 3 ring buffers templatés sur des capacités distinctes
  au lieu d'un type unique, quadruple boilerplate producteur en trampolines manuels, IDs
  magiques non centralisés (`ProducerIdConfig=41`, `StatusMsgRuntimeBase=32`…).

## 4. Profils et build

- **Waveshare est un quasi-fork de FlowIO** : ~450 lignes strictement identiques entre
  `WaveshareIoAssembly.cpp` (673 l.) et `FlowIOIoAssembly.cpp` (510 l.) ; bootstraps recopiés
  à quelques lignes près (`buildNetworkSnapshot`, `configurePoolDevices`, `postInit`…) ;
  `IoLayout.h` largement dupliqués. Toute correction doit être portée deux fois.
- **>17 000 lignes de modules hors périmètre Waveshare** (Micronova, SupervisorHMI,
  TFTModuleS3, I2CCfg client/serveur, FlowConnectDisplay…) toujours maintenues et filtrées.
  Décision actée : on les conserve — mais c'est le premier poste de volume du dépôt.
- **`platformio.ini` (454 l.)** : `build_flags` et `lib_deps` recopiés quasi intégralement
  dans chaque env au lieu d'un socle `[env]`/`extends` ; `build_src_filter` **par
  soustraction** (chaque env part de `+<*>` et doit connaître/exclure tous les autres profils).
- **`src/App/BuildFlags.h`** : matrice de gardes 5×5 (10 paires interdites) qui croît en
  O(n²) avec les profils.
- **Incohérence à noter** : `platformio.ini:2` a `default_envs = FlowIO` alors que la cible
  de référence déclarée (CLAUDE.md, quality-gates) est `Waveshare-ESP32-S3` — et l'env FlowIO
  ne linke plus. Un `pio run` sans `-e` échoue donc par défaut.
- **Doc en retard** : `docs/core/profiles-board-domain-app.md` décrit 2 firmwares /
  4 environnements ; la réalité est 5 profils / 8 environnements (ni Waveshare, ni Micronova,
  ni FlowConnectDisplay n'y figurent).

## 5. Recommandations hiérarchisées (non appliquées)

Classées par rapport gain/risque. Chaque item est indépendant et peut faire l'objet d'un
chantier court validé par `pio run -e Waveshare-ESP32-S3` (et la taille binaire — marge
flash ~93 %).

### P1 — Chaîne E/S (répond directement à la lourdeur drivers/capteurs/ioslots)

1. **Supprimer la double résolution web/module** : exposer la résolution de `IOModule` via le
   service `IIO` (ou un helper partagé dans `IOModuleTypes.h`) et supprimer
   `waveshareIoPortBackendChannel_` + le switch de libellés de `WebInterfaceServer.cpp`.
   Gain : un seul endroit à modifier par backend. Risque : faible.
2. **Fusionner les 3 tables keyées sur le rôle** : une seule table
   `{rôle, type de slot, index, nom, bindingPort d'usine}` par profil, dont dérivent
   `kDomainSlots`, `kDomainIoSlots` et `kAnalogRoleDefaults` (constexpr). Gain : ajouter une
   sonde = 1 ligne au lieu de 3-4 tables. Risque : moyen (touche Domain + 2 profils).
3. **Unifier les 3 enums matériels** : dériver `IOAnalogSource` et le couple
   (backend, canal) de `IOBindingPortKind` via une table constexpr unique
   `{kind → backend, source, maxChannel}` au lieu de switch. Risque : faible.
4. **Uniformiser les ConfigVar de `IOModule.h`** : étendre la macro existante aux slots 0-5
   et sorties 0-7 (~250 lignes gagnées). Risque : faible (vérifier que les clés NVS générées
   restent identiques — compat NVS obligatoire).

### P2 — Core : sûreté et déduplication

5. **Lier `ServiceId` au type** : un trait `ServiceTraits<ServiceId>::type` +
   `static_assert` dans `ServiceRegistry::get<T>` élimine le `reinterpret_cast` aveugle.
   Purement compile-time, zéro coût binaire. Risque : faible.
6. **Migrer les 18 fichiers de trampolines manuels vers `ServiceBinding::bind`**
   (MQTTProducers, IOProviders, PoolLogicControl en tête) : supprime la majorité des
   `static_cast<T*>(ctx)` à risque et du boilerplate. Risque : faible, mécanique.
7. **Factoriser le `switch(ConfigType)` de ConfigStore** en un dispatch unique
   (table ou template). Risque : moyen (cœur de la persistance — tester import/export JSON
   et migration NVS).
8. Divers ModuleManager : utiliser `modulesById[]` dans `buildInitOrder`,
   `static_assert(MaxModules <= 32)` pour rendre la limite du bitmask explicite,
   scinder `startModule_`. Risque : faible.

### P3 — Build

9. **Socle commun `[env]` + `extends`** dans `platformio.ini` (build_flags, lib_deps,
   extra_scripts partagés) et **filtre par addition** (`build_src_filter = +<Core/> +<App/>
   +<Profiles/X/> +<Modules/...>` par env) : chaque profil déclare ce qu'il inclut au lieu
   d'exclure tous les autres. Corriger au passage `default_envs = Waveshare-ESP32-S3`.
   Risque : moyen (vérifier que chaque env compile à l'identique — comparer les listes
   d'objets compilés avant/après).

### P4 — Hors périmètre (décision produit, non retenu à ce stade)

10. Factorisation d'un socle commun Waveshare/FlowIO (bootstrap + IoAssembly partagés),
    voire retrait des profils non maintenus. C'est le plus gros gisement (~20-25 % du code)
    mais il suppose de statuer sur l'avenir des matériels FlowIO/Supervisor/Micronova/
    FlowConnectDisplay. **Explicitement exclu de cette analyse à la demande du propriétaire.**

## Méthode

Analyse statique du dépôt au commit `2fc4502` (branche `flowio-waveshare-16mb-pioarduino`),
comptages par `find`/`wc`/`grep`, lecture ciblée de `src/Core/`, `src/Modules/IOModule/`,
`src/Profiles/`, `src/Domain/`, `platformio.ini` et des docs `docs/core/` + `docs/notes/`.
Les numéros de lignes cités correspondent à cet état du code.
