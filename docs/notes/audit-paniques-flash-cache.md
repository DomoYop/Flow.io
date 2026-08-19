# Audit — paniques pendant les écritures flash (Waveshare ESP32-S3)

> Revue statique du 2026-08-19, sur l'arbre de travail de `main` (non commité).
> **Rapport seul** : aucune source modifiée, aucune compilation, aucune écriture en
> flash. Reprend et complète [ota-spiffs-gzip-bilan.md](ota-spiffs-gzip-bilan.md),
> § « Panne non résolue » (nuit du 2026-08-18).
>
> Chaque constat porte son statut : **[PROUVÉ]** (lisible dans la configuration ou le
> code), **[PLAUSIBLE]** (mécanisme cohérent, non vérifié sur cible), **[RÉFUTÉ]**
> (testé et écarté — ne pas rejouer).

---

## Synthèse

La panne a une explication qui rend compte de **tous** les faits relevés le 18/08, et
elle n'est ni dans le code OTA, ni dans SPIFFS.

**Sur le profil Waveshare, cinq tâches FreeRTOS ont leur pile *et* leur TCB alloués en
PSRAM.** Or la PSRAM de ce module est **octale**, donc accessible uniquement par le
cache — le même cache que la flash. Chaque écriture flash coupe ce cache sur les deux
cœurs : pendant ces fenêtres, une pile en PSRAM devient de la mémoire **inatteignable**.
Une seule interruption non masquable (IPC de blocage de l'autre cœur, watchdog
d'interruption) ou un seul parcours de TCB par l'ordonnanceur pendant la fenêtre suffit
alors à lire ou écrire dans le vide.

Un OTA ouvre **~2 140 de ces fenêtres**. La question n'est pas de savoir si l'appareil
plante, mais lequel des ~2 140 tirages tombe mal.

Cela explique, point par point, ce que trois hypothèses successives n'expliquaient pas :

| Fait observé le 18/08 | Explication |
|---|---|
| Tâche accusée différente à chaque essai (`EventBus`, `mqtt`, `async_tcp`) | Ce n'est pas la coupable : c'est la tâche courante — ou celle que l'ordonnanceur allait activer — au moment du tirage perdant |
| Backtrace tombant dans `_frxt_dispatch → vTaskSwitchContext → vPortEnterCritical` (redécodé pour cet audit) | L'ordonnanceur parcourt les listes de tâches prêtes, dont les maillons sont **dans les TCB** — et cinq TCB sont en PSRAM ; le code, lui, est bien en IRAM (PC `0x403839EF` est dans `0x4037_0000–0x403D_FFFF`) |
| `exc_cause = 65`, hors de la plage Xtensa 0-63 | Cause **pseudo** du gestionnaire de panique IDF — la famille qui contient « cache désactivé mais région cachée accédée », pas un `LoadProhibited` ordinaire |
| `PC = 0xFFFFFFFE`, `corrupted: true` | Adresse de retour relue depuis une pile devenue illisible |
| L'essai n° 4, **OTA firmware pur**, plante pareil | Le mécanisme ne dépend pas de la partition écrite, seulement du fait d'écrire en flash |
| `SpiffsAccessLock` et le retrait de `SPIFFS.end()` n'ont rien changé | Ils traitent une course SPIFFS ; le défaut n'est pas dans SPIFFS |
| Deux réussites le 15/08, cinq échecs le 18/08, **à code équivalent** | Probabilité, pas déterminisme : elle suit la charge de fond, c'est-à-dire la probabilité qu'une tâche à pile PSRAM soit la tâche courante au moment de chaque fenêtre |
| SPIFFS corrompu après chaque tentative | Conséquence, pas cause : le crash survient en pleine écriture et laisse la partition à moitié écrite |

Statut : **[PLAUSIBLE]**, mais avec une mesure décisive disponible **immédiatement**,
par HTTP, sans rien écrire — voir ci-dessous. Si elle confirme, le correctif tient en
cinq lignes supprimées.

---

## Le test décisif, à faire avant toute autre chose

Le vidage de crash déjà en flash porte la réponse, et `CoreDumpInfo` l'expose déjà :
il enregistre les registres **a0-a15**, donc **a1 = le pointeur de pile au moment du
crash**.

```bash
curl.exe -s "http://10.10.50.90/api/system/coredump"
```

Lire `regs[1]` (= a1) et `exc_vaddr`, puis les situer dans la carte mémoire de
l'ESP32-S3 :

| Plage | Signification |
|---|---|
| `0x3FC8_8000 – 0x3FCF_FFFF` | SRAM interne (données) — pile saine |
| `0x3C00_0000 – 0x3DFF_FFFF` | **PSRAM** (données, via cache) — pile en mémoire externe |
| `0x4037_0000 – 0x403D_FFFF` | IRAM (code) |
| `0x4200_0000 – 0x43FF_FFFF` | Flash (code, via cache) |

- **`a1` dans `0x3Cxx_xxxx`** → la tâche interrompue avait sa pile en PSRAM :
  hypothèse **confirmée**, et le correctif est celui du § Axe 1.
- **`exc_cause` = 28 (`LoadProhibited`) ou 29 (`StoreProhibited`) avec `exc_vaddr`
  dans `0x3Cxx_xxxx`** → même conclusion, par l'autre bout.
- `a1` en SRAM interne et `exc_vaddr` hors PSRAM → l'hypothèse tombe, et il faut
  reprendre par l'axe 2.

Coût : une requête GET. Aucun risque : le vidage n'est effacé que sur `DELETE` explicite
(`CoreDumpInfo::erase`, [WebInterfaceServer.cpp:7364](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp)).

**Attention** : le vidage en place est celui du **dernier** crash
(`CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` n'est pas posé) et il appartient au firmware
qui tournait alors. Relever `elf_sha256` et le confronter aux ELF de `binary/debug/`
avant de croire le backtrace — les registres, eux, restent lisibles sans symboles.

---

## Ce que dit le vidage déjà en main **[relevé et décodé le 2026-08-19]**

Un relevé du 18/08 à 19:51 traîne à la racine du dépôt sous le nom `scratch_coredump.bin` —
malgré l'extension, c'est le **résumé JSON** de `GET /api/system/coredump`, pas l'image
brute. Il a été décodé pour cet audit :

```json
{"ok":true,"present":true,"task":"EventBus","pc":"0x403839EF","exc_cause":65,
 "exc_vaddr":"0x00000000","reason":"","elf_sha256":"8131e772b","size":62244,
 "corrupted":true,"backtrace":["0x403839EF","0x40384E04","0x40383D38"]}
```

### L'ELF correspondant s'identifie en une commande

`app_elf_sha256` fait 9 caractères par construction (`CONFIG_APP_RETRIEVE_LEN_ELF_SHA`) :
ce n'est pas une troncature. Il suffit de comparer au préfixe du sha256 des ELF conservés :

```bash
for f in binary/debug/*.elf; do printf "%-45s %s\n" "$f" "$(sha256sum "$f" | cut -c1-9)"; done
```

`8131e772b` → **`binary/debug/flowios3-14a0af9.elf`**. La méthode est validée au passage
sur le cas connu du bilan : `53d1e8565` → `flowios3-4.3.3+20260815.000211.elf`.

### Backtrace décodé

```
0x403839ef  xPortSetInterruptMaskFromISR  portmacro.h:552
            (inline dans) xPortEnterCriticalTimeout  port.c:478
0x40384e04  vPortEnterCritical  portmacro.h:567
            (inline dans) vTaskSwitchContext  tasks.c:3653
0x40383d38  _frxt_dispatch  portasm.S:451
```

Le crash est donc **dans le changement de contexte FreeRTOS**, sur du code bien situé en
IRAM (`0x4037_0000–0x403D_FFFF`) : le code n'a pas disparu, c'est bien un accès mémoire
qui a échoué. `vTaskSwitchContext` parcourt les listes de tâches prêtes, dont les maillons
sont **embarqués dans les TCB** — et cinq TCB sont en PSRAM (§ Axe 1).

### Trois détails qui comptent

- **`exc_cause = 65` est hors de la plage matérielle Xtensa** (`XCHAL_EXCCAUSE_NUM = 64`,
  causes valides 0-63). C'est donc une **cause pseudo** posée par le gestionnaire de
  panique de l'IDF (famille `PANIC_RSN_*` : double exception, exception noyau, watchdog
  d'interruption, **erreur de cache**), pas un `LoadProhibited` ordinaire — ce que
  confirme `exc_vaddr = 0`. Le décodage exact demande `espcoredump.py` avec l'ELF
  ci-dessus ; parmi les candidats, `PANIC_RSN_CACHEERR` (« cache désactivé mais région
  cachée accédée ») **est la description littérale de l'axe 1**.
- **`size = 62 244 o` sur une partition de 65 536 o, soit 95 %.** Le vidage remplit
  presque la partition `coredump` : rien ne garantit qu'un vidage un peu plus fourni
  (plus de tâches vivantes) y tienne encore. Argument direct pour l'axe 7.
- **Ce relevé ne porte pas `regs`** : il est antérieur à l'ajout des registres a0-a15
  (firmware de 20:13, relevé de 19:51). Le firmware en service **expose désormais** ces
  registres — d'où le test décisif ci-dessus, qui n'était pas réalisable ce soir-là.

Rangement : `scratch_coredump.bin` est un fichier de travail non suivi, à la racine et mal
nommé. À déplacer dans `binary/debug/` sous un nom parlant, ou à supprimer.

---

## Axe 1 — La fenêtre « cache flash désactivé » **[cause probable]**

### Ce que dit la configuration réellement liée

`board = esp32-s3-devkitc1-n16r8` + `board_build.psram = enabled` sélectionnent les
bibliothèques précompilées `esp32s3/qio_opi`. Leur `sdkconfig.h` :

| Réglage | Valeur | Conséquence |
|---|---|---|
| `CONFIG_SPIRAM_MODE_OCT` | 1 | PSRAM **octale** : même cache que la flash → pendant une écriture flash, la PSRAM est inaccessible elle aussi |
| `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` | 1 | les piles de tâches **peuvent** être placées en mémoire externe (garde-fou IDF levé) |
| `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` | = ci-dessus | idem |
| `CONFIG_SPIRAM_USE_MALLOC` | 1 | `malloc`/`new` peuvent rendre de la PSRAM sans qu'on le demande |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 4096 | tout bloc **> 4 Ko** part en PSRAM par défaut |
| `CONFIG_ARDUINO_ISR_IRAM` | **non posé** | `ARDUINO_ISR_FLAG = 0` : les ISR GPIO d'Arduino sont **masquées** pendant la fenêtre |
| `CONFIG_ESP_INT_WDT_TIMEOUT_MS` | 300 | le watchdog d'interruption reste armé pendant la fenêtre |

Rejouable :

```bash
grep -E "SPIRAM_MODE_OCT|ALLOW_STACK_EXTERNAL|TASK_CREATE_ALLOW_EXT_MEM|SPIRAM_USE_MALLOC|MALLOC_ALWAYSINTERNAL|ARDUINO_ISR_IRAM|INT_WDT_TIMEOUT" ~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h
```

### Les cinq piles en PSRAM **[PROUVÉ]**

`ModuleManager` bascule sur `xTaskCreatePinnedToCoreWithCaps` dès que `taskStackCaps()`
demande de la PSRAM ([ModuleManager.cpp:283](../../src/Core/ModuleManager.cpp)) — et
cette fonction alloue **la pile *et* le TCB** avec les capacités demandées.

| Module | Cœur | Pile | Déclaration |
|---|---|---|---|
| `AlarmModule` | 1 | 2 560 | [AlarmModule.h:24](../../src/Modules/AlarmModule/AlarmModule.h) |
| `IOModule` | 1 | 2 560 | [IOModule.h:94](../../src/Modules/IOModule/IOModule.h) |
| `LogDispatcherModule` | 1 (défaut) | 3 072 (défaut) | [LogDispatcherModule.h:20](../../src/Modules/Logs/LogDispatcherModule/LogDispatcherModule.h) |
| `HAModule` | 0 | 2 560 | [HAModule.h:28](../../src/Modules/Network/HAModule/HAModule.h) |
| `SystemMonitorModule` | 0 | 3 072 | [SystemMonitorModule.h:27](../../src/Modules/System/SystemMonitorModule/SystemMonitorModule.h) |

(`TFTModuleS3` déclare la même chose mais est exclu du `build_src_filter` Waveshare.)

Toutes sont gardées par `#if defined(FLOW_PROFILE_WAVESHARE)` : **le défaut n'existe que
sur le profil qui sert de source de vérité runtime**, ce qui explique qu'il n'ait jamais
été vu ailleurs.

### Le mécanisme

Une écriture flash passe par `spi_flash_disable_interrupts_caches_and_other_cpu()`, qui

1. coupe le cache sur les deux cœurs — flash **et** PSRAM deviennent illisibles ;
2. masque les interruptions **non** déclarées `ESP_INTR_FLAG_IRAM` ;
3. **immobilise l'autre cœur par une interruption de haut niveau (IPC)** — laquelle
   s'exécute, comme toute interruption Xtensa, **sur la pile de la tâche courante de ce
   cœur**.

Le point 3 est le nœud : si la tâche qui tournait sur le cœur 1 a sa pile en PSRAM,
l'ISR de blocage écrit son contexte dans une zone inatteignable. Le même raisonnement
vaut pour le watchdog d'interruption (niveau 4, jamais masqué) et pour tout parcours de
TCB par l'ordonnanceur.

C'est exactement le tableau des vidages : le PC est en IRAM (code valide, l'ordonnanceur),
la pile ne l'est plus, l'adresse de retour revient à `0xFFFFFFFE`, et la tâche nommée
change à chaque essai.

### Le correctif, si le test décisif confirme

Retirer les cinq surcharges `taskStackCaps()` (revenir à `Module::taskStackCaps()`,
soit `MALLOC_CAP_INTERNAL`). Coût : **~13,8 Ko de DRAM interne** (2 560 + 2 560 + 3 072 +
2 560 + 3 072). À confronter à la marge réelle avant de trancher — et noter que le seuil
« panique mémoire » de `SystemMonitorModule` est à 12 000 o de libre (voir axe 6), donc
la marge n'est pas illimitée.

Trois façons de payer ces 13,8 Ko si nécessaire, par ordre de préférence :

1. ne rapatrier que les tâches **du cœur 1** (`Alarm`, `IO`, `LogDispatcher` — 8,2 Ko) :
   ce sont celles que l'ISR de blocage interrompt, donc les plus exposées ;
2. réduire les piles rapatriées à leur consommation mesurée
   (`uxTaskGetStackHighWaterMark`, voir axe 3) ;
3. laisser en PSRAM les **données** volumineuses, qui ne posent aucun problème : ce n'est
   pas la PSRAM qui est en cause, ce sont les piles et les TCB.

### Ce qui est écarté sur cet axe

- **[RÉFUTÉ] Le compteur d'impulsions GPIO n'est pas la cause.**
  `attachInterruptArg` ([GpioCounterDriver.cpp:61](../../src/Modules/IOModule/IODrivers/GpioCounterDriver.cpp))
  passe par `gpio_install_isr_service(ARDUINO_ISR_FLAG)` et `ARDUINO_ISR_FLAG` vaut 0
  (`CONFIG_ARDUINO_ISR_IRAM` non posé) : l'interruption est **masquée** pendant la
  fenêtre. Deux remarques séparées, sans lien avec le crash :
  - des impulsions sont **perdues** pendant les ~42 s d'écriture (données, pas stabilité) ;
  - `handleInterrupt_` est annoté `IRAM_ATTR` alors que `digitalRead()` et `micros()` ne
    le sont pas dans ce build — l'annotation donne une fausse impression de sûreté et
    deviendrait un vrai bug le jour où `CONFIG_ARDUINO_ISR_IRAM` serait activé.
- **[RÉFUTÉ] Le tampon de secteur d'`Update` n'est pas en PSRAM.**
  `UpdateClass::_writeBuffer` alloue `new uint8_t[SPI_FLASH_SEC_SIZE]`, soit **exactement
  4 096 o** : au seuil `ALWAYSINTERNAL`, donc interne — à un octet près.
- **[RÉFUTÉ] Le tampon de réponse d'ESPAsyncWebServer non plus** :
  `ASYNC_RESPONCE_BUFF_SIZE = CONFIG_LWIP_TCP_MSS * 2` ≈ 2,8 Ko → interne.

---

## Axe 2 — Le chemin d'écriture OTA **[sain, mais il amplifie]**

Relu intégralement : `spiffsInflateMem_`
([FirmwareUpdateModule.cpp:1440](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.cpp)),
`unmountSpiffsForWrite_` (:1563), `runSpiffsFromGz_` (:1599). Rien à reprocher au fond :
vérification CRC avant écriture, essai à blanc, `tinfl` sur le tas, `Update.abort()` sur
chaque sortie d'erreur, yield tous les 4 tours. Trois observations quand même.

- **[PROUVÉ] Le nombre de fenêtres est le vrai problème.** `UpdateClass::_writeBuffer`
  efface par blocs de 64 Ko et écrit par secteurs de 4 Ko : 8 257 536 o donnent
  **126 effacements + 2 016 écritures ≈ 2 140 fenêtres cache-off**. C'est le multiplicateur
  de l'axe 1, et c'est ce que l'axe 7 divise par 5.
- **[PROUVÉ, sans conséquence] `Update.write(dict + dictOfs, …)` reçoit un pointeur
  PSRAM** (:1503, `dict` alloué en PSRAM à :1454). Sans effet : `UpdateClass::write`
  recopie d'abord dans son tampon interne, cache actif. À documenter pour que la question
  ne soit pas reposée.
- **[À vérifier] Durée d'un effacement de bloc de 64 Ko contre `INT_WDT` à 300 ms.**
  IDF fractionne les effacements longs, mais la marge n'a jamais été mesurée ici.

---

## Axe 3 — Piles FreeRTOS et cœur 0 **[risque réel, distinct]**

`Limits::Core::Task::DefaultStackSize = 3072` ; neuf modules épinglés sur le cœur 0 ; le
Task Watchdog ne surveille que l'idle du **cœur 0** (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0`,
5 s). Les piles les plus fines sont `HMIBuzzer` (2 048), puis `Alarm`, `EventBus`, `IO`,
`HA` (2 560).

