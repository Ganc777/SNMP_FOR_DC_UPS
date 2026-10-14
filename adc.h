//==============================================================
// SNMP_FOR_DC_UPS
// Release : R10.8
// File    : adc.h
//==============================================================
// Модуль чтения АЦП.
//
// История:
//   R01.0: базовое чтение ADC, калибровка.
//   R07.11: фильтр — медиана 15 значений.
//   R10.7: физическая перепайка делителей (1 кОм + 10 кОм).
//   R10.8: добавлено EMA-сглаживание поверх медианы.
//          Параметр ADC_EMA_ALPHA в config.h.
//==============================================================

#ifndef ADC_H
#define ADC_H

#include <Arduino.h>
#include "storage.h"

//--------------------------------------------------------------
// Структура с обработанными значениями одного канала
//--------------------------------------------------------------
struct AdcChannel {
    uint16_t rawMv;        // последнее "сырое" значение ADC в мВ
    uint16_t filteredMv;   // после медианы + EMA
    float    adcVolts;     // напряжение на входе ADC после фильтра, В
    float    realVolts;    // реальное напряжение (после делителя и калибровки), В
};

//--------------------------------------------------------------
// API
//--------------------------------------------------------------
void adcInit();

void adcUpdate(const Config &cfg);

AdcChannel adcGetBattery();
AdcChannel adcGetInput();

uint16_t adcReadRawMv(uint8_t pin);

float adcConvertToReal(uint16_t rawMv, float offsetMv, float gain);

float adcReadRealBattery(const Config &cfg);
float adcReadRealInput(const Config &cfg);

#endif // ADC_H
