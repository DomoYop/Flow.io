/**
 * @file FirmwareUpdateModule.cpp
 * @brief Firmware updater implementation.
 */

#include "FirmwareUpdateModule.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <FS.h>
#include <HTTPClient.h>
#include <Update.h>
#include <string.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <miniz.h>

// Deux variantes d'OTA SPIFFS ont ete tentees pour reduire le volume transfere :
// l'image compressee (FLOW_OTA_SPIFFS_GZIP) et le paquet de fichiers
// (FLOW_OTA_SPIFFS_PKG).
//
// L'image compressee est active depuis le 2026-08-15 : le .gz est ramene entier en
// memoire, la connexion est fermee, l'image est verifiee (taille et CRC-32 du trailer
// gzip) puis ecrite sans reseau. La liaison WiFi n'a plus a tenir que le temps de
// transferer 333 Ko au lieu de rester ouverte pendant l'ecriture des 8,26 Mo. Un
// serveur qui ne publie pas le .gz continue de servir l'image complete, sans rien
// changer de son cote. Voir docs/notes/ota-spiffs-reduction-volume.md
#ifndef FLOW_OTA_SPIFFS_GZIP
#define FLOW_OTA_SPIFFS_GZIP 1
#endif
#ifndef FLOW_OTA_SPIFFS_PKG
#define FLOW_OTA_SPIFFS_PKG 0
#endif

#include "App/BuildFlags.h"
#include "Board/BoardSpec.h"
#include "Core/ErrorCodes.h"
#include "Core/FirmwareVersion.h"
#include "Core/SpiffsAccessLock.h"
#include "Core/SystemLimits.h"

#include <ESPNexUpload.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::FirmwareUpdateModule)
#include "Core/ModuleLog.h"

namespace {

const SupervisorBoardSpec& supervisorBoardSpec_(const BoardSpec& board)
{
    // Safety fallback used only when the selected BoardSpec does not expose
    // a supervisor extension block (board.supervisor == nullptr).
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            0,
            14,
            15,
            4,
            5,
            35,
            18,
            19,
            false,
            true,
            8000000U,
            80
        },
        {
            36,
            120,
            true,
            23,
            40
        },
        {
            25,
            26,
            13,
            115200U
        }
    };
    const SupervisorBoardSpec* cfg = boardSupervisorConfig(board);
    return cfg ? *cfg : kFallback;
}

const UartSpec& panelUartSpec_(const BoardSpec& board)
{
    static constexpr UartSpec kFallback{"panel", 2, 33, 32, 115200, false, -1};
    const UartSpec* spec = boardFindUart(board, "panel");
    if (!spec) spec = boardFindUart(board, "hmi");
    return spec ? *spec : kFallback;
}

}  // namespace

FirmwareUpdateModule::FirmwareUpdateModule(const BoardSpec& board)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    const UartSpec& panelUart = panelUartSpec_(board);
    flowIoEnablePin_ = boardCfg.update.flowIoEnablePin;
    nextionRxPin_ = panelUart.rxPin;
    nextionTxPin_ = panelUart.txPin;
    nextionRebootPin_ = boardCfg.update.nextionRebootPin;
    nextionUploadBaud_ = boardCfg.update.nextionUploadBaud;
}

static bool writeSimpleError_(char* out, size_t outLen, const char* msg)
{
    if (!out || outLen == 0) return false;
    if (!msg) msg = "failed";
    const int n = snprintf(out, outLen, "%s", msg);
    return n > 0 && (size_t)n < outLen;
}

static bool parseReqJsonObject_(const char* json, StaticJsonDocument<256>& doc)
{
    if (!json || json[0] == '\0') return false;
    const auto err = deserializeJson(doc, json);
    return !err && doc.is<JsonObjectConst>();
}

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static bool fileContainsToken_(fs::FS& fs, const char* path, const char* token)
{
    if (!path || !token || token[0] == '\0') return false;
    File f = fs.open(path, FILE_READ);
    if (!f) return false;

    const size_t tokLen = strlen(token);
    size_t match = 0;
    while (f.available()) {
        const int ch = f.read();
        if (ch < 0) break;
        if ((char)ch == token[match]) {
            ++match;
            if (match == tokLen) {
                f.close();
                return true;
            }
            continue;
        }
        match = ((char)ch == token[0]) ? 1U : 0U;
    }
    f.close();
    return false;
}

static bool validateCfgDocsFile_(fs::FS& fs, const char* path, char* errOut, size_t errOutLen)
{
    if (!path) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs path null");
        return false;
    }
    File f = fs.open(path, FILE_READ);
    if (!f) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs open failed");
        return false;
    }
    const size_t size = (size_t)f.size();
    f.close();
    if (size < 16U) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs too small");
        return false;
    }
    if (!fileContainsToken_(fs, path, "\"docs\"")) {
        writeSimpleError_(errOut, errOutLen, "cfgdocs missing docs");
        return false;
    }
    return true;
}

static void configureDownloadHttp_(HTTPClient& http)
{
    http.setReuse(false);
    http.setConnectTimeout(Limits::FirmwareUpdate::Http::ConnectTimeoutMs);
    http.setTimeout(Limits::FirmwareUpdate::Http::RequestTimeoutMs);
}

static bool writeHttpBeginFailedError_(const char* resourceLabel,
                                       const char* url,
                                       char* errOut,
                                       size_t errOutLen)
{
    const char* resource = (resourceLabel && resourceLabel[0] != '\0') ? resourceLabel : "ressource";
    LOGE("HTTP begin failed resource=%s url=%s", resource, url ? url : "-");
    return writeSimpleError_(errOut, errOutLen, "serveur HTTP injoignable");
}

static bool writeHttpCodeFailedError_(const char* resourceLabel,
                                      const char* url,
                                      HTTPClient& http,
                                      int code,
                                      char* errOut,
                                      size_t errOutLen)
{
    const char* resource = (resourceLabel && resourceLabel[0] != '\0') ? resourceLabel : "fichier";
    const String raw = http.errorToString(code);
    const char* rawErr = raw.c_str();
    char msg[96] = {0};

    if (code == 404) {
        snprintf(msg, sizeof(msg), "%s introuvable (404)", resource);
    } else if (code < 0) {
        snprintf(msg, sizeof(msg), "serveur HTTP injoignable");
    } else {
        snprintf(msg, sizeof(msg), "erreur HTTP %d", code);
    }

    LOGE("HTTP request failed resource=%s code=%d err=%s url=%s", resource, code, rawErr, url ? url : "-");
    return writeSimpleError_(errOut, errOutLen, msg);
}

static bool appendUrlSegment_(char* out, size_t outLen, const char* segment)
{
    if (!out || outLen == 0) return false;
    if (!segment || segment[0] == '\0') return true;

    while (*segment == '/') ++segment;
    if (*segment == '\0') return true;

    const size_t len = strlen(out);
    if (len >= outLen) return false;
    const bool needSlash = len > 0 && out[len - 1] != '/';
    const int n = snprintf(out + len, outLen - len, "%s%s", needSlash ? "/" : "", segment);
    return n >= 0 && (size_t)n < (outLen - len);
}

const char* FirmwareUpdateModule::stateStr_(UpdateState s)
{
    switch (s) {
        case UpdateState::Idle: return "idle";
        case UpdateState::Queued: return "queued";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::Flashing: return "flashing";
        case UpdateState::Rebooting: return "rebooting";
        case UpdateState::Done: return "done";
        case UpdateState::Error: return "error";
        default: return "unknown";
    }
}

const char* FirmwareUpdateModule::targetStr_(FirmwareUpdateTarget t)
{
    switch (t) {
        case FirmwareUpdateTarget::Nextion: return "nextion";
        case FirmwareUpdateTarget::Waveshare: return "waveshare";
        case FirmwareUpdateTarget::Spiffs: return "spiffs";
        default: return "unknown";
    }
}

void FirmwareUpdateModule::setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg)
{
    portENTER_CRITICAL(&lock_);
    status_.state = state;
    status_.target = target;
    status_.progress = progress;
    status_.updatedAtMs = millis();
    if (!msg) msg = "";
    snprintf(status_.msg, sizeof(status_.msg), "%s", msg);
    portEXIT_CRITICAL(&lock_);

    const bool otaActive = state == UpdateState::Queued ||
                           state == UpdateState::Downloading ||
                           state == UpdateState::Flashing ||
                           state == UpdateState::Rebooting;
    setHmiOtaCondition_(otaActive);
}

void FirmwareUpdateModule::setError_(FirmwareUpdateTarget target, const char* msg)
{
    setStatus_(UpdateState::Error, target, 0, msg ? msg : "failed");
}

void FirmwareUpdateModule::setHmiOtaCondition_(bool active)
{
    if (hmiOtaActive_ == active) return;
    if (!hmiSvc_ && services_) {
        hmiSvc_ = services_->get<HmiService>(ServiceId::Hmi);
    }
    if (hmiSvc_ && hmiSvc_->setLedCondition) {
        (void)hmiSvc_->setLedCondition(hmiSvc_->ctx, HmiLedCondition::OtaInProgress, active);
    }
    hmiOtaActive_ = active;
}

void FirmwareUpdateModule::onProgressChunk_(uint32_t chunkBytes)
{
    portENTER_CRITICAL(&lock_);
    if (activeTotalBytes_ == 0) {
        portEXIT_CRITICAL(&lock_);
        return;
    }
    uint32_t next = activeSentBytes_ + chunkBytes;
    if (next > activeTotalBytes_) next = activeTotalBytes_;
    activeSentBytes_ = next;
    status_.progress = (uint8_t)((activeSentBytes_ * 100U) / activeTotalBytes_);
    status_.updatedAtMs = millis();
    portEXIT_CRITICAL(&lock_);
}

