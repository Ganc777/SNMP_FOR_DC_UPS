//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.9
// File    : storage.h
//==============================================================
// Структура Config и API NVS.
//
// История:
//   R06.0: добавлены поля Telegram.
//   R07.0: добавлены поля Email.
//   R07.7: удалено logMaxBytes.
//   R09.0: добавлены uiLang, uiEventMask.
//   R09.6: ntpUse удалён; добавлены ntpSyncHour, ntpSyncMin.
//   R10.9: добавлены otaUser, otaPass (хранятся в NVS).
//==============================================================

#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>

struct Config {
    // Wi-Fi
    String   wifiSsid;
    String   wifiPass;
    String   hostname;

    // Сеть
    bool     useDhcp;
    String   ip;
    String   mask;
    String   gw;
    String   dns;

    // NTP
    String   ntpServer1;
    String   ntpServer2;
    String   ntpTz;
    uint8_t  ntpSyncHour;   // 0..23
    uint8_t  ntpSyncMin;    // 0..59

    // SNMP
    String   qnapIp;
    String   qnapMac;
    String   snmpCommunity;
    uint16_t snmpPort;
    bool     useInform;
    String   enterpriseOid;

    // Батарея
    float    battFullV;
    float    battEmptyV;
    float    battWarnV;
    float    battCapacityWh;
    float    loadCurrentA;

    // Пороги
    float    inputLostV;
    float    hystV;
    float    tempHighC;

    // Периоды
    uint16_t adcPollMs;
    uint16_t informPeriodSec;

    // Калибровка
    float    battOffsetMv;
    float    battGain;
    float    inputOffsetMv;
    float    inputGain;

    // Логгер
    bool     logEnable;
    uint16_t logVoltPeriodSec;

    // Wake-on-LAN
    bool     wolEnable;
    uint16_t wolOutageMinSec;
    uint16_t wolStableSec;
    uint8_t  wolBattMinPct;

    // Telegram
    String   tgToken;
    String   tgChatId;
    bool     tgEnable;
    uint16_t tgPeriod;
    uint16_t tgEvents;

    // Email
    String   emSmtpHost;
    uint16_t emSmtpPort;
    String   emUser;
    String   emPass;
    String   emFrom;
    String   emTo;
    bool     emEnable;
    uint16_t emPeriod;
    uint16_t emEvents;

    // UI
    String   uiLang;
    uint16_t uiEventMask;

    // R10.9: OTA
    String   otaUser;
    String   otaPass;
};

void storageLoad(Config &cfg);
void storageSave(const Config &cfg);
void storageResetDefaults(Config &cfg);
void storageFillDefaults(Config &cfg);
void storageDump(const Config &cfg);

#endif // STORAGE_H
