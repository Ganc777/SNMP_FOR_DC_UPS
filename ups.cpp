//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : ups.cpp
//==============================================================
// Логика ИБП + блок статуса для уведомлений.
//
// История:
//   R01.0: базовая логика ИБП.
//   R01.1: пороги с гистерезисом.
//   R06.0: флаги batteryLow/batteryCritical с гистерезисом.
//   R07.11: нелинейная кривая Li-ion (2S).
//   R10:   upsFormatStatusBlock().
//   R10.2: счётчик пропаданий; локализация; маркеры [H].
//   R10.6: upsBuildBody10() — единое тело из 10 строк:
//          Батарея, Вход, Runtime, Температура, Состояние,
//          NTP, Пропадания, Uptime, IP, Release.
//          upsFormatStatusBlock() оставлена для совместимости
//          (6 строк + NTP, без Uptime/IP/Release).
//==============================================================

#include "ups.h"
#include "config.h"
#include "debug.h"
#include "system.h"
#include "logger.h"
#include "ntp.h"
#include "wifi.h"
#include "telegram.h"

static UpsStatus s_status;
static uint32_t  s_inputLostAtMs       = 0;
static uint32_t  s_inputRestoredAtMs   = 0;
static bool      s_inputWasPresent     = true;

static uint32_t  s_powerLossCount      = 0;

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void upsInit(const Config &cfg) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = UPS_STATE_NORMAL;
    s_inputWasPresent = true;
    s_inputRestoredAtMs = millis();
    s_powerLossCount = 0;

    DbgInfo("UPS initialized: full=%.2fV empty=%.2fV warn=%.2fV hyst=%.2fV",
            cfg.battFullV, cfg.battEmptyV, cfg.battWarnV, cfg.hystV);
}

const char* upsStateName(UpsState st) {
    switch (st) {
        case UPS_STATE_NORMAL:     return "NORMAL";
        case UPS_STATE_INPUT_LOST: return "INPUT_LOST";
        case UPS_STATE_LOW_BATT:   return "LOW_BATT";
        case UPS_STATE_CRITICAL:   return "CRITICAL";
        default:                   return "UNKNOWN";
    }
}

//--------------------------------------------------------------
// Нелинейная кривая разряда Li-ion (2S)
//--------------------------------------------------------------
struct LiPoint { float v; uint8_t p; };
static const LiPoint LI_CURVE[] = {
    { 8.40f, 100 }, { 8.20f,  95 }, { 8.00f,  88 }, { 7.80f,  75 },
    { 7.60f,  60 }, { 7.40f,  45 }, { 7.20f,  32 }, { 7.00f,  22 },
    { 6.80f,  14 }, { 6.60f,   8 }, { 6.40f,   4 }, { 6.20f,   2 },
    { 6.00f,   0 },
};
static const uint8_t LI_CURVE_N = sizeof(LI_CURVE) / sizeof(LI_CURVE[0]);

uint8_t upsCalcBatteryPercent(float volts, const Config &cfg) {
    float vFull  = cfg.battFullV;
    float vEmpty = cfg.battEmptyV;

    if (volts >= vFull)  return 100;
    if (volts <= vEmpty) return 0;

    for (uint8_t i = 0; i < LI_CURVE_N - 1; i++) {
        float vHi = LI_CURVE[i].v;
        float vLo = LI_CURVE[i + 1].v;
        if (volts <= vHi && volts >= vLo) {
            uint8_t pHi = LI_CURVE[i].p;
            uint8_t pLo = LI_CURVE[i + 1].p;
            float k = (volts - vLo) / (vHi - vLo);
            float p = pLo + k * (float)(pHi - pLo);
            return (uint8_t)sysClamp((int)p, 0, 100);
        }
    }
    return 0;
}

uint32_t upsCalcRuntimeSec(uint8_t percent, const Config &cfg) {
    if (percent == 0) return 0;
    float remainingWh = cfg.battCapacityWh * (percent / 100.0f);
    float loadW = cfg.loadCurrentA * 12.0f;
    if (loadW < 0.1f) return 0;
    float hours = remainingWh / loadW;
    return (uint32_t)(hours * 3600.0f);
}

