//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : telegram.cpp
//==============================================================
// Уведомления в Telegram. Единый формат — 10 строк под чертой.
//
// История:
//   R06.0: базовый модуль Telegram.
//   R06.1: telegramStatusCode() / telegramStatusQueue().
//   R07.0: строка "Температура CPU".
//   R07.7: удалены telegramSetLang() и s_lang.
//   R09.0: периодический отчёт привязан к 12:00.
//   R09.1: логирование через loggerEventNoNotify().
//   R10:   эмодзи категории, ✅/❌, разделитель, блок статуса.
//   R10.2: единый формат с Email, локализация, подсветка.
//   R10.3: диагностика маски в лог, q=%u.
//   R10.5: фикс uptime-бага (boot после 12:00).
//   R10.6: ЕДИНЫЙ формат для всех уведомлений — 10 строк:
//          Батарея, Вход, Runtime, Температура, Состояние,
//          NTP, Пропадания, Uptime, IP, Release.
//          Заголовок с эмодзи категории + ✅/❌.
//          - Периодический отчёт: 📊
//          - Boot-отчёт: 🔄, безусловно, отложенно на 3 сек.
//          - Тест: 🧪 ✅.
//          Счётчик пропаданий сбрасывается в отчёте и boot.
//==============================================================

#include "telegram.h"
#include "config.h"
#include "debug.h"
#include "logger.h"
#include "system.h"
#include "ups.h"
#include "adc.h"
#include "wifi.h"
#include "ntp.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h>

//--------------------------------------------------------------
// Внутреннее состояние
//--------------------------------------------------------------
static Config   s_cfg;
static WiFiClientSecure *s_client = nullptr;
static UniversalTelegramBot *s_bot = nullptr;
static bool     s_ready = false;

#define TG_QUEUE_SIZE 20
static String   s_queue[TG_QUEUE_SIZE];
static uint8_t  s_qHead = 0;
static uint8_t  s_qTail = 0;
static uint8_t  s_qCount = 0;

static uint8_t  s_lastReportDay  = 0;
static uint8_t  s_lastReportMon  = 0;
static uint16_t s_lastReportYear = 0;

static const uint8_t REPORT_HOUR = 12;

// R10.6: одноразовый boot-отчёт
static bool     s_bootReportDone = false;
static uint32_t s_bootAtMs       = 0;
static const uint32_t BOOT_REPORT_DELAY_MS = 3000UL;

//--------------------------------------------------------------
// Очередь
//--------------------------------------------------------------
static bool queuePush(const String &msg) {
    if (s_qCount >= TG_QUEUE_SIZE) {
        DbgWarn("TG: queue full, drop oldest");
        s_qHead = (s_qHead + 1) % TG_QUEUE_SIZE;
        s_qCount--;
    }
    s_queue[s_qTail] = msg;
    s_qTail = (s_qTail + 1) % TG_QUEUE_SIZE;
    s_qCount++;
    return true;
}

static bool queuePop(String &out) {
    if (s_qCount == 0) return false;
    out = s_queue[s_qHead];
    s_qHead = (s_qHead + 1) % TG_QUEUE_SIZE;
    s_qCount--;
    return true;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void telegramInit(const Config &cfg) {
    s_cfg = cfg;
    s_ready = false;
    s_bootAtMs = millis();
    s_bootReportDone = false;

    if (cfg.tgToken.length() == 0 || cfg.tgChatId.length() == 0) {
        DbgInfo("TG: token or chat_id empty, module disabled");
        return;
    }

    if (s_client == nullptr) {
        s_client = new WiFiClientSecure();
    }
    s_client->setInsecure();
    s_client->setTimeout(TG_SEND_TIMEOUT_MS / 1000);

    if (s_bot == nullptr) {
        s_bot = new UniversalTelegramBot(cfg.tgToken, *s_client);
    }

    s_ready = true;

    if (ntpIsSynced()) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        if (ti.tm_hour >= REPORT_HOUR) {
            s_lastReportDay  = ti.tm_mday;
            s_lastReportMon  = ti.tm_mon + 1;
            s_lastReportYear = ti.tm_year + 1900;
            DbgInfo("TG: boot after %u:00 — today marked as reported",
                    (unsigned)REPORT_HOUR);
        }
    }

    DbgInfo("TG: ready, chat_id=%s, events=0x%04X, period=%u",
            cfg.tgChatId.c_str(),
            (unsigned)cfg.tgEvents,
            (unsigned)cfg.tgPeriod);
}

void telegramSetConfig(const Config &cfg) {
    s_cfg = cfg;

    if (cfg.tgToken.length() > 0) {
        if (s_client == nullptr) {
            s_client = new WiFiClientSecure();
            s_client->setInsecure();
            s_client->setTimeout(TG_SEND_TIMEOUT_MS / 1000);
        }
        if (s_bot != nullptr) {
            delete s_bot;
            s_bot = nullptr;
        }
        s_bot = new UniversalTelegramBot(cfg.tgToken, *s_client);
    }

    s_ready = (cfg.tgToken.length() > 0 && cfg.tgChatId.length() > 0);
}

