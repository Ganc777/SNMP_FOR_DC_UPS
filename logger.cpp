//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.2
// File    : logger.cpp
//==============================================================
// Логирование + локализация RU/EN + определение характеристик
// событий по тексту.
//
// История:
//   R05.2: убрано опасное пересоздание файла из writeLine().
//   R06.0: loggerEvent()/loggerEventCat() дублируют событие
//          в Telegram и Email по категории.
//   R07.6.3: двухфайловая ротация log.txt + log.old.
//   R07.7: удалены loggerReadAll() и пр. неиспользуемые функции.
//   R08.2: режим "только события" (g_logEventsOnly).
//   R08.4: снимок [LOG] после [EVT] пишется при ЛЮБОМ периоде,
//          но не чаще, чем раз в 60 сек с момента последней
//          записи [LOG].
//   R09.1: loggerEventNoNotify() — пишет [EVT] в лог, но НЕ
//          ставит в очередь TG/EM, чтобы разорвать рекурсию.
//   R10:   detectEvCategory() вынесена в публичный API;
//          добавлена detectEvtResult().
//   R10.2: - detectEvtTitle() — заголовок с действием и локализацией.
//          - i18n() — таблица локализации RU/EN.
//          - Компактный [LOG]: 7.76V/72%|12.21V/OK|2770s|53.3C.
//          - Timestamp без NTP — "up HH:MM:SS".
//          - TG:OK / TG:FAIL / EM:OK / EM:FAIL.
//          - NTP-лог: "NTP synced (sched, 1/3)" / "NTP FAILED (sched, 3/3)".
//==============================================================

#include "logger.h"
#include "config.h"
#include "debug.h"
#include "system.h"
#include "ntp.h"
#include "ups.h"
#include "telegram.h"
#include "email.h"
#include <LittleFS.h>
#include <stdarg.h>

static bool     s_fsReady       = false;
static uint32_t s_lastVoltLogMs = 0;
static uint32_t s_lastLogMs     = 0;
static uint32_t s_maxBytes      = 16384UL;

bool g_logEventsOnly = false;

static const uint32_t SNAPSHOT_MIN_MS = 60000UL;

//==============================================================
// ЛОКАЛИЗАЦИЯ RU/EN
//==============================================================

struct I18nEntry {
    const char *key;
    const char *ru;
    const char *en;
};

static const I18nEntry I18N_TABLE[] = {
    { "battery",         "Батарея",              "Battery" },
    { "input",           "Вход",                 "Input" },
    { "runtime",         "Runtime",              "Runtime" },
    { "cpu_temp",        "Температура CPU",      "CPU temp" },
    { "state",           "Состояние",            "State" },
    { "ntp",             "NTP",                  "NTP" },
    { "uptime",          "Uptime",               "Uptime" },
    { "ip",              "IP",                   "IP" },
    { "power_losses",    "Пропаданий питания",   "Power losses" },
    { "ntp_synced",      "синхронизировано",     "synced" },
    { "ntp_not_synced",  "не синхронизировано",  "not synced" },
    { "unit_v",          "В",                    "V" },
    { "unit_min",        "мин",                  "min" },
    { "unit_sec",        "с",                    "s" },
    { "unit_c",          "°C",                   "°C" },
    { "test_link",       "Тест связи",           "Test message" },
    { "periodic_report", "Периодический отчёт",  "Periodic report" },
    { "unknown",         "Неизвестно",           "Unknown" },
    { "device_online",   "Устройство",           "Device" },
    { "device_online2",  "на связи",             "online" },
    { "release_label",   "Release",              "Release" },
};

static const size_t I18N_TABLE_N =
    sizeof(I18N_TABLE) / sizeof(I18N_TABLE[0]);

String i18n(const char *key, const String &lang) {
    if (!key) return String();
    for (size_t i = 0; i < I18N_TABLE_N; i++) {
        if (strcmp(I18N_TABLE[i].key, key) == 0) {
            return String((lang == "en") ? I18N_TABLE[i].en
                                         : I18N_TABLE[i].ru);
        }
    }
    return String(key);
}

