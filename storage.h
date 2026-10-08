//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.0
// File    : storage.h
//==============================================================
// R06.0: добавлены поля Telegram в Config.
// R07.0: добавлены поля Email (SMTP) в Config.
// R07.7: удалено неиспользуемое поле logMaxBytes.
// R09.0: добавлены поля uiLang и uiEventMask для сохранения
//        выбора языка интерфейса и чекбоксов событий в NVS.
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
    bool     ntpUse;
    String   ntpServer1;
    String   ntpServer2;
    String   ntpTz;

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

    // Telegram (R06.0)
    String   tgToken;
    String   tgChatId;
    bool     tgEnable;
    uint16_t tgPeriod;
    uint16_t tgEvents;

    // Email (R07.0)
    String   emSmtpHost;
    uint16_t emSmtpPort;
    String   emUser;
    String   emPass;
    String   emFrom;
    String   emTo;
    bool     emEnable;
    uint16_t emPeriod;
    uint16_t emEvents;

    // UI (R09.0)
    String   uiLang;        // "ru" / "en"
    uint16_t uiEventMask;   // чекбоксы событий на вкладке Графики
};

void storageLoad(Config &cfg);
void storageSave(const Config &cfg);
void storageResetDefaults(Config &cfg);
void storageFillDefaults(Config &cfg);
void storageDump(const Config &cfg);

#endif // STORAGE_H