Le précédent fait foi : `tinfl_decompressor` (10,7 Ko) déclaré en local sur une pile de
6 144 o, en août — la panne a coûté cinq correctifs à l'aveugle. Une pile de 2 560 o
laisse très peu de marge dès qu'un `snprintf` ou un tampon JSON local s'invite.

**Manque à combler** : aucune remontée systématique de `uxTaskGetStackHighWaterMark`.
Une ligne par tâche dans `/api/system/*` transformerait cette famille de pannes en
lecture de tableau. C'est peu de code, sans risque, et utile bien au-delà de ce chantier.

---

## Axe 4 — Routes `async_tcp` **[un défaut confirmé, une famille ouverte]**

- **[PROUVÉ] `/api/system/coredump/raw` : le mécanisme de la troncature est dans le code.**
  Le filler ([WebInterfaceServer.cpp:7352](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp))
  renvoie `0` dès que `CoreDumpInfo::imageRead` échoue. Or côté bibliothèque, `0` ne veut
  pas dire « erreur » mais **fin de flux** : `AsyncAbstractResponse::_ack` fait
  `_state = RESPONSE_END`. Résultat : un `200` tronqué, silencieux, avec un
  `Content-Length` qui ne sera jamais atteint — au lieu d'une erreur lisible.
  La *cause* de l'échec de lecture reste inconnue ; le correctif utile est de la rendre
  visible : journaliser le `esp_err_t` d'`esp_partition_read` et l'offset fautif dans
  `CoreDumpInfo::imageRead` ([CoreDumpInfo.cpp:176](../../src/Core/CoreDumpInfo.cpp)),
  puis relire. Rendre observable avant de corriger, comme pour l'essai à blanc.
  À noter : `imageRead` refuse aussi `len == 0` en renvoyant `false`, ce qui confond un
  appel dégénéré avec une vraie erreur.
