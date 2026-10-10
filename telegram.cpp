//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.1
// File    : telegram.cpp
//==============================================================
// Отправка уведомлений через Telegram Bot API.
//
// История:
//   R06.1: добавлены telegramStatusCode() и telegramStatusQueue().
//   R07.0: строка "Температура CPU" вместо "Температура платы".
//   R07.7: удалены telegramSetLang() и s_lang — язык всегда "ru".
//   R09.0: периодический отчёт привязан к 12:00.
//   R09.1: логирование факта отправки — через loggerEventNoNotify()
//          вместо loggerEventCat(). Иначе была бесконечная рекурсия.
//   R10:   красивое оформление уведомлений:
//          - эмодзи категории события (⚡ 🔋 ⏻ 🔄 📶 🚀 🕐 🌡️)
//          - ✅ / ❌ в зависимости от результата (автоопределение)
//          - тонкий разделитель ─────
//          - блок статуса ИБП внизу каждого события
//          - "TG: message sent OK" вместо "TG: mail sent OK"
//   R10.1: убраны буквы за ракетой — "🚀S" → "🚀".
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

// R09.0: последняя дата отправки периодического отчёта
static uint8_t  s_lastReportDay  = 0;
static uint8_t  s_lastReportMon  = 0;
static uint16_t s_lastReportYear = 0;

static const uint8_t REPORT_HOUR = 12;

//--------------------------------------------------------------
// Работа с очередью
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
// R09.1: лог через loggerEventNoNotify — без рекурсии.
// R10:   формулировка "TG: message sent OK".
//--------------------------------------------------------------
static bool sendMessage(const String &html) {
    if (!s_ready || s_bot == nullptr) return false;

    bool ok = s_bot->sendMessage(s_cfg.tgChatId, html, "HTML");

    if (ok) {
        DbgInfo("TG: message sent");
        loggerEventNoNotify("TG: message sent OK");
    } else {
        DbgWarn("TG: send FAILED");
        loggerEventNoNotify("TG: send FAILED");
    }
    return ok;
}

//--------------------------------------------------------------
// R10: подбор эмодзи категории по биту.
// R10.1: убраны буквы за ракетой.
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
// R10: суффикс результата — ✅ / ❌ / пусто.
//--------------------------------------------------------------
static const char* evtResultSuffix(EvtResult res) {
    switch (res) {
        case EVT_RES_OK:   return " ✅";
        case EVT_RES_FAIL: return " ❌";
        default:           return "";
    }
}

//--------------------------------------------------------------
// R10: заголовок события — эмодзи + ✅/❌ + название.
//--------------------------------------------------------------
static String formatHeader(uint16_t bit, EvtResult res,
                           const String &title) {
    String h;
    h.reserve(64);
    h += evtEmoji(bit);
    h += evtResultSuffix(res);
    h += " <b>";
    h += title;
    h += "</b>";
    return h;
}

//--------------------------------------------------------------
// R10: тонкий разделитель.
//--------------------------------------------------------------
static String separator() {
    return String("\n─────────────────\n");
}

//--------------------------------------------------------------
// R10: футер — блок статуса ИБП + дата/время.
//--------------------------------------------------------------
static String formatFooter() {
    String f;
    f.reserve(400);

    f += "\n";
    f += upsFormatStatusBlock(true);
    f += "\n\n<i>";
    if (ntpIsSynced()) {
        f += ntpDateTimeStr();
    } else {
        f += "up " + sysFormatDHMS(sysUptimeSec());
    }
    f += "</i>";
    return f;
}

//--------------------------------------------------------------
// R10: полное сообщение события.
//--------------------------------------------------------------
static String formatEventMessage(uint16_t bit, EvtResult res,
                                 const String &title,
                                 const String &body) {
    String m;
    m.reserve(600);

    m += formatHeader(bit, res, title);
    m += separator();
    if (body.length() > 0) {
        m += body;
    }
    m += formatFooter();
    return m;
}

//--------------------------------------------------------------
// Общее формирование сообщения (для отчётов и теста).
//--------------------------------------------------------------
static String formatMessage(const String &title, const String &body) {
    String m;
    m.reserve(600);
    m += title;
    m += separator();
    if (body.length() > 0) {
        m += body;
    }
    m += formatFooter();
    return m;
}

