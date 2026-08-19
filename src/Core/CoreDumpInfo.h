#pragma once
/**
 * @file CoreDumpInfo.h
 * @brief Resume du dernier vidage de crash conserve en flash.
 *
 * Le framework est compile avec CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH et le format
 * ELF : chaque panique ecrit un vidage complet dans la partition `coredump`. Le
 * Task Watchdog en fait partie -- CONFIG_ESP_TASK_WDT_PANIC est pose, donc un
 * `reset=task_wdt` passe lui aussi par le gestionnaire de panique.
 *
 * Rien ne lisait cette partition jusqu'ici : apres un redemarrage anormal, la tache
 * fautive restait inconnue sans cable USB. C'est ce qui a fait diagnostiquer a
 * l'aveugle les blocages OTA d'aout 2026 -- voir
 * docs/notes/ota-spiffs-gzip-bilan.md.
 *
 * Seul le dernier vidage est conserve (CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE
 * n'est pas pose) : le relever avant le crash suivant.
 */

#include <stddef.h>
#include <stdint.h>

namespace CoreDumpInfo {

/** @brief Nombre d'adresses de retour rendues par l'IDF sur Xtensa. */
constexpr size_t kBacktraceMax = 16U;
/** @brief a0-a15 au moment du crash (registre fenetre Xtensa courant). */
constexpr size_t kRegisterCount = 16U;
/** @brief PC empiles par niveau d'interruption (EPC1..EPC7 selon le coeur). */
constexpr size_t kEpcxMax = 8U;

struct Summary {
    bool present = false;       ///< un vidage valide est en flash
    char task[20] = {0};        ///< tache fautive
    char reason[100] = {0};     ///< raison de la panique, formulee par l'IDF
    uint32_t pc = 0;            ///< compteur de programme au moment du crash
    uint32_t excCause = 0;      ///< cause d'exception Xtensa
    uint32_t excVaddr = 0;      ///< adresse fautive, quand elle a un sens
    uint32_t backtrace[kBacktraceMax] = {0};
    uint8_t depth = 0;
    bool corrupted = false;     ///< backtrace juge incomplet par l'IDF
    // a1 est le pointeur de pile : une valeur hors de la plage de la tache
    // designe un debordement de pile a l'oeil nu, sans decodage. a0 est
    // l'adresse de retour du registre fenetre courant.
    uint32_t regs[kRegisterCount] = {0};
    uint32_t epcx[kEpcxMax] = {0};
    uint8_t epcxBits = 0;       ///< bit i pose si epcx[i] est renseigne
    char elfSha256[68] = {0};   ///< identifie le build a utiliser pour decoder
    uint32_t size = 0;          ///< taille du vidage en flash
};

/** @brief Lit le resume. false si aucun vidage valide n'est present. */
bool read(Summary& out);

/**
 * @brief Serialise le resume en JSON.
 *
 * Les adresses sont rendues en hexadecimal : elles se decodent au poste de travail
 * avec `xtensa-esp-elf-addr2line -e <firmware.elf>`, l'ELF devant etre celui du
 * build identifie par `elf_sha256`.
 */
bool toJson(char* out, size_t outLen);

/** @brief Efface le vidage. Jamais automatique : uniquement sur demande explicite. */
bool erase();

/** @brief Taille et emplacement du vidage brut, pour le servir tel quel. */
bool imageLocation(uint32_t& offsetOut, uint32_t& sizeOut);

/** @brief Copie une tranche du vidage brut depuis la flash. */
bool imageRead(uint32_t offset, void* dst, size_t len);

}  // namespace CoreDumpInfo
