//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.3
// File    : snmp.cpp
//==============================================================
// Версия с исправленным OID upsOutputSource (RFC 1628: 1.4.1.0)
// и ловушкой с varbinds для QNAP.
//==============================================================

#include "snmp.h"
#include "config.h"
#include "debug.h"
#include "logger.h"
#include "system.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <SNMPTrap.h>

static WiFiUDP         s_udp;
static SNMPAgent       s_snmp("public", "private");
static SNMPTrap       *s_trap = nullptr;
static TimestampCallback *s_uptimeCb = nullptr;

// --- UPS MIB (RFC 1628) ---
static int  s_upsBatteryStatus     = 2;
static int  s_upsSecondsOnBattery  = 0;
static int  s_upsEstimatedMinutes  = 0;
static int  s_upsEstimatedCharge   = 0;
static int  s_upsOutputSource      = 3; // 3=normal, 5=battery
static int  s_upsAlarmsPresent     = 0;
static int  s_upsInputVoltage      = 0;
static int  s_upsOutputVoltage     = 0;
static int  s_upsOutputCurrent     = 0;
static int  s_upsOutputPercentLoad = 0;

// --- Приватные OID ---
static int  s_pvBatteryMv          = 0;
static int  s_pvInputMv            = 0;
static int  s_pvInputPresent       = 1;
static int  s_pvBatteryLow         = 0;
static int  s_pvBatteryCritical    = 0;
static int  s_pvRuntimeSec         = 0;
static int  s_pvUptimeSec          = 0;
static int  s_pvStateCode          = 0;
static int  s_pvLastEventCode      = 0;

static uint32_t s_tensOfMillis = 0;
static bool s_logRequests = true;

static std::string s_strManufacturer = FW_MANUFACTURER;
static std::string s_strModel        = FW_MODEL;
static std::string s_strName         = FW_DEVICE_NAME;
static std::string s_strContact      = SNMP_SYS_CONTACT;
static std::string s_strLocation     = SNMP_SYS_LOCATION;
static std::string s_strDescr        = "DC UPS Monitor (2S Li-ion)";

static Config s_cfg;
static bool   s_initialized = false;

// --- Функции-обёртки с логированием ---
#define DEF_DYN_INT(fn_name, var_name, oid_str)              \
    static int fn_name() {                                   \
        if (s_logRequests) {                                 \
            DbgInfo("[SNMP-REQ] " oid_str " = %d", var_name);\
        }                                                    \
        return var_name;                                     \
    }

DEF_DYN_INT(dyn_upsBatteryStatus,     s_upsBatteryStatus,     ".1.3.6.1.2.1.33.1.2.1.0")
DEF_DYN_INT(dyn_upsSecondsOnBattery,  s_upsSecondsOnBattery,  ".1.3.6.1.2.1.33.1.2.2.0")
DEF_DYN_INT(dyn_upsEstimatedMinutes,  s_upsEstimatedMinutes,  ".1.3.6.1.2.1.33.1.2.3.0")
DEF_DYN_INT(dyn_upsEstimatedCharge,   s_upsEstimatedCharge,   ".1.3.6.1.2.1.33.1.2.4.0")
DEF_DYN_INT(dyn_upsOutputSource,      s_upsOutputSource,      ".1.3.6.1.2.1.33.1.4.1.0") // ИСПРАВЛЕНО
DEF_DYN_INT(dyn_upsInputVoltage,      s_upsInputVoltage,      ".1.3.6.1.2.1.33.1.3.3.1.5.1")
DEF_DYN_INT(dyn_upsOutputVoltage,     s_upsOutputVoltage,     ".1.3.6.1.2.1.33.1.4.4.1.2.1")
DEF_DYN_INT(dyn_upsOutputCurrent,     s_upsOutputCurrent,     ".1.3.6.1.2.1.33.1.4.4.1.3.1")
DEF_DYN_INT(dyn_upsOutputPercentLoad, s_upsOutputPercentLoad, ".1.3.6.1.2.1.33.1.4.4.1.5.1")
DEF_DYN_INT(dyn_upsAlarmsPresent,     s_upsAlarmsPresent,     ".1.3.6.1.2.1.33.1.4.2.0") // Перенесён

