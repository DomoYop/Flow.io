# Bilan — OTA SPIFFS compressé : où on en est, et par où reprendre

> **Statut au 2026-08-20 : résolu. L'avertissement qui figurait ici est levé.**
> L'OTA firmware et l'OTA SPIFFS sont fiables depuis la version `4.4.3`.
>
> La « panne non résolue » décrite plus bas — cinq écritures, cinq corruptions,
> `PC=0xFFFFFFFE`, tâche accusée variable — n'était **pas** un défaut du chemin
> d'écriture ni un problème de cache flash. C'étaient des **débordements de pile** :
> `mqtt` vivait à 36 octets du déclenchement du point d'arrêt de fin de pile, et la
> corruption du système de fichiers était la *conséquence* d'une panique survenue en
> pleine écriture, pas sa cause. Diagnostic, mesures et correctif dans
> [audit-paniques-flash-cache.md](audit-paniques-flash-cache.md).
>
> Les sections qui suivent sont conservées telles qu'elles ont été écrites, y compris
> le raisonnement réfuté : il était cohérent, il expliquait tous les faits, et il était
> faux. Savoir pourquoi évite de le refaire.
>
> Détail technique et historique complet :
> [ota-spiffs-reduction-volume.md](ota-spiffs-reduction-volume.md) et
> [ota-spiffs-lenteur-plantage.md](ota-spiffs-lenteur-plantage.md).
> Ce document est le point d'entrée : ce qui marche, ce qui reste ouvert, comment
> retester.

## Le problème traité

L'OTA du contenu web transférait **8,26 Mo pour 324 Ko utiles** (`mkspiffs` pade
l'image jusqu'à la taille de la partition), pendant plusieurs minutes, sur une liaison
WiFi médiocre — d'où des plantages à répétition. L'opération est de surcroît
destructive : une coupure en cours d'écriture laisse un système de fichiers illisible,
donc un appareil sans interface web, récupérable seulement par USB.

Une première tentative de transfert compressé (2026-08-11) n'avait jamais abouti sur
cible, après cinq correctifs appliqués sans aucun élément d'observation.

## Ce qui a changé

**1. Le `.gz` est ramené entier en mémoire, puis la connexion est fermée.** L'ancien
code décompressait *en flux* : la connexion TCP devait rester ouverte pendant toute
l'écriture des 8,26 Mo. Le volume était divisé par 25, mais pas la durée pendant
laquelle le WiFi devait tenir — c'est-à-dire pas le problème réel.

**2. L'image est prouvée intègre avant la première écriture.** Le trailer gzip porte la
taille décompressée et le CRC-32 : les deux sont vérifiés d'abord, par une passe de
décompression complète sans écriture. Auparavant, une incohérence de taille
n'apparaissait qu'en fin de flux, partition déjà détruite.

**3. Un essai à blanc (`dry run`) exécute tout sauf l'écriture et le redémarrage.**
C'est le mode d'observation qui manquait : il rejoue le code de production, pas une
doublure, et ne peut rien casser.

Le décompresseur, enfin, est alloué sur le tas — voir « la cause » plus bas.

## Mesures sur cible

Firmware `4.3.3+20260815.000211`, image `flowios3-spiffs-*.bin.gz` de 332 891 o (4,03 %
du `.bin`).

| Phase | Durée |
|---|---|
| Téléchargement 333 Ko **+ vérification complète** (8,26 Mo décompressés + CRC-32) | **~3 s** |
| Écriture des 8,26 Mo | ~42 s |
| Total, redémarrage compris | **~50 s** |

**La liaison WiFi n'est sollicitée que ~3 secondes**, contre plusieurs minutes
auparavant, et l'écriture se fait connexion fermée : une coupure radio pendant les 42 s
d'écriture n'a plus aucun effet.

