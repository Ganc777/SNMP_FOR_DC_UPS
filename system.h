//==============================================================
// SNMP_FOR_DC_UPS
// Release : R06.0
// File    : system.h
//==============================================================
// R06.0: глобальная переменная температуры платы (g_lastTempC)
//        вынесена в system — чтобы её видели все модули
//        (в т.ч. telegram.cpp).
//==============================================================

#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>

// Аптайм
uint32_t sysUptimeSec();
String   sysUptimeStr();

// Перезапуск
void sysRestart();
void sysScheduleRestart(uint32_t delayMs);
void sysLoop();

// Форматирование времени
String sysFormatHMS(uint32_t seconds);
String sysFormatDHMS(uint32_t seconds);

// Утилиты
template <typename T>
T sysClamp(T v, T lo, T hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool sysIsFloat(const String &s);
bool sysIsInt(const String &s);

// Температура платы (встроенный датчик ESP32)
float sysReadTemperature();

//--------------------------------------------------------------
// R06.0: глобальная температура платы, обновляется из loop().
// Доступна всем модулям (telegram.cpp, web.cpp и т.д.).
//--------------------------------------------------------------
extern float g_lastTempC;

// Обновить значение (читает датчик и сохраняет в g_lastTempC)
float sysUpdateTemperature();

#endif // SYSTEM_H
