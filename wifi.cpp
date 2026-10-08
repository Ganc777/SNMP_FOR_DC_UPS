//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.1
// File    : wifi.cpp
//==============================================================
// Реализация модуля Wi-Fi.
//==============================================================

#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "logger.h"
#include <WiFi.h>

// Внутреннее состояние
static WifiState s_state        = WIFI_ST_DISCONNECTED;
static uint32_t  s_lastTryMs    = 0;
static uint32_t  s_connectStartMs = 0;
static bool      s_wasConnected = false;

// Интервал повторного переподключения
static const uint32_t WIFI_RETRY_MS = 10000UL;   // 10 сек
// Таймаут одной попытки подключения
static const uint32_t WIFI_TIMEOUT_MS = 20000UL; // 20 сек

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void wifiInit(const Config &cfg) {
    DbgInfo("WiFi: init, SSID='%s' mode=%s",
            cfg.wifiSsid.c_str(),
            cfg.useDhcp ? "DHCP" : "STATIC");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg.hostname.c_str());

    // Сохраняем конфиг Wi-Fi без записи во flash на каждом старте
    WiFi.persistent(false);

    // Настраиваем IP-режим
    if (!cfg.useDhcp) {
        IPAddress ip, gw, mask, dns;
        if (ip.fromString(cfg.ip)     &&
            gw.fromString(cfg.gw)     &&
            mask.fromString(cfg.mask) &&
            dns.fromString(cfg.dns)) {
            WiFi.config(ip, gw, mask, dns);
            DbgInfo("WiFi: static IP %s / %s / gw %s",
                    cfg.ip.c_str(), cfg.mask.c_str(), cfg.gw.c_str());
        } else {
            DbgError("WiFi: bad static IP config, falling back to DHCP");
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }
    } else {
        // Явно DHCP
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    // Старт подключения
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    s_state = WIFI_ST_CONNECTING;
    s_connectStartMs = millis();
    s_lastTryMs = millis();
}

//--------------------------------------------------------------
// Строковое имя состояния
//--------------------------------------------------------------
const char* wifiStateName(WifiState st) {
    switch (st) {
        case WIFI_ST_DISCONNECTED: return "DISCONNECTED";
        case WIFI_ST_CONNECTING:   return "CONNECTING";
        case WIFI_ST_CONNECTED:    return "CONNECTED";
        case WIFI_ST_FAILED:       return "FAILED";
        default:                   return "UNKNOWN";
    }
}

//--------------------------------------------------------------
// Основной цикл
//--------------------------------------------------------------
void wifiLoop() {
    uint32_t now = millis();
    wl_status_t st = WiFi.status();

    switch (s_state) {

        case WIFI_ST_CONNECTING: {
            // Успех?
            if (st == WL_CONNECTED) {
                s_state = WIFI_ST_CONNECTED;
                DbgInfo("WiFi: connected, IP=%s RSSI=%d dBm",
                        WiFi.localIP().toString().c_str(),
                        (int)WiFi.RSSI());
                loggerEvent("WiFi connected, IP=%s",
                            WiFi.localIP().toString().c_str());
                s_wasConnected = true;
                return;
            }
            // Таймаут?
            if (now - s_connectStartMs > WIFI_TIMEOUT_MS) {
                DbgWarn("WiFi: connect timeout, retry in %lu s",
                        (unsigned long)(WIFI_RETRY_MS / 1000));
                s_state = WIFI_ST_FAILED;
                s_lastTryMs = now;
            }
            break;
        }

        case WIFI_ST_CONNECTED: {
            if (st != WL_CONNECTED) {
                DbgWarn("WiFi: connection lost");
                loggerEvent("WiFi lost");
                s_state = WIFI_ST_CONNECTING;
                s_connectStartMs = now;
                s_lastTryMs = now;
                WiFi.disconnect();
            }
            break;
        }

        case WIFI_ST_DISCONNECTED:
        case WIFI_ST_FAILED: {
            // Пробуем подключиться снова с интервалом
            if (now - s_lastTryMs >= WIFI_RETRY_MS) {
                s_lastTryMs = now;
                DbgInfo("WiFi: reconnecting...");
                WiFi.disconnect();
                WiFi.begin();
                s_state = WIFI_ST_CONNECTING;
                s_connectStartMs = now;
            }
            break;
        }
    }
}

//--------------------------------------------------------------
// Геттеры
//--------------------------------------------------------------
WifiState wifiGetState() {
    return s_state;
}

String wifiGetIP() {
    if (s_state != WIFI_ST_CONNECTED) return String("0.0.0.0");
    return WiFi.localIP().toString();
}

int32_t wifiGetRSSI() {
    if (s_state != WIFI_ST_CONNECTED) return 0;
    return WiFi.RSSI();
}

String wifiGetSSID() {
    if (s_state != WIFI_ST_CONNECTED) return String("");
    return WiFi.SSID();
}

//--------------------------------------------------------------
// Принудительное переподключение
//--------------------------------------------------------------
void wifiReconnect() {
    DbgInfo("WiFi: manual reconnect requested");
    WiFi.disconnect();
    s_state = WIFI_ST_DISCONNECTED;
    s_lastTryMs = 0;   // попытка немедленно
}
