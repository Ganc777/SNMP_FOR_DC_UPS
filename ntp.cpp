//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.0
// File    : ntp.cpp
//==============================================================
// R04.2.1: принудительный tzset() перед каждым localtime_r().
// R07.7: убраны вызовы loggerSetTimeSource() — функция удалена.
// R09.0: периодическая синхронизация раз в сутки в 11:30.
//        Синхронизация при старте — только если время ещё не
//        синхронизировано. Логирование NTP: sync OK / FAILED.
//==============================================================

#include "ntp.h"
#include "config.h"
#include "debug.h"
#include "wifi.h"
#include "logger.h"
#include <time.h>
#include <sys/time.h>

static bool     s_ntpUse     = true;
static bool     s_synced     = false;
static String   s_server1;
static String   s_server2;
static String   s_tz;

// R09.0: расписание — раз в сутки в 11:30
static const uint8_t NTP_HOUR   = 11;
static const uint8_t NTP_MINUTE = 30;

// Последняя дата, когда синхронизация уже выполнялась (день месяца)
static uint8_t  s_lastSyncDay = 0;
static bool     s_forceRequested = false;

//--------------------------------------------------------------
// Применить TZ (принудительно)
//--------------------------------------------------------------
static void applyTz() {
    setenv("TZ", s_tz.c_str(), 1);
    tzset();
}

//--------------------------------------------------------------
// Низкоуровневая синхронизация с логом
// Возвращает true при успехе.
//--------------------------------------------------------------
static bool doSync(const char *reason) {
    applyTz();
    configTime(0, 0, s_server1.c_str(), s_server2.c_str());

    uint32_t t0 = millis();
    while (millis() - t0 < 10000UL) {
        time_t t = time(nullptr);
        if (t > 1600000000) {
            s_synced = true;
            struct tm ti;
            applyTz();
            localtime_r(&t, &ti);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
            DbgInfo("NTP: sync OK (%s), time = %s", reason, buf);
            loggerEvent("NTP: time synced (%s)", reason);
            return true;
        }
        delay(200);
    }
    DbgWarn("NTP: sync FAILED (%s)", reason);
    loggerEvent("NTP: sync FAILED (%s)", reason);
    return false;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void ntpInit(const Config &cfg) {
    s_ntpUse  = cfg.ntpUse;
    s_server1 = cfg.ntpServer1;
    s_server2 = cfg.ntpServer2;
    s_tz      = cfg.ntpTz;

    if (!s_ntpUse) {
        DbgInfo("NTP: disabled in config");
        return;
    }

    applyTz();

    // R09.0: при старте синхронизируемся только если время ещё не установлено
    time_t t = time(nullptr);
    if (t > 1600000000) {
        s_synced = true;
        struct tm ti;
        applyTz();
        localtime_r(&t, &ti);
        s_lastSyncDay = ti.tm_mday;
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
        DbgInfo("NTP: time already set, skip init sync (%s)", buf);
        return;
    }

    if (doSync("init")) {
        struct tm ti;
        time_t now = time(nullptr);
        applyTz();
        localtime_r(&now, &ti);
        s_lastSyncDay = ti.tm_mday;
    }
}

//--------------------------------------------------------------
// Основной цикл
//--------------------------------------------------------------
void ntpLoop() {
    if (!s_ntpUse) return;

    time_t t = time(nullptr);

    // Если время ещё не установлено — пробуем принудительно
    if (t < 1600000000) {
        if (doSync("unsynced")) {
            struct tm ti;
            applyTz();
            localtime_r(&t, &ti);
            s_lastSyncDay = ti.tm_mday;
        }
        return;
    }

    s_synced = true;

    // Обработка ручного запроса
    if (s_forceRequested) {
        s_forceRequested = false;
        if (doSync("manual")) {
            struct tm ti;
            time_t now = time(nullptr);
            applyTz();
            localtime_r(&now, &ti);
            s_lastSyncDay = ti.tm_mday;
        }
        return;
    }

    // R09.0: проверка расписания 11:30 раз в сутки
    struct tm ti;
    applyTz();
    localtime_r(&t, &ti);

    if (ti.tm_mday != s_lastSyncDay &&
        ti.tm_hour == NTP_HOUR &&
        ti.tm_min >= NTP_MINUTE) {
        if (doSync("schedule 11:30")) {
            s_lastSyncDay = ti.tm_mday;
        }
    }
}

//--------------------------------------------------------------
// Принудительная синхронизация (кнопка)
//--------------------------------------------------------------
void ntpForceSync() {
    if (!s_ntpUse) {
        DbgWarn("NTP: force sync, but NTP disabled");
        return;
    }
    DbgInfo("NTP: force sync requested");
    s_forceRequested = true;
}

//--------------------------------------------------------------
// Проверка синхронизации
//--------------------------------------------------------------
bool ntpIsSynced() {
    if (!s_ntpUse) return false;
    return s_synced && (time(nullptr) > 1600000000);
}

//--------------------------------------------------------------
// Строки времени
//--------------------------------------------------------------
String ntpDateTimeStr() {
    if (!ntpIsSynced()) return String("--:--:-- --.--.----");
    applyTz();
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    return String(buf);
}

String ntpDateStr() {
    if (!ntpIsSynced()) return String("--.--.----");
    applyTz();
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &ti);
    return String(buf);
}

String ntpTimeStr() {
    if (!ntpIsSynced()) return String("--:--:--");
    applyTz();
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return String(buf);
}

uint32_t ntpUnixTime() {
    if (!ntpIsSynced()) return 0;
    return (uint32_t)time(nullptr);
}
