# Note de travail — réduire le volume de l'OTA SPIFFS : deux tentatives, aucune aboutie

> Statut : **les deux variantes sont désactivées**. L'OTA SPIFFS utilise l'image
> complète, qui fonctionne. Le code des variantes est conservé, compilé, inactif.
> Date : 2026-08-11
> Suite de la piste n° 3 de [ota-spiffs-lenteur-plantage.md](ota-spiffs-lenteur-plantage.md).

## Pourquoi on cherchait à réduire

`mkspiffs` pade l'image jusqu'à la taille de la partition. On transfère et on écrit
donc 25 fois ce qui est utile, à chaque mise à jour du contenu web :

| | |
|---|---|
| Partition `spiffs` | 8 257 536 o (`0x7E0000`) |
| Contenu réel de `data/` | **324 496 o** (172 fichiers) |
| Occupation | 3,9 % |

Sur une liaison WiFi ordinaire, l'opération dure plusieurs minutes — et elle est
**destructive** : elle réécrit la partition, donc une coupure ou un reset en cours
d'écriture laisse un système de fichiers illisible, c'est-à-dire un appareil sans
interface web, récupérable seulement par USB.

## Ce qui fonctionne aujourd'hui

Le chemin d'origine : téléchargement de `flowios3-spiffs-<version>.bin` (8,3 Mo) et
écriture via `Update.write(U_SPIFFS)`. Dernier flash abouti confirmé : 2026-08-10 à
15:08. **C'est le chemin actif**, et les deux variantes ci-dessous sont désactivées
par défaut.

## Tentative A — transférer l'image compressée

`FLOW_OTA_SPIFFS_GZIP`, défaut **0**.

Le padding `0xFF` se compresse à presque rien : `329 253 o`, soit **4 % du `.bin`**.
`export_binaries.py` produit le `.gz` après chaque `buildfs` ; `runSpiffsUpdate_`
demande `<url>.bin.gz` d'abord et retombe sur le `.bin` ; `runSpiffsInflate_`
décompresse en flux avec `tinfl` (miniz est en ROM sur ESP32-S3, donc sans coût
flash), fenêtre de 32 Ko en PSRAM.

Le gain ne porte que sur le **transfert** : on écrit toujours 8,3 Mo.

**Résultat sur cible : l'OTA ne se termine jamais.** L'interface reste figée, le
serveur voit pourtant le `GET …bin.gz` répondu 200.

## Tentative B — envoyer un paquet de fichiers

`FLOW_OTA_SPIFFS_PKG`, défaut **0**.

Format dans [build_web_package.py](../../scripts/build_web_package.py) : en-tête de
24 octets, index (chemin, taille, CRC-32 par fichier), puis les fichiers concaténés.
Non compressé, car 169 des 172 fichiers sont **déjà gzippés individuellement** par
`prepare_spiffs_data.py` — 91 % des octets. Mesures :

| | |
|---|---|
| Paquet non compressé | 329 433 o |
| Paquet compressé | 307 682 o |
| Image `.bin.gz` (tentative A) | 329 253 o |

Les trois se tiennent en 7 % : **le transfert n'avait plus rien à gagner**. L'intérêt
du paquet était ailleurs — écrire ~660 Ko au lieu de 8,3 Mo, et surtout ne jamais
toucher au contenu en place tant que le paquet complet n'est pas vérifié.

Installation en trois temps : dépôt dans `/ota/pkg.tmp`, vérification de l'en-tête,
des tailles et du CRC global, puis remplacement fichier par fichier (temporaire court
`/ota/t` — SPIFFS plafonne les noms à 32 caractères et le plus long chemin en fait
29 — vérification du CRC, puis bascule). Un fichier déjà identique n'est pas réécrit.

**Résultat sur cible : l'installation ne se termine pas non plus.** Le téléchargement
et la vérification passent (le serveur voit le `GET …pkg` en 200, et la progression
atteint 100 %) ; c'est l'extraction qui n'aboutit pas.

## Ce qui est établi

- La négociation fonctionne : le serveur reçoit bien la requête `.gz` ou `.pkg`.
- Pour le paquet, le **téléchargement et la vérification réussissent** : le blocage se
  produit après, dans `webPkgExtract_`.
- Les formats sont corrects, vérifiés hors cible : le `.gz` décompressé rend un flux
  identique octet pour octet au `.bin` ; le `.pkg` relu avec le parcours exact du
  firmware rend les 172 fichiers identiques au staging, index et payload entièrement
  consommés. L'implémentation CRC-32 par quartets du firmware a été comparée à zlib.
