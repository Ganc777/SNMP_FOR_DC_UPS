//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.6
// File    : storage.cpp
//==============================================================
// R06.1: informPeriodSec принудительно = 30 при загрузке.
// R07.0: добавлены поля Email (SMTP) в load/save/dump/defaults.
// R07.7: удалено неиспользуемое поле logMaxBytes.
// R09.0: добавлены поля uiLang и uiEventMask.
// R09.6: ntpUse удалён, добавлены ntpSyncHour и ntpSyncMin.
//        Старое значение "ntpUse" из NVS игнорируется.
//==============================================================

#include "storage.h"
#include "config.h"
#include "debug.h"
#include <Preferences.h>

static Preferences prefs;

//--------------------------------------------------------------
// Дефолты
//--------------------------------------------------------------
void storageFillDefaults(Config &cfg) {
    cfg.wifiSsid          = WIFI_SSID_DEFAULT;
    cfg.wifiPass          = WIFI_PASS_DEFAULT;
    cfg.hostname          = WIFI_HOSTNAME;

    cfg.useDhcp           = NET_USE_DHCP_DEFAULT;
    cfg.ip                = NET_IP_DEFAULT;
    cfg.mask              = NET_MASK_DEFAULT;
    cfg.gw                = NET_GW_DEFAULT;
    cfg.dns               = NET_DNS_DEFAULT;

    // R09.6: ntpUse удалён — NTP всегда включён.
    cfg.ntpServer1        = NTP_SERVER1_DEFAULT;
    cfg.ntpServer2        = NTP_SERVER2_DEFAULT;
    cfg.ntpTz             = NTP_TZ_DEFAULT;
    cfg.ntpSyncHour       = NTP_SYNC_HOUR_DEFAULT;
    cfg.ntpSyncMin        = NTP_SYNC_MIN_DEFAULT;

    cfg.qnapIp            = SNMP_QNAP_IP_DEFAULT;
    cfg.qnapMac           = SNMP_QNAP_MAC_DEFAULT;
    cfg.snmpCommunity     = SNMP_COMMUNITY_DEF;
    cfg.snmpPort          = SNMP_PORT_DEFAULT;
    cfg.useInform         = SNMP_USE_INFORM_DEF;
    cfg.enterpriseOid     = SNMP_ENTERPRISE_OID;

    cfg.battFullV         = BATT_FULL_V;
    cfg.battEmptyV        = BATT_EMPTY_V;
    cfg.battWarnV         = BATT_WARN_V;
    cfg.battCapacityWh    = BATT_CAPACITY_WH;
    cfg.loadCurrentA      = BATT_LOAD_CURRENT_A;

    cfg.inputLostV        = INPUT_LOST_V;
    cfg.hystV             = THRESHOLD_HYST_V;
    cfg.tempHighC         = TEMP_HIGH_C;

    cfg.adcPollMs         = ADC_POLL_MS;
    cfg.informPeriodSec   = 30;

    cfg.battOffsetMv      = ADC_BATT_OFFSET_MV_DEF;
    cfg.battGain          = ADC_BATT_GAIN_DEF;
    cfg.inputOffsetMv     = ADC_INPUT_OFFSET_MV_DEF;
    cfg.inputGain         = ADC_INPUT_GAIN_DEF;

    cfg.logEnable         = LOG_ENABLE_DEFAULT;
    cfg.logVoltPeriodSec  = LOG_VOLT_PERIOD_SEC;

    cfg.wolEnable         = WOL_ENABLE_DEFAULT;
    cfg.wolOutageMinSec   = WOL_OUTAGE_MIN_SEC;
    cfg.wolStableSec      = WOL_STABLE_SEC;
    cfg.wolBattMinPct     = WOL_BATT_MIN_PCT;

    cfg.tgToken           = TG_TOKEN_DEFAULT;
    cfg.tgChatId          = TG_CHATID_DEFAULT;
    cfg.tgEnable          = TG_ENABLE_DEFAULT;
    cfg.tgPeriod          = TG_PERIOD_DEFAULT;
    cfg.tgEvents          = TG_EVENTS_DEFAULT;

    cfg.emSmtpHost        = EM_HOST_DEFAULT;
    cfg.emSmtpPort        = EM_PORT_DEFAULT;
    cfg.emUser            = EM_USER_DEFAULT;
    cfg.emPass            = EM_PASS_DEFAULT;
    cfg.emFrom            = EM_FROM_DEFAULT;
    cfg.emTo              = EM_TO_DEFAULT;
    cfg.emEnable          = EM_ENABLE_DEFAULT;
    cfg.emPeriod          = EM_PERIOD_DEFAULT;
    cfg.emEvents          = EM_EVENTS_DEFAULT;

    cfg.uiLang            = "ru";
    cfg.uiEventMask       = 0x00FF;
}

