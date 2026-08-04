# Assainissement de la gestion des traductions

**Statut : outillage implémenté le 2026-08-04** sur la branche `i18n-assainissement`.
La campagne de réécriture des catalogues anglais reste à mener (voir §6).

---

## 1. La chaîne, telle qu'elle est

```
src/Modules/<M>/text/          5 SOURCES écrites à la main, aucune dérivée d'une autre
  ├─ i18n.fr.json              catalogue FR : token → texte, clés triées
  ├─ i18n.en.json              catalogue EN, idem
  ├─ cfgdocs.fr.json           type + tokens label_t / help_t des variables de config
  ├─ cfgmods.fr.json           visible_if, hidden, enum_set, meta.enum_sets
  └─ runtimeui.json            descripteurs Runtime UI, tokens *_t (7 modules)
        │
        ├─ pre: validate_i18n.py            ← garde-fou, n'écrit rien
        ├─ pre: generate_runtimeui_manifest.py → src/Core/Generated/RuntimeUi*_Generated.h
        └─ pre: prepare_spiffs_data.py
                 ├→ generate_config_docs.py   → data/webinterface/cfgdocs.json (transitoire)
                 │    + libellés de ports depuis scripts/io_port_labels.py
                 └→ generate_cfgdoc_chunks.py → data/wc/{i.j, m<fnv1a32>.j, i18n.<loc>.j}
```

Catalogue distinct, **hors de ce périmètre** : `data/webinterface/i18n/{fr,en}.json` (457 clés),
celui du shell web. Écrit à la main, sans résidu français, c'est la **référence de style**.

### Deux résolveurs de tokens, aux comportements différents

| | cfgdocs / cfgmods | runtimeui |
|---|---|---|
| Sortie | `label` + `label_i18n` + `label_t` conservés | `_t` **supprimé**, pas de `*_i18n` |
| Changement de langue | re-traduit côté client (`app.js cfgDocTr`) | **figé dans le binaire** |

### Le mode d'échec, à connaître

Un token sans traduction **ne casse rien** : il s'affiche brut à l'écran. C'est le
`translations.get(token, token)` présent aux cinq points de résolution. Les JSON invalides étaient
avalés par des `except Exception: return {}`. `validate_i18n.py` est le seul garde-fou.

---

## 2. Ce qui a été supprimé, et pourquoi

| Script | Ce qu'il faisait |
|---|---|
| `generate_module_i18n_en.py` | Réécrivait les 33 `i18n.en.json` **sans lire l'existant**, par cascade de `str.replace` (`" d'" → " of "`, `" l'" → " "`). Remplaçait « Water temperature » par « Temperature eau ». Deux règles de son glossaire contenaient déjà sa propre sortie franglaise en clé d'entrée, donc mortes. |
| `migrate_text_manifests.py` | Outil one-shot. Réécrivait **inconditionnellement** les 3 sources FR des 33 modules. Ses entrées (`data/webinterface/cfgdocs.fr.json`) n'existent pas — le générateur produit `cfgdocs.json` sans locale — et son `_load_json` renvoie `{}` sur fichier absent : le lancer aurait vidé toutes les sources. C'est lui qui avait posé le `"_meta": {"generated": true}` trompeur. |
| `config_docs.fr.overrides.json` | Orphelin, remplacé par les `cfgdocs.fr.json` par module. |

### Table `MODULE_RULES`, récupérée avant suppression

Mapping chemin de config → module, information non redondante ailleurs :

```
^*/            → PoolDeviceModule          ^alarms/       → AlarmModule
^elink/client/ → Network/I2CCfgClientModule ^elink/server/ → Network/I2CCfgServerModule
^elink/lcd/    → SupervisorHMIModule        ^fcd/          → FlowConnectDisplay/…UdpClientModule
^fwupdate/     → Network/FirmwareUpdateModule ^ha/         → Network/HAModule
^hmi/nextion_udp/ → Network/HmiUdpServerModule ^hmi(/|$)   → HMIModule
^io/           → IOModule                   ^log(/|$)     → Logs/LogHubModule
^mqtt/ ^network/mqtt(/|$) → Network/MQTTModule
^network/time(/|$) ^time(/|$) → Network/TimeModule
^network(/wifi)?(/|$) ^wifi(/|$) → Network/WifiModule
^pdm(/|$)      → PoolDeviceModule           ^poollogic(/|$) → PoolLogicModule
^sysmon(/|$)   → System/SystemMonitorModule  (défaut)      → System/SystemModule
```

