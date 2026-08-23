#pragma once
/**
 * @file ConfigMigrations.h
 * @brief Config migration steps for ConfigStore.
 */
#include <Preferences.h>
#include "Core/ConfigStore.h"
#include "Core/NvsKeys.h"

/** @brief Current configuration schema version. */
constexpr uint32_t CURRENT_CFG_VERSION = 4;

/** @brief Migration step from version 0 to 1. */
static bool mig_0_to_1(Preferences& prefs, bool clearOnFail)
{
    (void)prefs;
    (void)clearOnFail;
    // TODO migration logic
    return true; // true = OK, false = failed
}

/** @brief Migration step from version 1 to 2. */
static bool mig_1_to_2(Preferences& prefs, bool clearOnFail)
{
    (void)clearOnFail;
    if (prefs.isKey(NvsKeys::Hmi::RemoteUdpEnabledLegacy) &&
        !prefs.isKey(NvsKeys::Hmi::FlowConnectUdpEnabled)) {
        const bool enabled = prefs.getBool(NvsKeys::Hmi::RemoteUdpEnabledLegacy, false);
        (void)prefs.putBool(NvsKeys::Hmi::FlowConnectUdpEnabled, enabled);
    }
    if (prefs.isKey(NvsKeys::Hmi::RemoteUdpTokenLegacy) &&
        !prefs.isKey(NvsKeys::Hmi::FlowConnectUdpToken)) {
        char token[33]{};
        if (prefs.getString(NvsKeys::Hmi::RemoteUdpTokenLegacy, token, sizeof(token)) > 0U) {
            (void)prefs.putString(NvsKeys::Hmi::FlowConnectUdpToken, token);
        }
    }
    return true;
}

/**
 * @brief Migration step from version 2 to 3: pressure keys `pl_psi*` -> `pl_pr*`.
 *
 * Le capteur ne s'exprime plus en PSI depuis son renommage en « Pression »
 * (unite bar) ; ses cles NVS avaient ete laissees telles quelles. On recopie la
 * valeur reglee par l'utilisateur sous le nouveau nom, puis on efface l'ancienne
 * cle -- sans quoi NVS garderait un doublon inerte.
 *
 * Best-effort volontaire : une cle absente n'est pas une erreur, et la nouvelle
 * cle deja presente n'est jamais ecrasee. Un retour `false` ici declencherait un
 * `clear()` de toute la configuration (voir ConfigStore::runMigrations).
 */
static bool mig_2_to_3(Preferences& prefs, bool clearOnFail)
{
    (void)clearOnFail;

    // Seuils d'alarme, en bar : ConfigType::Float -> putFloat/getFloat.
    struct KeyRename {
        const char* from;
        const char* to;
    };
    static const KeyRename kFloatRenames[] = {
        {NvsKeys::PoolLogic::PressureLowLegacy, NvsKeys::PoolLogic::PressureRef},
        {NvsKeys::PoolLogic::PressureHighLegacy, NvsKeys::PoolLogic::PressureHigh},
    };
    for (const KeyRename& r : kFloatRenames) {
        if (!prefs.isKey(r.from)) continue;
        if (!prefs.isKey(r.to)) {
            (void)prefs.putFloat(r.to, prefs.getFloat(r.from, 0.0f));
        }
        (void)prefs.remove(r.from);
    }

    // Delai avant surveillance, en secondes : ConfigType::UInt8 -> putUChar.
    if (prefs.isKey(NvsKeys::PoolLogic::PressureDelayLegacy)) {
        if (!prefs.isKey(NvsKeys::PoolLogic::PressureDelay)) {
            (void)prefs.putUChar(NvsKeys::PoolLogic::PressureDelay,
                                 prefs.getUChar(NvsKeys::PoolLogic::PressureDelayLegacy, 0));
        }
        (void)prefs.remove(NvsKeys::PoolLogic::PressureDelayLegacy);
    }
    return true;
}

/**
 * @brief Migration step from version 3 to 4: `pl_prlow` change de sens.
 *
 * La cle portait le seuil de pression basse qui coupait la filtration -- un
 * detecteur de debit par defaut herite de PoolMaster. Elle porte desormais la
 * pression de service filtre propre, qui ne declenche rien et sert de reference
 * a l'alerte d'encrassement.
 *
 * L'ancienne valeur (0,15 bar par defaut) serait une reference de service
 * absurde et desactiverait silencieusement l'alerte : elle est donc remise a
 * zero **inconditionnellement**, ce qui relance l'apprentissage automatique.
 * C'est le seul cas du fichier ou l'on ecrase un reglage utilisateur ; il est
 * assume, la valeur n'ayant aucun sens dans son nouveau role.
 *
 * `pl_prhigh` garde sa valeur : son role de securite mecanique est inchange.
 */
static bool mig_3_to_4(Preferences& prefs, bool clearOnFail)
{
    (void)clearOnFail;

    (void)prefs.putFloat(NvsKeys::PoolLogic::PressureRef, 0.0f);

    if (!prefs.isKey(NvsKeys::PoolLogic::PressureFoulingDelta)) {
        (void)prefs.putFloat(NvsKeys::PoolLogic::PressureFoulingDelta, 0.40f);
    }

    // Le blob de latch porte des AlarmId bruts : l'id 1000 y a ete ecrit dans son
    // ancien sens (« pression basse »). Le restaurer reactiverait une alarme qui
    // ne designe plus la meme chose. Elle ne coupe plus rien, mais s'afficherait
    // active sans cause -- on repart d'une ardoise propre.
    if (prefs.isKey(NvsKeys::Alarm::LatchBlob)) {
        (void)prefs.remove(NvsKeys::Alarm::LatchBlob);
    }
    return true;
}

/** @brief Ordered list of migrations. */
static const MigrationStep steps[] = {
    {0, 1, mig_0_to_1},
    {1, 2, mig_1_to_2},
    {2, 3, mig_2_to_3},
    {3, 4, mig_3_to_4}
};

/** @brief Number of migration steps. */
static constexpr size_t MIGRATION_COUNT = sizeof(steps) / sizeof(steps[0]);