- **[PROUVÉ] `printJsonEscaped_` est corrigé** : écriture par segments, avec le
  commentaire d'explication (:80-110). Le défaut quadratique est bien fermé.
- **[OUVERT] La famille, elle, ne l'est pas.** 16 appels à `beginResponseStream` dans ce
  fichier, **un seul pré-dimensionné** (:4788, celui du journal d'activité). Les 15 autres
  gardent le tampon par défaut (~1,4 Ko) et repartent en réallocations recopiantes dès
  qu'une réponse grossit. Ce n'est pas la panne du 18/08, mais c'est le même mode d'échec
  que celui qui a produit 28 `task_wdt` en trois jours.

---

## Axe 5 — Panique 20 à 27 s après un boot post-OTA **[non élucidé, piste ciblée]**

Relevé du 15/08 : 2 paniques sur 4 boots post-OTA, jamais en fonctionnement courant, le
boot suivant toujours stable. Deux éléments convergent vers la fenêtre de démarrage de
`ha` :

- le séquencement libère `mqtt` à 1,5 s, `poollogic` à 10 s et **`ha` à 15 s** — soit
  5 à 12 s avant le crash ;
- `HAModule` cumule la **pile la plus fine des tâches réseau (2 560 o)**, la découverte
  `FLOW_HA_ONESHOT_DISCOVERY=1` (rafale de gros payloads JSON au démarrage) **et** une
  pile en PSRAM (axe 1).

Deux mécanismes distincts peuvent produire le même symptôme : débordement de pile pendant
la rafale de découverte, ou l'axe 1 déclenché par les écritures NVS/flash du premier
démarrage (marquage de validité de l'image OTA). Le vidage de crash tranche : si `a1` est
en PSRAM c'est l'axe 1, s'il est en SRAM interne mais hors des bornes de la pile de `ha`,
c'est un débordement.

---

## Axe 6 — `SystemMonitorModule` **[pas la cause, mais un faux ami]**

L'état `MemoryPressureState::Panic` (libre < 12 000 o **et** plus gros bloc < 5 000 o
**et** fragmentation > 55 %) déclenche un **redémarrage volontaire** après 5 s
([SystemMonitorModule.cpp:59 et :802](../../src/Modules/System/SystemMonitorModule/SystemMonitorModule.cpp)).

Deux conséquences à garder en tête :

- ce redémarrage se présente en `reset=software`, **pas** en `panic` : il ne peut donc
  pas expliquer les 3 `panic` du relevé, mais il pourrait expliquer des redémarrages
  attribués à autre chose ;
- l'homonymie est piégeuse — « panic » désigne ici un seuil de pression mémoire, sans
  aucun rapport avec le gestionnaire de panique de l'IDF. Les deux apparaissent dans les
  mêmes journaux.

### Et surtout : ces seuils mesurent le mauvais tas **[PROUVÉ]**

`deriveMemoryPressureState_` lit `snap.heap.freeBytes`, rempli par
[SystemStats.cpp:18](../../src/Core/SystemStats.cpp) :

```cpp
const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
```

`MALLOC_CAP_8BIT` **agrège la DRAM interne et la PSRAM**. Sur un module à 8 Mo de PSRAM,
`freeBytes` vaut plusieurs mégaoctets en permanence. Donc :

- les quatre seuils (24 k / 20 k / 16 k / **12 k**) ne peuvent **jamais** se déclencher ;
- `fragPercent`, calculée sur le tas combiné, est dominée par la PSRAM et reste basse ;
- l'état `Panic` et son redémarrage à 5 s sont morts-nés ;
- **le seul tas qui peut réellement s'épuiser — l'interne — n'est pas surveillé.**

C'est directement gênant pour le correctif de l'axe 1, qui reprend 13,8 Ko de DRAM
interne : l'instrument censé prévenir de l'épuisement regarde ailleurs.

**Traité en deux temps, volontairement.** Basculer les seuils sur `MALLOC_CAP_INTERNAL`
réveillerait d'un coup quatre seuils qui n'ont jamais servi, dont un qui **redémarre la
carte** : si la DRAM interne libre est réellement sous 12 Ko, l'appareil se mettrait à
redémarrer seul. Étape 1 (faite) : exposer les deux tas séparément, sans toucher à aucun
seuil. Étape 2 (à faire une fois les vrais chiffres connus) : recalibrer.

```bash
curl.exe -s "http://10.10.50.90/api/system/heap"
```

Rend `internal` (free, min_free, largest, total, frag), `psram` et `combined` — ce
dernier étant ce que les seuils continuent de lire. Les champs correspondants sont dans
`HeapStats` ([SystemStats.h](../../src/Core/SystemStats.h)).

### Les marges de pile existent déjà, mais en `LOGD`

L'item « exposer `uxTaskGetStackHighWaterMark` » de l'axe 3 est aux trois quarts fait :
`SystemMonitorModule` interroge déjà `uxTaskGetSystemState` et produit des lignes
`Stack <module>/<tâche>@c<cœur>=<marge>`, avec un `!` sous 300
([SystemMonitorModule.cpp:405](../../src/Modules/System/SystemMonitorModule/SystemMonitorModule.cpp)).
Elles partent en **`LOGD`**, donc invisibles au niveau de log courant. Rien à écrire :
il suffit de les rendre visibles. À confirmer avant de s'en servir pour redimensionner
une pile : si la valeur est en mots ou en octets.

À vérifier aussi : que les allocations de l'OTA (le `.gz` de 333 Ko et la fenêtre de
32 Ko, toutes deux en PSRAM) ne peuvent pas approcher ces seuils.