//--------------------------------------------------------------
// Загрузка
//--------------------------------------------------------------
void storageLoad(Config &cfg) {
    storageFillDefaults(cfg);

    prefs.begin(NVS_NAMESPACE, true);

    cfg.wifiSsid      = prefs.getString("wifiSsid", cfg.wifiSsid);
    cfg.wifiPass      = prefs.getString("wifiPass", cfg.wifiPass);
    cfg.hostname      = prefs.getString("hostname", cfg.hostname);

    cfg.useDhcp       = prefs.getBool  ("useDhcp",  cfg.useDhcp);
    cfg.ip            = prefs.getString("ip",       cfg.ip);
    cfg.mask          = prefs.getString("mask",     cfg.mask);
    cfg.gw            = prefs.getString("gw",       cfg.gw);
    cfg.dns           = prefs.getString("dns",      cfg.dns);

    // R09.6: "ntpUse" больше не читаем — NTP всегда включён.
    cfg.ntpServer1    = prefs.getString("ntpSrv1",  cfg.ntpServer1);
    cfg.ntpServer2    = prefs.getString("ntpSrv2",  cfg.ntpServer2);
    cfg.ntpTz         = prefs.getString("ntpTz",    cfg.ntpTz);
    cfg.ntpSyncHour   = prefs.getUChar ("ntpSyncH", cfg.ntpSyncHour);
    cfg.ntpSyncMin    = prefs.getUChar ("ntpSyncM", cfg.ntpSyncMin);

    cfg.qnapIp        = prefs.getString("qnapIp",   cfg.qnapIp);
    cfg.qnapMac       = prefs.getString("qnapMac",  cfg.qnapMac);
    cfg.snmpCommunity = prefs.getString("snmpComm", cfg.snmpCommunity);
    cfg.snmpPort      = prefs.getUShort("snmpPort", cfg.snmpPort);
    cfg.useInform     = prefs.getBool  ("useInform",cfg.useInform);
    cfg.enterpriseOid = prefs.getString("entOid",   cfg.enterpriseOid);

    cfg.battFullV     = prefs.getFloat ("battFullV",    cfg.battFullV);
    cfg.battEmptyV    = prefs.getFloat ("battEmptyV",   cfg.battEmptyV);
    cfg.battWarnV     = prefs.getFloat ("battWarnV",    cfg.battWarnV);
    cfg.battCapacityWh= prefs.getFloat ("battCapWh",    cfg.battCapacityWh);
    cfg.loadCurrentA  = prefs.getFloat ("loadCurrentA", cfg.loadCurrentA);

    cfg.inputLostV    = prefs.getFloat ("inputLostV", cfg.inputLostV);
    cfg.hystV         = prefs.getFloat ("hystV",      cfg.hystV);
    cfg.tempHighC     = prefs.getFloat ("tempHighC",  cfg.tempHighC);

    cfg.adcPollMs     = prefs.getUShort("adcPollMs",  cfg.adcPollMs);

    cfg.battOffsetMv  = prefs.getFloat ("battOffMv",  cfg.battOffsetMv);
    cfg.battGain      = prefs.getFloat ("battGain",   cfg.battGain);
    cfg.inputOffsetMv = prefs.getFloat ("inpOffMv",   cfg.inputOffsetMv);
    cfg.inputGain     = prefs.getFloat ("inpGain",    cfg.inputGain);

    cfg.logEnable     = prefs.getBool  ("logEn",      cfg.logEnable);
    cfg.logVoltPeriodSec = prefs.getUShort("logPerSec", cfg.logVoltPeriodSec);

    cfg.wolEnable       = prefs.getBool  ("wolEn",     cfg.wolEnable);
    cfg.wolOutageMinSec = prefs.getUShort("wolOutSec", cfg.wolOutageMinSec);
    cfg.wolStableSec    = prefs.getUShort("wolStbSec", cfg.wolStableSec);
    cfg.wolBattMinPct   = prefs.getUChar ("wolBattPc", cfg.wolBattMinPct);

    cfg.tgToken       = prefs.getString("tgToken",  cfg.tgToken);
    cfg.tgChatId      = prefs.getString("tgChatId", cfg.tgChatId);
    cfg.tgEnable      = prefs.getBool  ("tgEnable", cfg.tgEnable);
    cfg.tgPeriod      = prefs.getUShort("tgPeriod", cfg.tgPeriod);
    cfg.tgEvents      = prefs.getUShort("tgEvents", cfg.tgEvents);

    cfg.emSmtpHost    = prefs.getString("emHost",   cfg.emSmtpHost);
    cfg.emSmtpPort    = prefs.getUShort("emPort",   cfg.emSmtpPort);
    cfg.emUser        = prefs.getString("emUser",   cfg.emUser);
    cfg.emPass        = prefs.getString("emPass",   cfg.emPass);
    cfg.emFrom        = prefs.getString("emFrom",   cfg.emFrom);
    cfg.emTo          = prefs.getString("emTo",     cfg.emTo);
    cfg.emEnable      = prefs.getBool  ("emEnable", cfg.emEnable);
    cfg.emPeriod      = prefs.getUShort("emPeriod", cfg.emPeriod);
    cfg.emEvents      = prefs.getUShort("emEvents", cfg.emEvents);

    cfg.uiLang        = prefs.getString("uiLang",   cfg.uiLang);
    cfg.uiEventMask   = prefs.getUShort("uiEvMask", cfg.uiEventMask);

    prefs.end();

    cfg.informPeriodSec = 30;

    // R09.6: защита от некорректных значений в NVS
    if (cfg.ntpSyncHour > 23) cfg.ntpSyncHour = NTP_SYNC_HOUR_DEFAULT;
    if (cfg.ntpSyncMin  > 59) cfg.ntpSyncMin  = NTP_SYNC_MIN_DEFAULT;

    DbgInfo("Config loaded from NVS");
}

