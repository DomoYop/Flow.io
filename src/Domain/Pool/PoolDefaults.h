#pragma once

#include "Domain/DomainTypes.h"

namespace PoolDefaults {

constexpr uint8_t FiltrationPivotHour = 15;
constexpr uint8_t MinDurationHours = 2;

constexpr float TempLow = 12.0f;
constexpr float TempHigh = 24.0f;

constexpr uint8_t FiltrationStartMinHour = 8;
constexpr uint8_t FiltrationStopMaxHour = 23;

// Filtration par renouvellement volumique : besoin = volume * cycles(T) / debit.
constexpr float PumpFlowM3h = 10.0f;
constexpr uint16_t FiltrationMinTotalMinutes = (uint16_t)MinDurationHours * 60u;
constexpr uint16_t FiltrationMinSegmentMinutes = 30;

// Courbe cycles de renouvellement par jour en fonction de la temperature de
// l'eau (interpolation lineaire entre points, plateau aux extremes).
struct FiltrationCyclesPoint {
    float tempC;
    float cyclesPerDay;
};
inline constexpr FiltrationCyclesPoint kFiltrationCyclesCurve[] = {
    {10.0f, 0.25f},
    {14.0f, 0.40f},
    {18.0f, 0.60f},
    {22.0f, 1.00f},
    {25.0f, 1.40f},
    {28.0f, 2.00f},
    {31.0f, 3.00f},
};
constexpr uint8_t FiltrationCyclesCurveCount =
    (uint8_t)(sizeof(kFiltrationCyclesCurve) / sizeof(kFiltrationCyclesCurve[0]));

// Fenetres de filtration par defaut (minutes depuis minuit). La fenetre 1
// reprend la plage historique 08:00-23:00 ; la fenetre 2 est un gabarit
// heures creuses 23:30-07:30 livre desactive.
constexpr uint16_t FiltrWin1StartMinute = (uint16_t)FiltrationStartMinHour * 60u;
constexpr uint16_t FiltrWin1StopMinute = (uint16_t)FiltrationStopMaxHour * 60u;
constexpr uint16_t FiltrWin2StartMinute = 23u * 60u + 30u;
constexpr uint16_t FiltrWin2StopMinute = 7u * 60u + 30u;

constexpr float PressureLow = 0.15f;
constexpr float PressureHigh = 1.80f;
constexpr float WinterStartTempC = -2.0f;
constexpr float FreezeHoldTempC = 2.0f;
constexpr float SecureElectroTempC = 15.0f;

constexpr float PhSetpoint = 7.4f;
constexpr float OrpSetpoint = 700.0f;
constexpr float HeaterSetpoint = 27.0f;

constexpr float PhKp = 2000000.0f;
constexpr float PhKi = 0.0f;
constexpr float PhKd = 0.0f;
constexpr float OrpKp = 4500.0f;
constexpr float OrpKi = 0.0f;
constexpr float OrpKd = 0.0f;

constexpr int32_t PidWindowMs = 3600000;
constexpr int32_t PidMinOnMs = 30000;
constexpr int32_t PidSampleMs = 30000;

constexpr uint8_t PressureStartupDelaySec = 60;
constexpr uint8_t DelayPidsMin = 5;
constexpr uint8_t DelayElectroMin = 10;
constexpr uint8_t RobotDelayMin = 30;
constexpr uint8_t RobotDurationMin = 120;
constexpr uint8_t FillingMinOnSec = 30;

constexpr float PoolVolumeM3 = 50.0f;
constexpr float O2DoseMlPer10M3Week = 500.0f;
constexpr uint8_t O2MainHour = 20;
constexpr uint8_t O2SplitCount = 2;
constexpr bool O2TempComp = true;
constexpr float O2LoadFactor = 1.0f;
constexpr uint8_t O2MinFilterRunMin = 10;

constexpr float PeristalticFlowLPerHour = 1.2f;
constexpr float PeristalticTankCapacityMl = 20000.0f;
constexpr float PeristalticTankInitialMl = 20000.0f;
constexpr int32_t DosePumpMaxUptimeDaySec = 30 * 60;
constexpr int32_t ChlorineGeneratorMaxUptimeDaySec = 600 * 60;
constexpr int32_t FillPumpMaxUptimeDaySec = 30 * 60;

inline constexpr PoolLogicDefaultsSpec kLogicDefaults{
    TempLow,
    TempHigh,
    FiltrationStartMinHour,
    FiltrationStopMaxHour,
    PressureLow,
    PressureHigh,
    WinterStartTempC,
    FreezeHoldTempC,
    SecureElectroTempC,
    PhSetpoint,
    OrpSetpoint,
    HeaterSetpoint,
    PhKp,
    PhKi,
    PhKd,
    OrpKp,
    OrpKi,
    OrpKd,
    PidWindowMs,
    PidMinOnMs,
    PidSampleMs,
    PressureStartupDelaySec,
    DelayPidsMin,
    DelayElectroMin,
    RobotDelayMin,
    RobotDurationMin,
    FillingMinOnSec,
    PoolVolumeM3,
    O2DoseMlPer10M3Week,
    O2MainHour,
    O2SplitCount,
    O2TempComp,
    O2LoadFactor,
    O2MinFilterRunMin
};

}  // namespace PoolDefaults
