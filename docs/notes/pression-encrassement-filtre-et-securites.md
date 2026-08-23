# Pression : de la « sécurité pression » héritée à l'encrassement de filtre

**État : implémenté et compilé** le 23/08/2026 (profil `Waveshare-ESP32-S3`,
flash 48,1 %, cppcheck à zéro alerte). Reste la **validation sur cible**, dont
les scénarios sont listés en §11. Migration NVS automatique (schéma v3 → v4),
**pas d'effacement complet**.

## Le symptôme

L'alarme de pression basse coupe la filtration. Elle est latchée, donc elle ne
retombe pas seule ; elle est persistée en NVS, donc elle revient après un
redémarrage, filtration coupée d'entrée. Et le mode manuel ne reprend pas la
main : le test est placé **avant** la branche `!autoMode_`.

Le piège est un interlock auto-verrouillant : l'action supprime la grandeur
surveillée.

1. Pression basse → alarme latchée → filtration coupée.
2. Filtration coupée → `condPressureLowStatic_` renvoie `False` (premier test :
   `if (!self->filtrationFsm_.on)`).
3. L'alarme passe en `ClearedUnacked`, donc réinitialisable — mais tant qu'elle
   ne l'est pas, la filtration reste coupée.
4. Après reset, la pompe repart et relatche 62 s plus tard. Boucle sans fin.

La haute pression a exactement la même conséquence, alors qu'elle décrit le cas
« filtre encrassé » : le firmware traite aujourd'hui l'encrassement comme un
arrêt d'urgence.

## Origine des deux seuils

Les deux seuils arrivent au commit `e0f84ff` (« Created PoolLogicModule »,
12/02/2026) sous leurs noms d'alors `psi_low_threshold` / `psi_high_threshold`,
avec la logique encore en place aujourd'hui. C'est du **PoolMaster** repris tel
quel (`PSI_MedThreshold` / `PSI_HighThreshold` → `PSIError` → arrêt pompe).

PoolMaster n'a **pas de flowswitch** : le manomètre y est le seul capteur
hydraulique. D'où les rôles d'origine :

- **`pressure_low` = un détecteur de débit par défaut.** Pompe désamorcée, vanne
  d'aspiration fermée, préfiltre bouché, niveau sous le skimmer. On coupe la
  pompe pour la protéger de la marche à sec.
- **`pressure_high` = sécurité mécanique.** Vanne de refoulement fermée,
  canalisation obstruée, filtre saturé.

Rien n'a changé sur le fond depuis : renommage PSI → pression (`566b5a7`,
`47d8243`), passage au service d'alarmes centralisé, et délai de démarrage
abaissé de 180 s à 60 s.

**`pressure_low` n'a donc jamais été un indicateur d'encrassement.** Sur une
installation équipée d'un flowswitch, il fait doublon avec un capteur direct,
là où le manomètre ne fait que deviner. Le réaffecter est légitime.

## 1. Ce que le manomètre doit mesurer

La pression de filtre est une grandeur **relative**. La référence est la
pression de service filtre propre, relevée après un lavage ; on lave à environ
+0,3 à +0,5 bar au-dessus. Un seuil absolu n'est pas transposable d'une
installation à l'autre : certaines tournent à 0,5 bar, d'autres à 1,2.

La pompe est tout-ou-rien (aucune notion de vitesse variable dans le code) :
la pression de référence est donc une valeur unique, pas une valeur par vitesse.

## 2. Réglages

| Clé config | Clé NVS | Rôle | Défaut |
|---|---|---|---|
| `pressure_ref` (ex-`pressure_low_th`) | `pl_prlow` (réutilisée) | Pression de service filtre propre. **Ne déclenche rien.** | `0` = non calibrée |
| `pressure_fouling_dlt` | `pl_prdlt` (nouvelle) | Écart d'alerte de lavage | `0,40` bar |
| `pressure_high_th` | `pl_prhigh` (inchangée) | Sécurité mécanique, coupe la pompe | `1,80` bar |
| `pressure_start_dly_s` | `pl_prdelay` (inchangée) | Délai de démarrage (apprentissage + trip) | `60` s |

