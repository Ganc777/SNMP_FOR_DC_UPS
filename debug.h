//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.1
// File    : debug.h
//==============================================================
// Обёртка над Serial для единообразного вывода сообщений.
// Уровни: DBG_ERROR, DBG_WARN, DBG_INFO, DBG_VERBOSE.
//==============================================================

#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// Уровни логирования
#define DBG_LEVEL_NONE    0
#define DBG_LEVEL_ERROR   1
#define DBG_LEVEL_WARN    2
#define DBG_LEVEL_INFO    3
#define DBG_LEVEL_VERBOSE 4

// Текущий уровень (можно менять из config или из дашборда в будущем)
#ifndef DBG_LEVEL
  #define DBG_LEVEL DBG_LEVEL_INFO
#endif

// Инициализация UART
void debugInit(uint32_t baud = 115200);

// Базовые функции вывода
void dbgPrintf(const char *tag, const char *fmt, ...);

// Макросы для удобства
#if DBG_LEVEL >= DBG_LEVEL_ERROR
  #define DbgError(fmt, ...)  dbgPrintf("ERR", fmt, ##__VA_ARGS__)
#else
  #define DbgError(fmt, ...)  do {} while (0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_WARN
  #define DbgWarn(fmt, ...)   dbgPrintf("WRN", fmt, ##__VA_ARGS__)
#else
  #define DbgWarn(fmt, ...)   do {} while (0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_INFO
  #define DbgInfo(fmt, ...)   dbgPrintf("INF", fmt, ##__VA_ARGS__)
#else
  #define DbgInfo(fmt, ...)   do {} while (0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_VERBOSE
  #define DbgVerb(fmt, ...)   dbgPrintf("VRB", fmt, ##__VA_ARGS__)
#else
  #define DbgVerb(fmt, ...)   do {} while (0)
#endif

#endif // DEBUG_H
