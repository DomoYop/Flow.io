#pragma once
/**
 * @file IIO.h
 * @brief Unified I/O service interfaces.
 */
#include <stdint.h>
#include <stddef.h>

/** Numeric endpoint identifier shared across modules. */
typedef uint16_t IoId;
/** Monotonic sequence number for I/O cycles. */
typedef uint32_t IoSeq;

/** Invalid endpoint identifier sentinel. */
constexpr IoId IO_ID_INVALID = 0xFFFFu;
/** Reserved base for digital outputs. */
constexpr IoId IO_ID_DO_BASE = 0;
/** Reserved base for digital inputs. */
constexpr IoId IO_ID_DI_BASE = 64;
/** Reserved base for analog inputs. */
constexpr IoId IO_ID_AI_BASE = 192;
/** Hard upper bound used by static service implementations. */
constexpr uint8_t IO_SVC_MAX_ENDPOINTS = 40;
/** Max length for display names in metadata payloads. */
constexpr size_t IO_NAME_MAX_LEN = 24;
/** Max number of changed ids tracked per cycle. */
constexpr uint8_t IO_MAX_CHANGED_IDS = 40;

/** Result code for IOServiceV2 calls. */
enum IoStatus : uint8_t {
    IO_OK = 0,
    IO_ERR_INVALID_ARG = 1,
    IO_ERR_UNKNOWN_ID = 2,
    IO_ERR_TYPE_MISMATCH = 3,
    IO_ERR_READ_ONLY = 4,
    IO_ERR_NOT_READY = 5,
    IO_ERR_HW = 6
};

/** Runtime value type transported by I/O APIs. */
enum IoValueType : uint8_t {
    IO_VAL_BOOL = 0,
    IO_VAL_FLOAT = 1,
    IO_VAL_INT32 = 2
};

/** Logical I/O endpoint family. */
enum IoKind : uint8_t {
    IO_KIND_DIGITAL_IN = 0,
    IO_KIND_DIGITAL_OUT = 1,
    IO_KIND_ANALOG_IN = 2
};

/** Physical/backend origin of an endpoint. */
enum IoBackend : uint8_t {
    IO_BACKEND_GPIO = 0,
    IO_BACKEND_PCF8574 = 1,
    IO_BACKEND_ADS1115_INT = 2,
    IO_BACKEND_ADS1115_EXT_DIFF = 3,
    IO_BACKEND_DS18B20 = 4,
    IO_BACKEND_SHT40 = 5,
    IO_BACKEND_BMP280 = 6,
    IO_BACKEND_BME680 = 7,
    IO_BACKEND_POWERMON = 8,
    IO_BACKEND_TCA9554 = 9,
    IO_BACKEND_MCP23017 = 10
};

/** Endpoint capability bitmask. */
enum IoCap : uint8_t {
    IO_CAP_R = 1,
    IO_CAP_W = 2
};

/** Typed runtime value snapshot used by generic readers. */
struct IoValue {
    uint8_t valid = 0;
    /**
     * Mesure figee volontairement : la valeur est la derniere acquise pendant
     * que l'eau circulait, pas la lecture courante. Reste `valid`.
     */
    uint8_t held = 0;
    uint8_t type = IO_VAL_FLOAT;
    uint32_t tsMs = 0;
    IoSeq cycleSeq = 0;
    union {
        uint8_t b;
        float f;
        int32_t i32;
    } v{};
};

/** Static metadata describing one endpoint identity and capabilities. */
struct IoEndpointMeta {
    IoId id = IO_ID_INVALID;
    uint8_t kind = IO_KIND_DIGITAL_IN;
    uint8_t valueType = IO_VAL_BOOL;
    uint8_t backend = IO_BACKEND_GPIO;
    uint8_t channel = 0;
    uint8_t capabilities = 0;
    char name[IO_NAME_MAX_LEN] = {0};
    int32_t precision = 0;
    float minValid = 0.0f;
    float maxValid = 0.0f;
};

/** Per-cycle change summary exposed by IOServiceV2::lastCycle. */
struct IoCycleInfo {
    IoSeq seq = 0;
    uint32_t tsMs = 0;
    uint8_t changedCount = 0;
    IoId changedIds[IO_MAX_CHANGED_IDS] = {0};
};

/** Sensor validity reason bitmask. Disabled sensors are not considered faults. */
enum IoSensorInvalidReason : uint16_t {
    IO_SENSOR_INVALID_NONE = 0,
    IO_SENSOR_INVALID_DISABLED = 1U << 0,
    IO_SENSOR_INVALID_UNKNOWN_ID = 1U << 1,
    IO_SENSOR_INVALID_NOT_SENSOR = 1U << 2,
    IO_SENSOR_INVALID_NO_BINDING = 1U << 3,
    IO_SENSOR_INVALID_DRIVER_DISABLED = 1U << 4,
    IO_SENSOR_INVALID_NOT_READY = 1U << 5,
    IO_SENSOR_INVALID_NO_VALUE = 1U << 6,
    IO_SENSOR_INVALID_TYPE = 1U << 7
};