void telegramQueueEvent(uint16_t bit, const String &title, const String &body) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;

    if ((s_cfg.tgEvents & bit) == 0) {
        DbgVerb("TG: event 0x%04X disabled in mask", bit);
        return;
    }

    EvtResult res = detectEvtResult(body.c_str());
    String msg = formatEventMessage(bit, res, title, body);
    queuePush(msg);
    DbgInfo("TG: event queued (bit=0x%04X, res=%u)", bit, (unsigned)res);
}

void telegramNotify(uint16_t bit, const String &logLine) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;

    String title;
    switch (bit) {
        case TG_EV_POWER: title = "DC UPS: Питание";      break;
        case TG_EV_BATT:  title = "DC UPS: Батарея";      break;
        case TG_EV_WOL:   title = "DC UPS: WoL";          break;
        case TG_EV_BOOT:  title = "DC UPS: Перезагрузка"; break;
        case TG_EV_WIFI:  title = "DC UPS: Wi-Fi";        break;
        case TG_EV_SNMP:  title = "DC UPS: SNMP";         break;
        case TG_EV_NTP:   title = "DC UPS: NTP";          break;
        case TG_EV_TEMP:  title = "DC UPS: Температура";  break;
        default:          title = "DC UPS";               break;
    }

    String body;
    body.reserve(logLine.length() + 16);
    for (size_t i = 0; i < logLine.length(); i++) {
        char c = logLine[i];
        if (c == '<')      body += "&lt;";
        else if (c == '>') body += "&gt;";
        else if (c == '&') body += "&amp;";
        else               body += c;
    }

    telegramQueueEvent(bit, title, body);
}

//--------------------------------------------------------------
// R09.0 / R10: периодический отчёт.
//--------------------------------------------------------------
void telegramQueueReport(const String &lang) {
    if (!s_ready) return;
    if (!s_cfg.tgEnable) return;
    if (s_cfg.tgPeriod == 0) return;

    UpsStatus st = upsGetStatus();

    String title = "DC UPS: периодический отчёт";

    String body;
    body.reserve(400);
    body += "🔋 Батарея: <b>" + String(st.batteryVolts, 3) + " В</b> ("
            + String(st.batteryPercent) + "%)\n";
    body += "⚡ Вход:    <b>" + String(st.inputVolts, 3) + " В</b> ("
            + String(st.inputPresent ? "OK" : "LOST") + ")\n";
    body += "⏱ Runtime: " + String((unsigned long)(st.runtimeSec / 60))
            + " мин " + String((unsigned long)(st.runtimeSec % 60)) + " с\n";
    body += "🌡️ Температура CPU: " + String(g_lastTempC, 1) + " °C\n";
    body += "ℹ️ Состояние: " + String(upsStateName(st.state)) + "\n";
    body += "🕐 Uptime:  " + sysUptimeStr() + "\n";
    body += "🌐 IP: " + wifiGetIP();

    String m;
    m.reserve(700);
    m += "📊 <b>";
    m += title;
    m += "</b>";
    m += separator();
    m += body;
    m += "\n\n<i>";
    if (ntpIsSynced()) {
        m += ntpDateTimeStr();
    } else {
        m += "up " + sysFormatDHMS(sysUptimeSec());
    }
    m += "</i>";

    queuePush(m);
    DbgInfo("TG: periodic report queued");
}

//--------------------------------------------------------------
// R10: тестовое сообщение с зелёной галочкой.
// БЕЗ блока статуса ИБП — тест канала, не ИБП.
//--------------------------------------------------------------
bool telegramSendTest() {
    if (!s_ready) return false;
    if (!s_bot) return false;

    String title = "DC UPS: тест связи";

    String body;
    body.reserve(220);
    body += "Устройство <b>";
    body += FW_DEVICE_NAME;
    body += "</b> на связи.\n";
    body += "Release: " FW_RELEASE "\n";
    body += "🌐 IP: " + wifiGetIP();

    String m;
    m.reserve(500);
    m += "🧪 ✅ <b>";
    m += title;
    m += "</b>";
    m += separator();
    m += body;
    m += "\n\n<i>";
    if (ntpIsSynced()) {
        m += ntpDateTimeStr();
    } else {
        m += "up " + sysFormatDHMS(sysUptimeSec());
    }
    m += "</i>";

    return sendMessage(m);
}

//--------------------------------------------------------------
// R09.0: проверка расписания периодического отчёта
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
            telegramQueueReport("ru");
        }
    }
}
