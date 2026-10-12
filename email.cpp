//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : email.cpp
//==============================================================
// Отправка уведомлений по Email (SMTP). Единый формат — 10 строк.
//
// История:
//   R07.1: AUTH PLAIN + fallback на AUTH LOGIN.
//   R07.2: диагностика emailInit/emailSetConfig/emailSendTest.
//   R07.7: удалены emailSetLang() и s_lang.
//   R09.0: периодический отчёт привязан к 12:00.
//   R09.1: КРИТИЧНО — все вызовы loggerEventCat(EM_EV_NTP, ...)
//          заменены на loggerEventNoNotify().
//   R10:   SMTP-лог почищен; красивое оформление писем.
//   R10.2: единый формат с Telegram; локализация; подсветка.
//   R10.3: ФИКС БАГА — emailNotify() с пустым title → sep==0
//          → пропуск. Решение: subject в emailQueueEvent(),
//          разделитель — '\x01'.
//   R10.4: периодический отчёт — без рамки.
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

static uint8_t  s_lastReportDay  = 0;
static uint8_t  s_lastReportMon  = 0;
static uint16_t s_lastReportYear = 0;

static const uint8_t REPORT_HOUR = 12;

// Разделитель subject / html в очереди (не встречается в тексте)
static const char EM_SEP = '\x01';

// R10.6: одноразовый boot-отчёт
static bool     s_bootReportDone = false;
static uint32_t s_bootAtMs       = 0;
static const uint32_t BOOT_REPORT_DELAY_MS = 3000UL;

//--------------------------------------------------------------
// Очередь
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
// Эмодзи категории
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
// Суффикс результата
//--------------------------------------------------------------
static const char* emEvtResultSuffix(EvtResult res) {
    switch (res) {
        case EVT_RES_OK:   return " ✅";
        case EVT_RES_FAIL: return " ❌";
        default:           return "";
    }
}

//--------------------------------------------------------------
// Заголовок-фолбэк по категории
//--------------------------------------------------------------
static String categoryTitle(uint16_t bit, const String &lang) {
    bool en = (lang == "en");
    switch (bit) {
        case EM_EV_POWER: return en ? "Power"       : "Питание";
        case EM_EV_BATT:  return en ? "Battery"     : "Батарея";
        case EM_EV_WOL:   return "WoL";
        case EM_EV_BOOT:  return en ? "Reboot"      : "Перезагрузка";
        case EM_EV_WIFI:  return "Wi-Fi";
        case EM_EV_SNMP:  return "SNMP";
        case EM_EV_NTP:   return "NTP";
        case EM_EV_TEMP:  return en ? "Temperature" : "Температура";
        default:          return en ? "Event"       : "Событие";
    }
}

//--------------------------------------------------------------
// Разделитель
//--------------------------------------------------------------
static String emSeparator() {
    return String("<div style=\"color:#888;margin:8px 0;\">"
                  "─────────────────</div>");
}

//--------------------------------------------------------------
// Разметка [H]...[/H] → <span style="color:...">...</span>
//--------------------------------------------------------------
static String applyHighlight(const String &in, EvtResult res) {
    const char *color = "#0066cc";
    if (res == EVT_RES_OK)   color = "#3ecb6e";
    if (res == EVT_RES_FAIL) color = "#e84c3c";

    String out;
    out.reserve(in.length() + 32);
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
        out += "<span style=\"color:";
        out += color;
        out += ";font-weight:bold;\">";
        out += in.substring(h1 + 3, h2);
        out += "</span>";
        pos = h2 + 4;
    }
    return out;
}

//--------------------------------------------------------------
// R10.6: тело из 10 строк в HTML (каждая строка — <div>).
//--------------------------------------------------------------
static String emBody10Html(const String &lang, uint16_t highlightCat,
                           EvtResult res) {
    String body = upsBuildBody10(lang, true, highlightCat);
    body = applyHighlight(body, res);

    String f;
    f.reserve(body.length() + 200);

    int pos = 0;
    while (pos < (int)body.length()) {
        int nl = body.indexOf('\n', pos);
        String line;
        if (nl < 0) {
            line = body.substring(pos);
            pos = body.length();
        } else {
            line = body.substring(pos, nl);
            pos = nl + 1;
        }
        if (line.length() > 0) {
            f += "<div>";
            f += line;
            f += "</div>";
        }
    }
    return f;
}

