//==============================================================
// SNMP_FOR_DC_UPS
// Release : R09.4
// File    : web.cpp
//==============================================================
// R09.0: подсветка событий, цвета OK/FAIL, отчёты в 12:00,
//        без легенды, исправлена иконка батареи, NVS, beforeunload.
// R09.1: разорвана бесконечная рекурсия уведомлений (в logger).
// R09.2: иконка батареи — тоньше тело, длиннее носик;
//        неактивные табы и кнопки Час/Сутки — синяя рамка.
// R09.3: переименованы поля chart._ups*, radius=11.
// R09.4: иконка WoL на canvas — правильный arc (длинная дуга снизу);
//        стек по X-координате внутри iconsPlugin; при равном
//        расстоянии в mousemove берём последнюю (верхнюю).
//
// ВНИМАНИЕ: файл выдан в двух частях. Стык — строка
// "// === R09.4: СЕКЦИЯ 2 ===" — она не повторяется в коде.
//==============================================================

#include "web.h"
#include "config.h"
#include "debug.h"
#include "system.h"
#include "adc.h"
#include "wifi.h"
#include "logger.h"
#include "wol.h"
#include "ntp.h"
#include "telegram.h"
#include "email.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <WiFiClient.h>

static WebServer s_server(WEB_PORT);
static Config    s_cfg;
static UpsStatus s_lastStatus;
static float     s_lastTempC = 0.0f;