- Le chemin par image, lui, fonctionne.

## Hypothèses testées

**`tinfl` jamais rappelé après épuisement de l'entrée — INFIRMÉE.** La machine à
états a été portée en Python et rejouée sur le vrai `.gz` : l'ancienne structure
atteint `DONE` et produit les 8 257 536 octets attendus. La boucle a tout de même été
réécrite (appel à entrée vide, détection de flux tronqué), sans effet sur le blocage.

**Task Watchdog — plausible, corrigée deux fois, sans effet observé.** Le yield était
indexé sur les lectures réseau alors que la décompression écrit des mégaoctets entre
deux lectures ; puis, dans le paquet, sur les fichiers alors qu'un seul asset de
100 Ko représente des dizaines d'écritures SPIFFS d'affilée. Les deux ont été
corrigés — c'est le même motif que le plantage corrigé en juillet — **et le blocage
persiste**.

**Rollback OTA non validé — ÉCARTÉE.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` et
`CONFIG_APP_ROLLBACK_ENABLE` sont tous deux actifs, et le core Arduino valide l'image
au premier démarrage via `verifyOta()`. Une image restée en `PENDING_VERIFY` aurait pu
expliquer des redémarrages surprises ; ce n'est pas le cas.

## Ce qu'il faut mesurer avant de reprendre

Aucune de ces trois mesures n'a pu être obtenue, et c'est ce qui a fait tourner le
diagnostic en rond :

1. **Logs firmware** pendant la tentative. Le paquet trace son avancement tous les
   16 fichiers (`Paquet web : N/172 fichiers…`) : le dernier message dit si
   l'extraction démarre, où elle s'arrête, et sur quel fichier.
2. **`curl -s http://<ip>/fwupdate/status`** pendant que l'interface est figée : dit
   si l'état réel est `idle`, `downloading`, `flashing` ou `error`. Une interface
   figée sur un état ancien n'est pas la preuve que le firmware travaille encore.
3. **Raison du dernier redémarrage**, dans le journal d'activité (l'événement de
   démarrage porte `reset=…`) : confirme ou écarte définitivement le watchdog.

Une piste reste ouverte, jamais traitée : la tâche `FirmwareUpdateModule` est épinglée
sur le **cœur 0**, celui du driver WiFi et d'AsyncTCP — piste n° 2 de la note de
juillet. Elle devient d'autant plus plausible que les deux variantes ajoutent du
travail CPU sur ce même cœur.

## Leçon de méthode

Cinq correctifs ont été appliqués et flashés sans qu'aucun élément d'observation ne
soit disponible, sur la seule foi d'un raisonnement. Le premier diagnostic était faux
— un test l'a montré après coup. Les suivants portaient sur des causes plausibles mais
non vérifiées, et aucun n'a rien changé.

La bonne conduite aurait été de s'arrêter au deuxième échec et d'exiger les mesures
ci-dessus avant de toucher au code. La progression publiée par le paquet et la levée
de la pause du serveur web pendant l'extraction — les deux derniers changements — vont
dans ce sens : rendre l'opération observable plutôt qu'ajouter un correctif de plus.

## Option écartée : réduire la partition

Ramener `spiffs` à `0x100000` (1 Mo, 31 % d'occupation) diviserait le transfert **et**
l'écriture par 7,9, et `spiffs` étant l'avant-dernière partition, cela ne déplacerait
ni `app0` ni `app1`. Mais l'OTA ne met pas à jour la table de partitions : chaque
appareil en service exigerait un flash USB complet, impossible dans le contexte
d'exploitation actuel. À reconsidérer pour une série d'appareils neufs.

Un garde-fou issu de cette étude est conservé, utile en soi : `runSpiffsUpdate_`
refuse une image dont la taille diffère de celle de sa partition, avant toute
écriture.

## Origine de la proposition B

[chatgpt_proposition_ota_amelioré.md](chatgpt_proposition_ota_amelioré.md) — l'analyse
qui a orienté la tentative B. Ses idées de robustesse (dépôt puis vérification puis
extraction, empreinte, fichiers temporaires) ont été retenues. Ses chiffres, non : elle
annonce `tar.gz 92 ko` en supposant un contenu non compressé, alors que le nôtre l'est
déjà à 91 %.
