/**
 * @file FilesystemVersion.cpp
 * @brief Implementation file.
 */
#include "Core/FilesystemVersion.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr const char* kVersionPath = "/fsver.j";
// Le descripteur fait ~200 octets ; la marge couvre un champ ajoute plus tard.
constexpr size_t kMaxFileBytes = 512;

char g_core[24] = {0};
char g_buildRef[24] = {0};
char g_full[48] = {0};
char g_contentHash[17] = {0};
bool g_loaded = false;
bool g_present = false;

/**
 * @brief Extrait la valeur texte d'une cle JSON de premier niveau.
 *
 * Volontairement sans ArduinoJson : src/Core/ est compile par tous les profils,
 * dont les lib_deps peuvent diverger, et le producteur du fichier est notre
 * propre script de build (format controle, sans echappement ni imbrication).
 */
bool extractJsonString_(const char* json, const char* key, char* out, size_t outLen)
{
    if (!json || !key || !out || outLen == 0) return false;
    out[0] = '\0';

    char pattern[24] = {0};
    const int patternLen = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (patternLen <= 0 || (size_t)patternLen >= sizeof(pattern)) return false;

    const char* cursor = strstr(json, pattern);
    if (!cursor) return false;
    cursor += patternLen;

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor != ':') return false;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor != '"') return false;
    ++cursor;

    const char* end = strchr(cursor, '"');
    if (!end) return false;

    const size_t len = (size_t)(end - cursor);
    if (len == 0 || len >= outLen) return false;
    memcpy(out, cursor, len);
    out[len] = '\0';
    return true;
}

void load_()
{
    if (g_loaded) return;

    // Un echec de montage n'est pas mis en cache : l'ordre d'init des modules ne
    // garantit pas que SPIFFS soit deja monte au premier appel, et figer un
    // "absent" a ce moment-la serait definitif. Seul un montage reussi tranche.
    if (!SPIFFS.begin(false)) return;

    g_loaded = true;
    if (!SPIFFS.exists(kVersionPath)) return;

    File file = SPIFFS.open(kVersionPath, FILE_READ);
    if (!file) return;

    char raw[kMaxFileBytes + 1] = {0};
    const size_t got = file.readBytes(raw, kMaxFileBytes);
    file.close();
    if (got == 0) return;
    raw[got] = '\0';

    if (!extractJsonString_(raw, "version", g_core, sizeof(g_core))) return;
    (void)extractJsonString_(raw, "build_ref", g_buildRef, sizeof(g_buildRef));
    (void)extractJsonString_(raw, "content_hash", g_contentHash, sizeof(g_contentHash));

    if (g_buildRef[0] != '\0') {
        snprintf(g_full, sizeof(g_full), "%s+%s", g_core, g_buildRef);
    } else {
        snprintf(g_full, sizeof(g_full), "%s", g_core);
    }
    g_present = true;
}

}  // namespace

bool FilesystemVersion::present()
{
    load_();
    return g_present;
}

const char* FilesystemVersion::core()
{
    load_();
    return g_core;
}

const char* FilesystemVersion::buildRef()
{
    load_();
    return g_buildRef;
}

const char* FilesystemVersion::full()
{
    load_();
    return g_full;
}

const char* FilesystemVersion::contentHash()
{
    load_();
    return g_contentHash;
}