## 3. Arbitrages retenus

Trois points tranchés le 23/08/2026, dont deux s'écartent de la recommandation
initiale — les conséquences sont documentées ci-dessous, pas contournées.

- **Manque de débit → coupure immédiate de la pompe.** Flowswitch à « pas de
  débit » alors que la pompe tourne depuis plus de `pressure_start_dly_s` :
  arrêt et latch, sans cycle de tentatives.
- **Référence de pression : apprise ET modifiable à la main.**
- **Seuil de coupure haute pression maintenu à 1,80 bar** (et non relevé à 2,50).

### Conséquence du maintien à 1,80 bar

L'alerte d'encrassement se déclenche à `pressure_ref + pressure_fouling_dlt`.
Avec une référence apprise à 1,0 bar et un delta de 0,4, l'alerte tombe à
1,4 bar, sous le trip : correct.

Mais sur une installation à forte pression de service — référence apprise à
1,5 bar, cas courant sur un filtre à cartouche ou une tuyauterie longue —
l'alerte tomberait à 1,9 bar, soit **au-dessus du trip à 1,80** : l'arrêt
d'urgence sonnerait avant l'invitation à laver le filtre. Exactement le
comportement qu'on cherche à supprimer.

**Garde-fou obligatoire** : si `pressure_ref + delta >= pressure_high_th − 0,15`,
le seuil d'alerte est calé à `pressure_high_th − 0,15` et un `LOGW` est émis avec
les trois valeurs. L'utilisateur garde son trip à 1,80 ; l'alerte reste
utilisable, mais sa marge se réduit. C'est le compromis, il faut le savoir.

À surveiller après mise en service : si la référence apprise dépasse 1,3 bar,
relever `pressure_high_th` devient nécessaire pour que l'alerte serve à quelque
chose.

### Apprentissage de la référence

`pressure_ref = 0` signifie « non calibrée ». Au premier créneau de marche
stable — filtration ON depuis 10 min, débit confirmé par le flowswitch, mesure
stable — le firmware capture la moyenne et l'écrit.

Une commande `poollogic.filter_washed` remet la référence à `0` : réapprentissage
au prochain démarrage. Un bouton « Filtre lavé » dans l'UI, et rien à saisir.

Le champ reste **éditable à la main** dans la config : une valeur saisie non
nulle est respectée telle quelle et n'est jamais écrasée par l'apprentissage.

Limite connue : une référence apprise sur un filtre déjà sale sera trop haute,
et l'alerte sonnera trop tard. Cela se corrige au premier lavage suivi d'un appui
sur « Filtre lavé ».

## 4. Table de décision

| Événement | Condition | Sévérité | Latch | Action |
|---|---|---|---|---|
| **Filtre à laver** | `P > ref + delta` pendant 5 min | Warning | non | **aucune** — notification seule |
| **Surpression mécanique** | `P > pressure_high_th` | Critical | **oui** | coupe pompe + chauffage + traitements |
| **Capteur pression suspect** | débit confirmé et `P < 0,05` bar | Warning | non | aucune |
| **Manque de débit** | flowswitch OFF, pompe ON depuis > `pressure_start_dly_s` | Alarm | **oui** | coupe **pompe** + chauffage + traitements |

L'alarme « capteur suspect » n'est possible que parce qu'un flowswitch est
présent : quand il annonce du débit et que le manomètre lit 0,00 bar, c'est le
manomètre qui ment. Sans ce croisement, « pompe désamorcée » et « capteur mort »
sont indiscernables — c'est la cause du faux positif d'origine.

Le trip haute pression garde son latch : une vanne fermée ne se rouvre pas seule.
Garde-fou contre la boucle : trois re-trips consécutifs après réarmement →
verrouillage franc avec message explicite, au lieu d'un cycle infini.

## 5. Interlocks : qui coupe quoi