static const char DASHBOARD_HTML[] PROGMEM = R"HTMLDOC(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SNMP_FOR_DC_UPS</title>
<link rel="icon" type="image/svg+xml" href="/favicon.ico">
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
  :root{--bg:#111;--fg:#eee;--accent:#4ea1ff;--ok:#3ecb6e;
        --warn:#e8b53c;--err:#e84c3c;--dim:#888;--card:#1b1b1b;
        --bord:#2a2a2a;}
  *{box-sizing:border-box;}
  body{margin:0;font-family:system-ui,Segoe UI,Arial,sans-serif;
       background:var(--bg);color:var(--fg);padding:16px;}
  .topbar{display:flex;align-items:center;justify-content:space-between;
          gap:10px;margin-bottom:6px;}
  h1{font-size:20px;line-height:1.2;margin:0;}
  .langbtn{background:transparent;color:var(--fg);
    border:1px solid var(--bord);border-radius:5px;
    padding:3px 9px;font-size:13px;line-height:1.1;
    font-weight:600;cursor:pointer;font-family:inherit;
    white-space:nowrap;flex:0 0 auto;
    display:inline-flex;align-items:center;gap:6px;
    min-width:70px;min-height:22px;}
  .langbtn:hover{border-color:var(--accent);color:var(--accent);}
  .langbtn svg{display:block;}
  .langbtn #langFlag{display:inline-flex;align-items:center;
                     justify-content:center;width:18px;height:12px;}
  h2{font-size:16px;margin:24px 0 8px 0;color:var(--accent);}
  .sub{color:var(--dim);font-size:13px;margin-bottom:16px;}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));
        gap:12px;margin-bottom:20px;}
  .card{background:var(--card);border:1px solid var(--bord);
        border-radius:10px;padding:14px;}
  .label{color:var(--dim);font-size:12px;margin-bottom:2px;}
  .value{font-size:22px;font-weight:600;}
  .unit{color:var(--dim);font-size:14px;font-weight:400;}
  .state-NORMAL{color:var(--ok);}
  .state-INPUT_LOST{color:var(--warn);}
  .state-LOW_BATT{color:var(--warn);}
  .state-CRITICAL{color:var(--err);}
  .bar{height:10px;background:#2a2a2a;border-radius:5px;overflow:hidden;}
  .bar>div{height:100%;background:var(--accent);transition:width .3s;}

  .statustables{display:grid;grid-template-columns:1fr 1fr;gap:16px;
                margin-top:12px;}
  .statustables table{margin-top:0;}
  @media(max-width:600px){.statustables{grid-template-columns:1fr;}}

  table{width:100%;border-collapse:collapse;margin-top:12px;font-size:14px;}
  td{padding:6px 8px;border-bottom:1px solid var(--bord);vertical-align:middle;}
  td:first-child{color:var(--dim);width:auto;padding-right:24px;
    white-space:nowrap;}
  td:last-child{text-align:left;}
  input,select{background:#0a0a0a;color:var(--fg);border:1px solid var(--bord);
    border-radius:6px;padding:6px 8px;font-size:14px;width:100%;
    font-family:inherit;}
  input:focus,select:focus{outline:none;border-color:var(--accent);}
  button{background:var(--accent);color:#000;border:1px solid var(--accent);
    border-radius:6px;
    padding:8px 16px;font-size:14px;font-weight:600;cursor:pointer;
    font-family:inherit;}
  button:hover{filter:brightness(1.1);}
  button.danger{background:var(--err);color:#fff;border-color:var(--err);}
  button.ghost{background:transparent;color:var(--accent);
    border:1px solid var(--accent);}
  button.ok{background:var(--ok);color:#000;border-color:var(--ok);}
  button:disabled{opacity:0.45;cursor:not-allowed;}
  .actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px;}

  .tabs{display:flex;gap:4px;margin-bottom:12px;flex-wrap:wrap;}
  .tabs button{background:transparent;color:var(--dim);
    border:1px solid var(--accent);font-weight:400;}
  .tabs button:hover{border-color:var(--accent);color:var(--accent);}
  .tabs button.active{background:var(--accent);color:#000;font-weight:600;
    border-color:var(--accent);}

  .tab-pane{display:none;}
  .tab-pane.active{display:block;}
  .pane-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:18px;
    padding-top:16px;border-top:1px solid var(--bord);}
  pre.log{background:#0a0a0a;border:1px solid var(--bord);border-radius:6px;
    padding:12px;max-height:400px;overflow:auto;font-size:12px;
    white-space:pre-wrap;word-break:break-all;color:#ccc;}
  .footer{color:var(--dim);font-size:12px;margin-top:24px;}
  .msg{padding:8px 12px;border-radius:6px;margin:8px 0;display:none;}
  .msg.ok{background:#1e4620;color:#b8f5c0;display:block;}
  .msg.err{background:#4a1e1e;color:#f5b8b8;display:block;}
  .row2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;}
  .row3 .field{margin-bottom:8px;}
  .field{margin-bottom:8px;}
  .field .label{margin-bottom:2px;}
  .ota-link{display:inline-block;background:var(--ok);color:#000;
    padding:10px 18px;border-radius:6px;font-weight:600;
    text-decoration:none;font-size:14px;margin-top:8px;}
  .ota-link:hover{filter:brightness(1.1);}
  .info{background:#0e2a44;border:1px solid #1b4a7a;border-radius:6px;
    padding:10px 14px;color:#cde;font-size:13px;margin:12px 0;}
  .dupe{color:#8cf;font-size:11px;margin-top:2px;}
  .calhead{background:#141414;border:1px solid var(--bord);border-radius:6px;
    padding:8px 12px;font-size:13px;color:#ccc;margin:8px 0 14px 0;}
  .clockbar{background:#0e2a44;border:1px solid #1b4a7a;border-radius:6px;
    padding:10px 14px;font-size:13px;color:#cde;margin-bottom:14px;
    display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px;}
  .clockbar b{color:#fff;}
  .chartbox{background:#1b1b1b;border:1px solid var(--bord);border-radius:10px;
    padding:14px;margin-top:12px;position:relative;}
  .chartbox canvas{max-height:420px;display:block;}

  .loadmask{position:absolute;left:0;top:0;right:0;bottom:0;
    background:rgba(15,15,15,0.75);display:none;align-items:center;
    justify-content:center;color:#fff;font-size:15px;font-weight:600;
    border-radius:10px;z-index:5;}
  .loadmask.show{display:flex;}
  .loadmask .blink{animation:blink 1s infinite;}
  @keyframes blink{0%,50%{opacity:1;}51%,100%{opacity:0.2;}}

  .rangebtns{display:flex;align-items:center;gap:6px;flex-wrap:wrap;
    margin-bottom:4px;}
  .rangebtns button{background:transparent;color:var(--dim);
    border:1px solid var(--accent);font-weight:400;
    padding:8px 14px;font-size:14px;border-radius:6px;cursor:pointer;
    font-family:inherit;}
  .rangebtns button:hover{filter:brightness(1.1);}
  .rangebtns button.active{background:var(--accent);color:#000;font-weight:600;
    border-color:var(--accent);}
  .rangebtns .navbtn{background:transparent;color:var(--accent);
    border:1px solid var(--bord);font-weight:600;
    padding:6px 12px;font-size:15px;border-radius:6px;cursor:pointer;
    font-family:inherit;min-width:38px;}
  .rangebtns .navbtn:hover{border-color:var(--accent);}
  .rangebtns .navbtn:disabled{opacity:0.4;cursor:not-allowed;
    border-color:var(--bord);color:var(--dim);}
  .rangelabel{color:var(--dim);font-size:12px;margin-top:6px;
    font-family:monospace;letter-spacing:0.3px;}

  .legend{display:flex;gap:14px;flex-wrap:wrap;font-size:13px;margin-top:10px;}
  .legend label{display:flex;align-items:center;gap:6px;cursor:pointer;}
  .legend input{width:auto;}
  .legend .swatch{display:inline-block;width:12px;height:12px;border-radius:2px;
    vertical-align:middle;}
  .evtcats{display:flex;gap:14px;flex-wrap:wrap;font-size:13px;margin-top:14px;
    padding:12px;background:#141414;border:1px solid var(--bord);
    border-radius:8px;}
  .evtcats .title{color:var(--dim);margin-right:6px;}
  .evtcats label{display:flex;align-items:center;gap:6px;cursor:pointer;}
  .evtcats input{width:auto;}
  .evtcats svg{display:block;}
  .logcfg{background:#141414;border:1px solid var(--bord);border-radius:8px;
    padding:12px;margin-bottom:14px;}
  .logcfg .info2{color:#8cf;font-size:12px;line-height:1.7;margin-bottom:14px;}
  .logcfg .info2 b{color:#fff;}
  .logcfg .info2 .sep{color:#456;margin:0 8px;}
  .tgcats{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));
    gap:10px;margin-top:10px;}
  .tgcats label{display:flex;align-items:center;gap:8px;cursor:pointer;
    background:#141414;border:1px solid var(--bord);border-radius:6px;
    padding:8px 12px;font-size:14px;}
  .tgcats input{width:auto;}
  @media(max-width:700px){.row3{grid-template-columns:1fr;}}
  @media(max-width:600px){.row2{grid-template-columns:1fr;}}

  .evt-tip{position:fixed;background:rgba(0,0,0,0.92);color:#fff;
    border:1px solid var(--accent);border-radius:6px;padding:6px 10px;
    font-size:12px;line-height:1.3;pointer-events:none;z-index:1000;
    display:none;white-space:nowrap;box-shadow:0 2px 10px rgba(0,0,0,0.6);}
  .evt-tip b{color:var(--accent);}
</style>
</head>
<body>
<div class="topbar">
  <h1>SNMP_FOR_DC_UPS</h1>
  <button class="langbtn" id="langBtn" onclick="toggleLang()"><span id="langFlag"></span><span id="langCode">RU</span></button>
</div>
<div class="sub">DC UPS Monitor for QNAP &middot; <span id="host"></span>
  &middot; <span id="fwver">—</span></div>

<div class="clockbar">
  <span><span data-i18n="curTime">Текущее время</span>: <b id="clk_main">—</b></span>
  <span><span data-i18n="updTime">Обновлено</span>: <b id="clk_upd">—</b></span>
  <span>NTP: <b id="clk_ntp">—</b></span>
</div>

<div class="grid">
  <div class="card"><div class="label" data-i18n="state">Состояние</div>
    <div class="value" id="state">—</div></div>
  <div class="card"><div class="label" data-i18n="battery">Аккумулятор</div>
    <div class="value"><span id="battV">—</span> <span class="unit" data-i18n="voltUnit">В</span></div>
    <div class="bar" style="margin-top:8px"><div id="battBar" style="width:0%"></div></div>
    <div class="sub" style="margin-top:6px"><span id="battPct">—</span> %</div>
  </div>
  <div class="card"><div class="label" data-i18n="input12">Вход 12 В</div>
    <div class="value"><span id="inpV">—</span> <span class="unit" data-i18n="voltUnit">В</span></div>
    <div class="sub" style="margin-top:6px" id="inpSt">—</div>
  </div>
  <div class="card"><div class="label" data-i18n="runtime">Runtime (прогноз)</div>
    <div class="value" id="runtime">—</div></div>
</div>

<div class="statustables">
  <table>
    <tr><td data-i18n="ipWifi">IP / Wi-Fi</td><td id="net">—</td></tr>
    <tr><td>RSSI</td><td id="rssi">—</td></tr>
    <tr><td data-i18n="cpuTemp">Температура CPU</td><td id="cpu_temp">—</td></tr>
    <tr><td data-i18n="lastUpd">Последнее обновление</td><td id="updline">—</td></tr>
  </table>
  <table>
    <tr><td data-i18n="uptime">Uptime</td><td id="uptime">—</td></tr>
    <tr><td data-i18n="heapFree">Heap free</td><td id="heap_free">—</td></tr>
    <tr><td data-i18n="wolStatus">Статус WoL</td><td id="wolst">—</td></tr>
    <tr><td data-i18n="tgStatus">Статус Telegram</td><td id="tgst">—</td></tr>
    <tr><td data-i18n="emStatus">Статус Email</td><td id="emst">—</td></tr>
  </table>
</div>

<div id="msg" class="msg"></div>

<h2 data-i18n="settings">Настройки</h2>
<div class="tabs">
  <button data-tab="net" class="active" data-i18n="tabNet">Системные настройки</button>
  <button data-tab="snmp">SNMP</button>
  <button data-tab="cal" data-i18n="tabCal">Калибровка</button>
  <button data-tab="wol" data-i18n="tabWol">Автозапуск</button>
  <button data-tab="tg" data-i18n="tabTg">Telegram</button>
  <button data-tab="em" data-i18n="tabEm">Email</button>
  <button data-tab="chart" data-i18n="tabChart">Графики</button>
  <button data-tab="log" data-i18n="tabLog">Логи</button>
  <button data-tab="ota" data-i18n="tabOta">Обновление</button>
</div>

<div class="tab-pane active" id="tab-net">
  <div class="row3">
    <div>
      <div class="field"><div class="label" data-i18n="lblSsid">SSID</div>
        <input id="c_wifiSsid"></div>
      <div class="field"><div class="label" data-i18n="lblPassword">Пароль</div>
        <input id="c_wifiPass" type="password"></div>
      <div class="field"><div class="label">Hostname</div>
        <input id="c_hostname"></div>
      <div class="field"><div class="label" data-i18n="lblIpMode">Режим IP</div>
        <select id="c_useDhcp">
          <option value="0" data-i18n="static">Статический</option>
          <option value="1">DHCP</option>
        </select>
      </div>
      <div class="field"><div class="label">IP</div>
        <input id="c_ip"></div>
      <div class="field"><div class="label" data-i18n="lblNetmask">Маска</div>
        <input id="c_mask"></div>
      <div class="field"><div class="label" data-i18n="lblGateway">Шлюз</div>
        <input id="c_gw"></div>
    </div>
    <div>
      <div class="field"><div class="label">DNS</div>
        <input id="c_dns"></div>
      <div class="field"><div class="label" data-i18n="lblNtpSrv1">NTP-сервер 1</div>
        <input id="c_ntpServer1"></div>
      <div class="field"><div class="label" data-i18n="lblNtpSrv2">NTP-сервер 2</div>
        <input id="c_ntpServer2"></div>
      <div class="field"><div class="label" data-i18n="lblTimezone">Часовой пояс</div>
        <select id="c_ntpTz">
          <option value="UTC0" data-tz="tzUTC">UTC (UTC+0)</option>
          <option value="MSK-3" data-tz="tzMSK">Москва (UTC+3)</option>
          <option value="EET-2" data-tz="tzEET">Калининград / Хельсинки (UTC+2)</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4" data-tz="tzKIEV">Киев / Кишинёв (UTC+2/+3, DST)</option>
          <option value="+04-4" data-tz="tzSAMARA">Самара / Баку (UTC+4)</option>
          <option value="YEKT-5" data-tz="tzYEKT">Екатеринбург (UTC+5)</option>
          <option value="OMST-6" data-tz="tzOMSK">Омск (UTC+6)</option>
          <option value="KRAT-7" data-tz="tzKRAS">Красноярск / Новосибирск (UTC+7)</option>
          <option value="IRKT-8" data-tz="tzIRK">Иркутск / Улан-Удэ (UTC+8)</option>
          <option value="YAKT-9" data-tz="tzYAK">Якутск / Чита (UTC+9)</option>
          <option value="VLAT-10" data-tz="tzVLA">Владивосток (UTC+10)</option>
          <option value="MAGT-11" data-tz="tzMAG">Магадан (UTC+11)</option>
          <option value="PETT-12" data-tz="tzPET">Камчатка (UTC+12)</option>
          <option value="+05-5" data-tz="tzTASH">Ташкент / Астана (UTC+5)</option>
          <option value="+03-3" data-tz="tzIST">Стамбул / Багдад (UTC+3)</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3" data-tz="tzCET">Берлин / Париж (UTC+1/+2, DST)</option>
          <option value="GMT0BST,M3.5.0/1,M10.5.0" data-tz="tzGMT">Лондон (UTC+0/+1, DST)</option>
          <option value="EST5EDT,M3.2.0,M11.1.0" data-tz="tzEST">Нью-Йорк (UTC−5/−4, DST)</option>
          <option value="PST8PDT,M3.2.0,M11.1.0" data-tz="tzPST">Лос-Анджелес (UTC−8/−7, DST)</option>
          <option value="JST-9" data-tz="tzTOK">Токио (UTC+9)</option>
          <option value="CST-8" data-tz="tzCHN">Пекин / Шанхай (UTC+8)</option>
          <option value="IST-5:30" data-tz="tzDEL">Дели (UTC+5:30)</option>
          <option value="AEST-10AEDT,M10.1.0,M4.1.0/3" data-tz="tzSYD">Сидней (UTC+10/+11, DST)</option>
        </select>
      </div>
      <div class="field"><div class="label" data-i18n="lblUseNtp">Использовать NTP</div>
        <select id="c_ntpUse">
          <option value="1" data-i18n="yes">Да</option>
          <option value="0" data-i18n="no">Нет</option>
        </select>
      </div>
      <div class="field"><div class="label" data-i18n="lblTempHighC">Порог температуры, °C</div>
        <input id="c_tempHighC" type="number" step="0.1"></div>
      <div class="field"><div class="label" data-i18n="lblBattFullV">Батарея полная, В</div>
        <input id="c_battFullV" type="number" step="0.01"></div>
    </div>
    <div>
      <div class="field"><div class="label" data-i18n="lblBattWarnV">Предупреждение, В</div>
        <input id="c_battWarnV" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblBattEmptyV">Батарея пустая, В</div>
        <input id="c_battEmptyV" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblBattCapacityWh">Ёмкость, Вт·ч</div>
        <input id="c_battCapacityWh" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblInputLostV">Пропадание входа, В</div>
        <input id="c_inputLostV" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblLoadCurrentA">Ток нагрузки, А</div>
        <input id="c_loadCurrentA" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblHystV">Гистерезис, В</div>
        <input id="c_hystV" type="number" step="0.01"></div>
      <div class="field"><div class="label" data-i18n="lblAdcPollMs">Период АЦП, мс</div>
        <input id="c_adcPollMs" type="number"></div>
    </div>
  </div>

  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('net')" data-i18n="resetChanges">Сбросить изменения</button>
    <button class="ok" onclick="ntpSyncNow()" data-i18n="syncTime">Синхронизировать время</button>
  </div>
</div>

<div class="tab-pane" id="tab-snmp">
  <div class="row2">
    <div><div class="label" data-i18n="lblQnapIp">IP QNAP (адрес NAS-сервера)</div>
      <input id="c_qnapIp"></div>
    <div><div class="label" data-i18n="lblQnapMac">MAC-адрес QNAP (для WoL)</div>
      <input id="c_qnapMac" placeholder="AA:BB:CC:DD:EE:FF"></div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblCommunity">Community (Группа доступа)</div>
      <input id="c_snmpCommunity"></div>
    <div><div class="label" data-i18n="lblAgentPort">Порт агента</div>
      <input id="c_snmpPort" type="number"></div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblInformType">Тип уведомлений</div>
      <select id="c_useInform">
        <option value="1">INFORM</option>
        <option value="0">TRAP</option>
      </select>
    </div>
    <div><div class="label" data-i18n="lblEnterpriseOid">OID производителя</div>
      <input id="c_enterpriseOid"></div>
  </div>
  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('snmp')" data-i18n="resetChanges">Сбросить изменения</button>
  </div>
</div>

<div class="tab-pane" id="tab-cal">
  <div class="row2">
    <div>
      <h2 data-i18n="battery">Аккумулятор</h2>
      <div class="info" data-i18n="calHint">Введи реальное напряжение, измеренное мультиметром.
        Коэффициент gain рассчитается автоматически.</div>
      <div class="calhead">
        <span data-i18n="now">Сейчас</span>: off=<span id="cal_batt_off_show">—</span> mV,
        gain=<span id="cal_batt_gain_show">—</span>,
        <span data-i18n="reading">показание</span>=<span id="cal_batt_v">—</span> <span data-i18n="voltUnit">В</span>
      </div>
      <div><div class="label" data-i18n="lblRealVoltage">Реальное напряжение, В</div>
        <input id="cal_batt_real" type="number" step="0.001"></div>
      <div class="actions">
        <button onclick="applyCal('batt')" data-i18n="calcGain">Рассчитать gain</button>
        <button class="ghost" onclick="resetCal('batt')" data-i18n="reset">Сброс</button>
      </div>
    </div>
    <div>
      <h2 data-i18n="input12">Вход 12 В</h2>
      <div class="info" data-i18n="calHint">Введи реальное напряжение, измеренное мультиметром.
        Коэффициент gain рассчитается автоматически.</div>
      <div class="calhead">
        <span data-i18n="now">Сейчас</span>: off=<span id="cal_input_off_show">—</span> mV,
        gain=<span id="cal_input_gain_show">—</span>,
        <span data-i18n="reading">показание</span>=<span id="cal_input_v">—</span> <span data-i18n="voltUnit">В</span>
      </div>
      <div><div class="label" data-i18n="lblRealVoltage">Реальное напряжение, В</div>
        <input id="cal_input_real" type="number" step="0.001"></div>
      <div class="actions">
        <button onclick="applyCal('input')" data-i18n="calcGain">Рассчитать gain</button>
        <button class="ghost" onclick="resetCal('input')" data-i18n="reset">Сброс</button>
      </div>
    </div>
  </div>
  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('cal')" data-i18n="resetChanges">Сбросить изменения</button>
  </div>
</div>

<div class="tab-pane" id="tab-wol">
  <h2 data-i18n="wolTitle">Автозапуск сервера (Wake-on-LAN)</h2>
  <div class="info" data-i18n="wolInfo">
    Пакет WoL отправляется, когда одновременно выполнено:<br>
    1. Было пропадание входного питания не менее заданного времени.<br>
    2. После восстановления питание стабильно удерживается.<br>
    3. Заряд аккумулятора не ниже указанного порога.<br>
    Пакет отправляется один раз за цикл аварии.
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblQnapIp">IP QNAP (адрес NAS-сервера)</div>
      <input id="c2_qnapIp">
      <div class="dupe" data-i18n="syncSnmpHint">Синхронизируется с вкладкой SNMP</div>
    </div>
    <div><div class="label" data-i18n="lblQnapMac">MAC-адрес QNAP (для WoL)</div>
      <input id="c2_qnapMac" placeholder="AA:BB:CC:DD:EE:FF">
      <div class="dupe" data-i18n="syncSnmpHint">Синхронизируется с вкладкой SNMP</div>
    </div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblWolEnabled">Автозапуск включён
        (включить NAS после восстановления питания)</div>
      <select id="c_wolEnable">
        <option value="1" data-i18n="yes">Да</option>
        <option value="0" data-i18n="no">Нет</option>
      </select>
    </div>
    <div><div class="label" data-i18n="lblWolOutage">Пропадание питания более чем, сек</div>
      <input id="c_wolOutageMinSec" type="number"></div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblWolStable">Есть стабильное питание более чем, сек</div>
      <input id="c_wolStableSec" type="number"></div>
    <div><div class="label" data-i18n="lblWolBattPct">Мин. заряд батареи, %</div>
      <input id="c_wolBattMinPct" type="number"></div>
  </div>
  <div class="sub">
    <span data-i18n="curStatus">Текущий статус</span>: <b id="wol_status">—</b>
  </div>
  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('wol')" data-i18n="resetChanges">Сбросить изменения</button>
    <button class="ok" onclick="wolManual()" data-i18n="wolSendBtn">Отправить WoL вручную</button>
  </div>
</div>

<div class="tab-pane" id="tab-tg">
  <h2 data-i18n="tgTitle">Уведомления в Telegram</h2>
  <div class="info" data-i18n="tgInfo">
    Сообщения отправляются через Telegram Bot API по HTTPS.<br>
    Токен и Chat ID хранятся в NVS и не попадают в код прошивки.<br>
    Категории событий фильтруют, какие уведомления отправлять.
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblTgToken">Bot Token</div>
      <input id="c_tgToken" type="password" placeholder="123456789:AA...">
      <div class="dupe" data-i18n="tgTokenHint">Получить у @BotFather</div>
    </div>
    <div><div class="label" data-i18n="lblTgChatId">Chat ID</div>
      <input id="c_tgChatId" placeholder="123456789">
      <div class="dupe" data-i18n="tgChatHint">Узнать у @userinfobot</div>
    </div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblTgEnabled">Отправка включена</div>
      <select id="c_tgEnable">
        <option value="1" data-i18n="yes">Да</option>
        <option value="0" data-i18n="no">Нет</option>
      </select>
    </div>
    <div><div class="label" data-i18n="lblTgPeriod">Периодический отчёт</div>
      <select id="c_tgPeriod">
        <option value="0" data-i18n="tgPeriodNone">Нет</option>
        <option value="1" data-i18n="tgPeriodDay">День</option>
        <option value="2" data-i18n="tgPeriodWeek">Неделя</option>
        <option value="3" data-i18n="tgPeriodMonth">Месяц</option>
      </select>
    </div>
  </div>

  <h2 data-i18n="tgEvents">События для отправки</h2>
  <div class="tgcats">
    <label><input type="checkbox" id="evt_power"> <span data-i18n="evPower">Питание</span></label>
    <label><input type="checkbox" id="evt_batt">  <span data-i18n="evBatt">Батарея</span></label>
    <label><input type="checkbox" id="evt_wol">   <span>WoL</span></label>
    <label><input type="checkbox" id="evt_boot">  <span data-i18n="evBoot">Перезагрузки</span></label>
    <label><input type="checkbox" id="evt_wifi">  <span data-i18n="evWifi">Wi-Fi</span></label>
    <label><input type="checkbox" id="evt_snmp">  <span>SNMP</span></label>
    <label><input type="checkbox" id="evt_ntp">   <span>NTP</span></label>
    <label><input type="checkbox" id="evt_temp">  <span data-i18n="evTemp">Температура</span></label>
  </div>

  <div class="sub" style="margin-top:14px">
    <span data-i18n="curStatus">Текущий статус</span>: <b id="tg_status">—</b>
  </div>

  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('tg')" data-i18n="resetChanges">Сбросить изменения</button>
    <button class="ok" onclick="tgTest()" data-i18n="tgTestBtn">Отправить тестовое сообщение</button>
  </div>
</div>

<div class="tab-pane" id="tab-em">
  <h2 data-i18n="emTitle">Уведомления по Email</h2>
  <div class="info" data-i18n="emInfo">
    Письма отправляются напрямую через SMTP-сервер по SSL (implicit TLS).<br>
    Провайдер по умолчанию — Mail.ru (smtp.mail.ru:465). Для Mail.ru
    нужен «пароль для внешнего приложения», обычный пароль не подойдёт.<br>
    Логин, пароль и адреса хранятся в NVS и не попадают в код прошивки.<br>
    Категории событий фильтруют, какие уведомления отправлять.
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblEmHost">SMTP-сервер</div>
      <input id="c_emSmtpHost" placeholder="smtp.mail.ru"></div>
    <div><div class="label" data-i18n="lblEmPort">Порт (465 — SSL)</div>
      <input id="c_emSmtpPort" type="number" placeholder="465"></div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblEmUser">Логин (email)</div>
      <input id="c_emUser" placeholder="user@mail.ru">
      <div class="dupe" data-i18n="emUserHint">Обычно совпадает с адресом From</div>
    </div>
    <div><div class="label" data-i18n="lblEmPass">Пароль (App Password)</div>
      <input id="c_emPass" type="password" placeholder="******">
      <div class="dupe" data-i18n="emPassHint">Mail.ru: Настройки → Пароли для внешних приложений</div>
    </div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblEmFrom">От кого (From)</div>
      <input id="c_emFrom" placeholder="user@mail.ru"></div>
    <div><div class="label" data-i18n="lblEmTo">Кому (To)</div>
      <input id="c_emTo" placeholder="user@mail.ru"></div>
  </div>
  <div class="row2">
    <div><div class="label" data-i18n="lblEmEnabled">Отправка включена</div>
      <select id="c_emEnable">
        <option value="1" data-i18n="yes">Да</option>
        <option value="0" data-i18n="no">Нет</option>
      </select>
    </div>
    <div><div class="label" data-i18n="lblEmPeriod">Периодический отчёт</div>
      <select id="c_emPeriod">
        <option value="0" data-i18n="tgPeriodNone">Нет</option>
        <option value="1" data-i18n="tgPeriodDay">День</option>
        <option value="2" data-i18n="tgPeriodWeek">Неделя</option>
        <option value="3" data-i18n="tgPeriodMonth">Месяц</option>
      </select>
    </div>
  </div>

  <h2 data-i18n="emEvents">События для отправки</h2>
  <div class="tgcats">
    <label><input type="checkbox" id="emt_power"> <span data-i18n="evPower">Питание</span></label>
    <label><input type="checkbox" id="emt_batt">  <span data-i18n="evBatt">Батарея</span></label>
    <label><input type="checkbox" id="emt_wol">   <span>WoL</span></label>
    <label><input type="checkbox" id="emt_boot">  <span data-i18n="evBoot">Перезагрузки</span></label>
    <label><input type="checkbox" id="emt_wifi">  <span data-i18n="evWifi">Wi-Fi</span></label>
    <label><input type="checkbox" id="emt_snmp">  <span>SNMP</span></label>
    <label><input type="checkbox" id="emt_ntp">   <span>NTP</span></label>
    <label><input type="checkbox" id="emt_temp">  <span data-i18n="evTemp">Температура</span></label>
  </div>

  <div class="sub" style="margin-top:14px">
    <span data-i18n="curStatus">Текущий статус</span>: <b id="em_status">—</b>
  </div>

  <div class="pane-actions">
    <button onclick="saveAllPanes()" data-i18n="saveChanges">Сохранить изменения</button>
    <button class="danger" onclick="resetPane('em')" data-i18n="resetChanges">Сбросить изменения</button>
    <button class="ok" onclick="emTest()" data-i18n="emTestBtn">Отправить тестовое письмо</button>
  </div>
</div>

<div class="tab-pane" id="tab-chart">
  <div class="rangebtns" id="rangeRow">
    <span id="rangeRowInner"></span>
    <div class="rangelabel" id="rangeLabel">—</div>
  </div>
  <div class="sub" data-i18n="chartHint">График строится из лога напряжений.</div>

  <div class="legend">
    <label><input type="checkbox" id="chk_batt" checked>
      <span class="swatch" style="background:#4ea1ff"></span>
      <span data-i18n="legendBatt">Батарея, В</span></label>
    <label><input type="checkbox" id="chk_inp" checked>
      <span class="swatch" style="background:#3ecb6e"></span>
      <span data-i18n="legendInput">Вход 12 В, В</span></label>
    <label><input type="checkbox" id="chk_temp" checked>
      <span class="swatch" style="background:#ff8c42"></span>
      <span data-i18n="legendTemp">Температура, °C</span></label>
  </div>

  <div class="chartbox">
    <canvas id="chartMain"></canvas>
    <div class="loadmask" id="chartMask">
      <div class="blink" data-i18n="chartLoading">Загрузка</div>
    </div>
  </div>

  <div class="evtcats">
    <span class="title" data-i18n="events">События:</span>
    <label><input type="checkbox" id="ev_power" checked>
      <svg width="16" height="16" viewBox="0 0 32 32"><path d="M18 4 L8 18 L14 18 L12 28 L24 12 L17 12 L20 4 Z" fill="#e84c3c"/></svg>
      <span data-i18n="evPower">Питание</span></label>
    <label><input type="checkbox" id="ev_batt" checked>
      <svg width="16" height="16" viewBox="0 0 32 32"><rect x="4" y="10" width="22" height="12" rx="1" fill="none" stroke="#e8b53c" stroke-width="2"/><rect x="26" y="13" width="3" height="6" fill="#e8b53c"/><rect x="7" y="13" width="5" height="6" fill="#e8b53c"/></svg>
      <span data-i18n="evBatt">Батарея</span></label>
    <label><input type="checkbox" id="ev_wifi">
      <svg width="16" height="16" viewBox="0 0 32 32"><path d="M4 14 A14 14 0 0 1 28 14" fill="none" stroke="#3ecb6e" stroke-width="2" stroke-linecap="round"/><path d="M8 18 A10 10 0 0 1 24 18" fill="none" stroke="#3ecb6e" stroke-width="2" stroke-linecap="round"/><path d="M12 22 A6 6 0 0 1 20 22" fill="none" stroke="#3ecb6e" stroke-width="2" stroke-linecap="round"/><circle cx="16" cy="26" r="1.8" fill="#3ecb6e"/></svg>
      <span data-i18n="evWifi">Сеть</span></label>
    <label><input type="checkbox" id="ev_snmp">
      <svg width="16" height="16" viewBox="0 0 32 32"><rect x="4" y="8" width="24" height="16" rx="2" fill="none" stroke="#a35eff" stroke-width="2"/><path d="M4 10 L16 19 L28 10" fill="none" stroke="#a35eff" stroke-width="2" stroke-linejoin="round"/></svg>
      <span>SNMP</span></label>
    <label><input type="checkbox" id="ev_wol" checked>
      <svg width="16" height="16" viewBox="0 0 32 32"><path d="M 8.2 11.5 A 9 9 0 1 0 23.8 11.5" fill="none" stroke="#4ea1ff" stroke-width="2.4" stroke-linecap="round"/><line x1="16" y1="4" x2="16" y2="17" stroke="#4ea1ff" stroke-width="2.4" stroke-linecap="round"/></svg>
      <span>WoL</span></label>
    <label><input type="checkbox" id="ev_ntp">
      <svg width="16" height="16" viewBox="0 0 32 32"><circle cx="16" cy="16" r="11" fill="none" stroke="#00d4ff" stroke-width="2"/><line x1="16" y1="16" x2="16" y2="9" stroke="#00d4ff" stroke-width="2" stroke-linecap="round"/><line x1="16" y1="16" x2="21" y2="19" stroke="#00d4ff" stroke-width="2" stroke-linecap="round"/></svg>
      <span>NTP</span></label>
    <label><input type="checkbox" id="ev_boot">
      <svg width="16" height="16" viewBox="0 0 32 32"><path d="M27 16 A11 11 0 1 1 16 5" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round"/><path d="M16 1 L22 5 L16 9 Z" fill="#ffffff"/></svg>
      <span data-i18n="evBoot">Перезагрузки</span></label>
    <label><input type="checkbox" id="ev_tg_em">
      <svg width="16" height="16" viewBox="0 0 32 32"><rect x="4" y="8" width="24" height="16" rx="2" fill="none" stroke="#3ecb6e" stroke-width="2"/><path d="M4 10 L16 19 L28 10" fill="none" stroke="#3ecb6e" stroke-width="2" stroke-linejoin="round"/></svg>
      <span data-i18n="evNotify">Уведомления</span></label>
  </div>

  <div class="actions" style="margin-top:14px">
    <button onclick="loadChart()" data-i18n="refreshChart">Обновить график</button>
  </div>
</div>

<div class="tab-pane" id="tab-log">
  <div class="logcfg">
    <h2 style="margin-top:0" data-i18n="logTitle">Настройки журнала</h2>

    <div class="info2">
      <span data-i18n="logFresh">Свежий</span>:
      <b><span id="lc_logsize">—</span></b>
      <span class="sep">|</span>
      <span data-i18n="logArchive">Архив</span>:
      <b><span id="lc_logold">—</span></b>
      <span class="sep">|</span>
      <span data-i18n="logTotal">Всего</span>:
      <b><span id="lc_logtotal">—</span></b>
      <span class="sep">|</span>
      <span data-i18n="logLimitPerFile">Лимит на файл</span>:
      <b><span id="lc_maxbytes">—</span></b>
      <br>
      <span data-i18n="freeInLfs">свободно в LittleFS</span>:
      <b><span id="lc_lfsfree">—</span></b>
    </div>

    <div>
      <div class="label">
        <span data-i18n="logPeriod">Период записи</span>
        (<span data-i18n="fillIn">свежий заполнится за</span>
        ~<span id="lc_fill1">—</span>)
      </div>
      <div style="max-width:260px">
        <select id="lc_period" onchange="recalcFill()">
          <option value="0" data-i18n="logOff">Откл. (только события)</option>
          <option value="1" data-i18n="sec1">1 секунда</option>
          <option value="30" data-i18n="sec30">30 секунд</option>
          <option value="60" data-i18n="min1">1 минута</option>
          <option value="120" data-i18n="min2">2 минуты</option>
          <option value="300" data-i18n="min5">5 минут</option>
          <option value="600" data-i18n="min10">10 минут</option>
          <option value="1800" data-i18n="min30">30 минут</option>
          <option value="3600" data-i18n="min60">60 минут</option>
        </select>
      </div>
    </div>

    <div class="actions" style="margin-top:12px">
      <button class="ok" onclick="saveLogCfg()" data-i18n="saveReboot">Сохранить и перезагрузить</button>
    </div>
  </div>

  <div class="actions">
    <button onclick="loadLog()" data-i18n="read">Прочитать</button>
    <button class="ghost" onclick="downloadLog()" data-i18n="download">Скачать</button>
    <button class="danger" onclick="clearLog()" data-i18n="clear">Очистить</button>
  </div>
  <div style="position:relative">
    <pre class="log" id="logbox" data-i18n="logHint">Нажми «Прочитать», чтобы открыть лог.</pre>
    <div class="loadmask" id="logMask">
      <div class="blink" data-i18n="chartLoading">Загрузка</div>
    </div>
  </div>
</div>

<div class="tab-pane" id="tab-ota">
  <h2 data-i18n="otaTitle">Обновление прошивки по воздуху</h2>
  <p class="sub" data-i18n="otaHint">Нажми кнопку ниже, откроется страница загрузки.</p>
  <a class="ota-link" href="/update" target="_blank" data-i18n="otaOpen">Открыть страницу обновления</a>
  <p class="sub" style="margin-top:16px" data-i18n="otaSteps">
    1. В Arduino IDE: Скетч → Экспорт бинарного файла.<br>
    2. Получится файл .ino.bin.<br>
    3. Открой страницу обновления, выбери файл, нажми «Обновить».<br>
    4. Устройство перезагрузится автоматически.
  </p>
</div>

<div class="footer" data-i18n="footer">Страница обновляется раз в 2 секунды.</div>

<div class="evt-tip" id="evtTip"></div>

<script>
//=================================================================
// SVG-ФЛАГИ
//=================================================================
const FLAG_RU = '<svg width="18" height="12" viewBox="0 0 18 12"><rect width="18" height="4" fill="#fff"/><rect y="4" width="18" height="4" fill="#0039a6"/><rect y="8" width="18" height="4" fill="#d52b1e"/></svg>';
const FLAG_GB = '<svg width="18" height="12" viewBox="0 0 60 30"><rect width="60" height="30" fill="#012169"/><path d="M0,0 L60,30 M60,0 L0,30" stroke="#fff" stroke-width="6"/><path d="M0,0 L60,30 M60,0 L0,30" stroke="#C8102E" stroke-width="2"/><path d="M30,0 V30 M0,15 H60" stroke="#fff" stroke-width="10"/><path d="M30,0 V30 M0,15 H60" stroke="#C8102E" stroke-width="6"/></svg>';

//=================================================================
// Глобальное состояние
//=================================================================
let curLang = 'ru';
let isDirty = false;

let lastWolCode = 'IDLE';
let lastWolSec  = 0;
let lastTgCode  = 'NOTSET';
let lastTgSec   = 0;
let lastEmCode  = 'NOTSET';
let lastEmSec   = 0;
let lastStateName = 'NORMAL';

let chartRange   = 'hours';
let chartToMs    = 0;
let chartDataMin = 0;
let chartDataMax = 0;
let chartLoaded  = false;

const HOUR_MS = 3600 * 1000;
const DAY_MS  = 24 * 3600 * 1000;

let uiCfgLoaded = false;

//=================================================================
// i18n
//=================================================================
const i18n = {
  ru: {
    curTime: "Текущее время",
    updTime: "Обновлено",
    state: "Состояние",
    battery: "Аккумулятор",
    input12: "Вход 12 В",
    runtime: "Runtime (прогноз)",
    voltUnit: "В",
    ipWifi: "IP / Wi-Fi",
    cpuTemp: "Температура CPU",
    uptime: "Время работы",
    heapFree: "Свободная память",
    lastUpd: "Последнее обновление",
    wolStatus: "Статус WoL",
    tgStatus: "Статус Telegram",
    emStatus: "Статус Email",
    settings: "Настройки",
    tabNet: "Системные настройки",
    tabCal: "Калибровка",
    tabWol: "Автозапуск",
    tabTg: "Telegram",
    tabEm: "Email",
    tabChart: "Графики",
    tabLog: "Логи",
    tabOta: "Обновление",

    lblSsid: "SSID",
    lblPassword: "Пароль",
    lblIpMode: "Режим IP",
    lblNetmask: "Маска",
    lblGateway: "Шлюз",
    lblNtpSrv1: "NTP-сервер 1",
    lblNtpSrv2: "NTP-сервер 2",
    lblTimezone: "Часовой пояс",
    lblUseNtp: "Использовать NTP",
    lblTempHighC: "Порог температуры, °C",
    lblBattFullV: "Батарея полная, В",
    lblBattWarnV: "Предупреждение, В",
    lblBattEmptyV: "Батарея пустая, В",
    lblBattCapacityWh: "Ёмкость, Вт·ч",
    lblInputLostV: "Пропадание входа, В",
    lblLoadCurrentA: "Ток нагрузки, А",
    lblHystV: "Гистерезис, В",
    lblAdcPollMs: "Период АЦП, мс",
    lblQnapIp: "IP QNAP (адрес NAS-сервера)",
    lblQnapMac: "MAC-адрес QNAP (для WoL)",
    lblCommunity: "Community (Группа доступа)",
    lblAgentPort: "Порт агента",
    lblInformType: "Тип уведомлений",
    lblEnterpriseOid: "OID производителя",
    lblRealVoltage: "Реальное напряжение, В",
    lblWolEnabled: "Автозапуск включён (включить NAS после восстановления питания)",
    lblWolOutage: "Пропадание питания более чем, сек",
    lblWolStable: "Есть стабильное питание более чем, сек",
    lblWolBattPct: "Мин. заряд батареи, %",
    lblTgToken: "Bot Token",
    lblTgChatId: "Chat ID",
    lblTgEnabled: "Отправка включена",
    lblTgPeriod: "Периодический отчёт",
    lblEmHost: "SMTP-сервер",
    lblEmPort: "Порт (465 — SSL)",
    lblEmUser: "Логин (email)",
    lblEmPass: "Пароль (App Password)",
    lblEmFrom: "От кого (From)",
    lblEmTo: "Кому (To)",
    lblEmEnabled: "Отправка включена",
    lblEmPeriod: "Периодический отчёт",

    static: "Статический",
    yes: "Да",
    no: "Нет",
    syncTime: "Синхронизировать время",
    saveChanges: "Сохранить изменения",
    resetChanges: "Сбросить изменения",
    calHint: "Введи реальное напряжение, измеренное мультиметром. Коэффициент gain рассчитается автоматически.",
    now: "Сейчас",
    reading: "показание",
    calcGain: "Рассчитать gain",
    reset: "Сброс",
    wolTitle: "Автозапуск сервера (Wake-on-LAN)",
    wolInfo: "Пакет WoL отправляется, когда одновременно выполнено:<br>1. Было пропадание входного питания не менее заданного времени.<br>2. После восстановления питание стабильно удерживается.<br>3. Заряд аккумулятора не ниже указанного порога.<br>Пакет отправляется один раз за цикл аварии.",
    syncSnmpHint: "Синхронизируется с вкладкой SNMP",
    curStatus: "Текущий статус",
    wolSendBtn: "Отправить WoL вручную",
    tgTitle: "Уведомления в Telegram",
    tgInfo: "Сообщения отправляются через Telegram Bot API по HTTPS.<br>Токен и Chat ID хранятся в NVS и не попадают в код прошивки.<br>Категории событий фильтруют, какие уведомления отправлять.",
    tgTokenHint: "Получить у @BotFather",
    tgChatHint: "Узнать у @userinfobot",
    tgPeriodNone: "Нет",
    tgPeriodDay: "День",
    tgPeriodWeek: "Неделя",
    tgPeriodMonth: "Месяц",
    tgEvents: "События для отправки",
    tgTestBtn: "Отправить тестовое сообщение",
    emTitle: "Уведомления по Email",
    emInfo: "Письма отправляются напрямую через SMTP-сервер по SSL (implicit TLS).<br>Провайдер по умолчанию — Mail.ru (smtp.mail.ru:465). Для Mail.ru нужен «пароль для внешнего приложения», обычный пароль не подойдёт.<br>Логин, пароль и адреса хранятся в NVS и не попадают в код прошивки.<br>Категории событий фильтруют, какие уведомления отправлять.",
    emUserHint: "Обычно совпадает с адресом From",
    emPassHint: "Mail.ru: Настройки → Пароли для внешних приложений",
    emEvents: "События для отправки",
    emTestBtn: "Отправить тестовое письмо",
    rangeHours: "Час",
    rangeDay: "Сутки",
    chartHint: "График строится из лога напряжений.",
    chartLoading: "Загрузка",
    legendBatt: "Батарея, В",
    legendInput: "Вход 12 В, В",
    legendTemp: "Температура, °C",
    events: "События:",
    evPower: "Питание",
    evBatt: "Батарея",
    evWifi: "Сеть",
    evBoot: "Перезагрузки",
    evTemp: "Температура",
    evNotify: "Уведомления",
    refreshChart: "Обновить график",
    logTitle: "Настройки журнала",
    logFresh: "Свежий",
    logArchive: "Архив",
    logTotal: "Всего",
    logLimitPerFile: "Лимит на файл",
    freeInLfs: "свободно в LittleFS",
    logPeriod: "Период записи",
    fillIn: "свежий заполнится за",
    logOff: "Откл. (только события)",
    sec1: "1 секунда",
    sec30: "30 секунд",
    min1: "1 минута",
    min2: "2 минуты",
    min5: "5 минут",
    min10: "10 минут",
    min30: "30 минут",
    min60: "60 минут",
    saveReboot: "Сохранить и перезагрузить",
    read: "Прочитать",
    download: "Скачать",
    clear: "Очистить",
    logHint: "Нажми «Прочитать», чтобы открыть лог.",
    otaTitle: "Обновление прошивки по воздуху",
    otaHint: "Нажми кнопку ниже, откроется страница загрузки.",
    otaOpen: "Открыть страницу обновления",
    otaSteps: "1. В Arduino IDE: Скетч → Экспорт бинарного файла.<br>2. Получится файл .ino.bin.<br>3. Открой страницу обновления, выбери файл, нажми «Обновить».<br>4. Устройство перезагрузится автоматически.",
    footer: "Страница обновляется раз в 2 секунды.",
    powerOk: "Питание есть",
    powerLost: "Питание пропало",
    ntpSynced: "синхронизировано",
    ntpNo: "нет",
    confirmSave: "Сохранить изменения",
    confirmReset: "Сбросить изменения",
    confirmResetQ: "к значениям по умолчанию?",
    confirmResetNote: "Будут сброшены ТОЛЬКО поля этой вкладки.",
    confirmReboot: "Устройство перезагрузится.",
    savedOk: "сохранены. Перезагрузка через 2 секунды…",
    resetOk: "сброшены. Перезагрузка через 2 секунды…",
    saveErr: "Ошибка сохранения: ",
    resetErr: "Ошибка сброса: ",
    confirmNtp: "Синхронизировать время по NTP сейчас?",
    ntpSent: "Запрос NTP-синхронизации отправлен",
    ntpErr: "Ошибка запроса NTP",
    confirmWol: "Отправить Wake-on-LAN на MAC из настроек?",
    wolSent: "WoL-пакет отправлен",
    wolErr: "Ошибка отправки WoL",
    confirmClearLog: "Удалить весь лог (свежий и архив)?",
    logCleared: "Лог очищен",
    confirmSaveLog: "Сохранить настройки журнала и перезагрузить устройство?",
    logSaved: "Настройки сохранены. Устройство перезагружается…",
    logSaveErr: "Ошибка сохранения настроек лога",
    confirmResetCal: "Сбросить калибровку для",
    battShort: "аккумулятора",
    inputShort: "входа 12 В",
    calApplied: "Калибровка применена: gain=",
    calApplied2: ". Нажми «Сохранить изменения».",
    calReset: "Калибровка сброшена. Нажми «Сохранить изменения».",
    calErr: "Ошибка калибровки",
    badVoltage: "Введи корректное напряжение",
    confirmTgTest: "Отправить тестовое сообщение в Telegram?",
    tgTestOk: "Тестовое сообщение отправлено",
    tgTestErr: "Ошибка отправки. Проверь токен, chat_id и подключение.",
    confirmEmTest: "Отправить тестовое письмо?",
    emTestOk: "Тестовое письмо отправлено",
    emTestErr: "Ошибка отправки. Проверь SMTP-настройки и пароль приложения.",
    paneNet: "Системные настройки",
    paneSnmp: "SNMP",
    paneCal: "Калибровка",
    paneWol: "Автозапуск",
    paneTg: "Telegram",
    paneEm: "Email",

    stateNORMAL:     "Норма",
    stateINPUT_LOST: "Питание пропало",
    stateLOW_BATT:   "Низкий заряд",
    stateCRITICAL:   "Критический",

    wolStIdle:   "Ожидание (авария не зафиксирована)",
    wolStSent:   "Пакет отправлен",
    wolStOutage: "Пропадание входа: {s} с",
    wolStWait:   "Ожидание стабильного питания",
    wolStStable: "Стабильно {s} с",

    tgStNotSet: "Не настроен",
    tgStOff:    "Отключён",
    tgStActive: "Активен",
    tgStQueue:  "Активен, очередь: {s}",

    emStNotSet: "Не настроен",
    emStOff:    "Отключён",
    emStActive: "Активен",
    emStQueue:  "Активен, очередь: {s}",

    tzUTC: "UTC (UTC+0)",
    tzMSK: "Москва (UTC+3)",
    tzEET: "Калининград / Хельсинки (UTC+2)",
    tzKIEV: "Киев / Кишинёв (UTC+2/+3, DST)",
    tzSAMARA: "Самара / Баку (UTC+4)",
    tzYEKT: "Екатеринбург (UTC+5)",
    tzOMSK: "Омск (UTC+6)",
    tzKRAS: "Красноярск / Новосибирск (UTC+7)",
    tzIRK: "Иркутск / Улан-Удэ (UTC+8)",
    tzYAK: "Якутск / Чита (UTC+9)",
    tzVLA: "Владивосток (UTC+10)",
    tzMAG: "Магадан (UTC+11)",
    tzPET: "Камчатка (UTC+12)",
    tzTASH: "Ташкент / Астана (UTC+5)",
    tzIST: "Стамбул / Багдад (UTC+3)",
    tzCET: "Берлин / Париж (UTC+1/+2, DST)",
    tzGMT: "Лондон (UTC+0/+1, DST)",
    tzEST: "Нью-Йорк (UTC−5/−4, DST)",
    tzPST: "Лос-Анджелес (UTC−8/−7, DST)",
    tzTOK: "Токио (UTC+9)",
    tzCHN: "Пекин / Шанхай (UTC+8)",
    tzDEL: "Дели (UTC+5:30)",
    tzSYD: "Сидней (UTC+10/+11, DST)",

    tipEvtInputLost:  "Питание пропало",
    tipEvtInputRest:  "Питание восстановлено",
    tipEvtBattLow:    "Низкий заряд",
    tipEvtBattCrit:   "Критический разряд",
    tipEvtSnmp:       "SNMP-уведомление",
    tipEvtWol:        "Wake-on-LAN",
    tipEvtNtpOk:      "NTP: синхронизация OK",
    tipEvtNtpFail:    "NTP: ошибка синхронизации",
    tipEvtBoot:       "Перезагрузка",
    tipEvtWifiOk:     "Wi-Fi подключён",
    tipEvtWifiLost:   "Wi-Fi потерян",
    tipEvtTempHigh:   "Перегрев CPU",
    tipEvtTgOk:       "Telegram: отправлено",
    tipEvtTgFail:     "Telegram: ошибка",
    tipEvtEmOk:       "Email: отправлено",
    tipEvtEmFail:     "Email: ошибка",

    leaveWarn: "На странице есть несохранённые изменения. Покинуть без сохранения?"
  },
  en: {
    curTime: "Current time",
    updTime: "Updated",
    state: "State",
    battery: "Battery",
    input12: "Input 12V",
    runtime: "Runtime (est.)",
    voltUnit: "V",
    ipWifi: "IP / Wi-Fi",
    cpuTemp: "CPU temperature",
    uptime: "Uptime",
    heapFree: "Heap free",
    lastUpd: "Last update",
    wolStatus: "WoL status",
    tgStatus: "Telegram status",
    emStatus: "Email status",
    settings: "Settings",
    tabNet: "System settings",
    tabCal: "Calibration",
    tabWol: "Auto-start",
    tabTg: "Telegram",
    tabEm: "Email",
    tabChart: "Charts",
    tabLog: "Logs",
    tabOta: "Update",

    lblSsid: "SSID",
    lblPassword: "Password",
    lblIpMode: "IP mode",
    lblNetmask: "Netmask",
    lblGateway: "Gateway",
    lblNtpSrv1: "NTP server 1",
    lblNtpSrv2: "NTP server 2",
    lblTimezone: "Timezone",
    lblUseNtp: "Use NTP",
    lblTempHighC: "Temperature threshold, °C",
    lblBattFullV: "Battery full, V",
    lblBattWarnV: "Warning threshold, V",
    lblBattEmptyV: "Battery empty, V",
    lblBattCapacityWh: "Capacity, Wh",
    lblInputLostV: "Input lost, V",
    lblLoadCurrentA: "Load current, A",
    lblHystV: "Hysteresis, V",
    lblAdcPollMs: "ADC period, ms",
    lblQnapIp: "QNAP IP (NAS address)",
    lblQnapMac: "QNAP MAC (for WoL)",
    lblCommunity: "Community (access group)",
    lblAgentPort: "Agent port",
    lblInformType: "Notification type",
    lblEnterpriseOid: "Vendor OID",
    lblRealVoltage: "Real voltage, V",
    lblWolEnabled: "Auto-start enabled (wake NAS after power restore)",
    lblWolOutage: "Power outage more than, s",
    lblWolStable: "Stable power more than, s",
    lblWolBattPct: "Min. battery charge, %",
    lblTgToken: "Bot Token",
    lblTgChatId: "Chat ID",
    lblTgEnabled: "Sending enabled",
    lblTgPeriod: "Periodic report",
    lblEmHost: "SMTP server",
    lblEmPort: "Port (465 — SSL)",
    lblEmUser: "Login (email)",
    lblEmPass: "Password (App Password)",
    lblEmFrom: "From address",
    lblEmTo: "To address",
    lblEmEnabled: "Sending enabled",
    lblEmPeriod: "Periodic report",

    static: "Static",
    yes: "Yes",
    no: "No",
    syncTime: "Sync time",
    saveChanges: "Save changes",
    resetChanges: "Reset changes",
    calHint: "Enter the real voltage measured with a multimeter. The gain coefficient will be calculated automatically.",
    now: "Current",
    reading: "reading",
    calcGain: "Calculate gain",
    reset: "Reset",
    wolTitle: "Server auto-start (Wake-on-LAN)",
    wolInfo: "WoL packet is sent when all of the following is true:<br>1. Input power was lost for at least the specified time.<br>2. After restoration, power is stable for at least the specified time.<br>3. Battery charge is not below the specified threshold.<br>The packet is sent once per outage cycle.",
    syncSnmpHint: "Synchronized with SNMP tab",
    curStatus: "Current status",
    wolSendBtn: "Send WoL manually",
    tgTitle: "Telegram notifications",
    tgInfo: "Messages are sent via Telegram Bot API over HTTPS.<br>Token and Chat ID are stored in NVS, not in firmware code.<br>Event categories filter which notifications are sent.",
    tgTokenHint: "Get it from @BotFather",
    tgChatHint: "Get it from @userinfobot",
    tgPeriodNone: "None",
    tgPeriodDay: "Day",
    tgPeriodWeek: "Week",
    tgPeriodMonth: "Month",
    tgEvents: "Events to send",
    tgTestBtn: "Send test message",
    emTitle: "Email notifications",
    emInfo: "Emails are sent directly to the SMTP server over SSL (implicit TLS).<br>Default provider — Mail.ru (smtp.mail.ru:465). Mail.ru requires an 'external app password', a regular password will not work.<br>Login, password and addresses are stored in NVS, not in firmware code.<br>Event categories filter which notifications are sent.",
    emUserHint: "Usually matches the From address",
    emPassHint: "Mail.ru: Settings → External app passwords",
    emEvents: "Events to send",
    emTestBtn: "Send test email",
    rangeHours: "Hour",
    rangeDay: "Day",
    chartHint: "Chart is built from the voltage log.",
    chartLoading: "Loading",
    legendBatt: "Battery, V",
    legendInput: "Input 12V, V",
    legendTemp: "Temperature, °C",
    events: "Events:",
    evPower: "Power",
    evBatt: "Battery",
    evWifi: "Network",
    evBoot: "Reboots",
    evTemp: "Temperature",
    evNotify: "Notifications",
    refreshChart: "Refresh chart",
    logTitle: "Log settings",
    logFresh: "Fresh",
    logArchive: "Archive",
    logTotal: "Total",
    logLimitPerFile: "Limit per file",
    freeInLfs: "free in LittleFS",
    logPeriod: "Log interval",
    fillIn: "fresh will fill in",
    logOff: "Off (events only)",
    sec1: "1 second",
    sec30: "30 seconds",
    min1: "1 minute",
    min2: "2 minutes",
    min5: "5 minutes",
    min10: "10 minutes",
    min30: "30 minutes",
    min60: "60 minutes",
    saveReboot: "Save and reboot",
    read: "Read",
    download: "Download",
    clear: "Clear",
    logHint: "Press «Read» to open the log.",
    otaTitle: "Over-the-air firmware update",
    otaHint: "Press the button below to open the upload page.",
    otaOpen: "Open update page",
    otaSteps: "1. In Arduino IDE: Sketch → Export compiled binary.<br>2. You get a .ino.bin file.<br>3. Open the update page, select the file, press «Update».<br>4. The device will reboot automatically.",
    footer: "Page refreshes every 2 seconds.",
    powerOk: "Power OK",
    powerLost: "Power lost",
    ntpSynced: "synced",
    ntpNo: "no",
    confirmSave: "Save changes",
    confirmReset: "Reset changes",
    confirmResetQ: "to default values?",
    confirmResetNote: "Only fields of this tab will be reset.",
    confirmReboot: "The device will reboot.",
    savedOk: "saved. Reboot in 2 seconds…",
    resetOk: "reset. Reboot in 2 seconds…",
    saveErr: "Save error: ",
    resetErr: "Reset error: ",
    confirmNtp: "Sync time via NTP now?",
    ntpSent: "NTP sync request sent",
    ntpErr: "NTP request error",
    confirmWol: "Send Wake-on-LAN to the configured MAC?",
    wolSent: "WoL packet sent",
    wolErr: "WoL send error",
    confirmClearLog: "Delete the entire log (fresh and archive)?",
    logCleared: "Log cleared",
    confirmSaveLog: "Save log settings and reboot the device?",
    logSaved: "Settings saved. Device is rebooting…",
    logSaveErr: "Log settings save error",
    confirmResetCal: "Reset calibration for",
    battShort: "battery",
    inputShort: "input 12V",
    calApplied: "Calibration applied: gain=",
    calApplied2: ". Press «Save changes».",
    calReset: "Calibration reset. Press «Save changes».",
    calErr: "Calibration error",
    badVoltage: "Enter a valid voltage",
    confirmTgTest: "Send a test message to Telegram?",
    tgTestOk: "Test message sent",
    tgTestErr: "Send failed. Check token, chat_id and connection.",
    confirmEmTest: "Send a test email?",
    emTestOk: "Test email sent",
    emTestErr: "Send failed. Check SMTP settings and app password.",
    paneNet: "System settings",
    paneSnmp: "SNMP",
    paneCal: "Calibration",
    paneWol: "Auto-start",
    paneTg: "Telegram",
    paneEm: "Email",

    stateNORMAL:     "Normal",
    stateINPUT_LOST: "Input lost",
    stateLOW_BATT:   "Low battery",
    stateCRITICAL:   "Critical",

    wolStIdle:   "Idle (no outage recorded)",
    wolStSent:   "Packet sent",
    wolStOutage: "Input outage: {s} s",
    wolStWait:   "Waiting for stable power",
    wolStStable: "Stable for {s} s",

    tgStNotSet: "Not configured",
    tgStOff:    "Disabled",
    tgStActive: "Active",
    tgStQueue:  "Active, queue: {s}",

    emStNotSet: "Not configured",
    emStOff:    "Disabled",
    emStActive: "Active",
    emStQueue:  "Active, queue: {s}",

    tzUTC: "UTC (UTC+0)",
    tzMSK: "Moscow (UTC+3)",
    tzEET: "Kaliningrad / Helsinki (UTC+2)",
    tzKIEV: "Kiev / Chisinau (UTC+2/+3, DST)",
    tzSAMARA: "Samara / Baku (UTC+4)",
    tzYEKT: "Yekaterinburg (UTC+5)",
    tzOMSK: "Omsk (UTC+6)",
    tzKRAS: "Krasnoyarsk / Novosibirsk (UTC+7)",
    tzIRK: "Irkutsk / Ulan-Ude (UTC+8)",
    tzYAK: "Yakutsk / Chita (UTC+9)",
    tzVLA: "Vladivostok (UTC+10)",
    tzMAG: "Magadan (UTC+11)",
    tzPET: "Kamchatka (UTC+12)",
    tzTASH: "Tashkent / Astana (UTC+5)",
    tzIST: "Istanbul / Baghdad (UTC+3)",
    tzCET: "Berlin / Paris (UTC+1/+2, DST)",
    tzGMT: "London (UTC+0/+1, DST)",
    tzEST: "New York (UTC−5/−4, DST)",
    tzPST: "Los Angeles (UTC−8/−7, DST)",
    tzTOK: "Tokyo (UTC+9)",
    tzCHN: "Beijing / Shanghai (UTC+8)",
    tzDEL: "Delhi (UTC+5:30)",
    tzSYD: "Sydney (UTC+10/+11, DST)",

    tipEvtInputLost:  "Input lost",
    tipEvtInputRest:  "Input restored",
    tipEvtBattLow:    "Low battery",
    tipEvtBattCrit:   "Critical battery",
    tipEvtSnmp:       "SNMP notification",
    tipEvtWol:        "Wake-on-LAN",
    tipEvtNtpOk:      "NTP: sync OK",
    tipEvtNtpFail:    "NTP: sync FAILED",
    tipEvtBoot:       "Reboot",
    tipEvtWifiOk:     "Wi-Fi connected",
    tipEvtWifiLost:   "Wi-Fi lost",
    tipEvtTempHigh:   "CPU overheat",
    tipEvtTgOk:       "Telegram: sent",
    tipEvtTgFail:     "Telegram: FAILED",
    tipEvtEmOk:       "Email: sent",
    tipEvtEmFail:     "Email: FAILED",

    leaveWarn: "There are unsaved changes on this page. Leave without saving?"
  }
};

function tr(key) {
  return (i18n[curLang] && i18n[curLang][key]) || key;
}

function stateLabel(name) {
  return tr('state' + name);
}

// === R09.4: СЕКЦИЯ 2 ===
//=================================================================
// ФУНКЦИИ ОТОБРАЖЕНИЯ СТАТУСОВ
//=================================================================
function updateWolStatus() {
  let s = '';
  switch (lastWolCode) {
    case 'SENT':   s = tr('wolStSent'); break;
    case 'OUTAGE': s = tr('wolStOutage').replace('{s}', lastWolSec); break;
    case 'WAIT':   s = tr('wolStWait'); break;
    case 'STABLE': s = tr('wolStStable').replace('{s}', lastWolSec); break;
    default:       s = tr('wolStIdle'); break;
  }
  const a = document.getElementById('wolst');
  const b = document.getElementById('wol_status');
  if (a) a.textContent = s;
  if (b) b.textContent = s;
}

function updateTgStatus() {
  let s = '';
  switch (lastTgCode) {
    case 'NOTSET': s = tr('tgStNotSet'); break;
    case 'OFF':    s = tr('tgStOff'); break;
    case 'QUEUE':  s = tr('tgStQueue').replace('{s}', lastTgSec); break;
    default:       s = tr('tgStActive'); break;
  }
  const a = document.getElementById('tgst');
  const b = document.getElementById('tg_status');
  if (a) a.textContent = s;
  if (b) b.textContent = s;
}

function updateEmStatus() {
  let s = '';
  switch (lastEmCode) {
    case 'NOTSET': s = tr('emStNotSet'); break;
    case 'OFF':    s = tr('emStOff'); break;
    case 'QUEUE':  s = tr('emStQueue').replace('{s}', lastEmSec); break;
    default:       s = tr('emStActive'); break;
  }
  const a = document.getElementById('emst');
  const b = document.getElementById('em_status');
  if (a) a.textContent = s;
  if (b) b.textContent = s;
}

//=================================================================
// Применение языка + перевод часовых поясов
//=================================================================
function applyLang(lang) {
  curLang = lang;
  document.documentElement.lang = lang;

  document.querySelectorAll('[data-i18n]').forEach(el => {
    const key = el.getAttribute('data-i18n');
    const val = tr(key);
    if (el.classList.contains('label') && val.indexOf(':') < 0) {
      el.textContent = val + ':';
    } else if (val.indexOf('<') >= 0) {
      el.innerHTML = val;
    } else {
      el.textContent = val;
    }
  });

  document.querySelectorAll('#c_ntpTz option[data-tz]').forEach(opt => {
    const tzKey = opt.getAttribute('data-tz');
    const txt = tr(tzKey);
    if (txt && txt !== tzKey) opt.textContent = txt;
  });

  const flagEl = document.getElementById('langFlag');
  const codeEl = document.getElementById('langCode');
  if (flagEl) flagEl.innerHTML = (lang === 'ru' ? FLAG_RU : FLAG_GB);
  if (codeEl) codeEl.textContent = (lang === 'ru' ? 'RU' : 'EN');

  if (document.getElementById('clk_ntp')) {
    const isSynced = document.getElementById('clk_ntp').dataset.synced === '1';
    document.getElementById('clk_ntp').textContent =
      isSynced ? tr('ntpSynced') : tr('ntpNo');
  }

  if (document.getElementById('inpSt')) {
    const isOk = document.getElementById('inpSt').dataset.ok === '1';
    document.getElementById('inpSt').textContent =
      isOk ? tr('powerOk') : tr('powerLost');
  }

  const stEl = document.getElementById('state');
  if (stEl) stEl.textContent = stateLabel(lastStateName);

  updateWolStatus();
  updateTgStatus();
  updateEmStatus();
  renderRangeRow();
}

function toggleLang() {
  const newLang = (curLang === 'ru' ? 'en' : 'ru');
  applyLang(newLang);
  saveUiLang(newLang);
}

async function saveUiLang(lang) {
  try {
    await fetch('/api/ui_config', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({uiLang: lang})
    });
  } catch(e) { /* silent */ }
}

//=================================================================
// НАВИГАЦИЯ ПО ГРАФИКУ
//=================================================================
function rangeMs() {
  return chartRange === 'hours' ? HOUR_MS : DAY_MS;
}

function ceilToHour(ms) {
  const h = 3600 * 1000;
  return Math.ceil(ms / h) * h;
}

function initChartWindow() {
  chartToMs = ceilToHour(Date.now());
}

function fmtDT(ms) {
  const d = new Date(ms);
  const dd = pad(d.getDate()) + '.' + pad(d.getMonth() + 1);
  const hh = pad(d.getHours()) + ':' + pad(d.getMinutes());
  return dd + ' ' + hh;
}

function formatRangeLabel() {
  const to   = chartToMs || ceilToHour(Date.now());
  const from = to - rangeMs();
  return fmtDT(from) + ' – ' + fmtDT(to);
}

function renderRangeRow() {
  const inner = document.getElementById('rangeRowInner');
  const label = document.getElementById('rangeLabel');
  if (!inner) return;

  const isHour = (chartRange === 'hours');
  const canBack = chartLoaded && chartDataMin > 0 &&
                  (chartToMs - rangeMs()) > chartDataMin;
  const canFwd  = chartLoaded && chartToMs < ceilToHour(Date.now());

  const btnHour = '<button class="' + (isHour ? 'active' : '') +
                  '" onclick="setRange(\'hours\')">' + tr('rangeHours') + '</button>';
  const btnDay  = '<button class="' + (!isHour ? 'active' : '') +
                  '" onclick="setRange(\'day\')">' + tr('rangeDay') + '</button>';

  const navBack = '<button class="navbtn" ' +
                  (canBack ? '' : 'disabled ') +
                  'onclick="shiftChart(-1)" title="Назад">&#9664;</button>';
  const navFwd  = '<button class="navbtn" ' +
                  (canFwd ? '' : 'disabled ') +
                  'onclick="shiftChart(+1)" title="Вперёд">&#9654;</button>';

  let html;
  if (isHour) {
    html = navBack + ' ' + btnHour + ' ' + navFwd + '   ' + btnDay;
  } else {
    html = btnHour + '   ' + navBack + ' ' + btnDay + ' ' + navFwd;
  }
  inner.innerHTML = html;

  if (label) label.textContent = formatRangeLabel();
}

function setRange(r) {
  if (r === chartRange) return;
  chartRange = r;
  renderRangeRow();
  rebuildChart();
}

function shiftChart(dir) {
  if (!chartLoaded) return;
  const step = rangeMs();
  const maxTo = ceilToHour(Date.now());

  let newTo = chartToMs + dir * step;
  if (newTo > maxTo) newTo = maxTo;

  const minTo = chartDataMin > 0
    ? ceilToHour(chartDataMin) + step
    : 0;
  if (newTo < minTo) newTo = minTo;

  if (newTo === chartToMs) return;
  chartToMs = newTo;
  renderRangeRow();
  rebuildChart();
}

function showChartMask(show) {
  const m = document.getElementById('chartMask');
  if (!m) return;
  if (show) m.classList.add('show');
  else      m.classList.remove('show');
}

//=================================================================
// ИКОНКИ СОБЫТИЙ
//=================================================================
function drawLightning(ctx, x, y, s, color, crossed) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(x + s*0.1, y - s*0.5);
  ctx.lineTo(x - s*0.2, y + s*0.1);
  ctx.lineTo(x + s*0.05, y + s*0.1);
  ctx.lineTo(x - s*0.1, y + s*0.5);
  ctx.lineTo(x + s*0.25, y - s*0.1);
  ctx.lineTo(x - s*0.05, y - s*0.1);
  ctx.closePath();
  ctx.stroke();
  if (crossed) {
    ctx.beginPath();
    ctx.moveTo(x - s*0.6, y - s*0.6);
    ctx.lineTo(x + s*0.6, y + s*0.6);
    ctx.stroke();
  }
  ctx.restore();
}

function drawBattery(ctx, x, y, s, color, level) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 1.6;
  ctx.lineJoin = 'round';

  const w = s * 0.82;
  const h = s * 0.42;
  const left = x - w / 2;
  const top  = y - h / 2;

  ctx.strokeRect(left, top, w, h);

  const capW = w * 0.14;
  const capH = h * 0.42;
  ctx.fillRect(left + w, y - capH / 2, capW, capH);

  let fillW = 0;
  if (level === 1) fillW = (w - 4) * 0.35;
  else if (level === 2) fillW = (w - 4) * 0.85;
  if (fillW > 0) {
    ctx.fillRect(left + 2, top + 2, fillW, h - 4);
  }
  ctx.restore();
}

function drawWifi(ctx, x, y, s, color, crossed) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';
  for (let i = 1; i <= 3; i++) {
    ctx.beginPath();
    ctx.arc(x, y + s*0.4, s*0.2*i, Math.PI * 1.25, Math.PI * 1.75);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.arc(x, y + s*0.35, s*0.08, 0, 2*Math.PI);
  ctx.fill();
  if (crossed) {
    ctx.beginPath();
    ctx.moveTo(x - s*0.6, y - s*0.6);
    ctx.lineTo(x + s*0.6, y + s*0.6);
    ctx.stroke();
  }
  ctx.restore();
}

// R09.4: правильный arc — идём ПРОТИВ часовой от -145° до -35°.
// С anticlockwise=true Canvas идёт «длинным» путём через низ,
// что даёт разрыв сверху между -145° и -35° (110°).
function drawPowerSymbol(ctx, x, y, s, color) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';

  const r = s * 0.42;

  const startA = -Math.PI * 145 / 180;   // -145° — левая часть разрыва
  const endA   = -Math.PI * 35 / 180;    // -35°  — правая часть разрыва

  ctx.beginPath();
  ctx.arc(x, y, r, startA, endA, true);  // anticlockwise = true
  ctx.stroke();

  // Вертикальная черта в разрыв
  ctx.beginPath();
  ctx.moveTo(x, y - r * 1.05);
  ctx.lineTo(x, y + r * 0.15);
  ctx.stroke();
  ctx.restore();
}

function drawEnvelope(ctx, x, y, s, color) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  const w = s*0.9, h = s*0.6;
  ctx.strokeRect(x - w/2, y - h/2, w, h);
  ctx.beginPath();
  ctx.moveTo(x - w/2, y - h/2);
  ctx.lineTo(x, y + h*0.1);
  ctx.lineTo(x + w/2, y - h/2);
  ctx.stroke();
  ctx.restore();
}

function drawClock(ctx, x, y, s, color) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc(x, y, s*0.45, 0, 2*Math.PI);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x, y - s*0.3);
  ctx.moveTo(x, y);
  ctx.lineTo(x + s*0.25, y + s*0.1);
  ctx.stroke();
  ctx.restore();
}

function drawRefresh(ctx, x, y, s, color) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc(x, y, s*0.4, Math.PI * 0.3, Math.PI * 1.8);
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(x + s*0.3, y - s*0.4);
  ctx.lineTo(x + s*0.5, y - s*0.3);
  ctx.lineTo(x + s*0.4, y - s*0.1);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
}