//--------------------------------------------------------------
// Сохранение
//--------------------------------------------------------------
void storageSave(const Config &cfg) {
    prefs.begin(NVS_NAMESPACE, false);

    prefs.putString("wifiSsid", cfg.wifiSsid);
    prefs.putString("wifiPass", cfg.wifiPass);
    prefs.putString("hostname", cfg.hostname);

    prefs.putBool  ("useDhcp",  cfg.useDhcp);
    prefs.putString("ip",       cfg.ip);
    prefs.putString("mask",     cfg.mask);
    prefs.putString("gw",       cfg.gw);
    prefs.putString("dns",      cfg.dns);

    // R09.6: "ntpUse" не сохраняем (устаревшее поле).
    prefs.putString("ntpSrv1",  cfg.ntpServer1);
    prefs.putString("ntpSrv2",  cfg.ntpServer2);
    prefs.putString("ntpTz",    cfg.ntpTz);
    prefs.putUChar ("ntpSyncH", cfg.ntpSyncHour);
    prefs.putUChar ("ntpSyncM", cfg.ntpSyncMin);

    prefs.putString("qnapIp",   cfg.qnapIp);
    prefs.putString("qnapMac",  cfg.qnapMac);
    prefs.putString("snmpComm", cfg.snmpCommunity);
    prefs.putUShort("snmpPort", cfg.snmpPort);
    prefs.putBool  ("useInform",cfg.useInform);
    prefs.putString("entOid",   cfg.enterpriseOid);

    prefs.putFloat ("battFullV",    cfg.battFullV);
    prefs.putFloat ("battEmptyV",   cfg.battEmptyV);
    prefs.putFloat ("battWarnV",    cfg.battWarnV);
    prefs.putFloat ("battCapWh",    cfg.battCapacityWh);
    prefs.putFloat ("loadCurrentA", cfg.loadCurrentA);

    prefs.putFloat ("inputLostV", cfg.inputLostV);
    prefs.putFloat ("hystV",      cfg.hystV);
    prefs.putFloat ("tempHighC",  cfg.tempHighC);

    prefs.putUShort("adcPollMs",  cfg.adcPollMs);
    prefs.putUShort("informSec",  cfg.informPeriodSec);

    prefs.putFloat ("battOffMv",  cfg.battOffsetMv);
    prefs.putFloat ("battGain",   cfg.battGain);
    prefs.putFloat ("inpOffMv",   cfg.inputOffsetMv);
    prefs.putFloat ("inpGain",    cfg.inputGain);

    prefs.putBool  ("logEn",      cfg.logEnable);
    prefs.putUShort("logPerSec",  cfg.logVoltPeriodSec);

    prefs.putBool  ("wolEn",      cfg.wolEnable);
    prefs.putUShort("wolOutSec",  cfg.wolOutageMinSec);
    prefs.putUShort("wolStbSec",  cfg.wolStableSec);
    prefs.putUChar ("wolBattPc",  cfg.wolBattMinPct);

    prefs.putString("tgToken",  cfg.tgToken);
    prefs.putString("tgChatId", cfg.tgChatId);
    prefs.putBool  ("tgEnable", cfg.tgEnable);
    prefs.putUShort("tgPeriod", cfg.tgPeriod);
    prefs.putUShort("tgEvents", cfg.tgEvents);

    prefs.putString("emHost",   cfg.emSmtpHost);
    prefs.putUShort("emPort",   cfg.emSmtpPort);
    prefs.putString("emUser",   cfg.emUser);
    prefs.putString("emPass",   cfg.emPass);
    prefs.putString("emFrom",   cfg.emFrom);
    prefs.putString("emTo",     cfg.emTo);
    prefs.putBool  ("emEnable", cfg.emEnable);
    prefs.putUShort("emPeriod", cfg.emPeriod);
    prefs.putUShort("emEvents", cfg.emEvents);

    prefs.putString("uiLang",   cfg.uiLang);
    prefs.putUShort("uiEvMask", cfg.uiEventMask);

    prefs.end();

    DbgInfo("Config saved to NVS");
}

