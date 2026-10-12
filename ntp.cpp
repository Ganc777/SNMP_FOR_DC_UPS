//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.2
// File    : ntp.cpp
//==============================================================
// NTP: окно синхронизации 1 час, время начала настраивается,
// до 3 попыток с интервалом 30 мин. Убрано уведомление о входе
// в окно — остаются только итоговые сообщения.
//
// История:
//   R03.0: базовая NTP-синхронизация.
//   R04.2.1: tzset() перед каждым localtime_r().
//   R07.7: убраны вызовы loggerSetTimeSource().
//   R09.0: периодическая синхронизация раз в сутки в 11:30.
//   R09.5: КРИТИЧНО — doSync() проверяет реальный статус SNTP
//          через sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED.
//          Добавлена ntpSetConfig().
//   R09.6: NTP всегда включён. Окно 1 час. cfg.ntpSyncHour/Min.
//          ФИКС: ntpInit() больше не ставит s_lastSyncDay при
//          обнаружении уже установленного времени.
//          s_lastSyncDay обновляется даже при неудаче.
//   R10.2: - До 3 попыток с интервалом 30 мин (третья может
//            выйти за границу окна — допустимо).
//          - Убрана строка "NTP: schedule window reached, ...".
//          - Лог: "NTP synced (sched, N/3)" / "NTP FAILED (sched, N/3)".
//          - s_lastSyncDay обновляется при ПЕРВОЙ попытке.
//==============================================================

#include "ntp.h"
#include "config.h"
#include "debug.h"
#include "wifi.h"
#include "logger.h"
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

static bool     s_synced     = false;
static String   s_server1;
static String   s_server2;
static String   s_tz;

// Время начала окна синхронизации (1 час)
static uint8_t  s_syncHour   = NTP_SYNC_HOUR_DEFAULT;
static uint8_t  s_syncMin    = NTP_SYNC_MIN_DEFAULT;

// Последняя дата (день месяца), когда попытка плановой
// синхронизации уже была (успешная или нет).
static uint8_t  s_lastSyncDay = 0;
static bool     s_forceRequested = false;

// R10.2: состояние серии попыток
static uint8_t  s_syncAttempts   = 0;      // 0..3
static uint32_t s_lastAttemptMs  = 0;
static bool     s_windowActive   = false;

static const uint32_t RETRY_INTERVAL_MS = 30UL * 60UL * 1000UL;  // 30 мин
static const uint8_t  MAX_ATTEMPTS      = 3;

//--------------------------------------------------------------
// Применить TZ (принудительно)
//--------------------------------------------------------------
static void applyTz() {
    setenv("TZ", s_tz.c_str(), 1);
    tzset();
}

//--------------------------------------------------------------
// Низкоуровневая синхронизация через SNTP-статус.
// Логируется без номера попытки; номер добавляет ntpLoop().
//--------------------------------------------------------------
static bool doSync(const char *reason) {
    applyTz();

    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(0, 0, s_server1.c_str(), s_server2.c_str());

    const uint32_t TIMEOUT_MS = 15000UL;
    uint32_t t0 = millis();

    while (millis() - t0 < TIMEOUT_MS) {
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
                return true;
            }
        }
        delay(200);
    }

    DbgWarn("NTP: sync FAILED (%s), status=%d",
            reason, (int)sntp_get_sync_status());
    return false;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void ntpInit(const Config &cfg) {
    s_server1  = cfg.ntpServer1;
    s_server2  = cfg.ntpServer2;
    s_tz       = cfg.ntpTz;
    s_syncHour = cfg.ntpSyncHour;
    s_syncMin  = cfg.ntpSyncMin;

    DbgInfo("NTP: init, srv1='%s' srv2='%s' TZ=%s syncAt=%02u:%02u",
            s_server1.c_str(), s_server2.c_str(), s_tz.c_str(),
            (unsigned)s_syncHour, (unsigned)s_syncMin);

    applyTz();

    time_t t = time(nullptr);
    if (t > 1600000000) {
        s_synced = true;
        struct tm ti;
        applyTz();
        localtime_r(&t, &ti);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
        DbgInfo("NTP: time already set, skip init sync (%s)", buf);
        return;
    }

    // Время не установлено — пробуем синхронизироваться сразу
    if (doSync("init")) {
        loggerEvent("NTP synced (init)");
        struct tm ti;
        time_t now = time(nullptr);
        applyTz();
        localtime_r(&now, &ti);
        s_lastSyncDay = ti.tm_mday;
    } else {
        loggerEvent("NTP FAILED (init)");
    }
}