//==============================================================
// ЗАГОЛОВКИ СОБЫТИЙ (с действием)
//==============================================================

struct EvtTitleEntry {
    const char *pattern;
    const char *ru;
    const char *en;
};

static const EvtTitleEntry EVT_TITLES[] = {
    { "STATE -> INPUT_LOST",  "Питание пропало",              "Power lost" },
    { "INPUT LOST",           "Питание пропало",              "Power lost" },
    { "INPUT RESTORED",       "Питание восстановлено",        "Power restored" },
    { "STATE -> NORMAL",      "Питание восстановлено",        "Power restored" },
    { "STATE -> LOW_BATT",    "Батарея разряжена",            "Battery low" },
    { "LOW_BATT",             "Батарея разряжена",            "Battery low" },
    { "STATE -> CRITICAL",    "Батарея критически разряжена", "Battery critical" },
    { "CRITICAL",             "Батарея критически разряжена", "Battery critical" },
    { "BATT_RECOVERED",       "Батарея восстановлена",        "Battery recovered" },
    { "WoL sent",             "WoL отправлен",                "WoL sent" },
    { "Device boot",          "Перезагрузка",                 "Reboot" },
    { "WiFi connected",       "Wi-Fi подключён",              "Wi-Fi connected" },
    { "WiFi lost",            "Wi-Fi потерян",                "Wi-Fi lost" },
    { "SNMP event sent",      "SNMP-уведомление",             "SNMP notification" },
    { "NTP: time synced",     "NTP синхронизировано",         "NTP synced" },
    { "NTP synced",           "NTP синхронизировано",         "NTP synced" },
    { "NTP: sync FAILED",     "NTP не синхронизировано",      "NTP sync failed" },
    { "NTP FAILED",           "NTP не синхронизировано",      "NTP sync failed" },
    { "TEMP_HIGH",            "Перегрев CPU",                 "CPU overheat" },
    { "TEMP_OK",              "Температура в норме",          "Temperature normal" },
    { "TG: message sent OK",  "Telegram отправлено",          "Telegram sent" },
    { "TG:OK",                "Telegram отправлено",          "Telegram sent" },
    { "TG: send FAILED",      "Telegram не отправлено",       "Telegram failed" },
    { "TG:FAIL",              "Telegram не отправлено",       "Telegram failed" },
    { "EM: mail sent OK",     "Email отправлено",             "Email sent" },
    { "EM:OK",                "Email отправлено",             "Email sent" },
    { "EM: send FAILED",      "Email не отправлено",          "Email failed" },
    { "EM:FAIL",              "Email не отправлено",          "Email failed" },
};

static const size_t EVT_TITLES_N =
    sizeof(EVT_TITLES) / sizeof(EVT_TITLES[0]);

String detectEvtTitle(const char *msg, const String &lang) {
    if (!msg) return String();
    for (size_t i = 0; i < EVT_TITLES_N; i++) {
        if (strstr(msg, EVT_TITLES[i].pattern)) {
            return String((lang == "en") ? EVT_TITLES[i].en
                                         : EVT_TITLES[i].ru);
        }
    }
    return String();
}

uint16_t detectEvCategory(const char *msg) {
    if (!msg) return 0;

    if (strstr(msg, "STATE ->"))        return TG_EV_POWER;
    if (strstr(msg, "INPUT LOST"))      return TG_EV_POWER;
    if (strstr(msg, "INPUT RESTORED"))  return TG_EV_POWER;
    if (strstr(msg, "LOW_BATT"))        return TG_EV_BATT;
    if (strstr(msg, "CRITICAL"))        return TG_EV_BATT;
    if (strstr(msg, "BATT"))            return TG_EV_BATT;
    if (strstr(msg, "WoL"))             return TG_EV_WOL;
    if (strstr(msg, "boot"))            return TG_EV_BOOT;
    if (strstr(msg, "Device boot"))     return TG_EV_BOOT;
    if (strstr(msg, "WiFi"))            return TG_EV_WIFI;
    if (strstr(msg, "SNMP"))            return TG_EV_SNMP;
    if (strstr(msg, "NTP"))             return TG_EV_NTP;
    if (strstr(msg, "TEMP"))            return TG_EV_TEMP;
    if (strstr(msg, "TG:"))             return TG_EV_SNMP;
    if (strstr(msg, "EM:"))             return EM_EV_SNMP;
    return 0;
}

