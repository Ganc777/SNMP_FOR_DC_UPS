//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.1
// File    : email.cpp
//==============================================================
// Отправка уведомлений по Email (SMTP).
// Ручной SMTP-диалог через WiFiClientSecure (implicit TLS).
// R07.1: AUTH PLAIN + fallback на AUTH LOGIN, диагностика SMTP.
// R07.2: диагностика emailInit/emailSetConfig/emailSendTest.
// R07.7: удалены emailSetLang() и s_lang — язык всегда "ru".
// R09.0: периодический отчёт привязан к 12:00.
// R09.1: КРИТИЧНО — все вызовы loggerEventCat(EM_EV_NTP, ...)
//        заменены на loggerEventNoNotify(). Иначе была
//        бесконечная рекурсия: loggerEventCat → emailNotify →
//        очередь → smtpSendMail → loggerEventCat → ...
//        Диагностические строки [EVT] EM: * оставлены.
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
// Собственно отправка письма
// R09.1: все loggerEventCat(EM_EV_NTP, ...) → loggerEventNoNotify().
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
    loggerEventNoNotify("EM: connecting to %s:%u",
                s_cfg.emSmtpHost.c_str(), (unsigned)s_cfg.emSmtpPort);

    if (!client.connect(s_cfg.emSmtpHost.c_str(), s_cfg.emSmtpPort)) {
        DbgWarn("EM: TCP connect FAILED (heap=%u)",
                (unsigned)ESP.getFreeHeap());
        loggerEventNoNotify("EM: TCP connect FAILED");
        loggerEventNoNotify("EM: send FAILED");
        return false;
    }
    DbgInfo("EM: TCP connect OK (heap=%u)", (unsigned)ESP.getFreeHeap());
    loggerEventNoNotify("EM: TCP connect OK");

    String raw;
    if (!smtpWaitCode(client, "220", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: greeting FAILED");
        loggerEventNoNotify("EM: send FAILED");
        client.stop();
        return false;
    }
    loggerEventNoNotify("EM: greeting 220 OK");

    if (!smtpSend(client, "EHLO " FW_DEVICE_NAME)) { client.stop(); return false; }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        DbgWarn("EM: EHLO failed, trying HELO");
        if (!smtpSend(client, "HELO " FW_DEVICE_NAME)) { client.stop(); return false; }
        if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: EHLO/HELO FAILED");
            loggerEventNoNotify("EM: send FAILED");
            client.stop();
            return false;
        }
    }
    loggerEventNoNotify("EM: EHLO OK");

    bool authOk = false;
    String authErr;

    {
        String user = s_cfg.emUser;
        String pass = s_cfg.emPass;
        size_t payloadLen = 1 + user.length() + 1 + pass.length();
        uint8_t *payload = (uint8_t *)malloc(payloadLen);
        if (!payload) {
            loggerEventNoNotify("EM: AUTH PLAIN malloc FAILED");
            loggerEventNoNotify("EM: send FAILED");
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
            loggerEventNoNotify("EM: AUTH PLAIN base64 FAILED");
            loggerEventNoNotify("EM: send FAILED");
            client.stop();
            return false;
        }

        loggerEventNoNotify("EM: trying AUTH PLAIN");
        if (!smtpSend(client, "AUTH PLAIN " + plainB64)) { client.stop(); return false; }

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
                            loggerEventNoNotify("EM: AUTH PLAIN unsupported (%s), trying LOGIN",
                                        code.c_str());
                            done = true; authErr = "fallback"; break;
                        } else {
                            loggerEventNoNotify("EM: AUTH PLAIN FAILED, code=%s",
                                        code.c_str());
                            authErr = code; done = true; break;
                        }
                    }
                }
            }
            if (!done) delay(10);
        }
        if (!done && !authOk) {
            loggerEventNoNotify("EM: AUTH PLAIN timeout");
            authErr = "timeout";
        }
    }

    if (!authOk && authErr == "fallback") {
        loggerEventNoNotify("EM: trying AUTH LOGIN");
        if (!smtpSend(client, "AUTH LOGIN")) { client.stop(); return false; }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: AUTH LOGIN not accepted");
            loggerEventNoNotify("EM: send FAILED");
            client.stop();
            return false;
        }
        String userB64 = b64Encode(s_cfg.emUser);
        if (userB64.length() == 0) { client.stop(); return false; }
        if (!smtpSend(client, userB64)) { client.stop(); return false; }
        if (!smtpWaitCode(client, "334", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: AUTH LOGIN user rejected");
            loggerEventNoNotify("EM: send FAILED");
            client.stop();
            return false;
        }
        String passB64 = b64Encode(s_cfg.emPass);
        if (passB64.length() == 0) { client.stop(); return false; }
        if (!smtpSend(client, passB64)) { client.stop(); return false; }
        if (!smtpWaitCode(client, "235", EM_SEND_TIMEOUT_MS, &raw)) {
            loggerEventNoNotify("EM: AUTH LOGIN FAILED (check user/password/app-password)");
            loggerEventNoNotify("EM: send FAILED");
            client.stop();
            return false;
        }
        authOk = true;
    }

    if (!authOk) {
        DbgWarn("EM: AUTH failed (%s)", authErr.c_str());
        loggerEventNoNotify("EM: send FAILED");
        client.stop();
        return false;
    }
    loggerEventNoNotify("EM: AUTH OK");

    String from = s_cfg.emFrom.length() ? s_cfg.emFrom : s_cfg.emUser;
    if (!smtpSend(client, "MAIL FROM:<" + from + ">")) { client.stop(); return false; }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: MAIL FROM rejected");
        loggerEventNoNotify("EM: send FAILED");
        client.stop();
        return false;
    }
    DbgInfo("EM: MAIL FROM OK");

    if (!smtpSend(client, "RCPT TO:<" + s_cfg.emTo + ">")) { client.stop(); return false; }
    if (!smtpWaitCode(client, "250", EM_SEND_TIMEOUT_MS, &raw)) {
        String code = (raw.length() >= 3) ? raw.substring(0, 3) : "?";
        loggerEventNoNotify("EM: RCPT TO rejected, code=%s", code.c_str());
        loggerEventNoNotify("EM: send FAILED");
        client.stop();
        return false;
    }
    loggerEventNoNotify("EM: RCPT TO OK");

    if (!smtpSend(client, "DATA")) { client.stop(); return false; }
    if (!smtpWaitCode(client, "354", EM_SEND_TIMEOUT_MS, &raw)) {
        loggerEventNoNotify("EM: DATA rejected");
        loggerEventNoNotify("EM: send FAILED");
        client.stop();
        return false;
    }
    DbgInfo("EM: DATA OK");

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
        loggerEventNoNotify("EM: message rejected after DATA");
        loggerEventNoNotify("EM: send FAILED");
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
    loggerEvent("EM init: enable=%d host='%s' user='%s' plen=%u to='%s'",
                cfg.emEnable ? 1 : 0,
                cfg.emSmtpHost.c_str(),
                cfg.emUser.c_str(),
                (unsigned)cfg.emPass.length(),
                cfg.emTo.c_str());

    s_cfg = cfg;
    s_ready = false;

    if (!cfg.emEnable) {
        DbgInfo("EM: disabled in config");
        loggerEvent("EM init: DISABLED (emEnable=false)");
        return;
    }
    if (cfg.emSmtpHost.length() == 0 ||
        cfg.emUser.length() == 0 ||
        cfg.emPass.length() == 0 ||
        cfg.emTo.length() == 0) {
        DbgInfo("EM: not configured (host/user/pass/to empty)");
        loggerEvent("EM init: NOT CONFIGURED (host/user/pass/to empty)");
        return;
    }

    s_ready = true;

    DbgInfo("EM: ready, %s:%u, user=%s, to=%s, period=%u, events=0x%04X",
            cfg.emSmtpHost.c_str(), (unsigned)cfg.emSmtpPort,
            cfg.emUser.c_str(), cfg.emTo.c_str(),
            (unsigned)cfg.emPeriod, (unsigned)cfg.emEvents);
    loggerEvent("EM init: READY");
}