function drawThermometer(ctx, x, y, s, color) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 2;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc(x, y + s*0.3, s*0.18, 0, 2*Math.PI);
  ctx.fill();
  ctx.beginPath();
  ctx.moveTo(x, y - s*0.45);
  ctx.lineTo(x, y + s*0.2);
  ctx.stroke();
  ctx.restore();
}

function parseEvents(text) {
  const lines = text.split(/\r?\n/);
  const events = [];
  let lastBatt = 8.0;

  const map = [
    { re: /\[EVT\]\s+STATE\s+->\s+INPUT_LOST/, type: 'INPUT_LOST',  cat: 'power' },
    { re: /\[EVT\]\s+STATE\s+->\s+NORMAL/,     type: 'INPUT_REST', cat: 'power' },
    { re: /\[EVT\]\s+STATE\s+->\s+LOW_BATT/,   type: 'BATT_LOW',   cat: 'batt'  },
    { re: /\[EVT\]\s+STATE\s+->\s+CRITICAL/,   type: 'BATT_CRIT',  cat: 'batt'  },
    { re: /\[EVT\]\s+SNMP event sent:/,        type: 'SNMP',       cat: 'snmp'  },
    { re: /\[EVT\]\s+WoL sent to/,             type: 'WOL',        cat: 'wol'   },
    { re: /\[EVT\]\s+NTP:\s+time synced/,      type: 'NTP_OK',     cat: 'ntp'   },
    { re: /\[EVT\]\s+NTP:\s+sync FAILED/,      type: 'NTP_FAIL',   cat: 'ntp'   },
    { re: /\[EVT\]\s+Device boot/,             type: 'BOOT',       cat: 'boot'  },
    { re: /\[EVT\]\s+WiFi connected/,          type: 'WIFI_OK',    cat: 'wifi'  },
    { re: /\[EVT\]\s+WiFi lost/,               type: 'WIFI_LOST',  cat: 'wifi'  },
    { re: /\[EVT\]\s+TEMP_HIGH/,               type: 'TEMP_HIGH',  cat: 'temp'  },
    { re: /\[EVT\]\s+TG:\s+mail sent OK/,      type: 'TG_OK',      cat: 'notify' },
    { re: /\[EVT\]\s+TG:\s+send FAILED/,       type: 'TG_FAIL',    cat: 'notify' },
    { re: /\[EVT\]\s+EM:\s+mail sent OK/,      type: 'EM_OK',      cat: 'notify' },
    { re: /\[EVT\]\s+EM:\s+send FAILED/,       type: 'EM_FAIL',    cat: 'notify' }
  ];

  const reLog = /\[LOG\]\s+BATT\s+([\d.]+)V/;
  for (const line of lines) {
    const mLog = line.match(reLog);
    if (mLog) lastBatt = parseFloat(mLog[1]);
    for (const m of map) {
      if (m.re.test(line)) {
        const mTs = line.match(/^(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})/);
        if (!mTs) continue;
        const d = new Date(mTs[1].replace(' ', 'T'));
        if (isNaN(d.getTime())) continue;
        events.push({ t: d.getTime(), type: m.type, cat: m.cat, y: lastBatt });
        break;
      }
    }
  }
  return events;
}

