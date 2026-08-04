# Régulation pH par pompe péristaltique — état de l'art et refonte proposée

**Statut : implémenté le 2026-08-04** sur la branche `ph-dosage-volumetrique`. Rédigé le
2026-07-25 à partir des logs du 25/07 (blocage `max_uptime` de la pompe pH, slot device 1 /
`pd1`) et de la relecture de `stepTemporalPid_` / `applyDeviceControl_`.

Les sections 1 à 4 restent le constat d'origine, conservé tel quel pour mémoire. La §5 décrit la
refonte telle qu'elle a été livrée ; les écarts par rapport à la proposition initiale sont
regroupés en §5.8.

**Installation de référence** : pompe doseuse pH **1,8 L/h** (= 30 mL/min = 0,5 mL/s).
⚠️ Le défaut d'usine du firmware est `PoolDefaults::PeristalticFlowLPerHour = 1.2f`
([PoolDefaults.h:83](../../src/Domain/Pool/PoolDefaults.h)) : vérifier que la clé de config du
slot (`pd1flh`) porte bien **1,8**, sinon toute la comptabilité volumétrique (`injectedMl*`,
niveau de bidon) est fausse de 50 %.

---

## 1. Nature du procédé

Quatre propriétés interdisent de traiter le pH comme une boucle PID classique :

| Propriété | Ordre de grandeur | Conséquence |
|---|---|---|
| Retard pur (recirculation) | 1 turnover = 3 à 8 h | Une dose n'est visible par la sonde qu'après un brassage complet |
| Constante de temps | heures | Le système ne répond jamais dans la fenêtre de décision |
| Dérive naturelle | +0,05 à +0,2 pH/jour | Perturbation lente quasi constante (dégazage CO₂, électrolyse au sel, baigneurs) |
| Actionneur | tout-ou-rien, débit fixe | On ne module que le **temps**, donc le **volume** |

Le pouvoir tampon est porté par le **TAC (alcalinité)**, pas par le pH. Ordre de grandeur
usuel : **≈ 10 mL d'acide chlorhydrique 33 % par m³ pour −0,1 pH à TAC ≈ 100 mg/L**
(× 0,5 à × 2 selon TAC et concentration du produit). Pour 50 m³ : **≈ 500 mL par 0,1 pH**.

Contraintes de mesure : validité seulement en eau circulante depuis quelques minutes, bruit
de ±0,02 à 0,05 pH, dérive lente, étalonnage 2 points périodique.

## 2. Les trois familles d'algorithmes du marché

**a) ON/OFF à hystérésis + temporisations** — entrée de gamme. Dose tant que
`pH > consigne + hyst`, avec durée d'injection max et temps d'attente imposé entre deux
injections. Le temps d'attente est en réalité un substitut au temps de mélange.

**b) Proportionnel par fenêtre (PWM lent, « dosage proportionnel »)** — milieu de gamme
(Bayrol, Zodiac, Seko, Emec, Pool Technologie…). Durée ON proportionnelle à l'écart sur une
fenêtre de 15 min à 1 h. **C'est ce que fait flow.io aujourd'hui.** Correct *si et seulement
si* la fenêtre est du même ordre que le temps de recirculation.

**c) Dosage volumétrique par lots avec temps de mélange (batch + rest)** — haut de gamme,
et pratique standard pour tout procédé à grand retard. On calcule un **volume** à partir du
volume d'eau et de l'écart, on l'injecte, puis on **attend un temps de recirculation avant de
re-mesurer**. Rien n'est décidé pendant l'attente. Les versions évoluées mesurent l'effet
réellement obtenu et recalent leur gain chimique seules.

Le terme intégral n'existe quasiment jamais dans ces produits : la dose *est* l'intégrale, et
l'attente de mélange remplace l'anti-windup.

## 3. Sécurités considérées comme obligatoires dans le métier