//--------------------------------------------------------------
// Обновление логики
//--------------------------------------------------------------
bool upsUpdate(const Config &cfg,
               float batteryVolts,
               float inputVolts) {

    UpsState prevState = s_status.state;

    s_status.batteryVolts = batteryVolts;
    s_status.inputVolts   = inputVolts;

    if (s_status.inputPresent) {
        if (inputVolts < (cfg.inputLostV - cfg.hystV)) {
            s_status.inputPresent = false;
        }
    } else {
        if (inputVolts > cfg.inputLostV) {
            s_status.inputPresent = true;
        }
    }

    if (s_inputWasPresent && !s_status.inputPresent) {
        s_inputLostAtMs = millis();
        s_powerLossCount++;
        DbgWarn("INPUT LOST (%.2f V)", inputVolts);
    } else if (!s_inputWasPresent && s_status.inputPresent) {
        s_inputRestoredAtMs = millis();
        DbgInfo("INPUT RESTORED (%.2f V)", inputVolts);
    }
    s_inputWasPresent = s_status.inputPresent;

    if (!s_status.inputPresent) {
        s_status.secondsOnBattery = (millis() - s_inputLostAtMs) / 1000UL;
        s_status.secondsSinceInputRestore = 0;
    } else {
        s_status.secondsOnBattery = 0;
        s_status.secondsSinceInputRestore = (millis() - s_inputRestoredAtMs) / 1000UL;
    }

    s_status.batteryPercent = upsCalcBatteryPercent(batteryVolts, cfg);

    if (s_status.batteryLow) {
        if (batteryVolts > (cfg.battWarnV + cfg.hystV)) s_status.batteryLow = false;
    } else {
        if (batteryVolts < cfg.battWarnV) s_status.batteryLow = true;
    }

    if (s_status.batteryCritical) {
        if (batteryVolts > (cfg.battEmptyV + cfg.hystV)) s_status.batteryCritical = false;
    } else {
        if (batteryVolts < cfg.battEmptyV) s_status.batteryCritical = true;
    }

    UpsState newState;
    if (s_status.batteryCritical) {
        newState = UPS_STATE_CRITICAL;
    } else if (s_status.batteryLow) {
        newState = UPS_STATE_LOW_BATT;
    } else if (!s_status.inputPresent) {
        newState = UPS_STATE_INPUT_LOST;
    } else {
        newState = UPS_STATE_NORMAL;
    }
    s_status.state = newState;

    s_status.runtimeSec = upsCalcRuntimeSec(s_status.batteryPercent, cfg);

    return (prevState != newState);
}

UpsStatus upsGetStatus() {
    return s_status;
}

//--------------------------------------------------------------
// Счётчик пропаданий
//--------------------------------------------------------------
uint32_t upsGetPowerLossCount() {
    return s_powerLossCount;
}

void upsResetPowerLossCount() {
    s_powerLossCount = 0;
}

//--------------------------------------------------------------
// R10.6: ЕДИНОЕ тело уведомления — 10 строк.
//--------------------------------------------------------------
String upsBuildBody10(const String &lang,
                      bool withEmoji,
                      uint16_t highlightCategory) {

    UpsStatus st = upsGetStatus();

    bool hiInput   = false;
    bool hiBattery = false;
    bool hiState   = false;
    bool hiNtp     = false;
    bool hiTemp    = false;

    if (highlightCategory & TG_EV_POWER) { hiInput = true; hiState = true; }
    if (highlightCategory & TG_EV_BATT)  { hiBattery = true; hiState = true; }
    if (highlightCategory & TG_EV_NTP)   { hiNtp = true; }
    if (highlightCategory & TG_EV_TEMP)  { hiTemp = true; }

    String batteryLbl = i18n("battery",       lang);
    String inputLbl   = i18n("input",         lang);
    String runtimeLbl = i18n("runtime",       lang);
    String tempLbl    = i18n("cpu_temp",      lang);
    String stateLbl   = i18n("state",         lang);
    String ntpLbl     = i18n("ntp",           lang);
    String lossLbl    = i18n("power_losses",  lang);
    String uptimeLbl  = i18n("uptime",        lang);
    String ipLbl      = i18n("ip",            lang);
    String releaseLbl = i18n("release_label", lang);
    String minLbl     = i18n("unit_min",      lang);
    String secLbl     = i18n("unit_sec",      lang);
    String cLbl       = i18n("unit_c",        lang);
    String vLbl       = i18n("unit_v",        lang);

    String s;
    s.reserve(560);

    // 1. 🔋 Батарея
    if (withEmoji) s += "🔋 ";
    s += batteryLbl + ": ";
    if (hiBattery) s += "[H]";
    s += String(st.batteryVolts, 3) + " " + vLbl
         + " (" + String(st.batteryPercent) + "%)";
    if (hiBattery) s += "[/H]";
    s += "\n";

    // 2. ⚡ Вход
    if (withEmoji) s += "⚡ ";
    s += inputLbl + ": ";
    if (hiInput) s += "[H]";
    s += String(st.inputVolts, 3) + " " + vLbl
         + " (" + String(st.inputPresent ? "OK" : "LOST") + ")";
    if (hiInput) s += "[/H]";
    s += "\n";

    // 3. ⏱ Runtime
    if (withEmoji) s += "⏱ ";
    s += runtimeLbl + ": "
         + String((unsigned long)(st.runtimeSec / 60)) + " " + minLbl + " "
         + String((unsigned long)(st.runtimeSec % 60)) + " " + secLbl + "\n";

    // 4. 🌡️ Температура CPU
    if (withEmoji) s += "🌡️ ";
    s += tempLbl + ": ";
    if (hiTemp) s += "[H]";
    s += String(g_lastTempC, 1) + " " + cLbl;
    if (hiTemp) s += "[/H]";
    s += "\n";

    // 5. ℹ️ Состояние
    if (withEmoji) s += "ℹ️ ";
    s += stateLbl + ": ";
    if (hiState) s += "[H]";
    s += String(upsStateName(st.state));
    if (hiState) s += "[/H]";
    s += "\n";

    // 6. 🕐 NTP
    if (withEmoji) s += "🕐 ";
    s += ntpLbl + ": ";
    if (hiNtp) s += "[H]";
    s += ntpIsSynced() ? i18n("ntp_synced", lang)
                       : i18n("ntp_not_synced", lang);
    if (hiNtp) s += "[/H]";
    s += "\n";

    // 7. ⚡ Пропаданий питания
    if (withEmoji) s += "⚡ ";
    s += lossLbl + ": " + String((unsigned long)s_powerLossCount) + "\n";

    // 8. 🕐 Uptime
    if (withEmoji) s += "🕐 ";
    s += uptimeLbl + ":  " + sysUptimeStr() + "\n";

    // 9. 🌐 IP
    if (withEmoji) s += "🌐 ";
    s += ipLbl + ": " + wifiGetIP() + "\n";

    // 10. 📦 Release
    if (withEmoji) s += "📦 ";
    s += releaseLbl + ": " + String(FW_RELEASE);

    return s;
}

