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

Les fichiers d'intégration Home Assistant référencent les entités par leur
`unique_id`. Ils portaient encore `psi_low`/`psi_high` jusqu'à la deuxième passe
du §7, qui les a alignés sur `pressure_low`/`pressure_high`. Une installation HA
existante utilisant les anciens noms devra être re-liée aux nouvelles entités.

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
- `docs/integration/home_assistant_alarm_{dashboard,helpers}_pool.yaml`
  (renommés depuis `*_fio53.yaml` : le suffixe porte le préfixe d'entité HA,
  qui vaut `pool` sur cette installation)
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
- Recherche exhaustive de `psi`/`Psi`/`PSI` résiduel **dans `src/` et `include/`**
  après renommage : plus aucune occurrence hors `vaPSINiddle` (intentionnel, §3)
  et quelques faux positifs de sous-chaîne (`kO2DoseEpsilonMl`, `collapsible`).
  La documentation et les fichiers d'intégration, eux, n'avaient pas suivi — voir §7.
- `data/wc/*.j` (SPIFFS) régénéré au build : les clés `pressure_*` sont bien
  présentes, plus aucune clé `psi_*`.

> Note environnement : lancer `pio` depuis **PowerShell** natif Windows, pas
> depuis le shell Bash/MSys (rejet d'`esptool`).

## 7. Deuxième passe — documentation et intégration (2026-08-11)

Le renommage de juillet portait sur `src/` et `include/`. La documentation, les
plans de tests et les fichiers d'intégration Home Assistant étaient restés au
vocabulaire « PSI », et **décrivaient des identifiants qui n'existaient plus** :
`PSI_BLOCKED`, `psi_low_th`, `psi_high_th`, `psi_start_dly_s`, `psi_io_id`,
`psiError_`, `AlarmId::PoolPsiLow/High`, la valeur de blocage O2 `psi`. Aucun de
ces symboles n'était présent dans les sources — la doc décrivait un firmware
révolu.

Corrigé dans : `docs/modules/PoolLogicModule.md`, `docs/modules/IOModule.md`,
`docs/integration/flowio-poollogic-business.md`,
`docs/integration/nextion-esp-protocol.md`,
`docs/rapport-analyse-domaine-piscine.md`, `docs/core/module-quality-gates.md`,
les deux plans de tests CSV et les trois fichiers d'intégration Home Assistant
(entités `pool_psi_low_*` → `pool_pressure_low_*`).

Les deux exceptions du §3 restent en place, pour les mêmes raisons :

- **`vaPSINiddle`** (voir ci-dessous) est désormais la seule exception : les clés
  NVS ont été renommées dans la foulée, voir §8.
- **`vaPSINiddle`** : nom de composant figé dans le projet Nextion Editor, hors
  de ce dépôt. Le renommer côté firmware sans régénérer le `.tft` casse l'aiguille
  de pression de l'écran V2 legacy.

## 8. Clés NVS renommées — schéma de config v3 (2026-08-11)

Dernier point du §3 levé. Le renommage existait depuis le 07/08 sur la branche
`refactor/fonctions-piscine-et-io` (commit `9447203`), jamais fusionnée dans
`main` ; il est ici porté seul, sans le reste de cette branche.

| Avant | Après | Type |
|---|---|---|
| `pl_psil` | `pl_prlow` | float (bar) |
| `pl_psih` | `pl_prhigh` | float (bar) |
| `pl_psdt` | `pl_prdelay` | uint8 (s) |

`pl_piid` est **conservée** : ce n'est pas une trace de « PSI » mais `p` +
suffixe `iid`, commun à tous les IoId (`pl_aiid`, `pl_wiid`, `pl_oiid`…).

`CURRENT_CFG_VERSION` passe de 2 à 3 et `mig_2_to_3`
([ConfigMigrations.h](../../src/Core/ConfigMigrations.h)) recopie chaque valeur
sous son nouveau nom puis efface l'ancienne. **Aucun effacement NVS requis, aucun
réglage perdu.** La migration est best-effort et renvoie toujours `true` : un
`false` déclencherait un `clear()` de toute la configuration.

Les trois clés `*Legacy` de `NvsKeys.h` ne servent qu'à cette migration. Elles
seront supprimables quand plus aucune installation ne pourra encore être en
schéma v2 — c'est-à-dire une fois toutes les cartes passées à ce firmware.