1. **Overfeed / dosing timeout** — durée d'injection continue max et volume max par jour → arrêt + alarme.
2. **Deadband** autour de la consigne (±0,05 à ±0,1) — ne jamais doser sur le bruit de sonde.
3. **Plage de validité de la mesure** (hors 6,0–8,5 = sonde suspecte) → dosage interdit, jamais maximal.
4. **Interlock débit** — filtration ON depuis N minutes, idéalement contrôleur de débit.
5. **Détection « dose sans effet »** — après N lots sans variation : bidon vide, tuyau percé, sonde figée.
6. **Niveau de bidon** + comptage volumétrique du consommé.
7. **Non-simultanéité acide / chlore liquide** — mélange = dégagement de chlore gazeux.
8. **Calibration du débit réel de la pompe** — le débit nominal dérive avec l'usure du tuyau.

## 4. Constat chiffré sur l'implémentation actuelle

Défauts en vigueur : `PhKp = 2 000 000` ms/pH, `Ki = Kd = 0`, fenêtre **1 h**, `minOn` 30 s,
échantillonnage 30 s, quota `DosePumpMaxUptimeDaySec` = **30 min/jour**, volume bassin 50 m³,
**pompe 1,8 L/h**.

| Écart pH | Durée ON commandée / fenêtre | Volume injecté / h |
|---|---:|---:|
| 0,05 | 100 s | 50 mL |
| 0,1 | 200 s | 100 mL |
| 0,2 | 400 s | 200 mL |
| 0,3 | 600 s | 300 mL |
| ≥ 1,8 | 3600 s (saturé) | 1800 mL |

`minOn` = 30 s correspond à **15 mL**, soit un écart de 0,015 pH : le bruit de sonde suffit à
déclencher une injection.

### 4.1 Le Kp par défaut équivaut à une dose volumétrique — mais non reliée au bassin

`Kp = 2e6 ms/pH` × 0,5 mL/s = **1000 mL par unité pH et par fenêtre**, soit **100 mL par
0,1 pH et par heure**. La dose théorique pour 50 m³ étant de 500 mL par 0,1 pH, il faut
**5 fenêtres = 5 h** pour délivrer la dose juste — ce qui correspond au turnover d'un bassin
de 50 m³ à 10 m³/h. Le réglage par défaut est donc *accidentellement* cohérent, pour un seul
jeu de valeurs (50 m³, 10 m³/h, 1,8 L/h, TAC 100).

**Rien dans le firmware ne relie `Kp` au volume du bassin, au débit de filtration ni au débit
de la pompe doseuse** — alors que ces trois grandeurs y sont déjà configurées
(`pool_volume_m3`, `pump_flow_m3h`, `pd1flh`). Changer de bassin ou de pompe casse
silencieusement le réglage.

### 4.2 Le quota journalier est devenu la limite de fonctionnement

30 min/j × 1,8 L/h = **900 mL/jour**, soit **≈ 0,18 pH de correction maximale par jour** pour
50 m³ — à peine au-dessus de la dérive naturelle (0,05 à 0,2 pH/jour). Conséquences :

- écart soutenu de 0,1 pH → quota épuisé en 9 h de filtration ;
- écart de 0,2 pH → 4,5 h ; écart de 0,3 pH → **3 h** ;
- rattraper un écart de 0,3 pH demande 1500 mL = **50 min de pompe**, ce que le quota interdit
  en une seule journée.

C'est exactement le `dayS=1800 maxS=1800` observé le 25/07 : **comportement nominal d'un
dimensionnement trop juste, pas une anomalie**.

> **Confirmé le 26/07** : uptime pH relevé à 0 juste après minuit. Le rollover
> (`reconcilePeriodCountersFromClock_` + levée automatique du latch dans `tickDevices_`)
> fonctionne, et la persistance du compteur à travers un reboot **dans la même journée** est le
> comportement voulu du blob NVS `pd1rt`. Il n'y a donc pas de bug de comptage : le quota est
> légitimement consommé chaque jour, ce qui est précisément le problème traité par cette note.
> Reste séparément en cause le *reset manuel*, qui n'existe qu'en MQTT (bouton HA), sans
> équivalent dans l'UI web.

### 4.3 Cinq défauts de conception

1. **Fenêtre (1 h) ≪ temps de recirculation (≈ 5 h)** : le régulateur redose 5 fois avant de
   voir l'effet de la première dose. Structurellement surdoseur — masqué ici par un `Kp`
   faible qui, lui, sous-dose. Deux erreurs qui se compensent tant que les paramètres ne bougent pas.