void emailSetConfig(const Config &cfg) {
    loggerEvent("EM setConfig: enable=%d host='%s' plen=%u to='%s'",
                cfg.emEnable ? 1 : 0,
                cfg.emSmtpHost.c_str(),
                (unsigned)cfg.emPass.length(),
                cfg.emTo.c_str());

    s_cfg = cfg;

    s_ready = (cfg.emEnable &&
               cfg.emSmtpHost.length() > 0 &&
               cfg.emUser.length() > 0 &&
               cfg.emPass.length() > 0 &&
               cfg.emTo.length() > 0);

    DbgInfo("EM: config updated, ready=%s", s_ready ? "yes" : "no");
    loggerEvent("EM setConfig: ready=%d", s_ready ? 1 : 0);
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
// Формирование HTML-письма
//--------------------------------------------------------------
static String formatHtmlMessage(const String &title, const String &body) {
    String m;
    m.reserve(1024);
    m += "<html><body style=\"font-family:Arial,sans-serif;"
         "font-size:14px;color:#222;\">";
    m += "<h3 style=\"margin:0 0 10px 0;color:#0066cc;\">";
    m += title;
    m += "</h3>";
    if (body.length() > 0) {
        m += "<div style=\"white-space:pre-wrap;line-height:1.4;\">";
        m += body;
        m += "</div>";
    }
    m += "<hr style=\"border:none;border-top:1px solid #ccc;margin:14px 0;\">";
    m += "<div style=\"font-size:12px;color:#888;\">";
    if (ntpIsSynced()) {
        m += ntpDateTimeStr();
    } else {
        m += "up " + sysFormatDHMS(sysUptimeSec());
    }
    m += "</div>";
    m += "</body></html>";
    return m;
}

//--------------------------------------------------------------
// Постановка события в очередь
//--------------------------------------------------------------
void emailQueueEvent(uint16_t bit, const String &title, const String &body) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    if ((s_cfg.emEvents & bit) == 0) {
        DbgVerb("EM: event 0x%04X disabled in mask", bit);
        return;
    }

    String html = formatHtmlMessage(title, body);
    String item;
    item.reserve(html.length() + title.length() + 16);
    item += title;
    item += "\n";
    item += html;
    queuePush(item);
    DbgInfo("EM: event queued (bit=0x%04X)", bit);
}

