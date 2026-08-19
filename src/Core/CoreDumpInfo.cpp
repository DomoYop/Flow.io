/**
 * @file CoreDumpInfo.cpp
 * @brief Lecture du vidage de crash depuis la partition `coredump`.
 */

#include "Core/CoreDumpInfo.h"

#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <stdio.h>
#include <string.h>

namespace {

const esp_partition_t* coreDumpPartition_()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
}

}  // namespace

namespace CoreDumpInfo {

bool read(Summary& out)
{
    out = Summary{};

    size_t addr = 0U;
    size_t size = 0U;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0U) return false;
    // Verifie l'entete et la somme de controle : un vidage tronque par une coupure
    // en pleine ecriture serait lu de travers, et son backtrace inventerait des
    // adresses plausibles.
    if (esp_core_dump_image_check() != ESP_OK) return false;
    out.size = (uint32_t)size;

    // Sur le tas, pas sur la pile : la structure porte le backtrace, les seize
    // registres a0-a15 et les EPCx, et cette fonction est appelee depuis la tache
    // du serveur web comme depuis celle du journal. C'est exactement le genre de
    // structure qui a fait deborder la pile de `fwupdate` en aout 2026.
    esp_core_dump_summary_t* summary =
        (esp_core_dump_summary_t*)heap_caps_malloc(sizeof(esp_core_dump_summary_t),
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!summary) return false;

    if (esp_core_dump_get_summary(summary) != ESP_OK) {
        heap_caps_free(summary);
        return false;
    }

    snprintf(out.task, sizeof(out.task), "%.*s", (int)sizeof(summary->exc_task), summary->exc_task);
    out.pc = summary->exc_pc;
    out.excCause = summary->ex_info.exc_cause;
    out.excVaddr = summary->ex_info.exc_vaddr;
    out.corrupted = summary->exc_bt_info.corrupted;

    for (size_t i = 0U; i < kRegisterCount; ++i) out.regs[i] = summary->ex_info.exc_a[i];
    const size_t epcxCount = sizeof(summary->ex_info.epcx) / sizeof(summary->ex_info.epcx[0]);
    for (size_t i = 0U; i < epcxCount && i < kEpcxMax; ++i) out.epcx[i] = summary->ex_info.epcx[i];
    out.epcxBits = summary->ex_info.epcx_reg_bits;

    const uint32_t depth = (summary->exc_bt_info.depth > kBacktraceMax)
                               ? (uint32_t)kBacktraceMax
                               : summary->exc_bt_info.depth;
    for (uint32_t i = 0U; i < depth; ++i) out.backtrace[i] = summary->exc_bt_info.bt[i];
    out.depth = (uint8_t)depth;

    snprintf(out.elfSha256, sizeof(out.elfSha256), "%.*s",
             (int)sizeof(summary->app_elf_sha256), (const char*)summary->app_elf_sha256);

    heap_caps_free(summary);

    // Texte libre de l'IDF ("Task watchdog got triggered...", "StoreProhibited"...).
    // Absent sur certains vidages : ce n'est pas une erreur de lecture.
    char reason[200] = {0};
    if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK) {
        snprintf(out.reason, sizeof(out.reason), "%s", reason);
    }

    out.present = true;
    return true;
}

bool toJson(char* out, size_t outLen)
{
    if (!out || outLen == 0U) return false;

    Summary dump;
    if (!read(dump)) {
        const int n = snprintf(out, outLen, "{\"ok\":true,\"present\":false}");
        return n > 0 && (size_t)n < outLen;
    }

    int n = snprintf(out,
                     outLen,
                     "{\"ok\":true,\"present\":true,\"task\":\"%s\",\"pc\":\"0x%08lX\","
                     "\"exc_cause\":%lu,\"exc_vaddr\":\"0x%08lX\",\"reason\":\"%s\","
                     "\"elf_sha256\":\"%s\",\"size\":%lu,\"corrupted\":%s,\"backtrace\":[",
                     dump.task,
                     (unsigned long)dump.pc,
                     (unsigned long)dump.excCause,
                     (unsigned long)dump.excVaddr,
                     dump.reason,
                     dump.elfSha256,
                     (unsigned long)dump.size,
                     dump.corrupted ? "true" : "false");
    if (n <= 0 || (size_t)n >= outLen) return false;

    for (uint8_t i = 0U; i < dump.depth; ++i) {
        const int w = snprintf(out + n,
                               outLen - (size_t)n,
                               "%s\"0x%08lX\"",
                               (i == 0U) ? "" : ",",
                               (unsigned long)dump.backtrace[i]);
        if (w <= 0 || (size_t)(n + w) >= outLen) return false;
        n += w;
    }

    // a1 = pointeur de pile au moment du crash : une valeur hors de la plage
    // habituelle de la tache incriminee designe un debordement de pile sans le
    // moindre decodage. a0 = adresse de retour du registre fenetre courant.
    const int wr = snprintf(out + n, outLen - (size_t)n, "],\"regs\":[");
    if (wr <= 0 || (size_t)(n + wr) >= outLen) return false;
    n += wr;
    for (size_t i = 0U; i < kRegisterCount; ++i) {
        const int w = snprintf(out + n,
                               outLen - (size_t)n,
                               "%s\"0x%08lX\"",
                               (i == 0U) ? "" : ",",
                               (unsigned long)dump.regs[i]);
        if (w <= 0 || (size_t)(n + w) >= outLen) return false;
        n += w;
    }

    const int we = snprintf(out + n, outLen - (size_t)n, "],\"epcx\":[");
    if (we <= 0 || (size_t)(n + we) >= outLen) return false;
    n += we;
    bool firstEpcx = true;
    for (size_t i = 0U; i < kEpcxMax; ++i) {
        if (((dump.epcxBits >> i) & 1U) == 0U) continue;
        const int w = snprintf(out + n,
                               outLen - (size_t)n,
                               "%s\"0x%08lX\"",
                               firstEpcx ? "" : ",",
                               (unsigned long)dump.epcx[i]);
        if (w <= 0 || (size_t)(n + w) >= outLen) return false;
        n += w;
        firstEpcx = false;
    }

    const int w = snprintf(out + n, outLen - (size_t)n, "]}");
    return w > 0 && (size_t)(n + w) < outLen;
}

bool erase()
{
    return esp_core_dump_image_erase() == ESP_OK;
}

bool imageLocation(uint32_t& offsetOut, uint32_t& sizeOut)
{
    size_t addr = 0U;
    size_t size = 0U;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0U) return false;
    const esp_partition_t* part = coreDumpPartition_();
    if (!part) return false;
    // `esp_core_dump_image_get` rend une adresse absolue en flash ; les lectures se
    // font relativement a la partition.
    offsetOut = (addr >= part->address) ? (uint32_t)(addr - part->address) : 0U;
    sizeOut = (uint32_t)size;
    return true;
}

bool imageRead(uint32_t offset, void* dst, size_t len)
{
    const esp_partition_t* part = coreDumpPartition_();
    if (!part || !dst || len == 0U) return false;
    if ((uint64_t)offset + len > (uint64_t)part->size) return false;
    return esp_partition_read(part, offset, dst, len) == ESP_OK;
}

}  // namespace CoreDumpInfo