//--------------------------------------------------------------
// Только дата
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
// Формирование HTML-письма: заголовок + разделитель + 10 строк + дата
//--------------------------------------------------------------
static String formatHtmlMessage(const String &headerHtml,
                                const char *titleColor,
                                const String &lang,
                                uint16_t highlightCat,
                                EvtResult res) {
    String m;
    m.reserve(1800);

    m += "<html><body style=\"font-family:Arial,sans-serif;"
         "font-size:14px;color:#222;\">";

    m += "<h3 style=\"margin:0 0 10px 0;color:";
    m += titleColor;
    m += ";\">";
    m += headerHtml;
    m += "</h3>";

    m += emSeparator();
    m += emBody10Html(lang, highlightCat, res);
    m += emTimestampOnly();
    m += "</body></html>";
    return m;
}

//--------------------------------------------------------------
// Отправка письма
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
        loggerEventNoNotify("EM:FAIL (TCP connect)");
        return false;
    }

    String raw;
    if (!smtpWaitCode(client, "220", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM:FAIL (greeting)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "EHLO " FW_DEVICE_NAME)) {
        client.stop();
        loggerEventNoNotify("EM:FAIL (EHLO send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        DbgWarn("EM: EHLO failed, trying HELO");
        if (!smtpSend(client, "HELO " FW_DEVICE_NAME)) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (HELO send)");
            return false;
        }
        if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM:FAIL (EHLO/HELO)");
            client.stop();
            return false;
        }
    }

    bool authOk = false;
    String authErr;

    {
        String user = s_cfg.emUser;
        String pass = s_cfg.emPass;
        size_t payloadLen = 1 + user.length() + 1 + pass.length();
        uint8_t *payload = (uint8_t *)malloc(payloadLen);
        if (!payload) {
            loggerEventNoNotify("EM:FAIL (AUTH malloc)");
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
            loggerEventNoNotify("EM:FAIL (AUTH base64)");
            client.stop();
            return false;
        }

        if (!smtpSend(client, "AUTH PLAIN " + plainB64)) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (AUTH PLAIN send)");
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
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN send)");
            return false;
        }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN rejected)");
            client.stop();
            return false;
        }
        String userB64 = b64Encode(s_cfg.emUser);
        if (userB64.length() == 0) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN user b64)");
            return false;
        }
        if (!smtpSend(client, userB64)) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN user send)");
            return false;
        }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN user rejected)");
            client.stop();
            return false;
        }
        String passB64 = b64Encode(s_cfg.emPass);
        if (passB64.length() == 0) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN pass b64)");
            return false;
        }
        if (!smtpSend(client, passB64)) {
            client.stop();
            loggerEventNoNotify("EM:FAIL (AUTH LOGIN pass send)");
            return false;
        }
        if (!smtpWaitCode(client, "235", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM:FAIL (AUTH)");
            client.stop();
            return false;
        }
        authOk = true;
    }

    if (!authOk) {
        DbgWarn("EM: AUTH failed (%s)", authErr.c_str());
        loggerEventNoNotify("EM:FAIL (AUTH)");
        client.stop();
        return false;
    }

    String from = s_cfg.emFrom.length() ? s_cfg.emFrom : s_cfg.emUser;
    if (!smtpSend(client, "MAIL FROM:<" + from + ">")) {
        client.stop();
        loggerEventNoNotify("EM:FAIL (MAIL FROM send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM:FAIL (MAIL FROM)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "RCPT TO:<" + s_cfg.emTo + ">")) {
        client.stop();
        loggerEventNoNotify("EM:FAIL (RCPT TO send)");
        return false;
    }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        String code = (raw.length() >= 3) ? raw.substring(0, 3) : "?";
        DbgWarn("EM: RCPT TO rejected, code=%s", code.c_str());
        loggerEventNoNotify("EM:FAIL (RCPT TO)");
        client.stop();
        return false;
    }

    if (!smtpSend(client, "DATA")) {
        client.stop();
        loggerEventNoNotify("EM:FAIL (DATA send)");
        return false;
    }
    if (!smtpWaitCode(client, "354", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM:FAIL (DATA)");
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
        loggerEventNoNotify("EM:FAIL (message rejected)");
        client.stop();
        return false;
    }

    smtpSend(client, "QUIT");
    client.stop();

    DbgInfo("EM: mail sent OK (heap=%u)", (unsigned)ESP.getFreeHeap());
    loggerEventNoNotify("EM:OK");
    return true;
}