//--------------------------------------------------------------
// Применить новую конфигурацию NTP без перезагрузки.
//--------------------------------------------------------------
void ntpSetConfig(const Config &cfg) {
    DbgInfo("NTP: setConfig, srv1='%s' srv2='%s' tz='%s' syncAt=%02u:%02u",
            cfg.ntpServer1.c_str(),
            cfg.ntpServer2.c_str(),
            cfg.ntpTz.c_str(),
            (unsigned)cfg.ntpSyncHour,
            (unsigned)cfg.ntpSyncMin);

    s_server1  = cfg.ntpServer1;
    s_server2  = cfg.ntpServer2;
    s_tz       = cfg.ntpTz;
    s_syncHour = cfg.ntpSyncHour;
    s_syncMin  = cfg.ntpSyncMin;

    applyTz();
}

//--------------------------------------------------------------
// Проверка, находимся ли мы внутри окна синхронизации.
// Окно: [s_syncHour:s_syncMin] .. +NTP_SYNC_WINDOW_MIN минут.
//--------------------------------------------------------------
static bool inSyncWindow(const struct tm &ti) {
    int nowMin   = ti.tm_hour * 60 + ti.tm_min;
    int startMin = s_syncHour * 60 + s_syncMin;
    int endMin   = startMin + NTP_SYNC_WINDOW_MIN;

    if (endMin > 24 * 60) {
        endMin = 24 * 60;
    }

    return (nowMin >= startMin && nowMin < endMin);
}

//--------------------------------------------------------------
// Основной цикл
//--------------------------------------------------------------
void ntpLoop() {
    time_t t = time(nullptr);

    // Время ещё не установлено — пробуем принудительно
    if (t < 1600000000) {
        if (doSync("unsynced")) {
            loggerEvent("NTP synced (unsynced)");
            struct tm ti;
            applyTz();
            localtime_r(&t, &ti);
            s_lastSyncDay = ti.tm_mday;
        } else {
            loggerEvent("NTP FAILED (unsynced)");
        }
        return;
    }

    s_synced = true;

    // Ручная синхронизация — вне расписания
    if (s_forceRequested) {
        s_forceRequested = false;
        if (doSync("manual")) {
            loggerEvent("NTP synced (manual)");
            struct tm ti;
            time_t now = time(nullptr);
            applyTz();
            localtime_r(&now, &ti);
            s_lastSyncDay = ti.tm_mday;
        } else {
            loggerEvent("NTP FAILED (manual)");
        }
        return;
    }

    // Плановое окно
    struct tm ti;
    applyTz();
    localtime_r(&t, &ti);

    bool inWindow = inSyncWindow(ti);

    // Выход из окна — сброс серии попыток
    if (!inWindow && s_windowActive) {
        s_windowActive = false;
        s_syncAttempts = 0;
    }

    // Вне окна и серия не активна — ничего не делаем
    if (!inWindow && s_syncAttempts == 0) return;

    // Заходим в окно впервые — инициализируем серию
    if (inWindow && !s_windowActive) {
        s_windowActive  = true;
        s_syncAttempts  = 0;
        s_lastAttemptMs = 0;
    }

    // Ещё не начинали сегодня? Проверяем день.
    if (s_syncAttempts == 0 && ti.tm_mday == s_lastSyncDay) {
        return;
    }

    // Ещё есть попытки?
    if (s_syncAttempts >= MAX_ATTEMPTS) return;

    // Пора делать попытку?
    bool due = (s_syncAttempts == 0) ||
               ((uint32_t)(millis() - s_lastAttemptMs) >= RETRY_INTERVAL_MS);
    if (!due) return;

    // Обновляем s_lastSyncDay на первой попытке
    if (s_syncAttempts == 0) {
        s_lastSyncDay = ti.tm_mday;
    }

    s_syncAttempts++;
    s_lastAttemptMs = millis();

    char reason[24];
    snprintf(reason, sizeof(reason), "sched, %u/%u",
             (unsigned)s_syncAttempts, (unsigned)MAX_ATTEMPTS);

    bool ok = doSync("sched");
    if (ok) {
        loggerEvent("NTP synced (%s)", reason);
        s_syncAttempts = MAX_ATTEMPTS;   // больше не пытаемся
    } else {
        loggerEvent("NTP FAILED (%s)", reason);
    }
}

//--------------------------------------------------------------
// Принудительная синхронизация (кнопка)
//--------------------------------------------------------------
void ntpForceSync() {
    DbgInfo("NTP: force sync requested");
    s_forceRequested = true;
}

//--------------------------------------------------------------
// Проверка синхронизации
//--------------------------------------------------------------
bool ntpIsSynced() {
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
