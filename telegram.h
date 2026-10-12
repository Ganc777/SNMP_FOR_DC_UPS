//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : telegram.h
//==============================================================
// Модуль отправки уведомлений в Telegram.
//
// История:
//   R06.0: базовый модуль Telegram (UniversalTelegramBot).
//   R06.1: telegramStatusCode(), telegramStatusQueue().
//   R07.7: удалены telegramSetLang() и s_lang.
//   R09.0: периодический отчёт привязан к 12:00.
//   R10:   красивое оформление (эмодзи, ✅/❌, разделитель).
//   R10.2: единый формат с Email, локализация, подсветка.
//   R10.3: диагностика маски в лог, q=%u.
//   R10.5: фикс uptime-бага (boot после 12:00).
//   R10.6: ЕДИНЫЙ формат для всех уведомлений — 10 строк.
//          Добавлена telegramQueueBootReport() — boot-отчёт
//          отправляется безусловно (без маски), отложенно
//          на 3 сек после старта, один раз за сессию.
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

// R10.6: boot-отчёт (безусловный, 10 строк, заголовок 🔄)
void telegramQueueBootReport();

bool telegramSendTest();
bool telegramIsReady();

String telegramStatusCode();
uint32_t telegramStatusQueue();

String telegramStatusText();

void telegramNotify(uint16_t bit, const String &logLine);

#endif // TELEGRAM_H
