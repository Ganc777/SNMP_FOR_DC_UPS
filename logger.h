//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.1
// File    : logger.h
//==============================================================
// R04.2: размер буфера лога вычисляется автоматически.
// R06.0: loggerEvent() дублирует событие в Telegram.
// R07.6.3: экспорт loggerTotalSize() (log.txt + log.old).
// R07.7: удалены loggerReadAll(), loggerSetTimeSource(),
//        loggerHasRealTime() — не использовались.
// R08.2: глобальный флаг g_logEventsOnly — режим "только
//        события" (без регулярного [LOG]).
// R09.1: добавлена loggerEventNoNotify() — событие в лог
//        БЕЗ дублирования в Telegram/Email. Нужна для
//        внутреннего логирования факта отправки из
//        telegram.cpp/email.cpp, чтобы не было рекурсии.
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
// R08.2: глобальный флаг «только события».
//--------------------------------------------------------------
extern bool g_logEventsOnly;

//--------------------------------------------------------------
// API
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

//--------------------------------------------------------------
// R09.1: событие БЕЗ дублирования в Telegram/Email.
// Используется внутри telegram.cpp / email.cpp при
// логировании успешной/неудачной отправки, чтобы избежать
// бесконечной рекурсии (loggerEventCat → notify → send →
// loggerEventCat → ...).
//--------------------------------------------------------------
void loggerEventNoNotify(const char *fmt, ...);

size_t loggerFileSize();
void   loggerClear();

String loggerTimestamp();

uint32_t loggerMaxBytes();
size_t   loggerTotalSize();

#endif // LOGGER_H