2. **Aucun deadband** : dosage sur le bruit de sonde (cf. 4).
3. **Cible recalculée en cours de fenêtre**
   ([PoolLogicControl.cpp:878-916](../../src/Modules/PoolLogicModule/PoolLogicControl.cpp)) :
   `demandOn = elapsed < outputOnMs` avec `outputOnMs` réévalué toutes les 30 s → la pompe peut
   se rallumer en milieu de fenêtre après s'être arrêtée.
4. **Boucle ouverte totale** : le retour de `writeDeviceDesired_` est ignoré
   ([PoolLogicControl.cpp:940](../../src/Modules/PoolLogicModule/PoolLogicControl.cpp)), donc la
   fenêtre ON s'écoule même pompe bloquée (7 min 34 le 25/07 à 20:51), et le retry 5 s
   ([PoolLogicControl.cpp:937](../../src/Modules/PoolLogicModule/PoolLogicControl.cpp)) produit
   2 lignes `W` par tour sur une cause qui dure jusqu'à minuit (≈ 184 lignes ce soir-là).
5. **Aucune détection « j'ai dosé, rien n'a bougé »** : bidon vide, tuyau percé ou sonde figée
   sont indiscernables d'un fonctionnement normal.

Défauts déjà répertoriés par ailleurs et cohérents avec ce constat : F5 (reset de l'intégrale
au croisement de consigne, pas d'anti-windup par clamp) dans
[rapport-analyse-domaine-piscine.md](../rapport-analyse-domaine-piscine.md).

### 4.4 Dimensionnement matériel

1,8 L/h pour 50 m³ reste modeste (usuel : 1,5 à 5 L/h). Le débit conditionne la granularité
minimale de dose (`minOn` × débit) autant que la capacité de rattrapage.

---

## 5. Refonte proposée : dosage volumétrique par lots à gain auto-calibré

### 5.1 Machine à états

```
        ┌──────────────────────────────────────────────────┐
        ↓                                                  │
    [Idle] ──filtration stable N min──→ [Measure]          │
                                            │              │
                        écart ≤ deadband ───┘              │
                        écart > deadband                   │
                                            ↓              │
                                        [Dosing] ──dose délivrée──→ [Mixing]
                                            │                           │
                                  refus device / bidon vide             │ attente = 1 turnover
                                            ↓                           ↓
                                        [Blocked] ←──pas d'effet──── [Evaluate] ──→ recale le gain
```

- **Measure** : exige une mesure fraîche (`ph_sample_max_age_s`), dans la plage de validité,
  filtration ON depuis `DelayPidsMin`.
- **Dosing** : injecte une dose **en millilitres**, pas en secondes. `PoolDeviceModule` sait
  déjà convertir (`flowLPerHour` → `injectedMlDay`) : on arrête le lot au mL consommé.
- **Mixing** : rien ne se décide. Durée = `max(ph_mix_wait_min, turnover calculé)`.
  **C'est le cœur de la correction.**
- **Evaluate** : compare le ΔpH réellement obtenu à la dose injectée → recale le gain et
  détecte l'absence d'effet.

### 5.2 Calcul de la dose

```
dose_theorique_ml = gain_ml_par_m3_par_01pH × volume_m3 × (|erreur| / 0,1)
dose_lot_ml       = min(dose_theorique_ml × facteur_prudence,
                        ph_dose_max_ml_batch,
                        quota_jour_restant_ml,
                        bidon_restant_ml)
```

`facteur_prudence` ≈ 0,5 : viser la moitié de l'écart par lot. Avec l'attente de mélange, la
convergence est géométrique et sans dépassement — comportement qu'un intégrateur ne sait pas
produire sur un procédé à retard pur.

### 5.3 Auto-calibration du gain

Après chaque cycle complet :

```
gain_observé = dose_injectée_ml / (Δph_mesuré × volume_m3 / 0,1)
```

intégré en moyenne glissante bornée (±50 % du gain configuré, 8 échantillons). L'utilisateur
n'a jamais à connaître son TAC ni la concentration de son produit : le régulateur l'apprend.
Si `|Δph| < ph_no_effect_threshold` sur `ph_no_effect_batches` lots consécutifs → alarme
dédiée « dosage sans effet » + passage en `Blocked`.

### 5.4 Paramètres de configuration

| Clé | Défaut | Rôle |
|---|---|---|
| `ph_algo` | 1 (lots) | 0 = proportionnel legacy, 1 = lots volumétriques |
| `ph_dose_ml_per_m3` | 10 | mL de produit par m³ pour 0,1 pH — auto-calibré |
| `ph_deadband` | 0,05 | Zone morte autour de la consigne |
| `ph_mix_wait_min` | 0 (= turnover auto) | Attente de mélange ; 0 = calculée depuis volume / débit filtration |
| `ph_dose_max_ml_batch` | 250 | Plafond par lot |
| `ph_dose_max_ml_day` | 1500 | **Sécurité** volumétrique, au-dessus du besoin théorique |
| `ph_valid_min` / `ph_valid_max` | 6,0 / 8,5 | Hors plage → dosage interdit |
| `ph_sample_max_age_s` | 300 | Fraîcheur de mesure exigée |
| `ph_no_effect_batches` | 3 | Seuil d'alarme « dosage sans effet » |

`Kp/Ki/Kd` restent en place, masqués derrière `ph_algo == 0` via `visible_if` (hidden/visible_if
sont par champ, pas par page) → **pas de migration NVS**, conformément à la contrainte de la
branche `io-refonte-unifiee`.

### 5.5 Intégration dans l'architecture

- **Helper pur** `src/Modules/PoolLogicModule/DosingController.{h,cpp}`, sur le modèle de
  `FiltrationWindow.{h,cpp}` : entrées = struct (mesures, état, quotas, flags interlock, nowMs),
  sortie = `{pumpOn, phase, blockReason, doseTargetMl, doseDeliveredMl}`. Zéro dépendance ESP →
  **testable en natif**, ce qui rejoint le point P1 du
  [rapport d'analyse domaine](../rapport-analyse-domaine-piscine.md) (l'env `native` est absent
  de `platformio.ini`, à créer).
- **Consommer le retour de `writeDeviceDesired_`** : sur refus, la FSM passe en `Blocked` avec la
  raison de `meta.blockReason`, la dose en cours est **suspendue et non perdue**, et le log de
  blocage est émis une fois puis dédupliqué tant que la raison ne change pas.
- **Quota device** : `maxUptimeDaySec` redevient une sécurité de dernier recours ; la limite
  métier passe à `ph_dose_max_ml_day`, dans l'unité que l'utilisateur comprend. Relever au
  passage `DosePumpMaxUptimeDaySec` (30 min), qui plafonne aujourd'hui la régulation avant toute
  sécurité (cf. 4.2).
- **Non-simultanéité acide / chlore liquide** : arbitrage dans `runControlLoop_`, priorité au pH
  (dont dépend l'efficacité du chlore).
- **Débit pompe** : exposer `flowLPerHour` dans `PoolDeviceSvcMeta` (point P2.4 du rapport
  domaine) plutôt que de le relire en JSON.

### 5.6 Plan par étapes — état de réalisation

1. ✅ **Garde-fous du chemin actionneur** — `applyDeviceControl_` renvoie un `DeviceWriteResult`
   et ne latche `lastDesired` que sur succès ; le log de blocage est dédupliqué par raison, répété
   au plus toutes les 30 min, avec une ligne `recovered` au retour à la normale (l'incident du
   25/07 passe de ~184 lignes à ~7). `stepTemporalPid_` fige `outputOnMs` sur la fenêtre en cours
   (bénéficie à l'ORP, seul utilisateur restant).
2. ✅ **`DosingController` pur + env `native` + 36 tests Unity** — `[env:native]` a nécessité de
   renommer `[env]` en `[esp32_base]` : une section `[env]` est héritée par *tous* les envs, et un
   `board =` vide reste « défini » pour PlatformIO. Débloque au passage
   `test_poollogic_filtration_window`, qui n'avait jamais pu s'exécuter.
3. ✅ **Câblage dans `PoolLogicModule`** — sans `ph_algo` : voir §5.8.
4. ✅ **Auto-calibration du gain + alarme « dosage sans effet »** (`AlarmId::PoolPhDoseNoEffect` = 1007).
5. ⬜ **Extension à la désinfection liquide** — non fait. Le helper est déjà agnostique de la
   grandeur régulée (`unitStep`, `batchStartValue`, `error`) ; il reste à instancier un second
   `DosingState`, exposer un `dis_algo` et transformer l'arbitrage de non-simultanéité en
   suspension de lot des deux côtés.

### 5.7 Vérifications préalables sur l'installation

- [ ] `pd1flh` (débit pompe pH) = **1,8** L/h et non 1,2.
- [ ] `pool_volume_m3` conforme au bassin réel (sert au turnover *et* au dosage).
- [ ] `pump_flow_m3h` conforme à la pompe de filtration réelle.
- [ ] TAC mesuré une fois, pour valider l'ordre de grandeur du gain initial.

### 5.8 Écarts par rapport à la proposition initiale

**L'algorithme historique n'a pas été conservé.** La §5.4 prévoyait un `ph_algo` avec le PID masqué
derrière `visible_if`, pour éviter une migration NVS. Décision inverse à l'implémentation : les clés
`ph_kp`, `ph_ki`, `ph_kd`, `ph_window_ms`, `ph_min_on_ms`, `ph_sample_ms` sont **supprimées**, avec
leurs constantes `PoolDefaults` et leurs champs de `PoolLogicDefaultsSpec`. Garder deux algorithmes
aurait figé du code mort et une page de configuration à double lecture, pour un algo dont la §4
démontre qu'il est structurellement inadapté. Un effacement NVS est recommandé (les anciennes clés
sont simplement ignorées, pas migrées). Le PID temporel reste en place pour l'ORP.

**Le décompte du mélange ne court que si l'eau circule** (`DosingInput::circulating`). Point non
couvert par la note : sans cela, une filtration qui s'arrête pendant le `Mixing` fait « passer » le
turnover sans qu'un seul m³ ait été brassé, et le lot suivant est décidé sur une mesure non
homogénéisée.

**La détection « sans effet » ignore le signe du delta.** La §5.3 rejetait implicitement les deltas
du mauvais signe comme des perturbations. C'est juste pour l'*apprentissage du gain* (une observation
aberrante fausserait durablement la calibration) et c'est ce qui a été implémenté. Mais c'est faux
pour la *détection d'inefficacité* : avec un bidon vide, la dérive naturelle fait **remonter** le pH,
donc le delta est négatif — un filtre sur le signe rendrait la détection aveugle à son cas principal.
Le compteur s'incrémente donc dès que la correction attendue ne se produit pas, quel que soit le
signe ; l'exigence de `ph_no_effect_lots` lots *consécutifs* suffit à écarter les perturbations
ponctuelles.

**Le garde-fou de timeout de lot a été retiré du plan.** Il était mathématiquement inatteignable :
le volume délivré et le temps ON sont calculés depuis le *même* débit, donc `delivered >= target` se
déclenche toujours avant `elapsedOn > 3 × nominal`. Les protections réelles contre une pompe bloquée
sont le quota volumétrique journalier et le `max_uptime_day_s` du slot PoolDevice.

**`unitStep` est un paramètre dès l'origine** (défaut 0,1) plutôt qu'un `0.1f` en dur, pour que
l'extension ORP (§5.6-5) ne demande pas de migration.

**`PoolDeviceSvcMeta` porte les métriques volumétriques** (`flowLPerHour`, `injectedMlDay`,
`tankRemainingMl`) — voie A de la proposition. `readPoolDeviceFlowLh_`, qui re-sérialisait la config
en JSON puis faisait un `strstr`, en devient supprimable.

**Reprise après reboot** : variante horodatée retenue (`ph_last_dose_ts`), pas de `Mixing` forcé au
démarrage à froid, qui aurait bloqué la régulation jusqu'à 5 h après chaque OTA.