Deux trous existaient et ont été comblés dans le même chantier, sans quoi la
refonte aurait fait **perdre** de la sécurité :

- **`flow_interlock` ne coupait pas la pompe** — `PoolLogicControl.cpp` ne coupait
  que `phPumpDesired`, `orpPumpDesired`, `swgDesired`. Retirer la coupure de
  `pressure_low` sans rien ajouter aurait supprimé toute protection
  anti-marche-à-sec.
- **`flow_interlock` ne coupait pas le chauffage** — seul `pressureError_` le
  faisait. Un réchauffeur alimenté sans débit est le cas le plus dangereux du lot,
  et il n'était couvert que par ricochet.

Côté code : casser `pressureError_` en `pressureTrip_` (haute pression seule),
supprimer le chemin « pression basse coupe », et ajouter `noFlowError_` aux
causes qui coupent le chauffage et la pompe.

## 6. Alarmes

`MaxAlarms = 16`, 11 utilisées, 5 libres. Réaffectation plutôt que rupture :

| Id | Avant | Après |
|---|---|---|
| `1000` | `PoolPressureLow` | **`PoolPressureSensorFault`** — « pression anormalement basse » reste sémantiquement continu, l'entité HA existante garde du sens |
| `1001` | `PoolPressureHigh` | inchangé, rôle trip mécanique |
| `1009` | — | **`PoolFilterFouling`** (nouveau) |
| `1010` | — | **`PoolNoFlow`** (nouveau, latché) |

Les `static_assert` d'`AlarmModule.cpp` sont à mettre à jour avec un commentaire
expliquant la réaffectation de `1000`.

## 7. Migration NVS (v3 → v4)

`pl_prlow` est réutilisée telle quelle (même type `float`) mais **doit être
écrasée** : une valeur 0,15 héritée serait une référence de service absurde et
désactiverait silencieusement l'alerte d'encrassement.

`mig_3_to_4` :

