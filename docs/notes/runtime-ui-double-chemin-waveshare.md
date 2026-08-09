# Note de travail — Runtime UI : deux chemins de lecture, dont un en dur sur Waveshare

> Statut : **corrigé pour le domaine `ph`** ; le piège de fond reste entier.
> Date : 2026-08-09
> Contexte : l'ajout de valeurs de suivi du dosage pH n'affichait rien dans l'UI web,
> et le tableau de bord montrait « pH auto : Arrêt » alors que le mode était actif.
> Deux bugs distincts, tous deux nés de la même zone.

## Le piège : `writeRuntimeUiValue` ne sert pas le web sur Waveshare

Il existe **deux implémentations** de la lecture des valeurs Runtime UI :

| Chemin | Qui | Pour quoi |
|---|---|---|
| `Module::writeRuntimeUiValue(valueId, writer)` | chaque module porteur | MQTT, écran, profils Supervisor/FlowIO (I2C) |
| `appendWaveshareLocalRuntimeValue_()` — un `switch (id)` en dur dans [WebInterfaceServer.cpp](../../src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp) | le serveur web | **`/api/runtime/values` sur le profil Waveshare** |

Le second lit **directement le DataStore et le ConfigStore** : il n'appelle jamais le
provider du module. Conséquence, contre-intuitive et sans le moindre message d'erreur :

> **Ajouter une valeur dans `runtimeui.json` et dans `writeRuntimeUiValue` ne suffit
> pas à la faire apparaître dans l'interface web.** Il faut en plus un `case` dans le
> switch Waveshare, et une source lisible depuis le DataStore ou le ConfigStore.

Une valeur déclarée au manifeste mais absente du switch tombe dans le `default`, qui
répond `status: "unavailable"`. Le manifeste la décrit (libellé, unité, décimales),
l'UI la demande, le firmware répond « indisponible » — et rien ne le signale.

### Ce que ça avait déjà cassé

Le domaine `ph` comptait trois valeurs (`pool.ph_dose_phase`, `pool.ph_dose_day_ml`,
`pool.ph_gain`) déclarées au manifeste et implémentées dans `PoolLogicRuntime.cpp`.
Aucune n'était câblée dans le switch : **elles n'avaient jamais rien affiché**, sur
aucun écran de l'interface web, depuis leur création.

### État après correction

Le switch couvre désormais 60 des 63 entrées du manifeste. Restent orphelines :

- `powermon.temperature` (2218), `powermon.energy` (2219), `powermon.charge` (2220)
  — domaine `sondes`, affichées « Indisponible » dans le tableau de bord.

Asymétrie inverse, sans conséquence mais à connaître : le switch sert 4 ids absents du
manifeste principal (1004, 1301-1303) ; l'UI ne les demande jamais, puisqu'elle ne
connaît que le manifeste.

### Vérification rapide

Comparer les `case` du switch aux entrées du manifeste embarqué :

```bash
python3 -c "import re,json; src=open('src/Modules/Network/WebInterfaceModule/WebInterfaceServer.cpp',encoding='utf-8').read(); body=src[src.index('bool appendWaveshareLocalRuntimeValue_'):src.index('void sendWaveshareLocalRuntimeValuesResponse_')]; cases=set(int(m) for m in re.findall(r'case (\d+):', body)); man=json.loads(re.search(r'R\"RUI\((.*?)\)RUI\"', open('src/Core/Generated/RuntimeUiManifestJson_Generated.h',encoding='utf-8').read(), re.S).group(1)); print(sorted((v['id'], v['key']) for v in man['values'] if v['id'] not in cases))"
```

## Le chemin de données : PoolLogic n'avait pas de DataModel

Le switch ne sachant lire que le DataStore et le ConfigStore, une valeur qui vit dans
un module doit y être publiée. `PoolLogicModule` n'avait **aucun** `*ModuleDataModel.h` :
l'état de la FSM de dosage n'existait que dans ses membres privés et dans le snapshot
MQTT `rt/poollogic/ph`.

