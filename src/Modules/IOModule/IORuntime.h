#pragma once
/**
 * @file IORuntime.h
 * @brief IO runtime helpers and keys.
 */

#include <math.h>
#include <stdint.h>
#include "Core/DataStore/DataStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_IO_BASE = DataKeys::IoBase;
static_assert(IO_MAX_ENDPOINTS <= DataKeys::IoReservedCount, "DataKeys::IoReservedCount too small for IO endpoints");

static inline float ioRoundToPrecision(float value, int32_t decimals)
{
    if (decimals <= 0) return (float)((int32_t)lroundf(value));
    float scale = 1.0f;
    for (int32_t i = 0; i < decimals; ++i) scale *= 10.0f;
    return roundf(value * scale) / scale;
}

static inline bool ioChangedAtPrecision(float a, float b, int32_t decimals)
{
    return ioRoundToPrecision(a, decimals) != ioRoundToPrecision(b, decimals);
}

static inline bool ioEndpointFloat(const DataStore& ds, uint8_t idx, float& out)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;
    const IOEndpointRuntime& ep = ds.data().io.endpoints[idx];
    if (!ep.valid) return false;
    if (ep.valueType != IO_VALUE_FLOAT) return false;
    out = ep.floatValue;
    return true;
}

static inline bool ioEndpointBool(const DataStore& ds, uint8_t idx, bool& out)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;
    const IOEndpointRuntime& ep = ds.data().io.endpoints[idx];
    if (!ep.valid) return false;
    if (ep.valueType != IO_VALUE_BOOL) return false;
    out = ep.boolValue;
    return true;
}

static inline bool ioEndpointInt(const DataStore& ds, uint8_t idx, int32_t& out)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;
    const IOEndpointRuntime& ep = ds.data().io.endpoints[idx];
    if (!ep.valid) return false;
    if (ep.valueType != IO_VALUE_INT32) return false;
    out = ep.intValue;
    return true;
}

/**
 * Resout l'index de registre d'un endpoint a partir de son identite logique.
 *
 * L'index d'une case de `IORuntimeData::endpoints` est un index de registre :
 * seuls les slots dont le binding est resolu y entrent, donc il glisse des
 * qu'un slot amont perd son port. Toute lecture doit passer par l'ioId, jamais
 * par une position codee en dur.
 */
static inline bool ioEndpointIndexByIoId(const DataStore& ds, IoId ioId, uint8_t& idxOut)
{
    if (ioId == IO_ID_INVALID) return false;
    const RuntimeData& rt = ds.data();
    for (uint8_t i = 0; i < IO_MAX_ENDPOINTS; ++i) {
        if (rt.io.endpoints[i].ioId != ioId) continue;
        idxOut = i;
        return true;
    }
    return false;
}

static inline bool ioEndpointFloatByIoId(const DataStore& ds, IoId ioId, float& out)
{
    uint8_t idx = 0;
    if (!ioEndpointIndexByIoId(ds, ioId, idx)) return false;
    return ioEndpointFloat(ds, idx, out);
}

static inline bool ioEndpointBoolByIoId(const DataStore& ds, IoId ioId, bool& out)
{
    uint8_t idx = 0;
    if (!ioEndpointIndexByIoId(ds, ioId, idx)) return false;
    return ioEndpointBool(ds, idx, out);
}

static inline bool ioEndpointIntByIoId(const DataStore& ds, IoId ioId, int32_t& out)
{
    uint8_t idx = 0;
    if (!ioEndpointIndexByIoId(ds, ioId, idx)) return false;
    return ioEndpointInt(ds, idx, out);
}

/**
 * Publie l'identite logique de la case `idx`. Appele une fois par endpoint a
 * l'assemblage du registre ; `IO_ID_INVALID` libere la case.
 */
static inline void setIoEndpointIoId(DataStore& ds, uint8_t idx, IoId ioId)
{
    if (idx >= IO_MAX_ENDPOINTS) return;
    ds.dataMutable().io.endpoints[idx].ioId = ioId;
}

/** Libere l'identite de toutes les cases (reconfiguration du registre). */
static inline void clearIoEndpointIoIds(DataStore& ds)
{
    RuntimeData& rt = ds.dataMutable();
    for (uint8_t i = 0; i < IO_MAX_ENDPOINTS; ++i) rt.io.endpoints[i].ioId = IO_ID_INVALID;
}

static inline bool setIoEndpointFloat(DataStore& ds, uint8_t idx, float value, uint32_t tsMs)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_FLOAT &&
        ep.floatValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_FLOAT;
    ep.floatValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx));
    return true;
}

/**
 * Vrai des qu'une mesure au moins est figee faute de circulation.
 *
 * Source unique de l'indicateur "mesures figees" : la question posee est
 * toujours « qu'est-ce qui est reellement gele », jamais « quelle etait
 * l'intention » -- ce qui evite de reimplementer la temporisation de reprise
 * dans chaque consommateur (snapshot MQTT, Runtime UI, interface web).
 */
static inline bool ioAnyEndpointHeld(const DataStore& ds)
{
    const RuntimeData& rt = ds.data();
    for (uint8_t i = 0; i < IO_MAX_ENDPOINTS; ++i) {
        if (rt.io.endpoints[i].held) return true;
    }
    return false;
}

/**
 * Marque la mesure comme figee ou vivante. Separe de setIoEndpointFloat : le
 * marqueur change bien plus rarement que la valeur, et il ne doit pas provoquer
 * une notification a chaque acquisition.
 */
static inline bool setIoEndpointHeld(DataStore& ds, uint8_t idx, bool held)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    IOEndpointRuntime& ep = ds.dataMutable().io.endpoints[idx];
    if (ep.held == held) return false;
    ep.held = held;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx));
    return true;
}

static inline bool setIoEndpointInvalid(DataStore& ds, uint8_t idx, uint8_t valueType, uint32_t tsMs)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (!ep.valid && ep.valueType == valueType) {
        return false;
    }

    ep.valid = false;
    ep.valueType = valueType;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx));
    return true;
}

static inline bool setIoEndpointBool(DataStore& ds, uint8_t idx, bool value, uint32_t tsMs)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_BOOL &&
        ep.boolValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_BOOL;
    ep.boolValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx));
    return true;
}

static inline bool setIoEndpointInt(DataStore& ds, uint8_t idx, int32_t value, uint32_t tsMs)
{
    if (idx >= IO_MAX_ENDPOINTS) return false;

    RuntimeData& rt = ds.dataMutable();
    IOEndpointRuntime& ep = rt.io.endpoints[idx];

    if (ep.valid &&
        ep.valueType == IO_VALUE_INT32 &&
        ep.intValue == value &&
        ep.timestampMs == tsMs) {
        return false;
    }

    ep.valid = true;
    ep.valueType = IO_VALUE_INT32;
    ep.intValue = value;
    ep.timestampMs = tsMs;

    ds.notifyChanged((DataKey)(DATAKEY_IO_BASE + idx));
    return true;
}
