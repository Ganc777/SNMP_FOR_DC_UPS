//==============================================================
// SNMP_FOR_DC_UPS
// Release : R07.11
// File    : adc.cpp
//==============================================================
// Реализация модуля АЦП.
// R07.11: фильтр переделан на медиану 15 значений.
//         Раньше: сортировка + отброс по 3 крайних + среднее 9.
//         Сейчас: сортировка + центральный элемент (8-й).
//         Медиана полностью игнорирует единичные выбросы,
//         не требуя параметра ADC_FILTER_DISCARD.
//==============================================================

#include "adc.h"
#include "config.h"
#include "debug.h"

// Внутреннее состояние
static AdcChannel s_batt;
static AdcChannel s_input;

//--------------------------------------------------------------
// Инициализация АЦП
//--------------------------------------------------------------
void adcInit() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_BATTERY, ADC_11db);
    analogSetPinAttenuation(PIN_ADC_INPUT,   ADC_11db);

    s_batt  = {0, 0, 0.0f, 0.0f};
    s_input = {0, 0, 0.0f, 0.0f};

    DbgInfo("ADC initialized: BAT=IO%d INPUT=IO%d, K=%.6f",
            PIN_ADC_BATTERY, PIN_ADC_INPUT, DIVIDER_K);
}

//--------------------------------------------------------------
// Одиночное "сырое" чтение в мВ (без фильтрации)
//--------------------------------------------------------------
uint16_t adcReadRawMv(uint8_t pin) {
    return (uint16_t)analogReadMilliVolts(pin);
}

//--------------------------------------------------------------
// R07.11: медиана 15 значений.
// Собираем 15 выборок, сортируем, берём центральный (8-й).
// Полностью игнорирует единичные выбросы.
//--------------------------------------------------------------
static uint16_t filterPin(uint8_t pin) {
    uint16_t buf[ADC_FILTER_SAMPLES];   // 15

    // 1) Собрать выборки
    for (uint8_t i = 0; i < ADC_FILTER_SAMPLES; i++) {
        buf[i] = adcReadRawMv(pin);
        delayMicroseconds(200);
    }

    // 2) Сортировка (простая вставками — размер мал)
    for (uint8_t i = 1; i < ADC_FILTER_SAMPLES; i++) {
        uint16_t key = buf[i];
        int8_t j = i - 1;
        while (j >= 0 && buf[j] > key) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }

    // 3) Медиана = центральный элемент
    return buf[ADC_FILTER_SAMPLES / 2];
}

//--------------------------------------------------------------
// Пересчёт сырого мВ -> реальное напряжение
//--------------------------------------------------------------
float adcConvertToReal(uint16_t rawMv, float offsetMv, float gain) {
    float correctedMv = (rawMv + offsetMv) * gain;
    float realV = (correctedMv / 1000.0f) / DIVIDER_K;
    return realV;
}

//--------------------------------------------------------------
// Обновление обоих каналов
//--------------------------------------------------------------
void adcUpdate(const Config &cfg) {
    // --- BATTERY ---
    uint16_t rawBatt = adcReadRawMv(PIN_ADC_BATTERY);
    uint16_t fltBatt = filterPin(PIN_ADC_BATTERY);
    s_batt.rawMv      = rawBatt;
    s_batt.filteredMv = fltBatt;
    s_batt.adcVolts   = fltBatt / 1000.0f;
    s_batt.realVolts  = adcConvertToReal(fltBatt,
                                         cfg.battOffsetMv,
                                         cfg.battGain);

    // --- INPUT ---
    uint16_t rawInput = adcReadRawMv(PIN_ADC_INPUT);
    uint16_t fltInput = filterPin(PIN_ADC_INPUT);
    s_input.rawMv      = rawInput;
    s_input.filteredMv = fltInput;
    s_input.adcVolts   = fltInput / 1000.0f;
    s_input.realVolts  = adcConvertToReal(fltInput,
                                          cfg.inputOffsetMv,
                                          cfg.inputGain);

    DbgVerb("ADC: BATT raw=%u mV flt=%u mV -> %.3f V | INPUT raw=%u mV flt=%u mV -> %.3f V",
            rawBatt, fltBatt, s_batt.realVolts,
            rawInput, fltInput, s_input.realVolts);
}

//--------------------------------------------------------------
// Доступ к результатам
//--------------------------------------------------------------
AdcChannel adcGetBattery() { return s_batt;  }
AdcChannel adcGetInput()   { return s_input; }

//--------------------------------------------------------------
// Одиночные замеры реального напряжения (для калибровки из web)
//--------------------------------------------------------------
float adcReadRealBattery(const Config &cfg) {
    uint16_t flt = filterPin(PIN_ADC_BATTERY);
    return adcConvertToReal(flt, cfg.battOffsetMv, cfg.battGain);
}

float adcReadRealInput(const Config &cfg) {
    uint16_t flt = filterPin(PIN_ADC_INPUT);
    return adcConvertToReal(flt, cfg.inputOffsetMv, cfg.inputGain);
}