//--------------------------------------------------------------
// R10.2 (оставлено для совместимости): 6 строк + NTP,
// без Uptime, IP, Release.
//--------------------------------------------------------------
String upsFormatStatusBlock(const String &lang,
                            bool withEmoji,
                            uint16_t highlightCategory) {

    UpsStatus st = upsGetStatus();

    bool hiInput   = false;
    bool hiBattery = false;
    bool hiState   = false;
    bool hiNtp     = false;
    bool hiTemp    = false;

    if (highlightCategory & TG_EV_POWER) { hiInput = true; hiState = true; }
    if (highlightCategory & TG_EV_BATT)  { hiBattery = true; hiState = true; }
    if (highlightCategory & TG_EV_NTP)   { hiNtp = true; }
    if (highlightCategory & TG_EV_TEMP)  { hiTemp = true; }

    String batteryLbl = i18n("battery",       lang);
    String inputLbl   = i18n("input",         lang);
    String runtimeLbl = i18n("runtime",       lang);
    String tempLbl    = i18n("cpu_temp",      lang);
    String stateLbl   = i18n("state",         lang);
    String ntpLbl     = i18n("ntp",           lang);
    String minLbl     = i18n("unit_min",      lang);
    String secLbl     = i18n("unit_sec",      lang);
    String cLbl       = i18n("unit_c",        lang);
    String vLbl       = i18n("unit_v",        lang);

    String s;
    s.reserve(420);

    if (withEmoji) s += "🔋 ";
    s += batteryLbl + ": ";
    if (hiBattery) s += "[H]";
    s += String(st.batteryVolts, 3) + " " + vLbl
         + " (" + String(st.batteryPercent) + "%)";
    if (hiBattery) s += "[/H]";
    s += "\n";

    if (withEmoji) s += "⚡ ";
    s += inputLbl + ": ";
    if (hiInput) s += "[H]";
    s += String(st.inputVolts, 3) + " " + vLbl
         + " (" + String(st.inputPresent ? "OK" : "LOST") + ")";
    if (hiInput) s += "[/H]";
    s += "\n";

    if (withEmoji) s += "⏱ ";
    s += runtimeLbl + ": "
         + String((unsigned long)(st.runtimeSec / 60)) + " " + minLbl + " "
         + String((unsigned long)(st.runtimeSec % 60)) + " " + secLbl + "\n";

    if (withEmoji) s += "🌡️ ";
    s += tempLbl + ": ";
    if (hiTemp) s += "[H]";
    s += String(g_lastTempC, 1) + " " + cLbl;
    if (hiTemp) s += "[/H]";
    s += "\n";

    if (withEmoji) s += "ℹ️ ";
    s += stateLbl + ": ";
    if (hiState) s += "[H]";
    s += String(upsStateName(st.state));
    if (hiState) s += "[/H]";
    s += "\n";

    if (withEmoji) s += "🕐 ";
    s += ntpLbl + ": ";
    if (hiNtp) s += "[H]";
    s += ntpIsSynced() ? i18n("ntp_synced", lang)
                       : i18n("ntp_not_synced", lang);
    if (hiNtp) s += "[/H]";

    return s;
}
