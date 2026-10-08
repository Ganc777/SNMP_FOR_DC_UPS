//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.5
// File    : config.h
//==============================================================
// Конфигурация устройства. Значения ниже — ДЕФОЛТЫ.
// Они применяются только при ПЕРВОМ запуске, когда NVS пуста.
// После первого сохранения настроек через веб-дашборд реальные
// значения хранятся в NVS и дефолты НЕ перезаписывают их.
//
// ВНИМАНИЕ: перед публикацией репозитория секретные значения
// (пароли, токены) заменены на заглушки. Заполните своими при
// первом запуске через веб-дашборд или перед сборкой локально.
//==============================================================

#ifndef CONFIG_H
#define CONFIG_H

//--------------------------------------------------------------
// 1. ИДЕНТИФИКАЦИЯ УСТРОЙСТВА
//--------------------------------------------------------------
#define FW_NAME              "SNMP_FOR_DC_UPS"
#define FW_RELEASE           "R09.5"
#define FW_DEVICE_NAME       "SNMP_FOR_DC_UPS_ESP32_WEB"
#define FW_MANUFACTURER      "Ganc777"
#define FW_MODEL             "ESP32-ETH01 v1.4 DC UPS Monitor"

//--------------------------------------------------------------
// 2. ПИНЫ АЦП
//--------------------------------------------------------------
#define PIN_ADC_BATTERY      36
#define PIN_ADC_INPUT        39

//--------------------------------------------------------------
// 3. ДЕЛИТЕЛИ
//--------------------------------------------------------------
#define DIVIDER_R_LOW        4600.0f
#define DIVIDER_R_HIGH       47000.0f
#define DIVIDER_K            (DIVIDER_R_LOW / (DIVIDER_R_LOW + DIVIDER_R_HIGH))

//--------------------------------------------------------------
// 4. АККУМУЛЯТОР
//--------------------------------------------------------------
#define BATT_FULL_V          8.4f
#define BATT_EMPTY_V         6.0f
#define BATT_WARN_V          7.0f
#define BATT_BMS_CUTOFF_V    5.0f
#define BATT_CAPACITY_WH     38.48f
#define BATT_LOAD_CURRENT_A  2.0f

//--------------------------------------------------------------
// 5. ПОРОГИ
//--------------------------------------------------------------
#define INPUT_LOST_V         11.0f
#define THRESHOLD_HYST_V     0.2f

//--------------------------------------------------------------
// 6. ПЕРИОДЫ
//--------------------------------------------------------------
#define ADC_POLL_MS          500
#define INFORM_PERIOD_SEC    30
#define ADC_FILTER_SAMPLES   15
#define TEMP_POLL_MS         5000
#define TEMP_HIGH_C          75.0f

//--------------------------------------------------------------
// 7. WI-FI
//--------------------------------------------------------------
// >>> ЗАПОЛНИТЕ СВОИМИ ДАННЫМИ ПРИ ПЕРВОМ ЗАПУСКЕ ЧЕРЕЗ ДАШБОРД
#define WIFI_SSID_DEFAULT    "YOUR_WIFI_SSID"
#define WIFI_PASS_DEFAULT    "YOUR_WIFI_PASSWORD"
#define WIFI_HOSTNAME        "SNMP_FOR_DC_UPS_ESP32_WEB"

//--------------------------------------------------------------
// 8. СЕТЬ
//--------------------------------------------------------------
#define NET_USE_DHCP_DEFAULT false
#define NET_IP_DEFAULT       "192.168.1.150"
#define NET_MASK_DEFAULT     "255.255.255.0"
#define NET_GW_DEFAULT       "192.168.1.1"
#define NET_DNS_DEFAULT      "192.168.1.1"
#define WEB_PORT             80

//--------------------------------------------------------------
// 9. NTP
//--------------------------------------------------------------
#define NTP_USE_DEFAULT      true
#define NTP_SERVER1_DEFAULT  "pool.ntp.org"
#define NTP_SERVER2_DEFAULT  "time.google.com"
#define NTP_TZ_DEFAULT       "MSK-3"

