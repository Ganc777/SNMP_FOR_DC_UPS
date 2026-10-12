//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.2
// File    : logger.h
//==============================================================
// Модуль логирования с двухфайловой ротацией + локализация
// уведомлений (RU/EN) + определение категории/результата/заголовка
// события по тексту.
//
// История:
//   R04.2: размер буфера лога вычисляется автоматически.
//   R06.0: loggerEvent() дублирует событие в Telegram.
//   R07.6.3: экспорт loggerTotalSize() (log.txt + log.old).
//   R07.7: удалены loggerReadAll(), loggerSetTimeSource(),
//          loggerHasRealTime() — не использовались.
//   R08.2: глобальный флаг g_logEventsOnly — режим "только
//          события" (без регулярного [LOG]).
//   R08.4: снимок [LOG] после [EVT] — не чаще 1 раза в 60 сек.
//   R09.1: loggerEventNoNotify() — событие в лог БЕЗ дублирования
//          в Telegram/Email, чтобы разорвать рекурсию.
//   R10:   detectEvCategory(), detectEvtResult().
//   R10.2: - detectEvtTitle() — заголовок события с действием
//            («Питание пропало», «Батарея восстановлена», ...).
//          - i18n() — таблица локализации строк статуса RU/EN.
//          - Компактный формат [LOG]:
//              7.76V/72%|12.21V/OK|2770s|53.3C
//          - Timestamp без NTP — "up HH:MM:SS" (без даты).
//          - Формулировки TG/EM в логе: TG:OK / TG:FAIL / EM:OK / EM:FAIL.
//          - NTP-лог: "NTP synced (sched, 1/3)" / "NTP FAILED (sched, 3/3)".
//==============================================================

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "storage.h"

//--------------------------------------------------------------
// Уровни записей в логе
//--------------------------------------------------------------
enum LogLevel : uint8_t {
    LOGLEVEL_INFO  = 0,
    LOGLEVEL_WARN  = 1,
    LOGLEVEL_ERROR = 2
};

//--------------------------------------------------------------
// Результат события для оформления уведомлений.
//--------------------------------------------------------------
enum EvtResult : uint8_t {
    EVT_RES_FACT = 0,   // просто факт — без ✅/❌
    EVT_RES_OK   = 1,   // удача — ✅
    EVT_RES_FAIL = 2    // неудача — ❌
};

//--------------------------------------------------------------
// R08.2: глобальный флаг «только события».
//--------------------------------------------------------------
extern bool g_logEventsOnly;

//--------------------------------------------------------------
// API логирования
//--------------------------------------------------------------
void loggerInit();

void loggerLoop(const Config &cfg,
                float batteryVolts,
                float inputVolts,
                uint8_t batteryPercent,
                bool inputPresent,
                uint32_t runtimeSec,
                float tempC);

void loggerLog(LogLevel level, const char *fmt, ...);
void loggerEvent(const char *fmt, ...);
void loggerEventCat(uint16_t tgBit, const char *fmt, ...);
void loggerEventNoNotify(const char *fmt, ...);

//--------------------------------------------------------------
// Определение характеристик события по тексту.
//--------------------------------------------------------------
uint16_t  detectEvCategory(const char *msg);
EvtResult detectEvtResult(const char *msg);
String    detectEvtTitle(const char *msg, const String &lang);

//--------------------------------------------------------------
// Локализация строк статуса уведомлений.
//--------------------------------------------------------------
String    i18n(const char *key, const String &lang);

//--------------------------------------------------------------
// Размеры файлов и очистка
//--------------------------------------------------------------
size_t loggerFileSize();
void   loggerClear();

String loggerTimestamp();

uint32_t loggerMaxBytes();
size_t   loggerTotalSize();

#endif // LOGGER_H
