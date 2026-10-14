//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.8
// File    : adc.cpp
//==============================================================
// Реализация модуля АЦП.
//
// История:
//   R07.10: фильтр — медиана 11 значений.
//   R07.11: медиана 15 значений.
//   R10.7:  делители 1,0 кОм + 10 кОм (K ≈ 0,0909).
//   R10.8:  EMA-сглаживание поверх медианы.
//           filterPin() применяет:
//             1) сбор 15 выборок,
//             2) медиана (убирает импульсные выбросы),
//             3) EMA (убирает медленный шум).
//           Параметр ADC_EMA_ALPHA — в config.h.
//==============================================================

#include "adc.h"
#include "config.h"
#include "debug.h"

// Внутреннее состояние
static AdcChannel s_batt;
static AdcChannel s_input;

// R10.8: состояние EMA-фильтра (по одному на канал)
// 0.0f — признак «ещё не инициализирован»
static float s_emaBatt  = 0.0f;
static float s_emaInput = 0.0f;

//--------------------------------------------------------------
// Инициализация АЦП
//--------------------------------------------------------------
void adcInit() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_BATTERY, ADC_11db);
    analogSetPinAttenuation(PIN_ADC_INPUT,   ADC_11db);

    s_batt  = {0, 0, 0.0f, 0.0f};
    s_input = {0, 0, 0.0f, 0.0f};

    s_emaBatt  = 0.0f;
    s_emaInput = 0.0f;

    DbgInfo("ADC initialized: BAT=IO%d INPUT=IO%d, K=%.6f, EMA alpha=%.2f",
            PIN_ADC_BATTERY, PIN_ADC_INPUT, DIVIDER_K,
            (float)ADC_EMA_ALPHA);
}

//--------------------------------------------------------------
// Одиночное "сырое" чтение в мВ (без фильтрации)
//--------------------------------------------------------------
uint16_t adcReadRawMv(uint8_t pin) {
    return (uint16_t)analogReadMilliVolts(pin);
}

//--------------------------------------------------------------
// R10.8: фильтр — медиана 15 + EMA.
//
//   ema — ссылка на состояние EMA для этого канала.
//         При первом вызове ema == 0.0f → инициализируется
//         значением медианы.
//--------------------------------------------------------------
static uint16_t filterPin(uint8_t pin, float &ema) {
    uint16_t buf[ADC_FILTER_SAMPLES];   // 15

    // 1) Собрать выборки
    for (uint8_t i = 0; i < ADC_FILTER_SAMPLES; i++) {
        buf[i] = adcReadRawMv(pin);
        delayMicroseconds(200);
    }

    // 2) Сортировка вставками
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
    uint16_t median = buf[ADC_FILTER_SAMPLES / 2];

    // 4) EMA поверх медианы
    if (ema == 0.0f) {
        // Первый замер — инициализация
        ema = (float)median;
    } else {
        ema = ADC_EMA_ALPHA * (float)median
            + (1.0f - ADC_EMA_ALPHA) * ema;
    }

    return (uint16_t)(ema + 0.5f);
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
    uint16_t fltBatt = filterPin(PIN_ADC_BATTERY, s_emaBatt);
    s_batt.rawMv      = rawBatt;
    s_batt.filteredMv = fltBatt;
    s_batt.adcVolts   = fltBatt / 1000.0f;
    s_batt.realVolts  = adcConvertToReal(fltBatt,
                                         cfg.battOffsetMv,
                                         cfg.battGain);

    // --- INPUT ---
    uint16_t rawInput = adcReadRawMv(PIN_ADC_INPUT);
    uint16_t fltInput = filterPin(PIN_ADC_INPUT, s_emaInput);
    s_input.rawMv      = rawInput;
    s_input.filteredMv = fltInput;
    s_input.adcVolts   = fltInput / 1000.0f;
    s_input.realVolts  = adcConvertToReal(fltInput,
                                          cfg.inputOffsetMv,
                                          cfg.inputGain);

    DbgVerb("ADC: BATT raw=%u mV flt=%u mV (ema=%.1f) -> %.3f V | "
            "INPUT raw=%u mV flt=%u mV (ema=%.1f) -> %.3f V",
            rawBatt, fltBatt, s_emaBatt, s_batt.realVolts,
            rawInput, fltInput, s_emaInput, s_input.realVolts);
}

//--------------------------------------------------------------
// Доступ к результатам
//--------------------------------------------------------------
AdcChannel adcGetBattery() { return s_batt;  }
AdcChannel adcGetInput()   { return s_input; }

//--------------------------------------------------------------
// Одиночные замеры реального напряжения (для калибровки)
// Используется только для калибровки — фильтр сбрасывается.
//--------------------------------------------------------------
float adcReadRealBattery(const Config &cfg) {
    float ema = 0.0f;
    uint16_t flt = filterPin(PIN_ADC_BATTERY, ema);
    return adcConvertToReal(flt, cfg.battOffsetMv, cfg.battGain);
}

float adcReadRealInput(const Config &cfg) {
    float ema = 0.0f;
    uint16_t flt = filterPin(PIN_ADC_INPUT, ema);
    return adcConvertToReal(flt, cfg.inputOffsetMv, cfg.inputGain);
}