---

## 3. Deux pièges de la chaîne, reproduits volontairement dans le validateur

1. **La locale est extraite par découpe brute du nom de fichier**
   (`name[len("i18n."):-len(".json")]`). Un `i18n.fr.bak.json` oublié dans un `text/` créerait une
   locale `fr.bak` et un catalogue parasite dans `data/wc/`. Le validateur applique la **même**
   découpe et refuse toute locale hors `fr`/`en` : il valide le comportement réel de la chaîne, pas
   le comportement souhaité.
2. **Les 33 catalogues sont fusionnés à plat par locale**, dernier gagnant, silencieusement. Deux
   modules définissant le même token avec des valeurs différentes se seraient écrasés sans bruit ;
   c'est désormais une erreur bloquante (`TOKEN_COLLISION`).

---

## 4. Libellés de ports IO

`generate_config_docs.py` réécrivait les enum_sets de binding avec des libellés français codés en dur
**et effaçait les tokens** (`pop("label_t")`, `pop("label_i18n")`) : ces menus étaient intraduisibles
par construction.

`scripts/io_port_labels.py` sépare les données (port, alias, indice) des seuls fragments de langue :
10 noms de périphériques et 7 gabarits, contre 80 libellés figés auparavant. Points à connaître :

- Les tokens sont **segmentés par famille** (`analog`/`din`/`dout`/`slot`) : le slot 0 et
  « non connecté » valent tous deux 0 et se seraient écrasés.
- Ils sont **résolus par profil** : la valeur 407 désigne `MCPOut8 - MCP23017 bit 7` sur Waveshare et
  `PortPCF0Bit7 - Sortie PCF8574 - Bit 7` sur flow.io. Cohérent avec `data/wc/`, déjà dépendant du
  profil compilé.
- Le contrôle du validateur porte sur la **complétude des tables de langue**, pas sur les tokens
  rendus : `render()` retombe sur la clé brute quand une locale manque, donc le token existe quand
  même et une comparaison d'ensembles ne verrait rien.

Bug d'affichage corrigé au passage, que le codage en dur masquait sur Waveshare : les ports MCP23017
bit 7 à 15 annonçaient `[3107]`–`[3115]`, hors de la grille de binding 100-415, au lieu de
`[407]`–`[415]`. Visible tel quel sur les profils `generic` et flow.io.

---

## 5. État de départ mesuré (2026-08-04)

0 erreur, **737 entrées anglaises à reprendre sur 1920 (38,4 %)**. Le catalogue FR est sain :
1920 clés couvrant 100 % des 1880 tokens référencés, 40 clés orphelines sans effet.

| Module | Dette EN | | Module | Dette EN |
|---|---:|---|---|---:|
| `IOModule` | 289 | | `Network/HAModule` | 9 |
| `PoolLogicModule` | 124 | | `System/SystemModule` | 8 |
| `TFTModuleS3` | 106 | | `HMIModule` | 7 |
| `PoolDeviceModule` | 70 | | `System/SystemMonitorModule` | 6 |
| `Logs/LogHubModule` | 29 | | `Network/FirmwareUpdateModule` | 5 |
| `SupervisorHMIModule` | 27 | | `Network/HmiUdpServerModule` | 2 |
| `Network/TimeModule` | 25 | | `AlarmModule` | 1 |
| `Network/MQTTModule` | 16 | | `FlowConnectDisplay/…UdpClient` | 1 |
| `Network/WifiModule` | 10 | | `HMIBuzzerModule`, `Network/EthernetModule` | 1 + 1 |

La dette est comptée par une heuristique : entrée identique au FR alors qu'elle porte de la langue,
ou entrée contenant un accent, une élision ou un mot français sans équivalent orthographique anglais.
Elle est donc approximative par nature — c'est un indicateur de progression, pas une vérité.

---

## 6. Campagne de réécriture — méthode

Un lot = un commit, sur un ou deux `i18n.en.json`.