function evtColor(type) {
  switch (type) {
    case 'INPUT_LOST': return '#e84c3c';
    case 'INPUT_REST': return '#3ecb6e';
    case 'BATT_LOW':   return '#e8b53c';
    case 'BATT_CRIT':  return '#e84c3c';
    case 'SNMP':       return '#a35eff';
    case 'WOL':        return '#4ea1ff';
    case 'NTP_OK':     return '#3ecb6e';
    case 'NTP_FAIL':   return '#e84c3c';
    case 'BOOT':       return '#ffffff';
    case 'WIFI_OK':    return '#3ecb6e';
    case 'WIFI_LOST':  return '#e84c3c';
    case 'TEMP_HIGH':  return '#ff8c42';
    case 'TG_OK':      return '#3ecb6e';
    case 'TG_FAIL':    return '#e84c3c';
    case 'EM_OK':      return '#3ecb6e';
    case 'EM_FAIL':    return '#e84c3c';
    default:           return '#ffffff';
  }
}

function evtTipText(type) {
  switch (type) {
    case 'INPUT_LOST': return tr('tipEvtInputLost');
    case 'INPUT_REST': return tr('tipEvtInputRest');
    case 'BATT_LOW':   return tr('tipEvtBattLow');
    case 'BATT_CRIT':  return tr('tipEvtBattCrit');
    case 'SNMP':       return tr('tipEvtSnmp');
    case 'WOL':        return tr('tipEvtWol');
    case 'NTP_OK':     return tr('tipEvtNtpOk');
    case 'NTP_FAIL':   return tr('tipEvtNtpFail');
    case 'BOOT':       return tr('tipEvtBoot');
    case 'WIFI_OK':    return tr('tipEvtWifiOk');
    case 'WIFI_LOST':  return tr('tipEvtWifiLost');
    case 'TEMP_HIGH':  return tr('tipEvtTempHigh');
    case 'TG_OK':      return tr('tipEvtTgOk');
    case 'TG_FAIL':    return tr('tipEvtTgFail');
    case 'EM_OK':      return tr('tipEvtEmOk');
    case 'EM_FAIL':    return tr('tipEvtEmFail');
    default:           return type;
  }
}