void FirmwareUpdateModule::attachWebInterfaceSvcIfNeeded_()
{
    if (webInterfaceSvc_ || !services_) return;
    webInterfaceSvc_ = services_->get<WebInterfaceService>(ServiceId::WebInterface);
}

void FirmwareUpdateModule::attachFlowCfgSvcIfNeeded_()
{
    if (flowCfgSvc_ || !services_) return;
    flowCfgSvc_ = services_->get<FlowCfgRemoteService>(ServiceId::FlowCfg);
}

bool FirmwareUpdateModule::setFlowCfgPaused_(bool paused)
{
    attachFlowCfgSvcIfNeeded_();
    if (!flowCfgSvc_ || !flowCfgSvc_->setPaused) return false;
    return flowCfgSvc_->setPaused(flowCfgSvc_->ctx, paused);
}

bool FirmwareUpdateModule::resolveUrl_(FirmwareUpdateTarget target,
                                       const char* explicitUrl,
                                       char* out,
                                       size_t outLen,
                                       char* errOut,
                                       size_t errOutLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    if (explicitUrl && explicitUrl[0] != '\0') {
        const int n = snprintf(out, outLen, "%s", explicitUrl);
        if (n <= 0 || (size_t)n >= outLen) {
            writeSimpleError_(errOut, errOutLen, "url too long");
            return false;
        }
        return true;
    }

    (void)target;
    writeSimpleError_(errOut, errOutLen, "url required");
    return false;
}

bool FirmwareUpdateModule::resolveUpdateUrl_(const char* path,
                                             char* out,
                                             size_t outLen,
                                             char* errOut,
                                             size_t errOutLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    if (cfgData_.updateHost[0] == '\0') {
        writeSimpleError_(errOut, errOutLen, "update_host empty");
        return false;
    }
    if (!path || path[0] == '\0') {
        writeSimpleError_(errOut, errOutLen, "path empty");
        return false;
    }

    const bool hasProto =
        (strncmp(cfgData_.updateHost, "http://", 7) == 0) || (strncmp(cfgData_.updateHost, "https://", 8) == 0);
    const int n = hasProto
                      ? snprintf(out, outLen, "%s", cfgData_.updateHost)
                      : snprintf(out, outLen, "http://%s", cfgData_.updateHost);
    if (n <= 0 || (size_t)n >= outLen ||
        !appendUrlSegment_(out, outLen, cfgData_.updatePath) ||
        !appendUrlSegment_(out, outLen, path)) {
        writeSimpleError_(errOut, errOutLen, "resolved url too long");
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    StaticJsonDocument<256> doc;
    if (parseReqJsonObject_(req.args, doc)) {
        const char* url = doc["url"] | nullptr;
        if (url && url[0] != '\0') {
            snprintf(out, outLen, "%s", url);
            return true;
        }
    }

    doc.clear();
    if (parseReqJsonObject_(req.json, doc)) {
        const char* rootUrl = doc["url"] | nullptr;
        if (rootUrl && rootUrl[0] != '\0') {
            snprintf(out, outLen, "%s", rootUrl);
            return true;
        }
        JsonVariantConst args = doc["args"];
        if (args.is<JsonObjectConst>()) {
            const char* nestedUrl = args["url"] | nullptr;
            if (nestedUrl && nestedUrl[0] != '\0') {
                snprintf(out, outLen, "%s", nestedUrl);
                return true;
            }
        }
    }

    return false;
}

bool FirmwareUpdateModule::parseDryRunArg_(const CommandRequest& req) const
{
    // `dry` est accepte en booleen comme en entier : les appels viennent aussi bien
    // d'un JSON construit a la main que d'un parametre de requete converti en 1/0.
    auto readDry = [](const StaticJsonDocument<256>& doc) -> bool {
        JsonVariantConst v = doc["dry"];
        if (v.isNull()) {
            JsonVariantConst args = doc["args"];
            if (args.is<JsonObjectConst>()) v = args["dry"];
        }
        if (v.isNull()) return false;
        if (v.is<bool>()) return v.as<bool>();
        if (v.is<int>()) return v.as<int>() != 0;
        const char* s = v.as<const char*>();
        return s && (s[0] == '1' || s[0] == 't' || s[0] == 'T');
    };

    StaticJsonDocument<256> doc;
    if (parseReqJsonObject_(req.args, doc) && readDry(doc)) return true;
    doc.clear();
    return parseReqJsonObject_(req.json, doc) && readDry(doc);
}

bool FirmwareUpdateModule::statusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

    UpdateStatus snap{};
    bool busy = false;
    bool pending = false;
    portENTER_CRITICAL(&lock_);
    snap = status_;
    busy = busy_;
    pending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);

    sanitizeJsonString_(snap.msg);

    const int n = snprintf(out,
                           outLen,
                           "{\"ok\":true,\"state\":\"%s\",\"target\":\"%s\",\"busy\":%s,"
                           "\"pending\":%s,\"progress\":%u,\"ts_ms\":%lu,\"msg\":\"%s\"}",
                           stateStr_(snap.state),
                           targetStr_(snap.target),
                           busy ? "true" : "false",
                           pending ? "true" : "false",
                           (unsigned)snap.progress,
                           (unsigned long)snap.updatedAtMs,
                           snap.msg);
    return n > 0 && (size_t)n < outLen;
}

bool FirmwareUpdateModule::isBusy_()
{
    bool busy = false;
    bool pending = false;
    bool nextionReboot = false;
    portENTER_CRITICAL(&lock_);
    busy = busy_;
    pending = queuedJob_.pending;
    nextionReboot = nextionRebootQueued_;
    portEXIT_CRITICAL(&lock_);
    return busy || pending || nextionReboot;
}

bool FirmwareUpdateModule::configJson_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;

    char host[sizeof(cfgData_.updateHost)] = {0};
    char updatePath[sizeof(cfgData_.updatePath)] = {0};
    snprintf(host, sizeof(host), "%s", cfgData_.updateHost);
    snprintf(updatePath, sizeof(updatePath), "%s", cfgData_.updatePath);
    sanitizeJsonString_(host);
    sanitizeJsonString_(updatePath);

    const int n = snprintf(out,
                           outLen,
                           "{\"ok\":true,\"update_host\":\"%s\",\"update_path\":\"%s\"}",
                           host,
                           updatePath);
    return n > 0 && (size_t)n < outLen;
}

bool FirmwareUpdateModule::checkManifestJsonStream_(Print& out, char* errOut, size_t errOutLen)
{
    bool isBusy = false;
    bool hasPending = false;
    portENTER_CRITICAL(&lock_);
    isBusy = busy_;
    hasPending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);
    if (isBusy || hasPending) {
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }

    char url[kUrlLen] = {0};
    if (!resolveUpdateUrl_("manifest.json", url, sizeof(url), errOut, errOutLen)) {
        return false;
    }

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeHttpBeginFailedError_("manifest", url, errOut, errOutLen);
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        writeHttpCodeFailedError_("manifest", url, http, code, errOut, errOutLen);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();
    if (payload.length() == 0U) {
        writeSimpleError_(errOut, errOutLen, "manifest empty");
        return false;
    }

    size_t jsonCapacity = payload.length() + 1024U;
    if (jsonCapacity < 4096U) jsonCapacity = 4096U;
    DynamicJsonDocument doc(jsonCapacity);
    const DeserializationError jsonErr = deserializeJson(doc, payload);
    if (jsonErr || !doc.is<JsonObjectConst>()) {
        writeSimpleError_(errOut, errOutLen, "manifest invalid json");
        return false;
    }

    char safeUrl[kUrlLen] = {0};
    char current[48] = {0};
    snprintf(safeUrl, sizeof(safeUrl), "%s", url);
    snprintf(current, sizeof(current), "%s", FirmwareVersion::Full);
    sanitizeJsonString_(safeUrl);
    sanitizeJsonString_(current);

    out.print("{\"ok\":true,\"manifest_url\":\"");
    out.print(safeUrl);
    out.print("\",\"current\":{\"flowios3\":\"");
    out.print(current);
    out.print("\",\"esp32s3\":\"");
    out.print(current);
    out.print("\",\"waveshare\":\"");
    out.print(current);
    out.print("\"},\"manifest\":");
    out.print(payload);
    out.print("}");
    return true;
}

bool FirmwareUpdateModule::manifestUrl_(char* out, size_t outLen, char* errOut, size_t errOutLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    bool isBusy = false;
    bool hasPending = false;
    portENTER_CRITICAL(&lock_);
    isBusy = busy_;
    hasPending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);
    if (isBusy || hasPending) {
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }

    return resolveUpdateUrl_("manifest.json", out, outLen, errOut, errOutLen);
}

bool FirmwareUpdateModule::setConfig_(const char* updateHost,
                                      const char* updatePath,
                                      char* errOut,
                                      size_t errOutLen)
{
    if (!cfgStore_) {
        writeSimpleError_(errOut, errOutLen, "config store unavailable");
        return false;
    }

    bool isBusy = false;
    bool hasPending = false;
    portENTER_CRITICAL(&lock_);
    isBusy = busy_;
    hasPending = queuedJob_.pending;
    portEXIT_CRITICAL(&lock_);
    if (isBusy || hasPending) {
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }

    if (updateHost) {
        if (!cfgStore_->set(updateHostVar_, updateHost)) {
            writeSimpleError_(errOut, errOutLen, "set update_host failed");
            return false;
        }
    }
    if (updatePath) {
        if (!cfgStore_->set(updatePathVar_, updatePath)) {
            writeSimpleError_(errOut, errOutLen, "set update_path failed");
            return false;
        }
    }

    return true;
}

