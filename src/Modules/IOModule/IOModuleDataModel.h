#pragma once
/**
 * @file IOModuleDataModel.h
 * @brief IO runtime data model contribution.
 */

#include <stdint.h>

#include "Core/Services/IIO.h"

constexpr uint8_t IO_MAX_ENDPOINTS = 40;

enum IOValueType : uint8_t {
    IO_VALUE_BOOL = 0,
    IO_VALUE_FLOAT = 1,
    IO_VALUE_INT32 = 2
};

struct IOEndpointRuntime {
    bool valid = false;
    uint8_t valueType = IO_VALUE_FLOAT;
    float floatValue = 0.0f;
    bool boolValue = false;
    int32_t intValue = 0;
    uint32_t timestampMs = 0;
    // Identite logique du slot occupant cette case. L'index de la case est un
    // index de registre (ordre d'insertion des endpoints resolus), il glisse des
    // qu'un slot perd son binding : les consommateurs doivent resoudre par ioId.
    IoId ioId = IO_ID_INVALID;
};

struct IORuntimeData {
    IOEndpointRuntime endpoints[IO_MAX_ENDPOINTS];
};

// MODULE_DATA_MODEL: IORuntimeData io
