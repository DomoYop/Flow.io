#pragma once
/**
 * @file IFirmwareUpdate.h
 * @brief Firmware update service interface.
 */

#include <stddef.h>
#include <stdint.h>

class Print;

enum class FirmwareUpdateTarget : uint8_t {
    Nextion = 2,
    Waveshare = 3,
    Spiffs = 4
};

struct FirmwareUpdateService {
    bool (*start)(void* ctx, FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool (*statusJson)(void* ctx, char* out, size_t outLen);
    bool (*isBusy)(void* ctx);
    bool (*configJson)(void* ctx, char* out, size_t outLen);
    bool (*checkManifestJsonStream)(void* ctx, Print& out, char* errOut, size_t errOutLen);
    bool (*manifestUrl)(void* ctx, char* out, size_t outLen, char* errOut, size_t errOutLen);
    bool (*setConfig)(void* ctx,
                      const char* updateHost,
                      const char* updatePath,
                      char* errOut,
                      size_t errOutLen);
    /**
     * @brief Essai a blanc de l'OTA SPIFFS : telecharge, decompresse, verifie.
     *
     * Aucune ecriture flash, aucun redemarrage. Sert a savoir ou une mise a jour
     * echoue sans risquer le systeme de fichiers en place.
     */
    bool (*startSpiffsDryRun)(void* ctx, const char* url, char* errOut, size_t errOutLen);
    void* ctx;
};
