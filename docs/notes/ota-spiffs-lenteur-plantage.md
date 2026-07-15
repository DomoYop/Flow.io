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

### 3. Image SPIFFS transférée à taille pleine de partition, pas à taille réelle (non implémenté)

`board_build.filesystem = spiffs` + `partitions_flowios3_ota_16mb.csv`
([platformio.ini:212-213](../../platformio.ini#L212)) : la partition `spiffs` fait
`0x7E0000` (~7,87 Mo). L'outil `mkspiffs` de PlatformIO génère systématiquement un
`.bin` à la taille de la partition (padding `0xFF`), même si le contenu réel de
`data/` avoisine 1,4 Mo. Chaque mise à jour SPIFFS transfère et flashe donc
4 à 6× plus de données que nécessaire, ce qui amplifie d'autant l'exposition aux
points 1 et 2.

Piste : générer/transférer une image de taille réelle (pas paddée à la partition
entière) — nécessiterait d'adapter `scripts/prepare_spiffs_data.py` ou la commande
de build de l'image SPIFFS.

## Reste à faire

- [x] Yield périodique dans `runSpiffsUpdate_()`.
- [ ] Étudier le déplacement de la tâche `FirmwareUpdateModule` hors du cœur 0.
- [ ] Étudier la génération d'une image SPIFFS de taille réelle (hors padding).
- [ ] Test terrain après le correctif n°1 : vérifier qu'un flash SPIFFS complet
      passe désormais sans reset, y compris sur une liaison WiFi rapide.
