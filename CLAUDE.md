# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Vue d'ensemble

flow.io est un firmware ESP32 (PlatformIO + Arduino-ESP32) pour l'automatisation de piscine : régulation pH/ORP, filtration, chauffage, électrolyse/O2, supervision MQTT + Home Assistant, écran Nextion et interface web locale.

Un même arbre de sources compile **plusieurs firmwares** distincts, chacun sélectionné par un environnement PlatformIO et une macro de profil. Le périmètre de chaque firmware est découpé à la compilation via `build_src_filter` dans `platformio.ini` (les modules non pertinents sont exclus du build, pas seulement désactivés au runtime).

Le développement actif se fait sur `main`, qui cible principalement le profil **Waveshare ESP32-S3** (`[env:Waveshare-ESP32-S3]`), la source de vérité runtime actuelle — voir [docs/core/module-quality-gates.md](docs/core/module-quality-gates.md).

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

### Gates CI reproductibles en local

[.github/workflows/build.yml](.github/workflows/build.yml) applique six gates bloquants (`i18n`, `io-port-sync`, `gitleaks`, `cppcheck`, `native-tests`, `waveshare` + budget de partition) et un job informatif (`flowio-compile`). Quatre sont rejouables localement :

```bash
python scripts/validate_i18n.py --ratchet scripts/i18n_ratchet.json
python scripts/check_io_port_sync.py
python scripts/check_firmware_size.py --env Waveshare-ESP32-S3 --max-percent 85
```

- **`check_firmware_size.py`** lit la taille de la partition `app` dans le CSV déclaré par l'env (résolu en suivant `extends`), jamais un chiffre en dur — c'est ce qui avait fait dériver la mention « ~93 % » quand la partition est passée de 2 à 4 Mo. Exige un `pio run` préalable.
- **`check_io_port_sync.py`** compare les ports proposés par l'interface web (`scripts/io_port_labels.py`) à `kBindingPorts[]` du firmware. Une divergence ne casse aucun build : l'UI propose simplement un binding que le firmware refuse au boot, sans erreur visible — c'est ce qui était arrivé aux ports MCP23017 400-415. Sans `--profile`, tous les profils de `PROFILE_LAYOUTS` sont vérifiés.
- **cppcheck** n'est pas installé sur la machine de dev, mais PlatformIO le fournit : `pio pkg install --global --tool platformio/tool-cppcheck` puis `~/.platformio/packages/tool-cppcheck/cppcheck.exe`. Rejouer **exactement** les options du workflow, `--platform=unix32` compris : sans elle, cppcheck prend la plateforme de l'hôte et les alertes de portabilité diffèrent entre Windows et la CI. L'arbre courant sort à **zéro alerte** ; le gate ne se déclenche donc que sur une régression.
- **gitleaks** n'est pas rejouable en local (pas de binaire installé) ; son allowlist est dans [.gitleaks.toml](.gitleaks.toml), à étendre par une regex ciblée en cas de faux positif.

## Code généré — ne pas éditer à la main

Des scripts Python tournent **avant chaque build** (`extra_scripts` dans `platformio.ini`) et régénèrent du code et des assets. Ne modifiez jamais les fichiers générés directement ; modifiez la source puis recompilez.

