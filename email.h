//==============================================================
// SNMP_FOR_DC_UPS
// Release : R07.7
// File    : email.h
//==============================================================
// Модуль отправки уведомлений по Email (SMTP).
// Ручной SMTP-диалог через WiFiClientSecure (implicit TLS,
// порт 465). Провайдер по умолчанию — Mail.ru.
// R07.7: удалено объявление emailSetLang() — не использовалось.
//==============================================================

#ifndef EMAIL_H
#define EMAIL_H

#include <Arduino.h>
#include "storage.h"

//--------------------------------------------------------------
// Категории событий (битовая маска) — совпадают с TG_EV_*
//--------------------------------------------------------------
#define EM_EV_POWER   0x0001
#define EM_EV_BATT    0x0002
#define EM_EV_WOL     0x0004
#define EM_EV_BOOT    0x0008
#define EM_EV_WIFI    0x0010
#define EM_EV_SNMP    0x0020
#define EM_EV_NTP     0x0040
#define EM_EV_TEMP    0x0080

//--------------------------------------------------------------
// API
//--------------------------------------------------------------
void emailInit(const Config &cfg);
void emailSetConfig(const Config &cfg);
void emailLoop();

void emailQueueEvent(uint16_t bit, const String &title, const String &body);
void emailQueueReport(const String &lang);

bool emailSendTest();
bool emailIsReady();

String   emailStatusCode();
uint32_t emailStatusQueue();

String emailStatusText();

void emailNotify(uint16_t bit, const String &logLine);

#endif // EMAIL_H
