/**
 * @file SpiffsAccessLock.cpp
 * @brief Implementation du verrou partage autour de SPIFFS.
 */

#include "Core/SpiffsAccessLock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SpiffsAccessLock {

namespace {

SemaphoreHandle_t gMutex = nullptr;
portMUX_TYPE gCreateMux = portMUX_INITIALIZER_UNLOCKED;

// Creation paresseuse : ce module n'a pas de point d'init dedie dans le cycle de
// vie des Module/ModulePassive, et les premiers appelants (ActivityLogModule,
// WebInterfaceModule) peuvent demarrer dans un ordre variable. La section
// critique protege uniquement la creation elle-meme, pas son usage -- un mutex
// FreeRTOS gere son propre acces concurrent une fois cree.
SemaphoreHandle_t mutex_()
{
    if (gMutex) return gMutex;
    portENTER_CRITICAL(&gCreateMux);
    if (!gMutex) gMutex = xSemaphoreCreateRecursiveMutex();
    portEXIT_CRITICAL(&gCreateMux);
    return gMutex;
}

}  // namespace

bool acquire(uint32_t timeoutMs)
{
    SemaphoreHandle_t m = mutex_();
    if (!m) return false;
    return xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void release()
{
    if (gMutex) xSemaphoreGiveRecursive(gMutex);
}

}  // namespace SpiffsAccessLock