bool FirmwareUpdateModule::queueJob_(FirmwareUpdateTarget target,
                                     const char* url,
                                     bool dryRun,
                                     char* errOut,
                                     size_t errOutLen)
{
    UpdateJob job{};
    job.target = target;
    job.dryRun = dryRun;
    if (!resolveUrl_(target, url, job.url, sizeof(job.url), errOut, errOutLen)) {
        return false;
    }

    portENTER_CRITICAL(&lock_);
    if (busy_ || queuedJob_.pending) {
        portEXIT_CRITICAL(&lock_);
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }
    queuedJob_ = job;
    queuedJob_.pending = true;
    portEXIT_CRITICAL(&lock_);

    setStatus_(UpdateState::Queued, target, 0, dryRun ? "queued (dry run)" : "queued");
    LOGI("Update queued target=%s%s url=%s", targetStr_(target), dryRun ? " (dry run)" : "", job.url);
    return true;
}

bool FirmwareUpdateModule::startUpdate_(FirmwareUpdateTarget target,
                                        const char* url,
                                        char* errOut,
                                        size_t errOutLen)
{
    return queueJob_(target, url, false, errOut, errOutLen);
}

bool FirmwareUpdateModule::startSpiffsDryRun_(const char* url, char* errOut, size_t errOutLen)
{
    return queueJob_(FirmwareUpdateTarget::Spiffs, url, true, errOut, errOutLen);
}

bool FirmwareUpdateModule::queueNextionReboot_(char* errOut, size_t errOutLen)
{
    if (nextionRebootPin_ < 0) {
        writeSimpleError_(errOut, errOutLen, "nextion reboot pin not configured");
        return false;
    }

    portENTER_CRITICAL(&lock_);
    if (busy_ || queuedJob_.pending || nextionRebootQueued_) {
        portEXIT_CRITICAL(&lock_);
        writeSimpleError_(errOut, errOutLen, "updater busy");
        return false;
    }
    nextionRebootQueued_ = true;
    portEXIT_CRITICAL(&lock_);

    LOGI("Nextion reboot queued");
    return true;
}

bool FirmwareUpdateModule::runWaveshareUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Waveshare, 0, "downloading");

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeHttpBeginFailedError_("fichier de mise a jour", url, errOut, errOutLen);
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        writeHttpCodeFailedError_("fichier de mise a jour", url, http, code, errOut, errOutLen);
        http.end();
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Waveshare, 0, "flashing");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (contentLength > 0) ? (uint32_t)contentLength : 0U;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition) {
        writeSimpleError_(errOut, errOutLen, "ota partition unavailable");
        http.end();
        return false;
    }
    if (runningPartition && updatePartition->address == runningPartition->address) {
        writeSimpleError_(errOut, errOutLen, "ota target equals running partition");
        http.end();
        return false;
    }
    if (contentLength > 0 && (size_t)contentLength > updatePartition->size) {
        writeSimpleError_(errOut, errOutLen, "ota image too large for partition");
        http.end();
        return false;
    }

    attachWebInterfaceSvcIfNeeded_();
    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
    }

    char failMsg[128] = {0};
    const size_t beginSize = (contentLength > 0) ? (size_t)contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(beginSize, U_FLASH)) {
        snprintf(failMsg, sizeof(failMsg), "ota begin failed (%u)", (unsigned)Update.getError());
    } else {
        auto* stream = http.getStreamPtr();
        int32_t remaining = contentLength;
        uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
        uint32_t lastReadMs = millis();

        while (http.connected() && (contentLength <= 0 || remaining > 0)) {
            const size_t avail = stream ? stream->available() : 0;
            if (avail == 0U) {
                if (contentLength <= 0 && stream && !stream->connected()) {
                    break;
                }
                if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                    snprintf(failMsg, sizeof(failMsg), "ota stream timeout");
                    break;
                }
                delay(1);
                continue;
            }

            const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
            const int rd = stream->readBytes((char*)buf, toRead);
            if (rd <= 0) {
                delay(1);
                continue;
            }
            lastReadMs = millis();

            const size_t wr = Update.write(buf, (size_t)rd);
            if (wr != (size_t)rd) {
                snprintf(failMsg, sizeof(failMsg), "ota write failed (%u)", (unsigned)Update.getError());
                break;
            }

            onProgressChunk_((uint32_t)wr);

            if (contentLength > 0) {
                remaining -= rd;
                if (remaining <= 0) {
                    break;
                }
            }
        }

        if (failMsg[0] == '\0' && contentLength > 0 && remaining > 0) {
            snprintf(failMsg, sizeof(failMsg), "incomplete download");
        }
        if (failMsg[0] == '\0' && !Update.end()) {
            snprintf(failMsg, sizeof(failMsg), "ota end failed (%u)", (unsigned)Update.getError());
        }
        if (failMsg[0] == '\0' && !Update.isFinished()) {
            snprintf(failMsg, sizeof(failMsg), "ota not finished");
        }
    }

    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
    }

    http.end();

    if (failMsg[0] != '\0') {
        writeSimpleError_(errOut, errOutLen, failMsg);
        return false;
    }

    setStatus_(UpdateState::Rebooting, FirmwareUpdateTarget::Waveshare, 100, "rebooting");
    delay(1800);
    ESP.restart();
    return true;
}

bool FirmwareUpdateModule::runNextionUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    if (nextionRxPin_ < 0 || nextionTxPin_ < 0) {
        writeSimpleError_(errOut, errOutLen, "nextion board pins not configured");
        return false;
    }

    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Nextion, 0, "downloading");

    if (flowIoEnablePin_ >= 0) {
        pinMode(flowIoEnablePin_, OUTPUT);
        digitalWrite(flowIoEnablePin_, LOW);
    }

    if (nextionRebootPin_ >= 0) {
        pinMode(nextionRebootPin_, OUTPUT);
        digitalWrite(nextionRebootPin_, HIGH);
    }

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeHttpBeginFailedError_("fichier de mise a jour", url, errOut, errOutLen);
        if (flowIoEnablePin_ >= 0) {
            digitalWrite(flowIoEnablePin_, HIGH);
            pinMode(flowIoEnablePin_, INPUT);
        }
        return false;
    }

    const int code = http.GET();
    const int32_t contentLength = http.getSize();
    if (code != HTTP_CODE_OK) {
        writeHttpCodeFailedError_("fichier de mise a jour", url, http, code, errOut, errOutLen);
        http.end();
        if (flowIoEnablePin_ >= 0) {
            digitalWrite(flowIoEnablePin_, HIGH);
            pinMode(flowIoEnablePin_, INPUT);
        }
        return false;
    }
    if (contentLength <= 0) {
        writeSimpleError_(errOut, errOutLen, "invalid content-length");
        http.end();
        if (flowIoEnablePin_ >= 0) {
            digitalWrite(flowIoEnablePin_, HIGH);
            pinMode(flowIoEnablePin_, INPUT);
        }
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Nextion, 0, "flashing");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (uint32_t)contentLength;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    bool ok = false;
    ESPNexUpload nextion(nextionUploadBaud_, nextionRxPin_, nextionTxPin_);
    nextion.setUpdateProgressCallback([this]() {
        this->onProgressChunk_(2048U);
    });

    if (!nextion.prepareUpload((uint32_t)contentLength)) {
        writeSimpleError_(errOut, errOutLen, nextion.statusMessage.c_str());
    } else if (!nextion.upload(*http.getStreamPtr())) {
        writeSimpleError_(errOut, errOutLen, nextion.statusMessage.c_str());
    } else {
        ok = true;
    }
    nextion.end();

    pinMode(nextionRxPin_, INPUT);
    pinMode(nextionTxPin_, INPUT);

    http.end();
    if (flowIoEnablePin_ >= 0) {
        digitalWrite(flowIoEnablePin_, HIGH);
        pinMode(flowIoEnablePin_, INPUT);
    }

    if (!ok) return false;

    setStatus_(UpdateState::Done, FirmwareUpdateTarget::Nextion, 100, "nextion update complete");
    return true;
}