Ajouté :

- [PoolLogicModuleDataModel.h](../../src/Modules/PoolLogicModule/PoolLogicModuleDataModel.h) —
  `PoolLogicPhDosingRuntimeData` : phase, cause d'inaction, attente de mélange, lot
  visé, lot injecté, effet attendu, cumul du jour, gain effectif. 32 octets.
- [PoolLogicRuntime.h](../../src/Modules/PoolLogicModule/PoolLogicRuntime.h) —
  accesseurs, `DataKeys::PoolPhDosing = 13` (plage 13-39 qui était libre).
- `PoolLogicModule` dépend maintenant de `ModuleId::DataStore` (7 → 8 dépendances) et
  republie à chaque pas de FSM ainsi qu'au reset.
- `printRuntimeEnum_` dans le serveur : ce chemin n'émettait pas le type `enum`, sans
  lequel une phase reste un nombre nu à l'écran.

Deux valeurs sont publiées « indisponibles » plutôt qu'à zéro quand elles n'ont pas de
sens — l'attente hors phase de mélange, l'effet attendu sans lot décidé. L'UI omet
alors la ligne au lieu d'afficher « Indisponible ».

Le drapeau `valid` reflète `DosingState::tickValid` : tant que la FSM n'a pas tourné,
tout le bloc est indisponible. Une tuile pH qui montre ses réglages sans ses métriques
signale donc que PoolLogic n'est pas actif, pas un défaut d'affichage.

## Le bug jumeau : une branche de config sérialisée dans un buffer trop petit

`waveshareLoadPoolModeFlags_` sérialisait **toute la branche `poollogic/ph` dans 320
octets** pour en extraire un seul booléen. La branche compte 17 champs (~430 octets) :

1. `toJsonModule` remplit, tronque, positionne `truncated`… et **retourne `true`**
   (sa valeur de retour est `any`, « au moins un champ écrit »).
2. L'appelant ignorait `truncated`, donc parsait un JSON incomplet.
3. `deserializeJson` échouait, et `ph_auto_mode` restait à son défaut `false`.

Résultat : **« pH auto : Arrêt » alors que le mode était actif**. Le
`StaticJsonDocument<128>` était de toute façon trop petit pour 17 champs.
`dis_auto_mode` (branche `disinfection`, 21 champs) était cassé de la même façon.

Corrigé : buffer 768, un seul document réutilisé pour les trois branches (moins de pile
qu'avec trois documents), troncature traitée comme un échec avec `LOGW`, et surtout un
**drapeau de disponibilité par mode** — une lecture qui échoue affiche « Indisponible »
au lieu d'un faux « Arrêt ». C'est ce silence qui avait rendu le bug invisible.

Le même correctif couvre `/api/flow/status?domain=pool` (champs `pha` / `ora`).

**À surveiller** : le dimensionnement reste implicite. Ajouter des champs à
`poollogic/ph` ou `poollogic/disinfection` rapprochera du plafond de 768 octets. Le
`LOGW` est le seul garde-fou ; il n'y a pas de `static_assert` possible ici, la taille
sérialisée n'étant pas connue à la compilation.

## À retenir

- Sur Waveshare, une valeur Runtime UI a besoin de **trois** choses, pas d'une :
  le descripteur `runtimeui.json`, une source dans le DataStore/ConfigStore, et un
  `case` dans `appendWaveshareLocalRuntimeValue_`.
- Le mode d'échec par défaut de cette zone est le **silence** : `unavailable` côté
  serveur, valeur par défaut côté lecture de config. Préférer partout une
  indisponibilité explicite à une valeur plausible mais fausse.
- Côté UI web, `registerRuntimeManifestEntry` **jette** toute entrée dont le domaine
  n'est pas dans la liste blanche `runtimeManifestDomainKeys()`. Un nouveau domaine
  runtime consommé par une carte doit y être déclaré, sinon le manifeste est chargé
  et ses entrées perdues sans trace.