//--------------------------------------------------------------
// Сброс к дефолтам
//--------------------------------------------------------------
void storageResetDefaults(Config &cfg) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    storageFillDefaults(cfg);
    storageSave(cfg);

    DbgWarn("Config reset to defaults");
}

//--------------------------------------------------------------
// Dump
//--------------------------------------------------------------
void storageDump(const Config &cfg) {
    DbgInfo("---- Config dump ----");
    DbgInfo("WiFi SSID      : %s", cfg.wifiSsid.c_str());
    DbgInfo("Hostname       : %s", cfg.hostname.c_str());
    DbgInfo("DHCP           : %s", cfg.useDhcp ? "yes" : "no");
    DbgInfo("IP/Mask/GW     : %s / %s / %s",
            cfg.ip.c_str(), cfg.mask.c_str(), cfg.gw.c_str());
    DbgInfo("NTP srv1/2     : %s / %s",
            cfg.ntpServer1.c_str(), cfg.ntpServer2.c_str());
    DbgInfo("NTP TZ         : %s", cfg.ntpTz.c_str());
    DbgInfo("NTP sync time  : %02u:%02u (window %u min)",
            (unsigned)cfg.ntpSyncHour, (unsigned)cfg.ntpSyncMin,
            (unsigned)NTP_SYNC_WINDOW_MIN);
    DbgInfo("QNAP IP/MAC    : %s / %s",
            cfg.qnapIp.c_str(), cfg.qnapMac.c_str());
    DbgInfo("SNMP community : %s", cfg.snmpCommunity.c_str());
    DbgInfo("SNMP port      : %u", (unsigned)cfg.snmpPort);
    DbgInfo("SNMP mode      : %s", cfg.useInform ? "INFORM" : "TRAP");
    DbgInfo("Batt full/empty/warn : %.2f / %.2f / %.2f V",
            cfg.battFullV, cfg.battEmptyV, cfg.battWarnV);
    DbgInfo("Batt capacity  : %.2f Wh", cfg.battCapacityWh);
    DbgInfo("Load current   : %.2f A",  cfg.loadCurrentA);
    DbgInfo("Input lost at  : %.2f V",  cfg.inputLostV);
    DbgInfo("Hysteresis     : %.2f V",  cfg.hystV);
    DbgInfo("Temp high at   : %.1f C",  cfg.tempHighC);
    DbgInfo("ADC poll       : %u ms",   (unsigned)cfg.adcPollMs);
    DbgInfo("INFORM period  : %u s (fixed)", (unsigned)cfg.informPeriodSec);
    DbgInfo("Calib BATT     : off=%.2f mV gain=%.4f",
            cfg.battOffsetMv, cfg.battGain);
    DbgInfo("Calib INPUT    : off=%.2f mV gain=%.4f",
            cfg.inputOffsetMv, cfg.inputGain);
    DbgInfo("Log enable     : %s", cfg.logEnable ? "yes" : "no");
    DbgInfo("Log volt period: %u s", (unsigned)cfg.logVoltPeriodSec);
    DbgInfo("WoL enable     : %s", cfg.wolEnable ? "yes" : "no");
    DbgInfo("WoL outage min : %u s", (unsigned)cfg.wolOutageMinSec);
    DbgInfo("WoL stable     : %u s", (unsigned)cfg.wolStableSec);
    DbgInfo("WoL batt min   : %u %%", (unsigned)cfg.wolBattMinPct);
    DbgInfo("TG enable      : %s", cfg.tgEnable ? "yes" : "no");
    DbgInfo("TG chat id     : %s", cfg.tgChatId.c_str());
    DbgInfo("TG period      : %u", (unsigned)cfg.tgPeriod);
    DbgInfo("TG events mask : 0x%04X", (unsigned)cfg.tgEvents);
    DbgInfo("EM enable      : %s", cfg.emEnable ? "yes" : "no");
    DbgInfo("EM smtp host   : %s:%u",
            cfg.emSmtpHost.c_str(), (unsigned)cfg.emSmtpPort);
    DbgInfo("EM user/from   : %s / %s",
            cfg.emUser.c_str(), cfg.emFrom.c_str());
    DbgInfo("EM to          : %s", cfg.emTo.c_str());
    DbgInfo("EM period      : %u", (unsigned)cfg.emPeriod);
    DbgInfo("EM events mask : 0x%04X", (unsigned)cfg.emEvents);
    DbgInfo("UI lang        : %s", cfg.uiLang.c_str());
    DbgInfo("UI event mask  : 0x%04X", (unsigned)cfg.uiEventMask);
    DbgInfo("---------------------");
}
