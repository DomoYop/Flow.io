#pragma once
/**
 * @file NvsKeys.h
 * @brief Centralized NVS key constants used by ConfigStore-registered variables.
 */

namespace NvsKeys {

/** @brief Preferences namespace opened at boot (`main.cpp`). */
constexpr char StorageNamespace[] = "flowio"; // Preferences namespace name used at boot to open the firmware NVS partition.
/** @brief Preferences namespace for the Flow Connect Display firmware. */
constexpr char FlowConnectDisplayStorageNamespace[] = "flowconnectdisp";
/** @brief Config schema version key read/written by `ConfigStore::runMigrations`. */
constexpr char ConfigVersion[] = "cfg_ver"; // Persistent schema-version marker used to select and run config migrations.

namespace Wifi {
constexpr char Enabled[] = "wifi_en"; // WiFi module persisted key for field `wifi_en`.
constexpr char Ssid[] = "wifi_ssid"; // WiFi module persisted key for field `wifi_ssid`.
constexpr char Pass[] = "wifi_pass"; // WiFi module persisted key for field `wifi_pass`.
}  // namespace Wifi

namespace Ethernet {
constexpr char Enabled[] = "eth_en"; // Ethernet module persisted key for field `eth_en`.
}  // namespace Ethernet

namespace Mqtt {
constexpr char Host[] = "mq_host"; // MQTT module persisted key for field `mq_host`.
constexpr char Port[] = "mq_port"; // MQTT module persisted key for field `mq_port`.
constexpr char User[] = "mq_user"; // MQTT module persisted key for field `mq_user`.
constexpr char Pass[] = "mq_pass"; // MQTT module persisted key for field `mq_pass`.
constexpr char BaseTopic[] = "mq_base"; // MQTT module persisted key for field `mq_base`.
constexpr char TopicDeviceId[] = "mq_tid"; // MQTT topic device id override for `<base>/<deviceId>/...`.
constexpr char DeviceName[] = "mq_name"; // MQTT device display name used by HA discovery `device.name`.
constexpr char Enabled[] = "mq_en"; // MQTT module persisted key for field `mq_en`.
constexpr char SensorMinPublishMs[] = "mq_smin"; // MQTT module persisted key for field `mq_smin`.
}  // namespace Mqtt

namespace Ha {
constexpr char Enabled[] = "ha_en"; // Home Assistant module persisted key for field `ha_en`.
constexpr char Vendor[] = "ha_vend"; // Home Assistant module persisted key for field `ha_vend`.
constexpr char DeviceId[] = "ha_devid"; // Home Assistant module persisted key for field `ha_devid`.
constexpr char EntityPrefix[] = "ha_epfx"; // Home Assistant module persisted key for field `ha_epfx`.
constexpr char DiscoveryPrefix[] = "ha_pref"; // Home Assistant module persisted key for field `ha_pref`.
constexpr char Model[] = "ha_model"; // Home Assistant module persisted key for field `ha_model`.
}  // namespace Ha

namespace Time {
constexpr char Server1[] = "ntp_s1"; // Time module persisted key for field `ntp_s1`.
constexpr char Server2[] = "ntp_s2"; // Time module persisted key for field `ntp_s2`.
constexpr char Tz[] = "ntp_tz"; // Time module persisted key for field `ntp_tz`.
constexpr char Enabled[] = "ntp_en"; // Time module persisted key for field `ntp_en`.
constexpr char ManualTime[] = "tm_manual"; // Time module persisted key for field `manual_time`.
constexpr char WeekStartMonday[] = "tm_wkmon"; // Time module persisted key for field `tm_wkmon`.
constexpr char ScheduleBlob[] = "tm_sched"; // Time module persisted key for field `tm_sched`.
constexpr char MetaBlob[] = "tm_meta"; // Time module internal diagnostic metadata blob.
}  // namespace Time

namespace System {
constexpr char Language[] = "sys_lang"; // System module persisted key for field `sys_lang`.
constexpr char DeviceName[] = "sys_dname"; // System module persisted key for field `sys_dname`.
}  // namespace System

namespace SystemMonitor {
constexpr char TraceEnabled[] = "sm_tren"; // System monitor module persisted key for field `sm_tren`.
constexpr char TracePeriodMs[] = "sm_trms"; // System monitor module persisted key for field `sm_trms`.
constexpr char WebWatchdogEnabled[] = "sm_wden"; // System monitor module persisted key for field `sm_wden`.
constexpr char WebWatchdogCheckPeriodMs[] = "sm_wdck"; // System monitor module persisted key for field `sm_wdck`.
constexpr char WebWatchdogStaleMs[] = "sm_wdst"; // System monitor module persisted key for field `sm_wdst`.
constexpr char WebWatchdogBootGraceMs[] = "sm_wdbg"; // System monitor module persisted key for field `sm_wdbg`.
constexpr char WebWatchdogMaxFailures[] = "sm_wdmf"; // System monitor module persisted key for field `sm_wdmf`.
constexpr char WebWatchdogAutoReboot[] = "sm_wdrb"; // System monitor module persisted key for field `sm_wdrb`.
}  // namespace SystemMonitor

namespace Io {
// Les cles par slot (a00..a31, i00..i07, d00..d15) sont generees au boot par
// IoSlotConfigVars ("io_a%02u<sfx>", "io_i%02u<sfx>", "io_d%02u<sfx>").
constexpr char IO_ADS[] = "io_ads"; // IO module persisted key for field `io_ads`.
constexpr char IO_AEAD[] = "io_aead"; // IO module persisted key for field `io_aead`.
constexpr char IO_AGAI[] = "io_agai"; // IO module persisted key for field `io_agai`.
constexpr char IO_AIAD[] = "io_aiad"; // IO module persisted key for field `io_aiad`.
constexpr char IO_ARAT[] = "io_arat"; // IO module persisted key for field `io_arat`.
constexpr char IO_DIN[] = "io_din"; // IO module persisted key for field `io_din`.
constexpr char IO_DS[] = "io_ds"; // IO module persisted key for field `io_ds`.
constexpr char IO_EN[] = "io_en"; // IO module persisted key for field `io_en`.
constexpr char IO_SHTEN[] = "io_shten"; // IO module persisted key for field `io_sht40_enabled`.
constexpr char IO_SHTAD[] = "io_shtad"; // IO module persisted key for field `io_sht40_address`.
constexpr char IO_SHTPL[] = "io_shtpl"; // IO module persisted key for field `io_sht40_poll_ms`.
constexpr char IO_BMPEN[] = "io_bmpen"; // IO module persisted key for field `io_bmp280_enabled`.
constexpr char IO_BMPAD[] = "io_bmpad"; // IO module persisted key for field `io_bmp280_address`.
constexpr char IO_BMPPL[] = "io_bmppl"; // IO module persisted key for field `io_bmp280_poll_ms`.
constexpr char IO_BMEEN[] = "io_bmeen"; // IO module persisted key for field `io_bme680_enabled`.
constexpr char IO_BMEAD[] = "io_bmead"; // IO module persisted key for field `io_bme680_address`.
constexpr char IO_BMEPL[] = "io_bmepl"; // IO module persisted key for field `io_bme680_poll_ms`.
constexpr char IO_PMEN[] = "io_pmen"; // IO module persisted key for field `io_powermon_enabled`.
constexpr char IO_PMMD[] = "io_pmmd"; // IO module persisted key for field `io_powermon_model`.
constexpr char IO_PMAD[] = "io_pmad"; // IO module persisted key for field `io_powermon_address`.
constexpr char IO_PMPL[] = "io_pmpl"; // IO module persisted key for field `io_powermon_poll_ms`.
constexpr char IO_PMSH[] = "io_pmsh"; // IO module persisted key for field `io_powermon_shunt_ohms`.
constexpr char IO_PCFAD[] = "io_pcfad"; // IO module persisted key for field `io_pcfad`.
constexpr char IO_PCFAL[] = "io_pcfal"; // IO module persisted key for field `io_pcfal`.
constexpr char IO_PCFEN[] = "io_pcfen"; // IO module persisted key for field `io_pcfen`.
constexpr char IO_PCFMK[] = "io_pcfmk"; // IO module persisted key for field `io_pcfmk`.
constexpr char IO_MCPEN[] = "io_mcpen"; // IO module persisted key for field `io_mcp23017_enabled`.
constexpr char IO_MCPAD[] = "io_mcpad"; // IO module persisted key for field `io_mcp23017_address`.
constexpr char IO_TCAEN[] = "io_tcaen"; // IO module persisted key for field `io_tca9554_enabled`.
constexpr char IO_TCAAD[] = "io_tcaad"; // IO module persisted key for field `io_tca9554_address`.
constexpr char IO_SCL[] = "io_scl"; // IO module persisted key for field `io_scl`.
constexpr char IO_SDA[] = "io_sda"; // IO module persisted key for field `io_sda`.
constexpr char IO_SCL_S3[] = "io_scl3"; // IO module persisted key for S3 field `io_scl`.
constexpr char IO_SDA_S3[] = "io_sda3"; // IO module persisted key for S3 field `io_sda`.
constexpr char IO_TREN[] = "io_tren"; // IO module persisted key for field `io_tren`.
constexpr char IO_TRMS[] = "io_trms"; // IO module persisted key for field `io_trms`.
constexpr char IO_DS24EN[] = "io_ds24en"; // DS2484 1-Wire bridge enabled.
constexpr char IO_DS24AD[] = "io_ds24ad"; // DS2484 1-Wire bridge I2C address.
constexpr char IO_DS24PL[] = "io_ds24pl"; // DS2484 1-Wire bridge poll period (ms).
constexpr char IO_OW1EN[] = "io_ow1en"; // GPIO 1-Wire bus #1 enabled.
constexpr char IO_OW1GP[] = "io_ow1gp"; // GPIO 1-Wire bus #1 GPIO pin.
constexpr char IO_OW1PL[] = "io_ow1pl"; // GPIO 1-Wire bus #1 poll period (ms).
constexpr char IO_OW2EN[] = "io_ow2en"; // GPIO 1-Wire bus #2 enabled.
constexpr char IO_OW2GP[] = "io_ow2gp"; // GPIO 1-Wire bus #2 GPIO pin.
constexpr char IO_OW2PL[] = "io_ow2pl"; // GPIO 1-Wire bus #2 poll period (ms).
constexpr char IO_DSWR[] = "io_dswr"; // DS18B20 water sensor ROM selection.
constexpr char IO_DSAR[] = "io_dsar"; // DS18B20 air sensor ROM selection.
constexpr char DsRomWater[] = "io_dswrm"; // IO module runtime DS18 water ROM blob.
constexpr char DsRomAir[] = "io_dsarm"; // IO module runtime DS18 air ROM blob.
}  // namespace Io

namespace I2cCfg {
constexpr char ClientEnabled[] = "ic_cli_en"; // I2C cfg client enabled.
constexpr char ClientSda[] = "ic_cli_sda"; // I2C cfg client SDA pin.
constexpr char ClientScl[] = "ic_cli_scl"; // I2C cfg client SCL pin.
constexpr char ClientFreq[] = "ic_cli_frq"; // I2C cfg client bus frequency.
constexpr char ClientAddr[] = "ic_cli_adr"; // I2C cfg client target slave address.
constexpr char LcdAutoOffEnabled[] = "ic_lcd_ao"; // Supervisor LCD auto-off after 60s when no motion.
constexpr char LcdMotionGpio[] = "ic_lcd_mg"; // Supervisor LCD motion detector GPIO pin.
constexpr char DashboardEnabledFmt[] = "ic_d%u_en"; // I2C cfg dashboard slot enabled.
constexpr char DashboardRuntimeIdFmt[] = "ic_d%u_rt"; // I2C cfg dashboard slot runtime UI id.
constexpr char DashboardLabelFmt[] = "ic_d%u_lb"; // I2C cfg dashboard slot label.
constexpr char DashboardColorIdFmt[] = "ic_d%u_ci"; // I2C cfg dashboard slot color preset id.

constexpr char ServerEnabled[] = "ic_srv_en"; // I2C cfg server enabled.
constexpr char ServerSda[] = "ic_srv_sda"; // I2C cfg server SDA pin.
constexpr char ServerScl[] = "ic_srv_scl"; // I2C cfg server SCL pin.
constexpr char ServerFreq[] = "ic_srv_frq"; // I2C cfg server bus frequency.
constexpr char ServerAddr[] = "ic_srv_adr"; // I2C cfg server own slave address.
}  // namespace I2cCfg

namespace PoolLogic {
constexpr char Enabled[] = "pl_en"; // Pool logic module persisted key for field `pl_en`.
constexpr char AutoMode[] = "pl_auto"; // Pool logic module persisted key for field `pl_auto`.
constexpr char WinterMode[] = "pl_wint"; // Pool logic module persisted key for field `pl_wint`.
constexpr char PhAutoMode[] = "pl_pha"; // Pool logic module persisted key for field `pl_pha`.
constexpr char OrpAutoMode[] = "pl_orpa"; // Pool logic module persisted key for field `pl_orpa`.
constexpr char HeaterAutoMode[] = "pl_hta"; // Pool logic module persisted key for field `pl_hta`.
constexpr char PhDosePlus[] = "pl_phpl"; // Pool logic module persisted key for field `pl_phpl`.
constexpr char DisinfectionType[] = "pl_dtype"; // Pool logic module persisted key for field `disinfection_type`.
constexpr char SwgControlMode[] = "pl_swgm"; // Pool logic module persisted key for field `swg_control_mode`.
constexpr char O2PoolVolumeM3[] = "pl_o2vol"; // Pool logic O2 pool volume in m3.
constexpr char O2DoseMlPer10M3Week[] = "pl_o2dose"; // Pool logic O2 weekly dose in ml/10m3.
constexpr char O2MainHour[] = "pl_o2hr"; // Pool logic O2 main dosing hour.
constexpr char O2SplitCount[] = "pl_o2spl"; // Pool logic O2 weekly split count.
constexpr char O2TempComp[] = "pl_o2tc"; // Pool logic O2 temperature compensation enable.
constexpr char O2LoadFactor[] = "pl_o2load"; // Pool logic O2 manual load factor.
constexpr char O2MinFilterRunMin[] = "pl_o2mfr"; // Pool logic O2 minimum filtration runtime before dosing.
constexpr char O2ProtocolState[] = "pl_o2st"; // Pool logic O2 persisted protocol state.
constexpr char O2LastDoseDay[] = "pl_o2day"; // Pool logic O2 last dose day key.
constexpr char O2WeeklyDoneMl[] = "pl_o2done"; // Pool logic O2 weekly injected volume.
constexpr char O2PendingMl[] = "pl_o2pend"; // Pool logic O2 pending volume after reboot.
constexpr char TempLow[] = "pl_tlow"; // Pool logic module persisted key for field `pl_tlow`.
constexpr char TempSetpoint[] = "pl_tset"; // Pool logic module persisted key for field `pl_tset`.
constexpr char FiltrationStartMin[] = "pl_smin"; // Pool logic module persisted key for field `pl_smin`.
constexpr char FiltrationStopMax[] = "pl_smax"; // Pool logic module persisted key for field `pl_smax`.
constexpr char PhIoId[] = "pl_phiid"; // Pool logic module persisted key for field `pl_phiid`.
constexpr char OrpIoId[] = "pl_oiid"; // Pool logic module persisted key for field `pl_oiid`.
constexpr char PressureIoId[] = "pl_piid"; // Pool logic module persisted key for field `pl_piid`.
constexpr char WaterTempIoId[] = "pl_wiid"; // Pool logic module persisted key for field `pl_wiid`.
constexpr char AirTempIoId[] = "pl_aiid"; // Pool logic module persisted key for field `pl_aiid`.
constexpr char LevelIoId[] = "pl_liid"; // Pool logic module persisted key for field `pl_liid`.
constexpr char PhLevelIoId[] = "pl_phli"; // Pool logic module persisted key for field `pl_phli`.
constexpr char ChlorineLevelIoId[] = "pl_clli"; // Pool logic module persisted key for field `pl_clli`.
constexpr char PressureLow[] = "pl_psil"; // Pool logic module persisted key for field `pl_psil`.
constexpr char PressureHigh[] = "pl_psih"; // Pool logic module persisted key for field `pl_psih`.
constexpr char WinterStart[] = "pl_wstr"; // Pool logic module persisted key for field `pl_wstr`.
constexpr char FreezeHold[] = "pl_whld"; // Pool logic module persisted key for field `pl_whld`.
constexpr char SecureElectro[] = "pl_sect"; // Pool logic module persisted key for field `pl_sect`.
constexpr char PhSetpoint[] = "pl_phsp"; // Pool logic module persisted key for field `pl_phsp`.
constexpr char OrpSetpoint[] = "pl_orps"; // Pool logic module persisted key for field `pl_orps`.
constexpr char HeaterSetpoint[] = "pl_htsp"; // Pool logic module persisted key for field `pl_htsp`.
constexpr char PhKp[] = "pl_phkp"; // Pool logic module persisted key for field `pl_phkp`.
constexpr char PhKi[] = "pl_phki"; // Pool logic module persisted key for field `pl_phki`.
constexpr char PhKd[] = "pl_phkd"; // Pool logic module persisted key for field `pl_phkd`.
constexpr char OrpKp[] = "pl_okp"; // Pool logic module persisted key for field `pl_okp`.
constexpr char OrpKi[] = "pl_oki"; // Pool logic module persisted key for field `pl_oki`.
constexpr char OrpKd[] = "pl_okd"; // Pool logic module persisted key for field `pl_okd`.
constexpr char PhWindowMs[] = "pl_phwms"; // Pool logic module persisted key for field `pl_phwms`.
constexpr char OrpWindowMs[] = "pl_owms"; // Pool logic module persisted key for field `pl_owms`.
constexpr char PidMinOnMs[] = "pl_pmon"; // Pool logic module persisted key for field `pl_pmon`.
constexpr char PidSampleMs[] = "pl_psamp"; // Pool logic module persisted key for field `pl_psamp`.
constexpr char PressureDelay[] = "pl_psdt"; // Pool logic module persisted key for field `pl_psdt`.
constexpr char DelayPids[] = "pl_dpds"; // Pool logic module persisted key for field `pl_dpds`.
constexpr char DelayElectro[] = "pl_delt"; // Pool logic module persisted key for field `pl_delt`.
constexpr char RobotDelay[] = "pl_rdel"; // Pool logic module persisted key for field `pl_rdel`.
constexpr char RobotDuration[] = "pl_rdur"; // Pool logic module persisted key for field `pl_rdur`.
constexpr char FillingMinOn[] = "pl_fmin"; // Pool logic module persisted key for field `pl_fmin`.
constexpr char FiltrationSlot[] = "pl_sfil"; // Pool logic module persisted key for field `pl_sfil`.
constexpr char SwgSlot[] = "pl_sswg"; // Pool logic module persisted key for field `pl_sswg`.
constexpr char RobotSlot[] = "pl_srob"; // Pool logic module persisted key for field `pl_srob`.
constexpr char FillingSlot[] = "pl_sfill"; // Pool logic module persisted key for field `pl_sfill`.
constexpr char PhPumpSlot[] = "pl_sphp"; // Pool logic module persisted key for field `pl_sphp`.
constexpr char OrpPumpSlot[] = "pl_sorp"; // Pool logic module persisted key for field `pl_sorp`.
constexpr char HeaterSlot[] = "pl_shea"; // Pool logic module persisted key for field `pl_shea`.
constexpr char FiltrationCalcStart[] = "pl_fcst"; // Pool logic runtime key for calculated filtration start hour.
constexpr char FiltrationCalcStop[] = "pl_fcen"; // Pool logic runtime key for calculated filtration stop hour.
constexpr char FlowCopyDelay[] = "pl_fscdl"; // Flowswitch copy output activation delay (s).
constexpr char FlowInterlock[] = "pl_flilk"; // Flowswitch safety interlock enable (block dosing/electrolysis when no flow).
}  // namespace PoolLogic

namespace PoolDevice {
/** @brief printf format for per-slot `enabled` key (example `pd0en`). */
constexpr char EnabledFmt[] = "pd%uen"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot dependency mask key (example `pd0dp`). */
constexpr char DependsFmt[] = "pd%udp"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot flow key (example `pd0flh`). */
constexpr char FlowFmt[] = "pd%uflh"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot tank capacity key (example `pd0tc`). */
constexpr char TankCapFmt[] = "pd%utc"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot tank initial value key (example `pd0ti`). */
constexpr char TankInitFmt[] = "pd%uti"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot max daily uptime in seconds (example `pd0mu`). */
constexpr char MaxUptimeFmt[] = "pd%umu"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot runtime metrics blob key (example `pd0rt`). */
constexpr char RuntimeFmt[] = "pd%urt"; // Pool device module runtime metrics key template; `%u` is replaced by slot index before NVS access.
}  // namespace PoolDevice

namespace Alarm {
constexpr char Enabled[] = "al_en"; // Alarm module persisted key for field `enabled`.
constexpr char EvalPeriodMs[] = "al_epms"; // Alarm module persisted key for field `eval_period_ms`.
}  // namespace Alarm

namespace Hmi {
constexpr char LedsEnabled[] = "hmi_leds"; // HMI module persisted key for logical LED-panel writes enable.
constexpr char WaveshareLedEnabled[] = "hmi_wsled"; // HMI module persisted key for Waveshare WS2812 status LED enable.
constexpr char NextionEnabled[] = "hmi_nxen"; // HMI module persisted key for Nextion output enable.
constexpr char FlowConnectUdpEnabled[] = "hmi_fcden"; // HMI module persisted key for Flow Connect Display UDP driver enable.
constexpr char FlowConnectUdpToken[] = "hmi_fcdtk"; // Shared token for Flow Connect Display UDP pairing.
constexpr char RemoteUdpEnabledLegacy[] = "hmi_udpen"; // Legacy remote UDP display driver enable key.
constexpr char RemoteUdpTokenLegacy[] = "hmi_udptk"; // Legacy remote UDP display pairing token key.
constexpr char VeniceEnabled[] = "hmi_vcen"; // HMI module persisted key for Venice RF433 output enable.
constexpr char VeniceTxGpio[] = "hmi_vcgp"; // HMI module persisted key for Venice RF433 TX GPIO.
constexpr char BuzzerEnable[] = "hmi_bz_en"; // HMI buzzer module persisted key for config-ack beep enable.
}  // namespace Hmi

namespace FlowConnectDisplay {
constexpr char UdpToken[] = "fcd_udptk"; // Flow Connect Display persisted pairing token key.
}  // namespace FlowConnectDisplay

}  // namespace NvsKeys
