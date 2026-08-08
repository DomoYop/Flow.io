#pragma once
/**
 * @file MqttCommandPolicy.h
 * @brief Commandes refusees sur le canal MQTT. Logique pure, testee en natif.
 */

#include <string.h>

/**
 * @brief Vrai si la commande ne doit pas etre executee quand elle arrive par MQTT.
 *
 * Le topic `cmd` execute n'importe quelle commande enregistree dans le
 * CommandRegistry. Or le broker est un point d'entree partage : Home Assistant,
 * les autres integrations et, selon les ACL, d'autres appareils peuvent publier
 * sur ce topic. Une mise a jour de firmware declenchee depuis la ne demande
 * qu'un seul message, se telecharge depuis une URL fournie dans la commande, et
 * n'est pas rattrapable.
 *
 * Sont donc refuses `fw.update` et ses sous-commandes, a l'exception de
 * `fw.update.status`, en lecture seule. Les mises a jour restent disponibles par
 * l'interface web, ou l'operateur est devant la machine.
 *
 * Hors perimetre volontaire :
 * - `fw.nextion.reboot` : redemarre l'ecran, n'ecrit aucune memoire flash ;
 * - `system.factory_reset` et l'import de configuration complete : destructifs
 *   eux aussi, mais reversibles par reconfiguration, et leur refus se discute
 *   separement (Home Assistant peut legitimement vouloir piloter un reset).
 */
inline bool mqttCommandDenied(const char* cmd)
{
    if (!cmd) return false;

    static constexpr char kFwUpdate[] = "fw.update";
    static constexpr size_t kFwUpdateLen = sizeof(kFwUpdate) - 1U;
    if (strncmp(cmd, kFwUpdate, kFwUpdateLen) != 0) return false;

    // Ne pas attraper un futur `fw.updates_metrics` : seuls le nom exact et ses
    // sous-commandes pointees sont concernes.
    const char next = cmd[kFwUpdateLen];
    if (next != '\0' && next != '.') return false;

    return strcmp(cmd, "fw.update.status") != 0;
}
