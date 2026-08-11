# Note de travail — versionnage du firmware, du SPIFFS et du Nextion

> Statut : **étapes 1 et 1bis implémentées** (version embarquée dans l'image
> SPIFFS ; manifeste alimenté par le build), étapes 2 à 4 proposées, non
> implémentées.
> Date : 2026-08-08, complétée le 2026-08-11
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

## Étape 1bis (implémentée) — le manifeste republie l'identité du build

L'étape 1 a réglé « ce qui tourne » ; il restait « ce qui est disponible ».
[export_binaries.py](../../scripts/export_binaries.py) n'utilisait aucune des
identités produites par le build : il les **refabriquait** depuis le système de
fichiers — `version` parsée dans le *nom du fichier*, `build_date` = **mtime**. Soit
exactement `extractVersionFromUrl_` supprimé au §« Ce qui clochait », réapparu côté
producteur au lieu du consommateur.

Symptôme visible en page Mise à jour : pour un même 4.1.6, colonne « actuelle »
11:27:03 (début du build, `FLOW_BUILD_REF`) contre « disponible » 11:27:47 (fin du
link, mtime). Les 44 s sont le temps de compilation ; les deux colonnes ne
partageaient aucune donnée.

Deux défauts de fond, pas seulement d'affichage :

1. **Le manifeste était recalculé en entier à chaque copie**, depuis les mtime
   courants du dossier. Ce n'était pas un enregistrement mais une photo de
   `binary/` — dossier hors git (`git ls-files binary` → 0), donc une recopie
   réécrivait les 76 dates d'un coup.
2. **C'était déjà faux** : avant correction, `flowios3-2.0.3.bin` était daté
   17:03:43 alors que `2.0.4` était daté 15:23:51.

### Ce qui a changé

- **Side-car par artefact.** Chaque `.bin` publié est accompagné d'un
  `<software>-<version>.json` (`schema: flowio.artifact.v1`) écrit par le build qui
  l'a produit. Le manifeste devient une agrégation de ces fichiers.
- **L'entrée vient du build, pas du disque** : `version` ← `custom_version` (même
  source que `FIRMW` et que `/fsver.j`), `build_ref` ← le define `FLOW_BUILD_REF`,
  `build_date` ← **dérivé de `build_ref`** et non du mtime, plus `sha256`, `size`,
  `env`. Pour le SPIFFS, `content_hash`/`files`/`bytes` sont **relus dans le
  `fsver.j` du staging** : le manifeste et l'image annoncent la même empreinte, et
  deux images d'une même `custom_version` cessent d'y être indiscernables.
- **Mise à jour additive** : le manifeste précédent est rechargé et seules les
  entrées republiées changent. Vérifié sur l'arbre réel — un `buildfs` ne produit
  plus qu'un diff de deux entrées (`generated_at` + l'artefact reconstruit) là où
  il réécrivait 76 lignes de dates.
- Le nom de fichier publié est **construit** depuis `software` + `version` au lieu
  d'être réanalysé ensuite ; `_ARTIFACT_RE` ne sert plus qu'à l'inventaire.

Priorité de résolution d'une entrée : **side-car** (produit par le build de ce
fichier précis) → **entrée déjà publiée** → **repli disque**. Le repli existe pour
les binaires antérieurs à ce dispositif et pour les `.tft` déposés à la main ; il
est marqué `"source": "filesystem"` pour ne pas laisser croire que la date est
fiable. Les artefacts produits par le build portent `"source": "build"`.

Effet de bord voulu : `build_date` désigne désormais le **début** du build, comme
l'estampille embarquée. Les deux colonnes de la page Mise à jour affichent la même
valeur au caractère près dès que l'artefact affiché a été flashé depuis le même
build.

### Ce qui n'a pas bougé

- **L'UI n'a pas été touchée.** Elle lit toujours `build_date` (ISO), simplement
  fiable désormais ; `build_ref` est un champ additionnel. Et le choix du « plus
  récent » passait déjà par `compareFirmwareVersions`, la date ne servant que de
  départage ([app.js](../../data/webinterface/app.js) — `compareUpgradeArtifacts`,
  `manifestArtifactEntries`).