---

## Axe 7 — Retailler la partition SPIFFS **[gain réel, mais pas celui annoncé]**

### Ce qui occupe réellement la partition

`prepare_spiffs_data.py` gzip les assets dans un staging avant `mkspiffs` : ce qui est
flashé n'est pas `data/` (1,42 Mo de sources) mais
`.pio/build/Waveshare-ESP32-S3/spiffs_data` = **324 589 o (317 Ko), 164 fichiers** —
recoupé par `binary/flowios3-spiffs-4.3.6.pkg` (329 302 o). Sur une partition de
8 257 536 o : **3,9 % d'occupation**.

| Taille spiffs | Occupation | Écriture (197 Ko/s mesurés) | Fenêtres cache-off | Image `.gz` |
|---|---|---|---|---|
| 7,875 Mo (actuel) | 3,9 % | ~42 s | **~2 140** | 333 Ko |
| 2 Mo | ~21 % | ~11 s | ~545 | ~310 Ko |
| **1,5 Mo (proposé)** | **~28 %** | **~8 s** | **~410** | ~310 Ko |
| 1 Mo | ~42 % | ~5 s | ~270 | ~305 Ko |

Occupation = 317 Ko utiles majorés des surcoûts SPIFFS (pages de 256 o, une page d'index
par fichier sur 164 fichiers), soit ~420 Ko réellement occupés.

