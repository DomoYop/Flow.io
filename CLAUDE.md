# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Vue d'ensemble

flow.io est un firmware ESP32 (PlatformIO + Arduino-ESP32) pour l'automatisation de piscine : régulation pH/ORP, filtration, chauffage, électrolyse/O2, supervision MQTT + Home Assistant, écran Nextion et interface web locale.

Un même arbre de sources compile **plusieurs firmwares** distincts, chacun sélectionné par un environnement PlatformIO et une macro de profil. Le périmètre de chaque firmware est découpé à la compilation via `build_src_filter` dans `platformio.ini` (les modules non pertinents sont exclus du build, pas seulement désactivés au runtime).

La branche de développement active (`flowio-waveshare-16mb-pioarduino`) cible principalement le profil **Waveshare ESP32-S3** (`[env:Waveshare-ESP32-S3]`), qui est la source de vérité runtime actuelle — voir [docs/core/module-quality-gates.md](docs/core/module-quality-gates.md).

## Commandes

PlatformIO en ligne de commande (`pio` est dans `~/.platformio/penv/`). Les environnements sont définis dans [platformio.ini](platformio.ini).

```bash
# Compiler un firmware
pio run -e Waveshare-ESP32-S3      # cible principale actuelle (ESP32-S3 16MB)
pio run -e FlowIO                  # ESP32 principal historique (logique métier + IO)
pio run -e Supervisor             # ESP32 de supervision (web, provisioning, OTA, TFT)
# autres envs : FlowConnectDisplay, Micronova, FlowIOWokwi, SupervisorWokwi, WaveshareWokwi

# Flasher / moniteur série (115200)
pio run -e Waveshare-ESP32-S3 -t upload
pio device monitor -b 115200

# Tests unitaires hote (Unity) — voir test/ ; l'env `native` n'a pas de board ni de framework
pio test -e native
```

Note Windows : le shell par défaut est PowerShell ; le tool Bash exécute du POSIX sh. `pio` peut nécessiter d'être appelé via le venv PlatformIO (`~/.platformio/penv/Scripts/pio` sous Windows, `~/.platformio/penv/bin/pio` sous Linux).

- **Lancer `pio` depuis PowerShell natif, pas depuis le Bash/MSys** : `esptool` rejette l'environnement MSys/Mingw (« MSys/Mingw is not supported »), ce qui casse notamment le flash/upload.
- **`pio run -e FlowIO` ne linke pas actuellement**, pour une raison préexistante et indépendante du code métier (dérive du core Arduino-ESP32 : `ledcAttach`, `xTaskCreatePinnedToCoreWithCaps`, `driver/rmt_encoder.h`). Utiliser `Waveshare-ESP32-S3` comme cible de référence pour valider un build.
- **`pio test` sans `-e native` ne fonctionne pas** : `default_envs = FlowIO` ferait cibler l'ESP32. L'env `native` compile uniquement les helpers purs (`FiltrationWindow`, `DosingController`) et demande un `g++` hôte sur le PATH — absent de la machine de dev Windows, le job GitHub Actions `native-tests` s'en charge. Corollaire : la section commune des firmwares ESP32 s'appelle **`[esp32_base]`** et non `[env]`, car une section `[env]` serait héritée par tous les envs, y compris `native`.
- **`pio run -e Supervisor` échoue de la même manière** (mêmes symboles Arduino-ESP32 manquants, plus `NetworkEvents.h` et `RuntimeData::pool` — `PoolDeviceRuntime.h` est compilé alors que `PoolDeviceModuleDataModel.h` est hors `build_src_filter`). Préexistant, vérifié sur commit de référence : ne pas l'attribuer à une modification en cours.

## Code généré — ne pas éditer à la main

Des scripts Python tournent **avant chaque build** (`extra_scripts` dans `platformio.ini`) et régénèrent du code et des assets. Ne modifiez jamais les fichiers générés directement ; modifiez la source puis recompilez.

- `generate_build_version.py` → macros de version/build
- `generate_datamodel.py` → `src/Core/Generated/ModuleDataModel_Generated.h` (agrège tous les `*ModuleDataModel.h` et `*Runtime.h` des modules **inclus** par le `build_src_filter` courant)
- `generate_runtimeui_manifest.py` → manifeste Runtime UI + lookup Supervisor dans `src/Core/Generated/`
- ⚠ `generate_module_i18n_en.py` (hors build, à lancer à la main) est **destructif** : il régénère intégralement les `i18n.en.json` et écrase les traductions anglaises correctes par du franglais. Éditer `i18n.en.json` à la main plutôt que de le lancer.
- `prepare_spiffs_data.py` → prépare l'image SPIFFS (`data/`), génère les cfgdocs segmentés (`data/wc/*.j`)
- `export_binaries.py` (post-build) → copie les `.bin`/`.tft` dans `binary/` et met à jour `binary/manifest.json`

Conséquence : ajouter un champ runtime ou un texte de config implique de régénérer (recompiler) ; le contenu de `src/Core/Generated/` et de `data/wc/` reflète l'état d'un build précédent.

## Architecture runtime

