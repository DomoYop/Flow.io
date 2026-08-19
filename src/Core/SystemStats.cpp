/**
 * @file SystemStats.cpp
 * @brief Implementation file.
 */
#include "SystemStats.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

void SystemStats::collect(SystemStatsSnapshot& out) {
    const uint64_t uptimeMs64 = (uint64_t)(esp_timer_get_time() / 1000ULL);
    out.uptimeMs64 = uptimeMs64;
    // Keep the legacy 32-bit view for code paths that still expect uint32_t.
    out.uptimeMs = (uint32_t)uptimeMs64;

    const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t minFree8 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    out.heap.freeBytes = free8;
    out.heap.minFreeBytes = minFree8;
    out.heap.largestFreeBlock = largest;

    /// Fragmentation estimation:
    /// - if largest is close to free => low fragmentation
    /// - if largest is much smaller => high fragmentation
    if (free8 == 0) {
        out.heap.fragPercent = 100;
    } else {
        float ratio = (float)largest / (float)free8;   ///< 0..1
        float frag = 1.0f - ratio;                     ///< 0..1
        if (frag < 0.0f) frag = 0.0f;
        if (frag > 1.0f) frag = 1.0f;
        out.heap.fragPercent = (uint8_t)(frag * 100.0f);
    }

    // MALLOC_CAP_8BIT ci-dessus agrege DRAM interne et PSRAM. Les deux tas sont
    // repris separement ici : c'est l'interne qui s'epuise, et lui seul qui rend
    // une allocation impossible quand la PSRAM est encore a moitie vide.
    // MALLOC_CAP_INTERNAL est croise avec MALLOC_CAP_8BIT pour ne compter que ce
    // qu'une allocation ordinaire peut effectivement utiliser (la memoire
    // accessible seulement par mots en est exclue).
    constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t internalFree = heap_caps_get_free_size(kInternalCaps);
    const uint32_t internalLargest = heap_caps_get_largest_free_block(kInternalCaps);
    out.heap.internalFreeBytes = internalFree;
    out.heap.internalMinFreeBytes = (uint32_t)heap_caps_get_minimum_free_size(kInternalCaps);
    out.heap.internalLargestFreeBlock = internalLargest;
    out.heap.internalTotalBytes = (uint32_t)heap_caps_get_total_size(kInternalCaps);

    if (internalFree == 0U) {
        out.heap.internalFragPercent = 100;
    } else {
        float ratio = (float)internalLargest / (float)internalFree;
        float frag = 1.0f - ratio;
        if (frag < 0.0f) frag = 0.0f;
        if (frag > 1.0f) frag = 1.0f;
        out.heap.internalFragPercent = (uint8_t)(frag * 100.0f);
    }

    // Rend 0 partout si aucune PSRAM n'est montee : le champ reste lisible sans
    // garde cote appelant.
    out.heap.psramFreeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out.heap.psramLargestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    out.heap.psramTotalBytes = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

const char* SystemStats::resetReasonStr() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
    }
}
