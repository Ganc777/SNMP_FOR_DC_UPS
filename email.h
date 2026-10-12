//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : email.h
//==============================================================
// Модуль отправки уведомлений по Email (SMTP).
// Ручной SMTP-диалог через WiFiClientSecure (implicit TLS,
// порт 465). Провайдер по умолчанию — Mail.ru.
//
// История:
//   R07.0: базовый модуль Email (SMTP/SSL).
//   R07.1: AUTH PLAIN + fallback на AUTH LOGIN.
//   R07.2: диагностика emailInit/emailSetConfig/emailSendTest.
//   R07.7: удалены emailSetLang() и s_lang.
//   R09.0: периодический отчёт привязан к 12:00.
//   R09.1: КРИТИЧНО — все вызовы loggerEventCat(EM_EV_NTP, ...)
//          заменены на loggerEventNoNotify().
//   R10:   SMTP-лог почищен; красивое оформление писем.
//   R10.2: единый формат с Telegram; локализация; подсветка.
//   R10.3: фикс бага с очередью (subject через '\x01').
//   R10.4: периодический отчёт — без рамки.
//   R10.5: фикс uptime-бага (boot после 12:00).
//   R10.6: ЕДИНЫЙ формат для всех уведомлений — 10 строк.
//          Добавлена emailQueueBootReport() — boot-отчёт
//          отправляется безусловно (без маски), отложенно
//          на 3 сек после старта, один раз за сессию.
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

// R10.6: boot-отчёт (безусловный, 10 строк, заголовок 🔄)
void emailQueueBootReport();

bool emailSendTest();
bool emailIsReady();

String   emailStatusCode();
uint32_t emailStatusQueue();

String emailStatusText();

void emailNotify(uint16_t bit, const String &logLine);

#endif // EMAIL_H
