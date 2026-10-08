//==============================================================
// SNMP_FOR_DC_UPS
// Release : R07.11
// File    : system.cpp
//==============================================================
// R06.0: g_lastTempC вынесена сюда, добавлена sysUpdateTemperature().
// R07.10: сглаживание — медиана 11 значений + отсечка сверху 53.0.
// R07.11: отсечка сверху убрана — она маскировала реальный
//         перегрев. Датчик ESP32 имеет встроенный потолок
//         ~53.3 °C (ограничение формулы в ESP-IDF), выше
//         не покажет физически. Показываем всё, что может,
//         сглаживаем медианой 11 значений.
//==============================================================

#include "system.h"
#include "config.h"
#include "debug.h"

static bool     s_restartPending  = false;
static uint32_t s_restartAtMs     = 0;

//--------------------------------------------------------------
// R06.0: глобальная температура платы
//--------------------------------------------------------------
float g_lastTempC = 0.0f;

//--------------------------------------------------------------
// Аптайм
//--------------------------------------------------------------
uint32_t sysUptimeSec() {
    return (uint32_t)(millis() / 1000UL);
}

String sysUptimeStr() {
    return sysFormatDHMS(sysUptimeSec());
}

//--------------------------------------------------------------
// Перезапуск
//--------------------------------------------------------------
void sysRestart() {
    DbgWarn("System restart NOW");
    Serial.flush();
    delay(100);
    ESP.restart();
}

void sysScheduleRestart(uint32_t delayMs) {
    s_restartPending = true;
    s_restartAtMs    = millis() + delayMs;
    DbgWarn("Restart scheduled in %lu ms", (unsigned long)delayMs);
}

void sysLoop() {
    if (s_restartPending && (int32_t)(millis() - s_restartAtMs) >= 0) {
        sysRestart();
    }
}

//--------------------------------------------------------------
// Форматирование времени
//--------------------------------------------------------------
String sysFormatHMS(uint32_t seconds) {
    uint32_t h = seconds / 3600UL;
    uint32_t m = (seconds % 3600UL) / 60UL;
    uint32_t s = seconds % 60UL;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    return String(buf);
}

String sysFormatDHMS(uint32_t seconds) {
    uint32_t d = seconds / 86400UL;
    uint32_t rest = seconds % 86400UL;

    if (d == 0) return sysFormatHMS(seconds);

    uint32_t h = rest / 3600UL;
    uint32_t m = (rest % 3600UL) / 60UL;
    uint32_t s = rest % 60UL;

    char buf[24];
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
             (unsigned long)d, (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
    return String(buf);
}

//--------------------------------------------------------------
// Проверки строк
//--------------------------------------------------------------
bool sysIsFloat(const String &s) {
    if (s.length() == 0) return false;
    char *end = nullptr;
    String copy = s;
    copy.trim();
    if (copy.length() == 0) return false;
    strtod(copy.c_str(), &end);
    return (end != nullptr) && (*end == '\0');
}

bool sysIsInt(const String &s) {
    if (s.length() == 0) return false;
    char *end = nullptr;
    String copy = s;
    copy.trim();
    if (copy.length() == 0) return false;
    strtol(copy.c_str(), &end, 10);
    return (end != nullptr) && (*end == '\0');
}

//--------------------------------------------------------------
// Температура платы
//--------------------------------------------------------------
float sysReadTemperature() {
    return temperatureRead();
}

//--------------------------------------------------------------
// R07.11: обновление со сглаживанием (медиана 11 значений).
//
// Внутренний датчик ESP32 имеет физический потолок ~53.3 °C
// (ограничение формулы в ESP-IDF). Выше не покажет, даже
// при реальном перегреве кристалла.
//
// Отсечка сверху убрана — показываем всё, что датчик может
// выдать. Сглаживаем медианой 11 значений (~55 сек окна).
//--------------------------------------------------------------
float sysUpdateTemperature() {
    static const uint8_t TEMP_WINDOW = 11;
    static float   s_history[TEMP_WINDOW];
    static uint8_t s_idx   = 0;
    static bool    s_init  = false;

    float raw = temperatureRead();

    if (!s_init) {
        for (uint8_t i = 0; i < TEMP_WINDOW; i++) s_history[i] = raw;
        s_init = true;
    }

    s_history[s_idx] = raw;
    s_idx = (s_idx + 1) % TEMP_WINDOW;

    float sorted[TEMP_WINDOW];
    memcpy(sorted, s_history, sizeof(sorted));

    for (uint8_t i = 1; i < TEMP_WINDOW; i++) {
        float key = sorted[i];
        int8_t j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    g_lastTempC = sorted[TEMP_WINDOW / 2];
    return g_lastTempC;
}