- `generate_build_version.py` → macros de version/build
- `generate_datamodel.py` → `src/Core/Generated/ModuleDataModel_Generated.h` (agrège tous les `*ModuleDataModel.h` et `*Runtime.h` des modules **inclus** par le `build_src_filter` courant)
- `validate_i18n.py` → **valide** les manifestes texte, n'écrit rien. Bloquant sur : JSON invalide (avec ligne et colonne), token `*_t` sans traduction FR, collision de token entre modules, locale de fichier inconnue, clés non triées. Avertissements chiffrés sur la dette anglaise, avec cliquet par module dans `scripts/i18n_ratchet.json` qui ne peut que décroître (`--write-ratchet` pour l'abaisser après un lot de traduction, `--strict` en CI).
- `generate_runtimeui_manifest.py` → manifeste Runtime UI + lookup Supervisor dans `src/Core/Generated/`
- `prepare_spiffs_data.py` → prépare l'image SPIFFS (`data/`), génère les cfgdocs segmentés (`data/wc/*.j`). Les libellés des enum_sets de binding IO propres au profil viennent de `scripts/io_port_labels.py` (gabarits bilingues + tokens synthétiques injectés dans `i18n.<locale>.j`) : plus aucune chaîne française en dur dans le générateur.
- `export_binaries.py` (post-build) → copie les `.bin` dans `binary/`, écrit un side-car `<software>-<version>.json` par artefact (version, `build_ref`, sha256, `content_hash` du SPIFFS) et agrège le tout dans `binary/manifest.json`. Le manifeste est **additif** : il ne déduit plus rien du nom de fichier ni du mtime, et un build ne réécrit que sa propre entrée

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
- un dossier `text/` : **cinq sources écrites à la main**, dont aucune n'est dérivée d'une autre.
  - `i18n.fr.json` / `i18n.en.json` : catalogues de traduction (token → texte), clés triées.
  - `cfgdocs.fr.json` : `type` et tokens `label_t`/`help_t` des variables de config.
  - `cfgmods.fr.json` : `visible_if`, `hidden`, `enum_set`, `meta.enum_sets`.
  - `runtimeui.json` (7 modules) : descripteurs Runtime UI, tokens `*_t`.

  Les fichiers **générés** sont `data/wc/*.j` et `src/Core/Generated/RuntimeUi*_Generated.h`. Le marqueur `"_meta": {"source": "manual"}` en tête des cfgdocs/cfgmods rappelle leur statut de source (il valait `"generated": true`, ce qui était faux et trompeur).

  **Conflits git sur les catalogues** : ne pas les résoudre ligne à ligne. Les `i18n.<locale>.json` sont des dictionnaires plats — git conflicte sur des lignes voisines alors que l'arbitrage se fait clé par clé, et `git checkout --theirs` prendrait le fichier **entier** de l'autre côté au lieu des seuls hunks en conflit. Lancer [scripts/resolve_i18n_conflicts.py](scripts/resolve_i18n_conflicts.py) (outil manuel, hors build) : il fait la fusion 3-way par clé depuis les stages de l'index et ne remonte que les désaccords réels. Vérifier ensuite avec `validate_i18n.py`, qui exige des clés triées et sans doublon.

Fiches par module dans [docs/modules/](docs/modules/) ; le module métier principal est `PoolLogicModule` ([docs/modules/PoolLogicModule.md](docs/modules/PoolLogicModule.md)).

## Contraintes importantes

- **Un token i18n manquant ne casse rien : il s'affiche brut à l'écran.** C'est le mode d'échec de `translations.get(token, token)` ([generate_config_docs.py](scripts/generate_config_docs.py), [generate_runtimeui_manifest.py](scripts/generate_runtimeui_manifest.py)), et les JSON invalides sont avalés par des `except Exception: return {}`. Le pré-build `validate_i18n.py` est le seul garde-fou : ne pas le retirer de `extra_scripts` pour « débloquer » un build.