//=================================================================
// ПЛАГИН ИКОНОК + СТЕК ПО X-КООРДИНАТЕ
//
// R09.4: стек теперь считается ПО X-КООРДИНАТЕ (совпадение в
// пределах 3 px), независимо от разницы во времени. События,
// «прилипшие» к одной точке [LOG], выстраиваются столбиком
// снизу вверх — даже если их времена разнесены на минуты.
//
// Порядок `hits` теперь: сначала по xPix (возрастание), внутри
// одной X — снизу вверх по времени. В mousemove при равном
// расстоянии берём ПОСЛЕДНЮЮ (= верхнюю).
//=================================================================
const iconsPlugin = {
  id: 'iconsPlugin',
  afterDatasetsDraw(chart) {
    const ctx = chart.ctx;
    const events = chart._upsEvents || [];
    const pts = chart._upsPts || [];
    if (!events.length || !pts.length) { chart._upsHits = []; return; }

    const xScale = chart.scales.x;
    const yScale = chart.scales.y;

    function findNearestIndex(t) {
      let bestIdx = 0, bestDiff = Infinity;
      for (let i = 0; i < pts.length; i++) {
        const diff = Math.abs(pts[i].t - t);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
      }
      return bestIdx;
    }

    const yBottomBase = yScale.getPixelForValue(0);
    const chartBottom = chart.chartArea.bottom;
    const yBase = Math.min(yBottomBase, chartBottom) - 12;

    // Шаг 1: собрать события с xPix
    const items = [];
    for (const ev of events) {
      const idx = findNearestIndex(ev.t);
      const xPix = xScale.getPixelForValue(idx);
      if (isNaN(xPix)) continue;
      items.push({ ev, xPix });
    }

    // Шаг 2: сортировка — сначала по X, внутри X по t (возр.)
    items.sort((a, b) => {
      if (Math.abs(a.xPix - b.xPix) > 3) return a.xPix - b.xPix;
      return a.ev.t - b.ev.t;
    });

    // Шаг 3: стек по совпадению X
    for (let i = 0; i < items.length; i++) {
      items[i].stack = 0;
      for (let j = 0; j < i; j++) {
        if (Math.abs(items[i].xPix - items[j].xPix) < 3) {
          items[i].stack = Math.max(items[i].stack, items[j].stack + 1);
        }
      }
    }

    // Шаг 4: рисуем и заполняем hits в этом порядке (снизу вверх)
    const hits = [];
    for (const it of items) {
      const ev = it.ev;
      const xPix = it.xPix;
      const yPix = yBase - it.stack * 26;
      const color = evtColor(ev.type);

      ctx.save();
      ctx.fillStyle = 'rgba(0,0,0,0.75)';
      ctx.beginPath();
      ctx.arc(xPix, yPix, 12, 0, 2*Math.PI);
      ctx.fill();
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.8;
      ctx.stroke();
      ctx.restore();

      const s = 14;
      switch (ev.type) {
        case 'INPUT_LOST': drawLightning(ctx, xPix, yPix, s, color, true); break;
        case 'INPUT_REST': drawLightning(ctx, xPix, yPix, s, color, false); break;
        case 'BATT_LOW':   drawBattery(ctx, xPix, yPix, s, color, 1); break;
        case 'BATT_CRIT':  drawBattery(ctx, xPix, yPix, s, color, 0); break;
        case 'SNMP':       drawEnvelope(ctx, xPix, yPix, s, color); break;
        case 'WOL':        drawPowerSymbol(ctx, xPix, yPix, s, color); break;
        case 'NTP_OK':
        case 'NTP_FAIL':   drawClock(ctx, xPix, yPix, s, color); break;
        case 'BOOT':       drawRefresh(ctx, xPix, yPix, s, color); break;
        case 'WIFI_OK':    drawWifi(ctx, xPix, yPix, s, color, false); break;
        case 'WIFI_LOST':  drawWifi(ctx, xPix, yPix, s, color, true); break;
        case 'TEMP_HIGH':  drawThermometer(ctx, xPix, yPix, s, color); break;
        case 'TG_OK':
        case 'TG_FAIL':
        case 'EM_OK':
        case 'EM_FAIL':    drawEnvelope(ctx, xPix, yPix, s, color); break;
      }

      hits.push({ x: xPix, y: yPix, r: 11, type: ev.type });
    }

    chart._upsHits = hits;
  }
};

//=================================================================
// СОСТОЯНИЕ ГРАФИКА
//=================================================================
let chartObj = null;
let lastMaxBytes = 0;
let lastPoints = [];
let lastEvents = [];

function pad(n){return n<10?'0'+n:''+n;}

function fmtBytes(b){
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(1) + ' KB';
  return (b/1024/1024).toFixed(2) + ' MB';
}

function fmtDuration(sec){
  if (sec < 60) return Math.round(sec) + ' s';
  if (sec < 3600) return Math.round(sec/60) + ' min';
  if (sec < 86400) {
    const h = Math.floor(sec/3600);
    const m = Math.round((sec%3600)/60);
    return h + ' h ' + m + ' min';
  }
  const d = Math.floor(sec/86400);
  const h = Math.floor((sec%86400)/3600);
  return d + ' d ' + h + ' h';
}

//=================================================================
// ОБНОВЛЕНИЕ СТАТУСА
//=================================================================
async function refresh() {
  try {
    const r = await fetch('/api/status');
    const j = await r.json();
    lastStateName = j.state_name || 'NORMAL';
    document.getElementById('state').textContent = stateLabel(lastStateName);
    document.getElementById('state').className = 'value state-' + j.state_name;
    document.getElementById('battV').textContent = j.battery_v.toFixed(3);
    document.getElementById('battPct').textContent = j.battery_pct;
    document.getElementById('battBar').style.width = j.battery_pct + '%';
    document.getElementById('inpV').textContent = j.input_v.toFixed(3);

    const inpEl = document.getElementById('inpSt');
    inpEl.dataset.ok = j.input_present ? '1' : '0';
    inpEl.textContent = j.input_present ? tr('powerOk') : tr('powerLost');

    const rt = j.runtime_sec;
    document.getElementById('runtime').textContent =
      rt > 0 ? Math.floor(rt/60) + ' min ' + (rt%60) + ' s' : '0 s';
    document.getElementById('net').textContent = j.ip + ' / ' + j.ssid;
    document.getElementById('rssi').textContent = j.rssi + ' dBm';
    document.getElementById('uptime').textContent = j.uptime_str;
    document.getElementById('cpu_temp').textContent =
      (j.temp_c !== undefined) ? j.temp_c.toFixed(1) + ' °C' : '—';
    document.getElementById('heap_free').textContent =
      (j.heap_free !== undefined) ? fmtBytes(j.heap_free) : '—';

    const dt = j.datetime || '—';
    const now = new Date();
    const upd = pad(now.getHours()) + ':' + pad(now.getMinutes()) + ':' +
                pad(now.getSeconds()) + ' ' +
                pad(now.getDate()) + '.' + pad(now.getMonth()+1) + '.' +
                now.getFullYear();
    document.getElementById('clk_main').textContent = dt;
    document.getElementById('clk_upd').textContent  = upd;

    const ntpEl = document.getElementById('clk_ntp');
    ntpEl.dataset.synced = j.ntp_synced ? '1' : '0';
    ntpEl.textContent = j.ntp_synced ? tr('ntpSynced') : tr('ntpNo');

    document.getElementById('updline').textContent = upd;
    document.getElementById('cal_batt_off_show').textContent = j.cal_batt_off.toFixed(2);
    document.getElementById('cal_batt_gain_show').textContent = j.cal_batt_gain.toFixed(4);
    document.getElementById('cal_batt_v').textContent = j.battery_v.toFixed(3);
    document.getElementById('cal_input_off_show').textContent = j.cal_input_off.toFixed(2);
    document.getElementById('cal_input_gain_show').textContent = j.cal_input_gain.toFixed(4);
    document.getElementById('cal_input_v').textContent = j.input_v.toFixed(3);
    document.getElementById('host').textContent = location.host;
    if (j.fw) document.getElementById('fwver').textContent = j.fw;

    if (j.wol_status_code !== undefined) {
      lastWolCode = j.wol_status_code;
      lastWolSec  = j.wol_status_sec || 0;
      updateWolStatus();
    }
    if (j.tg_status_code !== undefined) {
      lastTgCode = j.tg_status_code;
      lastTgSec  = j.tg_status_queue || 0;
      updateTgStatus();
    }
    if (j.em_status_code !== undefined) {
      lastEmCode = j.em_status_code;
      lastEmSec  = j.em_status_queue || 0;
      updateEmStatus();
    }

    if (j.log_size !== undefined) {
      document.getElementById('lc_logsize').textContent = fmtBytes(j.log_size);
      document.getElementById('lc_logold').textContent =
        fmtBytes(j.log_old_bytes !== undefined ? j.log_old_bytes : 0);
      document.getElementById('lc_logtotal').textContent =
        fmtBytes(j.log_total_bytes !== undefined ? j.log_total_bytes : j.log_size);
      document.getElementById('lc_maxbytes').textContent = fmtBytes(j.log_max_bytes);
      document.getElementById('lc_lfsfree').textContent = fmtBytes(j.lfs_free);
      lastMaxBytes = j.log_max_bytes;
      const sel = document.getElementById('lc_period');
      if (sel && !sel.dataset.userChanged) {
        if (j.log_period_sec !== undefined) sel.value = j.log_period_sec;
      }
      recalcFill();
    }

    if (!uiCfgLoaded && j.ui_lang !== undefined && j.ui_event_mask !== undefined) {
      uiCfgLoaded = true;
      applyUiConfig(j.ui_lang, j.ui_event_mask);
    }
  } catch(e) { console.error(e); }
}
refresh();
setInterval(refresh, 2000);

