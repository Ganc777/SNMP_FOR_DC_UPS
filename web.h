//==============================================================
// SNMP_FOR_DC_UPS
// Release : R06.0
// File    : web.h
//==============================================================
// Web-сервер: дашборд, настройки, калибровка, логи, OTA,
// Telegram-бот.
//==============================================================

#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include "storage.h"
#include "ups.h"

void webInit(const Config &cfg);
void webLoop();
void webUpdateStatus(const UpsStatus &st);
void webUpdateTemp(float tempC);
void webSetConfig(const Config &cfg);

// Установить текущий язык интерфейса (для Telegram-сообщений)
void webSetLang(const String &lang);

#endif // WEB_H
