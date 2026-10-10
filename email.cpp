//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.1
// File    : email.cpp
//==============================================================
// Отправка уведомлений по Email (SMTP).
// Ручной SMTP-диалог через WiFiClientSecure (implicit TLS).
//
// История:
//   R07.1: AUTH PLAIN + fallback на AUTH LOGIN, диагностика SMTP.
//   R07.2: диагностика emailInit/emailSetConfig/emailSendTest.
//   R07.7: удалены emailSetLang() и s_lang — язык всегда "ru".
//   R09.0: периодический отчёт привязан к 12:00.
//   R09.1: КРИТИЧНО — все вызовы loggerEventCat(EM_EV_NTP, ...)
//          заменены на loggerEventNoNotify(). Иначе была
//          бесконечная рекурсия: loggerEventCat → emailNotify →
//          очередь → smtpSendMail → loggerEventCat → ...
//   R10:   - SMTP-лог почищен: промежуточные шаги убраны,
//            остались только:
//              "EM: mail sent OK"           — при успехе
//              "EM: send FAILED (этап)"     — при ошибке
//          - Красивое оформление писем:
//              * эмодзи категории (⚡ 🔋 ⏻ 🔄 📶 🚀 🕐 🌡️)
//              * ✅ / ❌ — автоопределение по тексту события
//              * тонкий разделитель ─────
//              * блок статуса ИБП в серой рамке
//              * цветной заголовок (зелёный/красный/синий)
//          - Тестовое письмо: заголовок с 🧪 ✅, БЕЗ блока статуса.
//   R10.1: - убраны буквы за ракетой — "🚀S" → "🚀".
//          - В периодическом отчёте строки обёрнуты в <div>,
//            чтобы гарантированно отображаться в столбик во всех
//            email-клиентах (в т.ч. Mail.ru web).
//==============================================================

#include "email.h"
#include "config.h"
#include "debug.h"
#include "logger.h"
#include "system.h"
#include "ups.h"
#include "ntp.h"
#include "wifi.h"
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <time.h>

//--------------------------------------------------------------
// Внутреннее состояние
//--------------------------------------------------------------
static Config   s_cfg;
static bool     s_ready = false;

#define EM_QUEUE_SIZE_LOCAL EM_QUEUE_SIZE
static String   s_queue[EM_QUEUE_SIZE_LOCAL];
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
    if (s_qCount >= EM_QUEUE_SIZE_LOCAL) {
        DbgWarn("EM: queue full, drop oldest");
        s_qHead = (s_qHead + 1) % EM_QUEUE_SIZE_LOCAL;
        s_qCount--;
    }
    s_queue[s_qTail] = msg;
    s_qTail = (s_qTail + 1) % EM_QUEUE_SIZE_LOCAL;
    s_qCount++;
    return true;
}

static bool queuePop(String &out) {
    if (s_qCount == 0) return false;
    out = s_queue[s_qHead];
    s_qHead = (s_qHead + 1) % EM_QUEUE_SIZE_LOCAL;
    s_qCount--;
    return true;
}

//--------------------------------------------------------------
// Base64 для AUTH
//--------------------------------------------------------------
static String b64Encode(const uint8_t *data, size_t len) {
    if (len == 0) return String();
    size_t outLen = 0;
    size_t bufLen = ((len + 2) / 3) * 4 + 1;
    unsigned char *buf = (unsigned char *)malloc(bufLen);
    if (!buf) return String();
    int rc = mbedtls_base64_encode(buf, bufLen, &outLen, data, len);
    if (rc != 0) { free(buf); return String(); }
    buf[outLen] = '\0';
    String out = String((char *)buf);
    free(buf);
    return out;
}

static String b64Encode(const String &in) {
    return b64Encode((const uint8_t *)in.c_str(), in.length());
}