bool FirmwareUpdateModule::runNextionReboot_(char* errOut, size_t errOutLen)
{
    if (nextionRebootPin_ < 0) {
        writeSimpleError_(errOut, errOutLen, "nextion reboot pin not configured");
        return false;
    }

    pinMode(nextionRebootPin_, OUTPUT);
    digitalWrite(nextionRebootPin_, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(nextionRebootPin_, LOW);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(nextionRebootPin_, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(nextionRebootPin_, LOW);

    LOGI("Nextion reboot pulse sequence completed on pin=%d", (int)nextionRebootPin_);
    return true;
}

namespace {

// Paquet web : voir scripts/build_web_package.py pour le format complet.
constexpr size_t kWebPkgHeaderBytes = 24U;
constexpr uint16_t kWebPkgFormat = 1U;
constexpr const char* kWebPkgMagic = "FLOWPKG1";
constexpr const char* kWebPkgPath = "/ota/pkg.tmp";
// SPIFFS plafonne les noms a CONFIG_SPIFFS_OBJ_NAME_LEN (32). Le plus long chemin
// du contenu web fait 29 caracteres : un suffixe ".new" deborderait. D'ou un
// temporaire court, reutilise pour chaque fichier avant sa bascule.
constexpr const char* kWebPkgFileTmp = "/ota/t";
constexpr size_t kSpiffsPathMax = 31U;

/** CRC-32 (polynome reflechi), convention zlib : init 0xFFFFFFFF, inversion finale. */
uint32_t crc32Update_(uint32_t crc, const uint8_t* data, size_t len)
{
    static const uint32_t kNibble[16] = {
        0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
        0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
        0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
        0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
    };
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ kNibble[crc & 0x0FU];
        crc = (crc >> 4) ^ kNibble[crc & 0x0FU];
    }
    return crc;
}

uint16_t readLe16_(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

uint32_t readLe32_(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

}  // namespace

bool FirmwareUpdateModule::webPkgDownload_(NetworkClient* stream,
                                           int32_t contentLength,
                                           char* failMsg,
                                           size_t failMsgLen)
{
    if (!stream) {
        snprintf(failMsg, failMsgLen, "web pkg: no stream");
        return false;
    }
    if (SPIFFS.exists(kWebPkgPath)) SPIFFS.remove(kWebPkgPath);

    File out = SPIFFS.open(kWebPkgPath, FILE_WRITE);
    if (!out) {
        snprintf(failMsg, failMsgLen, "web pkg: cannot create %s", kWebPkgPath);
        return false;
    }

    uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
    int32_t remaining = contentLength;
    uint32_t lastReadMs = millis();
    uint32_t chunkCount = 0U;
    bool ok = true;

    while (ok && (contentLength <= 0 || remaining > 0)) {
        const size_t avail = stream->available();
        if (avail == 0U) {
            if (!stream->connected()) break;
            if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                snprintf(failMsg, failMsgLen, "web pkg: stream timeout");
                ok = false;
                break;
            }
            delay(1);
            continue;
        }
        const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        const int rd = stream->readBytes((char*)buf, toRead);
        if (rd <= 0) {
            delay(1);
            continue;
        }
        lastReadMs = millis();
        if (out.write(buf, (size_t)rd) != (size_t)rd) {
            snprintf(failMsg, failMsgLen, "web pkg: write failed (disque plein ?)");
            ok = false;
            break;
        }
        onProgressChunk_((uint32_t)rd);
        if (contentLength > 0) remaining -= rd;
        if ((++chunkCount % 16U) == 0U) vTaskDelay(1);
    }
    out.close();

    if (ok && contentLength > 0 && remaining > 0) {
        snprintf(failMsg, failMsgLen, "web pkg: telechargement incomplet");
        ok = false;
    }
    if (!ok) SPIFFS.remove(kWebPkgPath);
    return ok;
}

bool FirmwareUpdateModule::webPkgVerify_(uint16_t& fileCountOut,
                                         uint32_t& indexBytesOut,
                                         char* failMsg,
                                         size_t failMsgLen)
{
    File pkg = SPIFFS.open(kWebPkgPath, FILE_READ);
    if (!pkg) {
        snprintf(failMsg, failMsgLen, "web pkg: introuvable apres telechargement");
        return false;
    }

    uint8_t header[kWebPkgHeaderBytes] = {0};
    if (pkg.read(header, sizeof(header)) != (int)sizeof(header)) {
        snprintf(failMsg, failMsgLen, "web pkg: en-tete tronque");
        pkg.close();
        return false;
    }
    if (memcmp(header, kWebPkgMagic, 8) != 0) {
        snprintf(failMsg, failMsgLen, "web pkg: signature invalide");
        pkg.close();
        return false;
    }
    const uint16_t format = readLe16_(header + 8);
    if (format != kWebPkgFormat) {
        snprintf(failMsg, failMsgLen, "web pkg: format %u non gere", (unsigned)format);
        pkg.close();
        return false;
    }
    fileCountOut = readLe16_(header + 10);
    indexBytesOut = readLe32_(header + 12);
    const uint32_t payloadBytes = readLe32_(header + 16);
    const uint32_t expectedCrc = readLe32_(header + 20);

    const size_t expectedSize = kWebPkgHeaderBytes + indexBytesOut + payloadBytes;
    if ((size_t)pkg.size() != expectedSize) {
        snprintf(failMsg, failMsgLen, "web pkg: taille %lu, %lu attendus",
                 (unsigned long)pkg.size(), (unsigned long)expectedSize);
        pkg.close();
        return false;
    }

    // L'empreinte porte sur index + payload : c'est elle qui autorise a toucher au
    // contenu en place. Tant qu'elle n'est pas verifiee, rien n'a ete remplace.
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t buf[512];
    uint32_t chunkCount = 0U;
    size_t left = indexBytesOut + payloadBytes;
    while (left > 0U) {
        const size_t want = (left > sizeof(buf)) ? sizeof(buf) : left;
        const int rd = pkg.read(buf, want);
        if (rd <= 0) break;
        crc = crc32Update_(crc, buf, (size_t)rd);
        left -= (size_t)rd;
        if ((++chunkCount % 64U) == 0U) vTaskDelay(1);
    }
    pkg.close();
    crc ^= 0xFFFFFFFFUL;

    if (left != 0U || crc != expectedCrc) {
        snprintf(failMsg, failMsgLen, "web pkg: empreinte invalide");
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::webPkgFileMatches_(const char* path, uint32_t size, uint32_t crcExpected)
{
    File existing = SPIFFS.open(path, FILE_READ);
    if (!existing) return false;
    if ((uint32_t)existing.size() != size) {
        existing.close();
        return false;
    }

    uint8_t buf[512];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t left = size;
    uint32_t blocks = 0U;
    while (left > 0U) {
        const size_t want = (left > sizeof(buf)) ? sizeof(buf) : left;
        const int rd = existing.read(buf, want);
        if (rd <= 0) break;
        crc = crc32Update_(crc, buf, (size_t)rd);
        left -= (uint32_t)rd;
        if ((++blocks % 32U) == 0U) vTaskDelay(1);
    }
    existing.close();
    return (left == 0U) && ((crc ^ 0xFFFFFFFFUL) == crcExpected);
}

bool FirmwareUpdateModule::webPkgExtract_(uint16_t fileCount,
                                          uint32_t indexBytes,
                                          char* failMsg,
                                          size_t failMsgLen)
{
    File pkg = SPIFFS.open(kWebPkgPath, FILE_READ);
    if (!pkg) {
        snprintf(failMsg, failMsgLen, "web pkg: introuvable a l'extraction");
        return false;
    }

    uint8_t* index = (uint8_t*)heap_caps_malloc(indexBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!index) index = (uint8_t*)malloc(indexBytes);
    if (!index) {
        snprintf(failMsg, failMsgLen, "web pkg: index trop gros (%lu o)", (unsigned long)indexBytes);
        pkg.close();
        return false;
    }
    if (!pkg.seek(kWebPkgHeaderBytes) || pkg.read(index, indexBytes) != (int)indexBytes) {
        snprintf(failMsg, failMsgLen, "web pkg: index illisible");
        free(index);
        pkg.close();
        return false;
    }

    const size_t payloadBase = kWebPkgHeaderBytes + indexBytes;
    size_t cursor = 0U;
    size_t payloadOfs = 0U;
    // 2 Ko plutot que 512 o : une page SPIFFS fait 256 o, donc huit pages par
    // ecriture au lieu de deux. L'ecriture est de loin l'operation couteuse ici.
    uint8_t buf[2048];
    bool ok = true;
    uint16_t replaced = 0U;
    uint16_t skipped = 0U;

    for (uint16_t i = 0; ok && i < fileCount; ++i) {
        if (cursor + 10U > indexBytes) {
            snprintf(failMsg, failMsgLen, "web pkg: index incoherent");
            ok = false;
            break;
        }
        // Avancement publie a chaque fichier : c'est ce que l'interface affiche, et
        // c'est aussi la trace qui dit ou l'operation s'arrete si elle s'arrete.
        const uint8_t pct = (fileCount > 0U) ? (uint8_t)(((uint32_t)i * 100U) / fileCount) : 0U;
        setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, pct, "installing web package");
        if ((i % 16U) == 0U) {
            LOGI("Paquet web : %u/%u fichiers (%u remplaces, %u deja a jour)",
                 (unsigned)i, (unsigned)fileCount, (unsigned)replaced, (unsigned)skipped);
        }

        const uint16_t pathLen = readLe16_(index + cursor);
        const uint32_t size = readLe32_(index + cursor + 2);
        const uint32_t crcExpected = readLe32_(index + cursor + 6);
        cursor += 10U;
        if (cursor + pathLen > indexBytes || pathLen == 0U || pathLen > kSpiffsPathMax) {
            snprintf(failMsg, failMsgLen, "web pkg: chemin invalide (%u o)", (unsigned)pathLen);
            ok = false;
            break;
        }
        char path[kSpiffsPathMax + 1] = {0};
        memcpy(path, index + cursor, pathLen);
        cursor += pathLen;

        // Un fichier deja identique n'est pas reecrit : sur une mise a jour ou seuls
        // quelques assets changent, cela evite l'essentiel des ecritures SPIFFS, de
        // loin l'operation la plus lente. Une lecture coute une fraction d'une
        // reecriture, et le CRC de l'index sert de comparaison.
        if (webPkgFileMatches_(path, size, crcExpected)) {
            payloadOfs += size;
            ++skipped;
            if ((i % 8U) == 0U) vTaskDelay(1);
            continue;
        }

        if (SPIFFS.exists(kWebPkgFileTmp)) SPIFFS.remove(kWebPkgFileTmp);
        File tmp = SPIFFS.open(kWebPkgFileTmp, FILE_WRITE);
        if (!tmp) {
            snprintf(failMsg, failMsgLen, "web pkg: temporaire impossible");
            ok = false;
            break;
        }
        if (!pkg.seek(payloadBase + payloadOfs)) {
            snprintf(failMsg, failMsgLen, "web pkg: lecture %s impossible", path);
            tmp.close();
            ok = false;
            break;
        }

        uint32_t crc = 0xFFFFFFFFUL;
        uint32_t left = size;
        uint32_t blocks = 0U;
        while (left > 0U) {
            const size_t want = (left > sizeof(buf)) ? sizeof(buf) : left;
            const int rd = pkg.read(buf, want);
            if (rd <= 0) break;
            if (tmp.write(buf, (size_t)rd) != (size_t)rd) {
                left = size;  // force l'echec ci-dessous
                break;
            }
            crc = crc32Update_(crc, buf, (size_t)rd);
            left -= (uint32_t)rd;
            // Le yield doit etre ICI, pas seulement entre deux fichiers : un seul
            // asset pese 100 Ko, soit des dizaines d'ecritures SPIFFS d'affilee.
            // Rendre la main uniquement d'un fichier a l'autre laisse la tache
            // monopoliser son coeur assez longtemps pour reveiller le watchdog.
            if ((++blocks % 4U) == 0U) vTaskDelay(1);
        }
        tmp.close();
        crc ^= 0xFFFFFFFFUL;

        if (left != 0U || crc != crcExpected) {
            snprintf(failMsg, failMsgLen, "web pkg: %s corrompu", path);
            SPIFFS.remove(kWebPkgFileTmp);
            ok = false;
            break;
        }

        // Bascule : SPIFFS refuse un rename vers un nom existant, d'ou le retrait
        // prealable. La fenetre est de quelques millisecondes, et le temporaire reste
        // sur place si elle est interrompue.
        if (SPIFFS.exists(path)) SPIFFS.remove(path);
        if (!SPIFFS.rename(kWebPkgFileTmp, path)) {
            snprintf(failMsg, failMsgLen, "web pkg: bascule de %s impossible", path);
            ok = false;
            break;
        }

        payloadOfs += size;
        ++replaced;
        vTaskDelay(1);
    }

    free(index);
    pkg.close();
    if (ok) {
        LOGI("Paquet web installe : %u remplaces, %u deja a jour", (unsigned)replaced, (unsigned)skipped);
    }
    return ok;
}

bool FirmwareUpdateModule::runWebPackageUpdate_(NetworkClient* stream,
                                                int32_t contentLength,
                                                char* failMsg,
                                                size_t failMsgLen)
{
    // Telechargement serveur web actif : il ne touche a rien, et l'interface peut
    // suivre la progression. Seule l'extraction, courte, impose la mise en pause.
    if (!webPkgDownload_(stream, contentLength, failMsg, failMsgLen)) return false;

    uint16_t fileCount = 0U;
    uint32_t indexBytes = 0U;
    if (!webPkgVerify_(fileCount, indexBytes, failMsg, failMsgLen)) {
        SPIFFS.remove(kWebPkgPath);
        return false;
    }

    // Le serveur web n'est deliberement PAS mis en pause pendant l'extraction. Le
    // chemin par image devait l'etre : il reecrivait la partition sous les pieds du
    // serveur. Ici on remplace des fichiers un par un, et le serveur n'a besoin que
    // des routes d'API pendant l'operation -- elles ne lisent pas le systeme de
    // fichiers. En echange, l'interface continue d'afficher l'avancement, ce qui
    // rend une extraction lente ou bloquee observable au lieu d'etre un ecran fige.
    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, 0, "installing web package");
    const bool ok = webPkgExtract_(fileCount, indexBytes, failMsg, failMsgLen);

    SPIFFS.remove(kWebPkgPath);
    return ok;
}

namespace {

/** En-tete gzip minimal produit par export_binaries.py : mtime=0, FLG=0, sans nom. */
constexpr size_t kGzHeaderBytes = 10U;
/** Queue gzip : CRC-32 puis taille decompressee, en petit-boutiste. */
constexpr size_t kGzTrailerBytes = 8U;

}  // namespace

FirmwareUpdateModule::GzStage FirmwareUpdateModule::spiffsStageGz_(const char* url,
                                                                   uint8_t** bufOut,
                                                                   uint32_t* lenOut,
                                                                   char* failMsg,
                                                                   size_t failMsgLen)
{
    if (!bufOut || !lenOut || !url) {
        snprintf(failMsg, failMsgLen, "spiffs gz: appel invalide");
        return GzStage::Failed;
    }
    *bufOut = nullptr;
    *lenOut = 0U;

    char gzUrl[kUrlLen] = {0};
    const int written = snprintf(gzUrl, sizeof(gzUrl), "%s.gz", url);
    if (written <= 0 || (size_t)written >= sizeof(gzUrl)) {
        // Pas de place pour le suffixe : on ne peut pas demander la variante, donc
        // elle est traitee comme absente et l'image complete prend le relais.
        return GzStage::Absent;
    }

    uint8_t* buf = nullptr;
    size_t bufCapacity = 0U;
    bool retryable = true;

    for (uint8_t attempt = 1U;
         retryable && attempt <= Limits::FirmwareUpdate::Spiffs::GzDownloadRetries;
         ++attempt) {
        failMsg[0] = '\0';
        HTTPClient http;
        configureDownloadHttp_(http);

        if (!http.begin(gzUrl)) {
            snprintf(failMsg, failMsgLen, "serveur HTTP injoignable");
        } else {
            const int code = http.GET();
            if (code == HTTP_CODE_NOT_FOUND) {
                // Serveur de mise a jour qui ne publie pas la variante compressee :
                // ce n'est pas une panne, l'image complete reste servie.
                http.end();
                if (buf) free(buf);
                LOGI("SPIFFS update: pas de variante compressee (404), image complete");
                return GzStage::Absent;
            }
            if (code != HTTP_CODE_OK) {
                snprintf(failMsg, failMsgLen, "erreur HTTP %d sur l'image compressee", code);
                http.end();
            } else {
                const int32_t len = http.getSize();
                if (len <= 0) {
                    snprintf(failMsg, failMsgLen, "taille de l'image compressee inconnue");
                    http.end();
                } else if ((size_t)len > Limits::FirmwareUpdate::Spiffs::GzStageMaxBytes) {
                    snprintf(failMsg,
                             failMsgLen,
                             "image compressee trop grosse (%lu o)",
                             (unsigned long)len);
                    http.end();
                    retryable = false;  // le fichier ne changera pas d'une tentative a l'autre
                } else {
                    if (bufCapacity < (size_t)len) {
                        if (buf) free(buf);
                        buf = (uint8_t*)heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (!buf) buf = (uint8_t*)malloc((size_t)len);
                        bufCapacity = buf ? (size_t)len : 0U;
                    }
                    if (!buf) {
                        snprintf(failMsg,
                                 failMsgLen,
                                 "pas de memoire pour %lu o",
                                 (unsigned long)len);
                        http.end();
                        retryable = false;
                    } else {
                        portENTER_CRITICAL(&lock_);
                        activeTotalBytes_ = (uint32_t)len;
                        activeSentBytes_ = 0;
                        portEXIT_CRITICAL(&lock_);

                        NetworkClient* stream = http.getStreamPtr();
                        uint32_t got = 0U;
                        uint32_t lastReadMs = millis();
                        uint32_t chunkCount = 0U;
                        while (stream && got < (uint32_t)len) {
                            // Un seul point de sortie sur le temps : teste a chaque tour
                            // et pas seulement quand le flux est vide. Une lecture qui
                            // rend 0 alors que `available()` reste positif ferait sinon
                            // tourner la boucle sans fin -- le genre de blocage muet que
                            // cette reprise cherche justement a supprimer.
                            if ((millis() - lastReadMs) >
                                Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                                break;
                            }
                            const size_t avail = stream->available();
                            if (avail == 0U) {
                                if (!stream->connected()) break;
                                delay(1);
                                continue;
                            }
                            size_t want = (size_t)((uint32_t)len - got);
                            if (want > avail) want = avail;
                            const int rd = stream->readBytes((char*)(buf + got), want);
                            if (rd <= 0) {
                                delay(1);
                                continue;
                            }
                            got += (uint32_t)rd;
                            lastReadMs = millis();
                            onProgressChunk_((uint32_t)rd);
                            if ((++chunkCount % 16U) == 0U) vTaskDelay(1);
                        }
                        http.end();

                        if (got == (uint32_t)len) {
                            LOGI("SPIFFS update: image compressee en memoire (%lu o, tentative %u)",
                                 (unsigned long)got,
                                 (unsigned)attempt);
                            *bufOut = buf;
                            *lenOut = got;
                            return GzStage::Ok;
                        }
                        snprintf(failMsg,
                                 failMsgLen,
                                 "telechargement incomplet (%lu/%lu o)",
                                 (unsigned long)got,
                                 (unsigned long)len);
                    }
                }
            }
        }

        LOGW("SPIFFS update: tentative %u/%u echouee : %s",
             (unsigned)attempt,
             (unsigned)Limits::FirmwareUpdate::Spiffs::GzDownloadRetries,
             failMsg[0] ? failMsg : "raison inconnue");
        if (retryable && attempt < Limits::FirmwareUpdate::Spiffs::GzDownloadRetries) {
            delay(Limits::FirmwareUpdate::Spiffs::GzRetryDelayMs);
        }
    }

    if (buf) free(buf);
    return GzStage::Failed;
}

bool FirmwareUpdateModule::spiffsInflateMem_(const uint8_t* gz,
                                             uint32_t gzLen,
                                             uint32_t expectedOut,
                                             bool writeToFlash,
                                             uint32_t* crcOut,
                                             char* failMsg,
                                             size_t failMsgLen)
{
    if (!gz || gzLen <= (uint32_t)(kGzHeaderBytes + kGzTrailerBytes)) {
        snprintf(failMsg, failMsgLen, "spiffs inflate: flux trop court");
        return false;
    }

    // Fenetre deflate de 32 Ko, en PSRAM quand elle est disponible : c'est aussi le
    // tampon de sortie, tinfl y ecrit de maniere circulaire.
    uint8_t* dict = (uint8_t*)heap_caps_malloc(TINFL_LZ_DICT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dict) dict = (uint8_t*)malloc(TINFL_LZ_DICT_SIZE);
    if (!dict) {
        snprintf(failMsg, failMsgLen, "spiffs inflate: no memory for window");
        return false;
    }

    // JAMAIS sur la pile : tinfl_decompressor porte trois tables de Huffman de
    // 3488 octets, soit ~10,7 Ko, alors que la tache fwupdate n'a que 6144 octets
    // de pile (FirmwareUpdateModule.h). Le declarer en local faisait paniquer le
    // firmware des le premier appel -- c'est la cause des trois OTA compressees
    // bloquees du 2026-08-11, longtemps imputee au Task Watchdog ou au decodeur.
    // Prefere la DRAM interne : les tables sont lues en acces aleatoire, la PSRAM
    // ne sert que de repli.
    tinfl_decompressor* decomp =
        (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!decomp) {
        decomp = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!decomp) {
        free(dict);
        snprintf(failMsg, failMsgLen, "spiffs inflate: no memory for decompressor");
        return false;
    }
    tinfl_init(decomp);

    const uint8_t* inPtr = gz + kGzHeaderBytes;
    size_t inLeft = (size_t)gzLen - kGzHeaderBytes - kGzTrailerBytes;
    size_t dictOfs = 0U;
    uint32_t totalOut = 0U;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t chunkCount = 0U;
    bool ok = true;
    bool done = false;

    while (ok && !done) {
        size_t inBytes = inLeft;
        size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
        // L'entree est entiere en memoire : TINFL_FLAG_HAS_MORE_INPUT n'est jamais
        // pose, donc tinfl sait qu'il n'y a pas de suite a attendre et termine de
        // lui-meme. C'est ce qu'entrelacer decompression et lecture reseau rendait
        // fragile -- premiere hypothese de la tentative A.
        const tinfl_status status =
            tinfl_decompress(decomp, inPtr, &inBytes, dict, dict + dictOfs, &outBytes, 0U);
        inPtr += inBytes;
        inLeft -= inBytes;

        if (outBytes > 0U) {
            if (writeToFlash && Update.write(dict + dictOfs, outBytes) != outBytes) {
                snprintf(failMsg,
                         failMsgLen,
                         "spiffs write failed (%u) a %lu o",
                         (unsigned)Update.getError(),
                         (unsigned long)totalOut);
                ok = false;
                break;
            }
            crc = crc32Update_(crc, dict + dictOfs, outBytes);
            totalOut += (uint32_t)outBytes;
            dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1U);
            onProgressChunk_((uint32_t)outBytes);
        }

        if (status == TINFL_STATUS_DONE) {
            done = true;
            break;
        }
        if (status < TINFL_STATUS_DONE) {
            snprintf(failMsg,
                     failMsgLen,
                     "spiffs inflate failed (%d) a %lu o",
                     (int)status,
                     (unsigned long)totalOut);
            ok = false;
            break;
        }
        // Ni consomme ni produit : le flux s'arrete avant sa fin. Sans ce garde-fou,
        // la boucle tournerait a vide.
        if (inBytes == 0U && outBytes == 0U) {
            snprintf(failMsg,
                     failMsgLen,
                     "spiffs inflate: flux tronque a %lu o",
                     (unsigned long)totalOut);
            ok = false;
            break;
        }

        // Le yield suit le travail produit, pas l'entree consommee : 333 Ko rendent
        // 7,9 Mo, donc un seul tour ecrit jusqu'a 32 Ko. 4 tours = 128 Ko, de quoi
        // rendre la main au coeur 0 bien avant le Task Watchdog.
        if ((++chunkCount % 4U) == 0U) vTaskDelay(1);
    }

    free(dict);
    free(decomp);

    if (ok && totalOut != expectedOut) {
        snprintf(failMsg,
                 failMsgLen,
                 "spiffs inflate: %lu o produits, %lu attendus",
                 (unsigned long)totalOut,
                 (unsigned long)expectedOut);
        ok = false;
    }
    if (crcOut) *crcOut = crc ^ 0xFFFFFFFFUL;
    return ok;
}

bool FirmwareUpdateModule::unmountSpiffsForWrite_()
{
    // NE PAS appeler SPIFFS.end() ici -- essaye le 2026-08-18, retire le meme soir.
    //
    // Hypothese testee : le journal d'activite et le serveur web continuant d'
    // utiliser SPIFFS pendant l'ecriture, il fallait demonter pour eviter une
    // collision. Mesure : SPIFFS.end() + Update.write(U_SPIFFS) a fait planter les
    // TROIS ecritures reelles tentees ce soir avec lui (avec et sans le verrou
    // SpiffsAccessLock ci-dessous), toujours au meme endroit -- pas dans du code
    // applicatif, mais dans l'ordonnanceur FreeRTOS lui-meme
    // (_frxt_dispatch -> vTaskSwitchContext -> vPortEnterCritical), ce qui rend le
    // nom de tache accuse par le vidage de crash (async_tcp, EventBus, mqtt --
    // different a chaque fois) sans signification : c'est la tache que
    // l'ordonnanceur s'appretait a activer, pas la cause.
    //
    // Or les deux seules ecritures reelles reussies de tout ce chantier (2026-08-15,
    // consignees dans docs/notes/ota-spiffs-gzip-bilan.md) tournaient sur un code qui
    // ne demontait pas SPIFFS. 0/3 avec demontage, 2/2 sans -- ce n'est pas une
    // preuve de mecanisme, mais une correlation trop nette pour l'ignorer, et
    // ajouter SPIFFS.end() sans l'avoir mesure est exactement l'erreur que ce
    // chantier a deja payee une fois (cf. les cinq correctifs aveugles du
    // 2026-08-11 dans ota-spiffs-reduction-volume.md). Revenir a l'etat mesure.
    //
    // Le verrou reste : il protege une course reelle et distincte (ActivityLogModule
    // et le serveur de fichiers statiques accedant a SPIFFS pendant que l'OTA le
    // fait), meme si elle n'est pas la cause du crash ci-dessus. Sans le demontage,
    // le risque qu'il visait a l'origine (le driver SPIFFS parlant a un cache
    // devenu invalide) redevient theorique -- a rouvrir seulement si une nouvelle
    // mesure le confirme.
    if (!SpiffsAccessLock::acquire(5000U)) {
        LOGE("SPIFFS update: verrou SPIFFS indisponible, ecriture annulee");
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::runSpiffsFromGz_(const uint8_t* gz,
                                            uint32_t gzLen,
                                            uint32_t partitionSize,
                                            bool dryRun,
                                            char* failMsg,
                                            size_t failMsgLen)
{
    if (gzLen < (uint32_t)(kGzHeaderBytes + kGzTrailerBytes)) {
        snprintf(failMsg, failMsgLen, "spiffs gz: flux trop court (%lu o)", (unsigned long)gzLen);
        return false;
    }
    // Le generateur produit un gzip minimal ; tout autre en-tete signalerait une
    // image d'une autre provenance, que ce decodeur lirait de travers.
    if (gz[0] != 0x1FU || gz[1] != 0x8BU || gz[2] != 0x08U) {
        snprintf(failMsg, failMsgLen, "spiffs gz: ce n'est pas un flux gzip");
        return false;
    }
    if (gz[3] != 0x00U) {
        snprintf(failMsg, failMsgLen, "spiffs gz: en-tete non minimal (FLG=0x%02X)", (unsigned)gz[3]);
        return false;
    }

    // La queue gzip porte la taille et l'empreinte de l'image decompressee : les deux
    // sont connues avant d'ecrire le moindre octet. C'est ce qui manquait au chemin en
    // flux, ou l'incoherence de taille n'apparaissait qu'une fois la partition
    // detruite.
    const uint32_t crcExpected = readLe32_(gz + gzLen - 8U);
    const uint32_t isize = readLe32_(gz + gzLen - 4U);
    if (isize != partitionSize) {
        snprintf(failMsg,
                 failMsgLen,
                 "spiffs gz: image %lu o, partition %lu o",
                 (unsigned long)isize,
                 (unsigned long)partitionSize);
        return false;
    }

    // La verification compte pour la premiere moitie de la progression, l'ecriture
    // pour la seconde. En essai a blanc il n'y a que la verification.
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = dryRun ? partitionSize : (partitionSize * 2U);
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    setStatus_(UpdateState::Flashing,
               FirmwareUpdateTarget::Spiffs,
               0,
               dryRun ? "dry run: verification" : "verification");

    // Passe de verification : l'image entiere est decompressee et son empreinte
    // comparee au trailer, sans qu'un octet soit ecrit. Le contenu en place n'est
    // touche qu'ensuite, et seulement si cette passe a reussi.
    const uint32_t startMs = millis();
    uint32_t crc = 0U;
    if (!spiffsInflateMem_(gz, gzLen, partitionSize, false, &crc, failMsg, failMsgLen)) {
        return false;
    }
    if (crc != crcExpected) {
        snprintf(failMsg,
                 failMsgLen,
                 "spiffs gz: empreinte %08lX, attendue %08lX",
                 (unsigned long)crc,
                 (unsigned long)crcExpected);
        return false;
    }
    LOGI("SPIFFS update: image verifiee, %lu o, crc %08lX, %lu ms",
         (unsigned long)partitionSize,
         (unsigned long)crc,
         (unsigned long)(millis() - startMs));

    if (dryRun) return true;

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, 50, "ecriture");
    if (!unmountSpiffsForWrite_()) {
        snprintf(failMsg, failMsgLen, "spiffs busy: verrou indisponible");
        return false;
    }
    if (!Update.begin((size_t)partitionSize, U_SPIFFS)) {
        snprintf(failMsg, failMsgLen, "spiffs begin failed (%u)", (unsigned)Update.getError());
        SPIFFS.begin(false);
        SpiffsAccessLock::release();
        return false;
    }
    if (!spiffsInflateMem_(gz, gzLen, partitionSize, true, nullptr, failMsg, failMsgLen)) {
        // Sans abandon explicite, Update reste engage et refuse le `begin` suivant :
        // une seule ecriture manquee condamnait toute nouvelle tentative jusqu'au
        // redemarrage.
        Update.abort();
        SPIFFS.begin(false);
        SpiffsAccessLock::release();
        return false;
    }
    if (!Update.end()) {
        snprintf(failMsg, failMsgLen, "spiffs end failed (%u)", (unsigned)Update.getError());
        Update.abort();
        SPIFFS.begin(false);
        SpiffsAccessLock::release();
        return false;
    }
    if (!Update.isFinished()) {
        snprintf(failMsg, failMsgLen, "spiffs not finished");
        Update.abort();
        SPIFFS.begin(false);
        SpiffsAccessLock::release();
        return false;
    }

    // Pas de SPIFFS.begin(false) ni de liberation du verrou ici : ESP.restart() suit
    // immediatement (appelant), et le redemarrage remet tout a plat de toute facon.
    LOGI("SPIFFS update: %lu o ecrits depuis l'image compressee", (unsigned long)partitionSize);
    return true;
}

bool FirmwareUpdateModule::runSpiffsUpdate_(const char* url, bool dryRun, char* errOut, size_t errOutLen)
{
    setStatus_(UpdateState::Downloading,
               FirmwareUpdateTarget::Spiffs,
               0,
               dryRun ? "dry run: telechargement" : "downloading");

    const esp_partition_t* spiffsPart =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!spiffsPart) {
        writeSimpleError_(errOut, errOutLen, "spiffs partition not found");
        return false;
    }

    // Paquet web : meme nom de base que l'image, extension .pkg. Prefere a l'image
    // quand il existe -- il ne reecrit pas la partition, donc une coupure ne peut
    // pas laisser l'appareil sans interface. Absent, on retombe sur l'image.
    //
    // Desactive par defaut : sur cible, l'installation ne se termine pas, et la
    // cause n'a pas ete trouvee. Le chemin par image, lui, fonctionne. Repasser
    // FLOW_OTA_SPIFFS_PKG a 1 pour reprendre le diagnostic.
#if FLOW_OTA_SPIFFS_PKG
    if (!dryRun) {
        HTTPClient http;
        configureDownloadHttp_(http);
        char pkgUrl[kUrlLen] = {0};
        const size_t urlLen = strlen(url);
        const bool endsWithBin = (urlLen > 4U) && (strcmp(url + urlLen - 4, ".bin") == 0);
        const int written = endsWithBin
                                ? snprintf(pkgUrl, sizeof(pkgUrl), "%.*s.pkg", (int)(urlLen - 4), url)
                                : -1;
        if (written > 0 && (size_t)written < sizeof(pkgUrl) && http.begin(pkgUrl)) {
            const int pkgCode = http.GET();
            if (pkgCode == HTTP_CODE_OK) {
                LOGI("SPIFFS update: paquet web retenu (%s)", pkgUrl);
                const int32_t pkgLen = http.getSize();
                setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, 0, "downloading web package");
                portENTER_CRITICAL(&lock_);
                activeTotalBytes_ = (pkgLen > 0) ? (uint32_t)pkgLen : 0U;
                activeSentBytes_ = 0;
                portEXIT_CRITICAL(&lock_);

                char pkgFail[128] = {0};
                const bool pkgOk = runWebPackageUpdate_(http.getStreamPtr(), pkgLen, pkgFail, sizeof(pkgFail));
                http.end();
                if (!pkgOk) {
                    LOGE("Paquet web refuse : %s", pkgFail);
                    writeSimpleError_(errOut, errOutLen, pkgFail);
                    return false;
                }
                setStatus_(UpdateState::Rebooting, FirmwareUpdateTarget::Spiffs, 100, "rebooting");
                delay(1800);
                ESP.restart();
                return true;
            }
            LOGI("SPIFFS update: pas de paquet web (HTTP %d), image complete", pkgCode);
            http.end();
        }
    }
#endif

    // L'image padee se compresse a ~4 % : on la ramene entiere en memoire, on ferme
    // la connexion, puis on ecrit sans reseau. Le gain n'est pas seulement le volume
    // transfere : la fenetre pendant laquelle la liaison WiFi doit tenir passe de
    // plusieurs minutes a quelques secondes, et un transfert manque se rejoue sans
    // que rien n'ait ete touche. Decompresser en flux, au contraire, gardait la
    // connexion ouverte pendant toute l'ecriture des 8,26 Mo.
    //
    // L'essai a blanc emprunte toujours ce chemin, meme si FLOW_OTA_SPIFFS_GZIP
    // repassait a 0 : il n'ecrit rien, donc il reste mesurable sur cible sans
    // engager la mise a jour reelle.
    if (dryRun || (FLOW_OTA_SPIFFS_GZIP != 0)) {
        uint8_t* gz = nullptr;
        uint32_t gzLen = 0U;
        char stageFail[128] = {0};
        const GzStage stage = spiffsStageGz_(url, &gz, &gzLen, stageFail, sizeof(stageFail));

        if (stage == GzStage::Failed) {
            // Pas de repli sur l'image complete : si 333 Ko ne passent pas, 8,26 Mo
            // passeront encore moins, et l'echec serait alors destructif.
            LOGE("SPIFFS update: image compressee non ramenee : %s", stageFail);
            writeSimpleError_(errOut, errOutLen, stageFail[0] ? stageFail : "image compressee indisponible");
            return false;
        }

        if (stage == GzStage::Ok) {
            attachWebInterfaceSvcIfNeeded_();
            if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
                webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
            }

            char gzFail[128] = {0};
            const bool gzOk =
                runSpiffsFromGz_(gz, gzLen, spiffsPart->size, dryRun, gzFail, sizeof(gzFail));
            free(gz);

            if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
                webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
            }

            if (!gzOk) {
                LOGE("SPIFFS update: %s", gzFail);
                writeSimpleError_(errOut, errOutLen, gzFail);
                return false;
            }

            if (dryRun) {
                // Le message porte le verdict : c'est lui qu'on lit dans
                // /api/fwupdate/status une fois l'essai termine.
                char okMsg[kMsgLen] = {0};
                snprintf(okMsg,
                         sizeof(okMsg),
                         "dry run ok: %lu o produits, crc conforme",
                         (unsigned long)spiffsPart->size);
                setStatus_(UpdateState::Done, FirmwareUpdateTarget::Spiffs, 100, okMsg);
                LOGI("SPIFFS dry run termine sans ecriture");
                return true;
            }

            setStatus_(UpdateState::Rebooting, FirmwareUpdateTarget::Spiffs, 100, "rebooting");
            delay(1800);
            ESP.restart();
            return true;
        }

        if (dryRun) {
            writeSimpleError_(errOut, errOutLen, "dry run: pas de variante compressee (.gz)");
            return false;
        }
    }

    HTTPClient http;
    configureDownloadHttp_(http);
    if (!http.begin(url)) {
        writeHttpBeginFailedError_("fichier de mise a jour", url, errOut, errOutLen);
        return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        writeHttpCodeFailedError_("fichier de mise a jour", url, http, code, errOut, errOutLen);
        http.end();
        return false;
    }

    const int32_t contentLength = http.getSize();

    // Une image SPIFFS couvre toujours toute sa partition (mkspiffs pade le reste).
    // Une taille differente signifie que l'image a ete produite pour une autre table
    // de partitions : l'ecrire donnerait un systeme de fichiers illisible, donc une
    // interface web perdue jusqu'au prochain flash USB. On refuse avant d'ecrire.
    if (contentLength > 0 && (uint32_t)contentLength != spiffsPart->size) {
        char sizeMsg[128] = {0};
        snprintf(sizeMsg,
                 sizeof(sizeMsg),
                 "spiffs size mismatch: image %lu o, partition %lu o",
                 (unsigned long)contentLength,
                 (unsigned long)spiffsPart->size);
        LOGE("%s", sizeMsg);
        writeSimpleError_(errOut, errOutLen, sizeMsg);
        http.end();
        return false;
    }

    setStatus_(UpdateState::Flashing, FirmwareUpdateTarget::Spiffs, 0, "flashing spiffs");
    portENTER_CRITICAL(&lock_);
    activeTotalBytes_ = (contentLength > 0) ? (uint32_t)contentLength : 0U;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    attachWebInterfaceSvcIfNeeded_();
    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, true);
    }
    const bool spiffsLockHeld = unmountSpiffsForWrite_();

    char failMsg[128] = {0};
    if (!spiffsLockHeld) {
        snprintf(failMsg, sizeof(failMsg), "spiffs busy: verrou indisponible");
    }
    const size_t beginSize = (contentLength > 0) ? (size_t)contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
    if (failMsg[0] == '\0' && !Update.begin(beginSize, U_SPIFFS)) {
        snprintf(failMsg, sizeof(failMsg), "spiffs begin failed (%u)", (unsigned)Update.getError());
    }

    auto* stream = http.getStreamPtr();
    uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
    int32_t remaining = contentLength;
    uint32_t lastReadMs = millis();
    uint32_t chunkCount = 0;
    if (failMsg[0] == '\0') {
        while (http.connected() && (contentLength <= 0 || remaining > 0)) {
            const size_t avail = stream ? stream->available() : 0;
            if (avail == 0U) {
                if (contentLength <= 0 && stream && !stream->connected()) {
                    break;
                }
                if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                    snprintf(failMsg, sizeof(failMsg), "spiffs stream timeout");
                    break;
                }
                delay(1);
                continue;
            }

            const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
            const int rd = stream->readBytes((char*)buf, toRead);
            if (rd <= 0) {
                delay(1);
                continue;
            }
            lastReadMs = millis();

            const size_t wr = Update.write(buf, (size_t)rd);
            if (wr != (size_t)rd) {
                snprintf(failMsg, sizeof(failMsg), "spiffs write failed (%u)", (unsigned)Update.getError());
                break;
            }

            onProgressChunk_((uint32_t)wr);

            // Un flux rapide/continu ne passe jamais par le delay(1) ci-dessus (avail==0) :
            // sans ce yield périodique, cette tâche (épinglée coeur 0, cf. taskCore())
            // affame l'idle task de ce coeur et déclenche le Task Watchdog en plein transfert.
            if ((++chunkCount % 16U) == 0U) {
                vTaskDelay(1);
            }

            if (contentLength > 0) {
                remaining -= rd;
                if (remaining <= 0) break;
            }
        }
    }
    http.end();

    if (failMsg[0] == '\0' && contentLength > 0 && remaining > 0) {
        snprintf(failMsg, sizeof(failMsg), "incomplete download");
    }
    if (failMsg[0] == '\0' && !Update.end()) {
        snprintf(failMsg, sizeof(failMsg), "spiffs end failed (%u)", (unsigned)Update.getError());
    }
    if (failMsg[0] == '\0' && !Update.isFinished()) {
        snprintf(failMsg, sizeof(failMsg), "spiffs not finished");
    }

    if (webInterfaceSvc_ && webInterfaceSvc_->setPaused) {
        webInterfaceSvc_->setPaused(webInterfaceSvc_->ctx, false);
    }

    if (failMsg[0] != '\0') {
        // Symetrique de unmountSpiffsForWrite_() : remonter et liberer seulement si
        // le verrou avait ete pris, sinon SPIFFS est deja dans l'etat ou l'echec a
        // ete detecte (jamais demonte).
        if (spiffsLockHeld) {
            SPIFFS.begin(false);
            SpiffsAccessLock::release();
        }
        writeSimpleError_(errOut, errOutLen, failMsg);
        return false;
    }

    // Pas de remontage ni de liberation du verrou : ESP.restart() suit
    // immediatement, et le redemarrage remet tout a plat de toute facon. Rien a
    // persister par ailleurs : l'image qui vient d'etre ecrite porte sa propre
    // version dans /fsver.j, lue au boot par FilesystemVersion.
    setStatus_(UpdateState::Rebooting, FirmwareUpdateTarget::Spiffs, 100, "rebooting");
    delay(1800);
    ESP.restart();
    return true;
}