- **Marge flash confortable** : l'image Waveshare-ESP32-S3 occupe **~47 %** de sa partition applicative de **4 Mo** (relevé build à jour). La flash n'est **pas** un facteur limitant : l'ancienne mention « ~93 % » datait d'une partition de 2 Mo depuis agrandie (le binaire n'a pas rétréci, la partition a doublé). Rester néanmoins sobre et vérifier la taille binaire après un ajout important — mais les vraies contraintes dures sont les **capacités compile-time** (tableaux statiques bornés, voir ci-dessous), pas la flash.
- **Capacités compile-time** (tableaux statiques bornés) : nombre d'endpoints IO, équipements `PoolDevice`, entités Home Assistant, routes runtime MQTT, variables de config… sont fixés à la compilation. Valeurs courantes dans [docs/README.md](docs/README.md) (« Capacités statiques »). Dépasser une capacité = troncature silencieuse, pas une erreur de build.
- **8 relais physiques sur Waveshare, et c'est le seul plafond non relevable.** `kBindingPorts[]` ne déclare que `PortExio1..PortExio8` (300-307, TCA9554) ; les ports MCP23017 (400-415) ont été retirés des listes déroulantes parce qu'aucun layout ne les porte. Déclarer une fonction de plus ne coûte que quelques variables de config, mais la **brancher** exige un port libre. 12 fonctions sont déclarées pour 8 relais : les trois modes de désinfection étant exclusifs, la contrainte n'est pas atteinte en pratique. Une fonction non liée est inerte (écriture no-op), pas en erreur.
- **Deux ESP32 historiques** : `FlowIO` (métier/IO) et `Supervisor` (web/provisioning/OTA/TFT) communiquent en I2C — voir [docs/core/flow-supervisor-i2c-protocol.md](docs/core/flow-supervisor-i2c-protocol.md). Le profil Waveshare regroupe ces rôles sur une seule carte.
- **Moniteur de puissance unifié `POWERMON`** : les anciens backends `INA226`/`INA228` sont fusionnés en un seul backend `IO_BACKEND_POWERMON` (=8), un seul jeu de ports (143-150) et une config `powermon*` (champ `model` = 226 ou 228 à l'exécution). **Ne plus utiliser les noms/enums `INA226`/`INA228`** côté IOModule. Détail : [docs/notes/refactor-powermon-ina226-ina228.md](docs/notes/refactor-powermon-ina226-ina228.md).
- **Capteur « Pression » (ex-PSI)** : le capteur de pression est nommé `pressure` partout (identifiants, clés config JSON `pressure_*`, entité HA `io_pressure`, clé RuntimeUI `pool.pressure`, unité affichée **bar**). Les clés NVS des seuils sont passées à `pl_prlow` / `pl_prhigh` / `pl_prdelay` (schéma de config v3, `mig_2_to_3` migre les anciennes sans perte) ; `pl_piid` est conservée (`iid` = suffixe commun à tous les IoId) et les valeurs d'AlarmId (1000/1001) sont inchangées. Détail : [docs/notes/renommage-capteur-psi-pression.md](docs/notes/renommage-capteur-psi-pression.md).

## Notes de travail (`docs/notes/`)

Notes personnelles hors doc officielle — utiles pour l'état d'avancement et les décisions non évidentes du code :

- [io-mapping-capteur-mesure.md](docs/notes/io-mapping-capteur-mesure.md) — comment carte + domaine + E/S se combinent (les 4 couches, où se définit « quel capteur pour quelle mesure »).
- [sorties-ha-inconnu-retain-mqtt.md](docs/notes/sorties-ha-inconnu-retain-mqtt.md) — sorties « inconnu » dans HA alors que les capteurs vont bien : les états runtime partaient en QoS 0 **non retenus**, donc une sortie qui ne change pas d'état n'était jamais republiée après une perte de paquet (**corrigé** le 18/08/2026 : `retain` sur les routes `ActuatorImmediate` seulement, les mesures restent non retenues volontairement). Défaut latent depuis mars 2026, révélé par un RSSI à **-84 dBm** — la cause première est radio, pas logicielle : **ne pas chercher de régression de code sur ce symptôme**, `/api/flow/status/domain?d=pool` tranche entre « le firmware ne sait pas » et « le message n'arrive pas ». Contient les six pistes explorées et écartées (dont la course one-shot de `configureRuntime_`, réelle mais non déclenchée ici, dont le seul témoin est un `LOGD`).
- [refactor-powermon-ina226-ina228.md](docs/notes/refactor-powermon-ina226-ina228.md) — unification INA226/INA228 → POWERMON (**implémenté et compilé**).
- [wifi-ap-sta-reprobe.md](docs/notes/wifi-ap-sta-reprobe.md) — retour automatique AP→STA en signal faible (**implémenté** le 18/08/2026 : probe STA périodique en AP, suspendu tant qu'un client portail est connecté ; reste la validation terrain).
- [renommage-capteur-psi-pression.md](docs/notes/renommage-capteur-psi-pression.md) — renommage complet PSI → Pression (**implémenté**, clés NVS binaires et AlarmId inchangés).
- [temperatures-slots-generiques.md](docs/notes/temperatures-slots-generiques.md) — sondes 1-Wire génériques 1..4 côté IO (ROM par slot), rôle eau/air déplacé dans PoolLogic, index DataStore résolus par IoId (**implémenté** ; RuntimeUiId 2201/2202 → 2406/2407, effacement NVS obligatoire).
- [ota-spiffs-lenteur-plantage.md](docs/notes/ota-spiffs-lenteur-plantage.md) — plantage TWDT pendant l'OTA SPIFFS (**corrigé** : yield périodique) + traçage `spiffs_version`.
- [ota-spiffs-gzip-bilan.md](docs/notes/ota-spiffs-gzip-bilan.md) — **point d'entrée du chantier OTA SPIFFS compressé** : état en service, mesures sur cible, points ouverts (panique post-OTA, étape 3, `.pkg`) et commandes de test. Renvoie aux deux notes ci-dessous pour l'historique.
- [ota-spiffs-gzip-bilan.md](docs/notes/ota-spiffs-gzip-bilan.md) — **⚠️ NE PAS écrire en flash (OTA firmware ou SPIFFS) sans avoir lu ce document.** `FLOW_OTA_SPIFFS_GZIP = 1`, mais l'écriture réelle en SPIFFS **corrompt le système de fichiers de façon répétée et non résolue** depuis la nuit du 18/08/2026 : cinq écritures réelles tentées ce soir-là, cinq corruptions, y compris un **simple OTA firmware qui ne touche pas SPIFFS**, ce qui élimine toute cause spécifique au SPIFFS. Trois hypothèses posées et réfutées par la mesure (course avec le journal d'activité, `SPIFFS.end()` avant l'écriture, tampon PSRAM). Piste actuelle, non confirmée : signature `PC=0xFFFFFFFE` typique d'une exécution dans de la flash effacée, cohérente avec du code non-IRAM interrompu pendant qu'un cœur a le cache flash désactivé pour une écriture — mécanisme documenté sur ESP32, mais qui n'explique pas pourquoi ça n'était jamais arrivé avant cette nuit. Outillage ajouté et à conserver : `src/Core/CoreDumpInfo.*` lit la partition `coredump` (résumé JSON, désormais avec les registres a0-a15/EPCx) via `GET /api/system/coredump` (route `/raw` bogguée, ne renvoie pas l'image complète) ; `src/Core/SpiffsAccessLock.*` (mutex autour des accès SPIFFS, protège une course réelle mais qui n'est pas la cause du crash) ; console de secours **`/rescue`** embarquée dans le firmware lui-même (indépendante de SPIFFS, permet de relancer un OTA sans USB même SPIFFS cassé). Historique complet des OTA SPIFFS compressés (gzip, dry run, débordement de pile `tinfl_decompressor`, bug quadratique `printJsonEscaped_`/`/api/activity/logs`) dans [ota-spiffs-reduction-volume.md](docs/notes/ota-spiffs-reduction-volume.md).
- [runtime-ui-double-chemin-waveshare.md](docs/notes/runtime-ui-double-chemin-waveshare.md) — sur Waveshare, `/api/runtime/values` passe par un `switch` en dur dans `WebInterfaceServer.cpp` et **jamais** par `writeRuntimeUiValue` : une valeur ajoutée au manifeste sans `case` répond « indisponible » en silence (les 63 entrées sont câblées à ce jour ; la note donne la commande de vérification). Contient aussi le bug de troncature `toJsonModule` qui affichait « pH auto : Arrêt » à tort (**corrigé**).
- [versionnage-firmware-et-spiffs.md](docs/notes/versionnage-firmware-et-spiffs.md) — la version du contenu SPIFFS est embarquée dans l'image (`/fsver.j`, lu par `FilesystemVersion`) au lieu d'être déduite du nom de fichier OTA et persistée en NVS (**étape 1 implémentée** ; clé `sp_version` supprimée, orpheline et ignorée, pas d'effacement NVS requis). **Étape 1bis implémentée** : le manifeste de `binary/` ne déduit plus la version du nom de fichier ni la date du mtime, il republie l'identité produite par le build (side-car par artefact, `build_ref`, `sha256`, `content_hash`) et se met à jour de façon additive. Étapes 2 (inventaire de versions unique dans `/api/web/meta`), 3 (alerte de désynchronisation firmware ↔ SPIFFS) et 4 (`build_ref` dérivé du commit git au lieu de l'horloge, pour que deux builds d'un même code donnent la même estampille) proposées, non implémentées.
- [audit-config-defaut-piscine-waveshare.md](docs/notes/audit-config-defaut-piscine-waveshare.md) — matrice des défauts métier injectés par `applyDomainDefaults` (DomainSpec).
- [audit-configstore-ui-poollogic.md](docs/notes/audit-configstore-ui-poollogic.md) — audit ConfigStore + chaîne cfgdocs/UI, refonte UX « équipements actifs » (**implémentée**) et backlog des chantiers de fond.
- [filtration-turnover-fenetres.md](docs/notes/filtration-turnover-fenetres.md) — filtration par renouvellement volumique (volume × cycles(T) ÷ débit) répartie sur 3 fenêtres priorisées, heures creuses incluses (**implémenté**, remplace « température/2 »).
- [regulation-ph-etat-art-et-refonte.md](docs/notes/regulation-ph-etat-art-et-refonte.md) — état de l'art du dosage pH par pompe péristaltique, constat chiffré sur l'ancien PID temporel, et refonte en dosage volumétrique par lots avec temps de mélange (**implémenté**). Le PID pH est **supprimé**, pas conservé derrière un switch : `ph_kp/ki/kd`, `ph_window_ms`, `ph_min_on_ms`, `ph_sample_ms` n'existent plus (effacement NVS recommandé) ; l'ORP garde son PID. `DosePumpMaxUptimeDaySec` passe à 90 min. Écarts par rapport à la proposition en §5.8.
- [autobind-analogique-slot-de-layout.md](docs/notes/autobind-analogique-slot-de-layout.md) — l'auto-binding analogique repose un port orphelin sur le slot que le layout lui réserve, plus sur le premier trou (**implémenté**). Deux garde-fous : pas d'auto-binding d'un canal DS18B20 sans ROM, et un slot réservé à un autre backend n'est plus un trou disponible. Correctif préventif : une NVS déjà décalée doit être remise à plat à la main.
- [gel-mesures-hors-circulation.md](docs/notes/gel-mesures-hors-circulation.md) — pompe à l'arrêt, les sondes en ligne ne mesurent plus que l'eau immobile du porte-sondes : pH, Redox et température d'eau republient leur dernière valeur acquise en circulation, marquée `held` (**implémenté**). Le gel ne touche que la donnée publiée, les régulations étaient déjà protégées. Trois pièges traités : horodatage qui continue d'avancer (sinon l'alarme « sonde d'eau muette » sonne chaque nuit), purge du filtre médian au redémarrage, pression volontairement **non** gelée. Délai de reprise borné à 240 s à cause de la sonde 5 min du chauffage.
- [refonte-fonctions-piscine-v2.md](docs/notes/refonte-fonctions-piscine-v2.md) — une fonction piscine = un PoolDevice = une sortie logique `dNN` de même index (**implémenté**). Les 12 fonctions sont renumérotées dans `PoolIds::Device*` → **effacement NVS obligatoire**. Les 7 champs `poollogic/*_slot` et leurs clés `pl_s*` sont supprimés : le seul choix laissé à l'utilisateur est le relais physique. Arbre de config `piscine/*` obtenu par alias d'affichage + `meta.order`, sans renommer aucune clé NVS. L'invariant `pdN ⇔ dNN` passe de `requireSetup` (boucle infinie au boot) à un `static_assert`.
- [entites-ha-brutes-et-metier.md](docs/notes/entites-ha-brutes-et-metier.md) — pourquoi HA voit à la fois les slots IO bruts (`io_temp1..4`, `io_a08..a15`) et les entités métier (`io_wat_tmp`, `io_air_tmp`) : deux chemins de découverte indépendants, `analogSlotPublished_` publie tout slot défini même sans binding, et le domaine ne fait que nommer. **Constat, rien de modifié.** Le vrai risque n'est pas le bruit visuel mais `MaxSensors = 48` : ≈ 45 entités consommées aujourd'hui, débordement muet (`addSensorEntry` renvoie `false` sans log).
- [desinfection-reglage-a-froid.md](docs/notes/desinfection-reglage-a-froid.md) — le type de désinfection est lu dans les Preferences au démarrage du profil et fige tout ce qui en dépend (**implémenté**, §7) : équipements, variables de config, alarmes et entités des deux modes non retenus ne sont plus déclarés du tout — `setEntityAbsent` n'avait jamais libéré la moindre place. `isDisinfectionType_` consulte `bootDisinfectionType_`, pas la config. Le mode en service n'est pas republié : il se **constate** en regardant lequel de `pd2`/`pd3`/`pd4` est défini dans le DataStore (`dis_live` de `/api/flow/status/domain?d=pool`, `pd0` servant de témoin de disponibilité) — c'est l'écart avec `disinfection_type` qui déclenche le bandeau « redémarrage requis ». Pas d'effacement NVS. Deux défauts corrigés au passage : un démarrage manuel de pompe via `pooldevice.write` écrivait `disinfection_type = 3` (bascule en oxygène actif), et le masquage de la tuile « désinfection auto » testait `!= 3` en dur, soit exactement l'inverse depuis le réordonnancement de l'enum.