function recalcFill() {
  const sel = document.getElementById('lc_period');
  if (!sel) return;
  sel.dataset.userChanged = '1';
  const period = parseInt(sel.value);
  const el = document.getElementById('lc_fill1');
  if (!el) return;

  if (period === 0) {
    el.textContent = '∞';
    return;
  }

  const lineBytes = 130;
  const capacity = Math.floor(lastMaxBytes / lineBytes);
  const baseSec = capacity * period;
  const evtSec = baseSec * 0.8;
  el.textContent = fmtDuration(evtSec);
}

function applyUiConfig(lang, mask) {
  if (lang === 'en' || lang === 'ru') {
    applyLang(lang);
  }
  const m = (mask !== undefined) ? parseInt(mask) : 0x00FF;
  const setChk = (id, bit) => {
    const el = document.getElementById(id);
    if (el) el.checked = !!(m & bit);
  };
  setChk('ev_power', 0x0001);
  setChk('ev_batt',  0x0002);
  setChk('ev_wifi',  0x0004);
  setChk('ev_snmp',  0x0008);
  setChk('ev_wol',   0x0010);
  setChk('ev_ntp',   0x0020);
  setChk('ev_boot',  0x0040);
  setChk('ev_tg_em', 0x0080);
}

async function saveUiEventMask() {
  let mask = 0;
  if (document.getElementById('ev_power').checked) mask |= 0x0001;
  if (document.getElementById('ev_batt').checked)  mask |= 0x0002;
  if (document.getElementById('ev_wifi').checked)  mask |= 0x0004;
  if (document.getElementById('ev_snmp').checked)  mask |= 0x0008;
  if (document.getElementById('ev_wol').checked)   mask |= 0x0010;
  if (document.getElementById('ev_ntp').checked)   mask |= 0x0020;
  if (document.getElementById('ev_boot').checked)  mask |= 0x0040;
  if (document.getElementById('ev_tg_em').checked) mask |= 0x0080;

  try {
    await fetch('/api/ui_config', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({uiEventMask: mask})
    });
  } catch(e) { /* silent */ }
}

document.querySelectorAll('.tabs button').forEach(b=>{
  b.onclick=()=>{
    document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.tab-pane').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    document.getElementById('tab-' + b.dataset.tab).classList.add('active');
    if (b.dataset.tab === 'chart') loadChart();
  };
});

['ev_power','ev_batt','ev_wifi','ev_snmp','ev_wol','ev_ntp','ev_boot','ev_tg_em']
  .forEach(id => {
    const el = document.getElementById(id);
    if (el) el.onchange = () => {
      saveUiEventMask();
      rebuildChart();
    };
  });

document.getElementById('chk_batt').onchange = () => rebuildChart();
document.getElementById('chk_inp').onchange  = () => rebuildChart();
document.getElementById('chk_temp').onchange = () => rebuildChart();

function showMsg(text, ok) {
  const m = document.getElementById('msg');
  m.textContent = text;
  m.className = 'msg ' + (ok ? 'ok' : 'err');
  setTimeout(()=>{ m.className='msg'; }, 4000);
}

//=================================================================
// ПОДСВЕТКА СОБЫТИЯ ПРИ НАВЕДЕНИИ
// R09.4: ищем БЛИЖАЙШУЮ иконку; при равном расстоянии берём
//        ПОСЛЕДНЮЮ в массиве (= визуально верхнюю в столбике).
//=================================================================
(function() {
  const tip = document.getElementById('evtTip');

  document.addEventListener('mousemove', (e) => {
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (!el || el.id !== 'chartMain') {
      if (tip.style.display !== 'none') tip.style.display = 'none';
      return;
    }
    if (!chartObj || !chartObj._upsHits || !chartObj._upsHits.length) {
      tip.style.display = 'none';
      return;
    }

    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    let hit = null;
    let bestDist2 = Infinity;
    for (const h of chartObj._upsHits) {
      const dx = x - h.x;
      const dy = y - h.y;
      const d2 = dx*dx + dy*dy;
      // R09.4: <= вместо < — при равном расстоянии берём последнюю
      if (d2 <= h.r * h.r && d2 <= bestDist2) {
        bestDist2 = d2;
        hit = h;
      }
    }

    if (!hit) {
      tip.style.display = 'none';
      return;
    }

    tip.innerHTML = '<b>' + evtTipText(hit.type) + '</b>';
    tip.style.left = (e.clientX + 12) + 'px';
    tip.style.top  = (e.clientY + 12) + 'px';
    tip.style.display = 'block';
  });

  document.addEventListener('mouseleave', () => {
    tip.style.display = 'none';
  });
})();

//=================================================================
// ПАРСИНГ ЛОГА
//=================================================================
function parseLogLines(text) {
  const lines = text.split(/\r?\n/);
  const points = [];
  const re = /^(.+?)\s+up\s+(?:(\d+)d\s+)?(\d{2}:\d{2}:\d{2})\s+\[LOG\]\s+BATT\s+([\d.]+)V\s+\((\d+)%\)\s+\|\s+INPUT\s+([\d.]+)V\s+\(([^)]+)\)\s+\|\s+runtime\s+(\d+)\s+s\s+\|\s+TEMP\s+([\-\d.]+)/;
  for (const line of lines) {
    const m = line.match(re);
    if (!m) continue;
    const ts = m[1];
    const batt = parseFloat(m[4]);
    const pct = parseInt(m[5]);
    const inp = parseFloat(m[6]);
    const rt = parseInt(m[8]);
    const temp = parseFloat(m[9]);
    const d = new Date(ts.replace(' ', 'T'));
    if (isNaN(d.getTime())) continue;
    points.push({t: d.getTime(), batt, pct, inp, rt, temp});
  }
  points.sort((a, b) => a.t - b.t);
  return points;
}

function filterByWindow(points, fromMs, toMs) {
  return points.filter(p => p.t >= fromMs && p.t <= toMs);
}

async function loadChart() {
  showChartMask(true);
  try {
    const r = await fetch('/api/log');
    const txt = await r.text();
    lastPoints = parseLogLines(txt);
    lastEvents = parseEvents(txt);

    chartLoaded = true;

    if (lastPoints.length > 0) {
      chartDataMin = lastPoints[0].t;
      chartDataMax = lastPoints[lastPoints.length - 1].t;
    } else {
      chartDataMin = 0;
      chartDataMax = 0;
    }

    if (chartToMs === 0) {
      initChartWindow();
    } else {
      const maxTo = ceilToHour(Date.now());
      if (chartToMs > maxTo) chartToMs = maxTo;
    }

    renderRangeRow();
    rebuildChart();
  } catch(e) {
    console.error(e);
  } finally {
    showChartMask(false);
  }
}

function rebuildChart() {
  const to   = chartToMs || ceilToHour(Date.now());
  const from = to - rangeMs();

  const pts = filterByWindow(lastPoints, from, to);
  const labels = pts.map(p => {
    const d = new Date(p.t);
    return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ' ' +
           pad(d.getDate()) + '.' + pad(d.getMonth()+1);
  });
  const dataBatt = pts.map(p => p.batt);
  const dataInp  = pts.map(p => p.inp);
  const dataTemp = pts.map(p => p.temp);

  const datasets = [];
  if (document.getElementById('chk_batt').checked) {
    datasets.push({ label:tr('legendBatt'), data:dataBatt,
      borderColor:'#4ea1ff', backgroundColor:'rgba(78,161,255,0.15)',
      yAxisID:'y', tension:0.2, pointRadius:0 });
  }
  if (document.getElementById('chk_inp').checked) {
    datasets.push({ label:tr('legendInput'), data:dataInp,
      borderColor:'#3ecb6e', backgroundColor:'rgba(62,203,110,0.15)',
      yAxisID:'y', tension:0.2, pointRadius:0 });
  }
  if (document.getElementById('chk_temp').checked) {
    datasets.push({ label:tr('legendTemp'), data:dataTemp,
      borderColor:'#ff8c42', backgroundColor:'rgba(255,140,66,0.15)',
      yAxisID:'y1', tension:0.2, pointRadius:0 });
  }

  const enabledCats = [];
  if (document.getElementById('ev_power').checked) enabledCats.push('power');
  if (document.getElementById('ev_batt').checked)  enabledCats.push('batt');
  if (document.getElementById('ev_wifi').checked)  enabledCats.push('wifi');
  if (document.getElementById('ev_snmp').checked)  enabledCats.push('snmp');
  if (document.getElementById('ev_wol').checked)   enabledCats.push('wol');
  if (document.getElementById('ev_ntp').checked)   enabledCats.push('ntp');
  if (document.getElementById('ev_boot').checked)  enabledCats.push('boot');
  if (document.getElementById('ev_tg_em').checked) enabledCats.push('notify');
  enabledCats.push('temp');

  // R09.4: без _stack — стек теперь считается в iconsPlugin по X.
  let visibleEvents = lastEvents
    .filter(ev => enabledCats.includes(ev.cat) && ev.t >= from && ev.t <= to)
    .sort((a, b) => a.t - b.t);

  const cfg = {
    type:'line',
    data:{ labels, datasets },
    plugins: [iconsPlugin],
    options:{
      responsive:true, animation:false,
      interaction:{ mode:'index', intersect:false },
      layout:{ padding:{ bottom: 40 } },
      scales:{
        y:{ type:'linear', position:'left',
            title:{ display:true, text:'Voltage, V', color:'#ccc' },
            ticks:{ color:'#aaa' }, grid:{ color:'#2a2a2a' } },
        y1:{ type:'linear', position:'right',
            title:{ display:true, text:'Temp., °C', color:'#ccc' },
            ticks:{ color:'#aaa' }, grid:{ drawOnChartArea:false } },
        x:{ ticks:{ color:'#aaa', maxRotation:45, autoSkip:true },
            grid:{ color:'#222' } }
      },
      plugins:{
        legend:{ display:false },
        tooltip:{ enabled:true }
      }
    }
  };

  if (chartObj) chartObj.destroy();
  chartObj = new Chart(document.getElementById('chartMain'), cfg);
  chartObj._upsEvents = visibleEvents;
  chartObj._upsPts    = pts;
  chartObj.update('none');
}

//=================================================================
// ЛОГ
//=================================================================
async function loadLogCfg() {
  try {
    const r = await fetch('/api/config');
    const c = await r.json();
    if (c.logVoltPeriodSec !== undefined) {
      const sel = document.getElementById('lc_period');
      if (sel) sel.value = c.logVoltPeriodSec;
      recalcFill();
    }
  } catch(e) { console.error(e); }
}
loadLogCfg();

async function saveLogCfg() {
  const sel = document.getElementById('lc_period');
  const period = parseInt(sel.value);
  let periodStr;
  if (period === 0) periodStr = tr('logOff');
  else              periodStr = period + ' s';

  if (!confirm(tr('confirmSaveLog') + '\n\n' +
               tr('logPeriod') + ': ' + periodStr)) return;

  const r = await fetch('/api/log_config', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({logVoltPeriodSec: period})
  });
  if (r.ok) {
    isDirty = false;
    showMsg(tr('logSaved'), true);
    setTimeout(()=>{ fetch('/api/reboot'); }, 1000);
  } else {
    showMsg(tr('logSaveErr'), false);
  }
}

async function ntpSyncNow() {
  if (!confirm(tr('confirmNtp'))) return;
  const r = await fetch('/api/ntp_sync', {method:'POST'});
  if (r.ok) showMsg(tr('ntpSent'), true);
  else      showMsg(tr('ntpErr'), false);
}

//=================================================================
// Сбор ВСЕХ полей со ВСЕХ вкладок
//=================================================================
const PANE_FIELDS = {
  net:  ['wifiSsid','wifiPass','hostname','useDhcp','ip','mask','gw','dns',
         'ntpServer1','ntpServer2','ntpTz','ntpUse','tempHighC',
         'battFullV','battEmptyV','battWarnV','battCapacityWh','loadCurrentA',
         'inputLostV','hystV','adcPollMs'],
  snmp: ['qnapIp','qnapMac','snmpCommunity','snmpPort','useInform','enterpriseOid'],
  cal:  ['battOffsetMv','battGain','inputOffsetMv','inputGain'],
  wol:  ['wolEnable','wolOutageMinSec','wolStableSec','wolBattMinPct'],
  tg:   ['tgToken','tgChatId','tgEnable','tgPeriod','tgEvents'],
  em:   ['emSmtpHost','emSmtpPort','emUser','emPass','emFrom','emTo',
         'emEnable','emPeriod','emEvents']
};

function collectAllFields() {
  const body = {};
  Object.keys(PANE_FIELDS).forEach(pane => {
    PANE_FIELDS[pane].forEach(f => {
      const el = document.getElementById('c_' + f);
      if (el) body[f] = el.value;
    });
  });
  const ipEl  = document.getElementById('c2_qnapIp');
  const macEl = document.getElementById('c2_qnapMac');
  if (ipEl)  body.qnapIp  = ipEl.value;
  if (macEl) body.qnapMac = macEl.value;

  let tgMask = 0;
  if (document.getElementById('evt_power').checked) tgMask |= 0x0001;
  if (document.getElementById('evt_batt').checked)  tgMask |= 0x0002;
  if (document.getElementById('evt_wol').checked)   tgMask |= 0x0004;
  if (document.getElementById('evt_boot').checked)  tgMask |= 0x0008;
  if (document.getElementById('evt_wifi').checked)  tgMask |= 0x0010;
  if (document.getElementById('evt_snmp').checked)  tgMask |= 0x0020;
  if (document.getElementById('evt_ntp').checked)   tgMask |= 0x0040;
  if (document.getElementById('evt_temp').checked)  tgMask |= 0x0080;
  body.tgEvents = tgMask;

  let emMask = 0;
  if (document.getElementById('emt_power').checked) emMask |= 0x0001;
  if (document.getElementById('emt_batt').checked)  emMask |= 0x0002;
  if (document.getElementById('emt_wol').checked)   emMask |= 0x0004;
  if (document.getElementById('emt_boot').checked)  emMask |= 0x0008;
  if (document.getElementById('emt_wifi').checked)  emMask |= 0x0010;
  if (document.getElementById('emt_snmp').checked)  emMask |= 0x0020;
  if (document.getElementById('emt_ntp').checked)   emMask |= 0x0040;
  if (document.getElementById('emt_temp').checked)  emMask |= 0x0080;
  body.emEvents = emMask;

  body.informPeriodSec = 30;

  body.uiLang = curLang;
  let uiMask = 0;
  if (document.getElementById('ev_power').checked) uiMask |= 0x0001;
  if (document.getElementById('ev_batt').checked)  uiMask |= 0x0002;
  if (document.getElementById('ev_wifi').checked)  uiMask |= 0x0004;
  if (document.getElementById('ev_snmp').checked)  uiMask |= 0x0008;
  if (document.getElementById('ev_wol').checked)   uiMask |= 0x0010;
  if (document.getElementById('ev_ntp').checked)   uiMask |= 0x0020;
  if (document.getElementById('ev_boot').checked)  uiMask |= 0x0040;
  if (document.getElementById('ev_tg_em').checked) uiMask |= 0x0080;
  body.uiEventMask = uiMask;

  return body;
}

async function saveAllPanes() {
  const body = collectAllFields();

  if (!confirm(tr('confirmSave') + '?\n\n' + tr('confirmReboot'))) return;

  try {
    const r = await fetch('/api/config', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify(body)
    });
    if (r.ok) {
      isDirty = false;
      showMsg(tr('saveChanges') + ' ' + tr('savedOk'), true);
      setTimeout(()=>{ fetch('/api/reboot'); }, 1500);
    } else {
      showMsg(tr('saveErr') + r.status, false);
    }
  } catch(e) {
    showMsg(tr('saveErr') + e, false);
  }
}

