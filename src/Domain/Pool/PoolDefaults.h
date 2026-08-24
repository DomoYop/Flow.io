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

// Ratio utilisateur applique aux cycles de renouvellement (%, 100 = courbe de
// reference). Permet d'allonger (ou de reduire) le temps de filtration
// preconise sans deformer la courbe temperature.
constexpr uint8_t FiltrationCycleRatioPct = 100;
constexpr uint8_t FiltrationCycleRatioMinPct = 50;
constexpr uint8_t FiltrationCycleRatioMaxPct = 200;

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

// Pression de service filtre propre. 0 = non calibree : la valeur est apprise a
// la premiere marche stable (cf. PoolLogicModule::updatePressureReference_).
// Un defaut fixe serait faux partout : une installation tourne a 0,5 bar, une
// autre a 1,2, et un seuil d'encrassement absolu n'a donc aucun sens.
constexpr float PressureRef = 0.0f;
// Ecart de lavage, valeur de metier usuelle (+0,3 a +0,5 bar au-dessus du
// filtre propre).
constexpr float PressureFoulingDelta = 0.40f;
constexpr float PressureHigh = 1.80f;
constexpr float WinterStartTempC = -2.0f;
constexpr float FreezeHoldTempC = 2.0f;
constexpr float SecureElectroTempC = 15.0f;

constexpr float PhSetpoint = 7.4f;
constexpr float OrpSetpoint = 700.0f;
constexpr float HeaterSetpoint = 27.0f;

constexpr float OrpKp = 4500.0f;
constexpr float OrpKi = 0.0f;
constexpr float OrpKd = 0.0f;

constexpr int32_t PidWindowMs = 3600000;
constexpr int32_t PidMinOnMs = 30000;
constexpr int32_t PidSampleMs = 30000;

// Dosage volumetrique par lots (pH). Le gain est un ordre de grandeur usuel :
// ~10 mL d'acide chlorhydrique 33 % par m3 pour -0,1 pH a TAC ~100 mg/L. Il est
// ensuite auto-calibre a partir de l'effet reellement obtenu.
constexpr float PhDoseMlPerM3 = 10.0f;
constexpr float PhDoseUnitStep = 0.1f;
constexpr float PhDeadband = 0.05f;
constexpr float PhDoseFactor = 0.5f;  // viser la moitie de l'ecart par lot
constexpr uint16_t PhMixWaitMin = 0;  // 0 => duree de turnover calculee
constexpr float PhDoseMaxBatchMl = 250.0f;
constexpr float PhDoseMaxDayMl = 1500.0f;
constexpr float PhValidMin = 6.0f;
constexpr float PhValidMax = 8.5f;
constexpr uint16_t PhSampleMaxAgeSec = 300;
constexpr uint8_t PhNoEffectBatches = 3;
constexpr float PhNoEffectDelta = 0.02f;

// Bornes internes du controleur de dosage.
constexpr uint32_t DosingMixWaitMinMs = 10UL * 60UL * 1000UL;
constexpr uint32_t DosingMixWaitMaxMs = 8UL * 3600UL * 1000UL;
constexpr uint16_t DosingMinBatchSec = 10;      // granularite minimale d'un lot
constexpr float DosingMinTankMl = 5.0f;         // sous ce reste, bidon considere vide
constexpr float DosingGainMinBatchMl = 50.0f;   // sous ce volume, gain non exploitable
constexpr float DosingGainBandPct = 0.5f;      // bornage +/-50 % du gain de reference
constexpr uint8_t DosingGainWindow = 8;        // fenetre de la moyenne glissante

constexpr uint8_t PressureStartupDelaySec = 60;
constexpr uint8_t DelayPidsMin = 5;
// Gel des mesures en ligne hors circulation. 90 s couvrent la purge du
// porte-sondes au redemarrage et le remplissage de la fenetre du filtre median
// (11 echantillons) meme pour les sources lentes.
constexpr bool SensorHold = true;
constexpr uint16_t SensorHoldSettleSec = 90;
// 30 s suffisent a sortir du transitoire d'arret de pompe (quelques secondes)
// sans remonter a une eau dont la chimie aurait eu le temps de bouger.
constexpr uint16_t SensorHoldRefAgeSec = 30;
constexpr bool SensorHoldWaterTemp = true;
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
// Securite de dernier recours, pas limite de fonctionnement : a 1,8 L/h, 30 min
// ne laissaient que 900 mL/jour (~0,18 pH), a peine plus que la derive naturelle
// du bassin. La limite metier est desormais ph_dose_max_day, en millilitres.
constexpr int32_t DosePumpMaxUptimeDaySec = 90 * 60;
constexpr int32_t ChlorineGeneratorMaxUptimeDaySec = 600 * 60;
constexpr int32_t FillPumpMaxUptimeDaySec = 30 * 60;

inline constexpr PoolLogicDefaultsSpec kLogicDefaults{
    TempLow,
    TempHigh,
    FiltrationStartMinHour,
    FiltrationStopMaxHour,
    PressureRef,
    PressureFoulingDelta,
    PressureHigh,
    WinterStartTempC,
    FreezeHoldTempC,
    SecureElectroTempC,
    PhSetpoint,
    OrpSetpoint,
    HeaterSetpoint,
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