//--------------------------------------------------------------
// Автоматическая постановка события
//--------------------------------------------------------------
void emailNotify(uint16_t bit, const String &logLine) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;

    String title;
    switch (bit) {
        case EM_EV_POWER: title = "DC UPS: Питание";      break;
        case EM_EV_BATT:  title = "DC UPS: Батарея";      break;
        case EM_EV_WOL:   title = "DC UPS: WoL";          break;
        case EM_EV_BOOT:  title = "DC UPS: Перезагрузка"; break;
        case EM_EV_WIFI:  title = "DC UPS: Wi-Fi";        break;
        case EM_EV_SNMP:  title = "DC UPS: SNMP";         break;
        case EM_EV_NTP:   title = "DC UPS: NTP";          break;
        case EM_EV_TEMP:  title = "DC UPS: Температура";  break;
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

    emailQueueEvent(bit, title, body);
}

//--------------------------------------------------------------
// Периодический отчёт
//--------------------------------------------------------------
void emailQueueReport(const String &lang) {
    if (!s_ready) return;
    if (!s_cfg.emEnable) return;
    if (s_cfg.emPeriod == 0) return;

    UpsStatus st = upsGetStatus();

    String title = "DC UPS: периодический отчёт";

    String body;
    body.reserve(400);
    body += "Батарея: <b>" + String(st.batteryVolts, 3) + " В</b> ("
            + String(st.batteryPercent) + "%)\n";
    body += "Вход:    <b>" + String(st.inputVolts, 3) + " В</b> ("
            + String(st.inputPresent ? "OK" : "LOST") + ")\n";
    body += "Runtime: " + String((unsigned long)(st.runtimeSec / 60))
            + " мин " + String((unsigned long)(st.runtimeSec % 60)) + " с\n";
    body += "Температура CPU: " + String(g_lastTempC, 1) + " °C\n";
    body += "Состояние: " + String(upsStateName(st.state)) + "\n";
    body += "Uptime:  " + sysUptimeStr();

    String html = formatHtmlMessage(title, body);
    String item;
    item.reserve(html.length() + title.length() + 16);
    item += title;
    item += "\n";
    item += html;
    queuePush(item);
    DbgInfo("EM: periodic report queued");
}

//--------------------------------------------------------------
// Отправка тестового письма
//--------------------------------------------------------------
bool emailSendTest() {
    loggerEvent("EM sendTest: ready=%d enable=%d host='%s' plen=%u to='%s'",
                s_ready ? 1 : 0,
                s_cfg.emEnable ? 1 : 0,
                s_cfg.emSmtpHost.c_str(),
                (unsigned)s_cfg.emPass.length(),
                s_cfg.emTo.c_str());

    if (!s_ready) return false;

    String title = "DC UPS: тест связи";
    String body;
    body.reserve(200);
    body += "Устройство ";
    body += FW_DEVICE_NAME;
    body += " на связи.\n";
    body += "Release: " FW_RELEASE "\n";
    body += "IP: " + wifiGetIP();

    String html = formatHtmlMessage(title, body);
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
                loggerEventNoNotify("Email send FAILED");
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