- `pl_prlow` → forcée à `0` (non calibrée, déclenche l'apprentissage) ;
- `pl_prdlt` → créée à `0,40` ;
- `pl_prhigh` → conservée telle quelle (arbitrage §3).

Modèle : `mig_2_to_3` dans `src/Core/ConfigMigrations.h`.

## 8. Recopies en dur à mettre à jour

Le dépôt duplique la liste d'alarmes à plusieurs endroits, sans garde-fou de
compilation entre eux :

- `AlarmModule.cpp` — table `kRuntimeUiAlarms` (ids Runtime UI 11..19, prochains
  libres : 20, 21) ;
- `WebInterfaceServer.cpp` — table équivalente, plus le `switch` en dur de
  `/api/runtime/values` (voir [runtime-ui-double-chemin-waveshare.md](runtime-ui-double-chemin-waveshare.md) :
  une valeur ajoutée au manifeste sans `case` répond « indisponible » en silence) ;
- `HMIModule.cpp` — libellés et mapping ;
- `TFTModuleS3.cpp` — liste d'ids et libellés ;
- `app.js` — tables recopiant les enums firmware ;
- les cinq fichiers `text/` de `PoolLogicModule` et d'`AlarmModule`.

## 9. Capacités

- **Alarmes** : 11 / 16 → 13 / 16 après ce chantier. Marge suffisante, mais elle
  se referme.
- **`binary_sensor` HA** (une par alarme) : 11 / 24 sur Waveshare. Confortable.
- **`sensor` HA** : ≈ 45 / 48. Un `filter_fouling_pct` publié en Runtime UI et
  exposé à HA consommerait un des trois derniers slots, avec troncature muette
  au-delà (`addSensorEntry` renvoie `false` sans log). À compter avant, pas après
  — voir [entites-ha-brutes-et-metier.md](entites-ha-brutes-et-metier.md).

## 10. Contournement disponible avant implémentation

- Activer `flow_interlock` (Piscine → Sécurité → « Sécurité manque de débit ») :
  le flowswitch câblé ne protège rien tant que ce réglage est inactif.
- Mettre `pressure_low_th` à `-1` pour neutraliser l'alarme fantôme. Aucun clamp
  n'existe sur ce champ, la valeur est prise telle quelle. `-1` plutôt que `0`
  pour absorber un offset de calibration négatif.

## 11. Ce qui a été implémenté, et ce qui reste à vérifier

### Écarts par rapport à la proposition initiale

- **Le seuil de coupure reste à 1,80 bar** (§3). Le garde-fou de bornage est donc
  structurellement sollicité dès que la pression de service dépasse ~1,3 bar.
- **Deux valeurs Runtime UI** au lieu d'une : `pool.filter_fouling_pct` (17) et
  `pool.pressure_ref` (18). La seconde ne coûte rien et évite d'avoir à ouvrir la
  page de configuration pour savoir sur quelle référence l'alerte se cale.
- **`generate_datamodel.py` acceptait un seul marqueur `MODULE_DATA_MODEL` par
  fichier** : la boucle s'arrêtait au premier. Rien ne l'imposait dans le format
  (l'agrégateur gère déjà une liste et détecte les doublons), donc le script lit
  désormais tous les marqueurs. Fusionner pression et dosage pH dans une seule
  struct aurait été le contournement, au prix de mélanger deux sujets.
- **Débordement des tables HA rendu visible.** `addSensorEntry` et
  `addBinarySensorEntry` renvoyaient `false` sans un mot, alors que les tables
  number/button/select loguaient déjà leur saturation. Ce sont précisément les
  deux qui approchent leur plafond ; le `LOGW` manquant a été ajouté.

### Fichiers touchés

Firmware : `AlarmIds.h`, `DataKeys.h`, `NvsKeys.h`, `SystemLimits.h` (inchangé),
`DomainTypes.h`, `PoolDefaults.h`, `ConfigMigrations.h`, les quatre fichiers de
`PoolLogicModule/`, `AlarmModule.cpp/.h`, `HAModule.cpp`, `WebInterfaceServer.cpp`,
`HMIModule.cpp`, `TFTModuleS3.cpp`.

Textes : `runtimeui.json` et `i18n.*` d'`AlarmModule` et `PoolLogicModule`,
`cfgdocs`/`cfgmods`/`i18n` de `PoolDeviceModule` (les seuils de pression y vivent,
sous l'alias `pdm/pd0`), `cfgmods.fr.json` de `PoolLogicModule` pour la section
composée `piscine/filtration`.

### Scénarios de validation sur cible

1. **Non-régression du symptôme** — pompe en marche, capteur débranché : la
   filtration doit **continuer**, `pressure_sensor_fault` apparaître, aucun trip.
2. **Apprentissage** — `pressure_ref` à 0, laisser tourner 11 min : la référence
   se fige sur la pression réelle et survit à un reboot.
3. **Saisie manuelle** — écrire une valeur non nulle : elle ne doit pas être
   écrasée au passage suivant.
4. **Encrassement** — abaisser `pressure_fouling_dlt` pour franchir le seuil :
   alerte après 5 min, **aucune coupure**, `filter_fouling_pct` cohérent dans
   l'interface web et dans Home Assistant.
5. **Bornage** — forcer `pressure_ref` à 1,7 bar : seuil borné à 1,65 et `LOGW`
   « Fouling threshold clamped ».
6. **Trip haute pression** — abaisser `pressure_high_th` : coupure, latch, reset
   possible une fois la pompe arrêtée. Trois re-trips → verrouillage franc.
7. **Manque de débit** — `flow_interlock` activé, flowswitch débranché : rien
   pendant 60 s, puis coupure, latch, chauffage bloqué (`HYDRAULIC_BLOCKED`).
8. **Migration** — partir d'une NVS v3 avec `pl_prlow = 0,15` : après flash,
   `pressure_ref` vaut 0 et l'apprentissage démarre.
9. **Comptage des entités HA** — relever au boot qu'aucun `LOGW ... table full`
   n'apparaît. C'est le point serré : ≈ 45 sensors sur 48 avant ce chantier, un
   de plus maintenant.
