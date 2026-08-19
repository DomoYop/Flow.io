#pragma once
/**
 * @file FirmwareUpdateModule.h
 * @brief Firmware updater.
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/ConfigTypes.h"
#include "Core/CommandRegistry.h"

struct BoardSpec;
class NetworkClient;  // flux TCP rendu par HTTPClient::getStreamPtr()

class FirmwareUpdateModule : public Module {
public:
    explicit FirmwareUpdateModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::FirmwareUpdate; }
    const char* taskName() const override { return "fwupdate"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 6144;
#else
        return 8192;
#endif
    }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint32_t startDelayMs() const override {
#if defined(FLOW_PROFILE_WAVESHARE)
        return 6000U;
#else
        return 0U;
#endif
    }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::WebInterface;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr size_t kUrlLen = 192;
    static constexpr size_t kMsgLen = 120;

    enum class UpdateState : uint8_t {
        Idle = 0,
        Queued,
        Downloading,
        Flashing,
        Rebooting,
        Done,
        Error
    };

    struct UpdateJob {
        bool pending = false;
        /** Essai a blanc : tout se deroule sauf l'ecriture flash et le redemarrage. */
        bool dryRun = false;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::Waveshare;
        char url[kUrlLen] = {0};
    };

    struct UpdateStatus {
        UpdateState state = UpdateState::Idle;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::Waveshare;
        uint8_t progress = 0;
        uint32_t updatedAtMs = 0;
        char msg[kMsgLen] = {0};
    };

    struct ConfigData {
        char updateHost[64] = "";
        char updatePath[64] = "/binary";
    } cfgData_{};

    ConfigVariable<char, 2> updateHostVar_{
        NVS_KEY("up_host"), "update_host", "fwupdate",
        ConfigType::CharArray, cfgData_.updateHost, ConfigPersistence::Persistent, sizeof(cfgData_.updateHost)
    };
    ConfigVariable<char, 2> updatePathVar_{
        NVS_KEY("up_base_path"), "update_path", "fwupdate",
        ConfigType::CharArray, cfgData_.updatePath, ConfigPersistence::Persistent, sizeof(cfgData_.updatePath)
    };
    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;
    const HmiService* hmiSvc_ = nullptr;

    int8_t flowIoEnablePin_ = -1;
    int8_t nextionRxPin_ = -1;
    int8_t nextionTxPin_ = -1;
    int8_t nextionRebootPin_ = -1;
    uint32_t nextionUploadBaud_ = 115200U;

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    UpdateJob queuedJob_{};
    UpdateStatus status_{};
    bool nextionRebootQueued_ = false;
    bool busy_ = false;
    bool hmiOtaActive_ = false;
    uint32_t activeTotalBytes_ = 0;
    uint32_t activeSentBytes_ = 0;

    static bool cmdStatus_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdWaveshare_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextionReboot_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSpiffs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool startUpdate_(FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool startSpiffsDryRun_(const char* url, char* errOut, size_t errOutLen);
    /** @brief Mise en file commune : `startUpdate_` et `startSpiffsDryRun_` n'en different que par le drapeau. */
    bool queueJob_(FirmwareUpdateTarget target, const char* url, bool dryRun, char* errOut, size_t errOutLen);
    bool queueNextionReboot_(char* errOut, size_t errOutLen);
    bool statusJson_(char* out, size_t outLen);
    bool isBusy_();
    bool configJson_(char* out, size_t outLen) const;
    bool checkManifestJsonStream_(Print& out, char* errOut, size_t errOutLen);
    bool manifestUrl_(char* out, size_t outLen, char* errOut, size_t errOutLen);
    bool setConfig_(const char* updateHost,
                    const char* updatePath,
                    char* errOut,
                    size_t errOutLen);
    bool runJob_(const UpdateJob& job);
    bool runWaveshareUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionReboot_(char* errOut, size_t errOutLen);
    bool runSpiffsUpdate_(const char* url, bool dryRun, char* errOut, size_t errOutLen);
    /**
     * @brief Installe un paquet web : telechargement, verification, remplacement.
     *
     * Variante non destructive de l'OTA SPIFFS. L'image classique reecrit toute la
     * partition (8,3 Mo pour ~320 Ko utiles) : une coupure en cours d'ecriture laisse
     * un systeme de fichiers illisible, donc plus d'interface web. Ici le paquet est
     * d'abord depose en entier dans le systeme de fichiers, verifie, et seulement
     * ensuite extrait fichier par fichier. Tant que la verification n'a pas reussi,
     * le contenu en place n'est pas touche.
     */
    bool runWebPackageUpdate_(NetworkClient* stream,
                              int32_t contentLength,
                              char* failMsg,
                              size_t failMsgLen);
    /** @brief Depose le paquet dans le systeme de fichiers, sans rien remplacer. */
    bool webPkgDownload_(NetworkClient* stream, int32_t contentLength, char* failMsg, size_t failMsgLen);
    /** @brief Verifie l'en-tete et l'empreinte du paquet depose. */
    bool webPkgVerify_(uint16_t& fileCountOut, uint32_t& indexBytesOut, char* failMsg, size_t failMsgLen);
    /** @brief Remplace les fichiers un par un, chacun verifie avant bascule. */
    bool webPkgExtract_(uint16_t fileCount, uint32_t indexBytes, char* failMsg, size_t failMsgLen);
    /** @brief true si le fichier en place a deja la taille et l'empreinte voulues. */
    static bool webPkgFileMatches_(const char* path, uint32_t size, uint32_t crcExpected);

    /**
     * @brief Prend le verrou SPIFFS exclusif puis demonte, avant de reecrire la partition.
     *
     * Le journal d'activite y ecrit en fonctionnement et le serveur web y lit ses
     * fichiers, chacun depuis sa propre tache : sans exclusion mutuelle, laisser le
     * driver monte pendant l'effacement corrompt le systeme de fichiers et fait
     * paniquer la tache qui s'en sert au mauvais moment. Voir Core/SpiffsAccessLock.h.
     *
     * @return false si le verrou n'a pas pu etre acquis : rien n'a ete touche,
     *         l'appelant doit abandonner l'ecriture proprement.
     */
    bool unmountSpiffsForWrite_();

    /** @brief Issue de la mise en tampon de l'image compressee. */
    enum class GzStage : uint8_t {
        Absent = 0,  ///< Pas de variante compressee publiee : l'image complete prend le relais.
        Failed,      ///< Variante presente mais non ramenee : erreur franche, rien n'est ecrit.
        Ok
    };

    /**
     * @brief Ramene `<url>.gz` en PSRAM, puis ferme la connexion.
     *
     * C'est le coeur de la reduction d'exposition au reseau : 333 Ko au lieu de
     * 8,26 Mo, et surtout plus aucune connexion ouverte pendant l'ecriture flash.
     * Tant que ce transfert n'a pas abouti, rien n'a ete touche, donc il est
     * reessaye sans risque.
     *
     * @param bufOut Tampon alloue par la fonction, a liberer par l'appelant.
     */
    GzStage spiffsStageGz_(const char* url,
                           uint8_t** bufOut,
                           uint32_t* lenOut,
                           char* failMsg,
                           size_t failMsgLen);
    /**
     * @brief Verifie puis, si demande, ecrit l'image compressee tenue en memoire.
     *
     * Deroule une passe de verification complete (decompression + CRC-32 compare au
     * trailer gzip) avant d'ecrire quoi que ce soit. En essai a blanc, s'arrete apres
     * cette passe.
     */
    bool runSpiffsFromGz_(const uint8_t* gz,
                          uint32_t gzLen,
                          uint32_t partitionSize,
                          bool dryRun,
                          char* failMsg,
                          size_t failMsgLen);
    /**
     * @brief Decompresse l'image tenue en memoire, avec ou sans ecriture flash.
     *
     * L'entree etant complete, `TINFL_FLAG_HAS_MORE_INPUT` n'est jamais positionne :
     * la machine a etats entrelacee avec les lectures reseau, et la classe de bugs
     * qui allait avec, disparaissent.
     *
     * @param crcOut CRC-32 des octets produits, convention zlib.
     */
    bool spiffsInflateMem_(const uint8_t* gz,
                           uint32_t gzLen,
                           uint32_t expectedOut,
                           bool writeToFlash,
                           uint32_t* crcOut,
                           char* failMsg,
                           size_t failMsgLen);
    bool resolveUrl_(FirmwareUpdateTarget target,
                     const char* explicitUrl,
                     char* out,
                     size_t outLen,
                     char* errOut,
                     size_t errOutLen) const;
    bool resolveUpdateUrl_(const char* path,
                           char* out,
                           size_t outLen,
                           char* errOut,
                           size_t errOutLen) const;
    bool parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const;
    /** @brief Drapeau `dry` des arguments de commande : essai a blanc, sans ecriture. */
    bool parseDryRunArg_(const CommandRequest& req) const;
    void setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg);
    void setError_(FirmwareUpdateTarget target, const char* msg);
    void setHmiOtaCondition_(bool active);
    void onProgressChunk_(uint32_t chunkBytes);
    void attachWebInterfaceSvcIfNeeded_();
    void attachFlowCfgSvcIfNeeded_();
    bool setFlowCfgPaused_(bool paused);
    static const char* stateStr_(UpdateState s);
    static const char* targetStr_(FirmwareUpdateTarget t);

    FirmwareUpdateService firmwareUpdateSvc_{
        ServiceBinding::bind<&FirmwareUpdateModule::startUpdate_>,
        ServiceBinding::bind<&FirmwareUpdateModule::statusJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::isBusy_>,
        ServiceBinding::bind<&FirmwareUpdateModule::configJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::checkManifestJsonStream_>,
        ServiceBinding::bind<&FirmwareUpdateModule::manifestUrl_>,
        ServiceBinding::bind<&FirmwareUpdateModule::setConfig_>,
        ServiceBinding::bind<&FirmwareUpdateModule::startSpiffsDryRun_>,
        this
    };
};
