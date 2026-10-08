//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.5
// File    : ntp.cpp
//==============================================================
// R04.2.1: принудительный tzset() перед каждым localtime_r().
// R07.7: убраны вызовы loggerSetTimeSource() — функция удалена.
// R09.0: периодическая синхронизация раз в сутки в 11:30.
// R09.5: КРИТИЧНО — doSync() теперь проверяет реальный статус
//        SNTP через sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED,
//        а не просто факт наличия времени в системе. Раньше при
//        сохранённых несуществующих серверах ручная синхронизация
//        отмечалась как успешная, хотя ответа от NTP не было.
//        Добавлена ntpSetConfig() — применение новых серверов
//        без перезагрузки.
//==============================================================

#include "ntp.h"
#include "config.h"
#include "debug.h"
#include "wifi.h"
#include "logger.h"
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

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
// R09.5: низкоуровневая синхронизация через SNTP-статус.
//
// Ключевое отличие от R09.4: раньше проверяли time(nullptr) >
// 1600000000, что давало ложный успех, если время уже было
// установлено в RTC-памяти. Теперь смотрим на реальный статус
// SNTP — SNTP_SYNC_STATUS_COMPLETED достигается только после
// получения ответа от сервера.
//--------------------------------------------------------------
static bool doSync(const char *reason) {
    applyTz();

    // Сброс статуса перед новым запросом
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);

    // Установить SNTP-серверы
    configTime(0, 0, s_server1.c_str(), s_server2.c_str());

    const uint32_t TIMEOUT_MS = 15000UL;   // 15 сек на попытку
    uint32_t t0 = millis();

    while (millis() - t0 < TIMEOUT_MS) {
        // R09.5: единственный достоверный признак успеха —
        // SNTP-статус COMPLETED (SNTP получил ответ от сервера).
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
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
        }
        delay(200);
    }

    // Таймаут без COMPLETED — значит, серверы не ответили
    DbgWarn("NTP: sync FAILED (%s), status=%d",
            reason, (int)sntp_get_sync_status());
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
// R09.5: применить новую конфигурацию NTP без перезагрузки.
// Вызывается из web при сохранении конфига.
//--------------------------------------------------------------
void ntpSetConfig(const Config &cfg) {
    DbgInfo("NTP: setConfig, srv1='%s' srv2='%s' tz='%s' use=%d",
            cfg.ntpServer1.c_str(),
            cfg.ntpServer2.c_str(),
            cfg.ntpTz.c_str(),
            cfg.ntpUse ? 1 : 0);

    s_ntpUse  = cfg.ntpUse;
    s_server1 = cfg.ntpServer1;
    s_server2 = cfg.ntpServer2;
    s_tz      = cfg.ntpTz;

    applyTz();
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