// Приватные
DEF_DYN_INT(dyn_pvBatteryMv,       s_pvBatteryMv,       "PRIV.1.1.0")
DEF_DYN_INT(dyn_pvInputMv,         s_pvInputMv,         "PRIV.1.2.0")
DEF_DYN_INT(dyn_pvInputPresent,    s_pvInputPresent,    "PRIV.1.3.0")
DEF_DYN_INT(dyn_pvBatteryLow,      s_pvBatteryLow,      "PRIV.1.4.0")
DEF_DYN_INT(dyn_pvBatteryCritical, s_pvBatteryCritical, "PRIV.1.5.0")
DEF_DYN_INT(dyn_pvRuntimeSec,      s_pvRuntimeSec,      "PRIV.1.6.0")
DEF_DYN_INT(dyn_pvUptimeSec,       s_pvUptimeSec,       "PRIV.1.7.0")
DEF_DYN_INT(dyn_pvStateCode,       s_pvStateCode,       "PRIV.1.8.0")
DEF_DYN_INT(dyn_pvLastEventCode,   s_pvLastEventCode,   "PRIV.1.9.0")

static String buildOid(const char *suffix) {
    String base = s_cfg.enterpriseOid;
    if (!base.startsWith(".")) base = "." + base;
    String s = suffix;
    if (!s.startsWith(".")) s = "." + s;
    return base + s;
}

void snmpInit(const Config &cfg) {
    s_cfg = cfg;
    DbgInfo("SNMP: init, community='%s', port=%u, enterprise=%s",
            cfg.snmpCommunity.c_str(), (unsigned)cfg.snmpPort, cfg.enterpriseOid.c_str());

    s_snmp.setUDP(&s_udp);
    s_snmp.begin();

    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.33.1.1.1.0", s_strManufacturer);
    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.33.1.1.2.0", s_strModel);
    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.33.1.1.5.0", s_strName);

    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.2.1.0", dyn_upsBatteryStatus);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.2.2.0", dyn_upsSecondsOnBattery);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.2.3.0", dyn_upsEstimatedMinutes);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.2.4.0", dyn_upsEstimatedCharge);

    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.3.3.1.5.1", dyn_upsInputVoltage);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.4.4.1.2.1", dyn_upsOutputVoltage);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.4.4.1.3.1", dyn_upsOutputCurrent);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.4.4.1.5.1", dyn_upsOutputPercentLoad);

    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.4.1.0", dyn_upsOutputSource);
    s_snmp.addDynamicIntegerHandler(".1.3.6.1.2.1.33.1.4.2.0", dyn_upsAlarmsPresent);

    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.1.1.0", s_strDescr);
    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.1.4.0", s_strContact);
    s_snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.1.6.0", s_strLocation);

    s_uptimeCb = (TimestampCallback *)s_snmp.addTimestampHandler(".1.3.6.1.2.1.1.3.0", &s_tensOfMillis);

    s_snmp.addDynamicIntegerHandler(buildOid(".1.1.0").c_str(), dyn_pvBatteryMv);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.2.0").c_str(), dyn_pvInputMv);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.3.0").c_str(), dyn_pvInputPresent);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.4.0").c_str(), dyn_pvBatteryLow);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.5.0").c_str(), dyn_pvBatteryCritical);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.6.0").c_str(), dyn_pvRuntimeSec);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.7.0").c_str(), dyn_pvUptimeSec);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.8.0").c_str(), dyn_pvStateCode);
    s_snmp.addDynamicIntegerHandler(buildOid(".1.9.0").c_str(), dyn_pvLastEventCode);

    s_snmp.sortHandlers();

    s_trap = new SNMPTrap(cfg.snmpCommunity.c_str(), SNMP_VERSION_2C);
    s_trap->setUDP(&s_udp);
    s_trap->setTrapOID(new OIDType(".1.3.6.1.2.1.33.2.1"));
    s_trap->setSpecificTrap(1);
    s_trap->setUptimeCallback(s_uptimeCb);
    s_trap->setVersion(cfg.useInform ? SNMP_VERSION_2C : SNMP_VERSION_1);
    s_trap->setInform(cfg.useInform);
    s_trap->setIP(WiFi.localIP());

    s_initialized = true;
    DbgInfo("SNMP: agent started on port 161");
    DbgInfo("SNMP: request logging ENABLED — see [SNMP-REQ] lines");
}