1. `python scripts/validate_i18n.py --module <Module>` → la liste exacte à traiter.
2. Réécriture manuelle, en respectant le glossaire ci-dessous et en **conservant strictement les
   segments neutres** : identifiants (`PortADSInternal0`), valeurs entre crochets (`[100]`), unités,
   `%`, `°C`, `pH`, placeholders. Laisser identiques au FR les entrées réellement neutres.
3. `python scripts/validate_i18n.py --write-ratchet` → abaisse le plafond, **inclus dans le commit** :
   le diff de `scripts/i18n_ratchet.json` est la preuve chiffrée du lot.
4. Build de non-régression tous les 2-3 lots. Un lot EN ne touche le binaire que via
   `RuntimeUiAlarmText_Generated.h`, donc seulement pour `AlarmModule` et `PoolLogicModule`.

Ordre proposé, par visibilité pour un utilisateur anglophone : `PoolLogicModule`, `PoolDeviceModule`,
`Network/WifiModule`, `System/SystemModule` (les premières minutes d'utilisation), puis
`Network/{Time,MQTT,HA}Module`, puis `IOModule` découpé par préfixe de token, puis le reste.
`TFTModuleS3` en dernier : l'écran lui-même est français en dur, son catalogue ne sert qu'à ses pages
de configuration web.

En fin de campagne : passer le validateur et la CI en `--strict`, ce qui rend toute régression
impossible.

### Glossaire FR → EN

| FR | EN | À bannir |
|---|---|---|
| bassin / piscine | pool | ~~basin~~ |
| créneau (horaire) | slot / time window | ~~niche~~ |
| durée | duration | |
| seuil | threshold | |
| consigne | setpoint | ~~instruction~~ |
| sonde | probe | ~~sensor~~ (réservé à *capteur*) |
| capteur | sensor | |
| débit | flow rate | |
| cuve / bidon | tank | |
| renouvellement volumique | turnover | |
| oxygène actif | active oxygen | |
| électrolyseur | salt chlorine generator (SWG) | ~~electrolyser~~ |
| appairage / liaison | binding | |
| entrée / sortie (IO) | input / output | |
| broche | pin | |
| canal | channel | |
| interne / externe | internal / external | |
| activer / désactiver | enable / disable | ~~activate~~ |
| moniteur de puissance | power monitor | |
| marche / arrêt | on / off | |
| libellé | label | |
| écran | display / screen | |
| mesure | reading / measurement | |
| réglage | setting | |
| par défaut | default | |

Reprendre en priorité la terminologie de `data/webinterface/i18n/en.json` (`Dashboard`,
`Input`/`Output`, `Calibration`…) plutôt que d'inventer.

### Journal des lots

| Date | Module | Dette avant → après |
|---|---|---|
| 2026-08-04 | `PoolLogicModule` (10 tokens manquants) | 134 → 124 |

---

## 7. Reste à faire, hors périmètre

Ces chantiers concernent des textes **jamais passés par la chaîne de tokens** : ni le validateur ni
les catalogues de modules ne les atteignent.

1. **~136 littéraux français dans `data/webinterface/app.js`** hors de tout appel `tr()`. Le
   catalogue web existe et est sain, `tr()` est en place : c'est du refactoring JS.
2. **Pas de sélecteur de langue dans le header web.** La locale vient de NVS (`sys_lang`) et écrase
   le choix du navigateur en moins de 15 s. Pour changer de langue il faut deviner le chemin
   Configuration → Config Store Supervisor → `system` → `lang`.
3. **`TFTModuleS3` est 100 % français en dur.** Demanderait un catalogue embarqué en flash et une
   sélection runtime.
4. **Libellés Home Assistant figés dans `PoolDomain.h`.** Déjà en anglais, et HA a sa propre couche
   de traduction.
5. **Pages de boot et de provisioning FR-only** (`index.html`, `app-core.js`, `light.*`, `prov.*`),
   servies avant tout chargement de catalogue.

**Piège connexe** : `data/wc/` est **versionné** (151 fichiers d'artefacts réécrits à chaque build et
dépendants du profil compilé). Un build Waveshare puis un build FlowIO se marchent dessus. Le diff
reste proportionnel au changement, donc supportable, mais c'est à savoir avant de s'étonner d'un
diff parasite.