- Les 75 entrées antérieures gardent leurs dates issues de mtime, figées telles
  quelles : aucune source ne permet de les reconstituer. Elles se corrigeront à la
  republication.
- `.gz` et `.pkg` restent hors manifeste (le firmware les devine), et sont
  désormais ignorés sans avertissement — comme les side-cars.

### Reste ouvert

L'estampille reste dérivée de l'horloge, pas du code : voir l'étape 4.

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

réduirait la fonction à un lookup. Depuis l'étape 1bis, `source` a une valeur de
plus à porter : le manifeste distingue déjà `build` (identité produite par le build)
de `filesystem` (repli sur le nom et le mtime), et l'UI gagnerait à ne pas présenter
les deux avec la même autorité.

Attention, **deux** limites à relever ensemble avant d'ajouter ce bloc, sinon
l'endpoint casse au lieu de tronquer : le `StaticJsonDocument<1024>` déjà bien
rempli, **et** le tampon de sérialisation `char out[960]`
([WebInterfaceServer.cpp](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp)
— fin du handler `web.meta`). Dépassement du second = `serializeJson` renvoie 0 ou
≥ `sizeof(out)` et le handler répond **500** ; c'est au moins bruyant, contrairement
au débordement du document JSON qui, lui, tronque en silence.

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

Avec l'étape 1bis, le manifeste porte aussi `content_hash` : une désynchronisation
peut se signaler **sans rien télécharger**, en comparant l'empreinte annoncée pour
l'image disponible à celle que `FilesystemVersion::contentHash()` lit dans l'image
présente. C'est la seule comparaison qui distingue deux SPIFFS de même
`custom_version` mais de contenu différent.

## Étape 4 (proposée) — `build_ref` dérivé du commit, pas de l'horloge

[generate_build_version.py](../../scripts/generate_build_version.py) pose
`FLOW_BUILD_REF = datetime.now()`, **réévalué à chaque invocation de `pio`**. Trois
conséquences, toutes constatées :

1. `pio run` et `pio run -t buildfs` du même arbre produisent deux estampilles
   différentes (11:27:03 pour le firmware, 11:28:22 pour le SPIFFS d'un même
   4.1.6) : les deux artefacts d'une même livraison ne sont pas rattachables l'un à
   l'autre par leur `build_ref`.
2. Deux builds d'un même commit donnent deux versions complètes différentes, alors
   que le binaire est identique. L'identifiant répond à « quand j'ai lancé la
   commande », pas à « quel code tourne ».
3. Corollaire de (1), l'étape 3 doit explicitement s'interdire de comparer les
   `build_ref` — contrainte qui disparaîtrait ici.

Remplacer par l'identité du commit : `git rev-parse --short HEAD`, suffixe `-dirty`
si l'arbre est sale, repli sur la date actuelle hors dépôt git (archive, CI sans
historique). `FLOW_FIRMWARE_VERSION_FULL` deviendrait `4.1.6+a1b2c3d`, et
`4.1.6+a1b2c3d-dirty` pendant le développement.

Points à trancher avant :

- **Le format d'affichage change.** [app.js](../../data/webinterface/app.js)
  scinde sur le `+` puis passe la partie droite à `formatUpgradeBuildStamp()`, qui
  reconnaît `YYYYMMDD.HHMMSS` et retombe sur `Date.parse` : un SHA ne matche ni
  l'un ni l'autre et serait **affiché brut** — acceptable, mais à décider. La date
  reste disponible via `build_date` du manifeste, qui devrait alors venir de la
  date du commit et non plus de `build_ref`.
- **`upgradeBuildStampValue()` retournerait 0** pour un SHA, donc plus de départage
  par date à version égale. Sans conséquence tant que les versions diffèrent.
- **Un `pio run` sans changement de commit ne produirait plus de nouvel
  identifiant** : c'est le but (reproductibilité), mais cela retire le repère qui
  permet aujourd'hui de vérifier d'un coup d'œil qu'un flash a bien pris. Le
  `-dirty` couvre le cas du développement en cours ; le `content_hash` du SPIFFS
  couvre le reste.
- Aucun effacement NVS : ni `FIRMW`, ni les clés de config, ni `/fsver.j` ne
  changent de forme.
