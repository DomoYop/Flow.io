#pragma once
/**
 * @file PoolIoHaDiscovery.h
 * @brief Publication Home Assistant commune des endpoints E/S piscine.
 *
 * La presentation (suffixe d'objet, nom, icone, unite) vient de kPoolRoles ;
 * les slots hors role recoivent un fallback generique ("io_aNN", "io_diN").
 * Le heap de discovery est allouee une fois puis liberee apres le one-shot
 * HA, comme dans l'implementation historique par profil.
 */

#include "Domain/DomainSpec.h"

class IOModule;
class DataStore;
class PoolDeviceModule;
struct HAService;

struct PoolIoHaContext {
    IOModule* io = nullptr;
    const HAService* ha = nullptr;
    DataStore* dataStore = nullptr;
    const DomainSpec* domain = nullptr;
    // Equipements : sert au tombstone HA des switches desactives (page
    // Equipements). Optionnel : sans lui, aucun switch n'est marque absent.
    const PoolDeviceModule* poolDevice = nullptr;
};

namespace PoolIoHa {

/** Declare capteurs analogiques, entrees digitales et switches aupres de HA. */
void registerDiscovery(const PoolIoHaContext& ctx);

/** Re-synchronise apres un changement de config analogique (mode dynamique). */
void refreshIfNeeded(const PoolIoHaContext& ctx);

/** Libere le heap de discovery une fois la publication one-shot terminee. */
void releaseDiscoveryHeapIfDone(const PoolIoHaContext& ctx);

}  // namespace PoolIoHa