EvtResult detectEvtResult(const char *msg) {
    if (!msg) return EVT_RES_FACT;

    if (strstr(msg, "FAILED")     ||
        strstr(msg, "FAIL")       ||
        strstr(msg, "rejected")   ||
        strstr(msg, "timeout")    ||
        strstr(msg, "Timeout")    ||
        strstr(msg, "CRITICAL")   ||
        strstr(msg, "LOW_BATT")   ||
        strstr(msg, "INPUT_LOST") ||
        strstr(msg, "LOST")       ||
        strstr(msg, "lost")       ||
        strstr(msg, "error")      ||
        strstr(msg, "Error")) {
        return EVT_RES_FAIL;
    }

    if (strstr(msg, "OK")          ||
        strstr(msg, "synced")      ||
        strstr(msg, "connected")   ||
        strstr(msg, "RESTORED")    ||
        strstr(msg, "RECOVERED")   ||
        strstr(msg, "NORMAL")) {
        return EVT_RES_OK;
    }

    return EVT_RES_FACT;
}

//--------------------------------------------------------------
// Пересчёт максимального размера буфера от LittleFS
//--------------------------------------------------------------
static void recalcMaxBytes() {
    if (!s_fsReady) return;

    uint32_t total = LittleFS.totalBytes();
    uint32_t reserve = LOG_RESERVE_BYTES;
    uint32_t usable  = (total > reserve) ? (total - reserve) : total;
    uint32_t cap     = (uint32_t)(usable * 0.40);

    if (cap < LOG_MAX_BYTES_MIN) cap = LOG_MAX_BYTES_MIN;
    if (cap > LOG_MAX_BYTES_MAX) cap = LOG_MAX_BYTES_MAX;

    s_maxBytes = cap;
    DbgInfo("Logger: max buffer per file = %u bytes (LittleFS %u, reserve %u)",
            (unsigned)s_maxBytes, (unsigned)total, (unsigned)reserve);
}

static bool rotateLogs();

