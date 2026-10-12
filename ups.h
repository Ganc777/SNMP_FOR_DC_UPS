//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.6
// File    : ups.h
//==============================================================
// Логика ИБП + блок статуса для уведомлений.
//
// История:
//   R01.0: базовая логика ИБП.
//   R01.1: пороги с гистерезисом.
//   R06.0: флаги batteryLow/batteryCritical с гистерезисом.
//   R07.11: нелинейная кривая Li-ion (2S).
//   R10:   upsFormatStatusBlock().
//   R10.2: upsGetPowerLossCount / upsResetPowerLossCount;
//          локализация RU/EN; маркеры [H]...[/H].
//   R10.6: ЕДИНЫЙ формат уведомлений — 10 строк:
//            1. Батарея
//            2. Вход
//            3. Runtime
//            4. Температура CPU
//            5. Состояние
//            6. NTP
//            7. Пропаданий питания
//            8. Uptime
//            9. IP
//           10. Release
//          upsBuildBody10() — единая функция формирования
//          тела для TG/EM (события, отчёт, тест, boot).
//==============================================================

#ifndef UPS_H
#define UPS_H

#include <Arduino.h>
#include "storage.h"

enum UpsState : uint8_t {
    UPS_STATE_NORMAL     = 0,
    UPS_STATE_INPUT_LOST = 1,
    UPS_STATE_LOW_BATT   = 2,
    UPS_STATE_CRITICAL   = 3
};

struct UpsStatus {
    UpsState state;

    bool  inputPresent;
    bool  batteryLow;
    bool  batteryCritical;

    float batteryVolts;
    float inputVolts;

    uint8_t batteryPercent;

    uint32_t runtimeSec;
    uint32_t secondsOnBattery;
    uint32_t secondsSinceInputRestore;
};

void upsInit(const Config &cfg);

bool upsUpdate(const Config &cfg,
               float batteryVolts,
               float inputVolts);

UpsStatus upsGetStatus();

const char* upsStateName(UpsState st);

uint8_t upsCalcBatteryPercent(float volts, const Config &cfg);
uint32_t upsCalcRuntimeSec(uint8_t percent, const Config &cfg);

//--------------------------------------------------------------
// Счётчик пропаданий питания (в RAM).
//--------------------------------------------------------------
uint32_t upsGetPowerLossCount();
void     upsResetPowerLossCount();

//--------------------------------------------------------------
// R10.6: ЕДИНОЕ тело уведомления — 10 строк.
//
//   lang       — "ru" / "en"
//   withEmoji  — подставлять эмодзи
//   highlightCat — бит TG_EV_* / EM_EV_* (0 = без подсветки).
//                  Строки, относящиеся к событию, обёрнуты
//                  маркерами [H]...[/H]. Вызывающая сторона
//                  заменяет их на <b> или <span style=...>.
//
// Возвращает строку из 10 строк, разделённых \n.
//--------------------------------------------------------------
String upsBuildBody10(const String &lang,
                      bool withEmoji = true,
                      uint16_t highlightCat = 0);

//--------------------------------------------------------------
// R10.2: оставлено для совместимости. Возвращает тот же текст,
// что и upsBuildBody10(), но без строк «Uptime» и «IP».
// Используется в старом формате (не применяется в R10.6).
//--------------------------------------------------------------
String upsFormatStatusBlock(const String &lang,
                            bool withEmoji = true,
                            uint16_t highlightCategory = 0);

#endif // UPS_H