### Verdict

- **Transfert réseau : aucun gain.** Le `.gz` pèse 333 Ko parce que le *contenu* pèse
  317 Ko ; le padding se compresse à presque rien. Le commentaire en tête de
  `partitions_flowios3_ota_16mb.csv` (« la rétrécir à 1 Mo diviserait le transfert par
  7,9 ») décrit l'état d'**avant** le gzip : ce raisonnement est périmé, à corriger dans
  le fichier.
- **Durée d'écriture : ÷5** (42 s → ~8 s).
- **Exposition à l'axe 1 : ÷5** (~2 140 fenêtres → ~410). Ne corrige rien, mitige beaucoup.
- **Fenêtre destructive en cas de coupure secteur : ÷5** ; montage SPIFFS au boot plus rapide.
- **Bonus diagnostic** : c'est la seule occasion d'agrandir `coredump` (64 Ko → 256 Ko),
  or le vidage complet de **toutes** les tâches est précisément la mesure qui manque.
  Ce n'est pas théorique : le vidage relevé le 18/08 pèse **62 244 o sur 65 536, soit
  95 % de la partition** (§ « Ce que dit le vidage déjà en main »). On est à la limite,
  et un vidage un peu plus fourni ne tiendrait pas.
- **Rend l'« Étape 3 — écrire moins » sans objet** : le gain visé par l'écriture page par
  page via `esp_partition_*` est obtenu par la table de partitions, sans quitter l'API
  `Update` ni écrire une ligne de code.

