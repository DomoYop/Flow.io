# Note de travail — versionnage du firmware, du SPIFFS et du Nextion

> Statut : **étape 1 implémentée** (version embarquée dans l'image SPIFFS),
> étapes 2 et 3 proposées, non implémentées.
> Date : 2026-08-08
> Contexte : la version du contenu SPIFFS était déduite du nom de fichier de l'URL
> OTA puis persistée en NVS comme variable de config, alors que la version du
> firmware est simplement compilée dans le binaire. Deux mécanismes pour la même
> question, dont un seul est fiable.

## Constat : trois mécanismes, pas deux

| Composant | Source de vérité | Où | Persistance |
|---|---|---|---|
| Firmware ESP32 | macro compile-time `FIRMW` (= `custom_version` de l'env) + `FLOW_BUILD_REF` | [FirmwareVersion.h](../../include/Core/FirmwareVersion.h) → `FirmwareVersion::Full` | aucune — embarquée dans le `.bin` |
| Nextion TFT | interrogée sur l'écran au runtime, comparée à `TFT_FIRMW` (attendu) | [HMIModule.cpp:100](../../src/Modules/HMIModule/HMIModule.cpp#L100), [NextionDriver.cpp:556](../../src/Modules/HMIModule/Drivers/NextionDriver.cpp#L556) | aucune — mesurée |
| SPIFFS (**avant**) | déduite du nom de fichier de l'URL OTA, puis écrite en NVS comme variable de config `fwupdate/spiffs_version` (clé `sp_version`) | `FirmwareUpdateModule` | NVS — inférée |

La version firmware « ne se voit nulle part » précisément parce qu'elle n'a besoin
de rien : elle est dans le binaire et lue directement par `HAModule`,
`SystemModule`, `WebInterfaceServer`, `ActivityLogModule`. C'est le bon modèle ;
le SPIFFS était le seul à ne pas le suivre.

## Ce qui clochait dans l'ancien mécanisme SPIFFS

1. **Inférence sur une chaîne d'URL.** `extractVersionFromUrl_` extrayait `4.1.2`
   de `flowios3-spiffs-4.1.2.bin`. URL renommée, redirection, fichier `latest.bin` :
   version fausse ou vide, sans erreur.
2. **Ne couvrait que le chemin OTA.** `pio run -t uploadfs`, flash usine,
   `esptool write_flash` : le contenu change, la NVS ne bouge pas → affichage
   périmé et silencieux. C'est le cas le plus fréquent en développement.
3. **Mauvais store.** `ConfigStore` porte des réglages utilisateur ; il s'agissait
   ici d'un état observé. Conséquences réelles : la clé était déclarée dans les
   cfgdocs sans `hidden`, donc **éditable à la main** depuis l'UI et MQTT ; elle
   partait dans les export/import JSON de config, donc **importer la config d'un
   autre appareil réécrivait une version SPIFFS sans rapport** ; et un effacement
   NVS (que ce projet impose régulièrement) l'effaçait alors que le SPIFFS était
   intact.
4. **Pas de granularité build.** Le firmware a `4.1.2+20260808.143512` ; le SPIFFS
   n'avait que `4.1.2`. Or `data/` change à chaque modification d'`app.js`, d'un
   i18n ou d'un cfgdocs, **sans** que `custom_version` bouge : deux images de
   contenu différent étaient indistinguables.
5. **Couplage inutile.** Lire cette version imposait que `FirmwareUpdateModule`
   soit compilé et passait par `FirmwareUpdateService::getSpiffsVersion`, là où
   `FirmwareVersion::Full` est une simple constante.

À noter : [`webAssetVersion_()`](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp#L1073)
calculait déjà un FNV-1a sur 5 fichiers SPIFFS — mais le combine au `BuildRef`
**du firmware**, pour du cache-busting HTTP. L'identité du contenu SPIFFS existait
donc à moitié, au mauvais endroit et pour un autre usage.

## Étape 1 (implémentée) — la version voyage avec l'artefact

Principe : le firmware embarque sa version dans le `.bin`, donc le SPIFFS embarque
la sienne dans l'image SPIFFS.

### Production : `/fsver.j`

[prepare_spiffs_data.py](../../scripts/prepare_spiffs_data.py) dispose déjà d'un
répertoire de staging (`$BUILD_DIR/spiffs_data`) : le fichier y est écrit juste
avant la construction de l'image, **et n'existe donc jamais dans `data/`** (rien à
ignorer dans git).

```json
{"schema":"flowio.fsver.v1","version":"4.1.2","build_ref":"20260808.143512",
 "env":"Waveshare-ESP32-S3","content_hash":"a1b2c3d4","files":412,"bytes":1438720}
```

- `version` : lue depuis `custom_version` de l'env — **même source que `FIRMW`**,
  donc cohérence garantie par construction.
- `build_ref` : relu depuis les `CPPDEFINES` posés par
  [generate_build_version.py](../../scripts/generate_build_version.py), qui tourne
  avant dans `extra_scripts`. Pas de second `datetime.now()` qui divergerait.
- `content_hash` : SHA-256 tronqué à 8 caractères, calculé sur le staging
  **complet et final** (chemins triés + tailles + contenus), donc sur ce qui est
  réellement flashé, gzip compris. Le fichier `fsver.j` est écrit après le calcul
  et ne s'inclut pas lui-même.

Propriété importante : `build_ref` change à chaque build, `content_hash` non. Deux
builds d'un `data/` inchangé donnent le même `content_hash` — c'est précisément la
grandeur qui manquait, puisque `custom_version` ne bouge pas quand un i18n change.

Les `.gz` sont produits avec `mtime=0` (déjà le cas), le hash est donc reproductible.

### Consommation : `FilesystemVersion`

[FilesystemVersion.h](../../include/Core/FilesystemVersion.h) /
[FilesystemVersion.cpp](../../src/Core/FilesystemVersion.cpp), API symétrique de
`FirmwareVersion` :

```cpp
FilesystemVersion::present();      // false = image antérieure au dispositif
FilesystemVersion::core();         // "4.1.2"
FilesystemVersion::buildRef();     // "20260808.143512"
FilesystemVersion::full();         // "4.1.2+20260808.143512"
FilesystemVersion::contentHash();  // "a1b2c3d4"
```

Chargement paresseux au premier appel, mis en cache — même pattern que
`webAssetVersion_()`. Deux détails volontaires :

- **pas de dépendance ArduinoJson** : extraction manuelle des champs (le producteur
  est notre propre script, le format est contrôlé). `src/Core/` est compilé par
  tous les profils, y compris ceux dont les `lib_deps` pourraient diverger.
- **échec de montage ≠ absence de fichier** : si `SPIFFS.begin(false)` échoue, rien
  n'est mis en cache et l'appel suivant retentera. Seul un montage réussi sans
  fichier fige `present() == false`. Sans ça, un appel arrivant avant le montage
  (l'ordre entre modules n'est pas garanti) figerait un « absent » définitif.

### Supprimé

- `extractVersionFromUrl_` et son appel dans `runSpiffsUpdate_`
- `spiffsVersionVar_`, `cfgData_.spiffsVersion`, le `registerVar` associé
- la clé NVS `sp_version`
- `getSpiffsVersion_` et le champ `getSpiffsVersion` de `FirmwareUpdateService`
- l'entrée `fwupdate/spiffs_version` des cfgdocs et ses 4 tokens i18n (fr + en)
- le membre `firmwareUpdateSvc_` de `WebInterfaceModule`, qui doublonnait
  `fwUpdateSvc_` et n'existait plus que pour cet appel

`/api/web/meta` continue d'exposer `spiffs_version`, mais avec la forme complète
`4.1.2+20260808.143512`. Côté UI, `splitUpgradeVersionStamp()` sépare déjà sur le
`+` : l'horodatage de build du contenu SPIFFS s'affiche donc sans modifier le JS.

### Migration

- Les appareils en service ont une image SPIFFS sans `/fsver.j` : `present()`
  retourne `false` et l'UI affiche `-`, exactement comme avant lorsque la NVS était
  vide. Le premier flash SPIFFS rétablit l'affichage.
- La clé NVS `sp_version` devient orpheline. `ConfigStore::loadPersistent` itère
  sur les variables enregistrées : une clé inconnue est simplement ignorée. Aucune
  migration à écrire, aucun effacement NVS nécessaire.
- Ce qui change de sens : la version affichée décrivait le contenu **demandé** lors
  de l'OTA, elle décrit désormais le contenu **réellement présent**. C'est le but.

### Coût

~200 octets dans SPIFFS, un SHA-256 sur ~1,4 Mo au build (quelques dizaines de ms),
aucune capacité compile-time touchée.

## Étape 2 (proposée) — inventaire de versions unique

L'UI reconstruit l'information à partir de trois globales JS alimentées par des
champs disparates (`supervisorFirmwareVersion`, `nextionDisplayVersion`,
`spiffsContentVersion`), avec un cas particulier par composant dans
`currentUpgradeVersionForComponent()`. Un bloc unique dans `/api/web/meta` :

```json
"versions": {
  "firmware": {"version":"4.1.2","build":"20260808.143512","source":"embedded"},
  "spiffs":   {"version":"4.1.2","build":"20260808.143512","source":"embedded","content":"a1b2c3d4"},
  "nextion":  {"version":"6.0.0","expected":"6.0.0","source":"probed"}
}
```

réduirait la fonction à un lookup. Attention : `/api/web/meta` utilise un
`StaticJsonDocument<1024>` déjà bien rempli — à redimensionner avant d'ajouter ce
bloc, sous peine de troncature silencieuse.

## Étape 3 (proposée) — signaler la désynchronisation firmware ↔ SPIFFS

Mode d'échec réel et aujourd'hui invisible : un OTA firmware sans OTA SPIFFS laisse
des cfgdocs et i18n d'une version antérieure. Or, par construction du projet, un
token i18n manquant **s'affiche brut** et une clé cfgdocs manquante disparaît de
l'UI — le symptôme est un libellé bizarre, jamais une erreur.

Avec les deux versions disponibles côte à côte, un log au boot et un badge dans la
page Mise à jour deviennent triviaux.

Nuance : comparer **`core()` uniquement**. Les `build_ref` diffèrent presque
toujours — un `pio run -t buildfs` seul en produit un nouveau — et servent au
diagnostic affiché, pas au verdict.
