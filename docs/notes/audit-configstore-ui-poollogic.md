# Audit ConfigStore + interface de configuration (focus poollogic/équipements)

Note d'audit (juillet 2026, branche `io-refonte-unifiee`, après le commit `a2c3f20` de refonte de l'arbre poollogic). Deux volets : le cœur ConfigStore côté firmware, et la chaîne textes/manifestes → UI web. Sert de base à la refonte UX « équipements actifs » et de backlog pour les chantiers de fond.

## Rappel du fonctionnement

- **Firmware** : chaque module déclare des `ConfigVariable<T>` (`src/Core/ConfigTypes.h`) pointant directement sur ses membres, enregistrées via `registerVar(var, moduleId, localBranchId)`. Le store copie tout dans une table `_meta` (capacité `Limits::MaxConfigVars` = 768, allouée en PSRAM sur Waveshare), persiste en NVS (`Preferences`, clés ≤ 15 car.) et publie `EventId::ConfigChanged` avec l'identité de branche 8/8 (moduleId/localBranchId) — les modules routent leurs réactions sans comparer de chaînes.
- **Textes/UI** : par module, `text/i18n.fr.json` (source) + `cfgdocs.fr.json` (descripteurs de champs) + `cfgmods.fr.json` (méta d'arbre : `visible_if`, `enum_set`, `hidden`, `compose`, alias, branches virtuelles). À chaque build, `generate_config_docs.py` résout tokens + profil, `generate_cfgdoc_chunks.py` découpe en chunks `data/wc/m{FNV1a32}.j` + bundles `i18n.<locale>.j` + index `i.j`, le tout gzippé en SPIFFS (`prepare_spiffs_data.py`). L'UI (`data/webinterface/app.js`) charge l'index puis les chunks à la demande (`/api/cfgdoc/*`), les valeurs via `/api/flowcfg/module`.

## Faiblesses relevées — cœur ConfigStore

1. **~7 switchs `switch(ConfigType)` dupliqués** sur `void* valuePtr` : `set()` (`ConfigStore.h:242`), `loadPersistentVar` (`h:272`), `loadPersistent` (`cpp:322`), `writePersistent` (`cpp:291`), `toJson` (`cpp:421`), `toJsonModule` (`cpp:502`), persistance d'`applyJson` (`cpp:811`). Toute évolution de type = 7 endroits ; la cohérence `ConfigType` ↔ type C++ n'est pas vérifiée par le compilateur.
2. **Export JSON manuel `snprintf` sans échappement** (`toJson`/`toJsonModule`) : un `char[]` contenant `"` ou `\` (ex. SSID) produit un JSON invalide. Asymétrie export plat vs import imbriqué `{module:{...}}` (`applyJson`).
3. **Pas de min/max déclaratif** : clamps ad hoc dans `applyJson` (bornes de type uniquement, rien sur les floats). Les bornes métier n'existent nulle part.
4. **Contrôle de longueur de clé NVS désactivé** dans `registerVar` (`ConfigStore.h:171-174`) ; la garde repose sur la discipline `NVS_KEY()`.
5. `set()` générique sur `CharArray` force `changed=true` sans comparaison → écritures NVS/événements superflus.
6. Masquage des secrets par `strcmp` en dur (`pass`/`token`/`secret`, `ConfigStore.cpp:394-399`).

## Faiblesses relevées — déclaration PoolLogic

- Chaque variable existe en 4 endroits : membre + initialiseur (`PoolLogicModule.h:306-453`), **69 réassignations** `xxxVar_.moduleName = kCfgModuleXxx;` (`PoolLogicLifecycle.cpp:239-311`, écrasent le `moduleName` du header — double source de vérité) puis **69 `registerVar`** (l.315-393). Simplification la plus rentable : bon `moduleName` dès le header, suppression du bloc de réassignation ; étape suivante : table de descripteurs + boucle.
- **L'ordre des `registerVar` pilote l'ordre d'affichage UI** (tri alphabétique supprimé côté app.js par a2c3f20) : couplage implicite, documenté seulement par commentaire.
- Les 4 variables O2 d'état runtime (`protocol_state`, `last_dose_day`, `weekly_done_ml`, `pending_ml`) sont des vars de config persistantes cachées : présentes dans l'export JSON/MQTT alors que ce sont des états. Décision : laissées en l'état (éviter un breaking change pendant la refonte).

## Faiblesses relevées — manifestes texte / UI

1. **Chemins morts** dans `PoolLogicModule/text/cfgmods.fr.json` : anciens chemins à plat `poollogic/filtr_*` doublonnant `poollogic/filtration/filtr_*` (corrigé par la refonte UX).
2. **`hours_of_day` sur-tokenisé** : 24 entrées `label_t` → 48 entrées i18n (FR+EN) pour « 00:00 »…« 23:00 ».
3. **`poollogic_device_slot` : 8 valeurs déclarées pour 16 PDM** — enum statique incohérent ; sur Waveshare l'UI reconstruit dynamiquement 16 options depuis les noms IO (`app.js` `dynamicPoolLogicDeviceSlotOptions`).
4. **Bundles i18n = charge SPIFFS dominante** : `data/wc/` ≈ 1,1 Mo dont `i18n.fr.j` 172 Ko + `i18n.en.j` 168 Ko ; chaque libellé existe ≥ 3 fois (token, FR, EN).
5. ~~**EN fragile** : `generate_module_i18n_en.py` (glossaire + regex, hors build) produit du franglais ; l'oubli de relance est silencieux.~~ → **corrigé** : script supprimé, catalogues EN devenus sources manuelles, validateur bloquant en pré-build. Voir [i18n-assainissement.md](i18n-assainissement.md).
6. ~~Marqueur `"_meta": {"generated": true}` sur des fichiers en réalité édités à la main~~ (→ **corrigé** en `{"source": "manual"}`, voir [i18n-assainissement.md](i18n-assainissement.md)) ; digest FNV1a des chunks implémenté en double (Python `generate_cfgdoc_chunks.py` / C++ `WebInterfaceServer.cpp:~5425`) sans test de cohérence (divergence = 404 silencieux).
7. Le nœud `poollogic/devices` et ses alias vivent dans `PoolDeviceModule/text/` alors qu'ils s'affichent sous poollogic (responsabilité éclatée, assumée).

## État des lieux équipements (avant refonte UX)

- 16 nœuds `poollogic/devices/pd0..pd15` affichés alors que **seuls pd0-pd7 ont des branches config réelles** (la boucle d'enregistrement saute les slots non `used`, `PoolDeviceLifecycle.cpp:193-253` ; `PoolIds::DeviceCount = 8`, pd8-15 jamais `defineDevice()`).
- Paramètres PDM (`flow_l_h`, `tank_cap_ml`, `tank_init_ml`, `max_uptime_day_s`, `depends_on_mask`) visibles même équipement désactivé ; seuls 3 champs `*_slot` étaient conditionnés à `pdm/pdN/enabled`.
- Activation dispersée : switch `compose` en tête de 7 pages métier + page `pdN`, sans vue de synthèse.
- Mécanismes UI disponibles : `visible_if` (`eq`/`in`, cross-module, fail-open, live si le champ conditionnant est sur la même page), `compose` (champs d'un autre module rendus et appliqués depuis une page métier), alias display↔store, branches virtuelles.

## Refonte UX retenue (implémentée avec cette note)

- Page de synthèse « Équipements » sur le nœud `poollogic/devices` (compose des 8 toggles `pdm/pdN/enabled` ; extension app.js : rendu compose sur nœud sans branche store).
- Sous-pages `poollogic/devices/pdN` supprimées ; les paramètres PDM sont rapatriés dans la page métier qui utilise le PDM (ph←pd1, chlorine←pd2 avec débit/cuve ; filtration/heater/robot/refill/swg avec uptime max).
- Champs des pages métier masqués quand l'équipement est inactif (`visible_if` vers `pdm/pdN/enabled`, support AND ajouté dans app.js pour combiner avec `disinfection_type`) ; les branches restent visibles au menu.
- pd8-15 retirés de l'arbre (alias + libellés).

## Backlog priorisé (chantiers non ouverts)

1. **Quick wins manifestes restants** (~1 h) : `hours_of_day` en labels directs, nettoyage `poollogic_device_slot` statique, marqueur `generated`.
2. **Dé-boilerplate PoolLogic** (~2-3 h) : suppression des 69 réassignations `moduleName`, option table de descripteurs.
3. **Refactor cœur ConfigStore** (½ journée + build complet) : factorisation des 7 switchs, échappement JSON, réactivation du contrôle de clé NVS, fix `set()` CharArray.
4. **Optimisation i18n/SPIFFS** (à chiffrer) : élagage des tokens orphelins, déduplication FR/EN, meilleure génération EN.