//--------------------------------------------------------------
// 10. mDNS
//--------------------------------------------------------------
#define MDNS_ENABLE          true
#define MDNS_HOSTNAME        "snmp-ups"

//--------------------------------------------------------------
// 11. SNMP
//--------------------------------------------------------------
// >>> IP и MAC QNAP-сервера укажите свои через дашборд
#define SNMP_QNAP_IP_DEFAULT  "192.168.1.100"
#define SNMP_QNAP_MAC_DEFAULT "00:00:00:00:00:00"
#define SNMP_COMMUNITY_DEF    "public"
#define SNMP_PORT_DEFAULT     161
#define SNMP_USE_INFORM_DEF   true
#define SNMP_ENTERPRISE_OID   "1.3.6.1.4.1.99999"
#define SNMP_SYS_CONTACT      "admin@local"
#define SNMP_SYS_LOCATION     "Home"

//--------------------------------------------------------------
// 12. ADC КАЛИБРОВКА
//--------------------------------------------------------------
#define ADC_BATT_OFFSET_MV_DEF   0.0f
#define ADC_BATT_GAIN_DEF        0.9747f
#define ADC_INPUT_OFFSET_MV_DEF  0.0f
#define ADC_INPUT_GAIN_DEF       0.9761f

//--------------------------------------------------------------
// 13. ЛОГГЕР
//--------------------------------------------------------------
#define LOG_FILE                "/log.txt"
#define LOG_FILE_OLD            "/log.old"
#define LOG_MAX_BYTES_MIN       16384UL
#define LOG_MAX_BYTES_MAX       524288UL
#define LOG_RESERVE_BYTES       16384UL
#define LOG_VOLT_PERIOD_SEC     120
#define LOG_ENABLE_DEFAULT      true

//--------------------------------------------------------------
// 14. NVS
//--------------------------------------------------------------
#define NVS_NAMESPACE        "snmpsups"

//--------------------------------------------------------------
// 15. OTA
//--------------------------------------------------------------
#define OTA_USERNAME         "admin"
#define OTA_PASSWORD         "CHANGE_ME"
#define OTA_PATH             "/update"

//--------------------------------------------------------------
// 16. WAKE-ON-LAN
//--------------------------------------------------------------
#define WOL_ENABLE_DEFAULT    true
#define WOL_OUTAGE_MIN_SEC    600
#define WOL_STABLE_SEC        300
#define WOL_BATT_MIN_PCT      70
#define WOL_PORT              9

//--------------------------------------------------------------
// 17. TELEGRAM
//--------------------------------------------------------------
// >>> Токен и Chat ID хранятся в NVS, здесь пусто
#define TG_TOKEN_DEFAULT      ""
#define TG_CHATID_DEFAULT     ""
#define TG_ENABLE_DEFAULT     false
#define TG_PERIOD_DEFAULT     0
#define TG_SEND_TIMEOUT_MS    10000UL
#define TG_EVENTS_DEFAULT     0x00FF

//--------------------------------------------------------------
// 18. EMAIL / SMTP
//--------------------------------------------------------------
// >>> Логин, пароль и адреса хранятся в NVS, здесь пусто
#define EM_HOST_DEFAULT      "smtp.mail.ru"
#define EM_PORT_DEFAULT      465
#define EM_USER_DEFAULT      ""
#define EM_PASS_DEFAULT      ""
#define EM_FROM_DEFAULT      ""
#define EM_TO_DEFAULT        ""
#define EM_ENABLE_DEFAULT    false
#define EM_PERIOD_DEFAULT    0
#define EM_EVENTS_DEFAULT    0x00FF
#define EM_SEND_TIMEOUT_MS   15000UL
#define EM_QUEUE_SIZE        10

#endif // CONFIG_H
