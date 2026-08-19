#pragma once
/**
 * @file SystemStats.h
 * @brief Lightweight system and heap stats helpers.
 */
#include <stdint.h>

/**
 * @brief Heap snapshot (no dynamic allocation).
 *
 * Les quatre premiers champs portent sur MALLOC_CAP_8BIT, qui agrege la DRAM
 * interne ET la PSRAM. Sur un module a 8 Mo de PSRAM ils restent hauts en
 * permanence et ne disent donc rien de l'epuisement du seul tas qui peut
 * reellement manquer, l'interne. Les champs `internal*` / `psram*` separent les
 * deux.
 *
 * Observation seule : `deriveMemoryPressureState_` continue de lire les champs
 * agreges. Recalibrer ses seuils sur `internalFreeBytes` reveillerait d'un coup
 * quatre seuils qui n'ont jamais servi, dont un qui redemarre la carte -- a faire
 * une fois les vrais chiffres connus, pas avant.
 * Voir docs/notes/audit-paniques-flash-cache.md.
 */
struct HeapStats {
    uint32_t freeBytes;           // heap_caps_get_free_size(MALLOC_CAP_8BIT)
    uint32_t minFreeBytes;        // lowest free 8-bit heap observed since boot
    uint32_t largestFreeBlock;    // heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    uint8_t fragPercent;          // 0..100 estimated fragmentation

    uint32_t internalFreeBytes;         // DRAM interne libre
    uint32_t internalMinFreeBytes;      // plancher observe depuis le demarrage
    uint32_t internalLargestFreeBlock;  // plus gros bloc interne contigu
    uint32_t internalTotalBytes;        // taille totale du tas interne
    uint8_t internalFragPercent;        // 0..100, calcule sur le seul tas interne

    uint32_t psramFreeBytes;
    uint32_t psramLargestFreeBlock;
    uint32_t psramTotalBytes;           // 0 si aucune PSRAM n'est montee
};

/** @brief Full system snapshot used by monitoring. */
struct SystemStatsSnapshot {
    uint64_t uptimeMs64;
    uint32_t uptimeMs;
    HeapStats heap;
};

/** @brief Lightweight helper (core, stateless). */
class SystemStats {
public:
    /** @brief Fill snapshot with current system metrics. */
    static void collect(SystemStatsSnapshot& out);

    /** @brief Reset reason as const string (ESP_RST_*). */
    static const char* resetReasonStr();
};
