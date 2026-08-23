#pragma once
/**
 * @file PoolLogicRuntime.h
 * @brief PoolLogic runtime helpers and keys (dosage pH).
 */

#include <string.h>

#include "Core/DataStore/DataStore.h"
#include "Core/DataKeys.h"
#include "Modules/PoolLogicModule/PoolLogicModuleDataModel.h"

// RUNTIME_PUBLIC

constexpr DataKey DATAKEY_POOL_PH_DOSING = DataKeys::PoolPhDosing;
constexpr DataKey DATAKEY_POOL_PRESSURE = DataKeys::PoolPressure;

static inline const PoolLogicPhDosingRuntimeData& poolPhDosingRuntime(const DataStore& ds)
{
    return ds.data().poolPhDosing;
}

/** @brief Publie l'etat du dosage pH. Retourne true si quelque chose a change. */
static inline bool setPoolPhDosingRuntime(DataStore& ds, const PoolLogicPhDosingRuntimeData& in)
{
    RuntimeData& rt = ds.dataMutable();
    if (memcmp(&rt.poolPhDosing, &in, sizeof(PoolLogicPhDosingRuntimeData)) == 0) return false;
    rt.poolPhDosing = in;
    ds.notifyChanged(DATAKEY_POOL_PH_DOSING);
    return true;
}

static inline const PoolLogicPressureRuntimeData& poolPressureRuntime(const DataStore& ds)
{
    return ds.data().poolPressure;
}

/** @brief Publie l'etat de pression/encrassement. True si quelque chose a change. */
static inline bool setPoolPressureRuntime(DataStore& ds, const PoolLogicPressureRuntimeData& in)
{
    RuntimeData& rt = ds.dataMutable();
    if (memcmp(&rt.poolPressure, &in, sizeof(PoolLogicPressureRuntimeData)) == 0) return false;
    rt.poolPressure = in;
    ds.notifyChanged(DATAKEY_POOL_PRESSURE);
    return true;
}