/** Status of a sensor-like IO endpoint. */
struct IoSensorStatus {
    IoId id = IO_ID_INVALID;
    uint8_t kind = IO_KIND_ANALOG_IN;
    uint8_t enabled = 0;
    uint8_t valid = 0;
    uint8_t reserved = 0;
    uint16_t invalidReasons = IO_SENSOR_INVALID_NONE;
    uint32_t tsMs = 0;
};

/**
 * @brief Unified static I/O service contract.
 *
 * Other modules must use numeric IoId access through this service.
 * Device names are metadata only for display/diagnostics.
 */
struct IOServiceV2 {
    /** Number of endpoints currently exposed by the service. */
    uint8_t (*count)(void* ctx);
    /** Resolve endpoint id by a compact index [0..count). */
    IoStatus (*idAt)(void* ctx, uint8_t index, IoId* outId);
    /** Fetch static metadata for a given endpoint id. */
    IoStatus (*meta)(void* ctx, IoId id, IoEndpointMeta* outMeta);
    /** Read the latest typed value for any endpoint kind. */
    IoStatus (*readValue)(void* ctx, IoId id, IoValue* outValue);

    /** Read the latest digital value (DI or DO). */
    IoStatus (*readDigital)(void* ctx, IoId id, uint8_t* outOn, uint32_t* outTsMs, IoSeq* outSeq);
    /** Write a digital output endpoint. */
    IoStatus (*writeDigital)(void* ctx, IoId id, uint8_t on, uint32_t tsMs);
    /** Read the latest analog value (AI). */
    IoStatus (*readAnalog)(void* ctx, IoId id, float* outValue, uint32_t* outTsMs, IoSeq* outSeq);

    /** Optional explicit tick hook for modules driving scheduled acquisition. */
    IoStatus (*tick)(void* ctx, uint32_t nowMs);
    /** Retrieve last completed cycle information. */
    IoStatus (*lastCycle)(void* ctx, IoCycleInfo* outCycle);
    /** Evaluate whether one sensor endpoint is enabled and currently valid. */
    IoStatus (*sensorStatus)(void* ctx, IoId id, IoSensorStatus* outStatus);
    /** List enabled sensor endpoints that are currently invalid. */
    IoStatus (*listInvalidSensors)(void* ctx, IoId* outIds, uint8_t maxIds, uint8_t* outCount);
    /**
     * Config status of a driver backend (IO_BACKEND_*): outEnabled = driver
     * active per config, outConfigurable = driver has an on/off config toggle
     * (0 = always-on driver like GPIO/ADS/TCA). Used by the I/O summary page.
     */
    IoStatus (*backendInfo)(void* ctx, uint8_t backend, uint8_t* outEnabled, uint8_t* outConfigurable);

    /**
     * Marque un endpoint analogique comme "gelable hors circulation" (hold = 1)
     * ou le libere (hold = 0).
     *
     * Une sonde en ligne ne mesure plus que l'eau immobile du porte-sondes des
     * que la pompe s'arrete : sa valeur derive sans rien dire du bassin. Un
     * endpoint marque republie alors sa derniere valeur acquise en circulation
     * au lieu de cette derive. Le marquage suit le role metier (pH, ORP,
     * temperature d'eau...), pas le slot : c'est a l'appelant de le refaire
     * quand son binding change.
     */
    IoStatus (*setAnalogHold)(void* ctx, IoId id, uint8_t hold);
    /**
     * Publie l'etat hydraulique : circulating = 0 gele les endpoints marques,
     * 1 les libere apres `settleSec`.
     *
     * Le delai de reprise couvre la purge du porte-sondes et le renouvellement
     * de la fenetre du filtre median, videe au front montant.
     */
    IoStatus (*setCirculating)(void* ctx, uint8_t circulating, uint16_t settleSec);
    /**
     * Age minimal de la valeur figee, en secondes (0 = figer la derniere
     * acquisition, comportement d'origine).
     *
     * La reference etant rafraichie a chaque cycle tant que l'eau circule, elle
     * capture le transitoire de l'arret de pompe : sur une sonde montee en
     * ligne, la disparition du potentiel d'ecoulement et le changement de
     * reference de masse deplacent la mesure dans les secondes qui encadrent la
     * coupure -- exactement quand la reference se fige. Deux etages permutes
     * toutes les `seconds` la decalent : la valeur gelee a alors entre une et
     * deux fois cet age, prise en circulation etablie.
     */
    IoStatus (*setAnalogHoldRefAge)(void* ctx, uint16_t seconds);

    /** Opaque implementation context. */
    void* ctx;
};