bool FirmwareUpdateModule::runJob_(const UpdateJob& job)
{
    if (!netAccessSvc_ && services_) {
        netAccessSvc_ = services_->get<NetworkAccessService>(ServiceId::NetworkAccess);
    }
    bool netReady = false;
    if (netAccessSvc_ && netAccessSvc_->isWebReachable) {
        netReady = netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
    } else if (wifiSvc_ && wifiSvc_->isConnected) {
        netReady = wifiSvc_->isConnected(wifiSvc_->ctx);
    }
    if (!netReady) {
        setError_(job.target, "network not connected");
        return false;
    }

    char err[128] = {0};
    bool ok = false;
    switch (job.target) {
        case FirmwareUpdateTarget::Waveshare:
            ok = runWaveshareUpdate_(job.url, err, sizeof(err));
            break;
        case FirmwareUpdateTarget::Nextion:
            ok = runNextionUpdate_(job.url, err, sizeof(err));
            break;
        case FirmwareUpdateTarget::Spiffs:
            ok = runSpiffsUpdate_(job.url, job.dryRun, err, sizeof(err));
            break;
        default:
            snprintf(err, sizeof(err), "unsupported target");
            ok = false;
            break;
    }

    if (!ok) {
        setError_(job.target, err[0] ? err : "update failed");
        LOGE("Update failed target=%s reason=%s", targetStr_(job.target), err[0] ? err : "unknown");
        return false;
    }

    LOGI("Update done target=%s", targetStr_(job.target));
    return true;
}