### Coût

- Un flash USB complet — **déjà engagé** : le SPIFFS de la carte est aujourd'hui non
  monté, et seul un flash filaire la remet d'aplomb de façon fiable. Le repartitionnement
  ne coûte alors rien de plus que le passage déjà nécessaire.
- **Aucun changement de code** : vérifié, aucune taille SPIFFS en dur ; tout vient
  d'`esp_partition_find_first` ([FirmwareUpdateModule.cpp:1719](../../src/Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.cpp))
  et le contrôle `isize != partitionSize` (:1627) suit automatiquement.
- NVS préservée tant que `nvs` garde `0x9000 / 0x5000`. Exporter quand même la config en
  JSON avant, et effacer `otadata` au passage.

### Option A — minimale, recommandée

```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x400000,
app1,     app,  ota_1,   0x410000, 0x400000,
spiffs,   data, spiffs,  0x810000, 0x180000,
coredump, data, coredump,0x990000, 0x40000,
```

`app0`/`app1` inchangés à 4 Mo (binaire 2,01 Mo = 48 %, marge intacte), `spiffs` 1,5 Mo,
`coredump` 256 Ko. Fin à `0x9D0000` : **6,4 Mo laissés libres** en queue de flash.

### Option B — prévoyante

Idem, plus une `spiffs_b` de 1,5 Mo (`0x990000`, `coredump` repoussé à `0xB10000`),
réservée à une future bascule A/B du contenu web : l'OTA écrirait la partition inactive,
et une coupure laisserait le système de fichiers en service intact — ce qui supprime le
caractère destructif de l'opération. Inerte tant qu'aucun code ne la monte.

