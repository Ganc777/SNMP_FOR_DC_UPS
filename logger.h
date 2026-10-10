//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10
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
// R10:   добавлена detectEvtResult() — автоопределение
//        результата события (удача/неудача/факт) для
//        красивого оформления уведомлений TG/EM.
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
// R10: результат события для оформления уведомлений.
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
//--------------------------------------------------------------
void loggerEventNoNotify(const char *fmt, ...);

//--------------------------------------------------------------
// R10: автоопределение категории и результата события по тексту.
// Используется в telegram.cpp/email.cpp для оформления.
//--------------------------------------------------------------
uint16_t  detectEvCategory(const char *msg);
EvtResult detectEvtResult(const char *msg);

size_t loggerFileSize();
void   loggerClear();

String loggerTimestamp();

uint32_t loggerMaxBytes();
size_t   loggerTotalSize();

#endif // LOGGER_H
