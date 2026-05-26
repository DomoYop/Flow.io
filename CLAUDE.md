# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Vue d'ensemble

**Flow.io** est une plateforme de gestion autonome de piscine basée sur deux firmwares ESP32 (PlatformIO/Arduino). Le système automatise la qualité de l'eau (pH, ORP/chlore, filtration), contrôle des pompes péristaltiques, générateurs de chlore salin, et expose un HMI Nextion tactile + intégration MQTT/Home Assistant.

### Profils firmware

| Profil | Rôle |
|--------|------|
| `FlowIO` | ESP32 principal – logique piscine, I/O, MQTT, HMI Nextion |
| `Supervisor` | ESP32 secondaire – provisioning Wi-Fi, interface web, OTA, afficheur TFT |
| `FlowConnectDisplay` | Variante minimale pour afficheur Nextion autonome |
| `Micronova` | Intégration chaudière externe |

## Commandes de build (PlatformIO)

```bash
# Compiler un profil
pio run -e FlowIO
pio run -e Supervisor
pio run -e FlowConnectDisplay
pio run -e Micronova

# Simulation Wokwi
pio run -e FlowIOWokwi
pio run -e SupervisorWokwi

# Flasher et moniteur série (115200 baud)
pio run -e FlowIO -t upload
pio run -e FlowIO -t monitor
pio run -e Supervisor -t monitor
```

### Scripts pré-build (lancés automatiquement par platformio.ini)

Les scripts dans `scripts/` génèrent du code dans `src/Core/Generated/` — **ne pas éditer ce dossier manuellement** :

- `generate_datamodel.py` – modèle de données
- `generate_build_version.py` – injection de version
- `generate_runtimeui_manifest.py` – métadonnées UI
- `prepare_spiffs_data.py` – assets web Supervisor
- `export_binaries.py` – export post-build vers `binary/`

### Versions firmware

Modifier dans la section `[common]` de `platformio.ini` :
```ini
supervisor_firmware_version = '"1.0.2"'
flowio_firmware_version = '"1.0.2"'
```

## Architecture

### Couches (bas → haut)

1. **Hardware** (`src/Board/`, `src/Domain/`) — mappings GPIO, specs carte, presets domaine piscine
2. **Core** (`src/Core/`) — Module, ConfigStore, DataStore, EventBus, LogHub, ServiceRegistry, CommandRegistry
3. **Modules** (`src/Modules/`) — fonctionnalités encapsulées (réseau, I/O, logique piscine, HMI…)
4. **Profiles** (`src/Profiles/`) — assemblage des modules pour chaque firmware

### Modèle de services

Les modules communiquent via des interfaces C légères enregistrées dans `ServiceRegistry` — pas de couplage direct entre classes :

```cpp
// Déclarer une interface dans src/Core/Services/IMyService.h
struct IMyService {
    bool (*doWork)(void* ctx, int value);
    void* ctx;
};

// Enregistrer dans Module::init()
services.add<IMyService>({myFunc, this});

// Consommer dans un autre module
auto svc = services.get<IMyService>();
if (svc) svc->doWork(svc->ctx, 42);
```

### ConfigStore / DataStore / EventBus

- **ConfigStore** : configuration persistante en NVS ESP32, exposée en JSON, modifiable via MQTT (`cfg/*`)
- **DataStore** : état volatile runtime, publié sur MQTT (`rt/*`) si une route est enregistrée
- **EventBus** : signalisation asynchrone interne — `EventId::ConfigChanged` et `EventId::DataChanged` déclenchent les mises à jour croisées entre modules

### Cycle de vie des modules

`init()` → `onConfigLoaded()` → `onStart()` → `loop()`

Le `ModuleManager` résout les dépendances topologiquement. Les dépendances se déclarent via `dependencyCount()` / `dependency()`.

### Séquence d'initialisation typique

**FlowIO** : LogHub → EventBus → ConfigStore → DataStore → CommandRegistry → I2CCfgServer → HMI Nextion → Alarmes → WiFi/Time/MQTT/HA → IOModule → PoolLogic → SystemMonitor

**Supervisor** : Logs → EventBus/Config/Data → Alarmes → WiFi/Provisioning → Time → I2CCfgClient → WebInterface → FirmwareUpdate → HMI TFT → SystemMonitor

### Régulation PID (PoolLogicModule)

PID sur fenêtre temporelle pour pompes pH et ORP :
- Calcul toutes les 30 s (configurable)
- Sortie (0–100 %) convertie en durée ON dans une fenêtre (typiquement 1 h)
- Interlocks de sécurité : filtration arrêtée, mode hivernage, capteur invalide, défaut pression → sortie désactivée

### Communication I2C FlowIO ↔ Supervisor

- FlowIO SDA(5)/SCL(15) ↔ Supervisor SDA(27)/SCL(13)
- `I2CCfgServerModule` (FlowIO) + `I2CCfgClientModule` (Supervisor) synchronisent la configuration
- Protocole détaillé : `docs/core/flow-supervisor-i2c-protocol.md`

### Topics MQTT

| Préfixe | Usage |
|---------|-------|
| `cfg/*` | Configuration (lecture/écriture) |
| `rt/*` | État runtime (lecture seule, auto-publié) |
| `cmd/` | Commandes (écriture) |
| `ack/` | Acquittements |

Home Assistant découvre les entités automatiquement via MQTT discovery.

## Ajouter un module

1. Créer `src/Modules/MyModule/MyModule.h` (interface, hériter de `Module` ou `ModulePassive`)
2. Créer `src/Modules/MyModule/MyModule.cpp` (implémenter `init()`, `loop()`, déclarer dépendances)
3. Si d'autres modules doivent l'appeler, créer `src/Core/Services/IMyModule.h`
4. Instancier et enregistrer dans le `Bootstrap.cpp` du profil concerné
5. Ajouter un filtre dans `platformio.ini` si le module est spécifique à un profil

## Ajouter un paramètre de configuration

```cpp
// Dans Module::init()
cfg.declare(ConfigPath::MyParam, 10.0f, "description");

// Dans onConfigLoaded() — charger dans l'état du module
// S'abonner à EventId::ConfigChanged pour réagir aux mises à jour MQTT
```

## Tests

Tests unitaires dans `test/test_poollogic_filtration_window/` (calculs fenêtre filtration). Framework PlatformIO natif.

## Documentation de référence

- Architecture globale : `docs/core/architecture.md`
- Services et modèle runtime : `docs/core/services.md`
- Flux données/événements/config : `docs/core/data-event-model.md`
- Topics MQTT : `docs/core/mqtt-topics.md`
- Protocole I2C : `docs/core/flow-supervisor-i2c-protocol.md`
- Qualité des modules : `docs/core/module-quality-gates.md`
- Mise en service : `docs/integration/mise-en-service.md`
- Docs par module : `docs/modules/`