Piège à connaître : deux partitions de sous-type `spiffs` rendent
`esp_partition_find_first(DATA, SPIFFS, nullptr)` ambigu — il rend la première de la
table, donc le comportement actuel est préservé, mais tout code futur devra monter
**par label**.

À poser maintenant si l'idée séduit, puisque la table ne se réécrit qu'exceptionnellement.

### Option C — à écarter

Réduire aussi `app0`/`app1` à 3 Mo : aucun gain (la flash n'est pas la ressource rare)
contre un plafond rapproché — le binaire est passé de 1,956 Mo à 2,009 Mo en un mois.

---

## Hypothèses réfutées — ne pas les rejouer

| Hypothèse | Réfutée par |
|---|---|
| Course SPIFFS entre l'OTA, le journal d'activité et le serveur de fichiers | Essai n° 3 : verrou actif, même crash |
| `SPIFFS.end()` avant l'écriture | Essai n° 3 (retiré, même crash) et essai n° 4 (OTA firmware, aucun SPIFFS) |
| Défaut propre au SPIFFS ou au gzip | Essai n° 4 : OTA firmware pur, même crash |
| Tampon de secteur d'`Update` en PSRAM | `new uint8_t[4096]`, exactement au seuil `ALWAYSINTERNAL` → interne |
| Tampon de réponse `async_tcp` en PSRAM | `CONFIG_LWIP_TCP_MSS * 2` ≈ 2,8 Ko → interne |
| ISR du compteur d'impulsions exécutée cache coupé | `ARDUINO_ISR_FLAG = 0` → interruption masquée pendant la fenêtre |

---

## Ordre de marche proposé

1. **Lire le vidage** (`GET /api/system/coredump`) et situer `a1` et `exc_vaddr`. Une
   requête, zéro risque, réponse binaire sur l'axe 1. Identifier l'ELF par le préfixe
   sha256 (§ « Ce que dit le vidage déjà en main ») avant de décoder quoi que ce soit.
2. Si confirmé : **retirer les surcharges `taskStackCaps()`** (au minimum celles des
   tâches du cœur 1), vérifier la marge de DRAM interne, compiler.
3. **Repartitionner** (option A ou B) pendant le flash USB de toute façon nécessaire pour
   remettre SPIFFS d'aplomb — en profitant pour passer `coredump` à 256 Ko.
4. Rejouer la campagne d'OTA du bilan (une dizaine d'essais, en relevant le `reset=`) :
   avec le correctif *et* la partition réduite, le taux doit tomber à zéro. Deux
   changements à la fois, mais le n° 3 ne peut pas masquer un échec du n° 2 — il le rend
   seulement plus rare, ce que dix essais distinguent.