//--------------------------------------------------------------
// Ожидание ответа SMTP
//--------------------------------------------------------------
static bool smtpWaitCode(WiFiClientSecure &c, const char *expectCode,
                         uint32_t timeoutMs, String *rawOut = nullptr) {
    uint32_t t0 = millis();
    String line;
    String lastLine;

    while (millis() - t0 < timeoutMs) {
        while (c.available()) {
            char ch = (char)c.read();
            line += ch;
            if (ch == '\n') {
                lastLine = line;
                if (line.length() >= 3) {
                    String code = line.substring(0, 3);
                    if (code == expectCode) {
                        if (line.length() < 4 || line[3] != '-') {
                            if (rawOut) *rawOut = line;
                            return true;
                        }
                        line = "";
                        break;
                    } else {
                        DbgWarn("EM: SMTP unexpected code '%s' (expected %s)",
                                code.c_str(), expectCode);
                        DbgWarn("EM: raw: %s", line.c_str());
                        if (rawOut) *rawOut = line;
                        return false;
                    }
                }
            }
        }
        delay(10);
    }
    DbgWarn("EM: SMTP timeout waiting for %s (last: %s)",
            expectCode, lastLine.c_str());
    if (rawOut) *rawOut = lastLine;
    return false;
}

static bool smtpSend(WiFiClientSecure &c, const String &cmd) {
    if (!c.connected()) return false;
    c.print(cmd);
    c.print("\r\n");
    c.flush();
    return true;
}

//--------------------------------------------------------------
// R10: подбор эмодзи категории по биту.
// R10.1: убраны буквы за ракетой.
//--------------------------------------------------------------
static const char* emEvtEmoji(uint16_t bit) {
    switch (bit) {
        case EM_EV_POWER: return "⚡";
        case EM_EV_BATT:  return "🔋";
        case EM_EV_WOL:   return "⏻";
        case EM_EV_BOOT:  return "🔄";
        case EM_EV_WIFI:  return "📶";
        case EM_EV_SNMP:  return "🚀";
        case EM_EV_NTP:   return "🕐";
        case EM_EV_TEMP:  return "🌡️";
        default:          return "ℹ️";
    }
}

//--------------------------------------------------------------
// R10: суффикс результата — ✅ / ❌ / пусто.
//--------------------------------------------------------------
static const char* emEvtResultSuffix(EvtResult res) {
    switch (res) {
        case EVT_RES_OK:   return " ✅";
        case EVT_RES_FAIL: return " ❌";
        default:           return "";
    }
}

//--------------------------------------------------------------
// R10: разделитель.
//--------------------------------------------------------------
static String emSeparator() {
    return String("<div style=\"color:#888;margin:8px 0;\">"
                  "─────────────────</div>");
}

//--------------------------------------------------------------
// R10: футер — блок статуса ИБП в серой рамке + дата/время.
//--------------------------------------------------------------
static String emFooter() {
    String f;
    f.reserve(600);

    // Блок статуса в серой рамке
    f += "<div style=\"background:#f5f5f5;border-left:3px solid #888;"
         "padding:10px 14px;margin:14px 0;"
         "font-family:Consolas,Monaco,monospace;font-size:13px;"
         "color:#333;white-space:pre-wrap;line-height:1.5;\">";
    f += upsFormatStatusBlock(true);
    f += "</div>";

    // Дата/время
    f += "<div style=\"font-size:12px;color:#888;\">";
    if (ntpIsSynced()) {
        f += ntpDateTimeStr();
    } else {
        f += "up " + sysFormatDHMS(sysUptimeSec());
    }
    f += "</div>";

    return f;
}

//--------------------------------------------------------------
// R10: только дата/время (без блока статуса) — для теста.
//--------------------------------------------------------------
static String emTimestampOnly() {
    String f;
    f += "<div style=\"font-size:12px;color:#888;margin-top:14px;\">";
    if (ntpIsSynced()) {
        f += ntpDateTimeStr();
    } else {
        f += "up " + sysFormatDHMS(sysUptimeSec());
    }
    f += "</div>";
    return f;
}

