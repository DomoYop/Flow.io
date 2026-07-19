# Filtration par renouvellement volumique + fenêtres priorisées

**Statut : implémenté** (branche `io-refonte-unifiee`). Remplace l'ancienne règle « durée = température/2 ».

## Modèle

Le besoin journalier de filtration est un besoin **physique** de renouvellement de l'eau :

```
besoin (min) = volume bassin (m³) × cycles(T) / débit pompe (m³/h) × 60
```

- `volume bassin` : clé existante `poollogic/o2/pool_volume_m3` (**source unique**, partagée avec le dosage O2 ; un changement de volume déclenche un recalcul du plan).
- `débit pompe` : nouvelle clé `poollogic/filtration/pump_flow_m3h` (NVS `pl_pflw`, défaut 10 m³/h), exposée en number HA `pl_pump_flow`.
- `cycles(T)` : courbe non linéaire interpolée (plateau aux extrêmes), points dans `PoolDefaults::kFiltrationCyclesCurve` :
  ≤10°C→0,25 ; 14°C→0,4 ; 18°C→0,6 ; 22°C→1,0 ; 25°C→1,4 ; 28°C→2,0 ; ≥31°C→3,0.
- Bornes : minimum 2 h/jour (`FiltrationMinTotalMinutes`), maximum = capacité totale des fenêtres actives.

## Distribution : 3 fenêtres configurables priorisées

Chaque fenêtre (`filtr_w{1..3}_en/start/stop/prio`, NVS `pl_fw{i}e/s/p/r`) est en **minutes depuis minuit** ; `stop < start` = fenêtre traversant minuit. Le besoin est versé dans les fenêtres par ordre de `prio` (1 = remplie en premier), chaque segment **centré** dans sa fenêtre. Reliquat < 30 min arrondi à 30 min (pas de cycle pompe court) ; reliquat non plaçable tronqué.

**Heures creuses** : créer une fenêtre sur la plage HC (gabarit livré : fenêtre 2 = 23:30–07:30, désactivée) et lui donner `prio` 1 — elle est remplie en premier, le reste déborde sur les fenêtres suivantes.

Défauts d'usine : F1 = 08:00–23:00 prio 1 active (reprend `DomainSpec::filtrationStartMinHour/StopMaxHour`) ; F2 = gabarit HC prio 2 inactive ; F3 inactive.

## Implémentation

- **Fonction pure** : `computeFiltrationPlan()` / `isFiltrationPlanActiveAtMinute()` dans `src/Modules/PoolLogicModule/FiltrationWindow.{h,cpp}` (l'ancienne `computeFiltrationWindowDeterministic` est supprimée). Tests host : `test/test_poollogic_filtration_window/`.
- **Slots TimeScheduler** : 4–6 (`SLOT_FILTR_WINDOW_BASE` + segment), un par segment, même `eventId`. TimeModule gère nativement les fenêtres traversant minuit. Slot 3 = recalcul quotidien à 15 h (inchangé).
- **État actif** : recalculé par OU sur le plan (`filtrationPlan_`, gardé par `pendingMux_`) à chaque front scheduler et dans la boucle de contrôle — jamais déduit du dernier front seul (segments adjacents).
- **Recalcul déclenché par** : recalcul quotidien 15 h, boot (`onStart`), toute clé de la branche `poollogic/filtration` (hors clés calculées), et `pool_volume_m3` (branche O2).
- **Compat affichage** : `filtr_start_clc`/`filtr_stop_clc` publient désormais l'enveloppe (heures) du **segment prioritaire** ; l'activité `PoolLogicFiltrationWindowCalculated` détaille tous les segments. Leur modification externe est devenue sans effet (sorties pures).
- **Hors périmètre du plan** (inchangés) : hors-gel, hivernage, marches forcées chauffage/électrolyse, interlock débit.

## Réglages supprimés

- `filtr_start_min` / `filtr_stop_max` (NVS `pl_smin`/`pl_smax`) → remplacés par les fenêtres. Pas de migration NVS (choix de branche).
- `wat_temp_lo_th` / `wat_temp_setpt` (NVS `pl_tlow`/`pl_tset`) → ne nourrissaient que l'ancienne formule ; supprimés du module, de l'UI et de HA (`pl_wat_temp_sp`, `pl_flt_start_min`, `pl_flt_stop_max` retirés de HA). Les champs `tempLow`/`tempHigh`/`filtrationStartMinHour`/`filtrationStopMaxHour` de `PoolLogicDefaultsSpec` sont conservés (les deux derniers alimentent les défauts de la fenêtre 1).

## Idées ultérieures (non implémentées)

- Correction du débit estimé par la pression filtre (encrassement → durée +).
- Boucle fermée : ORP lent à remonter → bonus de durée le lendemain ; bouton « boost baignade ».
