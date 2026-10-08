//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.1
// File    : ups.cpp
//==============================================================
// Реализация логики ИБП.
//==============================================================

#include "ups.h"
#include "config.h"
#include "debug.h"
#include "system.h"

// Внутреннее состояние
static UpsStatus s_status;
static uint32_t  s_inputLostAtMs       = 0;   // момент пропадания входа
static uint32_t  s_inputRestoredAtMs   = 0;   // момент восстановления
static bool      s_inputWasPresent     = true;

//--------------------------------------------------------------
// Инициализация
//--------------------------------------------------------------
void upsInit(const Config &cfg) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = UPS_STATE_NORMAL;
    s_inputWasPresent = true;
    s_inputRestoredAtMs = millis();

    DbgInfo("UPS initialized: full=%.2fV empty=%.2fV warn=%.2fV hyst=%.2fV",
            cfg.battFullV, cfg.battEmptyV, cfg.battWarnV, cfg.hystV);
}

//--------------------------------------------------------------
// Строковое имя состояния
//--------------------------------------------------------------
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
// Нелинейная кривая разряда Li-ion (2S).
// Таблица: напряжение (В) -> процент заряда (%). Убывающая.
// Значения подобраны по типичной кривой Li-ion с пологим
// плато около 7,2..7,6 В и резким спадом ниже 7,0 В.
//--------------------------------------------------------------
struct LiPoint { float v; uint8_t p; };
static const LiPoint LI_CURVE[] = {
    { 8.40f, 100 },
    { 8.20f,  95 },
    { 8.00f,  88 },
    { 7.80f,  75 },
    { 7.60f,  60 },
    { 7.40f,  45 },
    { 7.20f,  32 },
    { 7.00f,  22 },
    { 6.80f,  14 },
    { 6.60f,   8 },
    { 6.40f,   4 },
    { 6.20f,   2 },
    { 6.00f,   0 },
};
static const uint8_t LI_CURVE_N = sizeof(LI_CURVE) / sizeof(LI_CURVE[0]);

//--------------------------------------------------------------
// Расчёт процента заряда с линейной интерполяцией по таблице
//--------------------------------------------------------------
uint8_t upsCalcBatteryPercent(float volts, const Config &cfg) {
    // Отсечки по конфигу (позволяют менять границы из дашборда)
    float vFull  = cfg.battFullV;
    float vEmpty = cfg.battEmptyV;

    if (volts >= vFull)  return 100;
    if (volts <= vEmpty) return 0;

    // Интерполяция по таблице
    for (uint8_t i = 0; i < LI_CURVE_N - 1; i++) {
        float vHi = LI_CURVE[i].v;
        float vLo = LI_CURVE[i + 1].v;
        if (volts <= vHi && volts >= vLo) {
            uint8_t pHi = LI_CURVE[i].p;
            uint8_t pLo = LI_CURVE[i + 1].p;
            // Линейная интерполяция между (vHi,pHi) и (vLo,pLo)
            float k = (volts - vLo) / (vHi - vLo);
            float p = pLo + k * (float)(pHi - pLo);
            return (uint8_t)sysClamp((int)p, 0, 100);
        }
    }
    return 0;
}

//--------------------------------------------------------------
// Расчёт runtime, сек
//--------------------------------------------------------------
uint32_t upsCalcRuntimeSec(uint8_t percent, const Config &cfg) {
    if (percent == 0) return 0;

    // Остаточная энергия в Вт·ч
    float remainingWh = cfg.battCapacityWh * (percent / 100.0f);

    // Мощность нагрузки: ток × напряжение (12 В)
    float loadW = cfg.loadCurrentA * 12.0f;
    if (loadW < 0.1f) return 0;

    // Время в часах -> секундах
    float hours = remainingWh / loadW;
    float seconds = hours * 3600.0f;
    return (uint32_t)seconds;
}

//--------------------------------------------------------------
// Обновление логики
//--------------------------------------------------------------
bool upsUpdate(const Config &cfg,
               float batteryVolts,
               float inputVolts) {

    UpsState prevState = s_status.state;

    // Сохраняем напряжения
    s_status.batteryVolts = batteryVolts;
    s_status.inputVolts   = inputVolts;

    // --- Определение наличия входа с гистерезисом ---
    // Если вход был — считаем пропавшим ниже (inputLostV - hyst)
    // Если входа не было — считаем восстановившимся выше inputLostV
    if (s_status.inputPresent) {
        if (inputVolts < (cfg.inputLostV - cfg.hystV)) {
            s_status.inputPresent = false;
        }
    } else {
        if (inputVolts > cfg.inputLostV) {
            s_status.inputPresent = true;
        }
    }

    // --- Детект перехода вход/без входа ---
    if (s_inputWasPresent && !s_status.inputPresent) {
        // Вход только что пропал
        s_inputLostAtMs = millis();
        DbgWarn("INPUT LOST (%.2f V)", inputVolts);
    } else if (!s_inputWasPresent && s_status.inputPresent) {
        // Вход восстановился
        s_inputRestoredAtMs = millis();
        DbgInfo("INPUT RESTORED (%.2f V)", inputVolts);
    }
    s_inputWasPresent = s_status.inputPresent;

    // --- Таймеры ---
    if (!s_status.inputPresent) {
        s_status.secondsOnBattery = (millis() - s_inputLostAtMs) / 1000UL;
        s_status.secondsSinceInputRestore = 0;
    } else {
        s_status.secondsOnBattery = 0;
        s_status.secondsSinceInputRestore = (millis() - s_inputRestoredAtMs) / 1000UL;
    }

    // --- Процент заряда ---
    s_status.batteryPercent = upsCalcBatteryPercent(batteryVolts, cfg);

    // --- Флаги низкого/критического заряда с гистерезисом ---
    // LOW: включаем ниже warn, выключаем выше warn+hyst
    if (s_status.batteryLow) {
        if (batteryVolts > (cfg.battWarnV + cfg.hystV)) {
            s_status.batteryLow = false;
        }
    } else {
        if (batteryVolts < cfg.battWarnV) {
            s_status.batteryLow = true;
        }
    }

    // CRITICAL: включаем ниже empty, выключаем выше empty+hyst
    if (s_status.batteryCritical) {
        if (batteryVolts > (cfg.battEmptyV + cfg.hystV)) {
            s_status.batteryCritical = false;
        }
    } else {
        if (batteryVolts < cfg.battEmptyV) {
            s_status.batteryCritical = true;
        }
    }

    // --- Определение состояния ---
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

    // --- Runtime ---
    // Считаем ВСЕГДА — это прогноз автономии при текущем заряде.
    s_status.runtimeSec = upsCalcRuntimeSec(s_status.batteryPercent, cfg);

    // Возвращаем true, если состояние изменилось
    return (prevState != newState);
}

//--------------------------------------------------------------
// Получить текущий статус
//--------------------------------------------------------------
UpsStatus upsGetStatus() {
    return s_status;
}