//--------------------------------------------------------------
// Формирование HTML-письма.
//   withFooter == true  → блок статуса ИБП + дата/время
//   withFooter == false → только дата/время (тест)
//--------------------------------------------------------------
static String formatHtmlMessage(const String &title, const String &body,
                                const char *titleColor = "#0066cc",
                                bool withFooter = true) {
    String m;
    m.reserve(1400);

    m += "<html><body style=\"font-family:Arial,sans-serif;"
         "font-size:14px;color:#222;\">";

    m += "<h3 style=\"margin:0 0 10px 0;color:";
    m += titleColor;
    m += ";\">";
    m += title;
    m += "</h3>";

    m += emSeparator();

    if (body.length() > 0) {
        m += "<div style=\"white-space:pre-wrap;line-height:1.5;\">";
        m += body;
        m += "</div>";
    }

    if (withFooter) {
        m += emFooter();
    } else {
        m += emTimestampOnly();
    }

    m += "</body></html>";
    return m;
}

//--------------------------------------------------------------
// R10: формирование HTML-письма для события.
//--------------------------------------------------------------
static String formatEventHtml(uint16_t bit, EvtResult res,
                              const String &title, const String &body) {
    // Цвет заголовка по результату
    const char *color = "#0066cc";   // синий — факт
    if (res == EVT_RES_OK)   color = "#3ecb6e";   // зелёный
    if (res == EVT_RES_FAIL) color = "#e84c3c";   // красный

    String h;
    h.reserve(80);
    h += emEvtEmoji(bit);
    h += emEvtResultSuffix(res);
    h += " DC UPS: ";
    h += title;

    return formatHtmlMessage(h, body, color, true);
}

