# Geler les mesures en ligne quand l'eau ne circule plus

**État : implémenté et compilé** (profil `Waveshare-ESP32-S3`). Clés NVS
nouvelles uniquement, **pas d'effacement requis**.

## Le problème

Les sondes pH et Redox vivent dans un porte-sondes monté sur la tuyauterie.
Pompe à l'arrêt, elles ne mesurent plus que l'eau immobile qui y reste
prisonnière : le pH monte avec le dégazage du CO₂, le Redox s'effondre faute de
renouvellement du désinfectant près de l'électrode. La sonde de température
d'eau, quand elle est montée en ligne (c'est le cas ici), suit le local
technique au lieu du bassin.

Ces valeurs partaient telles quelles vers Home Assistant, l'écran, l'interface
web : historique pollué chaque nuit, courbes illisibles, et une lecture du
dashboard qui ne veut rien dire douze heures par jour.

## Ce qui était déjà protégé — et ne l'a pas été deux fois

Le **côté régulation** ne souffrait pas du problème :

- le dosage pH/Redox n'est armé qu'après `dly_pid_min` (5 min) de marche
  filtration, et `circulating` est une entrée de la FSM de dosage ;
- `ph_sample_max_age` (300 s) rejette un échantillon trop vieux ;
- les alarmes de pression sortent en `False` dès que la filtration est arrêtée
  ([PoolLogicControl.cpp](../../src/Modules/PoolLogicModule/PoolLogicControl.cpp)) ;
- l'interlock flowswitch existe (optionnel, `flow_interlock`).

Le chantier ne porte donc **que sur la donnée publiée**. Le gel ne coupe rien et
ne décide rien : il empêche de publier la dérive du porte-sondes.

## Le mécanisme

Un seul point de gel : `IOModule::processAnalogDefinition_`, juste avant
`endpoint->update()`. Tout en découle — `readAnalog` (PoolLogic, HMI, TFT), le
DataStore (Runtime UI, interface web), et le snapshot `rt/io/input/aNN` que Home
Assistant consomme.

`PoolLogicModule` est le seul à savoir si l'eau circule, et il dépend déjà de
`ModuleId::Io` : la poussée se fait donc dans ce sens, via deux ajouts à
`IOServiceV2` ([IIO.h](../../src/Core/Services/IIO.h)) :

> **Qui décide que l'eau circule** dépend d'une seule question : le flowswitch
> est-il déclaré installé (`flow_present`, ou `flow_interlock` qui vaut
> déclaration) ?
>
> - **Oui** → le flowswitch décide **seul**. C'est une mesure physique du débit,
>   là où l'état du relais n'est qu'une intention : une pompe forcée à la main,
>   hors du firmware, donne quand même des mesures valides.
> - **Non** → c'est la mise en route de la pompe qui pilote, et l'entrée TOR est
>   ignorée.
>
> Cette déclaration n'est pas une formalité. Une entrée TOR libre est en pull-up
> et lit « pas de débit » en permanence ; la première version obéissait à cette
> lecture fantôme et gelait les mesures pour toujours, filtration en marche
> comprise (**corrigé**). C'est exactement la précaution qui rend l'interlock de
> sécurité opt-in.


| Appel | Rôle |
|---|---|
| `setAnalogHold(id, hold)` | marque un endpoint comme gelable |
| `setCirculating(circulating, settleSec)` | publie l'état hydraulique |

**Ce que voit l'utilisateur pendant le gel** : la dernière valeur acquise pompe
en marche, `available` toujours vrai (pas de trou dans l'historique, pas de
rafale de notifications `unavailable`), et un marqueur `held`.

La tuile « Mesures figées » du tableau de bord porte `invertSeverity` dans son
`displayConfig` : le rendu booléen colore `true` en vert par défaut, or ici
`true` est une dégradation. Le drapeau est générique et réutilisable pour tout
état dont le « vrai » est la mauvaise nouvelle.

### Trois décisions qui ne sont pas des détails

1. **L'horodatage continue d'avancer.** La mesure est volontairement figée, pas
   périmée. Le figer aussi ferait sonner chaque nuit `condWaterTempUnavailable`
   (`kHeaterTempFreshMaxMs` = 10 min) et basculerait `FiltrationWindow` sur son
   plan de repli au recalcul journalier.

