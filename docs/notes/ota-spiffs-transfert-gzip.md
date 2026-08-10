# Note de travail — OTA SPIFFS compressé : implémenté, bloqué sur cible

> Statut : **code en place, désactivé par défaut** (`FLOW_OTA_SPIFFS_GZIP = 0`).
> Trois tentatives sur matériel, trois blocages. Cause non identifiée.
> Date : 2026-08-10
> Suite de la piste n° 3 de [ota-spiffs-lenteur-plantage.md](ota-spiffs-lenteur-plantage.md).

## Objectif et gain mesuré

L'image SPIFFS est padée par `mkspiffs` jusqu'à la taille de la partition. Le
remplissage `0xFF` se compresse à presque rien :

| | |
|---|---|
| `flowios3-spiffs-4.1.5.bin` | 8 257 536 o |
| `flowios3-spiffs-4.1.5.bin.gz` | **329 253 o — 4,0 %, soit 25,1× moins** |

Le gain porte sur le **transfert réseau seulement** : l'écriture flash reste de
7,9 Mo, puisque l'image couvre toute la partition. C'était le compromis assumé face à
l'option « image creuse », qui aurait aussi réduit l'écriture mais demandait un format
de conteneur maison.

## Ce qui est en place

- **Génération** : `export_binaries.py` produit le `.gz` à côté du `.bin` après chaque
  `buildfs`. `mtime=0` et nom de fichier vide → en-tête gzip de **10 octets avec
  FLG=0**, taille fixe dont dépend le décodeur embarqué.
- **Négociation** : `runSpiffsUpdate_` demande `<url>.bin.gz` d'abord et retombe sur
  le `.bin` en cas d'échec. Rien à déclarer dans le manifeste, un serveur de mise à
  jour existant reste utilisable tel quel.
- **Décodage** : `runSpiffsInflate_` décompresse en flux avec `tinfl` (miniz, en ROM
  sur ESP32-S3 : aucun coût flash pour la bibliothèque). Fenêtre de 32 Ko allouée en
  PSRAM, servant aussi de tampon de sortie circulaire.
- **Contrôles** : magie gzip vérifiée, total décompressé comparé à la taille de la
  partition en fin de flux, flux tronqué détecté.

Tout cela est **compilé et conservé**, derrière `FLOW_OTA_SPIFFS_GZIP` (0 par défaut,
défini dans `FirmwareUpdateModule.cpp`).

## Le symptôme

L'OTA SPIFFS ne se termine jamais. L'interface reste figée sur l'étape « Connexion »,
progression 0 %. Le serveur HTTP voit bien passer un `GET …bin.gz` répondu **200**,
donc la négociation fonctionne et le job démarre.

## Hypothèses testées

**1. `tinfl` n'est jamais rappelé après épuisement de l'entrée — INFIRMÉE.**
L'idée : la boucle n'appelait `tinfl_decompress` que tant qu'il restait des octets à
consommer, donc le dernier appel — celui qui vide la fenêtre et rend `DONE` — n'avait
jamais lieu. La machine à états a été portée en Python et rejouée sur le vrai `.gz` :
l'ancienne structure **atteint bien `DONE`** et produit les 8 257 536 octets attendus.
L'hypothèse est fausse.

La boucle a malgré tout été réécrite, parce que la nouvelle version est plus robuste :
`tinfl` est appelé même à entrée vide, et un flux tronqué est détecté au lieu de faire
tourner la boucle à vide. Validé par le même portage Python : sortie identique octet
pour octet au `.bin`, et arrêt propre sur flux coupé.

**2. Task Watchdog — corrigée, sans effet observé.**
Le yield était indexé sur les *lectures réseau* (une fois tous les 16 chunks), or
329 Ko d'entrée produisent 7,9 Mo : la décompression écrit des mégaoctets entre deux
lectures sans rendre la main. C'est la forme exacte du plantage corrigé en juillet,
réintroduite par une autre porte. Le yield suit maintenant le travail produit (une
fois toutes les ~256 Ko). **Le blocage persiste après ce correctif.**

## L'observation qui oriente la suite

Pendant le « blocage », l'interface affiche toujours **« Appareil joignable » et une
heure RTC qui avance**. Or `runSpiffsUpdate_` met le serveur web en pause pendant tout
le flash : s'il répond, c'est que **le firmware n'est pas en train de flasher**.

Le job n'est donc pas en cours au moment observé. Deux lectures possibles, non
départagées :

- il a démarré puis s'est arrêté (erreur, ou reset suivi d'un redémarrage), et
  l'interface affiche une **session d'upgrade résiduelle** — elle est persistée côté
  navigateur, ce qui peut aussi empêcher d'en relancer une ;
- il n'a jamais démarré lors de la dernière tentative, et le `GET` observé venait
  d'un essai antérieur.

## À mesurer pour reprendre

Rien ne sert de continuer à corriger sans ces trois éléments :

1. **Logs firmware** pendant la tentative : on doit y lire
   `SPIFFS update: variante compressee retenue`, ou l'erreur qui suit.
2. **`curl -s http://<ip>/fwupdate/status`** pendant que l'interface est figée : dit
   si l'état réel est `idle`, `downloading`, `flashing` ou `error`.
3. **Raison du dernier redémarrage** dans le journal d'activité (l'événement de
   démarrage porte `reset=…`) : confirme ou écarte définitivement le watchdog.

Si la piste watchdog se confirme malgré le correctif, la suivante est la tâche OTA
épinglée sur le cœur 0 avec WiFi et AsyncTCP — piste n° 2 de la note d'origine, jamais
traitée, et qui devient d'autant plus plausible que la décompression ajoute de la
charge CPU sur ce même cœur.

## Reproduire la validation hors cible

Le portage Python de la machine à états (simulateur `tinfl` basé sur `zlib`, avec les
mêmes contraintes d'entrée consommée, de sortie plafonnée et les mêmes codes de
statut) est ce qui a permis d'invalider l'hypothèse n° 1. Il n'est pas versionné, mais
son principe est simple à refaire et vaut mieux qu'un raisonnement : alimenter la
boucle par chunks de 1024 octets — `Limits::FirmwareUpdate::Http::StreamChunkBytes` —
et vérifier que la sortie est identique au `.bin`.