//--------------------------------------------------------------
// Компактная метка времени.
//--------------------------------------------------------------
String loggerTimestamp() {
    if (ntpIsSynced()) {
        return ntpDateTimeStr();
    }
    return "up " + sysFormatDHMS(sysUptimeSec());
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void loggerInit() {
    if (!LittleFS.begin(true)) {
        DbgError("LittleFS mount FAILED");
        s_fsReady = false;
        return;
    }
    s_fsReady = true;

    recalcMaxBytes();

    if (!LittleFS.exists(LOG_FILE)) {
        File f = LittleFS.open(LOG_FILE, "w");
        if (f) {
            f.println("=== SNMP_FOR_DC_UPS log started ===");
            f.close();
            DbgInfo("Logger: log file created");
        } else {
            DbgError("Logger: cannot create log file");
            s_fsReady = false;
            return;
        }
    } else {
        File f = LittleFS.open(LOG_FILE, "r");
        if (!f) {
            DbgWarn("Logger: log.txt exists but cannot open, recreating");
            LittleFS.remove(LOG_FILE);
            File nf = LittleFS.open(LOG_FILE, "w");
            if (nf) {
                nf.println("=== log file recreated ===");
                nf.close();
            }
        } else {
            size_t sz = f.size();
            f.close();
            DbgInfo("Logger: existing log.txt = %u bytes", (unsigned)sz);

            if (s_maxBytes > 0 && sz > s_maxBytes) {
                DbgWarn("Logger: log.txt %u > max %u, rotating at init",
                        (unsigned)sz, (unsigned)s_maxBytes);
                rotateLogs();
            }
        }
    }

    if (LittleFS.exists(LOG_FILE_OLD)) {
        File f = LittleFS.open(LOG_FILE_OLD, "r");
        if (f) {
            size_t sz = f.size();
            f.close();
            DbgInfo("Logger: existing log.old = %u bytes", (unsigned)sz);
        }
    }

    DbgInfo("Logger ready, log.txt = %u, log.old = %u, max per file = %u",
            (unsigned)loggerFileSize(),
            (unsigned)(loggerTotalSize() - loggerFileSize()),
            (unsigned)s_maxBytes);
}

//--------------------------------------------------------------
// Двухфайловая ротация.
//--------------------------------------------------------------
static bool rotateLogs() {
    if (LittleFS.exists(LOG_FILE_OLD)) {
        if (!LittleFS.remove(LOG_FILE_OLD)) {
            DbgWarn("Logger: rotate — cannot remove old archive");
        }
    }

    if (!LittleFS.rename(LOG_FILE, LOG_FILE_OLD)) {
        DbgError("Logger: rotate — rename failed");
        return false;
    }

    File nf = LittleFS.open(LOG_FILE, "w");
    if (!nf) {
        DbgError("Logger: rotate — cannot create new log.txt");
        return false;
    }
    nf.println("=== new log after rotation ===");
    nf.close();

    size_t oldSz = 0;
    File of = LittleFS.open(LOG_FILE_OLD, "r");
    if (of) { oldSz = of.size(); of.close(); }

    DbgWarn("Logger: rotated, log.old = %u bytes, new log.txt created",
            (unsigned)oldSz);
    return true;
}

//--------------------------------------------------------------
// Запись строки в файл.
//--------------------------------------------------------------
static void writeLine(const String &line) {
    if (!s_fsReady) return;

    File f = LittleFS.open(LOG_FILE, "a");
    if (!f) {
        DbgError("Logger: cannot open for append");
        return;
    }
    f.println(line);
    f.close();

    if (s_maxBytes == 0) return;
    size_t sz = loggerFileSize();
    if (sz > s_maxBytes) {
        rotateLogs();
    }
}

//--------------------------------------------------------------
// Снимок [LOG] — компактный формат.
//--------------------------------------------------------------
static void writeCurrentSnapshot() {
    UpsStatus st = upsGetStatus();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "%.2fV/%u%%|%.2fV/%s|%lus|%.1fC",
             st.batteryVolts,
             st.batteryPercent,
             st.inputVolts,
             st.inputPresent ? "OK" : "LOST",
             (unsigned long)st.runtimeSec,
             g_lastTempC);
    String line = loggerTimestamp() + " [LOG] " + buf;
    writeLine(line);
    s_lastLogMs = millis();
}

//--------------------------------------------------------------
// Записать обычное сообщение
//--------------------------------------------------------------
void loggerLog(LogLevel level, const char *fmt, ...) {
    if (!s_fsReady) return;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const char *tag;
    switch (level) {
        case LOGLEVEL_WARN:  tag = "[WRN]"; break;
        case LOGLEVEL_ERROR: tag = "[ERR]"; break;
        default:             tag = "[INF]"; break;
    }

    String line = loggerTimestamp() + " " + tag + " " + msg;
    writeLine(line);
}

//--------------------------------------------------------------
// Критическое событие
//--------------------------------------------------------------
void loggerEvent(const char *fmt, ...) {
    if (!s_fsReady) return;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    String line = loggerTimestamp() + " [EVT] " + msg;
    writeLine(line);

    if ((uint32_t)(millis() - s_lastLogMs) >= SNAPSHOT_MIN_MS) {
        writeCurrentSnapshot();
    }

    DbgWarn("EVT: %s", msg);

    uint16_t bit = detectEvCategory(msg);
    if (bit != 0) {
        telegramNotify(bit, String(msg));
        emailNotify(bit, String(msg));
    }
}