//--------------------------------------------------------------
// Собственно отправка письма.
// R09.1: логирование — через loggerEventNoNotify().
// R10:   почищен SMTP-лог. Остаются только итоговые строки:
//          "EM: mail sent OK"           — при успехе
//          "EM: send FAILED (этап)"     — при ошибке
//--------------------------------------------------------------
static bool smtpSendMail(const String &subject, const String &htmlBody) {
    if (!s_ready) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(EM_SEND_TIMEOUT_MS / 1000);

    uint32_t heapBefore = ESP.getFreeHeap();
    DbgInfo("EM: connecting to %s:%u (heap=%u)",
            s_cfg.emSmtpHost.c_str(), (unsigned)s_cfg.emSmtpPort,
            (unsigned)heapBefore);

    if (!client.connect(s_cfg.emSmtpHost.c_str(), s_cfg.emSmtpPort)) {
        DbgWarn("EM: TCP connect FAILED (heap=%u)",
                (unsigned)ESP.getFreeHeap());
        loggerEventNoNotify("EM: send FAILED (TCP connect)");
        return false;
    }

    String raw;
    if (!smtpWaitCode(client, "220", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: send FAILED (greeting)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "EHLO " FW_DEVICE_NAME)) {
        client.stop();
        loggerEventNoNotify("EM: send FAILED (EHLO send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        DbgWarn("EM: EHLO failed, trying HELO");
        if (!smtpSend(client, "HELO " FW_DEVICE_NAME)) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (HELO send)");
            return false;
        }
        if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: send FAILED (EHLO/HELO)");
            client.stop();
            return false;
        }
    }

    //----------------------------------------------------------
    // AUTH: сначала PLAIN, при отказе — LOGIN
    //----------------------------------------------------------
    bool authOk = false;
    String authErr;

    {
        String user = s_cfg.emUser;
        String pass = s_cfg.emPass;
        size_t payloadLen = 1 + user.length() + 1 + pass.length();
        uint8_t *payload = (uint8_t *)malloc(payloadLen);
        if (!payload) {
            loggerEventNoNotify("EM: send FAILED (AUTH malloc)");
            client.stop();
            return false;
        }
        payload[0] = 0;
        memcpy(payload + 1, user.c_str(), user.length());
        payload[1 + user.length()] = 0;
        memcpy(payload + 1 + user.length() + 1, pass.c_str(), pass.length());

        String plainB64 = b64Encode(payload, payloadLen);
        free(payload);

        if (plainB64.length() == 0) {
            loggerEventNoNotify("EM: send FAILED (AUTH base64)");
            client.stop();
            return false;
        }

        if (!smtpSend(client, "AUTH PLAIN " + plainB64)) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH PLAIN send)");
            return false;
        }

        uint32_t t0 = millis();
        String line;
        bool done = false;
        while (millis() - t0 < EM_SEND_TIMEOUT_MS && !done) {
            while (client.available()) {
                char ch = (char)client.read();
                line += ch;
                if (ch == '\n') {
                    if (line.length() >= 3) {
                        String code = line.substring(0, 3);
                        if (code == "235") {
                            authOk = true; done = true; authErr = ""; break;
                        } else if (code == "504" || code == "500" ||
                                   code == "502" || code == "503") {
                            DbgInfo("EM: AUTH PLAIN unsupported (%s), trying LOGIN",
                                    code.c_str());
                            done = true; authErr = "fallback"; break;
                        } else {
                            DbgWarn("EM: AUTH PLAIN FAILED, code=%s",
                                    code.c_str());
                            authErr = code; done = true; break;
                        }
                    }
                }
            }
            if (!done) delay(10);
        }
        if (!done && !authOk) {
            authErr = "timeout";
        }
    }

    if (!authOk && authErr == "fallback") {
        if (!smtpSend(client, "AUTH LOGIN")) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN send)");
            return false;
        }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN rejected)");
            client.stop();
            return false;
        }
        String userB64 = b64Encode(s_cfg.emUser);
        if (userB64.length() == 0) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN user b64)");
            return false;
        }
        if (!smtpSend(client, userB64)) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN user send)");
            return false;
        }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN user rejected)");
            client.stop();
            return false;
        }
        String passB64 = b64Encode(s_cfg.emPass);
        if (passB64.length() == 0) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN pass b64)");
            return false;
        }
        if (!smtpSend(client, passB64)) {
            client.stop();
            loggerEventNoNotify("EM: send FAILED (AUTH LOGIN pass send)");
            return false;
        }
        if (!smtpWaitCode(client, "235", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: send FAILED (AUTH)");
            client.stop();
            return false;
        }
        authOk = true;
    }

    if (!authOk) {
        DbgWarn("EM: AUTH failed (%s)", authErr.c_str());
        loggerEventNoNotify("EM: send FAILED (AUTH)");
        client.stop();
        return false;
    }

    String from = s_cfg.emFrom.length() ? s_cfg.emFrom : s_cfg.emUser;
    if (!smtpSend(client, "MAIL FROM:<" + from + ">")) {
        client.stop();
        loggerEventNoNotify("EM: send FAILED (MAIL FROM send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: send FAILED (MAIL FROM)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "RCPT TO:<" + s_cfg.emTo + ">")) {
        client.stop();
        loggerEventNoNotify("EM: send FAILED (RCPT TO send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        String code = (raw.length() >= 3) ? raw.substring(0, 3) : "?";
        DbgWarn("EM: RCPT TO rejected, code=%s", code.c_str());
        loggerEventNoNotify("EM: send FAILED (RCPT TO)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "DATA")) {
        client.stop();
        loggerEventNoNotify("EM: send FAILED (DATA send)");
        return false;
    }
    if (!smtpWaitCode(client, "354", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: send FAILED (DATA)");
        client.stop();
        return false;
    }

    client.print("From: " + from + "\r\n");
    client.print("To: " + s_cfg.emTo + "\r\n");
    client.print("Subject: " + subject + "\r\n");
    client.print("MIME-Version: 1.0\r\n");
    client.print("Content-Type: text/html; charset=UTF-8\r\n");
    client.print("Content-Transfer-Encoding: 8bit\r\n");
    client.print("\r\n");
    client.print(htmlBody);
    client.print("\r\n");
    client.print(".\r\n");
    client.flush();

    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: send FAILED (message rejected)");
        client.stop();
        return false;
    }

    smtpSend(client, "QUIT");
    client.stop();

    DbgInfo("EM: mail sent OK (heap=%u)", (unsigned)ESP.getFreeHeap());
    loggerEventNoNotify("EM: mail sent OK");
    return true;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void emailInit(const Config &cfg) {
    s_cfg = cfg;
    s_ready = false;

    if (!cfg.emEnable) {
        DbgInfo("EM: disabled in config");
        return;
    }
    if (cfg.emSmtpHost.length() == 0 ||
        cfg.emUser.length() == 0 ||
        cfg.emPass.length() == 0 ||
        cfg.emTo.length() == 0) {
        DbgInfo("EM: not configured (host/user/pass/to empty)");
        return;
    }

    s_ready = true;

    DbgInfo("EM: ready, %s:%u, user=%s, to=%s, period=%u, events=0x%04X",
            cfg.emSmtpHost.c_str(), (unsigned)cfg.emSmtpPort,
            cfg.emUser.c_str(), cfg.emTo.c_str(),
            (unsigned)cfg.emPeriod, (unsigned)cfg.emEvents);
}

void emailSetConfig(const Config &cfg) {
    s_cfg = cfg;

    s_ready = (cfg.emEnable &&
               cfg.emSmtpHost.length() > 0 &&
               cfg.emUser.length() > 0 &&
               cfg.emPass.length() > 0 &&
               cfg.emTo.length() > 0);

    DbgInfo("EM: config updated, ready=%s", s_ready ? "yes" : "no");
}

bool emailIsReady() {
    return s_ready;
}

String emailStatusCode() {
    if (!s_cfg.emEnable) return "OFF";
    if (s_cfg.emSmtpHost.length() == 0 ||
        s_cfg.emUser.length() == 0 ||
        s_cfg.emPass.length() == 0 ||
        s_cfg.emTo.length() == 0) return "NOTSET";
    if (s_qCount > 0) return "QUEUE";
    return "ACTIVE";
}

uint32_t emailStatusQueue() {
    return (uint32_t)s_qCount;
}

String emailStatusText() {
    if (!s_cfg.emEnable) return "Отключён";
    if (s_cfg.emSmtpHost.length() == 0 ||
        s_cfg.emUser.length() == 0 ||
        s_cfg.emPass.length() == 0 ||
        s_cfg.emTo.length() == 0) return "Не настроен";
    if (s_qCount > 0) return "Активен, очередь: " + String(s_qCount);
    return "Активен";
}

//--------------------------------------------------------------
// Постановка события в очередь.
// R10: определяем результат по тексту и формируем красивое письмо.
//--------------------------------------------------------------
void emailQueueEvent(uint16_t bit, const String &title, const String &body) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    if ((s_cfg.emEvents & bit) == 0) {
        DbgVerb("EM: event 0x%04X disabled in mask", bit);
        return;
    }

    EvtResult res = detectEvtResult(body.c_str());
    String html = formatEventHtml(bit, res, title, body);

    String item;
    item.reserve(html.length() + title.length() + 16);
    item += title;
    item += "\n";
    item += html;
    queuePush(item);
    DbgInfo("EM: event queued (bit=0x%04X, res=%u)", bit, (unsigned)res);
}

//--------------------------------------------------------------
// Автоматическая постановка события
//--------------------------------------------------------------
void emailNotify(uint16_t bit, const String &logLine) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    String title;
    switch (bit) {
        case EM_EV_POWER: title = "Питание";      break;
        case EM_EV_BATT:  title = "Батарея";      break;
        case EM_EV_WOL:   title = "WoL";          break;
        case EM_EV_BOOT:  title = "Перезагрузка"; break;
        case EM_EV_WIFI:  title = "Wi-Fi";        break;
        case EM_EV_SNMP:  title = "SNMP";         break;
        case EM_EV_NTP:   title = "NTP";          break;
        case EM_EV_TEMP:  title = "Температура";  break;
        default:          title = "";             break;
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

    emailQueueEvent(bit, title, body);
}

//--------------------------------------------------------------
// R09.0 / R10 / R10.1: периодический отчёт.
// R10.1: каждая строка обёрнута в <div>, чтобы гарантированно
//        отображаться в столбик во всех email-клиентах.
//--------------------------------------------------------------
void emailQueueReport(const String &lang) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;
    if (s_cfg.emPeriod == 0) return;

    UpsStatus st = upsGetStatus();

    String title = "DC UPS: периодический отчёт";

    String body;
    body.reserve(700);
    body += "<div>🔋 Батарея: " + String(st.batteryVolts, 3) + " В ("
            + String(st.batteryPercent) + "%)</div>";
    body += "<div>⚡ Вход:    " + String(st.inputVolts, 3) + " В ("
            + String(st.inputPresent ? "OK" : "LOST") + ")</div>";
    body += "<div>⏱ Runtime: " + String((unsigned long)(st.runtimeSec / 60))
            + " мин " + String((unsigned long)(st.runtimeSec % 60)) + " с</div>";
    body += "<div>🌡️ Температура CPU: " + String(g_lastTempC, 1) + " °C</div>";
    body += "<div>ℹ️ Состояние: " + String(upsStateName(st.state)) + "</div>";
    body += "<div>🕐 Uptime:  " + sysUptimeStr() + "</div>";
    body += "<div>🌐 IP: " + wifiGetIP() + "</div>";

    String h;
    h.reserve(80);
    h += "📊 DC UPS: периодический отчёт";

    String html = formatHtmlMessage(h, body, "#0066cc", true);

    String item;
    item.reserve(html.length() + title.length() + 16);
    item += title;
    item += "\n";
    item += html;
    queuePush(item);
    DbgInfo("EM: periodic report queued");
}

//--------------------------------------------------------------
// R10: тестовое письмо — с зелёной галочкой, БЕЗ блока статуса.
//--------------------------------------------------------------
bool emailSendTest() {
    if (!s_ready) return false;

    String title = "DC UPS: тест связи";

    String body;
    body.reserve(220);
    body += "Устройство " FW_DEVICE_NAME " на связи.\n";
    body += "Release: " FW_RELEASE "\n";
    body += "🌐 IP: " + wifiGetIP();

    String h;
    h.reserve(80);
    h += "🧪 ✅ DC UPS: тест связи";

    // withFooter = false → без блока статуса ИБП, только дата/время
    String html = formatHtmlMessage(h, body, "#3ecb6e", false);
    String subject = "[DC UPS] " + title;
    return smtpSendMail(subject, html);
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

    switch (s_cfg.emPeriod) {
        case 1: return true;
        case 2: return (ti.tm_wday == 1);
        case 3: return (d == 1);
        default: return false;
    }
}

//--------------------------------------------------------------
// Основной цикл
//--------------------------------------------------------------
void emailLoop() {
    if (!s_ready) return;

    String item;
    if (queuePop(item)) {
        int sep = item.indexOf('\n');
        if (sep > 0) {
            String subject = "[DC UPS] " + item.substring(0, sep);
            String html    = item.substring(sep + 1);
            if (!smtpSendMail(subject, html)) {
                // Диагностика уже записана внутри smtpSendMail
            }
        }
    }

    if (s_cfg.emEnable && s_cfg.emPeriod > 0 && ntpIsSynced()) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);

        if (reportDueToday(ti)) {
            s_lastReportDay  = ti.tm_mday;
            s_lastReportMon  = ti.tm_mon + 1;
            s_lastReportYear = ti.tm_year + 1900;
            emailQueueReport("ru");
        }
    }
}
