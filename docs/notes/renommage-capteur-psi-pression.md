# Note de refactor — Renommage capteur « PSI » → « Pression »

> Statut : **implémenté, compilé et validé (Waveshare-ESP32-S3)**
> Date : 2026-07-07
> Profil de référence : Waveshare ESP32-S3 (source de vérité runtime)

## 1. Objectif

Le capteur de pression du circuit de filtration était nommé « PSI » partout dans
le firmware (identifiants, config, entités Home Assistant, écrans, docs), alors
que **PSI est une unité de pression, pas un nom de capteur** — et ce n'est même
pas une unité SI (contrairement au Pascal ou au bar). Cette confusion nom/unité
est corrigée en deux temps :

1. **Renommage du capteur** : `psi` → `pressure` (identifiants machine) /
   « Pression » (libellés affichés), sur tout le firmware.
2. **Correction de l'unité affichée** : `PSI` → `bar`, cohérent avec les
   valeurs de calibration existantes (seuils 0,15 / 1,80 — une plage typique de
   manomètre de filtration piscine en bar ; en Pascal ces seuils vaudraient
   15 000 / 180 000, impraticable à afficher).

## 2. Modèle avant / après

| | Avant | Après |
|---|---|---|
| Domain slot | `PoolIds::SensorPsi` | `PoolIds::SensorPressure` |
| Calibration | `Calib::Psi::DefaultC0/C1` | `Calib::Pressure::DefaultC0/C1` |
| Alarmes | `AlarmId::PoolPsiLow/High` (1000/1001, **inchangés**) | `AlarmId::PoolPressureLow/High` (1000/1001) |
| Entité HA capteur | `io_psi` / nom « PSI » / unité « PSI » | `io_pressure` / nom « Pressure » / unité **« bar »** |
| Entité HA seuils | `pl_safe_psi_low` / `pl_safe_psi_high` | `pl_safe_pressure_low` / `pl_safe_pressure_high` |
| Slug alarme HA | `psi_low` / `psi_high` | `pressure_low` / `pressure_high` |
| Clés JSON config | `psi_io_id`, `psi_low_th`, `psi_high_th`, `psi_start_dly_s` | `pressure_io_id`, `pressure_low_th`, `pressure_high_th`, `pressure_start_dly_s` |
| Clé RuntimeUI/MQTT | `pool.psi` (unité `PSI`) | `pool.pressure` (unité **`bar`**) |
| État heat-assist (MQTT `ri`) | `PSI_BLOCKED` | `PRESSURE_BLOCKED` |
| Raison blocage O2 (`o2.block_s`) | `psi` | `pressure` |
| Libellés FR affichés | « PSI bas » / « PSI haut » | « Pression basse » / « Pression haute » |

Les **identifiants C++ purs** (`pressureIoId_`, `condPressureLowStatic_`,
`RuntimeUiPressure`, `HMI_HOME_ALARM_PRESSURE`…) ont été renommés par cohérence
mais n'ont aucun coût flash (symboles de compilation, pas de chaînes en
binaire). Seules les chaînes littérales listées ci-dessus finissent dans
`firmware.bin` ; leur allongement (`psi` → `pressure`) représente quelques
dizaines d'octets, négligeable au vu de la marge flash réelle actuelle
(46,7 % utilisé sur Waveshare-ESP32-S3, chiffre à jour — celui de ~93 %
mentionné ailleurs dans la doc historique semble périmé).

**Décision prise avec l'utilisateur** : garder les identifiants machine en
toutes lettres (`pressure_io_id`, `pressure_low`…) plutôt que des codes courts
(`pre`, `prel`, `preh`) — lisibilité préférée à l'économie marginale de flash.

## 3. Ce qui n'a **pas** changé (compatibilité)

- **Clés NVS binaires réelles** (`pl_piid`, `pl_psil`, `pl_psih`, `pl_psdt` dans
  `include/Core/NvsKeys.h`) : laissées **inchangées**. Ce sont déjà des
  abréviations opaques (pas littéralement « psi »), donc **aucun reset NVS
  requis** — seul le nom du symbole C++ associé a changé.
- **Valeurs numériques** des `AlarmId` (1000/1001) et des seuils de calibration
  (0,15 / 1,80) : inchangées, seules les étiquettes texte évoluent.
- Le nom de composant Nextion **`vaPSINiddle`** (`NextionDriver.cpp`) est
  **volontairement conservé** : il est figé dans le projet Nextion Editor
  (fichier `.tft`), hors de ce dépôt — le renommer côté firmware sans regénérer
  l'écran casserait l'affichage V2 legacy.

## 4. Impact compatibilité JSON config export/import

Les clés `jsonName` exposées par `ConfigVariable` (`psi_io_id` →
`pressure_io_id`, etc.) servent à l'export/import JSON de config et aux
`cfgdocs`. Une sauvegarde de config exportée **avant** ce changement ne
réappliquera pas automatiquement les réglages du capteur de pression lors d'un
import après mise à jour — à resaisir manuellement le cas échéant. Aucun impact
sur le stockage NVS lui-même (voir §3).