Validation : deux OTA réels enchaînés, avec la version SPIFFS **changée puis restaurée**
(4.3.2 → 4.3.1 → 4.3.2) pour que la mesure prouve l'écriture — réécrire la même version
aurait été un faux positif si le firmware n'écrivait rien. Interface web à HTTP 200
après chacun. Cinq essais à blanc réussis auparavant, dont un juste après un
redémarrage et un à cheval sur le démarrage de `ha`.

## La cause des blocages de 2026-08-11

`tinfl_decompressor` pèse **~10,7 Ko** (trois tables de Huffman de 3 488 o) et était
déclaré **en variable locale**, dans une tâche dont la pile fait **6 144 octets**.
Débordement de pile au premier appel, avant même la première itération.

Tout s'explique de là : le chemin non compressé fonctionne parce qu'il n'appelle jamais
`tinfl` ; les cinq correctifs sur le yield et la machine à états ne pouvaient rien
changer, la fonction ne survivait pas à son prologue ; le portage Python de la machine
à états atteignait `DONE`, la logique étant juste.

Le premier essai à blanc a livré cette cause en une fois, sans rien détruire.

**Ne jamais remettre `tinfl_decompressor` sur la pile.** Il est alloué en DRAM interne,
PSRAM en repli.

## Point ouvert n° 1 — RÉSOLU : `/api/activity/logs` bloque `async_tcp`

**Cause identifiée le 2026-08-18 par lecture du vidage de crash.** Elle n'a **aucun
rapport avec l'OTA** : le lien « panique après une mise à jour » noté le 15/08 était une
coïncidence, les redémarrages suivant en réalité les consultations du journal
d'activité.

### Ce que dit le vidage

```
task    : async_tcp
reason  : Task watchdog got triggered. The following tasks/users
          did not reset the watchdog in time: - async
pc      : 0x400559DD
```

Backtrace décodé, du plus profond au plus haut :

```
xRingbufferSend ← cbuf::resize ← cbuf::resizeAdd
  ← AsyncResponseStream::write(uint8_t)          un octet à la fois
  ← Print::print(char)
  ← printJsonEscaped_             WebInterfaceServer.cpp:94
  ← writeActivityLogJsonEvent_    WebInterfaceServer.cpp:4725
  ← ActivityLogModule::readPage_  ActivityLogModule.cpp:273
  ← sendActivityLogHttpResponse_  WebInterfaceServer.cpp:4799
  ← lambda de route #23           WebInterfaceServer.cpp:5432   → /api/activity/logs
```

### Le mécanisme, et pourquoi il est quadratique

`printJsonEscaped_` sérialise **caractère par caractère** (`out.print(*p)`). Côté
bibliothèque :

```cpp
if (len > _content->room()) {
  size_t needed = len - _content->room();   // == 1 lorsqu'on écrit un octet
  _content->resizeAdd(needed);
```

et `cbuf::resize` crée un nouveau ringbuffer, **relit puis recopie l'intégralité du
contenu déjà écrit**, avant de libérer l'ancien. Une fois le tampon initial rempli
(~1,4 Ko par défaut), **chaque octet supplémentaire recopie toute la réponse en cours**.

Pour un journal de 317 entrées, soit ~80 Ko de JSON, cela représente des milliards
d'octets copiés — dans le callback HTTP, qui s'exécute dans la tâche `async_tcp`. Cette
tâche cesse de nourrir le watchdog, qui redémarre la carte au bout de 5 s.

Tout le tableau observé s'explique alors :

- **les rafales** : le journal est persisté et survit aux redémarrages, donc il reste
  gros ; chaque consultation de l'interface web replante la carte, en boucle, jusqu'à
  ce que le client cesse de rafraîchir ;
- **l'intermittence** : juste après une remise à zéro du journal la réponse est courte
  et passe sans encombre ; le défaut n'apparaît qu'au-delà de quelques dizaines
  d'entrées ;
- **le faux lien avec l'OTA** : le journal était consulté après chaque mise à jour,
  précisément pour y lire le `reset=`.

### Correctif à appliquer

