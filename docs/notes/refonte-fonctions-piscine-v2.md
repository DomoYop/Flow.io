# Refonte « une fonction piscine = une page » (v2)

**État : implémenté et compilé** (`pio run -e Waveshare-ESP32-S3`).
Remplace la note `refonte-fonctions-piscine-et-io.md`, dont la branche a été abandonnée.

> ⚠️ **Effacement NVS obligatoire avant de flasher.** La renumérotation des `PoolDevice`
> change le sens des clés `pdN*` : `pd3*` désignait le robot et désigne maintenant
> l'électrolyseur, `pd4*` passait de RELAY_STD à PERISTALTIC. Une valeur héritée activerait
> silencieusement le mauvais équipement.

## Le problème

`pdN` et `dNN` étaient le même objet portant deux noms, réglés dans deux menus différents.
Brancher une fonction demandait trois pages sans lien visible entre elles :

1. « Équipements » (`pdm`) → cocher l'appareil installé ;
2. la page métier PoolLogic → un champ `*_slot` choisissant quel `pdN` la fonction pilote ;
3. `io/output/dNN` → le port physique du relais.

## Le modèle retenu

Les trois couches internes sont **conservées** ; seule leur présentation change.

| Couche | Rôle | Où |
|---|---|---|
| **PoolLogic** | le *quand* — régulations, automatismes | `src/Modules/PoolLogicModule/` |
| **PoolDevice** | le *à quelles conditions* — interlocks, quotas, comptage d'heures | `src/Modules/PoolDeviceModule/` |
| **IOModule** | le *sur quel fil* — `dNN` → `binding_port` → relais | `src/Modules/IOModule/` |

**Une fonction = un `PoolDevice` = une sortie logique `dNN` de même index.** Le lien n'est plus
un réglage : il est porté par `PoolIds::Device*` et vérifié à la compilation. Le seul choix
laissé à l'utilisateur est le **relais physique**.

Chaque page `piscine/<fonction>` présente, dans cet ordre : **Activation** (`pdm/pdN/enabled`),
**Sortie physique** (`io/output/dNN/binding_port`), **Équipement** (débit, cuve, quotas),
**Régulation** (paramètres PoolLogic).

## Les 12 fonctions

| pd | Fonction | Rôle domaine | dNN | Type | Port d'usine |
|---:|---|---|---|---|---|
| 0 | Filtration | `ActuatorFiltrationPump` | d00 | FILTRATION | EXIO1 |
| 1 | Régulation pH | `ActuatorPhPump` | d01 | PERISTALTIC | EXIO2 |
| 2 | Désinfection — chlore/brome | `ActuatorChlorinePump` | d02 | PERISTALTIC | — |
| 3 | Désinfection — électrolyseur | `ActuatorChlorineGenerator` | d03 | RELAY_STD | — |
| 4 | Désinfection — O2 actif | `ActuatorO2Pump` *(nouveau)* | d04 | PERISTALTIC | — |
| 5 | Remplissage automatique | `ActuatorFillPump` | d05 | RELAY_STD | EXIO5 |
| 6 | Éclairage | `ActuatorLights` | d06 | RELAY_STD | EXIO7 |
| 7 | Chauffage | `ActuatorWaterHeater` | d07 | RELAY_STD | EXIO8 |
| 8 | Robot de nettoyage | `ActuatorRobot` | d08 | RELAY_STD | EXIO4 |
| 9 | Recopie temporisée du débit | `ActuatorFlowCopy` | d09 | RELAY_STD ¹ | — |
| 10 | Report d'état du volet | `ActuatorCoverClosed` | d10 | RELAY_STD ¹ | — |
| 11 | Sortie auxiliaire 1 | `ActuatorAux1` *(nouveau)* | d11 | RELAY_STD | — |

¹ `exposeHaSwitch = false` et `externallyCommandable = false`.

EXIO3 et EXIO6 restent libres pour le mode de désinfection retenu. **8 relais physiques
seulement** : 12 fonctions déclarées, 8 branchables simultanément. Une fonction non liée est
inerte (écriture no-op) — c'est le comportement voulu pour les modes exclusifs.

