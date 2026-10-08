//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.1
// File    : debug.cpp
//==============================================================
// Реализация отладочного вывода в UART.
//==============================================================

#include "debug.h"
#include "config.h"
#include "system.h"
#include <stdarg.h>

//--------------------------------------------------------------
// Инициализация UART
//--------------------------------------------------------------
void debugInit(uint32_t baud) {
    Serial.begin(baud);
    delay(50);
    Serial.println();
    Serial.println("================================================");
    Serial.println("SNMP_FOR_DC_UPS - DC UPS Monitor for QNAP");
    Serial.print("Release: ");
    Serial.println(FW_RELEASE);
    Serial.println("================================================");
}

//--------------------------------------------------------------
// Универсальный вывод строки с тегом и временем аптайма
//--------------------------------------------------------------
void dbgPrintf(const char *tag, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // Префикс: [ uptime_ms ][ TAG ]
    Serial.printf("[%8lu][%s] %s\n",
                  (unsigned long)millis(),
                  tag,
                  buf);
}