5. Indépendamment : pré-dimensionner les 15 `beginResponseStream` restants, instrumenter
   `imageRead`, exposer les marges de pile.

## Suite donnée — 2026-08-19 au soir

L'audit ci-dessus a d'abord été rendu sans toucher à quoi que ce soit. Les décisions
prises ensuite ont été appliquées :

| Changement | Fichiers |
|---|---|
| Les six surcharges `taskStackCaps()` supprimées (les cinq du build Waveshare + `TFTModuleS3`, hors build mais porteuse du même défaut) | `AlarmModule.h`, `IOModule.h`, `LogDispatcherModule.h`, `HAModule.h`, `SystemMonitorModule.h`, `TFTModuleS3.h` |
| La règle documentée une seule fois, au point qui la porte | [Module.h](../../src/Core/Module.h) |
| Table de partitions **option B**, `spiffs` et `spiffs_b` à **2 Mo**, `coredump` à 256 Ko | [partitions_flowios3_ota_16mb.csv](../../partitions_flowios3_ota_16mb.csv) |
| Exposition des deux tas, **sans toucher aux seuils** | [SystemStats.h](../../src/Core/SystemStats.h), [SystemStats.cpp](../../src/Core/SystemStats.cpp), route `GET /api/system/heap` |

**Rien n'a été écrit sur l'appareil** : ni OTA, ni flash USB, ni la moindre requête vers
la carte. Le firmware produit (`4.3.6+20260819.230427`) attend.

Gates rejoués localement, tous verts : `pio run -e Waveshare-ESP32-S3` (RAM statique
34,9 %, **inchangée** — les piles sont allouées sur le tas, le coût des 13,8 Ko ne se lit
pas ici ; flash 47,9 % sous le plafond de 85 %), `validate_i18n.py` (pré-script du build),
`check_io_port_sync.py` (waveshare 42/42), `check_firmware_size.py`, et **cppcheck aux
options exactes de la CI — zéro alerte**. Non rejouables sur ce poste : `gitleaks` et
`native-tests` (pas de `g++` hôte).

### Ordre impératif pour la suite

**Lire le vidage avant tout flash USB.** Un OTA firmware laisse la partition `coredump`
intacte, mais un effacement complet en USB la détruit — et avec elle la seule preuve
disponible. Le `curl` sur `/api/system/coredump` passe avant le fer à souder.

Et lors du passage USB, ne pas oublier l'image du système de fichiers : `firmware.factory.bin`
ne porte que bootloader + table + application. Sans un `uploadfs`, SPIFFS reste vide.