1. **Pré-dimensionner le flux** : `beginResponseStream("application/json", taille)` au
   lieu de la taille par défaut — c'est le correctif de fond, il supprime toute
   réallocation.
2. **Borner la page** : plafonner `limit`, la mémoire interne ne permettant pas de
   sérialiser un journal entier d'un bloc.
3. **Écrire par blocs** dans `printJsonEscaped_` : accumuler les segments sans
   échappement et n'appeler `write(ptr, len)` qu'une fois par segment.

Les points 1 et 2 valent pour **toutes** les routes bâties sur `beginResponseStream`,
pas seulement le journal : la même construction s'y retrouve une dizaine de fois.

### Comment le vidage a été obtenu

`CoreDumpInfo` (`src/Core/CoreDumpInfo.*`) lit la partition et expose :

| Route | Rôle |
|---|---|
| `GET /api/system/coredump` | résumé JSON : tâche, PC, backtrace, `elf_sha256`, raison |
| `GET /api/system/coredump/raw` | image brute, pour `espcoredump.py` |
| `DELETE /api/system/coredump` | effacement explicite |

Le journal d'activité signale en outre « Vidage de crash disponible » au démarrage
suivant. Le décodage des adresses se fait au poste de travail :

```bash
xtensa-esp-elf-addr2line -pfiaC -e binary/debug/<firmware>.elf 0x... 0x...
```

**L'`elf_sha256` du vidage doit correspondre à l'ELF utilisé** : ici `53d1e8565…`
désignait `4.3.3+20260815.000211`, sauvegardé dans `binary/debug/` avant recompilation —
sans quoi le backtrace aurait été illisible. Sauvegarder l'ELF de chaque build flashé
sur une carte de test.

## Historique — le relevé du 2026-08-18 avant diagnostic

**Requalifié le 2026-08-18, et c'est plus grave que ce qu'on croyait.** Ce qui avait
été noté le 15/08 comme « une panique intermittente après un OTA » est en réalité un
défaut permanent, sans lien avec l'OTA : trois jours plus tard, sans qu'aucun OTA n'ait
eu lieu, le journal porte **28 `reset=task_wdt` et 3 `reset=panic`**, groupés en
rafales.

| Fenêtre | Redémarrages |
|---|---|
| 15/08 00:18 → 00:22 | 5 × `task_wdt` |
| 18/08 08:49 → 09:00 | 16 × `task_wdt` + 1 `panic` |
| 18/08 16:16 → 16:24 | 9 × `task_wdt` + 2 `panic` |

Le cycle est d'une régularité frappante, identique à chaque itération :

```
boot                       ts_ms ≈ 3 900
PoolLogic est prêt         ts_ms ≈ 11 000
Filtration OFF demandé     ts_ms ≈ 11 200
… crash                    ts_ms ≈ 35 000-40 000
```

Soit un redémarrage toutes les **46 à 48 secondes**. Puis la rafale cesse d'elle-même
et l'appareil tient plusieurs heures. Entre deux rafales, le fonctionnement est normal.

### Ce que la configuration nous dit déjà

```
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y   (CPU1 : non surveillé)
```

Le watchdog ne surveille que l'**idle task du cœur 0** : le déclenchement signifie
qu'une tâche du cœur 0 a monopolisé le CPU plus de 5 secondes sans rendre la main.
C'est le motif exact du plantage corrigé en juillet (boucle d'écriture sans yield), et
celui du débordement de pile d'août — mais cette fois hors de tout OTA.

Tâches épinglées sur le cœur 0, donc suspectes (le défaut de `Module::taskCore()` est
le cœur 1) :

| Module | Pile |
|---|---|
| `EthernetModule` | 6 144 |
| `FirmwareUpdateModule` | 6 144 |
| `HAModule` | **2 560** |
| `MQTTModule` | 5 120 |
| `WebInterfaceModule` | 4 096 |
| `WifiModule` | 4 096 |
| `WifiProvisioningModule` | 5 120 |
| `ConfigStoreModule` | 3 072 |
| `SystemMonitorModule` | 3 072 |