//=================================================================
// ЗАГРУЗКА КОНФИГА
//=================================================================
async function loadConfig() {
  const r = await fetch('/api/config');
  const c = await r.json();
  for (const k in c) {
    const el = document.getElementById('c_' + k);
    if (el) el.value = c[k];
  }
  const ipEl  = document.getElementById('c2_qnapIp');
  const macEl = document.getElementById('c2_qnapMac');
  if (ipEl  && c.qnapIp)  ipEl.value  = c.qnapIp;
  if (macEl && c.qnapMac) macEl.value = c.qnapMac;

  const mask = (c.tgEvents !== undefined) ? parseInt(c.tgEvents) : 0x00FF;
  document.getElementById('evt_power').checked = !!(mask & 0x0001);
  document.getElementById('evt_batt').checked  = !!(mask & 0x0002);
  document.getElementById('evt_wol').checked   = !!(mask & 0x0004);
  document.getElementById('evt_boot').checked  = !!(mask & 0x0008);
  document.getElementById('evt_wifi').checked  = !!(mask & 0x0010);
  document.getElementById('evt_snmp').checked  = !!(mask & 0x0020);
  document.getElementById('evt_ntp').checked   = !!(mask & 0x0040);
  document.getElementById('evt_temp').checked  = !!(mask & 0x0080);

  const emask = (c.emEvents !== undefined) ? parseInt(c.emEvents) : 0x00FF;
  document.getElementById('emt_power').checked = !!(emask & 0x0001);
  document.getElementById('emt_batt').checked  = !!(emask & 0x0002);
  document.getElementById('emt_wol').checked   = !!(emask & 0x0004);
  document.getElementById('emt_boot').checked  = !!(emask & 0x0008);
  document.getElementById('emt_wifi').checked  = !!(emask & 0x0010);
  document.getElementById('emt_snmp').checked  = !!(emask & 0x0020);
  document.getElementById('emt_ntp').checked   = !!(emask & 0x0040);
  document.getElementById('emt_temp').checked  = !!(emask & 0x0080);

  if (c.tgPeriod !== undefined) {
    document.getElementById('c_tgPeriod').value = c.tgPeriod;
  }
  if (c.emPeriod !== undefined) {
    document.getElementById('c_emPeriod').value = c.emPeriod;
  }

  if (c.uiLang !== undefined) {
    applyLang(c.uiLang);
  }
  if (c.uiEventMask !== undefined) {
    applyUiConfig(c.uiLang, c.uiEventMask);
  }
}
loadConfig();

//=================================================================
// СБРОС ПАНЕЛИ
//=================================================================
function paneName(pane) {
  switch(pane) {
    case 'net':  return tr('paneNet');
    case 'snmp': return tr('paneSnmp');
    case 'cal':  return tr('paneCal');
    case 'wol':  return tr('paneWol');
    case 'tg':   return tr('paneTg');
    case 'em':   return tr('paneEm');
    default:     return pane;
  }
}

async function resetPane(pane) {
  const name = paneName(pane);
  if (!confirm(tr('confirmReset') + ' «' + name + '» ' + tr('confirmResetQ') + '\n\n' +
               tr('confirmResetNote') + '\n' + tr('confirmReboot'))) return;

  const r = await fetch('/api/config_reset_pane', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({pane: pane})
  });
  if (r.ok) {
    isDirty = false;
    showMsg('«' + name + '» ' + tr('resetOk'), true);
    setTimeout(()=>{ fetch('/api/reboot'); }, 1500);
  } else {
    showMsg(tr('resetErr') + r.status, false);
  }
}

//=================================================================
// КАЛИБРОВКА
//=================================================================
async function applyCal(which) {
  const realEl = document.getElementById('cal_' + which + '_real');
  const real = parseFloat(realEl.value);
  if (!isFinite(real) || real <= 0) {
    showMsg(tr('badVoltage'), false); return;
  }
  const r = await fetch('/api/calibrate', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({channel: which, real_v: real})
  });
  if (r.ok) {
    const j = await r.json();
    isDirty = true;
    showMsg(tr('calApplied') + j.gain.toFixed(4) + tr('calApplied2'), true);
  } else showMsg(tr('calErr'), false);
}

async function resetCal(which) {
  const whichName = which === 'batt' ? tr('battShort') : tr('inputShort');
  if (!confirm(tr('confirmResetCal') + ' ' + whichName + '?')) return;
  const r = await fetch('/api/calibrate_reset', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({channel: which})
  });
  if (r.ok) { isDirty = true; showMsg(tr('calReset'), true); }
}

//=================================================================
// WoL / Тесты
//=================================================================
async function wolManual() {
  if (!confirm(tr('confirmWol'))) return;
  const r = await fetch('/api/wol_manual', {method:'POST'});
  if (r.ok) {
    const j = await r.json();
    showMsg(j.ok ? tr('wolSent') : tr('wolErr'), j.ok);
  } else showMsg(tr('wolErr'), false);
}

async function tgTest() {
  if (!confirm(tr('confirmTgTest'))) return;
  const r = await fetch('/api/telegram_test', {method:'POST'});
  if (r.ok) {
    const j = await r.json();
    showMsg(j.ok ? tr('tgTestOk') : tr('tgTestErr'), j.ok);
  } else showMsg(tr('tgTestErr'), false);
}

async function emTest() {
  if (!confirm(tr('confirmEmTest'))) return;
  const r = await fetch('/api/email_test', {method:'POST'});
  if (r.ok) {
    const j = await r.json();
    showMsg(j.ok ? tr('emTestOk') : tr('emTestErr'), j.ok);
  } else showMsg(tr('emTestErr'), false);
}

//=================================================================
// ЛОГ-ДЕЙСТВИЯ
//=================================================================
async function loadLog() {
  const mask = document.getElementById('logMask');
  if (mask) mask.classList.add('show');
  try {
    const r = await fetch('/api/log');
    const t = await r.text();
    document.getElementById('logbox').textContent = t;
  } catch(e) {
    console.error(e);
    document.getElementById('logbox').textContent = 'Ошибка загрузки лога';
  } finally {
    if (mask) mask.classList.remove('show');
  }
}

async function downloadLog() {
  window.location = '/api/log_download';
}
async function clearLog() {
  if (!confirm(tr('confirmClearLog'))) return;
  const r = await fetch('/api/log_clear', {method:'POST'});
  if (r.ok) { showMsg(tr('logCleared'), true); loadLog(); }
}

//=================================================================
// Отслеживание несохранённых изменений
//=================================================================
(function() {
  document.addEventListener('input', (e) => {
    const t = e.target;
    if (!t) return;
    if (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA') {
      if (t.id === 'lc_period') return;
      isDirty = true;
    }
  }, true);

  document.addEventListener('change', (e) => {
    const t = e.target;
    if (!t) return;
    if (t.tagName === 'SELECT' || (t.tagName === 'INPUT' && t.type === 'checkbox')) {
      if (t.id === 'lc_period') return;
      isDirty = true;
    }
  }, true);

  window.addEventListener('beforeunload', (e) => {
    if (!isDirty) return;
    e.preventDefault();
    e.returnValue = tr('leaveWarn');
    return tr('leaveWarn');
  });
})();

//=================================================================
// ИНИЦИАЛИЗАЦИЯ
//=================================================================
(function() {
  initChartWindow();
  renderRangeRow();

  let saved = null;
  try { saved = localStorage.getItem('lang'); } catch(e) {}
  if (saved !== 'ru' && saved !== 'en') saved = null;

  if (saved) applyLang(saved);
  else       applyLang('ru');
})();
</script>
</body>
</html>
)HTMLDOC";

//=================================================================
// HTTP-ОБРАБОТЧИКИ
//=================================================================
static void handleFavicon() {
    static const char SVG[] PROGMEM =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
        "<rect width='32' height='32' rx='6' fill='#0066cc'/>"
        "<path d='M18 4 L8 18 L14 18 L12 28 L24 12 L17 12 L20 4 Z' "
        "fill='#ffffff'/>"
        "</svg>";
    s_server.send_P(200, "image/svg+xml", SVG);
}