Sur `FlowIO`, les fonctions 4 et 9..11 ne sont pas déclarées (gardes
`FLOW_BOARD_WAVESHARE_ESP32_S3`) ; les 8 autres gardent les mêmes index.

### Pourquoi chlore et O2 sont deux appareils

Les paramètres de *régulation* étaient déjà distincts dans PoolLogic (PID ORP d'un côté,
protocole hebdomadaire de l'autre). Ce que la duplication apporte, c'est un **débit de pompe,
un bidon et des compteurs de consommation séparés** : sans elle, basculer chlore → O2 pollue
les cumuls et oblige à ressaisir `flow_l_h`/`tank_cap_ml`. Coût : aucune nouvelle clé NVS (les
16 descripteurs de `PoolDeviceSlots.h` existaient déjà).

`orpPumpDeviceSlot_` est le seul slot dérivé : `updateDisinfectionDeviceSlot_()` le fait suivre
`disinfection_type`. L'ancienne pompe est arrêtée **avant** la bascule, sinon elle resterait en
marche sans pilote.

## Ce qui a été supprimé

- Les 7 champs `*_slot` de PoolLogic et leurs clés NVS `pl_sfil`, `pl_sswg`, `pl_srob`,
  `pl_sfill`, `pl_sphp`, `pl_sdis`, `pl_shea`. **Ne pas réutiliser ces noms.**
- L'`enum_set` `poollogic_device_slot` et son support dans `scripts/io_port_labels.py`.
- La page « Équipements » (`pdm`), désormais `hidden` : c'est une branche de stockage, pas un menu.
- Côté `app.js` : `poolLogicDeviceSlot*`, `moduleHasDeviceSlotField`,
  `loadPoolLogicDeviceSlotLabels`, `composeModuleFromSlot` et le support `module_from_slot`.
- Dans `HMIModule`, la lecture I2C des 5 slots (5 exports par cycle) devenue sans objet.

## Arborescence de config

Obtenue **sans renommer une seule clé NVS**, par trois mécanismes déjà présents dans `app.js` :
`meta.cfg_tree_virtual_branches`, `meta.cfg_tree_aliases` et `meta.order` (ce dernier était
implémenté mais déclaré nulle part).

```
piscine/            order 10      bassin(10) sensors(20) filtration(100) ph(110)
                                  desinfection-chlore(120) …-electrolyseur(130) …-o2(140)
                                  remplissage(150) eclairage(160) chauffage(170) robot(180)
                                  debit(190) volet(200) aux1(210) securite(900)
io/                 order 800     page électrique brute, conservée
```

### Deux pièges à connaître

- **Le masquage d'alias est global à la racine store.** `cfgIsAliasStoreShadowPath`
  (`app.js`) masque tout chemin situé dans la branche *store* d'un alias mais hors de sa
  branche *display*. Un seul alias `piscine/bassin → poollogic/bassin` fait donc disparaître
  **toute** la racine `poollogic` de l'arbre. C'est l'effet voulu ici, mais toute branche
  `poollogic/*` doit être couverte par un alias ou par un `compose`, sinon elle devient
  inaccessible.
- **`requires` est évalué après ce masquage.** Une branche virtuelle déclarant
  `"requires": "poollogic"` ne serait jamais injectée, puisque `poollogic` a déjà été retiré de
  la liste des enfants. `piscine` utilise `"requires": "pdm"`, qui n'est ciblé par aucun alias.

### Le dropdown de relais ne coûte aucun JS

`renderCfgComposeSections` appelle `renderConfigFields` avec le module de la *section*, et
`configIsBindingPortField` teste la regex `^io/output/d\d{2}$` sur ce module. Une entrée
`{"module": "io/output/d01", "fields": ["binding_port"]}` produit donc le menu déroulant des
relais sans une ligne de code supplémentaire.

## Durcissements associés

- **`static_assert` remplaçant `requireSetup`.** L'invariant `pdN ⇔ dNN` était vérifié au boot
  par un `while(true) delay(1000)` : une table mal formée briquait la carte avec pour seule
  trace un log série. `PoolDomain::detail::poolDevicesDriveTheirOwnOutput()` en fait une erreur
  de compilation (vérifié en injectant une incohérence).
- **Collision de ports.** `IOModule::dropDuplicateOutputBindings_()` délie la seconde sortie
  quand deux réclament le même relais, et le journalise. Le risque préexistait ; rendre le
  dropdown accessible depuis chaque page l'augmente statistiquement.
- **`dependsOnMask` élargi en `uint16_t`.** Il était `uint8_t` alors que les boucles allaient
  jusqu'à `POOL_DEVICE_MAX` (16) : les casts `(uint8_t)(1u << i)` tronquaient silencieusement
  au-delà du slot 7.
- **Page composée réparée.** `ConfigStore::toJsonModule` fait un `strcmp` exact, donc
  `/api/flowcfg/module?name=pdm` renvoie 404 ; l'apply rechargeait la page par ce chemin et la
  cassait. `rechargerCfgPageCourante()` route désormais vers `chargerCfgComposeOnlyPage`.
  Ce bug existait sur `main` et aurait frappé les 12 nouvelles pages.

## Suite : le tableau de bord ne suivait pas la renumérotation

Constaté après un reset usine, sur la carte « Équipements » du tableau de bord. Trois défauts
distincts, tous nés du fait que la renumérotation `PoolIds::Device*` n'avait pas été propagée
aux valeurs Runtime UI.

- **Préfixe de port décalé.** `poolEquipmentPortPrefix` (`app.js`) indexe les sorties avec
  `valueId - 1`, ce qui supposait que les `valueId` de `pooldev` suivaient l'ordre des
  fonctions. Ils suivaient l'ordre historique (`Robot` = 4, `Électrolyseur` = 6). La tuile
  Robot lisait donc le port du slot 3 — non relié, d'où le repli sur le nom d'endpoint et
  l'affichage « io_chl_gen - Robot ». Filtration et pH tombaient juste par coïncidence.
  Corrigé en **rétablissant l'invariant jusqu'à l'UI : `valueId = index pd + 1`**, les 12
  fonctions étant désormais déclarées (les 4 nouvelles n'avaient aucune tuile). Le repli sur
  `io_name` est supprimé : une fonction sans relais n'est pas préfixée.
- **Deux cartes « Équipements ».** Le regroupement se fait sur le libellé traduit
  (`cardKey = domaine + '::' + groupe`) : `pooldev` écrivait `Equipements` et `io`
  `Équipements`, l'accent suffisait à créer une seconde carte. Au fond, `io.flow_copy_out` et
  `io.cover_out` faisaient doublon avec pd9/pd10 depuis cette refonte : ces deux valeurs sont
  supprimées de l'IOModule (ids 2223/2224 libérés) plutôt que renommées.
- **Sorties de report décalées d'un cran.** Ces mêmes valeurs lisaient `digitalOutputSlot(8)`
  et `(9)`, numéros d'avant la refonte. « Sortie recopie flowswitch » affichait donc l'état du
  **robot**. Disparaît avec la suppression ci-dessus, pd9/pd10 lisant leur propre slot.

Le mapping `valueId → slot` était recopié à trois endroits (`PoolDeviceRuntime.cpp`,
`WebInterfaceServer.cpp`, `app.js`) ; seul celui du serveur web alimente réellement l'UI, ce
qui explique qu'une correction partielle n'aurait rien changé à l'écran. Les trois sont
maintenant des calculs sur l'index, sans table, et `static_assert(PoolIds::DeviceCount == 12)`
garde le bloc `2301..2312`. Les libellés du tableau de bord reprennent la nomenclature des
pages de config (« Régulation pH », « Éclairage », « Chauffage », « Robot de nettoyage »).

Pas d'impact NVS : seuls les `RuntimeUiId` changent, et aucun n'est écrit en dur hors du
manifeste généré.

## Écart assumé par rapport au plan initial

La temporisation de la recopie du débit reste `flow_copy_delay_s` (`pl_fscdl`, branche
`poollogic/safety`) au lieu de basculer sur `pd9/on_delay_s`. Le snapshot runtime publie
`flow_copy` et `delay_s` et alimente des binary_sensors HA : basculer aurait obligé à réécrire
cette chaîne pour un gain nul, la page affichant le délai via `compose` de toute façon.
En contrepartie, `on_delay_s` est masqué sur pd9 et pd10 pour éviter deux réglages concurrents,
et `onDelaySec` doit rester à 0 sur ces deux appareils.