Ne pas s'arrêter à cette liste : **c'est une piste, pas un diagnostic.** La leçon du
chantier précédent est qu'un raisonnement plausible sur ce genre de tableau coûte cinq
correctifs inutiles, là où une mesure donne la réponse en un essai.

### La mesure existe déjà, il suffit d'aller la chercher

`CONFIG_ESP_TASK_WDT_PANIC=y` signifie que le watchdog passe par le gestionnaire de
panique : **chaque `task_wdt` a donc écrit un vidage complet**, au même titre que les
`panic`. Le vidage présent en flash est celui du **dernier crash, le `task_wdt` de
18/08 16:24:08** — le redémarrage de 16:33:14 étant un `software`, il ne l'a pas
écrasé. Il nomme la tâche qui tenait le cœur 0.

## Historique — ce qui avait été observé le 15/08 après les OTA

### Le relevé initial


**Non corrigé, et sans lien avec ce chantier.** Le premier démarrage qui suit un OTA
panique parfois, une vingtaine de secondes après le boot ; le démarrage suivant est
stable et rien n'est perdu.

| Boot post-OTA | Type d'OTA | Suite |
|---|---|---|
| 23:43:38 | firmware | stable |
| 23:45:03 | firmware | **panic à +21 s** |
| 00:06:32 | SPIFFS compressé | **panic à +27 s** |
| 00:09:02 | SPIFFS compressé | stable |

Deux fois sur quatre, sur les **deux** types d'OTA : ni le gzip ni l'écriture SPIFFS
n'en sont la cause. Le phénomène a été observé sous `4.3.3+20260814.234353` **et** sous
`4.3.3+20260815.000211`, le firmware actuellement en service : il peut donc se
reproduire au prochain OTA.

Aucune occurrence en fonctionnement courant, hors de la fenêtre qui suit un OTA.

### Le vidage du crash existe déjà — vérifié le 2026-08-18

Le sdkconfig du framework précompilé porte :

```
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y
CONFIG_ESP_COREDUMP_CHECK_BOOT=y
```

Autrement dit **chaque panique écrit un vidage complet dans la partition `coredump`**
(0xFF0000, 64 Ko) : tâche fautive, PC, backtrace, état des autres tâches. Ce qui manque
n'est pas la capture, c'est la **lecture** — aucun code du firmware ne touche cette
partition (`grep -ri "coredump" src/` ne renvoie rien).

`CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` n'étant pas posé, seule la **dernière**
panique est conservée : lire le vidage avant de provoquer d'autres OTA.

**L'ELF du firmware qui a paniqué a été sauvegardé** dans `binary/debug/` — sans lui,
les adresses du backtrace sont indécodables, et `binary/` est ignoré par git donc un
`pio run` l'aurait écrasé :

| Fichier | Rôle |
|---|---|
| `binary/debug/flowios3-4.3.3+20260815.000211.elf` | symboles du firmware en service lors du panic de 00:06:59 |
| `binary/debug/flowios3-4.3.3+20260815.000211.bin` | image correspondante, sha256 `d5f48f6f…` |

### Voie 1 — par USB, sans toucher au firmware

La plus rapide si la carte est accessible physiquement. **Depuis PowerShell natif, pas
depuis Bash/MSys** : `esptool` rejette l'environnement MSys.

```powershell
esptool --port COM? read-flash 0xFF0000 0x10000 coredump.bin
```

```powershell
python "$env:USERPROFILE/.platformio/packages/framework-espidf/components/espcoredump/espcoredump.py" info_corefile -t raw -c coredump.bin binary/debug/flowios3-4.3.3+20260815.000211.elf
```

Rend directement la tâche fautive, le `PC`, le backtrace symbolisé et l'état de toutes
les tâches. C'est le diagnostic complet, en une commande.