//--------------------------------------------------------------
// Событие с явно указанной категорией
//--------------------------------------------------------------
void loggerEventCat(uint16_t tgBit, const char *fmt, ...) {
    if (!s_fsReady) return;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    String line = loggerTimestamp() + " [EVT] " + msg;
    writeLine(line);

    if ((uint32_t)(millis() - s_lastLogMs) >= SNAPSHOT_MIN_MS) {
        writeCurrentSnapshot();
    }

    DbgWarn("EVT: %s", msg);

    if (tgBit != 0) {
        telegramNotify(tgBit, String(msg));
        emailNotify(tgBit, String(msg));
    }
}

//--------------------------------------------------------------
// Событие БЕЗ дублирования в Telegram/Email.
//--------------------------------------------------------------
void loggerEventNoNotify(const char *fmt, ...) {
    if (!s_fsReady) return;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    String line = loggerTimestamp() + " [EVT] " + msg;
    writeLine(line);

    if ((uint32_t)(millis() - s_lastLogMs) >= SNAPSHOT_MIN_MS) {
        writeCurrentSnapshot();
    }

    DbgWarn("EVT(no-notify): %s", msg);
}

//--------------------------------------------------------------
// Редкий лог напряжений (компактный формат).
//--------------------------------------------------------------
void loggerLoop(const Config &cfg,
                float batteryVolts,
                float inputVolts,
                uint8_t batteryPercent,
                bool inputPresent,
                uint32_t runtimeSec,
                float tempC) {

    if (!s_fsReady) return;
    if (!cfg.logEnable) return;

    g_logEventsOnly = (cfg.logVoltPeriodSec == 0);

    if (g_logEventsOnly) return;
    if (s_maxBytes == 0) return;

    uint32_t now = millis();
    uint32_t periodMs = (uint32_t)cfg.logVoltPeriodSec * 1000UL;
    if (periodMs == 0) periodMs = 120000UL;

    if (now - s_lastVoltLogMs >= periodMs) {
        s_lastVoltLogMs = now;

        char buf[128];
        snprintf(buf, sizeof(buf),
                 "%.2fV/%u%%|%.2fV/%s|%lus|%.1fC",
                 batteryVolts,
                 batteryPercent,
                 inputVolts,
                 inputPresent ? "OK" : "LOST",
                 (unsigned long)runtimeSec,
                 tempC);
        String line = loggerTimestamp() + " [LOG] " + buf;
        writeLine(line);
        s_lastLogMs = now;
    }
}

//--------------------------------------------------------------
// Размер log.txt
//--------------------------------------------------------------
size_t loggerFileSize() {
    if (!s_fsReady) return 0;
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

//--------------------------------------------------------------
// Суммарный размер log.txt + log.old
//--------------------------------------------------------------
size_t loggerTotalSize() {
    if (!s_fsReady) return 0;
    size_t total = 0;
    File f = LittleFS.open(LOG_FILE, "r");
    if (f) { total += f.size(); f.close(); }
    File o = LittleFS.open(LOG_FILE_OLD, "r");
    if (o) { total += o.size(); o.close(); }
    return total;
}

//--------------------------------------------------------------
// Очистка (оба файла)
//--------------------------------------------------------------
void loggerClear() {
    if (!s_fsReady) return;
    LittleFS.remove(LOG_FILE);
    if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
    File f = LittleFS.open(LOG_FILE, "w");
    if (f) { f.println("=== log cleared ==="); f.close(); }
    DbgInfo("Logger: log cleared (both files)");
}

//--------------------------------------------------------------
// Экспорт максимального размера буфера
//--------------------------------------------------------------
uint32_t loggerMaxBytes() {
    return s_maxBytes;
}
