//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.6
// File    : ntp.cpp
//==============================================================
// R04.2.1: принудительный tzset() перед каждым localtime_r().
// R07.7: убраны вызовы loggerSetTimeSource() — функция удалена.
// R09.0: периодическая синхронизация раз в сутки в 11:30.
// R09.5: КРИТИЧНО — doSync() проверяет реальный статус SNTP
//        через sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED.
//        Добавлена ntpSetConfig() — применение новых серверов
//        без перезагрузки.
// R09.6: NTP всегда включён (поле ntpUse удалено).
//        Окно синхронизации 1 час, время начала задаётся в UI
//        (cfg.ntpSyncHour, cfg.ntpSyncMin).
//        ФИКС: ntpInit() больше не ставит s_lastSyncDay при
//        обнаружении уже установленного времени — иначе после
//        перезагрузки в тот же день плановая синхронизация
//        пропускалась.
//        s_lastSyncDay обновляется даже при неудаче — чтобы
//        не крутиться в цикле внутри окна.
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

// R09.6: время начала окна синхронизации (1 час)
static uint8_t  s_syncHour   = NTP_SYNC_HOUR_DEFAULT;
static uint8_t  s_syncMin    = NTP_SYNC_MIN_DEFAULT;

// Последняя дата (день месяца), когда попытка плановой
// синхронизации уже была (успешная или нет).
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
// R09.6: добавлен комментарий про s_lastSyncDay (не трогаем здесь).
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

    DbgWarn("NTP: sync FAILED (%s), status=%d",
            reason, (int)sntp_get_sync_status());
    loggerEvent("NTP: sync FAILED (%s)", reason);
    return false;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void ntpInit(const Config &cfg) {
    s_server1 = cfg.ntpServer1;
    s_server2 = cfg.ntpServer2;
    s_tz      = cfg.ntpTz;
    s_syncHour = cfg.ntpSyncHour;
    s_syncMin  = cfg.ntpSyncMin;

    DbgInfo("NTP: init, srv1='%s' srv2='%s' TZ=%s syncAt=%02u:%02u",
            s_server1.c_str(), s_server2.c_str(), s_tz.c_str(),
            (unsigned)s_syncHour, (unsigned)s_syncMin);

    applyTz();

    // R09.6: НЕ трогаем s_lastSyncDay даже если время уже установлено.
    // Время может сохраниться в RTC-памяти между перезагрузками,
    // но реальной синхронизации в этот день могло не быть.
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
        struct tm ti;
        time_t now = time(nullptr);
        applyTz();
        localtime_r(&now, &ti);
        s_lastSyncDay = ti.tm_mday;
    }
}

//--------------------------------------------------------------
// R09.5: применить новую конфигурацию NTP без перезагрузки.
// R09.6: обновляет также время начала окна.
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
// R09.6: проверка, находимся ли мы внутри окна синхронизации.
// Окно: [s_syncHour:s_syncMin] .. +NTP_SYNC_WINDOW_MIN минут.
//--------------------------------------------------------------
static bool inSyncWindow(const struct tm &ti) {
    int nowMin   = ti.tm_hour * 60 + ti.tm_min;
    int startMin = s_syncHour * 60 + s_syncMin;
    int endMin   = startMin + NTP_SYNC_WINDOW_MIN;   // 60 минут

    // Окно не может пересекать полночь (по ТЗ — окно 1 час,
    // разумно всегда внутри одних суток). Если пользователь
    // поставит 23:30 — окно будет 23:30..24:29, что уже за
    // пределами суток. Поэтому ограничим: последнее начало
    // окна — 22:59.
    if (endMin > 24 * 60) {
        endMin = 24 * 60;   // обрезаем до конца суток
    }

    return (nowMin >= startMin && nowMin < endMin);
}

//--------------------------------------------------------------
// Основной цикл
//--------------------------------------------------------------
void ntpLoop() {
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

    // R09.6: проверка окна синхронизации
    struct tm ti;
    applyTz();
    localtime_r(&t, &ti);

    if (ti.tm_mday != s_lastSyncDay && inSyncWindow(ti)) {
        DbgInfo("NTP: schedule window reached (%02u:%02u-%02u:%02u), attempting sync",
                (unsigned)s_syncHour, (unsigned)s_syncMin,
                (unsigned)((s_syncHour + 1) % 24), (unsigned)s_syncMin);
        loggerEvent("NTP: schedule window reached, attempting sync");

        // R09.6: обновляем s_lastSyncDay В ЛЮБОМ СЛУЧАЕ — и при
        // успехе, и при неудаче. Иначе внутри окна doSync() будет
        // вызываться каждые 15 секунд до конца окна.
        doSync("schedule");
        s_lastSyncDay = ti.tm_mday;
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
