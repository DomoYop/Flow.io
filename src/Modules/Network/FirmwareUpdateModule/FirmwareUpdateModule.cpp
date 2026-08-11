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
// (FLOW_OTA_SPIFFS_PKG). Aucune n'aboutit sur cible a ce jour ; les deux sont donc
// desactivees et le firmware utilise l'image complete, qui fonctionne.
// Voir docs/notes/ota-spiffs-reduction-volume.md
#ifndef FLOW_OTA_SPIFFS_GZIP
#define FLOW_OTA_SPIFFS_GZIP 0
#endif
#ifndef FLOW_OTA_SPIFFS_PKG
#define FLOW_OTA_SPIFFS_PKG 0
#endif

#include "App/BuildFlags.h"
#include "Board/BoardSpec.h"
#include "Core/ErrorCodes.h"
#include "Core/FirmwareVersion.h"
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

bool FirmwareUpdateModule::startUpdate_(FirmwareUpdateTarget target,
                                        const char* url,
                                        char* errOut,
                                        size_t errOutLen)
{
    UpdateJob job{};
    job.target = target;
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

    setStatus_(UpdateState::Queued, target, 0, "queued");
    LOGI("Update queued target=%s url=%s", targetStr_(target), job.url);
    return true;
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

bool FirmwareUpdateModule::runSpiffsInflate_(NetworkClient* stream,
                                             int32_t contentLength,
                                             uint32_t expectedOut,
                                             char* failMsg,
                                             size_t failMsgLen)
{
    if (!stream) {
        snprintf(failMsg, failMsgLen, "spiffs inflate: no stream");
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

    tinfl_decompressor decomp;
    tinfl_init(&decomp);

    uint8_t in[Limits::FirmwareUpdate::Http::StreamChunkBytes];
    size_t inFill = 0U;   // octets valides dans `in`
    size_t inPos = 0U;    // position de lecture dans `in`
    size_t dictOfs = 0U;
    uint32_t totalOut = 0U;
    uint32_t chunkCount = 0U;
    size_t headerLeft = 10U;  // en-tete gzip fixe : mtime=0 et FLG=0 cote generateur
    bool headerChecked = false;
    int32_t remaining = contentLength;
    uint32_t lastReadMs = millis();
    bool inputDone = false;  // plus rien a attendre du reseau
    bool ok = true;
    bool done = false;

    // tinfl doit encore etre appele une fois l'entree epuisee : c'est ce dernier
    // appel, avec HAS_MORE_INPUT retire, qui vide sa fenetre et rend DONE. Une boucle
    // qui ne l'appelle que tant qu'il reste des octets a consommer attend donc
    // indefiniment des donnees qui ne viendront plus.
    while (ok && !done) {
        if (inPos >= inFill && !inputDone) {
            const size_t avail = stream->available();
            if (avail == 0U) {
                const bool noMoreExpected = (contentLength > 0) ? (remaining <= 0) : !stream->connected();
                if (noMoreExpected) {
                    inputDone = true;
                } else if ((millis() - lastReadMs) > Limits::FirmwareUpdate::Http::StreamReadTimeoutMs) {
                    snprintf(failMsg, failMsgLen, "spiffs stream timeout");
                    ok = false;
                    break;
                } else {
                    delay(1);
                    continue;
                }
            } else {
                const size_t toRead = (avail > sizeof(in)) ? sizeof(in) : avail;
                const int rd = stream->readBytes((char*)in, toRead);
                if (rd <= 0) {
                    delay(1);
                    continue;
                }
                lastReadMs = millis();
                inFill = (size_t)rd;
                inPos = 0U;
                if (contentLength > 0) remaining -= rd;
                onProgressChunk_((uint32_t)rd);

                if (!headerChecked && inFill >= 3U) {
                    headerChecked = true;
                    // Le generateur produit un gzip minimal ; tout autre en-tete
                    // signalerait une image d'une autre provenance, que ce decodeur
                    // lirait de travers.
                    if (in[0] != 0x1FU || in[1] != 0x8BU || in[2] != 0x08U) {
                        snprintf(failMsg, failMsgLen, "spiffs inflate: not a gzip stream");
                        ok = false;
                        break;
                    }
                }
                if (headerLeft > 0U) {
                    const size_t skip = (headerLeft < inFill) ? headerLeft : inFill;
                    inPos += skip;
                    headerLeft -= skip;
                }
            }
        }

        size_t inBytes = inFill - inPos;
        size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
        const mz_uint32 flags = inputDone ? 0U : TINFL_FLAG_HAS_MORE_INPUT;
        const tinfl_status status =
            tinfl_decompress(&decomp, in + inPos, &inBytes, dict, dict + dictOfs, &outBytes, flags);
        inPos += inBytes;

        if (outBytes > 0U) {
            if (Update.write(dict + dictOfs, outBytes) != outBytes) {
                snprintf(failMsg, failMsgLen, "spiffs write failed (%u)", (unsigned)Update.getError());
                ok = false;
                break;
            }
            totalOut += (uint32_t)outBytes;
            dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1U);
        }

        if (status == TINFL_STATUS_DONE) {
            done = true;
            break;
        }
        if (status < TINFL_STATUS_DONE) {
            snprintf(failMsg, failMsgLen, "spiffs inflate failed (%d)", (int)status);
            ok = false;
            break;
        }
        // Entree epuisee et plus rien a lire, sans que le flux se soit termine :
        // l'image est tronquee. Sans ce garde-fou, la boucle tournerait a vide.
        if (inputDone && inBytes == 0U && outBytes == 0U) {
            snprintf(failMsg, failMsgLen, "spiffs inflate: truncated stream");
            ok = false;
            break;
        }

        // Le yield doit suivre le travail produit, pas les lectures reseau : 329 Ko
        // d'entree rendent 7,9 Mo, donc la decompression ecrit des megaoctets entre
        // deux lectures. Indexe sur le reseau, ce yield laisserait la tache monopoliser
        // son coeur assez longtemps pour reveiller le Task Watchdog -- le plantage
        // corrige en juillet, reintroduit par une autre porte. 8 tours = ~256 Ko.
        if ((++chunkCount % 8U) == 0U) vTaskDelay(1);
    }

    free(dict);

    if (ok && totalOut != expectedOut) {
        snprintf(failMsg,
                 failMsgLen,
                 "spiffs inflate: %lu o produits, %lu attendus",
                 (unsigned long)totalOut,
                 (unsigned long)expectedOut);
        ok = false;
    }
    return ok;
}

bool FirmwareUpdateModule::runSpiffsUpdate_(const char* url, char* errOut, size_t errOutLen)
{
    setStatus_(UpdateState::Downloading, FirmwareUpdateTarget::Spiffs, 0, "downloading");

    const esp_partition_t* spiffsPart =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!spiffsPart) {
        writeSimpleError_(errOut, errOutLen, "spiffs partition not found");
        return false;
    }

    // L'image est padee jusqu'a la taille de la partition : son .gz pese ~4 % du
    // .bin. On le demande d'abord et on retombe sur le .bin s'il est absent, ce qui
    // evite de declarer une seconde entree dans le manifeste et garde les serveurs
    // de mise a jour existants utilisables tels quels.
    //
    // Desactive par defaut : le transfert compresse n'a pas encore abouti sur cible
    // (l'OTA reste bloque sans que le firmware soit en train de flasher, cause non
    // identifiee a ce jour). Le chemin non compresse, lui, fonctionne. Repasser
    // FLOW_OTA_SPIFFS_GZIP a 1 pour reprendre le diagnostic.
    HTTPClient http;
    configureDownloadHttp_(http);
    bool compressed = false;
    int code = 0;

    // Paquet web : meme nom de base que l'image, extension .pkg. Prefere a l'image
    // quand il existe -- il ne reecrit pas la partition, donc une coupure ne peut
    // pas laisser l'appareil sans interface. Absent, on retombe sur l'image.
    //
    // Desactive par defaut : sur cible, l'installation ne se termine pas, et la
    // cause n'a pas ete trouvee. Le chemin par image, lui, fonctionne. Repasser
    // FLOW_OTA_SPIFFS_PKG a 1 pour reprendre le diagnostic.
#if FLOW_OTA_SPIFFS_PKG
    {
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

#if FLOW_OTA_SPIFFS_GZIP
    char gzUrl[kUrlLen] = {0};
    const int gzWritten = snprintf(gzUrl, sizeof(gzUrl), "%s.gz", url);
    if (gzWritten > 0 && (size_t)gzWritten < sizeof(gzUrl)) {
        if (http.begin(gzUrl)) {
            code = http.GET();
            if (code == HTTP_CODE_OK) {
                compressed = true;
                LOGI("SPIFFS update: variante compressee retenue (%s)", gzUrl);
            } else {
                LOGI("SPIFFS update: pas de variante compressee (HTTP %d), image brute", code);
                http.end();
            }
        }
    }
#endif

    if (!compressed) {
        configureDownloadHttp_(http);
        if (!http.begin(url)) {
            writeHttpBeginFailedError_("fichier de mise a jour", url, errOut, errOutLen);
            return false;
        }
        code = http.GET();
        if (code != HTTP_CODE_OK) {
            writeHttpCodeFailedError_("fichier de mise a jour", url, http, code, errOut, errOutLen);
            http.end();
            return false;
        }
    }

    const int32_t contentLength = http.getSize();

    // Une image SPIFFS couvre toujours toute sa partition (mkspiffs pade le reste).
    // Une taille differente signifie que l'image a ete produite pour une autre table
    // de partitions : l'ecrire donnerait un systeme de fichiers illisible, donc une
    // interface web perdue jusqu'au prochain flash USB. On refuse avant d'ecrire.
    // Comprime, c'est le total decompresse qui est verifie, en fin de flux.
    if (!compressed && contentLength > 0 && (uint32_t)contentLength != spiffsPart->size) {
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

    char failMsg[128] = {0};
    // Comprime, la taille ecrite est celle de la partition, pas celle du telechargement.
    const size_t beginSize = compressed
                                 ? (size_t)spiffsPart->size
                                 : ((contentLength > 0) ? (size_t)contentLength : (size_t)UPDATE_SIZE_UNKNOWN);
    if (!Update.begin(beginSize, U_SPIFFS)) {
        snprintf(failMsg, sizeof(failMsg), "spiffs begin failed (%u)", (unsigned)Update.getError());
    }

    auto* stream = http.getStreamPtr();
    if (failMsg[0] == '\0' && compressed) {
        (void)runSpiffsInflate_(stream, contentLength, spiffsPart->size, failMsg, sizeof(failMsg));
    }

    uint8_t buf[Limits::FirmwareUpdate::Http::StreamChunkBytes];
    int32_t remaining = contentLength;
    uint32_t lastReadMs = millis();
    uint32_t chunkCount = 0;
    if (failMsg[0] == '\0' && !compressed) {
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

    // Le controle de completude du mode compresse porte sur le total decompresse,
    // deja verifie par runSpiffsInflate_ contre la taille de la partition.
    if (failMsg[0] == '\0' && !compressed && contentLength > 0 && remaining > 0) {
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
        writeSimpleError_(errOut, errOutLen, failMsg);
        return false;
    }

    // Rien a persister : l'image qui vient d'etre ecrite porte sa propre version
    // dans /fsver.j, lue au boot par FilesystemVersion.
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
            ok = runSpiffsUpdate_(job.url, err, sizeof(err));
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
    char err[120] = {0};
    if (!self->startUpdate_(FirmwareUpdateTarget::Spiffs, explicitUrl, err, sizeof(err))) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "fw.update.spiffs")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true,\"target\":\"spiffs\"}");
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
