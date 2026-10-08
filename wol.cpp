//==============================================================
// SNMP_FOR_DC_UPS
// Release : R07.7
// File    : wol.cpp
//==============================================================
// R03.1: исправлено отображение статуса WoL.
// R06.1: добавлены wolStatusCode() и wolStatusSec().
// R07.7: в wolInit() добавлен s_udp.begin(WOL_PORT).
//==============================================================

#include "wol.h"
#include "config.h"
#include "debug.h"
#include "logger.h"
#include <WiFiUdp.h>

//--------------------------------------------------------------
// Внутреннее состояние
//--------------------------------------------------------------
static bool     s_outageHappened     = false;
static uint32_t s_outageStartedAt    = 0;
static uint32_t s_outageDurationMs   = 0;
static uint32_t s_stableSinceMs      = 0;
static bool     s_inputWasPresent    = true;
static bool     s_wolSentThisCycle   = false;

static WiFiUDP  s_udp;

//--------------------------------------------------------------
// Проверка MAC-адреса
//--------------------------------------------------------------
bool wolMacValid(const String &mac) {
    if (mac.length() != 17) return false;
    for (uint8_t i = 0; i < 17; i++) {
        char c = mac[i];
        if (i % 3 == 2) {
            if (c != ':' && c != '-') return false;
        } else {
            if (!isHexadecimalDigit(c)) return false;
        }
    }
    return true;
}

//--------------------------------------------------------------
// Парсинг MAC в 6 байт
//--------------------------------------------------------------
static bool parseMac(const String &mac, uint8_t out[6]) {
    if (!wolMacValid(mac)) return false;
    for (uint8_t i = 0; i < 6; i++) {
        String byteStr = mac.substring(i * 3, i * 3 + 2);
        out[i] = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
    }
    return true;
}

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void wolInit(const Config &cfg) {
    s_outageHappened     = false;
    s_outageStartedAt    = 0;
    s_outageDurationMs   = 0;
    s_stableSinceMs      = millis();
    s_inputWasPresent    = true;
    s_wolSentThisCycle   = false;

    // R07.7: явно инициализируем UDP-сокет.
    s_udp.begin(WOL_PORT);

    DbgInfo("WoL: enabled=%s, outage_min=%u s, stable=%u s, batt_min=%u%%",
            cfg.wolEnable ? "yes" : "no",
            (unsigned)cfg.wolOutageMinSec,
            (unsigned)cfg.wolStableSec,
            (unsigned)cfg.wolBattMinPct);
    DbgInfo("WoL: QNAP MAC = %s", cfg.qnapMac.c_str());
}

//--------------------------------------------------------------
// Отправка WoL-пакета
//--------------------------------------------------------------
static bool sendWolPacket(const String &mac) {
    uint8_t macBytes[6];
    if (!parseMac(mac, macBytes)) {
        DbgWarn("WoL: invalid MAC '%s'", mac.c_str());
        return false;
    }

    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (uint8_t i = 0; i < 16; i++) {
        memcpy(packet + 6 + i * 6, macBytes, 6);
    }

    IPAddress broadcast(255, 255, 255, 255);

    s_udp.beginPacket(broadcast, WOL_PORT);
    s_udp.write(packet, sizeof(packet));
    bool ok = s_udp.endPacket();

    if (ok) {
        DbgInfo("WoL: magic packet sent to %s", mac.c_str());
        loggerEvent("WoL sent to %s", mac.c_str());
    } else {
        DbgWarn("WoL: failed to send packet");
    }
    return ok;
}

//--------------------------------------------------------------
// Ручная отправка
//--------------------------------------------------------------
bool wolSendManual(const Config &cfg) {
    if (cfg.qnapMac.length() == 0 || cfg.qnapMac == "00:00:00:00:00:00") {
        DbgWarn("WoL: MAC not set");
        return false;
    }
    return sendWolPacket(cfg.qnapMac);
}

//--------------------------------------------------------------
// Основной цикл логики автозапуска
//--------------------------------------------------------------
void wolUpdate(const Config &cfg, const UpsStatus &st) {
    uint32_t now = millis();

    if (s_inputWasPresent && !st.inputPresent) {
        s_outageStartedAt  = now;
        s_wolSentThisCycle = false;
        DbgInfo("WoL: outage started");
    } else if (!s_inputWasPresent && st.inputPresent) {
        s_outageDurationMs = now - s_outageStartedAt;
        uint32_t outageSec = s_outageDurationMs / 1000UL;

        if (outageSec >= cfg.wolOutageMinSec) {
            s_outageHappened = true;
            s_stableSinceMs  = now;
            DbgInfo("WoL: outage lasted %lu s (>= %u s) — armed",
                    (unsigned long)outageSec,
                    (unsigned)cfg.wolOutageMinSec);
        } else {
            DbgInfo("WoL: outage too short (%lu s) — ignored",
                    (unsigned long)outageSec);
        }
    }

    if (!st.inputPresent) {
        s_stableSinceMs   = 0;
        s_inputWasPresent = false;
        return;
    }

    s_inputWasPresent = true;

    if (!cfg.wolEnable)                          return;
    if (!s_outageHappened)                       return;
    if (s_wolSentThisCycle)                      return;
    if (s_stableSinceMs == 0)                    return;
    if (st.batteryPercent < cfg.wolBattMinPct)   return;

    uint32_t stableSec = (now - s_stableSinceMs) / 1000UL;
    if (stableSec >= cfg.wolStableSec) {
        if (wolSendManual(cfg)) {
            s_wolSentThisCycle = true;
            s_outageHappened   = false;
            DbgInfo("WoL: server wake-up triggered (batt %u%%)",
                    st.batteryPercent);
        }
    }
}

//--------------------------------------------------------------
// Код статуса для i18n
//--------------------------------------------------------------
String wolStatusCode() {
    if (s_wolSentThisCycle) return "SENT";
    if (!s_inputWasPresent)  return "OUTAGE";
    if (s_outageHappened) {
        if (s_stableSinceMs == 0) return "WAIT";
        return "STABLE";
    }
    return "IDLE";
}

uint32_t wolStatusSec() {
    if (!s_inputWasPresent) {
        return (millis() - s_outageStartedAt) / 1000UL;
    }
    if (s_outageHappened && s_stableSinceMs != 0) {
        return (millis() - s_stableSinceMs) / 1000UL;
    }
    return 0;
}

//--------------------------------------------------------------
// Строка статуса
//--------------------------------------------------------------
String wolStatusText() {
    if (s_wolSentThisCycle)
        return "Пакет отправлен";

    if (!s_inputWasPresent) {
        uint32_t outageSec = (millis() - s_outageStartedAt) / 1000UL;
        return "Пропадание входа: " + String(outageSec) + " с";
    }

    if (s_outageHappened) {
        if (s_stableSinceMs == 0) return "Ожидание стабильного питания";
        uint32_t stableSec = (millis() - s_stableSinceMs) / 1000UL;
        return "Стабильно " + String(stableSec) + " с";
    }

    return "Ожидание (авария не зафиксирована)";
}