bool telegramIsReady() {
    return s_ready;
}

String telegramStatusCode() {
    if (s_cfg.tgToken.length() == 0 || s_cfg.tgChatId.length() == 0)
        return "NOTSET";
    if (!s_cfg.tgEnable)
        return "OFF";
    if (s_qCount > 0)
        return "QUEUE";
    return "ACTIVE";
}

uint32_t telegramStatusQueue() {
    return (uint32_t)s_qCount;
}

String telegramStatusText() {
    if (s_cfg.tgToken.length() == 0 || s_cfg.tgChatId.length() == 0)
        return "Не настроен";
    if (!s_cfg.tgEnable)
        return "Отключён";
    if (s_qCount > 0)
        return "Активен, очередь: " + String(s_qCount);
    return "Активен";
}

//--------------------------------------------------------------
// Отправка одной строки
//--------------------------------------------------------------
static bool sendMessage(const String &html) {
    if (!s_ready || s_bot == nullptr) return false;

    bool ok = s_bot->sendMessage(s_cfg.tgChatId, html, "HTML");

    if (ok) {
        DbgInfo("TG: message sent");
        loggerEventNoNotify("TG:OK");
    } else {
        DbgWarn("TG: send FAILED");
        loggerEventNoNotify("TG:FAIL");
    }
    return ok;
}

//--------------------------------------------------------------
// Эмодзи категории
//--------------------------------------------------------------
static const char* evtEmoji(uint16_t bit) {
    switch (bit) {
        case TG_EV_POWER: return "⚡";
        case TG_EV_BATT:  return "🔋";
        case TG_EV_WOL:   return "⏻";
        case TG_EV_BOOT:  return "🔄";
        case TG_EV_WIFI:  return "📶";
        case TG_EV_SNMP:  return "🚀";
        case TG_EV_NTP:   return "🕐";
        case TG_EV_TEMP:  return "🌡️";
        default:          return "ℹ️";
    }
}

//--------------------------------------------------------------
// Суффикс результата
//--------------------------------------------------------------
static const char* evtResultSuffix(EvtResult res) {
    switch (res) {
        case EVT_RES_OK:   return " ✅";
        case EVT_RES_FAIL: return " ❌";
        default:           return "";
    }
}

//--------------------------------------------------------------
// Разметка [H]...[/H] → <b>...</b>
//--------------------------------------------------------------
static String applyHighlight(const String &in) {
    String out;
    out.reserve(in.length() + 16);
    int pos = 0;
    while (pos < (int)in.length()) {
        int h1 = in.indexOf("[H]", pos);
        if (h1 < 0) {
            out += in.substring(pos);
            break;
        }
        out += in.substring(pos, h1);
        int h2 = in.indexOf("[/H]", h1);
        if (h2 < 0) {
            out += in.substring(h1);
            break;
        }
        out += "<b>";
        out += in.substring(h1 + 3, h2);
        out += "</b>";
        pos = h2 + 4;
    }
    return out;
}

//--------------------------------------------------------------
// Разделитель
//--------------------------------------------------------------
static String separator() {
    return String("\n─────────────────\n");
}

//--------------------------------------------------------------
// Заголовок-фолбэк по категории
//--------------------------------------------------------------
static String categoryTitle(uint16_t bit, const String &lang) {
    bool en = (lang == "en");
    switch (bit) {
        case TG_EV_POWER: return en ? "Power"       : "Питание";
        case TG_EV_BATT:  return en ? "Battery"     : "Батарея";
        case TG_EV_WOL:   return "WoL";
        case TG_EV_BOOT:  return en ? "Reboot"      : "Перезагрузка";
        case TG_EV_WIFI:  return "Wi-Fi";
        case TG_EV_SNMP:  return "SNMP";
        case TG_EV_NTP:   return "NTP";
        case TG_EV_TEMP:  return en ? "Temperature" : "Температура";
        default:          return en ? "Event"       : "Событие";
    }
}

//--------------------------------------------------------------
// R10.6: собрать сообщение — заголовок + разделитель + 10 строк + дата
//--------------------------------------------------------------
static String buildMessage(const String &headerHtml,
                           const String &lang,
                           uint16_t highlightCat) {
    String body = upsBuildBody10(lang, true, highlightCat);
    body = applyHighlight(body);

    String m;
    m.reserve(body.length() + 300);
    m += headerHtml;
    m += separator();
    m += body;
    m += "\n\n<i>";
    if (ntpIsSynced()) {
        m += ntpDateTimeStr();
    } else {
        m += "up " + sysFormatDHMS(sysUptimeSec());
    }
    m += "</i>";
    return m;
}