### Voie 2 — par HTTP, si la carte n'est pas accessible

Environ 80 lignes de firmware, utiles durablement :

1. au démarrage, `esp_core_dump_image_check()` puis `esp_core_dump_get_summary()` →
   inscrire tâche fautive, PC et backtrace dans le journal d'activité ;
2. `GET /api/system/coredump` : le résumé en JSON, et le vidage brut en base64 pour le
   décoder au poste de travail ;
3. `POST /api/system/coredump/erase` : effacement explicite, jamais automatique au boot.

**Piège** : le vidage appartient au firmware `4.3.3+20260815.000211`. Flasher le
firmware instrumenté ne l'efface pas, mais il faudra le décoder avec **l'ELF
sauvegardé**, pas avec le nouveau. `esp_core_dump_get_summary()` renvoie
`app_elf_sha256`, à comparer avec l'ELF choisi avant de croire au backtrace.

### Voie 3 — rendre le phénomène reproductible

2 paniques sur 4 boots post-OTA : sans reproductibilité, aucun correctif ne pourra être
validé. Enchaîner une dizaine d'OTA SPIFFS (≈1 min chacun) en relevant à chaque fois le
`reset=` du journal donne une base statistique — et, une fois la cause corrigée, la
preuve que le taux tombe à zéro.

## ~~Panne non résolue~~ — RÉSOLUE le 2026-08-20, section conservée pour l'historique

> **Élucidée** : débordements de pile, pas un défaut du chemin d'écriture.
> Voir [audit-paniques-flash-cache.md](audit-paniques-flash-cache.md). Tout ce qui suit
> décrit l'état des connaissances de la nuit du 18/08, y compris des conclusions depuis
> réfutées — notamment la piste `PC=0xFFFFFFFE` / cache flash, qui était fausse.

### Ce qui était écrit alors — nuit du 2026-08-18, à reprendre en priorité

**C'est la partie qui compte.** Tout ce qui précède dans ce document (gzip, dry run,
`tinfl`, `async_tcp`/journal d'activité, rafales `task_wdt`) est résolu et vérifié.
Ce qui suit ne l'est pas, et invalide la conclusion optimiste de la section « Étape 4 »
plus haut : l'OTA SPIFFS **n'est pas fiable en l'état**, et un simple OTA firmware peut
l'être tout autant.

### Chronologie de la soirée

| # | Opération | Résultat |
|---|---|---|
| 1 | OTA SPIFFS réel (correctif async_tcp tout juste flashé) | **corrompu** — `spiffs:false`, `app.js` 404 |
| 2 | OTA SPIFFS réel, avec `unmountSpiffsForWrite_()` (`SPIFFS.end()`) + `SpiffsAccessLock` | **corrompu**, `panic`, tâche accusée `EventBus`, PC `0xFFFFFFFE`, backtrace `corrupted:true` |
| 3 | OTA SPIFFS réel, verrou `SpiffsAccessLock` seul (sans `SPIFFS.end()`, retiré entre-temps) | **corrompu**, `panic`, tâche accusée `mqtt`, PC `0x403839EF` |
| 4 | **OTA firmware pur** (aucun octet SPIFFS touché) | **corrompu quand même** — `panic` pendant le redémarrage qui suit l'écriture, tâche accusée `EventBus`, PC `0xFFFFFFFE`, `elf_sha256` du vidage = celui du firmware qui vient d'être flashé |
| 5 | OTA SPIFFS réel, code revenu à l'état du 15/08 (aucun `SPIFFS.end()`, verrou présent mais inoffensif) | **corrompu** — progression jusqu'à 93 % cette fois, puis ~130 s d'indisponibilité (plusieurs cycles de redémarrage, pas un redémarrage propre), tâche accusée `EventBus`, PC `0x403839EF` |