void snmpUpdateValues(const Config &cfg, const UpsStatus &st) {
    if (!s_initialized) return;

    if (st.batteryCritical) s_upsBatteryStatus = 4;
    else if (st.batteryLow) s_upsBatteryStatus = 3;
    else s_upsBatteryStatus = 2;

    s_upsSecondsOnBattery = (int)st.secondsOnBattery;
    s_upsEstimatedMinutes = (int)(st.runtimeSec / 60);
    s_upsEstimatedCharge  = (int)st.batteryPercent;

    s_upsOutputSource = st.inputPresent ? 3 : 5;

    if (st.inputPresent) s_upsInputVoltage = (int)(st.inputVolts + 0.5f);
    else s_upsInputVoltage = 0;

    if (st.inputPresent) s_upsOutputVoltage = (int)(st.inputVolts + 0.5f);
    else s_upsOutputVoltage = (int)(st.batteryVolts + 0.5f);

    s_upsOutputCurrent = (int)(cfg.loadCurrentA * 10.0f + 0.5f);

    float pct = (cfg.loadCurrentA / 2.0f) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_upsOutputPercentLoad = (int)(pct + 0.5f);

    int alarms = 0;
    if (!st.inputPresent) alarms++;
    if (st.batteryLow) alarms++;
    if (st.batteryCritical) alarms++;
    s_upsAlarmsPresent = alarms;

    s_pvBatteryMv = (int)(st.batteryVolts * 1000.0f);
    s_pvInputMv   = (int)(st.inputVolts   * 1000.0f);
    s_pvInputPresent    = st.inputPresent ? 1 : 0;
    s_pvBatteryLow      = st.batteryLow ? 1 : 0;
    s_pvBatteryCritical = st.batteryCritical ? 1 : 0;
    s_pvRuntimeSec      = (int)st.runtimeSec;
    s_pvUptimeSec       = (int)sysUptimeSec();
    s_pvStateCode       = (int)st.state;
}

void snmpLoop() {
    if (!s_initialized) return;
    s_tensOfMillis = (uint32_t)(millis() / 10);
    s_snmp.loop();
}

void snmpNotifyStateChange(const Config &cfg, const UpsStatus &st) {
    static bool prevInputPresent = true;
    static bool prevBatteryLow   = false;
    static bool prevBatteryCrit  = false;

    UpsSnmpEvent evt = UPS_SNMP_EVT_NONE;

    if (prevInputPresent && !st.inputPresent) evt = UPS_SNMP_EVT_INPUT_LOST;
    else if (!prevInputPresent && st.inputPresent) evt = UPS_SNMP_EVT_INPUT_RESTORED;
    else if (!prevBatteryCrit && st.batteryCritical) evt = UPS_SNMP_EVT_BATT_CRITICAL;
    else if (!prevBatteryLow && st.batteryLow && !st.batteryCritical) evt = UPS_SNMP_EVT_BATT_LOW;
    else if ((prevBatteryLow || prevBatteryCrit) && !st.batteryLow && !st.batteryCritical) evt = UPS_SNMP_EVT_BATT_RECOVERED;

    prevInputPresent = st.inputPresent;
    prevBatteryLow   = st.batteryLow;
    prevBatteryCrit  = st.batteryCritical;

    if (evt != UPS_SNMP_EVT_NONE) snmpSendEvent(evt);
}

void snmpSendEvent(UpsSnmpEvent evt) {
    if (!s_initialized || s_trap == nullptr) return;

    s_pvLastEventCode = (int)evt;

    IPAddress qnapIp;
    if (!qnapIp.fromString(s_cfg.qnapIp)) {
        DbgWarn("SNMP: bad QNAP IP '%s', trap skipped", s_cfg.qnapIp.c_str());
        return;
    }

    s_trap->setVersion(s_cfg.useInform ? SNMP_VERSION_2C : SNMP_VERSION_1);
    s_trap->setInform(s_cfg.useInform);
    s_trap->setIP(WiFi.localIP());

    int32_t reqId = s_snmp.sendTrapTo(s_trap, qnapIp, true, 2, 5000);

    if (reqId != INVALID_SNMP_REQUEST_ID) {
        const char *name = "?";
        switch (evt) {
            case UPS_SNMP_EVT_INPUT_LOST:     name = "INPUT_LOST";     break;
            case UPS_SNMP_EVT_INPUT_RESTORED: name = "INPUT_RESTORED"; break;
            case UPS_SNMP_EVT_BATT_LOW:       name = "BATT_LOW";       break;
            case UPS_SNMP_EVT_BATT_CRITICAL:  name = "BATT_CRITICAL";  break;
            case UPS_SNMP_EVT_BATT_RECOVERED: name = "BATT_RECOVERED"; break;
            default: break;
        }
        DbgInfo("SNMP: event '%s' sent to %s (reqId=%ld)", name, qnapIp.toString().c_str(), (long)reqId);
        loggerEvent("SNMP event sent: %s", name);
    } else {
        DbgWarn("SNMP: failed to send event");
        loggerEvent("SNMP event send FAILED");
    }
}
