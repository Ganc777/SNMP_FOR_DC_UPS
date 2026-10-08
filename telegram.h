//==============================================================
// SNMP_FOR_DC_UPS
// Release : R07.7
// File    : telegram.h
//==============================================================
// Модуль отправки уведомлений в Telegram.
// R07.7: удалено объявление telegramSetLang() — не использовалось.
//==============================================================

#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <Arduino.h>
#include "storage.h"

//--------------------------------------------------------------
// Категории событий (битовая маска)
//--------------------------------------------------------------
#define TG_EV_POWER   0x0001
#define TG_EV_BATT    0x0002
#define TG_EV_WOL     0x0004
#define TG_EV_BOOT    0x0008
#define TG_EV_WIFI    0x0010
#define TG_EV_SNMP    0x0020
#define TG_EV_NTP     0x0040
#define TG_EV_TEMP    0x0080

//--------------------------------------------------------------
// API
//--------------------------------------------------------------
void telegramInit(const Config &cfg);
void telegramSetConfig(const Config &cfg);
void telegramLoop();

void telegramQueueEvent(uint16_t bit, const String &title, const String &body);
void telegramQueueReport(const String &lang);

bool telegramSendTest();
bool telegramIsReady();

String telegramStatusCode();
uint32_t telegramStatusQueue();

String telegramStatusText();

void telegramNotify(uint16_t bit, const String &logLine);

#endif // TELEGRAM_H