Détail complet : [docs/core/architecture.md](docs/core/architecture.md). Briques centrales (toutes dans `src/Core/`) :

- **`Module` / `ModulePassive` / `ModuleManager`** : cycle de vie. Le manager fait un tri topologique des dépendances puis libère chaque module progressivement (`init` → `ConfigStore::loadPersistent` → `onConfigLoaded` → séquenceur non bloquant → `onStart` → tâches FreeRTOS). Les délais de séquencement (`mqtt` 1.5s, `poollogic` 10s, `ha` 15s…) sont documentés dans l'architecture.
- **`ServiceRegistry`** : registre indexé par `ServiceId`. Les dépendances inter-modules passent par des **services C** = structs de pointeurs de fonctions + un champ `ctx` (`void*`, typiquement l'instance du module porteur). Le `ctx` n'est pas typé : un mauvais cast n'est pas détecté par le compilateur.
- **`ConfigStore`** : config persistante en NVS, export/import JSON, publie `EventId::ConfigChanged`.
- **`DataStore`** : état runtime partagé en RAM, publie `EventId::DataChanged`.
- **`EventBus`** : bus interne par queue (capacités bornées — voir docs).
- **`MQTTModule`** : transport job-based. Les modules enregistrent des producteurs et enqueuent des jobs `(producerId, messageId, priorité)` ; le module construit topic + payload à la publication et gère retries/backoff.

Les noms texte des logs (`mqtt`, `io`, `time.scheduler`…) sont des `toString(ServiceId)`. Le wiring réel passe par `ServiceId`, pas par les chaînes.

## Composition en couches : Profile / Board / Domain / App

Détail : [docs/core/profiles-board-domain-app.md](docs/core/profiles-board-domain-app.md). Pour situer une modification :

| Type de modification | Zone |
|---|---|
| broche / bus matériel | `src/Board/*` (`*Board.h`, `BoardSpec.h`) |
| rôle métier (mapping signal → fonction piscine) | `src/Domain/Pool/*`, `src/Domain/DomainSpec.h` |
| modules présents dans un produit | `src/Profiles/<Profil>/*` **et** `build_src_filter` dans `platformio.ini` |
| profil compilé | `platformio.ini` + `src/App/BuildFlags.h` |
| binding rôles ↔ ports IO | `src/Profiles/<Profil>/*IoLayout.h` et `*IoAssembly.cpp` |

- **`src/App/`** : `Bootstrap.cpp` résout le profil compilé (macros `FLOW_PROFILE_*`), construit `AppContext`, expose `ModuleManager`/`ServiceRegistry`/`ConfigStore`, appelle `setup`/`loop` du profil.
- **`src/Profiles/<Profil>/`** : `*Profile.cpp` (assemble BoardSpec + DomainSpec + identité + instances de modules), `*Bootstrap.cpp` (**ordre d'enregistrement** des modules, wiring de profil, enregistrement des providers runtime MQTT et Runtime UI). L'ordre d'enregistrement par profil est documenté dans la quality-gate et l'architecture.

### Chaîne E/S : de la broche au capteur métier

La carte ne fait que **dimensionner** (`IoCapacitySpec`, `MqttCapacitySpec`… = tailles de tableaux compile-time). Le rattachement « quel capteur physique pour quelle mesure » se joue en **4 couches** :

`Driver (backend HW)` → `Binding port (canal physique)` → `IO slot / endpoint (nommé, calibré)` → `Domain slot (rôle métier)`.

Seul le champ `bindingPort` de l'endpoint est **reconfigurable au runtime** (Config Store/NVS) ; le mapping driver↔port (`kind` dans `kBindingPorts[]`) et le mapping mesure↔slot (`kDomainIoSlots`) sont **figés en dur**. Les valeurs d'usine du binding sont dans `kAnalogRoleDefaults` / `kDigital*RoleDefaults` (`src/Profiles/<Profil>/*IoLayout.h`). Détail complet : [docs/notes/io-mapping-capteur-mesure.md](docs/notes/io-mapping-capteur-mesure.md).

## Modules

Sous `src/Modules/` (et `src/Modules/Network/` pour la connectivité). Chaque module possède en général :

- son `.cpp`/`.h`, un `*ModuleDataModel.h` (champs runtime exposés) et/ou `*Runtime.h`,
- un dossier `text/` : **`i18n.fr.json` est la source** ; `i18n.en.json`, `cfgdocs.fr.json`, `cfgmods.fr.json`, `runtimeui.json` en sont dérivés (scripts `generate_module_i18n_en.py`, `generate_config_docs.py`…).

Fiches par module dans [docs/modules/](docs/modules/) ; le module métier principal est `PoolLogicModule` ([docs/modules/PoolLogicModule.md](docs/modules/PoolLogicModule.md)).

## Contraintes importantes

- **Marge flash confortable** : l'image Waveshare-ESP32-S3 occupe **~47 %** de sa partition applicative de **4 Mo** (relevé build à jour). La flash n'est **pas** un facteur limitant : l'ancienne mention « ~93 % » datait d'une partition de 2 Mo depuis agrandie (le binaire n'a pas rétréci, la partition a doublé). Rester néanmoins sobre et vérifier la taille binaire après un ajout important — mais les vraies contraintes dures sont les **capacités compile-time** (tableaux statiques bornés, voir ci-dessous), pas la flash.
- **Capacités compile-time** (tableaux statiques bornés) : nombre d'endpoints IO, équipements `PoolDevice`, entités Home Assistant, routes runtime MQTT, variables de config… sont fixés à la compilation. Valeurs courantes dans [docs/README.md](docs/README.md) (« Capacités statiques »). Dépasser une capacité = troncature silencieuse, pas une erreur de build.
- **Deux ESP32 historiques** : `FlowIO` (métier/IO) et `Supervisor` (web/provisioning/OTA/TFT) communiquent en I2C — voir [docs/core/flow-supervisor-i2c-protocol.md](docs/core/flow-supervisor-i2c-protocol.md). Le profil Waveshare regroupe ces rôles sur une seule carte.
- **Moniteur de puissance unifié `POWERMON`** : les anciens backends `INA226`/`INA228` sont fusionnés en un seul backend `IO_BACKEND_POWERMON` (=8), un seul jeu de ports (143-150) et une config `powermon*` (champ `model` = 226 ou 228 à l'exécution). **Ne plus utiliser les noms/enums `INA226`/`INA228`** côté IOModule. Détail : [docs/notes/refactor-powermon-ina226-ina228.md](docs/notes/refactor-powermon-ina226-ina228.md).
- **Capteur « Pression » (ex-PSI)** : le capteur de pression est nommé `pressure` partout (identifiants, clés config JSON `pressure_*`, entité HA `io_pressure`, clé RuntimeUI `pool.pressure`, unité affichée **bar**). Les clés NVS binaires historiques (`pl_piid`, `pl_psil`, `pl_psih`, `pl_psdt`) et les valeurs d'AlarmId (1000/1001) sont conservées. Détail : [docs/notes/renommage-capteur-psi-pression.md](docs/notes/renommage-capteur-psi-pression.md).

## Notes de travail (`docs/notes/`)

Notes personnelles hors doc officielle — utiles pour l'état d'avancement et les décisions non évidentes du code :

- [io-mapping-capteur-mesure.md](docs/notes/io-mapping-capteur-mesure.md) — comment carte + domaine + E/S se combinent (les 4 couches, où se définit « quel capteur pour quelle mesure »).
- [refactor-powermon-ina226-ina228.md](docs/notes/refactor-powermon-ina226-ina228.md) — unification INA226/INA228 → POWERMON (**implémenté et compilé**).
- [wifi-ap-sta-reprobe.md](docs/notes/wifi-ap-sta-reprobe.md) — retour automatique AP→STA en signal faible (**analyse + proposition, non implémenté** : sur Waveshare le firmware ne retente jamais le STA une fois en AP).
- [renommage-capteur-psi-pression.md](docs/notes/renommage-capteur-psi-pression.md) — renommage complet PSI → Pression (**implémenté**, clés NVS binaires et AlarmId inchangés).
- [temperatures-slots-generiques.md](docs/notes/temperatures-slots-generiques.md) — sondes 1-Wire génériques 1..4 côté IO (ROM par slot), rôle eau/air déplacé dans PoolLogic, index DataStore résolus par IoId (**implémenté** ; RuntimeUiId 2201/2202 → 2406/2407, effacement NVS obligatoire).
- [ota-spiffs-lenteur-plantage.md](docs/notes/ota-spiffs-lenteur-plantage.md) — plantage TWDT pendant l'OTA SPIFFS (**corrigé** : yield périodique) + traçage `spiffs_version`.
- [audit-config-defaut-piscine-waveshare.md](docs/notes/audit-config-defaut-piscine-waveshare.md) — matrice des défauts métier injectés par `applyDomainDefaults` (DomainSpec).
- [audit-configstore-ui-poollogic.md](docs/notes/audit-configstore-ui-poollogic.md) — audit ConfigStore + chaîne cfgdocs/UI, refonte UX « équipements actifs » (**implémentée**) et backlog des chantiers de fond.
- [filtration-turnover-fenetres.md](docs/notes/filtration-turnover-fenetres.md) — filtration par renouvellement volumique (volume × cycles(T) ÷ débit) répartie sur 3 fenêtres priorisées, heures creuses incluses (**implémenté**, remplace « température/2 »).
- [regulation-ph-etat-art-et-refonte.md](docs/notes/regulation-ph-etat-art-et-refonte.md) — état de l'art du dosage pH par pompe péristaltique, constat chiffré sur l'ancien PID temporel, et refonte en dosage volumétrique par lots avec temps de mélange (**implémenté**). Le PID pH est **supprimé**, pas conservé derrière un switch : `ph_kp/ki/kd`, `ph_window_ms`, `ph_min_on_ms`, `ph_sample_ms` n'existent plus (effacement NVS recommandé) ; l'ORP garde son PID. `DosePumpMaxUptimeDaySec` passe à 90 min. Écarts par rapport à la proposition en §5.8.