static void handleRoot() {
    s_server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

static void handleApiStatus() {
    UpsStatus st = s_lastStatus;
    String j;
    j.reserve(2400);
    j  = "{";
    j += "\"fw\":\"" FW_RELEASE "\",";
    j += "\"state\":" + String((int)st.state) + ",";
    j += "\"state_name\":\"" + String(upsStateName(st.state)) + "\",";
    j += "\"battery_v\":" + String(st.batteryVolts, 3) + ",";
    j += "\"input_v\":"   + String(st.inputVolts, 3) + ",";
    j += "\"input_present\":" + String(st.inputPresent ? "true" : "false") + ",";
    j += "\"battery_low\":"      + String(st.batteryLow ? "true" : "false") + ",";
    j += "\"battery_critical\":" + String(st.batteryCritical ? "true" : "false") + ",";
    j += "\"battery_pct\":" + String(st.batteryPercent) + ",";
    j += "\"runtime_sec\":" + String((unsigned long)st.runtimeSec) + ",";
    j += "\"sec_on_batt\":" + String((unsigned long)st.secondsOnBattery) + ",";
    j += "\"uptime_sec\":"  + String((unsigned long)sysUptimeSec()) + ",";
    j += "\"uptime_str\":\"" + sysUptimeStr() + "\",";
    j += "\"datetime\":\"" + ntpDateTimeStr() + "\",";
    j += "\"ntp_synced\":" + String(ntpIsSynced() ? "true" : "false") + ",";
    j += "\"temp_c\":" + String(s_lastTempC, 1) + ",";
    j += "\"heap_free\":" + String((unsigned long)ESP.getFreeHeap()) + ",";
    j += "\"ip\":\"" + wifiGetIP() + "\",";
    j += "\"ssid\":\"" + wifiGetSSID() + "\",";
    j += "\"rssi\":" + String((int)wifiGetRSSI()) + ",";
    j += "\"wifi_state\":\"" + String(wifiStateName(wifiGetState())) + "\",";
    j += "\"cal_batt_off\":"  + String(s_cfg.battOffsetMv, 2) + ",";
    j += "\"cal_batt_gain\":" + String(s_cfg.battGain, 4) + ",";
    j += "\"cal_input_off\":"  + String(s_cfg.inputOffsetMv, 2) + ",";
    j += "\"cal_input_gain\":" + String(s_cfg.inputGain, 4) + ",";
    j += "\"wol_status\":\"" + wolStatusText() + "\",";
    j += "\"wol_status_code\":\"" + wolStatusCode() + "\",";
    j += "\"wol_status_sec\":" + String((unsigned long)wolStatusSec()) + ",";
    j += "\"tg_status\":\"" + telegramStatusText() + "\",";
    j += "\"tg_status_code\":\"" + telegramStatusCode() + "\",";
    j += "\"tg_status_queue\":" + String((unsigned long)telegramStatusQueue()) + ",";
    j += "\"em_status\":\"" + emailStatusText() + "\",";
    j += "\"em_status_code\":\"" + emailStatusCode() + "\",";
    j += "\"em_status_queue\":" + String((unsigned long)emailStatusQueue()) + ",";
    size_t logFresh = loggerFileSize();
    size_t logTotal = loggerTotalSize();
    size_t logOld   = (logTotal > logFresh) ? (logTotal - logFresh) : 0;
    j += "\"log_size\":" + String((unsigned long)logFresh) + ",";
    j += "\"log_old_bytes\":" + String((unsigned long)logOld) + ",";
    j += "\"log_total_bytes\":" + String((unsigned long)logTotal) + ",";
    j += "\"log_max_bytes\":" + String((unsigned long)loggerMaxBytes()) + ",";
    j += "\"log_period_sec\":" + String((unsigned)s_cfg.logVoltPeriodSec) + ",";
    j += "\"lfs_total\":" + String((unsigned long)LittleFS.totalBytes()) + ",";
    j += "\"ui_lang\":\"" + s_cfg.uiLang + "\",";
    j += "\"ui_event_mask\":" + String((unsigned)s_cfg.uiEventMask) + ",";
    j += "\"lfs_free\":"  + String((unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    j += "}";
    s_server.send(200, "application/json", j);
}

static void handleApiConfigGet() {
    String j;
    j.reserve(2400);
    j  = "{";
    j += "\"wifiSsid\":\"" + s_cfg.wifiSsid + "\",";
    j += "\"wifiPass\":\"" + s_cfg.wifiPass + "\",";
    j += "\"hostname\":\"" + s_cfg.hostname + "\",";
    j += "\"useDhcp\":" + String(s_cfg.useDhcp ? 1 : 0) + ",";
    j += "\"ip\":\""     + s_cfg.ip + "\",";
    j += "\"mask\":\""   + s_cfg.mask + "\",";
    j += "\"gw\":\""     + s_cfg.gw + "\",";
    j += "\"dns\":\""    + s_cfg.dns + "\",";
    j += "\"ntpUse\":" + String(s_cfg.ntpUse ? 1 : 0) + ",";
    j += "\"ntpServer1\":\"" + s_cfg.ntpServer1 + "\",";
    j += "\"ntpServer2\":\"" + s_cfg.ntpServer2 + "\",";
    j += "\"ntpTz\":\""      + s_cfg.ntpTz + "\",";
    j += "\"tempHighC\":" + String(s_cfg.tempHighC, 1) + ",";
    j += "\"qnapIp\":\""         + s_cfg.qnapIp + "\",";
    j += "\"qnapMac\":\""        + s_cfg.qnapMac + "\",";
    j += "\"snmpCommunity\":\""  + s_cfg.snmpCommunity + "\",";
    j += "\"snmpPort\":" + String((unsigned)s_cfg.snmpPort) + ",";
    j += "\"useInform\":" + String(s_cfg.useInform ? 1 : 0) + ",";
    j += "\"enterpriseOid\":\"" + s_cfg.enterpriseOid + "\",";
    j += "\"battFullV\":"  + String(s_cfg.battFullV, 2) + ",";
    j += "\"battEmptyV\":" + String(s_cfg.battEmptyV, 2) + ",";
    j += "\"battWarnV\":"  + String(s_cfg.battWarnV, 2) + ",";
    j += "\"battCapacityWh\":" + String(s_cfg.battCapacityWh, 2) + ",";
    j += "\"loadCurrentA\":"   + String(s_cfg.loadCurrentA, 2) + ",";
    j += "\"inputLostV\":" + String(s_cfg.inputLostV, 2) + ",";
    j += "\"hystV\":"      + String(s_cfg.hystV, 2) + ",";
    j += "\"adcPollMs\":" + String((unsigned)s_cfg.adcPollMs) + ",";
    j += "\"informPeriodSec\":" + String((unsigned)s_cfg.informPeriodSec) + ",";
    j += "\"battOffsetMv\":"  + String(s_cfg.battOffsetMv, 2) + ",";
    j += "\"battGain\":"      + String(s_cfg.battGain, 4) + ",";
    j += "\"inputOffsetMv\":" + String(s_cfg.inputOffsetMv, 2) + ",";
    j += "\"inputGain\":"     + String(s_cfg.inputGain, 4) + ",";
    j += "\"logVoltPeriodSec\":" + String((unsigned)s_cfg.logVoltPeriodSec) + ",";
    j += "\"wolEnable\":"        + String(s_cfg.wolEnable ? 1 : 0) + ",";
    j += "\"wolOutageMinSec\":"  + String((unsigned)s_cfg.wolOutageMinSec) + ",";
    j += "\"wolStableSec\":"     + String((unsigned)s_cfg.wolStableSec) + ",";
    j += "\"wolBattMinPct\":"    + String((unsigned)s_cfg.wolBattMinPct) + ",";
    j += "\"tgToken\":\""   + s_cfg.tgToken + "\",";
    j += "\"tgChatId\":\""  + s_cfg.tgChatId + "\",";
    j += "\"tgEnable\":" + String(s_cfg.tgEnable ? 1 : 0) + ",";
    j += "\"tgPeriod\":" + String((unsigned)s_cfg.tgPeriod) + ",";
    j += "\"tgEvents\":" + String((unsigned)s_cfg.tgEvents) + ",";
    j += "\"emSmtpHost\":\"" + s_cfg.emSmtpHost + "\",";
    j += "\"emSmtpPort\":" + String((unsigned)s_cfg.emSmtpPort) + ",";
    j += "\"emUser\":\"" + s_cfg.emUser + "\",";
    j += "\"emPass\":\"" + s_cfg.emPass + "\",";
    j += "\"emFrom\":\"" + s_cfg.emFrom + "\",";
    j += "\"emTo\":\""   + s_cfg.emTo + "\",";
    j += "\"emEnable\":" + String(s_cfg.emEnable ? 1 : 0) + ",";
    j += "\"emPeriod\":" + String((unsigned)s_cfg.emPeriod) + ",";
    j += "\"emEvents\":" + String((unsigned)s_cfg.emEvents) + ",";
    j += "\"uiLang\":\"" + s_cfg.uiLang + "\",";
    j += "\"uiEventMask\":" + String((unsigned)s_cfg.uiEventMask);
    j += "}";
    s_server.send(200, "application/json", j);
}

static bool jsonGetString(const String &src, const String &key, String &out) {
    String pattern = "\"" + key + "\"";
    int p = src.indexOf(pattern);
    if (p < 0) return false;
    p = src.indexOf(':', p + pattern.length());
    if (p < 0) return false;
    p++;
    while (p < (int)src.length() && (src[p] == ' ' || src[p] == '\t')) p++;
    if (p >= (int)src.length()) return false;
    if (src[p] == '"') {
        p++;
        int e = src.indexOf('"', p);
        if (e < 0) return false;
        out = src.substring(p, e);
        return true;
    } else {
        int e = p;
        while (e < (int)src.length() && src[e] != ',' && src[e] != '}') e++;
        out = src.substring(p, e);
        out.trim();
        return true;
    }
}
static bool jsonGetFloat(const String &src, const String &key, float &out) {
    String s;
    if (!jsonGetString(src, key, s)) return false;
    if (!sysIsFloat(s)) return false;
    out = s.toFloat();
    return true;
}
static bool jsonGetInt(const String &src, const String &key, int &out) {
    String s;
    if (!jsonGetString(src, key, s)) return false;
    if (!sysIsInt(s)) return false;
    out = s.toInt();
    return true;
}

static void handleApiConfigPost() {
    if (!s_server.hasArg("plain")) { s_server.send(400, "text/plain", "no body"); return; }
    String body = s_server.arg("plain");
    String s; int i; float f;

    if (jsonGetString(body, "wifiSsid", s))       s_cfg.wifiSsid = s;
    if (jsonGetString(body, "wifiPass", s))       s_cfg.wifiPass = s;
    if (jsonGetString(body, "hostname", s))       s_cfg.hostname = s;
    if (jsonGetInt   (body, "useDhcp", i))        s_cfg.useDhcp = (i != 0);
    if (jsonGetString(body, "ip", s))             s_cfg.ip = s;
    if (jsonGetString(body, "mask", s))           s_cfg.mask = s;
    if (jsonGetString(body, "gw", s))             s_cfg.gw = s;
    if (jsonGetString(body, "dns", s))            s_cfg.dns = s;

    if (jsonGetInt   (body, "ntpUse", i))         s_cfg.ntpUse = (i != 0);
    if (jsonGetString(body, "ntpServer1", s))     s_cfg.ntpServer1 = s;
    if (jsonGetString(body, "ntpServer2", s))     s_cfg.ntpServer2 = s;
    if (jsonGetString(body, "ntpTz", s))          s_cfg.ntpTz = s;
    if (jsonGetFloat (body, "tempHighC", f))      s_cfg.tempHighC = f;

    if (jsonGetString(body, "qnapIp", s))         s_cfg.qnapIp = s;
    if (jsonGetString(body, "qnapMac", s))        s_cfg.qnapMac = s;
    if (jsonGetString(body, "snmpCommunity", s))  s_cfg.snmpCommunity = s;
    if (jsonGetInt   (body, "snmpPort", i))       s_cfg.snmpPort = (uint16_t)i;
    if (jsonGetInt   (body, "useInform", i))      s_cfg.useInform = (i != 0);
    if (jsonGetString(body, "enterpriseOid", s))  s_cfg.enterpriseOid = s;

    if (jsonGetFloat (body, "battFullV", f))      s_cfg.battFullV = f;
    if (jsonGetFloat (body, "battEmptyV", f))     s_cfg.battEmptyV = f;
    if (jsonGetFloat (body, "battWarnV", f))      s_cfg.battWarnV = f;
    if (jsonGetFloat (body, "battCapacityWh", f)) s_cfg.battCapacityWh = f;
    if (jsonGetFloat (body, "loadCurrentA", f))   s_cfg.loadCurrentA = f;
    if (jsonGetFloat (body, "inputLostV", f))     s_cfg.inputLostV = f;
    if (jsonGetFloat (body, "hystV", f))          s_cfg.hystV = f;
    if (jsonGetInt   (body, "adcPollMs", i))      s_cfg.adcPollMs = (uint16_t)i;
    s_cfg.informPeriodSec = 30;

    if (jsonGetFloat (body, "battOffsetMv", f))   s_cfg.battOffsetMv = f;
    if (jsonGetFloat (body, "battGain", f))       s_cfg.battGain = f;
    if (jsonGetFloat (body, "inputOffsetMv", f))  s_cfg.inputOffsetMv = f;
    if (jsonGetFloat (body, "inputGain", f))      s_cfg.inputGain = f;

    if (jsonGetInt   (body, "wolEnable", i))      s_cfg.wolEnable = (i != 0);
    if (jsonGetInt   (body, "wolOutageMinSec", i))s_cfg.wolOutageMinSec = (uint16_t)i;
    if (jsonGetInt   (body, "wolStableSec", i))   s_cfg.wolStableSec = (uint16_t)i;
    if (jsonGetInt   (body, "wolBattMinPct", i))  s_cfg.wolBattMinPct = (uint8_t)i;

    if (jsonGetString(body, "tgToken", s))        s_cfg.tgToken = s;
    if (jsonGetString(body, "tgChatId", s))       s_cfg.tgChatId = s;
    if (jsonGetInt   (body, "tgEnable", i))       s_cfg.tgEnable = (i != 0);
    if (jsonGetInt   (body, "tgPeriod", i))       s_cfg.tgPeriod = (uint16_t)i;
    if (jsonGetInt   (body, "tgEvents", i))       s_cfg.tgEvents = (uint16_t)i;

    if (jsonGetString(body, "emSmtpHost", s))     s_cfg.emSmtpHost = s;
    if (jsonGetInt   (body, "emSmtpPort", i))     s_cfg.emSmtpPort = (uint16_t)i;
    if (jsonGetString(body, "emUser", s))         s_cfg.emUser = s;
    if (jsonGetString(body, "emPass", s))         s_cfg.emPass = s;
    if (jsonGetString(body, "emFrom", s))         s_cfg.emFrom = s;
    if (jsonGetString(body, "emTo", s))           s_cfg.emTo = s;
    if (jsonGetInt   (body, "emEnable", i))       s_cfg.emEnable = (i != 0);
    if (jsonGetInt   (body, "emPeriod", i))       s_cfg.emPeriod = (uint16_t)i;
    if (jsonGetInt   (body, "emEvents", i))       s_cfg.emEvents = (uint16_t)i;

    if (jsonGetString(body, "uiLang", s))         s_cfg.uiLang = s;
    if (jsonGetInt   (body, "uiEventMask", i))    s_cfg.uiEventMask = (uint16_t)i;

    storageSave(s_cfg);
    telegramSetConfig(s_cfg);
    emailSetConfig(s_cfg);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiUiConfig() {
    if (!s_server.hasArg("plain")) { s_server.send(400, "no body"); return; }
    String body = s_server.arg("plain");
    String s; int i;

    if (jsonGetString(body, "uiLang", s))      s_cfg.uiLang = s;
    if (jsonGetInt   (body, "uiEventMask", i)) s_cfg.uiEventMask = (uint16_t)i;

    storageSave(s_cfg);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiLogConfig() {
    if (!s_server.hasArg("plain")) { s_server.send(400, "text/plain", "no body"); return; }
    String body = s_server.arg("plain");
    int i;
    if (jsonGetInt(body, "logVoltPeriodSec", i)) {
        if (i < 0)    i = 0;
        if (i > 3600) i = 3600;
        s_cfg.logVoltPeriodSec = (uint16_t)i;
    }
    storageSave(s_cfg);
    DbgInfo("Log config applied: period=%u s",
            (unsigned)s_cfg.logVoltPeriodSec);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiConfigResetPane() {
    if (!s_server.hasArg("plain")) { s_server.send(400, "no body"); return; }
    String body = s_server.arg("plain");
    String pane;
    if (!jsonGetString(body, "pane", pane)) { s_server.send(400, "no pane"); return; }

    Config def;
    storageFillDefaults(def);

    if (pane == "net") {
        s_cfg.wifiSsid = def.wifiSsid; s_cfg.wifiPass = def.wifiPass;
        s_cfg.hostname = def.hostname; s_cfg.useDhcp = def.useDhcp;
        s_cfg.ip = def.ip; s_cfg.mask = def.mask; s_cfg.gw = def.gw; s_cfg.dns = def.dns;
        s_cfg.ntpUse = def.ntpUse;
        s_cfg.ntpServer1 = def.ntpServer1;
        s_cfg.ntpServer2 = def.ntpServer2;
        s_cfg.ntpTz = def.ntpTz;
        s_cfg.tempHighC = def.tempHighC;
        s_cfg.battFullV = def.battFullV; s_cfg.battEmptyV = def.battEmptyV;
        s_cfg.battWarnV = def.battWarnV; s_cfg.battCapacityWh = def.battCapacityWh;
        s_cfg.loadCurrentA = def.loadCurrentA; s_cfg.inputLostV = def.inputLostV;
        s_cfg.hystV = def.hystV; s_cfg.adcPollMs = def.adcPollMs;
    } else if (pane == "snmp") {
        s_cfg.qnapIp = def.qnapIp; s_cfg.qnapMac = def.qnapMac;
        s_cfg.snmpCommunity = def.snmpCommunity; s_cfg.snmpPort = def.snmpPort;
        s_cfg.useInform = def.useInform; s_cfg.enterpriseOid = def.enterpriseOid;
    } else if (pane == "cal") {
        s_cfg.battOffsetMv = def.battOffsetMv; s_cfg.battGain = def.battGain;
        s_cfg.inputOffsetMv = def.inputOffsetMv; s_cfg.inputGain = def.inputGain;
    } else if (pane == "wol") {
        s_cfg.wolEnable = def.wolEnable; s_cfg.wolOutageMinSec = def.wolOutageMinSec;
        s_cfg.wolStableSec = def.wolStableSec; s_cfg.wolBattMinPct = def.wolBattMinPct;
    } else if (pane == "tg") {
        s_cfg.tgToken  = def.tgToken;
        s_cfg.tgChatId = def.tgChatId;
        s_cfg.tgEnable = def.tgEnable;
        s_cfg.tgPeriod = def.tgPeriod;
        s_cfg.tgEvents = def.tgEvents;
    } else if (pane == "em") {
        s_cfg.emSmtpHost = def.emSmtpHost;
        s_cfg.emSmtpPort = def.emSmtpPort;
        s_cfg.emUser     = def.emUser;
        s_cfg.emPass     = def.emPass;
        s_cfg.emFrom     = def.emFrom;
        s_cfg.emTo       = def.emTo;
        s_cfg.emEnable   = def.emEnable;
        s_cfg.emPeriod   = def.emPeriod;
        s_cfg.emEvents   = def.emEvents;
    } else {
        s_server.send(400, "bad pane"); return;
    }
    storageSave(s_cfg);
    telegramSetConfig(s_cfg);
    emailSetConfig(s_cfg);
    DbgInfo("Web: config reset pane '%s'", pane.c_str());
    loggerEvent("Config reset pane '%s' via web", pane.c_str());
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiReboot() {
    s_server.send(200, "application/json", "{\"rebooting\":true}");
    delay(200);
    sysScheduleRestart(500);
}

static void handleApiNtpSync() {
    ntpForceSync();
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiCalibrate() {
    if (!s_server.hasArg("plain")) { s_server.send(400); return; }
    String body = s_server.arg("plain");
    String channel;
    float  real_v = 0.0f;
    if (!jsonGetString(body, "channel", channel) ||
        !jsonGetFloat (body, "real_v", real_v) || real_v <= 0.0f) {
        s_server.send(400, "bad params"); return;
    }
    float gain_new = 1.0f;
    if (channel == "batt") {
        uint16_t rawMv = adcReadRawMv(PIN_ADC_BATTERY);
        float base_v = ((float)rawMv / 1000.0f) / DIVIDER_K;
        if (base_v < 0.01f) { s_server.send(400, "no signal"); return; }
        gain_new = real_v / base_v;
        s_cfg.battOffsetMv = 0.0f; s_cfg.battGain = gain_new;
    } else if (channel == "input") {
        uint16_t rawMv = adcReadRawMv(PIN_ADC_INPUT);
        float base_v = ((float)rawMv / 1000.0f) / DIVIDER_K;
        if (base_v < 0.01f) { s_server.send(400, "no signal"); return; }
        gain_new = real_v / base_v;
        s_cfg.inputOffsetMv = 0.0f; s_cfg.inputGain = gain_new;
    } else { s_server.send(400, "bad channel"); return; }
    String j = "{\"ok\":true,\"gain\":" + String(gain_new, 6) + "}";
    s_server.send(200, "application/json", j);
}

static void handleApiCalibrateReset() {
    if (!s_server.hasArg("plain")) { s_server.send(400); return; }
    String body = s_server.arg("plain");
    String channel;
    if (!jsonGetString(body, "channel", channel)) { s_server.send(400); return; }
    if (channel == "batt") { s_cfg.battOffsetMv = 0.0f; s_cfg.battGain = 1.0f; }
    else if (channel == "input") { s_cfg.inputOffsetMv = 0.0f; s_cfg.inputGain = 1.0f; }
    else { s_server.send(400); return; }
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiWolManual() {
    bool ok = wolSendManual(s_cfg);
    s_server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleApiTelegramTest() {
    bool ok = telegramSendTest();
    s_server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleApiEmailTest() {
    bool ok = emailSendTest();
    s_server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleApiLog() {
    bool hasOld = LittleFS.exists(LOG_FILE_OLD);
    bool hasNew = LittleFS.exists(LOG_FILE);

    if (!hasOld && !hasNew) {
        s_server.send(404, "text/plain", "no log");
        return;
    }

    File fOld;
    File fNew;
    if (hasOld) fOld = LittleFS.open(LOG_FILE_OLD, "r");
    if (hasNew) fNew = LittleFS.open(LOG_FILE, "r");

    if (!fOld && !fNew) {
        s_server.send(404, "text/plain", "no log");
        return;
    }

    WiFiClient c = s_server.client();
    c.println("HTTP/1.1 200 OK");
    c.println("Content-Type: text/plain; charset=utf-8");
    c.println("Connection: close");
    c.println("Access-Control-Allow-Origin: *");
    c.println();

    uint8_t buf[512];
    if (fOld) {
        while (fOld.available()) {
            size_t n = fOld.read(buf, sizeof(buf));
            if (n == 0) break;
            c.write(buf, n);
        }
        fOld.close();
    }
    if (fNew) {
        while (fNew.available()) {
            size_t n = fNew.read(buf, sizeof(buf));
            if (n == 0) break;
            c.write(buf, n);
        }
        fNew.close();
    }
    c.flush();
}

static void handleApiLogDownload() {
    bool hasOld = LittleFS.exists(LOG_FILE_OLD);
    bool hasNew = LittleFS.exists(LOG_FILE);

    if (!hasOld && !hasNew) {
        s_server.send(404, "text/plain", "no log");
        return;
    }

    File fOld;
    File fNew;
    if (hasOld) fOld = LittleFS.open(LOG_FILE_OLD, "r");
    if (hasNew) fNew = LittleFS.open(LOG_FILE, "r");

    if (!fOld && !fNew) {
        s_server.send(404, "text/plain", "no log");
        return;
    }

    WiFiClient c = s_server.client();
    c.println("HTTP/1.1 200 OK");
    c.println("Content-Type: text/plain; charset=utf-8");
    c.println("Content-Disposition: attachment; filename=\"snmp_ups_log.txt\"");
    c.println("Connection: close");
    c.println("Access-Control-Allow-Origin: *");
    c.println();

    uint8_t buf[512];
    if (fOld) {
        while (fOld.available()) {
            size_t n = fOld.read(buf, sizeof(buf));
            if (n == 0) break;
            c.write(buf, n);
        }
        fOld.close();
    }
    if (fNew) {
        while (fNew.available()) {
            size_t n = fNew.read(buf, sizeof(buf));
            if (n == 0) break;
            c.write(buf, n);
        }
        fNew.close();
    }
    c.flush();
}

static void handleApiLogClear() {
    loggerClear();
    s_server.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
    s_server.send(404, "text/plain; charset=utf-8", "Not found");
}

void webInit(const Config &cfg) {
    s_cfg = cfg;

    s_server.on("/",                    HTTP_GET,  handleRoot);
    s_server.on("/favicon.ico",         HTTP_GET,  handleFavicon);
    s_server.on("/api/status",          HTTP_GET,  handleApiStatus);
    s_server.on("/api/config",          HTTP_GET,  handleApiConfigGet);
    s_server.on("/api/config",          HTTP_POST, handleApiConfigPost);
    s_server.on("/api/ui_config",       HTTP_POST, handleApiUiConfig);
    s_server.on("/api/log_config",      HTTP_POST, handleApiLogConfig);
    s_server.on("/api/config_reset_pane", HTTP_POST, handleApiConfigResetPane);
    s_server.on("/api/reboot",          HTTP_GET,  handleApiReboot);
    s_server.on("/api/ntp_sync",        HTTP_POST, handleApiNtpSync);
    s_server.on("/api/calibrate",       HTTP_POST, handleApiCalibrate);
    s_server.on("/api/calibrate_reset", HTTP_POST, handleApiCalibrateReset);
    s_server.on("/api/wol_manual",      HTTP_POST, handleApiWolManual);
    s_server.on("/api/telegram_test",   HTTP_POST, handleApiTelegramTest);
    s_server.on("/api/email_test",      HTTP_POST, handleApiEmailTest);
    s_server.on("/api/log",             HTTP_GET,  handleApiLog);
    s_server.on("/api/log_download",    HTTP_GET,  handleApiLogDownload);
    s_server.on("/api/log_clear",       HTTP_POST, handleApiLogClear);
    s_server.onNotFound(handleNotFound);

    ElegantOTA.begin(&s_server);
    ElegantOTA.setAuth(OTA_USERNAME, OTA_PASSWORD);
    DbgInfo("OTA: ElegantOTA ready at %s", OTA_PATH);

    s_server.begin();

    DbgInfo("Web: server started on port %u", (unsigned)WEB_PORT);
    DbgInfo("Web: open http://%s", wifiGetIP().c_str());
}

void webLoop() {
    s_server.handleClient();
    ElegantOTA.loop();
}

void webUpdateStatus(const UpsStatus &st) {
    s_lastStatus = st;
}

void webUpdateTemp(float tempC) {
    s_lastTempC = tempC;
}

void webSetConfig(const Config &cfg) {
    s_cfg = cfg;
}
