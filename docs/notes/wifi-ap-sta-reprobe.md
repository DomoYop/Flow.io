# Note de travail — Retour automatique AP → STA (re-probe WiFi)

> Statut : **analyse + proposition, non implémenté**
> Date : 2026-06-27
> Contexte : à faible réception (~-95 dBm), le firmware bascule en mode AP (portail
> captif) mais ne revient jamais automatiquement en STA quand le signal redevient
> exploitable. À reprendre demain.

## 1. Architecture de la connexion WiFi

Deux modules coopèrent via le service `WifiService` :

| Module | Rôle |
|---|---|
| [WifiModule](../../src/Modules/Network/WifiModule/WifiModule.cpp) | Machine d'état **STA pure** (connexion au routeur) |
| [WifiProvisioningModule](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp) | Overlay qui décide **quand basculer en AP** ; embarque la classe `NetworkManager` |

### Machine d'état STA (`WifiModule`)

États : `Disabled → Idle → Connecting → Connected → ErrorWait`
([WifiModule.cpp:1052-1168](../../src/Modules/Network/WifiModule/WifiModule.cpp#L1052)).

Drapeau clé : **`staRetryEnabled_`**, piloté de l'extérieur par le provisioning.
- `false` ⇒ l'état `Idle` ne fait plus rien : pas de `WiFi.begin()`, STA gelé
  ([ligne 1059-1062](../../src/Modules/Network/WifiModule/WifiModule.cpp#L1059)).
- L'auto-reconnect interne de l'ESP est désactivé (`WiFi.setAutoReconnect(false)`,
  [ligne 856](../../src/Modules/Network/WifiModule/WifiModule.cpp#L856)) : toute la
  logique de retry appartient à la machine d'état.

### Décision STA → AP (`NetworkManager::portalReason`)

[WifiProvisioningModule.h:60-80](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h#L60).
Avec les réglages Waveshare (`ETH_TIMEOUT_MS=7000`, `WIFI_TIMEOUT_MS=12000`) :

- **Boot** : attend `ethWait + 12 s`. STA non accroché dans la fenêtre ⇒
  `ConnectTimeout` ⇒ démarrage du portail AP.
- Pas d'identifiants ⇒ `MissingCredentials` immédiat.
- **Grâce après perte** : réseau déjà eu puis perdu ⇒ délai `ETH_TIMEOUT_MS` (7 s)
  avant de relancer le portail
  ([ligne 63-66](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h#L63)).

### Passage en AP : le STA est gelé « dur »

`startCaptivePortal_()`
([ligne 411-572](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L411)) :

```cpp
setStaRetryEnabled(false);   // gèle la machine d'état STA      (l.456-458)
WiFi.disconnect(...);
WiFi.enableSTA(false);        // éteint physiquement le STA      (l.468)
WiFi.mode(WIFI_MODE_NULL);    // puis bascule en softAP pur      (l.484-498)
```

Motivation (commentaire l.466) : un scan STA en arrière-plan en APSTA faisait sauter
des téléphones connectés au portail. D'où le choix de couper totalement le STA.

## 2. Diagnostic : pourquoi ça ne rebascule pas

Comportement **volontaire** mais inadapté au cas « signal faible intermittent ».
Une fois en AP :

1. La politique de re-test STA est explicitement désactivée — `handleStaProbePolicy_()`
   ne fait rien
   ([ligne 706-713](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L706)).
2. Le seul réveil du STA (`startStaProbe_`) n'est appelé que dans `handleSaveRequest_`
   (sauvegarde de nouveaux identifiants), et ce bloc n'est compilé que pour le profil
   Flow Connect Display
   ([ligne 1280](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L1280)).
   **Sur Waveshare, `startStaProbe_` n'est jamais appelé.**
3. La sortie du portail (`stopCaptivePortal_`) n'arrive que si `hasStationNetwork_()`
   devient vrai
   ([ligne 231-236](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L231)),
   ce qui exige un STA connecté… qui est gelé. **Verrou logique.**

**Conclusion** : une fois en AP, le firmware ne réessaie plus jamais le WiFi seul.
Sorties possibles aujourd'hui : **reboot**, ou **reconfiguration WiFi** via le portail
(`notifyWifiConfigChanged_` → `setStaRetryEnabled(true)` + `requestReconnect`,
[ligne 298-308](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L298)).

## 3. Changements proposés

Réactiver un re-test STA périodique en AP, **uniquement quand aucun client n'est
connecté au portail**. Tout est localisé dans `WifiProvisioningModule` ; la mécanique
existe déjà (`startStaProbe_`/`stopStaProbe_`/constantes `kStaProbe*`), elle est juste
débranchée. Aucun changement dans `WifiModule`.

### Changement 1 — Brancher la politique de probe (cœur)

Remplacer le no-op `handleStaProbePolicy_`
([ligne 706-713](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L706)) :

```cpp
void WifiProvisioningModule::handleStaProbePolicy_(uint32_t nowMs)
{
    if (!apActive_) return;

    // Règle d'or : ne jamais perturber un client connecté au portail.
    // Si un probe tourne et qu'un client arrive, on l'interrompt aussitôt
    // (le STA en APSTA peut déplacer le canal et casser l'association).
    if (apClientCount_ > 0U) {
        if (staProbeActive_) stopStaProbe_("AP client present");
        return;
    }

    // Sans identifiants STA exploitables, rien à tester.
    if (!wifiEnabled_ || !wifiConfigured_) {
        if (staProbeActive_) stopStaProbe_("STA not configured");
        return;
    }

    if (staProbeActive_) {
        // Fenêtre de test en cours. Le SUCCÈS est géré par loop() via
        // hasStationNetwork_() -> stopCaptivePortal_(). Ici on ne gère que
        // l'échec : fenêtre écoulée sans connexion -> retour en AP strict.
        if (!isStaConnected_() &&
            (uint32_t)(nowMs - lastStaProbeStartMs_) >= kStaProbeWindowMs) {
            stopStaProbe_("STA probe window elapsed");
        }
        return;
    }

    // Aucun probe en cours : on relance un test après l'intervalle.
    if (lastStaProbeStartMs_ == 0U ||
        (uint32_t)(nowMs - lastStaProbeStartMs_) >= kStaProbeIntervalMs) {
        startStaProbe_(nowMs);
    }
}
```

### Changement 2 — Lever le garde qui bloque le probe autonome

Dans `startStaProbe_`
([ligne 663-668](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.cpp#L663)),
retirer `if (!apClientEverSeen_) return;` :

```cpp
void WifiProvisioningModule::startStaProbe_(uint32_t nowMs)
{
    if (staProbeActive_) return;
    if (!wifiEnabled_ || !wifiConfigured_) return;
    // (garde apClientEverSeen_ retiré : la politique de probe est désormais
    //  pilotée par handleStaProbePolicy_, qui ne probe que si apClientCount_==0)
    ...
```

Sûr pour l'autre appelant (`handleSaveRequest_`, Flow Connect Display) : un client
vient justement de sauvegarder, la condition était de toute façon vraie.

### Changement 3 (recommandé) — Allonger la fenêtre de test

`kStaProbeWindowMs = 6000`
([WifiProvisioningModule.h:137](../../src/Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h#L137))
est court : à -95 dBm, association + DHCP peut dépasser 6 s. Passer à ~12 s :

```cpp
static constexpr uint32_t kStaProbeWindowMs = 12000U;  // était 6000U
```

`kStaProbeIntervalMs = 30000` (un essai / 30 s) reste raisonnable.

## 4. Comportement résultant

```
AP actif, 0 client
   └─ toutes les 30 s ─► startStaProbe_()
                          ├─ setStaRetryEnabled(true) + requestReconnect
                          ├─ WifiModule repasse en APSTA et tente WiFi.begin
                          │
                          ├─ succès (≤12 s) ─► loop(): hasStationNetwork_()==true
                          │                     ─► stopCaptivePortal_() ─► retour STA ✅
                          │
                          └─ échec (>12 s)  ─► stopStaProbe_() ─► AP strict, STA re-gelé
AP actif, ≥1 client
   └─ probe suspendu / interrompu (portail préservé)
```

## 5. Points de vigilance

- **Radio unique** : pendant un probe, l'AP suit le canal du STA. Sans client, sans
  effet ; avec client, on interrompt (garde `apClientCount_ > 0` en tête de la
  politique).
- `apClientCount_` est piloté par événements (`AP_STACONNECTED` / `AP_STADISCONNECTED`
  → `refreshApClientState_`) : il reflète l'arrivée/départ d'un client en quasi temps
  réel.
- Affinement optionnel : ne reprendre les probes qu'après un délai depuis le départ du
  dernier client. La constante `kApClientGraceMs = 120000` existe déjà et pourrait
  servir via `lastApClientSeenMs_`.

## 6. Reste à faire (demain)

- [ ] Appliquer les changements 1 à 3.
- [ ] Compiler `pio run -e Waveshare-ESP32-S3`.
- [ ] Test terrain : forcer le passage AP (couper/affaiblir le signal), puis rétablir
      et vérifier le retour STA automatique en l'absence de client portail.
- [ ] Décider si on ajoute la grâce `kApClientGraceMs` après départ du dernier client.