Cinq écritures réelles tentées, cinq corruptions. Trois signatures de crash
différentes selon les essais (`EventBus`/`0xFFFFFFFE`, `mqtt`/`0x403839EF`,
`EventBus`/`0x403839EF`) — la tâche accusée et l'adresse varient d'un essai à l'autre,
ce qui est cohérent avec une corruption mémoire ou une course dont le point de rupture
dépend du timing exact, pas avec un bug déterministe sur une ligne précise.

### Trois hypothèses posées, trois réfutées par la mesure suivante

1. **Course entre l'écriture OTA et `ActivityLogModule`/le serveur de fichiers
   statiques**, qui accèdent tous deux à SPIFFS depuis leur propre tâche pendant que
   l'OTA réécrit la partition sous eux. Correctif : `Core/SpiffsAccessLock.h/.cpp`,
   mutex récursif FreeRTOS, pris exclusivement par l'OTA (`unmountSpiffsForWrite_`)
   et brièvement par les deux autres avant chaque accès. → **Essai n° 2 : même crash.**
2. **`SPIFFS.end()` lui-même**, ajouté la même nuit et absent du code qui avait
   réussi deux fois le 15/08 (corrélation 0/3 avec démontage, 2/2 sans, au moment où
   l'hypothèse a été posée). Retiré, verrou conservé. → **Essai n° 3 : même crash,
   alors que le verrou aurait dû empêcher toute collision avec le journal — ce qui
   réfute AUSSI l'hypothèse n° 1**, puisque la protection était active et n'a rien
   changé.
3. **Ce n'est pas spécifique à SPIFFS.** L'essai n° 4 (OTA firmware pur, aucun code
   SPIFFS impliqué, chemin `runWaveshareUpdate_` inchangé et fiable depuis le début du
   projet) a **planté pareil**. Ça élimine d'un coup tout ce qui a été modifié cette
   nuit sur le chemin d'écriture SPIFFS : ce n'était pas la cause.

### Piste actuelle, non confirmée

`PC = 0xFFFFFFFE` est la signature classique d'une exécution qui atterrit dans de la
flash **effacée** (motif `0xFF`) plutôt que dans du code valide. Sur ESP32, écrire en
flash (peu importe la partition — firmware ou SPIFFS, même mécanisme
`esp_partition_write`) désactive brièvement le cache flash sur les deux cœurs ; du code
qui s'exécute sur l'autre cœur à ce moment précis et qui n'est pas placé en IRAM peut
alors lire du vide. Le backtrace décodé de l'essai n° 3 tombe justement en plein dans
l'ordonnanceur FreeRTOS (`_frxt_dispatch → vTaskSwitchContext → vPortEnterCritical →
xPortEnterCriticalTimeout`), du code qui est normalement protégé (`IRAM_ATTR`) pour
survivre exactement à ce genre de fenêtre.

Ceci reste une piste, pas un diagnostic : elle explique le mécanisme plausible, pas
pourquoi il ne s'est jamais manifesté avant cette nuit (le même `Update.write()` avec
un tampon PSRAM tournait déjà le 15/08, avec succès). Une charge de fond plus élevée ce
soir (plus d'événements PoolLogic/MQTT/HA, donc plus d'interruptions candidates à
tomber dans la fenêtre dangereuse) est compatible avec les faits mais non vérifiée.

### Outillage ajouté cette nuit, à conserver

Indépendamment de la panne non résolue, ces changements sont acquis et utiles :

- **`Core/CoreDumpInfo.h/.cpp`** — lit la partition `coredump` (résumé JSON : tâche,
  PC, cause, adresse fautive, **registres a0-a15**, **EPCx**, backtrace, `elf_sha256`,
  raison). C'est cet outil qui a permis de décoder les cinq essais ci-dessus sans
  câble USB.
- **Trois routes** : `GET /api/system/coredump` (résumé), `GET
  /api/system/coredump/raw` (image brute — **bug connu, ne renvoie que les tout
  premiers octets au lieu de l'image complète, cause non trouvée, non bloquant tant
  que le résumé JSON suffit**), `DELETE /api/system/coredump`.
- **`Core/SpiffsAccessLock.h/.cpp`** — protège une course réelle et distincte (accès
  concurrent à SPIFFS entre le journal d'activité, le serveur de fichiers et un futur
  écrivain), même si elle n'est pas la cause des crashs ci-dessus. Laissé en place.
- Console de secours déjà existante, redécouverte cette nuit : **`/rescue`** (et
  `/webinterface/rescue`), page HTML embarquée **dans le firmware lui-même**
  (`kWebInterfaceFallbackPage`, `WebInterfaceServer.cpp`), donc disponible même quand
  SPIFFS est illisible. Permet de relancer un OTA firmware ou SPIFFS depuis un
  navigateur sans dépendre du système de fichiers ni d'un câble USB.

### État de l'appareil en fin de nuit

Firmware `4.3.5+20260818.201300` en service, **automatismes piscine opérationnels**
(PoolLogic démarre, filtration s'enclenche normalement). **SPIFFS non monté**,
interface web complète indisponible (`/webinterface/app.js` → 404), accessible
uniquement via `/rescue`. Aucune perte de données de régulation — uniquement le
contenu web.

### Ce qu'il ne faut PAS faire au prochain réveil de ce chantier

- **Ne pas retenter une écriture réelle (firmware ou SPIFFS) sans un plan de mesure.**
  Cinq essais cette nuit, cinq corruptions ; le seul gain net de chaque tentative
  a été un vidage de crash de plus, jamais un pas vers le correctif.
- **Ne pas répéter une hypothèse déjà réfutée** (course SPIFFS, `SPIFFS.end()`) : les
  deux ont été testées et écartées par la mesure, pas par le raisonnement.

### Reste à faire, dans l'ordre

1. **Corriger `GET /api/system/coredump/raw`** avant tout : avoir le vidage complet
   (registres de toutes les tâches, pas seulement celle qui a fauté) vaut mieux que
   deviner depuis le résumé.
2. **Tester la piste de charge de fond** : couper MQTT/HA (voire suspendre PoolLogic)
   avant un essai isolé, pour voir si ça change quelque chose au taux de crash — un
   test ciblé sur l'hypothèse IRAM/cache, pas un nouvel essai à l'aveugle.
3. Si la carte redevient accessible en USB sans contrainte : `espcoredump.py` en
   mode complet donne l'état de **toutes** les tâches au moment du crash, pas
   seulement celle accusée — susceptible de révéler directement la vraie coupable
   (ex. une pile visiblement débordée sur une tâche qui n'est pas celle nommée par le
   résumé).
4. Vérifier si ce firmware/toolchain (pioarduino, `platform-espressif32` 55.03.38)
   a un problème connu de placement IRAM des fonctions d'ordonnanceur FreeRTOS —
   recherche à faire hors du dépôt, pas sur l'appareil.

## Points ouverts suivants

- **Étape 3 — écrire moins.** Mesuré sur une image réelle : aucun bloc de 4 Ko n'est
  entièrement vierge (SPIFFS pose 2 octets d'en-tête par bloc), mais à la granularité
  **256 o il n'y a que 911 Ko à écrire, soit 11 % de l'image**. Effacer la partition
  puis n'écrire que les pages non vierges, via `esp_partition_*` au lieu de l'API
  `Update` qui n'autorise pas les trous, ramènerait les 42 s d'écriture à quelques
  secondes — et d'autant la fenêtre pendant laquelle une coupure secteur casse le
  système de fichiers.
- **Le paquet de fichiers `.pkg`** (`FLOW_OTA_SPIFFS_PKG`, toujours à 0) reste bloqué
  dans `webPkgExtract_`. Il n'utilise pas `tinfl` : c'est une autre cause, à chercher
  avec la même méthode — rendre observable avant de corriger.
- **Réduire la partition SPIFFS** reste écarté : l'OTA ne met pas à jour la table de
  partitions, donc chaque appareil en service exigerait un flash USB. À reconsidérer
  pour une série d'appareils neufs.

## Comment retester

Essai à blanc — ne peut rien casser, à privilégier après toute modification du chemin :

```bash
curl.exe -X POST "http://10.10.50.90/fwupdate/spiffs?dry=1&url=http://10.10.10.38:8000/flowios3-spiffs-4.3.2.bin"
```

```bash
curl.exe -s "http://10.10.50.90/api/fwupdate/status"
```

Attendu : `done` en moins de 2 s, avec `dry run ok: 8257536 o produits, crc conforme`.
En cas d'arrêt, le message d'erreur porte le nombre d'octets produits, c'est-à-dire
l'offset exact du blocage.

OTA réel : la même commande sans `dry=1`. Versions installées, avant et après :

```bash
curl.exe -s "http://10.10.50.90/api/web/meta"
```

Raison du dernier démarrage (`reset=software` attendu, `reset=panic` à signaler) :

```bash
curl.exe -s "http://10.10.50.90/api/activity/logs?offset=0&limit=1"
```

Sous PowerShell, écrire `curl.exe` : `curl` y est un alias d'`Invoke-WebRequest`, qui
ne comprend pas `-X`. Et `url=` est obligatoire — `resolveUrl_` n'a jamais construit
l'URL lui-même, c'est l'interface web qui la fournit.

## Fichiers touchés

| Fichier | Nature |
|---|---|
| [FirmwareUpdateModule.cpp](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.cpp) | mise en tampon, vérification, décompression mémoire, essai à blanc, `FLOW_OTA_SPIFFS_GZIP = 1` |
| [FirmwareUpdateModule.h](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.h) | `UpdateJob::dryRun`, nouvelles méthodes, `GzStage` |
| [IFirmwareUpdate.h](../../src/Core/Services/IFirmwareUpdate.h) | `startSpiffsDryRun` |
| [WebInterfaceServer.cpp](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp) | paramètre `dry` sur `/fwupdate/spiffs`, `url` accepté en query |
| [SystemLimits.h](../../include/Core/SystemLimits.h) | `Spiffs::GzStageMaxBytes`, `GzDownloadRetries`, `GzRetryDelayMs` |
| [CoreDumpInfo.h](../../src/Core/CoreDumpInfo.h) / [.cpp](../../src/Core/CoreDumpInfo.cpp) | lecture de la partition `coredump`, résumé + registres a0-a15/EPCx |
| [SpiffsAccessLock.h](../../src/Core/SpiffsAccessLock.h) / [.cpp](../../src/Core/SpiffsAccessLock.cpp) | mutex récursif partagé autour des accès SPIFFS |
| [IActivityLog.h](../../src/Core/Services/IActivityLog.h) | `ActivityCode::SystemCoreDump` |
| [ActivityLogModule.cpp](../../src/Modules/Logs/ActivityLogModule/ActivityLogModule.cpp) / [.h](../../src/Modules/Logs/ActivityLogModule/ActivityLogModule.h) | événement « Vidage de crash disponible » au boot, verrou autour de `persist_`/`clear_`, pagination et pré-dimensionnement de `/api/activity/logs` |

Vérifié : `pio run -e Waveshare-ESP32-S3` (flash à 47,9 %), y compris avec les deux
variantes activées, et cppcheck aux options exactes de la CI — zéro alerte, à chaque
étape de la nuit. La compilation n'a jamais été en cause : c'est un défaut à
l'exécution, pas de build.

## Leçon de méthode, confirmée

La note précédente concluait qu'il aurait fallu s'arrêter au deuxième échec et exiger
des mesures avant de toucher au code. Le fait est là : **cinq correctifs raisonnés
n'avaient rien donné, un essai à blanc a donné la cause au premier lancement.** Rendre
l'opération observable coûte moins cher que la corriger à l'aveugle, et un mode qui
n'écrit rien se relance autant de fois qu'on veut.