//--------------------------------------------------------------
// R10.5: отметить день как «уже отправлен», если boot после 12:00.
//--------------------------------------------------------------
static void markTodayAsReportedIfLate() {
    if (!ntpIsSynced()) return;
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_hour >= REPORT_HOUR) {
        s_lastReportDay  = ti.tm_mday;
        s_lastReportMon  = ti.tm_mon + 1;
        s_lastReportYear = ti.tm_year + 1900;
        DbgInfo("EM: boot after %u:00 — today marked as reported",
                (unsigned)REPORT_HOUR);
    }
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void emailInit(const Config &cfg) {
    s_cfg = cfg;
    s_ready = false;
    s_bootAtMs = millis();
    s_bootReportDone = false;

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

    markTodayAsReportedIfLate();

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
// Постановка события в очередь
//--------------------------------------------------------------
void emailQueueEvent(uint16_t bit, const String &title, const String &body) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    if ((s_cfg.emEvents & bit) == 0) {
        loggerLog(LOGLEVEL_WARN, "EM: event 0x%04X disabled in mask", bit);
        return;
    }

    EvtResult res = detectEvtResult(body.c_str());

    // Заголовок события
    String evtTitle = detectEvtTitle(body.c_str(), s_cfg.uiLang);
    if (evtTitle.length() == 0) {
        evtTitle = categoryTitle(bit, s_cfg.uiLang);
    }

    const char *color = "#0066cc";
    if (res == EVT_RES_OK)   color = "#3ecb6e";
    if (res == EVT_RES_FAIL) color = "#e84c3c";

    String header;
    header.reserve(96);
    header += emEvtEmoji(bit);
    header += emEvtResultSuffix(res);
    header += " DC UPS: ";
    header += evtTitle;

    // Subject (без HTML, без цвета)
    String subject = header;

    String html = formatHtmlMessage(header, color, s_cfg.uiLang, bit, res);

    String item;
    item.reserve(subject.length() + html.length() + 4);
    item += subject;
    item += EM_SEP;
    item += html;

    queuePush(item);
    DbgInfo("EM: event queued (bit=0x%04X, res=%u, q=%u)",
            bit, (unsigned)res, (unsigned)s_qCount);
}

//--------------------------------------------------------------
// Автоматическая постановка события
//--------------------------------------------------------------
void emailNotify(uint16_t bit, const String &logLine) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;
    emailQueueEvent(bit, String(), logLine);
}

//--------------------------------------------------------------
// R10.6: периодический отчёт — 📊, 10 строк.
// Сброс счётчика пропаданий.
//--------------------------------------------------------------
void emailQueueReport(const String &lang) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;
    if (s_cfg.emPeriod == 0) return;

    String title = i18n("periodic_report", lang);

    String header;
    header.reserve(96);
    header += "📊 DC UPS: ";
    header += title;

    String html = formatHtmlMessage(header, "#0066cc", lang, 0, EVT_RES_FACT);

    String item;
    item.reserve(header.length() + html.length() + 4);
    item += header;
    item += EM_SEP;
    item += html;

    queuePush(item);
    DbgInfo("EM: periodic report queued");

    upsResetPowerLossCount();
}

//--------------------------------------------------------------
// R10.6: boot-отчёт — 🔄, 10 строк.
// Безусловно, один раз после boot (см. emailLoop).
// Сброс счётчика пропаданий.
//--------------------------------------------------------------
void emailQueueBootReport() {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    bool en = (s_cfg.uiLang == "en");
    String title = en ? "Reboot" : "Перезагрузка";

    String header;
    header.reserve(96);
    header += "🔄 DC UPS: ";
    header += title;

    String html = formatHtmlMessage(header, "#0066cc", s_cfg.uiLang, 0, EVT_RES_FACT);

    String item;
    item.reserve(header.length() + html.length() + 4);
    item += header;
    item += EM_SEP;
    item += html;

    queuePush(item);
    DbgInfo("EM: boot report queued");

    upsResetPowerLossCount();
}

//--------------------------------------------------------------
// R10.6: тест — 🧪 ✅, 10 строк.
//--------------------------------------------------------------
bool emailSendTest() {
    if (!s_ready) return false;

    String lang = s_cfg.uiLang;
    String title = i18n("test_link", lang);

    String header;
    header.reserve(96);
    header += "🧪 ✅ DC UPS: ";
    header += title;

    String html = formatHtmlMessage(header, "#3ecb6e", lang, 0, EVT_RES_OK);
    String subject = header;
    return smtpSendMail(subject, html);
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

    // R10.6: boot-отчёт через 3 сек после старта — один раз
    if (!s_bootReportDone &&
        (uint32_t)(millis() - s_bootAtMs) >= BOOT_REPORT_DELAY_MS) {
        s_bootReportDone = true;
        emailQueueBootReport();
    }

    String item;
    if (queuePop(item)) {
        int sep = item.indexOf(EM_SEP);
        if (sep > 0) {
            String subject = item.substring(0, sep);
            String html    = item.substring(sep + 1);
            if (!smtpSendMail(subject, html)) {
                // Диагностика уже записана внутри smtpSendMail
            }
        } else {
            DbgWarn("EM: malformed queue item (sep=%d)", sep);
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
            emailQueueReport(s_cfg.uiLang);
        }
    }
}
