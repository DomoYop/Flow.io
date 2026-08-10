# Note de travail — OTA SPIFFS : lenteur et plantage en cours de transfert

> Statut : **analyse + un correctif appliqué, deux pistes non implémentées**
> Date : 2026-07-07
> Contexte : l'OTA firmware (pull HTTP depuis un serveur local) passe sans souci,
> l'OTA SPIFFS est très lent et l'ESP32 redémarre systématiquement avant la fin du
> transfert — y compris sur une liaison WiFi correcte.

## Diagnostic

Logs serveur (`python -m http.server`) : le firmware (`flowios3-2.0.4.bin`) est
téléchargé une seule fois, proprement. Le SPIFFS (`flowios3-spiffs-2.0.4.bin`) est
redemandé en boucle avec des `ConnectionResetError` côté serveur — c'est-à-dire que
c'est l'ESP32 qui coupe la connexion TCP en plein transfert (reboot), pas une simple
perte radio.

Trois causes cumulatives, indépendantes de la qualité WiFi :

### 1. Boucle d'écriture sans yield périodique (corrigé)

Dans `runSpiffsUpdate_()`
([FirmwareUpdateModule.cpp:904-944](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.cpp#L904)),
la boucle de lecture/écriture ne cède la main (`delay(1)`) que lorsque le flux HTTP
n'a plus de données disponibles. Si le réseau alimente en continu (cas d'une
liaison correcte, justement), la boucle tourne sans jamais yield — la tâche affame
l'idle task de son cœur, le Task Watchdog finit par déclencher un reset. Aucun
`esp_task_wdt_reset()` n'existe nulle part dans le module.

**Correctif appliqué** : yield (`vTaskDelay(1)`) tous les 16 chunks, indépendamment
de `avail`, dans `runSpiffsUpdate_()`. Le même pattern existe dans
`runWaveshareUpdate_()` ([lignes 692-731](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.cpp#L692))
mais n'a pas été touché (partition deux fois plus petite, jamais vu planter en
pratique) — à surveiller si un firmware plus gros venait à poser le même problème.

### 2. Tâche OTA épinglée sur le même cœur que WiFi/AsyncTCP (non implémenté)

`FirmwareUpdateModule::taskCore()` retourne `0`
([FirmwareUpdateModule.h:21](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.h#L21)),
qui est aussi le cœur du driver WiFi et d'AsyncTCP (serveur web). Pendant l'écriture
flash, la tâche OTA est donc sur le même cœur que la pile réseau dont elle a besoin
pour continuer à recevoir les données — contention CPU auto-infligée, absente lors
d'un OTA firmware (aucune lecture concurrente de la partition app0/app1).

Piste : vérifier si l'épinglage cœur 0 est réellement requis (UART Nextion ?) ou si
la tâche peut tourner sur le cœur 1 comme les autres modules.

### 3. Image SPIFFS transférée à taille pleine de partition (toujours ouvert)

`board_build.filesystem = spiffs` + `partitions_flowios3_ota_16mb.csv` : la partition
`spiffs` fait `0x7E0000` (7,875 Mo). `mkspiffs` génère systématiquement un `.bin` à la
taille de la partition (padding `0xFF`), alors que `data/` ne pèse que **324 338 o**
(317 Ko, 171 fichiers, relevé du 2026-08-10 via `fsver.j`). Chaque mise à jour
transfère et écrit donc **25×** ce qui est utile.

Deux voies ont été explorées le 2026-08-10, aucune n'est active à ce jour.

**Réduire la partition — chiffré, puis écarté.** Ramener `spiffs` à `0x100000` (1 Mo,
31 % d'occupation) fait tomber l'image à 1 048 576 o, soit 7,9× moins à transférer
*et* à écrire. `spiffs` étant l'avant-dernière partition, la rétrécir ne déplace ni
`app0` ni `app1` : l'OTA firmware resterait compatible. Mais **l'OTA ne met pas à jour
la table de partitions** : chaque appareil en service exigerait un flash USB complet,
impossible dans le contexte d'exploitation actuel. Le CSV a donc été remis à
`0x7E0000`. À reconsidérer pour une future série d'appareils neufs.

Un garde-fou issu de cette étude est conservé, et il est utile en soi :
`runSpiffsUpdate_` refuse une image dont la taille diffère de celle de sa partition,
**avant toute écriture**. Sans lui, une image bâtie pour une autre table donnerait un
système de fichiers illisible, donc une interface web perdue jusqu'au prochain flash
USB. Le cas inverse était déjà rejeté par `Update.begin`.

**Transférer l'image compressée — implémenté, non fonctionnel sur cible.** Le padding
se compresse à presque rien : le `.gz` pèse 4 % du `.bin`. Le code est en place mais
désactivé par défaut, faute d'avoir abouti. Détail, hypothèses testées et ce qu'il
reste à mesurer : [ota-spiffs-transfert-gzip.md](ota-spiffs-transfert-gzip.md).

## Reste à faire

- [x] Yield périodique dans `runSpiffsUpdate_()`.
- [ ] Étudier le déplacement de la tâche `FirmwareUpdateModule` hors du cœur 0.
      Redevenu prioritaire : c'est l'hypothèse encore ouverte du blocage gzip.
- [ ] Réduire le volume transféré : partition écartée (flash USB), gzip non abouti.
- [ ] Test terrain après le correctif n°1 : vérifier qu'un flash SPIFFS complet
      passe désormais sans reset, y compris sur une liaison WiFi rapide.