//--------------------------------------------------------------
// Постановка события в очередь
//--------------------------------------------------------------
void telegramQueueEvent(uint16_t bit, const String &title, const String &body) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;

    if ((s_cfg.tgEvents & bit) == 0) {
        loggerLog(LOGLEVEL_WARN, "TG: event 0x%04X disabled in mask", bit);
        return;
    }

    EvtResult res = detectEvtResult(body.c_str());

    // Заголовок события
    String evtTitle = detectEvtTitle(body.c_str(), s_cfg.uiLang);
    if (evtTitle.length() == 0) {
        evtTitle = categoryTitle(bit, s_cfg.uiLang);
    }

    String header;
    header.reserve(96);
    header += evtEmoji(bit);
    header += evtResultSuffix(res);
    header += " <b>DC UPS: ";
    header += evtTitle;
    header += "</b>";

    String msg = buildMessage(header, s_cfg.uiLang, bit);
    queuePush(msg);
    DbgInfo("TG: event queued (bit=0x%04X, res=%u, q=%u)",
            bit, (unsigned)res, (unsigned)s_qCount);
}

//--------------------------------------------------------------
// Автоматическая постановка события
//--------------------------------------------------------------
void telegramNotify(uint16_t bit, const String &logLine) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;
    telegramQueueEvent(bit, String(), logLine);
}

//--------------------------------------------------------------
// R10.6: периодический отчёт — 📊, 10 строк.
// Сброс счётчика пропаданий.
//--------------------------------------------------------------
void telegramQueueReport(const String &lang) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;
    if (s_cfg.tgPeriod == 0) return;

    String title = i18n("periodic_report", lang);

    String header;
    header.reserve(96);
    header += "📊 <b>DC UPS: ";
    header += title;
    header += "</b>";

    String msg = buildMessage(header, lang, 0);
    queuePush(msg);
    DbgInfo("TG: periodic report queued");

    upsResetPowerLossCount();
}

//--------------------------------------------------------------
// R10.6: boot-отчёт — 🔄, 10 строк.
// Безусловно отправляется один раз после boot (см. telegramLoop).
// Сброс счётчика пропаданий (поскольку это «старт с чистого»).
//--------------------------------------------------------------
void telegramQueueBootReport() {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;

    bool en = (s_cfg.uiLang == "en");
    String title = en ? "Reboot" : "Перезагрузка";

    String header;
    header.reserve(96);
    header += "🔄 <b>DC UPS: ";
    header += title;
    header += "</b>";

    String msg = buildMessage(header, s_cfg.uiLang, 0);
    queuePush(msg);
    DbgInfo("TG: boot report queued");

    upsResetPowerLossCount();
}

//--------------------------------------------------------------
// R10.6: тест — 🧪 ✅, 10 строк.
//--------------------------------------------------------------
bool telegramSendTest() {
    if (!s_ready) return false;
    if (!s_bot) return false;

    String lang = s_cfg.uiLang;
    String title = i18n("test_link", lang);

    String header;
    header.reserve(96);
    header += "🧪 ✅ <b>DC UPS: ";
    header += title;
    header += "</b>";

    String msg = buildMessage(header, lang, 0);
    return sendMessage(msg);
}

//--------------------------------------------------------------
// Проверка расписания периодического отчёта
//--------------------------------------------------------------
static bool reportDueToday(struct tm &ti) {
    if (ti.tm_hour < REPORT_HOUR) return false;

    int y = ti.tm_year + 1900;
    int m = ti.tm_mon + 1;
    int d = ti.tm_mday;

    if (d == s_lastReportDay &&
        m == s_lastReportMon &&
        y == s_lastReportYear) {
        return false;
    }

    switch (s_cfg.tgPeriod) {
        case 1: return true;
        case 2: return (ti.tm_wday == 1);
        case 3: return (d == 1);
        default: return false;
    }
}

void telegramLoop() {
    if (!s_ready) return;

    // R10.6: boot-отчёт через 3 сек после старта — один раз
    if (!s_bootReportDone &&
        (uint32_t)(millis() - s_bootAtMs) >= BOOT_REPORT_DELAY_MS) {
        s_bootReportDone = true;
        telegramQueueBootReport();
    }

    String msg;
    if (queuePop(msg)) {
        sendMessage(msg);
    }

    if (s_cfg.tgEnable && s_cfg.tgPeriod > 0 && ntpIsSynced()) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);

        if (reportDueToday(ti)) {
            s_lastReportDay  = ti.tm_mday;
            s_lastReportMon  = ti.tm_mon + 1;
            s_lastReportYear = ti.tm_year + 1900;
            telegramQueueReport(s_cfg.uiLang);
        }
    }
}
