# Note de refactor — Unification INA226/INA228 → POWERMON

> Statut : **implémenté, compilé et validé (Waveshare-ESP32-S3)**
> Date : 2026-07-01 (cœur) / 2026-07-02 (finalisation textes + build)
> Profil de référence : Waveshare ESP32-S3 (source de vérité runtime)

## 1. Objectif

Les deux drivers de mesure électrique `INA226` et `INA228` étaient exposés comme
**deux backends / deux jeux de ports distincts**, alors qu'ils occupent la même
adresse I2C (0x40) et ne peuvent pas coexister. Ce refactor les fusionne en un
**moniteur de puissance unique `POWERMON`** :

- un seul backend, un seul jeu de ports, une seule config ;
- un champ de config **`model` (226 ou 228)** choisit la puce physique à l'exécution ;
- l'INA228 ajoute température / énergie / charge (canaux 5-7), **inactifs en 226**.

Bénéfice : suppression de la duplication (enums, config vars, ports, textes), une
UI plus claire, et un mapping de ports cohérent.

## 2. Modèle avant / après

| | Avant | Après |
|---|---|---|
| Backend | `IO_BACKEND_INA226` (8), `IO_BACKEND_INA228` (11) | `IO_BACKEND_POWERMON` (8) |
| Source analog | `IO_SRC_INA226` (7), `IO_SRC_INA228` (8) | `IO_SRC_POWERMON` (7) |
| Port kind | `IO_PORT_KIND_INA226` (8), `IO_PORT_KIND_INA228` (14) | `IO_PORT_KIND_POWERMON` (8) |
| Ports physiques | 138-142 (INA226) **+** 143-150 (INA228) | 143-150 (POWERMON, canaux 0-7) |
| Config | `ina226*` + `ina228*` (8 vars) | `powermon*` (5 vars, dont `model`) |
| Clés NVS | `io_inaen/…`, `io_in8en/…` | `io_pmen`, `io_pmmd`, `io_pmad`, `io_pmpl`, `io_pmsh` |

Les ports **138-142 (ancien INA226) ont été supprimés** ; les ports 143-150
conservent leurs identifiants et deviennent la plage POWERMON unique.

## 3. Sélection de la puce (runtime)

Dans `IOModule::configureRuntime_()`, l'allocation du driver dépend de `model` :

```cpp
if (cfgData_.powermonModel == 226) {
    driver = allocIna226Driver_("powermon", &i2cBus_, cfg226);  // canaux 0-4
} else {
    driver = allocIna228Driver_("powermon", &i2cBus_, cfg228);  // canaux 0-7
}
```

Les deux drivers physiques (`Ina226Driver`, `Ina228Driver`, libs `INA226_WE` /
`INA228`) sont **conservés** ; seule la couche d'exposition IOModule est unifiée.
En modèle 226, les canaux 5-7 (température/énergie/charge) ne sont pas fournis par
le driver → endpoints inactifs.

## 4. Fichiers touchés

**Cœur C++ / interface**
- `src/Core/Services/IIO.h` — enum `IoBackend`
- `src/Modules/IOModule/IOModuleTypes.h` — config, `IOAnalogSource`, `IOBindingPortKind`
- `include/Core/NvsKeys.h` — clés `io_pm*`
- `src/Modules/IOModule/IOModule.{h,cpp}` — config vars, route MQTT, mapping,
  allocation par model, runtime UI writes
- `src/Profiles/Waveshare/WaveshareIoLayout.h`, `src/Profiles/FlowIO/FlowIOIoLayout.h`
  — ports POWERMON
- `src/Modules/TFTModuleS3/TFTModuleS3.cpp`,
  `src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp` — dashboards

**Textes (source FR + dérivés)**
- `src/Modules/IOModule/text/{i18n.fr,cfgdocs.fr,cfgmods.fr,runtimeui}.json`
- `src/Modules/TFTModuleS3/text/i18n.fr.json`
- `scripts/generate_config_docs.py` — map de labels de binding waveshare
  (retrait 138-142, ajout 143-150 POWERMON, annotation « (INA228) » sur les canaux 5-7)
- `scripts/generate_module_i18n_en.py` — glossaire enrichi (« moniteur de puissance »
  → « power monitor », « Canal » → « Channel », grandeurs électriques)
- `i18n.en.json` (tous modules) régénérés depuis les sources FR

**Doc**
- `docs/modules/IOModule.md`

## 5. Validation

- **Build `Waveshare-ESP32-S3` : SUCCESS**, Flash ~47 % (large marge).
- `data/wc/*.j` régénéré : plus aucun label de port INA obsolète. Les seules
  mentions « INA226 »/« INA228 » restantes sont **intentionnelles** (help décrivant
  « INA226 ou INA228 » et annotation « (INA228) » sur température/énergie/charge).
- **Build `FlowIO` : ne linke pas**, mais pour une raison **préexistante et
  indépendante** du refactor (dérive de core : `ledcAttach`, `xTaskCreatePinnedToCoreWithCaps`,
  `driver/rmt_encoder.h`). Aucune erreur liée à POWERMON dans l'IOModule.

> Note environnement : lancer `pio` depuis **PowerShell** (natif Windows), pas
> depuis le shell Bash/MSys (rejet d'`esptool` : « MSys/Mingw is not supported »).

## 6. Compatibilité / migration

Les clés NVS ont changé (`io_ina*`/`io_in8*` → `io_pm*`). Une config persistée
issue d'un ancien firmware **ne migre pas automatiquement** : le moniteur de
puissance repart sur ses valeurs par défaut (désactivé, model 228, 0x40, 500 ms,
0,1 Ω). À reconfigurer via l'interface web si le capteur était utilisé.
