# Audit — configuration par défaut « mode piscine » (Waveshare-S3)

Date : 2026-07-04. Branche : `refactor/board-profiles-io-cleanup`.

Objectif : inventorier les valeurs par défaut de configuration du firmware Waveshare-ESP32-S3
(celles qui s'appliquent sur NVS vierge ou après `system.factory_reset`), identifier où elles
vivent, et préparer le câblage de `DomainSpec::poolLogicDefaults` (défauts métier fournis par
le profil, aujourd'hui rempli mais jamais lu). Décisions déjà actées : la carte reste **livrée
désactivée** (`pl_en=false`, modes auto off) ; le mécanisme cible est le spec par profil.

## 1. Mécanique d'un défaut (rappel)

1. Le membre C++ du module est initialisé à sa valeur par défaut compile-time
   (ex. `float phSetpoint_ = PoolDefaults::PhSetpoint;`).
2. Une `ConfigVariable` lie ce membre à une clé NVS + un nom JSON (`registerVar` en `init()`).
3. `ConfigStore::loadPersistent()` (`src/Core/ConfigStore.cpp:311`) lit chaque clé avec la
   valeur RAM comme fallback : **si la clé NVS n'existe pas, la valeur RAM au moment du load
   EST le défaut**.
4. `system.factory_reset` efface la NVS → retour intégral aux défauts compile-time.
   Migrations versionnées par `cfg_ver` (v3 : purge des clés IO slottées).

Fenêtre d'injection par profil : dans `setupProfile()` les fonctions `configureIoModule()` et
`configurePoolDevices()` poussent les presets **avant** `ctx.moduleManager.initAll()`
(`src/Profiles/Waveshare/WaveshareBootstrap.cpp:232-245`), donc avant `loadPersistent()`.
Tout défaut injecté là se comporte exactement comme un défaut compile-time. C'est la fenêtre
que devra utiliser `applyDomainDefaults()`.

## 2. PoolLogicModule — matrice complète (65 ConfigVariable)

Membres et déclarations : `src/Modules/PoolLogicModule/PoolLogicModule.h:143-417`.
Clés NVS : `include/Core/NvsKeys.h:142-208`. Sources de défaut :
**PD** = `PoolDefaults::` (`src/Domain/Pool/PoolDefaults.h`), **LIT** = littéral dans le
module, **DUP** = littéral identique à une constante `PoolDefaults` existante (duplication),
**MOD** = constante du module avec `#if` par profil, **DOM** = ID symbolique du domaine.

### poollogic/modes

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| enabled | pl_en | bool | false | LIT |
| auto_mode | pl_auto | bool | false | LIT |
| winter_mode | pl_wint | bool | false | LIT |
| disinfection_type | pl_dtype | u8 | 0 (Chlore/Brome) | LIT |

### poollogic/filtration

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| wat_temp_lo_th | pl_tlow | float | 12.0 | PD TempLow |
| wat_temp_setpt | pl_tset | float | 24.0 | PD TempHigh |
| filtr_start_min | pl_smin | u8 | 8 | PD FiltrationStartMinHour |
| filtr_stop_max | pl_smax | u8 | 23 | PD FiltrationStopMaxHour |
| filtr_start_clc | pl_fcst | u8 | 8 | PD (fenêtre **calculée**, persistée runtime) |
| filtr_stop_clc | pl_fcen | u8 | 23 | PD (idem) |

### poollogic/sensors (IoIds — voir §7)

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| ph_io_id | pl_phiid | u16 | AI slot 1 | MOD |
| dis_io_id | pl_oiid | u16 | AI slot 0 | MOD |
| pressure_io_id | pl_piid | u16 | AI slot 2 | MOD |
| wat_temp_io_id | pl_wiid | u16 | AI slot 4 | MOD |
| air_temp_io_id | pl_aiid | u16 | AI slot 5 | MOD |
| pool_lvl_io_id | pl_liid | u16 | DI 2 (Waveshare) / DI 0 (générique) | MOD `#if FLOW_PROFILE_WAVESHARE` |
| ph_lvl_io_id | pl_phli | u16 | DI 0 / DI 1 | MOD `#if` |
| chl_lvl_io_id | pl_clli | u16 | DI 1 / DI 2 | MOD `#if` |

### poollogic/safety

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| pressure_low_th | pl_psil | float | 0.15 | **DUP** (≡ PD PressureLow) |
| pressure_high_th | pl_psih | float | 1.80 | **DUP** (≡ PD PressureHigh) |
| winter_start_t | pl_wstr | float | -2.0 | **DUP** (≡ PD WinterStartTempC) |
| freeze_hold_t | pl_whld | float | 2.0 | **DUP** (≡ PD FreezeHoldTempC) |
| pressure_start_dly_s | pl_psdt | u8 | 60 | **DUP** (≡ PD PressureStartupDelaySec) |

### poollogic/ph

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| ph_auto_mode | pl_pha | bool | false | LIT |
| ph_dose_plus | pl_phpl | bool | false | LIT |
| ph_setpoint | pl_phsp | float | 7.4 | PD |
| ph_kp / ph_ki / ph_kd | pl_phkp/pl_phki/pl_phkd | float | 2 000 000 / 0 / 0 | PD |
| ph_window_ms | pl_phwms | i32 | 3 600 000 | PD PidWindowMs |

### poollogic/chlorine

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| dis_auto_mode | pl_orpa | bool | false | LIT |
| dis_setpoint | pl_orps | float | 700 | PD OrpSetpoint |
| dis_kp / dis_ki / dis_kd | pl_okp/pl_oki/pl_okd | float | 4500 / 0 / 0 | PD |
| dis_window_ms | pl_owms | i32 | 3 600 000 | PD PidWindowMs |

### poollogic/heater

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| heater_auto_mode | pl_hta | bool | false | LIT |
| heater_setpoint | pl_htsp | float | 27.0 | PD HeaterSetpoint — **absent du spec** |

### poollogic/swg

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| swg_control_mode | pl_swgm | u8 | SwgControlContinuous | LIT |
| secure_elec_t | pl_sect | float | 15.0 | **DUP** (≡ PD SecureElectroTempC) |
| dly_electro_min | pl_delt | u8 | 10 | **DUP** (≡ PD DelayElectroMin) |

### poollogic/regulation

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| pid_min_on_ms | pl_pmon | i32 | 30 000 | PD |
| pid_sample_ms | pl_psamp | i32 | 30 000 | PD |
| dly_pid_min | pl_dpds | u8 | 5 | **DUP** (≡ PD DelayPidsMin) |

### poollogic/robot, /refill

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| robot_delay_min | pl_rdel | u8 | 30 | **DUP** (≡ PD RobotDelayMin) |
| robot_dur_min | pl_rdur | u8 | 120 | **DUP** (≡ PD RobotDurationMin) |
| fill_min_on_s | pl_fmin | u8 | 30 | **DUP** (≡ PD FillingMinOnSec) |

### poollogic/o2 — distinguer config et curseurs d'état

Config utilisateur (7 — candidates au spec, aujourd'hui **littéraux hors PoolDefaults**) :

| JSON | Clé NVS | Type | Défaut | Source |
|---|---|---|---|---|
| pool_volume_m3 | pl_o2vol | float | 50.0 | LIT |
| dose_ml_10m3_week | pl_o2dose | float | 500.0 | LIT |
| main_hour | pl_o2hr | u8 | 20 | LIT |
| split_count | pl_o2spl | u8 | 2 | LIT |
| temp_comp | pl_o2tc | bool | true | LIT |
| load_factor | pl_o2load | float | 1.0 | LIT |
| min_filter_run_min | pl_o2mfr | u8 | 10 | LIT |

Curseurs de protocole persistés (4 — **état runtime, PAS des défauts à profiler**) :
`protocol_state` (pl_o2st), `last_dose_day` (pl_o2day), `weekly_done_ml` (pl_o2done),
`pending_ml` (pl_o2pend).

### Slots rôle → PoolDevice (répartis dans les branches métier)

Ex-branche `poollogic/devices`, supprimée : chaque `*_slot` vit dans sa branche
métier (filtration/swg/robot/refill/ph/chlorine/heater), clés NVS inchangées.

| JSON | Clé NVS | Défaut | Source |
|---|---|---|---|
| filtr_slot / swg_slot / robot_slot / fill_slot / ph_pump_slot / dis_pump_slot / heater_slot | pl_sfil / pl_sswg / pl_srob / pl_sfill / pl_sphp / pl_sorp / pl_shea | `PoolIds::Device*` | DOM — cohérents par construction, pas besoin de spec |

## 3. PoolDeviceModule — déjà alimenté par le domaine

`configurePoolDevices()` (bootstrap) instancie les 8+8 devices depuis
`PoolDomain::kPoolDevices` (presets : type, débit, réservoir, interlock, uptime max —
valeurs `PoolDefaults::Peristaltic*`, `*MaxUptimeDaySec`). Les `ConfigVariable` par slot
sont créées dynamiquement en PSRAM (`PoolDeviceLifecycle.cpp:66-76`) avec les valeurs du
preset comme défauts. Clés : `pd%u{en,dp,flh,tc,ti,mu}` + blob runtime `pd%urt`.
**Rien à faire ici : c'est le modèle cible.**

## 4. IOModule

- **Endpoints slottés** (`io_aNN*`, `io_iNN*`, `io_dNN*`, générés par `IOConfigSlots.h`) :
  défauts par rôle injectés par `configureIoModule()` depuis `WaveshareIoLayout.h`
  (`kAnalogRoleDefaults`, `kDigitalInputRoleDefaults`, `kDigitalOutputRoleDefaults`).
  **Déjà par profil.**
- **Drivers/bus** (`NvsKeys::Io`, ~45 clés) : défauts dans `IOModuleTypes.h:17-58`
  (`IOModuleConfig`), universels : sht40/bmp280/bme680/powermon/ds2484 **false**,
  mcp23017 **true**, oneWire1/oneWire2 **true**, pcf `FLOW_WIRDEF_IO_PCFEN`.
  Compatibles avec le matériel Waveshare (ADS externe activé via les bindings, TCA9554
  activé à la demande par les sorties). Pas de blocage, mais ces enables sont un 2e endroit
  où un défaut « produit » est universel plutôt que par board/profil.

## 5. Réseau et système (inventaire léger)

| Module | Branche | Défauts notables (NVS vierge) | Remarque |
|---|---|---|---|
| WifiModule | wifi | enabled=true, ssid="", pass=`FLOW_WIRDEF_WIFI_PASS` ("" hors overrides) | provisioning AP prend le relais |
| EthernetModule | ethernet | **enabled=false** | la Waveshare a un W5500 câblé (`ENABLE_ETHERNET=1` compile le support) — candidat à `true` par défaut sur ce profil |
| MQTTModule | mqtt | **enabled=false sur Waveshare** via `#if FLOW_PROFILE_WAVESHARE` (`MQTTModule.h:26-30`), sinon `FLOW_WIRDEF_MQ_EN` ; host/user/pass/base = macros `FLOW_WIRDEF_MQ_*` | hors overrides Wokwi les défauts embarquent **des identifiants de dev en dur** (`flowio.cloud.shiftr.io`, user/pass — `WokwiDefaultOverrides_Generated.h`) ; 3e spécialisation par macro à ranger |
| HAModule | ha | enabled=true, discoveryPrefix="homeassistant", entityPrefix=`FLOW_WIRDEF_HA_ENTITY_PREFIX` ("fio") | inopérant tant que MQTT off — cohérent |
| TimeModule | time | NTP pool.ntp.org / time.nist.gov, tz=`CET-1CEST…` (Europe/Paris), enabled=true | TZ correcte pour la cible ; la fenêtre de filtration en dépend |
| SystemModule | system | sys_lang, sys_dname | neutres |
| SystemMonitor | systemmonitor | trace + watchdog web (7 clés `sm_*`) | neutres |
| AlarmModule | alarm | al_en, al_epms | à vérifier si `enabled=true` attendu en prod |
| HMI / Buzzer / HmiUdpServer / LogHub / FirmwareUpdate | hmi, … | enables HMI (`hmi_*`), buzzer, token UDP | neutres pour le métier piscine |

Hors build Waveshare (exclus par `build_src_filter`) : Micronova, FlowConnectDisplay,
I2CCfgServer/Client, SupervisorHMI, TFTModuleS3.

## 6. Écart `PoolLogicDefaultsSpec` ↔ réalité

Le spec (`src/Domain/DomainTypes.h:91-118`, 26 champs) couvre : filtration (4), Pression (2+delay),
hiver/électrolyse (3), setpoints pH/ORP (2), PID pH/ORP (6), fenêtres PID (3), délais (5),
remplissage (1).

Non couverts aujourd'hui :

| Groupe | Champs | Recommandation |
|---|---|---|
| Chauffage | `heaterSetpoint` (27.0) | **à ajouter au spec** (seul setpoint métier manquant) |
| O2 actif | 7 champs de config (§2) | **à ajouter au spec** + constantes `PoolDefaults::O2*` (aujourd'hui littéraux) |
| Modes | enabled, auto, winter, disinfection_type, swg_control_mode, ph/orp/heater_auto, ph_dose_plus | laisser en littéraux module (décision : livré désactivé ; pas une affaire de profil) |
| IoIds capteurs | 8 champs | **ne pas mettre dans le spec** : à dériver du `DomainSpec` (§7) |
| Slots devices | 7 champs | rien à faire (IDs symboliques du domaine) |
| Fenêtre calculée | filtr_start_clc / filtr_stop_clc | état runtime persisté, pas un défaut |

## 7. IoIds capteurs : alignés mais dupliqués

`PoolLogicModule.h:146-161` réplique en dur le mapping du profil :
analogiques slots 0-5 (ORP, pH, Pression, eau=4, air=5) + un `#if defined(FLOW_PROFILE_WAVESHARE)`
pour les DIN (level=DI2/pH=DI0/chl=DI1 sur Waveshare, 0/1/2 sinon). Vérifié **aligné** avec
`kDomainIoSlotBindings` (`WaveshareIoLayout.h`) et `PoolDomain::kDomainIoSlots` (générique).

Le commentaire du code acte déjà la dette : « Réplique le mapping domainIoSlotBindings du
profil compilé ; à terme ces defaults devraient être injectés depuis le DomainSpec plutôt
que par macro. » Le câblage `applyDomainDefaults()` doit donc dériver ces 8 défauts via
`findIoSlotForDomainSlot(domain, PoolIds::Sensor*)` + `ioIdFromSlot()` et supprimer le `#if`.

## 8. Synthèse des trous / incohérences

1. **`DomainSpec::poolLogicDefaults` jamais consommé** (grep : rempli dans `PoolDomain.h:81`
   et `WaveshareProfile.cpp:23`, lu nulle part). Idem `configurationHook`.
2. **10 littéraux DUP** dans PoolLogicModule dupliquant `PoolDefaults` (§2 safety/swg/
   regulation/robot/refill) — dérive silencieuse possible ; à remplacer par le spec (ou au
   minimum par les constantes).
3. **8 IoIds capteurs** dupliqués par `#if` profil (§7) — à dériver du domaine.
4. **O2 config** : 7 littéraux hors `PoolDefaults`, absents du spec.
5. **`heaterSetpoint`** absent du spec.
6. **Spécialisations par macro dans les modules** : MQTT `enabled` (`#if FLOW_PROFILE_WAVESHARE`),
   IoIds DIN (§7) — le spec par profil est le bon endroit pour la partie métier ; le cas MQTT
   relève d'un futur « NetworkDefaults » par profil (hors périmètre actuel, à noter).
7. **Ethernet `enabled=false`** par défaut sur une carte au W5500 soudé (décision produit à
   prendre — hors périmètre PoolLogic).
8. **Identifiants réseau de dev en dur** dans les défauts MQTT hors overrides
   (`WokwiDefaultOverrides_Generated.h` : broker shiftr.io + user/pass) — à purger ou
   déplacer un jour vers le provisioning ; sans lien avec le spec métier.
9. Les curseurs O2 et la fenêtre de filtration calculée sont des **états persistés** déguisés
   en config — ne jamais les inclure dans un preset/spec.

## 9. Câblage implémenté (2026-07-04)

1. `PoolLogicDefaultsSpec` étendu (`src/Domain/DomainTypes.h`) : + `heaterSetpoint`,
   + 7 champs O2 ; constantes `PoolDefaults::O2*` ajoutées et `kLogicDefaults` complété
   (`src/Domain/Pool/PoolDefaults.h`).
2. `PoolLogicModule::applyDomainDefaults(const DomainSpec&)`
   (`src/Modules/PoolLogicModule/PoolLogicLifecycle.cpp`) : copie du spec dans les membres
   **et** dérivation des 8 IoIds capteurs via `findIoSlotForDomainSlot()` + `ioIdFromSlot()`.
   Le `#if FLOW_PROFILE_WAVESHARE` des `IO_ID_*_DEFAULT` est supprimé (le mapping générique
   reste comme filet) ; les littéraux DUP du §2 pointent désormais sur `PoolDefaults::`.
3. Appel dans `WaveshareBootstrap.cpp` et `FlowIOBootstrap.cpp` juste après
   `configurePoolDevices()`, donc avant `initAll()`/`loadPersistent()`.
4. Les initialisations membres restent en place (filet si `poolLogicDefaults == nullptr`).
5. `configurationHook` reste non câblé (accroche disponible, documentée ici).

Pour faire diverger Waveshare du générique : créer un `WavesharePoolDefaults` (mêmes champs)
et le référencer dans `kWavesharePoolDomain` (`WaveshareProfile.cpp`) à la place de
`&PoolDefaults::kLogicDefaults` — aucun autre changement nécessaire.

Statut : **audit terminé, câblage implémenté. Reste : validation matérielle après erase NVS
(checklist de `plan-items-9-10-remise-a-plat-io.md`).**