Les dashboards Home Assistant exemple
(`docs/integration/home_assistant_alarm_helpers_fio53.yaml` et
`..._dashboard_fio53.yaml`) référencent les entités par leur `unique_id`
(`fio53_pressure_low_active`, etc.) — mis à jour dans ce commit. Une
installation HA existante utilisant les anciens noms d'entité (`fio53_psi_low_active`)
devra être re-liée aux nouvelles entités après mise à jour du firmware.

## 5. Fichiers touchés

**Domaine / profils**
- `src/Domain/Pool/PoolIds.h`, `PoolDomain.h`, `PoolDefaults.h`,
  `src/Domain/DomainTypes.h`, `src/Domain/Calibration.h`
- `src/Profiles/Waveshare/WaveshareIoLayout.h`, `WaveshareIoAssembly.cpp`
- `src/Profiles/FlowIO/FlowIOIoLayout.h`, `FlowIOIoAssembly.cpp`
- `scripts/generate_wokwi_default_overrides.py`

**PoolLogicModule**
- `PoolLogicModule.{h,cpp}`, `PoolLogicControl.cpp`, `PoolLogicLifecycle.cpp`,
  `PoolLogicRuntime.cpp`
- `text/{i18n.fr,i18n.en,cfgdocs.fr}.json`

**Alarmes / IO / HMI**
- `include/Core/AlarmIds.h`, `include/Core/DataKeys.h`, `include/Core/NvsKeys.h`
- `src/Modules/IOModule/{IOModule.h,IOModule.cpp,IOModuleSnapshots.cpp}`,
  `text/runtimeui.json`
- `src/Modules/HMIModule/{HMIModule.h,HMIModule.cpp}`,
  `Drivers/{HmiDriverTypes.h,NextionDriver.cpp}`
- `src/Modules/TFTModuleS3/TFTModuleS3.cpp`, `text/{i18n.fr,i18n.en}.json`
- `src/Modules/SupervisorHMIModule/{SupervisorHMIModule.cpp,SupervisorHmiTextSet.cpp}`,
  `Drivers/St7789SupervisorDriver.{h,cpp}`
- `src/Modules/Network/I2CCfgClientModule/{I2CCfgClientModule.cpp,I2CCfgClientModuleDataModel.h,I2CCfgClientRuntime.h}`
- `src/Modules/Network/HmiUdpServerModule/HmiUdpServerModule.cpp`
- `src/Modules/FlowConnectDisplay/FlowConnectDisplayUdpClientModule/FlowConnectDisplayUdpClientModule.cpp`
- `src/Core/Hmi/HmiUdpProtocol.h`
- `src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp` (clé/unité
  RuntimeUi + dashboards Waveshare)

**Interface web**
- `data/webinterface/app.js`, `sh.html`, `i18n/{fr,en}.json`

**Wokwi / illustrations**
- `wokwi/waveshare/diagram.json`, `wokwi/flowio/diagram.json`, `diagram.json`
- `docs/pictures/ha_alarm_tiles_md3_mockup.svg`, `..._dense_mockup.svg`

**Doc**
- `docs/modules/{AlarmModule,IOModule,PoolLogicModule}.md`
- `docs/core/module-quality-gates.md`
- `docs/integration/flowio-poollogic-business.md`, `nextion-esp-protocol.md`
- `docs/integration/plan_tests_{poollogic_pdm_io,sequentiel_poollogic_pdm_io}.csv`
- `docs/integration/home_assistant_alarm_{dashboard,helpers}_fio53.yaml`
- `docs/rapport-analyse-domaine-piscine.md`
- `docs/notes/audit-config-defaut-piscine-waveshare.md`, `io-mapping-capteur-mesure.md`

**Généré (régénéré par le build, committé par convention du dépôt)**
- `src/Core/Generated/WokwiDefaultOverrides_Generated.h` — régénéré
  manuellement (`python scripts/generate_wokwi_default_overrides.py`) car ce
  script ne tourne qu'en pré-build des environnements `*Wokwi`, pas de
  `Waveshare-ESP32-S3`.
- `src/Core/Generated/{RuntimeUiManifest_Generated.h,RuntimeUiManifestJson_Generated.h}`
  — régénérés par `pio run -e Waveshare-ESP32-S3` (`generate_runtimeui_manifest.py`).

## 6. Validation

- **Build `Waveshare-ESP32-S3` : SUCCESS**, Flash 46,7 % (1 957 110 / 4 194 304
  octets), RAM 34,4 %.
- Recherche exhaustive de `psi`/`Psi`/`PSI` résiduel sur tout le dépôt après
  renommage : plus aucune occurrence hors `vaPSINiddle` (intentionnel, §3) et
  quelques faux positifs de sous-chaîne (`kO2DoseEpsilonMl`, `collapsible`).
- `data/wc/*.j` (SPIFFS) régénéré au build : les clés `pressure_*` sont bien
  présentes, plus aucune clé `psi_*`.

> Note environnement : lancer `pio` depuis **PowerShell** natif Windows, pas
> depuis le shell Bash/MSys (rejet d'`esptool`).