bool FirmwareUpdateModule::cmdStatus_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;
    if (!self->statusJson_(reply, replyLen)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.status")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    return true;
}

bool FirmwareUpdateModule::cmdWaveshare_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Waveshare, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.waveshare")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"waveshare\"}");
    return true;
}

bool FirmwareUpdateModule::cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Nextion, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.nextion")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"nextion\"}");
    return true;
}

bool FirmwareUpdateModule::cmdNextionReboot_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char err[120] = {0};
    if (!self->queueNextionReboot_(err, sizeof(err))) {
        sanitizeJsonString_(err);
        const int wrote = snprintf(reply,
                                   replyLen,
                                   "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fw.nextion.reboot\",\"msg\":\"%s\"}}",
                                   err[0] ? err : "failed");
        return wrote > 0 && (size_t)wrote < replyLen;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"nextion_reboot\"}");
    return true;
}

bool FirmwareUpdateModule::cmdSpiffs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    FirmwareUpdateModule* self = static_cast<FirmwareUpdateModule*>(userCtx);
    if (!self) return false;

    char url[kUrlLen] = {0};
    const char* explicitUrl = self->parseUrlArg_(req, url, sizeof(url)) ? url : nullptr;
    const bool dryRun = self->parseDryRunArg_(req);
    char err[120] = {0};
    const bool queued = dryRun
                            ? self->startSpiffsDryRun_(explicitUrl, err, sizeof(err))
                            : self->startUpdate_(FirmwareUpdateTarget::Spiffs, explicitUrl, err, sizeof(err));
    if (!queued) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.spiffs")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply,
             replyLen,
             "{\"ok\":true,\"queued\":true,\"target\":\"spiffs\",\"dry_run\":%s}",
             dryRun ? "true" : "false");
    return true;
}

void FirmwareUpdateModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    services_ = &services;
    cfgStore_ = &cfg;
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    webInterfaceSvc_ = services.get<WebInterfaceService>(ServiceId::WebInterface);
    flowCfgSvc_ = services.get<FlowCfgRemoteService>(ServiceId::FlowCfg);
    hmiSvc_ = services.get<HmiService>(ServiceId::Hmi);

    cfg.registerVar(updateHostVar_);
    cfg.registerVar(updatePathVar_);

    if (!services.add(ServiceId::FirmwareUpdate, &firmwareUpdateSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::FirmwareUpdate));
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.status", &FirmwareUpdateModule::cmdStatus_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.waveshare", &FirmwareUpdateModule::cmdWaveshare_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.nextion", &FirmwareUpdateModule::cmdNextion_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.nextion.reboot", &FirmwareUpdateModule::cmdNextionReboot_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "fw.update.spiffs", &FirmwareUpdateModule::cmdSpiffs_, this);
    }

    setStatus_(UpdateState::Idle, FirmwareUpdateTarget::Waveshare, 0, "idle");
    LOGI("Firmware updater ready");
}

void FirmwareUpdateModule::loop()
{
    UpdateJob job{};
    bool runNextionReboot = false;

    portENTER_CRITICAL(&lock_);
    if (busy_) {
        portEXIT_CRITICAL(&lock_);
        vTaskDelay(pdMS_TO_TICKS(60));
        return;
    }
    if (nextionRebootQueued_) {
        busy_ = true;
        nextionRebootQueued_ = false;
        runNextionReboot = true;
    } else if (queuedJob_.pending) {
        busy_ = true;
        job = queuedJob_;
        queuedJob_.pending = false;
    } else {
        portEXIT_CRITICAL(&lock_);
        vTaskDelay(pdMS_TO_TICKS(60));
        return;
    }
    portEXIT_CRITICAL(&lock_);

    if (runNextionReboot) {
        char err[128] = {0};
        if (!runNextionReboot_(err, sizeof(err))) {
            LOGE("Nextion reboot failed reason=%s", err[0] ? err : "unknown");
        } else {
            LOGI("Nextion reboot done");
        }
    } else {
        runJob_(job);
    }

    portENTER_CRITICAL(&lock_);
    busy_ = false;
    activeTotalBytes_ = 0;
    activeSentBytes_ = 0;
    portEXIT_CRITICAL(&lock_);

    vTaskDelay(pdMS_TO_TICKS(20));
}