2. **La fenêtre du filtre médian est vidée au front montant.** Elle contient
   11 échantillons d'eau immobile au moment où la pompe redémarre ; sans purge,
   on aurait cessé de mentir pour republier ce mélange une minute plus tard. La
   temporisation `sensor_hold_settle_s` (90 s) couvre la purge du porte-sondes
   **et** le remplissage de la fenêtre, y compris pour une source lente.

3. **Le marqueur `held` n'est pas cosmétique.** Sans lui, un pH parfaitement
   plat pendant douze heures est indiscernable d'une sonde morte. Il est publié
   sur trois canaux : champ `"held":true` dans le snapshot MQTT de l'endpoint,
   `IOEndpointRuntime::held` dans le DataStore, et un `binary_sensor` Home
   Assistant `pl_sensor_hold` (« Readings Held ») alimenté par
   `rt/poollogic/flow`.

L'indicateur global a **une seule source** : `ioAnyEndpointHeld(dataStore)`,
c'est-à-dire ce qui est réellement gelé. Ni le module ni l'interface web ne
recalculent la temporisation de reprise de leur côté.

## Ce qui est gelé, et ce qui ne l'est pas

| Capteur | Gelé | Pourquoi |
|---|---|---|
| pH, Redox | oui | toujours en ligne, dérivent à l'arrêt |
| Température d'eau | oui, si `sensor_hold_wat` | dépend du montage : en ligne ici |
| **Pression** | **non** | 0 bar à l'arrêt est une information vraie ; la geler ferait croire que la pompe tourne, et ses alarmes sont déjà neutralisées |
| Air, POWERMON, BME680/SHT40, compteur | jamais | indépendants de l'hydraulique |

Le marquage suit le **rôle métier**, pas le slot : rebinder `ph_io_id` sur une
autre entrée déplace le gel avec lui, sans redémarrage
(`applySensorHoldBindings_`, rejoué sur `ConfigChanged` des branches
`sensors` et `safety`).

## Réglages (page Piscine → Sécurité)

| Clé | Défaut | Remarque |
|---|---|---|
| `sensor_hold` (`pl_shold`) | activé | interrupteur global |
| `sensor_hold_settle_s` (`pl_shsdl`) | 90 s | **borné à 240 s** |
| `sensor_hold_wat` (`pl_shwat`) | activé | sonde d'eau montée en ligne |

Plus, dans **Piscine → Sondes**, `flow_present` (`pl_fspres`, défaut inactif) :
le flowswitch est-il réellement câblé. C'est lui qui arbitre qui décide de la
circulation (voir plus haut).

Le plafond de 240 s n'est pas arbitraire : la sonde de température du chauffage
fait tourner la filtration 5 min et **décide à la fin**
(`kHeatAssistProbeRunSec`). Un délai de reprise plus long lui ferait prendre sa
décision sur la valeur figée de la veille. Une valeur hors borne est écrêtée et
réécrite en NVS, avec un log.

## Limites connues

- **Après un redémarrage du firmware pompe à l'arrêt, rien n'est gelé** tant que
  la filtration n'a pas tourné une fois : il n'existe pas encore de valeur de
  référence (`heldValid`). Les mesures dérivent alors comme avant. C'est
  volontaire — figer sur une valeur d'eau stagnante serait pire.
- Une sonde qui tombe en panne pendant le gel passe `invalid` : le marqueur
  `held` retombe avec la validité. Panne et gel restent distinguables.
- **Si `flow_interlock` est activé avec un flowswitch défaillant, le gel ne se
  lève plus.** C'est voulu — le système ne peut pas savoir que l'eau circule —
  mais le dosage serait bloqué en même temps, donc le symptôme est visible. Un
  `LOGW` est émis quand le gel persiste alors que la filtration tourne.
- Le pire cas de `binary_sensor` Home Assistant passe à **22 sur 24**
  (8 entrées TOR + 10 alarmes + 4 PoolLogic). Il reste deux places ; au-delà,
  la troncature serait silencieuse — voir `HaCapacitySpec::binarySensors` dans
  [WaveshareBoard.h](../../src/Board/WaveshareBoard.h).

## À vérifier sur cible

1. Arrêt de filtration : le pH doit se figer et `binary_sensor.pl_sensor_hold`
   passer à ON dans la minute.
2. Redémarrage : la valeur doit rester figée ~90 s, puis repartir **sans
   à-coup** (c'est le test de la purge du filtre médian).
3. Nuit complète : vérifier que l'alarme « température d'eau indisponible » ne
   se déclenche pas (c'est le test de l'horodatage qui avance).
