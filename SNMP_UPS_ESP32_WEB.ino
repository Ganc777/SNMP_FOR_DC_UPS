//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.4
// File    : SNMP_UPS_ESP32_WEB.ino
//==============================================================
// R06.0: подключён модуль Telegram, добавлена детекция
//        превышения температуры с гистерезисом.
//        g_lastTempC теперь живёт в system.cpp (extern в system.h).
// R07.0: подключён модуль Email (SMTP), emailInit/emailLoop.
//==============================================================

#include "config.h"
#include "debug.h"
#include "system.h"
#include "storage.h"
#include "adc.h"
#include "ups.h"
#include "logger.h"
#include "wifi.h"
#include "snmp.h"
#include "web.h"
#include "wol.h"
#include "ntp.h"
#include "telegram.h"
#include "email.h"

Config cfg;

uint32_t lastAdcMs       = 0;
uint32_t lastWifiPrintMs = 0;
uint32_t lastTempMs      = 0;

// g_lastTempC — определена в system.cpp, объявлена extern в system.h.

static bool s_tempHighActive = false;

void setup() {
    debugInit(115200);
    DbgInfo("Boot start");

    storageLoad(cfg);
    storageDump(cfg);

    adcInit();
    upsInit(cfg);

    wifiInit(cfg);
    uint32_t t0 = millis();
    while (wifiGetState() != WIFI_ST_CONNECTED && millis() - t0 < 15000) {
        wifiLoop();
        delay(50);
    }

    ntpInit(cfg);

    loggerInit();
    loggerEvent("Device boot, release %s", FW_RELEASE);

    snmpInit(cfg);
    wolInit(cfg);
    webInit(cfg);
    telegramInit(cfg);
    emailInit(cfg);

    g_lastTempC = sysReadTemperature();
    DbgInfo("Ready. OTA page: http://%s/update", wifiGetIP().c_str());
    DbgInfo("CPU temp initial: %.1f C", g_lastTempC);
}

void loop() {
    sysLoop();
    wifiLoop();
    ntpLoop();

    uint32_t now = millis();

    //----------------------------------------------------------
    // Температура платы + детекция перегрева
    //----------------------------------------------------------
    if (now - lastTempMs >= TEMP_POLL_MS) {
        lastTempMs = now;
        sysUpdateTemperature();
        DbgVerb("CPU temp: %.1f C", g_lastTempC);

        float thr = cfg.tempHighC;
        float rel = thr - 5.0f;
        if (rel < 0.0f) rel = 0.0f;

        if (!s_tempHighActive && g_lastTempC > thr) {
            s_tempHighActive = true;
            loggerEvent("TEMP_HIGH: %.1f C (threshold %.1f C)",
                        g_lastTempC, thr);
            DbgWarn("TEMP_HIGH: %.1f C > %.1f C",
                    g_lastTempC, thr);
        } else if (s_tempHighActive && g_lastTempC < rel) {
            s_tempHighActive = false;
            loggerEvent("TEMP_OK: %.1f C (below %.1f C)",
                        g_lastTempC, rel);
            DbgInfo("TEMP_OK: %.1f C < %.1f C",
                    g_lastTempC, rel);
        }
    }

    //----------------------------------------------------------
    // ADC + UPS
    //----------------------------------------------------------
    if (now - lastAdcMs >= cfg.adcPollMs) {
        lastAdcMs = now;

        adcUpdate(cfg);
        AdcChannel b = adcGetBattery();
        AdcChannel i = adcGetInput();

        bool stateChanged = upsUpdate(cfg, b.realVolts, i.realVolts);
        UpsStatus st = upsGetStatus();

        snmpUpdateValues(cfg, st);
        webUpdateStatus(st);
        webUpdateTemp(g_lastTempC);
        wolUpdate(cfg, st);

        if (stateChanged) {
            loggerEvent("STATE -> %s | BATT %.3fV (%u%%) | INPUT %.3fV | runtime %lu s",
                        upsStateName(st.state),
                        st.batteryVolts, st.batteryPercent,
                        st.inputVolts,
                        (unsigned long)st.runtimeSec);
            DbgWarn("STATE -> %s | BATT %.3fV (%u%%) | INPUT %.3fV | runtime %lu s",
                    upsStateName(st.state),
                    st.batteryVolts, st.batteryPercent,
                    st.inputVolts,
                    (unsigned long)st.runtimeSec);
            snmpNotifyStateChange(cfg, st);
        } else {
            DbgInfo("BATT %.3fV %u%% | INPUT %.3fV (%s) | %s | runtime %lu s | WiFi %s",
                    st.batteryVolts, st.batteryPercent,
                    st.inputVolts,
                    st.inputPresent ? "OK" : "LOST",
                    upsStateName(st.state),
                    (unsigned long)st.runtimeSec,
                    wifiStateName(wifiGetState()));
        }

        loggerLoop(cfg,
                   st.batteryVolts,
                   st.inputVolts,
                   st.batteryPercent,
                   st.inputPresent,
                   st.runtimeSec,
                   g_lastTempC);
    }

    //----------------------------------------------------------
    // Периодический вывод Wi-Fi
    //----------------------------------------------------------
    if (now - lastWifiPrintMs >= 10000UL) {
        lastWifiPrintMs = now;
        DbgInfo("WiFi: %s IP=%s RSSI=%d dBm",
                wifiStateName(wifiGetState()),
                wifiGetIP().c_str(),
                (int)wifiGetRSSI());
    }

    snmpLoop();
    webLoop();
    telegramLoop();
    emailLoop();
}
