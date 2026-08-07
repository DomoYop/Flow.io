# Auto-binding analogique : rendre son slot au port, pas le premier trou

**État : implémenté et compilé** (profil `Waveshare-ESP32-S3`).

## Le symptôme

Relevé sur matériel, page E/S et Config Store, après avoir passé l'entrée `a03`
(rôle `Spare`, ADS int A3) à « Non connecté » :

| Slot | Rôle domaine | Bindé sur | Nom NVS | Valeur |
|---|---|---|---|---|
| a03 | Spare | DS18B20 ch2 | Température 3 | Erreur (pas de ROM) |
| a04 | Temperature 1 | DS18B20 ch0 | Temperature 1 | 36.6 |
| a05 | Temperature 2 | DS18B20 ch1 | Temperature 2 | 28.3 |
| a06 | Temperature 3 | DS18B20 ch3 | Température 4 | Erreur |
| a07 | **Temperature 4** | **INA22x ch0** | Puissance shunt mV | 3.03 |
| a08…a14 | — | INA ch1…ch7 | Puissance … | décalés d'un cran |

Deux orthographes cohabitaient, ce qui trahissait deux chemins de code : les noms
sans accent (« Temperature 1 ») viennent de l'`endpointId` du domaine
([PoolDomain.h](../../src/Domain/Pool/PoolDomain.h)), les noms accentués
(« Température 3 ») de la table d'auto-binding
([IoAnalogSlotDefaults.h](../../src/Modules/IOModule/IoAnalogSlotDefaults.h)).

Le dégât n'était pas cosmétique. Le rôle métier reste attaché au **numéro de
slot**, et c'est lui qui fabrique l'entité Home Assistant
([PoolIoHaDiscovery.cpp](../../src/Domain/Pool/PoolIoHaDiscovery.cpp)) : suffixe
`io_temp4`, icône thermomètre, unité °C, état lu sur `rt/io/input/a07`. Avec
l'INA posé sur a07, `io_temp4` publiait une tension de shunt en degrés.

## Le mécanisme

Deux causes se sont combinées.

1. **Un héritage NVS.** Les slots a07 et suivants portaient des bindings INA
   écrits par l'auto-binding sous une version antérieure du firmware, quand le
   layout ne réservait pas encore a06/a07 aux sondes 3 et 4. Ces valeurs NVS
   recouvrent à chaque boot les défauts compile-time `a06→OneWire3, a07→OneWire4`
   de [WaveshareIoLayout.h](../../src/Profiles/Waveshare/WaveshareIoLayout.h) :
   les ports 122 et 123 étaient **orphelins**, liés à aucun slot.
2. **Un trou suffit à tout décaler.** `autoBindEnabledAnalogDrivers_` posait tout
   port orphelin d'un backend activé sur le **premier slot libre en partant de
   a00**, puis persistait le résultat (nom + binding + calibration). Passer a03 à
   « Non connecté » a ouvert ce trou : ch2 s'y est installé, puis ch3 a pris le
   trou suivant, a06.

L'auto-binding ne regardait ni la ROM (une sonde absente ne l'arrêtait pas), ni
le slot que le layout réservait au port, ni le rôle métier déjà porté par le slot
visé. D'où l'effet « je débranche une entrée, tout se décale ».

## Ce qui change

| Élément | Avant | Après |
|---|---|---|
| Port d'usine d'un slot | déduit de `analogSlots_[i].cfg.bindingPort` | `analogLayoutPort_[]`, figé au montage |
| Choix du slot d'accueil | premier slot libre | slot réservé par le layout, puis premier libre |
| Canal DS18B20 sans ROM | auto-bindé quand même | ignoré (`LOGI`) |
| Slot réservé à un autre backend | utilisable comme trou | sauté (`LOGW` avec le compte) |
| Nom écrit sur le slot de layout | table d'auto-binding | nom du rôle métier (`analogSlots_[idx].id`) |

`analogLayoutPort_` est un tableau dédié et non une lecture de
`analogSlots_[i].cfg.bindingPort` : ce champ porte bien le défaut du layout au
moment de l'auto-binding, mais `configureRuntime_` l'écrase ensuite avec la
valeur de config. S'appuyer dessus aurait rendu le correctif dépendant de l'ordre
d'appel, sans que rien ne le signale le jour où cet ordre change. Il est
renseigné dans `defineAnalogInput` et `applyAnalogInputDefaults`.

Le nom du rôle n'est repris que sur le slot de layout : un nom hérité
(« Puissance shunt mV » sur le slot Temperature 4) cède, et les deux orthographes
du même capteur ne peuvent plus coexister. Sur un slot de repli, la table
d'auto-binding reste la source du nom.

## Points de vigilance

- **Le correctif est préventif, pas curatif.** Un binding déjà persisté rend
  `alreadyBound` vrai et l'auto-binding n'y touche pas. Une NVS déjà dans le
  désordre le reste.
- Remise en ordre sans effacer la NVS, dans cet ordre sous peine de doublons de
  port : a03 → ADS int A3, a06 → OneWire3, a07 → OneWire4, a08 à a14 → Non
  connecté, puis reboot. Les huit canaux INA se reposent alors sur a08…a15.
- Un slot dont le layout réserve le port au **même** backend reste un repli
  valide (a04 libre peut accueillir le rang 3) : le critère est le backend, pas
  le canal. Le nom peut alors ne pas refléter le rang.
- `ANALOG_CFG_SLOTS` peut dépasser `MAX_ANALOG_ENDPOINTS`
  ([SystemLimits.h](../../include/Core/SystemLimits.h)) : l'accès à
  `analogSlots_` est borné, `analogLayoutPort_` est dimensionné sur les slots de
  config.
- Le garde-fou ROM ne bloque rien au premier boot d'usine : a04…a07 sont bindés
  par le layout, donc `alreadyBound`, et `resolveDs18Sensors_` renseigne les ROM
  normalement.

## Vérification

Build `Waveshare-ESP32-S3` SUCCESS — RAM 34,8 %, flash 47,2 %
(1 981 754 / 4 194 304 o).

À contrôler sur matériel :

1. Série au boot : plus aucune ligne `io.autobind` quand tous les ports sont
   bindés ; `io.autobind: DS18B20 ch2 sans ROM -> aucun slot occupe` si le rang
   est orphelin et vide.
2. Passer a03 à « Non connecté » et rebooter : plus aucun décalage, a03 reste
   libre.
3. Après la remise en ordre : `io_temp4` redevient une température, la page E/S
   affiche un rôle et un binding cohérents ligne par ligne.
