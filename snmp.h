//==============================================================
// SNMP_FOR_DC_UPS
// Release : R01.1
// File    : snmp.h
//==============================================================
// SNMP-агент: стандартный UPS MIB (RFC 1628) + приватная ветка.
// Поддержка SNMP v1/v2c, GET/WALK, INFORM на QNAP при событиях.
//==============================================================

#ifndef SNMP_H
#define SNMP_H

#include <Arduino.h>
#include "storage.h"
#include "ups.h"

//--------------------------------------------------------------
// События, по которым отправляем INFORM/TRAP на QNAP.
// Имя типа изменено на UpsSnmpEvent, чтобы не конфликтовать
// со встроенным SnmpEvent из библиотеки SNMP_Agent.
//--------------------------------------------------------------
enum UpsSnmpEvent : uint8_t {
    UPS_SNMP_EVT_NONE            = 0,
    UPS_SNMP_EVT_INPUT_LOST      = 1,
    UPS_SNMP_EVT_INPUT_RESTORED  = 2,
    UPS_SNMP_EVT_BATT_LOW        = 3,
    UPS_SNMP_EVT_BATT_CRITICAL   = 4,
    UPS_SNMP_EVT_BATT_RECOVERED  = 5
};

//--------------------------------------------------------------
// API
//--------------------------------------------------------------
// Инициализация агента (вызывать после wifiInit и storageLoad)
void snmpInit(const Config &cfg);

// Обновить значения OID из текущего статуса ИБП.
// Вызывать при каждом обновлении логики (в loop).
void snmpUpdateValues(const Config &cfg, const UpsStatus &st);

// Основной цикл агента — обрабатывает входящие запросы
// и при необходимости отправляет INFORM.
void snmpLoop();

//--------------------------------------------------------------
// Отправка события на QNAP (INFORM или TRAP — по cfg.useInform)
//--------------------------------------------------------------
void snmpSendEvent(UpsSnmpEvent evt);

// Уведомить модуль о смене состояния ИБП — он сам решит,
// какое событие отправить.
void snmpNotifyStateChange(const Config &cfg, const UpsStatus &st);

#endif // SNMP_H
