# Note de travail — réduire le volume de l'OTA SPIFFS : deux tentatives, aucune aboutie

> Statut : **chantier repris le 2026-08-14**, voir « Reprise » en fin de note. Ce qui
> précède décrit l'état au 2026-08-11 : les deux variantes désactivées, l'OTA SPIFFS
> utilisant l'image complète, qui fonctionne.
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

---

# Reprise du 2026-08-14

Motif : les plantages en cours d'OTA SPIFFS sur liaison WiFi médiocre restent le
problème d'exploitation n° 1.

## Le constat qui change la cible

**Le gzip tel qu'il était écrit ne réduisait pas l'exposition au réseau.** La tentative A
décompressait *en streaming* : la connexion TCP devait rester ouverte pendant toute
l'écriture des 8,26 Mo, soit plusieurs dizaines de secondes. On divisait le volume par
25, pas la durée pendant laquelle le WiFi doit tenir. Sur une liaison qui coupe, la
vulnérabilité était donc inchangée — et toujours destructive, la partition étant à
moitié écrasée au moment de la coupure.

C'est aussi l'explication la plus crédible du blocage : pendant que la tâche OTA
(cœur 0) écrit des mégaoctets d'affilée, elle ne lit plus le socket ; le serveur bloque
en écriture et TCP attend sur le cœur du driver WiFi. Hypothèse **non retenue comme
correctif** — conformément à la leçon de méthode ci-dessus, on ne corrige plus une cause
supposée : on supprime la situation (plus de réseau ouvert pendant l'écriture) et on
rend l'opération observable.

## Mesures faites sur `binary/flowios3-spiffs-4.3.2.bin`

| | |
|---|---|
| Image | 8 257 536 o — `.gz` : 332 891 o (**4,03 %**) |
| Trailer gzip | `ISIZE = 8 257 536`, `CRC32 = 0cd7e507` |
| Blocs de 4 Ko entièrement vierges (0xFF) | **0 sur 2016** |
| Pages de 256 o non vierges | 3 559 → **911 Ko utiles, 11 % de l'image** |

Deux enseignements neufs. Le trailer gzip donne taille et empreinte de l'image
décompressée **avant d'écrire le moindre octet** : jusqu'ici l'incohérence de taille
n'était détectée qu'en fin de flux, partition déjà détruite. Et sauter les blocs vierges
ne marcherait pas — SPIFFS pose 2 octets d'en-tête dans chaque bloc de 4 Ko — mais à la
granularité 256 o il n'y a que 11 % de l'image à écrire.

## Plan en quatre étapes

**Étape 1 — rendre l'opération observable, sans aucun risque (`dry run`).** Télécharger
le `.gz` en entier en PSRAM, fermer HTTP, décompresser vers un compteur et un CRC-32,
sans un seul `Update.write` ni redémarrage. Verdict attendu : si le compteur tombe sur
8 257 536 et que le CRC concorde, `tinfl` et le transfert sont hors de cause et le
blocage est dans l'écriture flash ; sinon on a l'offset exact d'arrêt. C'était la mesure
manquante qui a fait tourner le diagnostic en rond.

**Étape 2 — dissocier transfert et écriture.** Même mise en tampon, puis `Update.begin`
et décompression *depuis la mémoire, connexion fermée*. La fenêtre pendant laquelle le
WiFi doit tenir passe de plusieurs minutes à quelques secondes ; le téléchargement est
réessayé sans que rien n'ait été touché ; l'`ISIZE` est comparé à la taille de partition
avant `Update.begin`. La piste « tâche épinglée cœur 0 » devient sans objet : il n'y a
plus de trafic réseau pendant l'écriture, donc pas de raison de déplacer la tâche.

**Étape 3 — diviser l'écriture par 9** (non commencée). Effacer la partition puis
n'écrire que les pages de 256 o non vierges, via `esp_partition_*` au lieu de l'API
`Update` qui n'autorise pas les trous. 911 Ko au lieu de 8,26 Mo, vérification par
relecture. La fenêtre pendant laquelle une coupure secteur casse le système de fichiers
tombe de ~1 min à quelques secondes.

**Étape 4 — activation** (non commencée) : `FLOW_OTA_SPIFFS_GZIP = 1` par défaut, repli
sur le `.bin` conservé.

Le `.pkg` reste en réserve : il écrit encore moins, mais son blocage est dans
`webPkgExtract_`, un tout autre code. À rouvrir seulement si l'étape 1 innocente la
flash.

## Étapes 1 et 2 — implémenté le 2026-08-14

Un seul chemin, `runSpiffsUpdate_` :

1. `spiffsStageGz_` télécharge `<url>.bin.gz` en PSRAM (borné à 1 Mo), 3 tentatives
   espacées de 1,5 s, puis **ferme la connexion**. Un 404 signifie « pas de variante
   compressée » et retombe sur l'image complète ; un échec de téléchargement d'un `.gz`
   *présent* est une erreur franche — retomber sur 8,26 Mo quand 333 Ko ne passent pas
   n'aurait aucun sens.
2. En-tête et trailer vérifiés avant toute écriture : magie gzip, `FLG = 0`, et surtout
   `ISIZE == spiffsPart->size`.
3. **Passe de vérification** : `spiffsInflateMem_(..., writeToFlash = false)` décompresse
   l'image entière et compare le CRC-32 produit au trailer. Aucune écriture.
4. **Passe d'écriture**, seulement si la précédente a réussi : `Update.begin` puis la
   même fonction avec `writeToFlash = true`.

Le mode `dry run` s'arrête après l'étape 3 et ne redémarre pas. Il exécute donc
exactement le code de production, pas une doublure.

La machine à états qui entrelaçait lecture réseau et `tinfl` est supprimée
(`runSpiffsInflate_`) : l'entrée étant complète en mémoire, `TINFL_FLAG_HAS_MORE_INPUT`
n'est jamais positionné et toute la classe de bugs « appel final manquant » disparaît —
c'était la première hypothèse, infirmée, de la tentative A.

Coût : une décompression de plus (quelques secondes de CPU, sans I/O). Bénéfice : on ne
commence à écrire que sur une image dont l'intégrité est prouvée de bout en bout.

### Comment lancer l'essai

```bash
curl -X POST "http://<ip>/fwupdate/spiffs?dry=1"
curl -s "http://<ip>/api/fwupdate/status"
```

`dry=1` est refusé sur les autres cibles. Sans le paramètre, la route se comporte comme
avant. Le statut affiche successivement `downloading` (téléchargement du `.gz`),
`flashing` avec le message `verification` puis `ecriture`, et se termine en `done` sur
un message du type `dry run ok: 8257536 o, crc ok`. La progression compte la passe de
vérification pour la première moitié et l'écriture pour la seconde.

Les traces `LOGI` donnent le détail : taille du `.gz`, tentative retenue, octets
produits, CRC attendu et obtenu. En cas d'arrêt en cours de décompression, le message
d'erreur porte le nombre d'octets produits — c'est-à-dire l'offset exact du blocage.

## Verdict du premier essai à blanc — la cause, enfin

Premier `dry run` sur cible, 2026-08-14 23:38 : **l'appareil redémarre**, sans qu'un
seul octet ait été écrit en flash. Le journal d'activité donne la raison :

> `flow.io a démarré — Firmware 4.3.3+20260814.232950, reset=panic`

**Une panique, pas un Task Watchdog.** Croisé avec la taille de la structure de
travail de `tinfl` :

| | |
|---|---|
| `tinfl_huff_table` | 288 + 1024×2 + 576×2 = 3 488 o |
| `tinfl_decompressor` (3 tables + état) | **≈ 10,7 Ko** |
| Pile de la tâche `fwupdate` sur Waveshare | **6 144 o** |

`tinfl_decompressor decomp;` était déclaré **en variable locale**. Débordement de pile
dès l'entrée dans la fonction, avant même la première itération. Le défaut existait
à l'identique dans l'ancien `runSpiffsInflate_` : c'est la cause des trois blocages du
2026-08-11.

Tout s'explique d'un coup : le chemin non compressé fonctionne parce qu'il n'appelle
jamais `tinfl` ; les cinq correctifs sur le yield et la machine à états ne pouvaient
rien changer, la fonction ne survivait pas à son prologue ; le portage Python de la
machine à états atteignait bien `DONE`, puisque la logique était juste.

**Corollaires.** L'hypothèse « tâche épinglée sur le cœur 0 » et celle du Task Watchdog
sont écartées : le crash survient sans réseau ouvert et sans écriture flash. Et la
lenteur du diagnostic tient à une seule chose — trois essais destructifs là où un essai
à blanc désignait la cause en une fois.

**Correctif** : le décompresseur est alloué sur le tas (DRAM interne, PSRAM en repli),
avec un commentaire qui interdit explicitement de le remettre sur la pile. La fenêtre
de 32 Ko y était déjà.

## Après correctif : le `dry run` passe

Firmware `4.3.3+20260814.234353`, image `flowios3-spiffs-4.3.2.bin.gz` (332 891 o).

```
{"state":"done","progress":100,"msg":"dry run ok: 8257536 o produits, crc conforme"}
```

**Trois essais consécutifs réussis, aucun redémarrage**, uptime continu. Et surtout :
l'`ts_ms` du statut passe de 129 767 à 132 054 ms entre deux fins d'essai, cycle `curl`
compris — le téléchargement des 333 Ko *plus* la décompression des 8,26 Mo *plus* le
CRC-32 tiennent en **moins de 2 secondes**. La vérification préalable, qu'on croyait
coûteuse, est donc quasi gratuite au regard d'un OTA.

### Un point non élucidé, mais non reproductible

Le journal porte une panique à 23:45:24 **sous le firmware déjà corrigé**, juste avant
le premier essai réussi. Cinq essais n'ont pas réussi à la reproduire :

| Essai | Uptime au lancement | Résultat |
|---|---|---|
| 1 à 3 | > 100 s | `done` |
| 4 | ~9 s, juste après redémarrage | `done` à 11 249 ms |
| 5 | ~19 s, à cheval sur le démarrage de `ha` (+15 s) | `done` à 21 607 ms |

L'hypothèse de la fenêtre de démarrage est donc écartée. Reste une particularité du cas
fautif, non rejouée : son boot précédent était le **premier démarrage après un OTA
firmware** (`verifyOta`, validation de la partition), avec un `dry run` lancé dans la
foulée. Piste faible, laissée ouverte.

Impossible d'en dire plus à distance : la partition `coredump` est déclarée dans la
table mais **aucun code ne la lit** — ni exposition HTTP, ni trace au boot. C'est un
manque qui a coûté cher à tout ce chantier.

## Étape 4 — activé par défaut, et OTA réel validé

`FLOW_OTA_SPIFFS_GZIP` vaut **1** depuis le 2026-08-15. Un serveur qui ne publie pas le
`.gz` continue de recevoir l'image complète, sans rien changer de son côté.

Deux OTA compressés réels enchaînés sur cible, firmware `4.3.3+20260815.000211`. Pour
que la mesure prouve vraiment l'écriture — réécrire la même version aurait été un faux
positif si le firmware n'écrivait rien —, la version installée a été **changée puis
restaurée** :

| | Avant | Après |
|---|---|---|
| OTA n° 1 | `spiffs 4.3.2+20260814.185951` | `spiffs 4.3.1+20260814.182559` |
| OTA n° 2 | `spiffs 4.3.1+20260814.182559` | `spiffs 4.3.2+20260814.185951` |

Interface web à HTTP 200 après chacun. Découpage du temps :

| Phase | Durée |
|---|---|
| Téléchargement 333 Ko + vérification complète (8,26 Mo décompressés + CRC-32) | **~3 s** |
| Écriture des 8,26 Mo (progression 50 → 100 %) | ~42 s |
| Total, redémarrage compris | **~50 s** |

**C'est l'objectif atteint** : la liaison WiFi n'est sollicitée que ~3 secondes, contre
plusieurs minutes auparavant, et l'écriture se fait connexion fermée — une coupure
radio pendant les 42 s d'écriture n'a plus aucun effet. L'étape 3 s'attaquerait à ces
42 s, qui sont désormais le seul poste restant.

## Panique intermittente au premier démarrage après un OTA

Fait à part, **antérieur à ce chantier et sans lien avec le gzip** : le premier
démarrage qui suit un OTA panique parfois, une vingtaine de secondes après le boot,
puis le démarrage suivant est stable.

| Boot post-OTA | Nature de l'OTA | Suite |
|---|---|---|
| 23:43:38 | firmware | stable |
| 23:45:03 | firmware | **panic à 23:45:24** (+21 s) |
| 00:06:32 | SPIFFS compressé | **panic à 00:06:59** (+27 s) |
| 00:09:02 | SPIFFS compressé | stable (90 s de surveillance) |

Deux fois sur quatre, sur les deux types d'OTA — donc ni le gzip ni l'écriture SPIFFS
n'en sont la cause. L'appareil s'en remet seul et le contenu écrit est intact. Le
diagnostic exige la tâche fautive, donc la partition `coredump`, que rien ne lit
aujourd'hui.

### Reste à faire

- [x] Lancer le `dry run` sur cible et relever le verdict → **panique, pile trop
      petite pour `tinfl_decompressor`**, corrigé.
- [x] Reflasher et relancer : `done` en moins de 2 s, cinq fois de suite.
- [x] Rejouer un `dry run` juste après un redémarrage, et à cheval sur le démarrage de
      `ha` : les deux passent, la panique ne se reproduit pas.
- [x] OTA réel et activation par défaut (étape 4).
- [ ] Élucider la panique intermittente du premier boot post-OTA — **prérequis :
      exposer la partition `coredump`**, sans quoi la tâche fautive reste inconnue.
- [ ] Étape 3 : n'écrire que les 911 Ko de pages non vierges, pour ramener les 42 s
      d'écriture à quelques secondes.
- [ ] Reprendre le `.pkg` : son blocage dans `webPkgExtract_` reste inexpliqué, mais
      il n'utilise pas `tinfl` — c'est donc une autre cause.
