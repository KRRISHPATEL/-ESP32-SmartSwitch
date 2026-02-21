#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <FastLED.h>

RTC_DS3231 rtc;
WebServer server(80);

#define RELAY1 26
#define RELAY2 27
#define SW1 32
#define SW2 33
#define LED_PIN 25
#define SW3 34
#define NUM_LEDS 5

CRGB leds[NUM_LEDS];

bool relay1State = false;
bool relay2State = false;
int restoreMode1 = 2;
int restoreMode2 = 2;
bool ledEnabled = true;
int ledBrightness = 128;
int ledRed = 255, ledGreen = 255, ledBlue = 255;
int ledEffect = 0;
int effectSpeed = 50;
String relay1Name = "Relay 1";
String relay2Name = "Relay 2";

struct Timer { int hour; int minute; bool action; bool enable; };
Timer r1Timer[5];
Timer r2Timer[5];
struct Alarm { int day; int month; int year; int hour; int minute; bool action; bool enable; bool triggered; };
Alarm r1Alarm[5];
Alarm r2Alarm[5];

const char* ssid = "ESP32_SWITCH";
const char* pass = "12345678";

#define EEPROM_SIZE 512
#define ADDR_RELAY1_STATE   0
#define ADDR_RELAY2_STATE   1
#define ADDR_RESTORE_MODE1  2
#define ADDR_RESTORE_MODE2  3
#define ADDR_RELAY1_NAME    4
#define ADDR_RELAY2_NAME    24
#define ADDR_LED_ENABLED    46
#define ADDR_LED_BRIGHTNESS 47
#define ADDR_LED_R          48
#define ADDR_LED_G          49
#define ADDR_LED_B          50
#define ADDR_LED_EFFECT     51
#define ADDR_LED_SPEED      52
#define ADDR_R1_TIMERS      60
#define ADDR_R2_TIMERS      160
#define ADDR_R1_ALARMS      260
#define ADDR_R2_ALARMS      380

void saveState()       { EEPROM.write(ADDR_RELAY1_STATE,relay1State); EEPROM.write(ADDR_RELAY2_STATE,relay2State); EEPROM.commit(); }
void saveRestoreMode() { EEPROM.write(ADDR_RESTORE_MODE1,restoreMode1); EEPROM.write(ADDR_RESTORE_MODE2,restoreMode2); EEPROM.commit(); }
void saveLEDSettings() { EEPROM.write(ADDR_LED_ENABLED,ledEnabled); EEPROM.write(ADDR_LED_BRIGHTNESS,ledBrightness); EEPROM.write(ADDR_LED_R,ledRed); EEPROM.write(ADDR_LED_G,ledGreen); EEPROM.write(ADDR_LED_B,ledBlue); EEPROM.write(ADDR_LED_EFFECT,ledEffect); EEPROM.write(ADDR_LED_SPEED,effectSpeed/10); EEPROM.commit(); }
void loadLEDSettings() { ledEnabled=EEPROM.read(ADDR_LED_ENABLED); ledBrightness=EEPROM.read(ADDR_LED_BRIGHTNESS); ledRed=EEPROM.read(ADDR_LED_R); ledGreen=EEPROM.read(ADDR_LED_G); ledBlue=EEPROM.read(ADDR_LED_B); ledEffect=EEPROM.read(ADDR_LED_EFFECT); effectSpeed=EEPROM.read(ADDR_LED_SPEED)*10; if(ledBrightness>255||ledBrightness<1)ledBrightness=128; if(ledEffect>14)ledEffect=0; if(effectSpeed<10||effectSpeed>2000)effectSpeed=50; }
void saveRelayNames() { for(int i=0;i<20;i++){EEPROM.write(ADDR_RELAY1_NAME+i,i<(int)relay1Name.length()?relay1Name[i]:0); EEPROM.write(ADDR_RELAY2_NAME+i,i<(int)relay2Name.length()?relay2Name[i]:0);} EEPROM.commit(); }
void loadRelayNames() { relay1Name=""; relay2Name=""; for(int i=0;i<20;i++){char c1=EEPROM.read(ADDR_RELAY1_NAME+i),c2=EEPROM.read(ADDR_RELAY2_NAME+i); if(c1&&c1!=255)relay1Name+=c1; else break; if(c2&&c2!=255)relay2Name+=c2;} if(relay1Name=="")relay1Name="Relay 1"; if(relay2Name=="")relay2Name="Relay 2"; }
void loadRestoreMode() { restoreMode1=EEPROM.read(ADDR_RESTORE_MODE1); restoreMode2=EEPROM.read(ADDR_RESTORE_MODE2); if(restoreMode1>2)restoreMode1=2; if(restoreMode2>2)restoreMode2=2; }
void loadState() { if(restoreMode1==2)relay1State=EEPROM.read(ADDR_RELAY1_STATE); else relay1State=(restoreMode1==1); if(restoreMode2==2)relay2State=EEPROM.read(ADDR_RELAY2_STATE); else relay2State=(restoreMode2==1); applyRelay(); }
void saveTimers() { EEPROM.put(ADDR_R1_TIMERS,r1Timer); EEPROM.put(ADDR_R2_TIMERS,r2Timer); EEPROM.commit(); }
void loadTimers() { EEPROM.get(ADDR_R1_TIMERS,r1Timer); EEPROM.get(ADDR_R2_TIMERS,r2Timer); }
void saveAlarms() { EEPROM.put(ADDR_R1_ALARMS,r1Alarm); EEPROM.put(ADDR_R2_ALARMS,r2Alarm); EEPROM.commit(); }
void loadAlarms() { EEPROM.get(ADDR_R1_ALARMS,r1Alarm); EEPROM.get(ADDR_R2_ALARMS,r2Alarm); }
void applyRelay() { digitalWrite(RELAY1,relay1State?LOW:HIGH); digitalWrite(RELAY2,relay2State?LOW:HIGH); }

String urlDecode(String s) { String d=""; for(int i=0;i<(int)s.length();i++){char c=s.charAt(i); if(c=='+')d+=' '; else if(c=='%'&&i+2<(int)s.length()){char h[3]={s.charAt(i+1),s.charAt(i+2),0};d+=(char)strtol(h,NULL,16);i+=2;}else d+=c;} return d; }

unsigned long lastEffectUpdate=0;
int effectStep=0;

String effectName(int e){switch(e){case 0:return "Solid";case 1:return "Blink";case 2:return "Breathe";case 3:return "Rainbow";case 4:return "Wave";case 5:return "Sparkle";case 6:return "Fire";case 7:return "Ice";case 8:return "Wipe";case 9:return "Chase";case 10:return "Twinkle";case 11:return "Comet";case 12:return "Fade";case 13:return "Strobe";case 14:return "Bounce";default:return "Solid";}}

void applyLEDs() {
  if(!ledEnabled){fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();return;}
  FastLED.setBrightness(ledBrightness);
  unsigned long now=millis();
  switch(ledEffect){
    case 0:fill_solid(leds,NUM_LEDS,CRGB(ledRed,ledGreen,ledBlue));FastLED.show();break;
    case 1:if(now-lastEffectUpdate>(unsigned long)effectSpeed*5){lastEffectUpdate=now;effectStep=!effectStep;fill_solid(leds,NUM_LEDS,effectStep?CRGB(ledRed,ledGreen,ledBlue):CRGB::Black);FastLED.show();}break;
    case 2:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;float v=(sin(effectStep*0.05)+1.0)/2.0;fill_solid(leds,NUM_LEDS,CRGB(ledRed*v,ledGreen*v,ledBlue*v));FastLED.show();effectStep++;}break;
    case 3:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;fill_rainbow(leds,NUM_LEDS,effectStep,255/NUM_LEDS);FastLED.show();effectStep+=3;}break;
    case 4:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;for(int i=0;i<NUM_LEDS;i++)leds[i]=CHSV((effectStep+(i*255/NUM_LEDS))%256,255,255);FastLED.show();effectStep+=2;}break;
    case 5:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;fill_solid(leds,NUM_LEDS,CRGB::Black);leds[random(NUM_LEDS)]=CRGB(ledRed,ledGreen,ledBlue);FastLED.show();}break;
    case 6:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;for(int i=0;i<NUM_LEDS;i++)leds[i]=CRGB(random(150,255),random(20,80),0);FastLED.show();}break;
    case 7:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;for(int i=0;i<NUM_LEDS;i++)leds[i]=CRGB(0,random(50,100),random(150,255));FastLED.show();}break;
    case 8:if(now-lastEffectUpdate>(unsigned long)effectSpeed*3){lastEffectUpdate=now;if(effectStep<NUM_LEDS)leds[effectStep]=CRGB(ledRed,ledGreen,ledBlue);else if(effectStep<NUM_LEDS*2)leds[effectStep-NUM_LEDS]=CRGB::Black;else effectStep=-1;FastLED.show();effectStep++;}break;
    case 9:if(now-lastEffectUpdate>(unsigned long)effectSpeed*2){lastEffectUpdate=now;fill_solid(leds,NUM_LEDS,CRGB::Black);leds[effectStep%NUM_LEDS]=CRGB(ledRed,ledGreen,ledBlue);FastLED.show();effectStep++;}break;
    case 10:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;for(int i=0;i<NUM_LEDS;i++){if(random(3)==0)leds[i]=CRGB(ledRed,ledGreen,ledBlue);else leds[i].fadeToBlackBy(50);}FastLED.show();}break;
    case 11:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;for(int i=0;i<NUM_LEDS;i++)leds[i].fadeToBlackBy(80);leds[effectStep%NUM_LEDS]=CRGB(ledRed,ledGreen,ledBlue);FastLED.show();effectStep++;}break;
    case 12:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;fill_solid(leds,NUM_LEDS,CHSV(effectStep%256,255,255));FastLED.show();effectStep++;}break;
    case 13:if(now-lastEffectUpdate>(unsigned long)effectSpeed){lastEffectUpdate=now;effectStep=!effectStep;fill_solid(leds,NUM_LEDS,effectStep?CRGB(ledRed,ledGreen,ledBlue):CRGB::Black);FastLED.show();}break;
    case 14:if(now-lastEffectUpdate>(unsigned long)effectSpeed*2){lastEffectUpdate=now;static bool dir=true;fill_solid(leds,NUM_LEDS,CRGB::Black);leds[effectStep]=CRGB(ledRed,ledGreen,ledBlue);FastLED.show();if(dir){effectStep++;if(effectStep>=NUM_LEDS-1)dir=false;}else{effectStep--;if(effectStep<=0)dir=true;}}break;
  }
}

/* ==========================================================
   SHARED CSS + JS (loaded once via <link> trick - embedded)
   ========================================================== */
String getCommonHead(String title) {
  return R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<title>)rawliteral" + title + R"rawliteral(</title>
<link href="https://fonts.googleapis.com/css2?family=Nunito:wght@300;400;500;600;700;800&display=swap" rel="stylesheet">
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
html,body{width:100%;height:100%;overflow-x:hidden;}
body{font-family:'Nunito',sans-serif;background:#0b0e14;color:#fff;min-height:100vh;}
a{text-decoration:none;color:inherit;}
</style>
</head>
<body>
)rawliteral";
}

/* ==========================================================
   HOME PAGE  (2 relay cards, Realme-style)
   ========================================================== */
void handleRoot() {
  DateTime now = rtc.now();
  char timeStr[20];
  sprintf(timeStr,"%02d:%02d",now.hour(),now.minute());

  String page = getCommonHead("SmartSwitch");
  page += R"rawliteral(
<style>
/* ---- BACKGROUND ---- */
body { background: #0b0e14; }
.bg-glow {
  position:fixed;inset:0;pointer-events:none;z-index:0;
  background: radial-gradient(ellipse 60% 40% at 50% 0%, rgba(255,255,255,0.04) 0%, transparent 70%);
}

/* ---- HEADER ---- */
.hdr {
  position:fixed;top:0;left:0;right:0;z-index:100;
  display:flex;align-items:center;justify-content:space-between;
  padding:18px 20px 14px;
  background: linear-gradient(to bottom, rgba(11,14,20,0.98) 60%, transparent);
}
.hdr-title { font-size:20px;font-weight:700;color:#fff;letter-spacing:-0.3px; }
.hdr-clock {
  font-size:13px;font-weight:600;color:rgba(255,255,255,0.4);
  background:rgba(255,255,255,0.06);padding:6px 12px;border-radius:50px;
  border:1px solid rgba(255,255,255,0.07);
}

/* ---- SCROLLABLE CONTENT ---- */
.content { padding: 80px 16px 120px; position:relative;z-index:1; }

/* ---- RELAY CARD — Realme style ---- */
.plug-card {
  background: #131720;
  border: 1px solid rgba(255,255,255,0.07);
  border-radius: 28px;
  padding: 0;
  margin-bottom: 16px;
  overflow: hidden;
  position: relative;
  cursor: pointer;
  transition: transform 0.18s cubic-bezier(0.34,1.56,0.64,1);
  display: block;
}
.plug-card:active { transform: scale(0.97); }
.plug-card:hover  { transform: translateY(-2px); }

/* Glow halo when ON */
.plug-card.on::before {
  content:'';
  position:absolute;top:0;left:50%;transform:translateX(-50%);
  width:300px;height:300px;border-radius:50%;
  background:radial-gradient(circle, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.03) 40%, transparent 70%);
  pointer-events:none;
  animation: haloBreath 3s ease-in-out infinite;
}
@keyframes haloBreath {
  0%,100%{opacity:0.8;transform:translateX(-50%) scale(1);}
  50%     {opacity:1;  transform:translateX(-50%) scale(1.08);}
}

/* shimmer top border on ON */
.plug-card.on::after {
  content:'';
  position:absolute;top:0;left:0;right:0;height:2px;
  background: linear-gradient(90deg,transparent,rgba(255,255,255,0.6),transparent);
  background-size:200% 100%;
  animation: shimTop 2.5s linear infinite;
}
@keyframes shimTop {
  0%{background-position:200% 0;} 100%{background-position:-200% 0;}
}

.card-inner {
  display:flex;align-items:center;gap:0;
  padding: 22px 24px;
}

/* ---- PLUG SVG AREA ---- */
.plug-wrap {
  width:90px;height:90px;flex-shrink:0;
  display:flex;align-items:center;justify-content:center;
  position:relative;
}
/* Outer glow ring */
.plug-ring {
  position:absolute;inset:-12px;border-radius:50%;
  transition:all 0.5s cubic-bezier(0.4,0,0.2,1);
}
.plug-card.on  .plug-ring { background:radial-gradient(circle,rgba(255,255,255,0.18),rgba(255,255,255,0.04) 55%,transparent 75%); animation:ringPulse 2s ease-in-out infinite; }
.plug-card.off .plug-ring { background:transparent; }
@keyframes ringPulse {
  0%,100%{transform:scale(1);opacity:0.8;}
  50%    {transform:scale(1.1);opacity:1;}
}

.plug-svg { transition:all 0.4s cubic-bezier(0.4,0,0.2,1); }

/* OFF state: dark grey socket */
.plug-card.off .socket-body { fill:#252b38; }
.plug-card.off .socket-pin  { fill:#1a1f2a; }
.plug-card.off .socket-led  { fill:#1e2430; }

/* ON state: bright white with glow */
.plug-card.on .socket-body {
  fill:#f0f0f0;
  filter:drop-shadow(0 0 24px rgba(255,255,255,0.9)) drop-shadow(0 0 48px rgba(255,255,255,0.5));
}
.plug-card.on  .socket-pin  { fill:#b0b8c8; }
.plug-card.on  .socket-led  { fill:#4ade80; filter:drop-shadow(0 0 4px #4ade80); }
.plug-card.on  .plug-svg { animation:plugFloat 3s ease-in-out infinite; }
@keyframes plugFloat {
  0%,100%{transform:translateY(0);}
  50%    {transform:translateY(-4px);}
}

/* ---- CARD INFO ---- */
.card-info { flex:1;padding-left:20px; }
.card-name {
  font-size:18px;font-weight:700;color:#fff;
  margin-bottom:6px;letter-spacing:-0.2px;
}
.card-status {
  display:inline-flex;align-items:center;gap:6px;
  padding:5px 14px;border-radius:50px;
  font-size:12px;font-weight:700;letter-spacing:0.3px;
}
.status-on  { background:rgba(74,222,128,0.15);color:#4ade80;border:1px solid rgba(74,222,128,0.25); }
.status-off { background:rgba(255,255,255,0.06);color:rgba(255,255,255,0.35);border:1px solid rgba(255,255,255,0.08); }
.status-dot {
  width:7px;height:7px;border-radius:50%;
  transition:all 0.3s;
}
.status-on  .status-dot { background:#4ade80;box-shadow:0 0 6px #4ade80;animation:dotBlink 2s ease-in-out infinite; }
.status-off .status-dot { background:rgba(255,255,255,0.2); }
@keyframes dotBlink {
  0%,100%{opacity:1;} 50%{opacity:0.4;}
}

.card-detail-btn {
  display:block;width:100%;
  padding:14px 24px;
  border-top:1px solid rgba(255,255,255,0.05);
  font-size:13px;font-weight:600;color:rgba(255,255,255,0.35);
  text-align:center;letter-spacing:0.2px;
  transition:all 0.2s;background:rgba(255,255,255,0.02);
}
.card-detail-btn:hover { color:rgba(255,255,255,0.6);background:rgba(255,255,255,0.04); }
.card-detail-btn .arr { display:inline-block;transition:transform 0.2s; }
.card-detail-btn:hover .arr { transform:translateX(3px); }

/* ---- QUICK ACTIONS ROW ---- */
.qrow { display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:4px; }
.qbtn {
  padding:16px;border-radius:20px;
  display:flex;flex-direction:column;align-items:center;gap:8px;
  font-size:13px;font-weight:700;cursor:pointer;
  border:1px solid;transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);
}
.qbtn:hover  { transform:translateY(-2px) scale(1.02); }
.qbtn:active { transform:scale(0.95); }
.qbtn-on  { background:rgba(74,222,128,0.1);color:#4ade80;border-color:rgba(74,222,128,0.2); }
.qbtn-off { background:rgba(255,69,58,0.08);color:#ff453a;border-color:rgba(255,69,58,0.18); }
.qbtn-icon { font-size:22px; }

/* ---- LED STRIP MINI ---- */
.led-mini-card {
  background:#131720;border:1px solid rgba(255,255,255,0.07);
  border-radius:24px;padding:20px;margin-top:4px;
  position:relative;overflow:hidden;
}
.led-mini-card::before {
  content:'';position:absolute;top:0;left:0;right:0;height:2px;
  background:linear-gradient(90deg,#ff2d78,#bf5af2,#0a84ff,#30d158,#ffd60a,#ff2d78);
  background-size:200% 100%;animation:rainbowBar 3s linear infinite;
}
@keyframes rainbowBar{0%{background-position:0% 50%;}100%{background-position:200% 50%;}}
.led-mini-row { display:flex;align-items:center;justify-content:space-between;margin-bottom:14px; }
.led-mini-title { font-size:14px;font-weight:700;color:rgba(255,255,255,0.7); }
.led-bars { display:flex;gap:6px;align-items:flex-end;height:36px; }
.led-b { border-radius:4px;transition:all 0.4s; }
.led-b:nth-child(1){width:8px;height:20px;animation:barAnim 1.8s 0.0s ease-in-out infinite;}
.led-b:nth-child(2){width:8px;height:30px;animation:barAnim 1.8s 0.2s ease-in-out infinite;}
.led-b:nth-child(3){width:8px;height:36px;animation:barAnim 1.8s 0.4s ease-in-out infinite;}
.led-b:nth-child(4){width:8px;height:30px;animation:barAnim 1.8s 0.6s ease-in-out infinite;}
.led-b:nth-child(5){width:8px;height:20px;animation:barAnim 1.8s 0.8s ease-in-out infinite;}
@keyframes barAnim{0%,100%{transform:scaleY(1);opacity:1;}50%{transform:scaleY(0.5);opacity:0.5;}}
.led-mini-actions { display:flex;gap:8px; }
.led-pill {
  padding:8px 18px;border-radius:50px;
  font-size:12px;font-weight:700;
  border:1px solid;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);
  cursor:pointer;
}
.led-pill:hover  { transform:scale(1.05); }
.led-pill:active { transform:scale(0.94); }
.led-pill-toggle { background:rgba(191,90,242,0.15);color:#bf5af2;border-color:rgba(191,90,242,0.3); }
.led-pill-config { background:rgba(255,255,255,0.06);color:rgba(255,255,255,0.5);border-color:rgba(255,255,255,0.1); }

/* ---- BOTTOM NAV ---- */
.bottom-nav {
  position:fixed;bottom:0;left:0;right:0;z-index:100;
  background:rgba(11,14,20,0.97);
  border-top:1px solid rgba(255,255,255,0.07);
  padding:12px 0 20px;
  display:flex;align-items:center;justify-content:space-around;
  backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);
}
.nav-item {
  display:flex;flex-direction:column;align-items:center;gap:5px;
  color:rgba(255,255,255,0.3);font-size:11px;font-weight:700;
  cursor:pointer;transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);
  text-decoration:none;letter-spacing:0.3px;
  padding:6px 16px;border-radius:14px;
}
.nav-item:hover,.nav-item.active { color:#fff;background:rgba(255,255,255,0.07); }
.nav-item:active { transform:scale(0.9); }
.nav-icon { font-size:22px;transition:transform 0.2s; }
.nav-item:hover .nav-icon { transform:scale(1.15); }
.nav-item.active .nav-icon { animation:navPop 0.3s cubic-bezier(0.34,1.56,0.64,1); }
@keyframes navPop{0%{transform:scale(0.8);}100%{transform:scale(1);}}

/* ---- PAGE ANIMATIONS ---- */
.content { animation:fadeUp 0.45s cubic-bezier(0.34,1.4,0.64,1) both; }
@keyframes fadeUp{from{opacity:0;transform:translateY(22px);}to{opacity:1;transform:translateY(0);}}
.plug-card:nth-child(1){animation:cardSlide 0.4s 0.05s both cubic-bezier(0.34,1.4,0.64,1);}
.plug-card:nth-child(2){animation:cardSlide 0.4s 0.12s both cubic-bezier(0.34,1.4,0.64,1);}
@keyframes cardSlide{from{opacity:0;transform:translateY(30px);}to{opacity:1;transform:translateY(0);}}
</style>

<div class="bg-glow"></div>

<div class="hdr">
  <div class="hdr-title">My Devices</div>
  <div class="hdr-clock" id="lc">)rawliteral";

  page += String(timeStr);
  page += R"rawliteral(</div>
</div>

<div class="content">
)rawliteral";

  /* ---- RELAY 1 CARD ---- */
  String r1Class = relay1State ? "on" : "off";
  page += "<a href='/relay/1' class='plug-card " + r1Class + "'>";
  page += "<div class='card-inner'>";
  page += "<div class='plug-wrap'>";
  page += "<div class='plug-ring'></div>";
  // Indian 3-pin socket SVG
  page += R"rawliteral(<svg class="plug-svg" width="80" height="80" viewBox="0 0 100 100" fill="none" xmlns="http://www.w3.org/2000/svg">
  <rect class="socket-body" x="8" y="8" width="84" height="84" rx="18"/>
  <circle class="socket-pin" cx="50" cy="32" r="9"/>
  <circle class="socket-pin" cx="32" cy="62" r="8"/>
  <circle class="socket-pin" cx="68" cy="62" r="8"/>
  <circle class="socket-led" cx="50" cy="16" r="4"/>
</svg>)rawliteral";
  page += "</div>";
  page += "<div class='card-info'>";
  page += "<div class='card-name'>" + relay1Name + "</div>";
  page += "<span class='card-status " + String(relay1State?"status-on":"status-off") + "'>";
  page += "<span class='status-dot'></span>";
  page += String(relay1State ? "Turned On" : "Turned Off");
  page += "</span></div></div>";
  page += "<div class='card-detail-btn'>Manage <span class='arr'>&#8594;</span></div>";
  page += "</a>";

  /* ---- RELAY 2 CARD ---- */
  String r2Class = relay2State ? "on" : "off";
  page += "<a href='/relay/2' class='plug-card " + r2Class + "'>";
  page += "<div class='card-inner'>";
  page += "<div class='plug-wrap'>";
  page += "<div class='plug-ring'></div>";
  page += R"rawliteral(<svg class="plug-svg" width="80" height="80" viewBox="0 0 100 100" fill="none" xmlns="http://www.w3.org/2000/svg">
  <rect class="socket-body" x="8" y="8" width="84" height="84" rx="18"/>
  <circle class="socket-pin" cx="50" cy="32" r="9"/>
  <circle class="socket-pin" cx="32" cy="62" r="8"/>
  <circle class="socket-pin" cx="68" cy="62" r="8"/>
  <circle class="socket-led" cx="50" cy="16" r="4"/>
</svg>)rawliteral";
  page += "</div>";
  page += "<div class='card-info'>";
  page += "<div class='card-name'>" + relay2Name + "</div>";
  page += "<span class='card-status " + String(relay2State?"status-on":"status-off") + "'>";
  page += "<span class='status-dot'></span>";
  page += String(relay2State ? "Turned On" : "Turned Off");
  page += "</span></div></div>";
  page += "<div class='card-detail-btn'>Manage <span class='arr'>&#8594;</span></div>";
  page += "</a>";

  /* ---- QUICK ACTIONS ---- */
  page += R"rawliteral(
  <div class="qrow">
    <a href="/allon"  class="qbtn qbtn-on"><span class="qbtn-icon">&#9889;</span>All ON</a>
    <a href="/alloff" class="qbtn qbtn-off"><span class="qbtn-icon">&#9898;</span>All OFF</a>
  </div>
)rawliteral";

  /* ---- LED MINI ---- */
  char ledHex[8]; sprintf(ledHex,"#%02X%02X%02X",ledRed,ledGreen,ledBlue);
  page += "<div class='led-mini-card'>";
  page += "<div class='led-mini-row'>";
  page += "<span class='led-mini-title'>&#128161; LED Strip &nbsp;<small style='color:rgba(255,255,255,0.3);font-weight:600;'>" + effectName(ledEffect) + "</small></span>";
  page += "<div class='led-bars'>";
  for(int i=0;i<NUM_LEDS;i++){
    String col = ledEnabled ? String(ledHex) : "#1e2430";
    String glow = ledEnabled ? ";box-shadow:0 0 10px "+col : "";
    page += "<div class='led-b' style='background:"+col+glow+"'></div>";
  }
  page += "</div></div>";
  page += "<div class='led-mini-actions'>";
  page += "<a href='/led/toggle' class='led-pill led-pill-toggle'>" + String(ledEnabled?"&#9679; ON":"&#9675; OFF") + "</a>";
  page += "<a href='/led' class='led-pill led-pill-config'>&#9881; Configure</a>";
  page += "</div></div>";

  page += "</div>"; // content

  /* ---- BOTTOM NAV ---- */
  page += R"rawliteral(
<div class="bottom-nav">
  <a href="/datetime" class="nav-item">
    <span class="nav-icon">&#128336;</span>
    <span>Time</span>
  </a>
  <a href="/restore" class="nav-item">
    <span class="nav-icon">&#128268;</span>
    <span>Restore</span>
  </a>
  <a href="/" class="nav-item active">
    <span class="nav-icon">&#8962;</span>
    <span>Home</span>
  </a>
  <a href="/led" class="nav-item">
    <span class="nav-icon">&#127912;</span>
    <span>LED</span>
  </a>
  <a href="/settings" class="nav-item">
    <span class="nav-icon">&#9881;&#65039;</span>
    <span>Settings</span>
  </a>
</div>
)rawliteral";

  page += R"rawliteral(
<script>
window.onbeforeunload=function(){sessionStorage.setItem('sp',window.scrollY);};
window.onload=function(){var p=sessionStorage.getItem('sp');if(p){window.scrollTo(0,parseInt(p));sessionStorage.removeItem('sp');}updateClock();};
function updateClock(){var el=document.getElementById('lc');if(el){var n=new Date();var p=function(v){return String(v).padStart(2,'0');};el.textContent=p(n.getHours())+':'+p(n.getMinutes());}}
setInterval(updateClock,10000);
</script>
</body></html>
)rawliteral";

  server.send(200,"text/html",page);
}

/* ==========================================================
   RELAY DETAIL PAGE  (Realme Smart Plug exact layout)
   ========================================================== */
void handleRelayPage(int relay) {
  bool state   = (relay==1) ? relay1State : relay2State;
  String rName = (relay==1) ? relay1Name  : relay2Name;
  Timer* T     = (relay==1) ? r1Timer : r2Timer;

  // Count active timers
  int activeTimers=0;
  for(int i=0;i<5;i++) if(T[i].enable) activeTimers++;

  String page = getCommonHead(rName);
  page += R"rawliteral(
<style>
body {
  background:#0b0e14;
  display:flex;flex-direction:column;min-height:100vh;
}

/* ---- HEADER ---- */
.hdr {
  display:flex;align-items:center;justify-content:space-between;
  padding:18px 20px 12px;flex-shrink:0;
  position:relative;z-index:10;
}
.back-btn {
  width:40px;height:40px;border-radius:50%;
  background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);
  display:flex;align-items:center;justify-content:center;
  font-size:18px;color:rgba(255,255,255,0.7);cursor:pointer;
  transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);
  text-decoration:none;
}
.back-btn:hover  { background:rgba(255,255,255,0.12);color:#fff;transform:scale(1.08); }
.back-btn:active { transform:scale(0.9); }
.hdr-name { font-size:17px;font-weight:700;color:#fff;letter-spacing:-0.2px; }
.hdr-menu {
  width:40px;height:40px;border-radius:50%;
  background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);
  display:flex;align-items:center;justify-content:center;
  font-size:18px;color:rgba(255,255,255,0.5);cursor:pointer;
  text-decoration:none;transition:all 0.18s;
}
.hdr-menu:hover { background:rgba(255,255,255,0.12);color:#fff; }

/* ---- PLUG STAGE ---- */
.plug-stage {
  flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;
  padding:30px 20px;position:relative;min-height:340px;
}

/* Ambient glow behind plug */
.ambient-glow {
  position:absolute;width:280px;height:280px;border-radius:50%;
  transition:all 0.6s cubic-bezier(0.4,0,0.2,1);pointer-events:none;
}
)rawliteral";

  // State-dependent glow
  if(state) {
    page += R"rawliteral(
.ambient-glow {
  background:radial-gradient(circle,rgba(255,255,255,0.14) 0%,rgba(255,255,255,0.04) 40%,transparent 70%);
  animation:ambientBreath 3s ease-in-out infinite;
}
@keyframes ambientBreath{0%,100%{transform:scale(1);opacity:0.9;}50%{transform:scale(1.12);opacity:1;}}
)rawliteral";
  } else {
    page += ".ambient-glow { background:transparent; }\n";
  }

  page += R"rawliteral(
/* ---- BIG PLUG SVG ---- */
.big-plug-wrap {
  position:relative;z-index:2;
  transition:all 0.5s cubic-bezier(0.4,0,0.2,1);
}
)rawliteral";

  if(state) {
    page += R"rawliteral(
.big-plug-wrap { animation: bigPlugFloat 3.5s ease-in-out infinite; }
@keyframes bigPlugFloat{0%,100%{transform:translateY(0);}50%{transform:translateY(-8px);}}
.big-socket-body {
  fill:#f2f2f2;
  filter:drop-shadow(0 0 30px rgba(255,255,255,1)) drop-shadow(0 0 60px rgba(255,255,255,0.6)) drop-shadow(0 0 100px rgba(255,255,255,0.3));
}
.big-socket-pin  { fill:#c0c8d8; }
.big-socket-led  { fill:#4ade80; filter:drop-shadow(0 0 6px #4ade80) drop-shadow(0 0 12px rgba(74,222,128,0.8)); }
.big-socket-shine{ opacity:0.35; }
)rawliteral";
  } else {
    page += R"rawliteral(
.big-socket-body { fill:#1e2430; }
.big-socket-pin  { fill:#151b25; }
.big-socket-led  { fill:#1a2030; }
.big-socket-shine{ opacity:0; }
)rawliteral";
  }

  page += R"rawliteral(
/* ---- STATUS TEXT ---- */
.status-txt {
  margin-top:30px;z-index:2;position:relative;
  display:inline-flex;align-items:center;gap:8px;
  padding:10px 24px;border-radius:50px;
  font-size:15px;font-weight:700;letter-spacing:0.2px;
  transition:all 0.4s ease;
}
)rawliteral";

  if(state) {
    page += ".status-txt{background:rgba(74,222,128,0.12);color:#4ade80;border:1px solid rgba(74,222,128,0.2);}";
    page += ".status-dot-live{width:8px;height:8px;border-radius:50%;background:#4ade80;box-shadow:0 0 8px #4ade80;animation:liveDot 1.5s ease-in-out infinite;}";
  } else {
    page += ".status-txt{background:rgba(255,255,255,0.06);color:rgba(255,255,255,0.45);border:1px solid rgba(255,255,255,0.08);}";
    page += ".status-dot-live{width:8px;height:8px;border-radius:50%;background:rgba(255,255,255,0.2);}";
  }

  page += R"rawliteral(
@keyframes liveDot{0%,100%{opacity:1;transform:scale(1);}50%{opacity:0.3;transform:scale(0.8);}}

/* ---- BOTTOM CONTROLS ---- */
.bottom-ctrls {
  padding:20px 20px 36px;flex-shrink:0;
  background:linear-gradient(to top, rgba(11,14,20,1) 60%, transparent);
}

/* Power button — large circle center */
.power-row {
  display:flex;align-items:center;justify-content:space-around;
  margin-bottom:20px;
}
.side-btn {
  display:flex;flex-direction:column;align-items:center;gap:6px;
  color:rgba(255,255,255,0.35);font-size:11px;font-weight:700;
  cursor:pointer;text-decoration:none;letter-spacing:0.3px;
  transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);
}
.side-btn:hover  { color:rgba(255,255,255,0.7);transform:translateY(-2px); }
.side-btn:active { transform:scale(0.9); }
.side-icon {
  width:52px;height:52px;border-radius:50%;
  background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.08);
  display:flex;align-items:center;justify-content:center;
  font-size:22px;transition:all 0.2s;
}
.side-btn:hover .side-icon { background:rgba(255,255,255,0.1); }

/* THE BIG POWER BUTTON */
.power-btn-wrap { position:relative;display:flex;align-items:center;justify-content:center; }
.power-btn-bg {
  position:absolute;width:90px;height:90px;border-radius:50%;
  transition:all 0.4s cubic-bezier(0.4,0,0.2,1);
}
)rawliteral";

  if(state) {
    page += ".power-btn-bg{background:radial-gradient(circle,rgba(74,222,128,0.3),rgba(74,222,128,0.05));animation:pwrGlow 2s ease-in-out infinite;}";
    page += "@keyframes pwrGlow{0%,100%{transform:scale(1);opacity:0.8;}50%{transform:scale(1.15);opacity:1;}}";
  } else {
    page += ".power-btn-bg{background:transparent;}";
  }

  page += R"rawliteral(
.power-btn {
  width:72px;height:72px;border-radius:50%;
  display:flex;align-items:center;justify-content:center;
  font-size:28px;cursor:pointer;
  text-decoration:none;position:relative;z-index:2;
  transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);
  border:2px solid;
}
)rawliteral";

  if(state) {
    page += ".power-btn{background:rgba(74,222,128,0.15);border-color:rgba(74,222,128,0.4);box-shadow:0 0 24px rgba(74,222,128,0.3),inset 0 1px 0 rgba(74,222,128,0.2);}";
  } else {
    page += ".power-btn{background:rgba(255,255,255,0.07);border-color:rgba(255,255,255,0.12);}";
  }

  page += R"rawliteral(
.power-btn:hover  { transform:scale(1.08); }
.power-btn:active { transform:scale(0.92); }
.power-icon { font-size:28px; transition:all 0.3s; }
)rawliteral";
  if(state) {
    page += ".power-icon{color:#4ade80;filter:drop-shadow(0 0 8px rgba(74,222,128,0.8));}";
  } else {
    page += ".power-icon{color:rgba(255,255,255,0.3);}";
  }

  page += R"rawliteral(

/* ---- QUICK INFO ROW ---- */
.info-row {
  display:grid;grid-template-columns:1fr 1fr;gap:10px;
}
.info-tile {
  background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.07);
  border-radius:18px;padding:14px 16px;
  display:flex;align-items:center;gap:12px;
  text-decoration:none;color:inherit;
  transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);
}
.info-tile:hover  { background:rgba(255,255,255,0.07);transform:translateY(-2px); }
.info-tile:active { transform:scale(0.96); }
.info-tile-icon {
  width:38px;height:38px;border-radius:12px;
  display:flex;align-items:center;justify-content:center;font-size:18px;
}
.info-tile-txt .ti-label { font-size:10px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:0.5px; }
.info-tile-txt .ti-val   { font-size:13px;font-weight:700;color:rgba(255,255,255,0.7);margin-top:2px; }

/* ---- PAGE ANIM ---- */
.plug-stage { animation:stageIn 0.5s cubic-bezier(0.34,1.4,0.64,1) both; }
.bottom-ctrls { animation:ctrlsIn 0.45s 0.1s cubic-bezier(0.34,1.4,0.64,1) both; }
@keyframes stageIn { from{opacity:0;transform:scale(0.95);} to{opacity:1;transform:scale(1);} }
@keyframes ctrlsIn { from{opacity:0;transform:translateY(24px);} to{opacity:1;transform:translateY(0);} }
</style>

<div class="hdr">
  <a href="/" class="back-btn">&#8592;</a>
  <div class="hdr-name">)rawliteral";

  page += rName;
  page += R"rawliteral(</div>
  <a href="/settings" class="hdr-menu">&#8230;</a>
</div>

<!-- PLUG STAGE -->
<div class="plug-stage">
  <div class="ambient-glow"></div>
  <div class="big-plug-wrap">
    <svg width="190" height="190" viewBox="0 0 120 120" fill="none" xmlns="http://www.w3.org/2000/svg">
      <!-- Shadow -->
      <ellipse cx="60" cy="112" rx="36" ry="6" fill="rgba(0,0,0,0.35)"/>
      <!-- Body -->
      <rect class="big-socket-body" x="10" y="8" width="100" height="100" rx="22"/>
      <!-- Top shine -->
      <rect class="big-socket-shine" x="18" y="12" width="60" height="20" rx="10" fill="white"/>
      <!-- Top pin (round) -->
      <circle class="big-socket-pin" cx="60" cy="38" r="11"/>
      <circle fill="rgba(0,0,0,0.15)" cx="60" cy="38" r="7"/>
      <!-- Bottom left pin -->
      <circle class="big-socket-pin" cx="38" cy="72" r="10"/>
      <circle fill="rgba(0,0,0,0.15)" cx="38" cy="72" r="6"/>
      <!-- Bottom right pin -->
      <circle class="big-socket-pin" cx="82" cy="72" r="10"/>
      <circle fill="rgba(0,0,0,0.15)" cx="82" cy="72" r="6"/>
      <!-- LED indicator -->
      <circle class="big-socket-led" cx="60" cy="18" r="5"/>
    </svg>
  </div>

  <div class="status-txt">
    <span class="status-dot-live"></span>
    )rawliteral";

  page += String(state ? "Plug turned on" : "Plug turned off");
  page += R"rawliteral(
  </div>
</div>

<!-- BOTTOM CONTROLS -->
<div class="bottom-ctrls">
  <div class="power-row">

    <!-- Timer -->
    <a href="/timers/)rawliteral";
  page += String(relay);
  page += R"rawliteral(" class="side-btn">
      <div class="side-icon">&#9200;</div>
      <span>Timer)rawliteral";
  if(activeTimers > 0) page += " (" + String(activeTimers) + ")";
  page += R"rawliteral(</span>
    </a>

    <!-- POWER BUTTON -->
    <div class="power-btn-wrap">
      <div class="power-btn-bg"></div>
      <a href="/relay/toggle/)rawliteral";
  page += String(relay);
  page += R"rawliteral(" class="power-btn">
        <span class="power-icon">&#9211;</span>
      </a>
    </div>

    <!-- Alarm -->
    <a href="/alarms/)rawliteral";
  page += String(relay);
  page += R"rawliteral(" class="side-btn">
      <div class="side-icon">&#128197;</div>
      <span>Alarm</span>
    </a>
  </div>

  <!-- Info tiles -->
  <div class="info-row">
    <a href="/restore" class="info-tile">
      <div class="info-tile-icon" style="background:rgba(10,132,255,0.12);">&#128268;</div>
      <div class="info-tile-txt">
        <div class="ti-label">On Restore</div>
        <div class="ti-val">)rawliteral";

  int mode = (relay==1) ? restoreMode1 : restoreMode2;
  page += String(mode==0 ? "Always Off" : mode==1 ? "Always On" : "Last State");
  page += R"rawliteral(</div>
      </div>
    </a>
    <a href="/led" class="info-tile">
      <div class="info-tile-icon" style="background:rgba(191,90,242,0.12);">&#128161;</div>
      <div class="info-tile-txt">
        <div class="ti-label">LED Strip</div>
        <div class="ti-val">)rawliteral";
  page += String(ledEnabled ? "On" : "Off");
  page += R"rawliteral(</div>
      </div>
    </a>
  </div>
</div>

<script>
window.onbeforeunload=function(){sessionStorage.setItem('sp',window.scrollY);};
window.onload=function(){var p=sessionStorage.getItem('sp');if(p){window.scrollTo(0,parseInt(p));sessionStorage.removeItem('sp');}};
</script>
</body></html>
)rawliteral";

  server.send(200,"text/html",page);
}

/* ==========================================================
   LED PAGE
   ========================================================== */
void handleLED() {
  char hexColor[8]; sprintf(hexColor,"#%02X%02X%02X",ledRed,ledGreen,ledBlue);
  String page = getCommonHead("LED Settings");
  page += R"rawliteral(
<style>
body{background:#0b0e14;}
.hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;}
.back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);}
.back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;transform:scale(1.08);}
.back-btn:active{transform:scale(0.9);}
.pg-title{font-size:19px;font-weight:800;color:#fff;}
.pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;}
.content{padding:0 16px 40px;}
.card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;animation:fadeUp 0.4s cubic-bezier(0.34,1.4,0.64,1) both;}
@keyframes fadeUp{from{opacity:0;transform:translateY(20px);}to{opacity:1;transform:translateY(0);}}
.card:nth-child(1){animation-delay:0.05s;}.card:nth-child(2){animation-delay:0.1s;}.card:nth-child(3){animation-delay:0.15s;}.card:nth-child(4){animation-delay:0.2s;}.card:nth-child(5){animation-delay:0.25s;}
.slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;display:flex;align-items:center;gap:8px;}
.slabel::after{content:'';flex:1;height:1px;background:linear-gradient(90deg,rgba(255,255,255,0.07),transparent);}

/* LED BARS PREVIEW */
.led-preview-card{background:#0d1a30;border:1px solid rgba(99,230,190,0.1);border-radius:24px;padding:20px;margin-bottom:12px;position:relative;overflow:hidden;}
.led-preview-card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;background:linear-gradient(90deg,#ff2d78,#bf5af2,#0a84ff,#30d158,#ffd60a,#ff2d78);background-size:200% 100%;animation:rainbow 3s linear infinite;}
@keyframes rainbow{0%{background-position:0% 50%;}100%{background-position:200% 50%;}}
.led-bars-big{display:flex;gap:8px;align-items:flex-end;height:60px;justify-content:center;padding:10px 0;}
.led-bb{border-radius:6px;width:30px;transition:all 0.3s;}
.led-bb:nth-child(1){height:30px;animation:bbA 1.8s 0.0s ease-in-out infinite;}
.led-bb:nth-child(2){height:45px;animation:bbA 1.8s 0.2s ease-in-out infinite;}
.led-bb:nth-child(3){height:60px;animation:bbA 1.8s 0.4s ease-in-out infinite;}
.led-bb:nth-child(4){height:45px;animation:bbA 1.8s 0.6s ease-in-out infinite;}
.led-bb:nth-child(5){height:30px;animation:bbA 1.8s 0.8s ease-in-out infinite;}
@keyframes bbA{0%,100%{transform:scaleY(1);opacity:1;}50%{transform:scaleY(0.45);opacity:0.5;}}

.toggle-row{display:flex;align-items:center;justify-content:space-between;margin-top:14px;}
.toggle-label{font-size:14px;font-weight:700;color:rgba(255,255,255,0.6);}
.toggle-chip{padding:9px 20px;border-radius:50px;font-size:13px;font-weight:700;text-decoration:none;border:1px solid;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);}
.toggle-chip:hover{transform:scale(1.06);}.toggle-chip:active{transform:scale(0.94);}
.chip-on{background:rgba(191,90,242,0.15);color:#bf5af2;border-color:rgba(191,90,242,0.3);}
.chip-off{background:rgba(255,255,255,0.05);color:rgba(255,255,255,0.3);border-color:rgba(255,255,255,0.08);}

/* COLOR */
.color-picker-row{display:flex;gap:10px;align-items:center;margin-bottom:12px;}
input[type="color"]{width:52px;height:48px;padding:2px;border-radius:12px;border:2px solid rgba(255,255,255,0.1);cursor:pointer;background:transparent;}
.color-prev-box{flex:1;height:52px;border-radius:14px;border:1px solid rgba(255,255,255,0.08);transition:background 0.3s;position:relative;overflow:hidden;}
.color-prev-box::after{content:'';position:absolute;inset:0;background:linear-gradient(135deg,rgba(255,255,255,0.12),transparent);}
.presets{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0;}
.pc{width:32px;height:32px;border-radius:50%;border:2px solid rgba(255,255,255,0.12);cursor:pointer;transition:all 0.2s cubic-bezier(0.34,1.56,0.64,1);}
.pc:hover{transform:scale(1.25);border-color:#fff;}
.pc:active{transform:scale(0.9);}

/* SLIDERS */
.slider-group{margin-bottom:16px;}
.sl-label{display:flex;justify-content:space-between;align-items:center;font-size:12px;color:rgba(255,255,255,0.35);margin-bottom:8px;font-weight:600;}
.sl-val{font-family:monospace;font-size:13px;font-weight:700;color:rgba(255,255,255,0.7);}
input[type="range"]{width:100%;height:6px;border-radius:3px;background:rgba(255,255,255,0.1);outline:none;border:none;cursor:pointer;-webkit-appearance:none;appearance:none;accent-color:#bf5af2;}
input[type="range"]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:linear-gradient(135deg,#bf5af2,#0a84ff);box-shadow:0 2px 10px rgba(191,90,242,0.5);cursor:pointer;transition:transform 0.15s;}
input[type="range"]:hover::-webkit-slider-thumb{transform:scale(1.2);}

/* BRIGHTNESS QUICK */
.bq{display:flex;gap:6px;margin-top:10px;flex-wrap:wrap;}
.bqb{flex:1;min-width:55px;padding:10px 4px;border-radius:12px;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.07);color:rgba(255,255,255,0.45);font-size:11px;font-weight:700;text-align:center;text-decoration:none;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);}
.bqb:hover{background:rgba(255,255,255,0.1);color:#fff;transform:translateY(-2px);}
.bqb:active{transform:scale(0.94);}

/* SPEED */
.sq{display:flex;gap:8px;margin-top:10px;}
.sqb{flex:1;padding:12px;border-radius:14px;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.07);color:rgba(255,255,255,0.45);font-size:13px;font-weight:700;text-align:center;text-decoration:none;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);}
.sqb:hover{background:rgba(255,255,255,0.1);color:#fff;transform:translateY(-2px);}

/* EFFECTS */
.fx-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;}
.fxb{padding:12px 6px;background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.07);border-radius:14px;color:rgba(255,255,255,0.4);text-align:center;font-size:12px;font-weight:700;text-decoration:none;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);}
.fxb:hover{transform:translateY(-2px) scale(1.03);color:#fff;border-color:rgba(191,90,242,0.3);background:rgba(191,90,242,0.08);}
.fxb:active{transform:scale(0.94);}
.fxb.active{background:rgba(191,90,242,0.15);border-color:rgba(191,90,242,0.4);color:#bf5af2;box-shadow:0 0 16px rgba(191,90,242,0.12);}

/* FORM */
input[type="number"],input[type="date"],input[type="time"],input[type="text"],select{width:100%;padding:12px 14px;background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.09);border-radius:14px;color:#fff;font-size:14px;font-family:'Nunito',sans-serif;transition:border-color 0.2s;-webkit-appearance:none;}
input:focus,select:focus{outline:none;border-color:rgba(191,90,242,0.5);}
select option{background:#1c2030;}
.btn{display:flex;align-items:center;justify-content:center;padding:14px;border-radius:16px;font-size:14px;font-weight:800;cursor:pointer;text-decoration:none;border:none;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);font-family:'Nunito',sans-serif;width:100%;margin-top:12px;}
.btn:hover{transform:translateY(-2px);}.btn:active{transform:scale(0.96);}
.btn-green{background:rgba(48,209,88,0.15);color:#30d158;border:1px solid rgba(48,209,88,0.3);}
.btn-green:hover{background:rgba(48,209,88,0.25);}
</style>
)rawliteral";

  // LED Preview
  page += "<div class='hdr'><a href='/' class='back-btn'>&#8592;</a><div><div class='pg-title'>LED Settings</div><div class='pg-sub'>WS2812B Strip</div></div></div>";
  page += "<div class='content'>";
  page += "<div class='led-preview-card'>";
  page += "<div class='led-bars-big'>";
  for(int i=0;i<NUM_LEDS;i++){
    String col = ledEnabled ? String(hexColor) : "#1a2030";
    String glow = ledEnabled ? ";box-shadow:0 0 16px "+col+",0 0 4px "+col : "";
    page += "<div class='led-bb' style='background:"+col+glow+"'></div>";
  }
  page += "</div>";
  page += "<div class='toggle-row'>";
  page += "<span class='toggle-label'>Status: <strong style='color:" + String(ledEnabled?"#4ade80":"rgba(255,255,255,0.3)") + ";'>" + String(ledEnabled?"ON":"OFF") + "</strong></span>";
  page += "<a href='/led/toggle' class='toggle-chip " + String(ledEnabled?"chip-on":"chip-off") + "'>" + String(ledEnabled?"Turn Off":"Turn On") + "</a>";
  page += "</div></div>";

  // Color Card
  page += "<div class='card'><div class='slabel'>Color</div>";
  page += "<div class='color-picker-row'><input type='color' id='chex' value='" + String(hexColor) + "' onchange='applyHex()'>";
  page += "<div id='cprev' class='color-prev-box' style='background:linear-gradient(135deg," + String(hexColor) + " 0%,rgba(" + String(ledRed) + "," + String(ledGreen) + "," + String(ledBlue) + ",0.4) 100%);'></div></div>";
  page += "<div class='slabel' style='margin-bottom:8px;'>Presets</div><div class='presets'>";
  const char* pr[][4]={{"255","0","0","#FF0000"},{"0","255","0","#00FF00"},{"0","0","255","#0000FF"},{"255","255","0","#FFFF00"},{"255","165","0","#FFA500"},{"128","0","128","#800080"},{"0","255","255","#00FFFF"},{"255","20","147","#FF1493"},{"255","255","255","#FFFFFF"},{"255","100","0","#FF6400"},{"0","128","255","#0080FF"},{"50","205","50","#32CD32"}};
  for(auto& p:pr) page += "<div class='pc' style='background:"+String(p[3])+";' onclick='setPC("+p[0]+","+p[1]+","+p[2]+")'></div>";
  page += "</div>";
  page += "<form action='/led/color' method='GET'>";
  page += "<div class='slider-group'><div class='sl-label'><span>&#128308; Red</span><span class='sl-val' id='rd'>"+String(ledRed)+"</span></div><input type='range' id='rv' name='r' min='0' max='255' value='"+String(ledRed)+"' oninput=\"document.getElementById('rd').textContent=this.value;updateCP()\" style='accent-color:#ff453a;'></div>";
  page += "<div class='slider-group'><div class='sl-label'><span>&#128994; Green</span><span class='sl-val' id='gd'>"+String(ledGreen)+"</span></div><input type='range' id='gv' name='g' min='0' max='255' value='"+String(ledGreen)+"' oninput=\"document.getElementById('gd').textContent=this.value;updateCP()\" style='accent-color:#30d158;'></div>";
  page += "<div class='slider-group'><div class='sl-label'><span>&#128309; Blue</span><span class='sl-val' id='bd'>"+String(ledBlue)+"</span></div><input type='range' id='bv' name='b' min='0' max='255' value='"+String(ledBlue)+"' oninput=\"document.getElementById('bd').textContent=this.value;updateCP()\" style='accent-color:#0a84ff;'></div>";
  page += "<button type='submit' class='btn btn-green'>Apply Color</button></form></div>";

  // Brightness
  page += "<div class='card'><div class='slabel'>Brightness</div><form action='/led/brightness' method='GET'>";
  page += "<div class='slider-group'><div class='sl-label'><span>Level</span><span class='sl-val' id='brd'>"+String(ledBrightness)+"</span></div><input type='range' name='v' min='0' max='255' value='"+String(ledBrightness)+"' oninput=\"document.getElementById('brd').textContent=this.value\"></div>";
  page += "<div class='bq'><a href='/led/brightness?v=30' class='bqb'>12%</a><a href='/led/brightness?v=80' class='bqb'>31%</a><a href='/led/brightness?v=128' class='bqb'>50%</a><a href='/led/brightness?v=200' class='bqb'>78%</a><a href='/led/brightness?v=255' class='bqb'>Max</a></div>";
  page += "<button type='submit' class='btn btn-green'>Set Brightness</button></form></div>";

  // Speed
  page += "<div class='card'><div class='slabel'>Effect Speed</div><form action='/led/speed' method='GET'>";
  page += "<div class='slider-group'><div class='sl-label'><span>Delay</span><span class='sl-val' id='spd'>"+String(effectSpeed)+"ms</span></div><input type='range' name='s' min='10' max='500' value='"+String(effectSpeed)+"' oninput=\"document.getElementById('spd').textContent=this.value+'ms'\"></div>";
  page += "<div class='sq'><a href='/led/speed?s=10' class='sqb'>&#9889; Fast</a><a href='/led/speed?s=50' class='sqb'>&#9898; Normal</a><a href='/led/speed?s=200' class='sqb'>&#127974; Slow</a></div>";
  page += "<button type='submit' class='btn btn-green'>Set Speed</button></form></div>";

  // Effects
  page += "<div class='card'><div class='slabel'>Effects</div><div class='fx-grid'>";
  for(int i=0;i<15;i++){String ac=(ledEffect==i)?" active":"";page+="<a href='/led/effect?e="+String(i)+"' class='fxb"+ac+"'>"+effectName(i)+"</a>";}
  page += "</div></div></div>";

  page += R"rawliteral(<script>
window.onbeforeunload=function(){sessionStorage.setItem('sp',window.scrollY);};
window.onload=function(){var p=sessionStorage.getItem('sp');if(p){window.scrollTo(0,parseInt(p));sessionStorage.removeItem('sp');}updateCP();};
function updateCP(){var r=document.getElementById('rv'),g=document.getElementById('gv'),b=document.getElementById('bv'),pr=document.getElementById('cprev'),hx=document.getElementById('chex');if(r&&g&&b&&pr){var c='rgb('+r.value+','+g.value+','+b.value+')';pr.style.background='linear-gradient(135deg,'+c+' 0%, rgba('+r.value+','+g.value+','+b.value+',0.3) 100%)';if(hx)hx.value='#'+[r.value,g.value,b.value].map(function(x){return parseInt(x).toString(16).padStart(2,'0');}).join('');}}
function applyHex(){var h=document.getElementById('chex').value;document.getElementById('rv').value=parseInt(h.slice(1,3),16);document.getElementById('gv').value=parseInt(h.slice(3,5),16);document.getElementById('bv').value=parseInt(h.slice(5,7),16);updateCP();}
function setPC(r,g,b){document.getElementById('rv').value=r;document.getElementById('gv').value=g;document.getElementById('bv').value=b;updateCP();}
</script></body></html>)rawliteral";
  server.send(200,"text/html",page);
}

/* -- Simple sub-pages -- */
void handleTimersPage(int relay) {
  String rName=(relay==1)?relay1Name:relay2Name;
  Timer* T=(relay==1)?r1Timer:r2Timer;
  String page=getCommonHead(rName+" Timers");
  page+=R"rawliteral(<style>body{background:#0b0e14;} .hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;} .back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s;} .back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;} .pg-title{font-size:19px;font-weight:800;color:#fff;} .pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;} .content{padding:0 16px 40px;} .card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;} .slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;} table{width:100%;border-collapse:collapse;font-size:13px;} th{background:rgba(255,255,255,0.04);color:rgba(255,255,255,0.3);padding:10px 10px;text-align:left;font-weight:700;font-size:10px;text-transform:uppercase;letter-spacing:0.5px;} td{padding:11px 10px;border-bottom:1px solid rgba(255,255,255,0.05);color:rgba(255,255,255,0.6);} tr:last-child td{border-bottom:none;} .td-acts{display:flex;gap:6px;} .btn-sm{padding:6px 12px;border-radius:10px;font-size:12px;font-weight:700;text-decoration:none;border:1px solid;transition:all 0.18s;} .btn-sm:hover{transform:scale(1.05);} .btn-sm:active{transform:scale(0.94);} .bi{background:rgba(10,132,255,0.12);color:#60a5fa;border-color:rgba(10,132,255,0.2);} .bd{background:rgba(255,69,58,0.1);color:#ff453a;border-color:rgba(255,69,58,0.2);} .fg{margin-bottom:12px;} .fl{display:block;font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:0.5px;margin-bottom:6px;} input[type="number"],select{width:100%;padding:11px 14px;background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.09);border-radius:13px;color:#fff;font-size:14px;font-family:'Nunito',sans-serif;-webkit-appearance:none;} input:focus,select:focus{outline:none;border-color:rgba(191,90,242,0.5);} select option{background:#1c2030;} .form-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;} .btn{display:flex;align-items:center;justify-content:center;padding:14px;border-radius:16px;font-size:14px;font-weight:800;cursor:pointer;text-decoration:none;border:none;transition:all 0.18s;font-family:'Nunito',sans-serif;width:100%;margin-top:10px;} .btn-g{background:rgba(48,209,88,0.12);color:#30d158;border:1px solid rgba(48,209,88,0.25);} .btn-g:hover{background:rgba(48,209,88,0.2);transform:translateY(-1px);} </style>)rawliteral";
  page+="<div class='hdr'><a href='/relay/"+String(relay)+"' class='back-btn'>&#8592;</a><div><div class='pg-title'>"+rName+"</div><div class='pg-sub'>Daily Timers</div></div></div><div class='content'>";
  page+="<div class='card'><div class='slabel'>Active Timers</div><table><tr><th>#</th><th>Time</th><th>Action</th><th>On</th><th></th></tr>";
  for(int i=0;i<5;i++){char t[8];sprintf(t,"%02d:%02d",T[i].hour,T[i].minute);page+="<tr><td>"+String(i+1)+"</td><td style='font-family:monospace;color:#fff;'>"+t+"</td><td style='color:"+(T[i].action?String("#4ade80"):String("#ff453a"))+"';font-weight:700;'>"+String(T[i].action?"ON":"OFF")+"</td><td style='color:"+(T[i].enable?String("#4ade80"):String("rgba(255,255,255,0.2)"))+";'>"+String(T[i].enable?"&#10003;":"&#10007;")+"</td><td><div class='td-acts'><a href='/timer/toggle/"+String(relay)+"/"+String(i)+"' class='btn-sm bi'>"+String(T[i].enable?"Off":"On")+"</a><a href='/timer/delete/"+String(relay)+"/"+String(i)+"' class='btn-sm bd'>Del</a></div></td></tr>";}
  page+="</table></div><div class='card'><div class='slabel'>Add Timer</div><form action='/timer/add/"+String(relay)+"' method='GET'><div class='form-row'><div class='fg'><label class='fl'>Hour</label><input type='number' name='h' min='0' max='23' value='12'></div><div class='fg'><label class='fl'>Minute</label><input type='number' name='m' min='0' max='59' value='0'></div></div><div class='fg'><label class='fl'>Action</label><select name='a'><option value='1'>Turn ON</option><option value='0'>Turn OFF</option></select></div><button type='submit' class='btn btn-g'>Add Timer</button></form></div></div>";
  page+="<script>window.onbeforeunload=function(){sessionStorage.setItem('sp',window.scrollY);};window.onload=function(){var p=sessionStorage.getItem('sp');if(p){window.scrollTo(0,parseInt(p));sessionStorage.removeItem('sp');}}</script></body></html>";
  server.send(200,"text/html",page);
}

void handleAlarmsPage(int relay) {
  String rName=(relay==1)?relay1Name:relay2Name;
  Alarm* A=(relay==1)?r1Alarm:r2Alarm;
  String page=getCommonHead(rName+" Alarms");
  page+=R"rawliteral(<style>body{background:#0b0e14;} .hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;} .back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s;} .back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;} .pg-title{font-size:19px;font-weight:800;color:#fff;} .pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;} .content{padding:0 16px 40px;} .card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;overflow-x:auto;} .slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;} table{width:100%;border-collapse:collapse;font-size:12px;} th{background:rgba(255,255,255,0.04);color:rgba(255,255,255,0.3);padding:10px 8px;text-align:left;font-weight:700;font-size:10px;text-transform:uppercase;} td{padding:10px 8px;border-bottom:1px solid rgba(255,255,255,0.05);color:rgba(255,255,255,0.6);} tr:last-child td{border-bottom:none;} .td-acts{display:flex;gap:5px;} .btn-sm{padding:6px 11px;border-radius:10px;font-size:11px;font-weight:700;text-decoration:none;border:1px solid;transition:all 0.18s;} .bi{background:rgba(10,132,255,0.12);color:#60a5fa;border-color:rgba(10,132,255,0.2);} .bd{background:rgba(255,69,58,0.1);color:#ff453a;border-color:rgba(255,69,58,0.2);} .fg{margin-bottom:12px;} .fl{display:block;font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:0.5px;margin-bottom:6px;} input[type="date"],input[type="time"],select{width:100%;padding:11px 14px;background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.09);border-radius:13px;color:#fff;font-size:14px;font-family:'Nunito',sans-serif;-webkit-appearance:none;} input:focus,select:focus{outline:none;border-color:rgba(191,90,242,0.5);} select option{background:#1c2030;} .form-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;} .btn{display:flex;align-items:center;justify-content:center;padding:14px;border-radius:16px;font-size:14px;font-weight:800;cursor:pointer;text-decoration:none;border:none;transition:all 0.18s;font-family:'Nunito',sans-serif;width:100%;margin-top:10px;} .btn-g{background:rgba(48,209,88,0.12);color:#30d158;border:1px solid rgba(48,209,88,0.25);} </style>)rawliteral";
  page+="<div class='hdr'><a href='/relay/"+String(relay)+"' class='back-btn'>&#8592;</a><div><div class='pg-title'>"+rName+"</div><div class='pg-sub'>One-Time Alarms</div></div></div><div class='content'>";
  page+="<div class='card'><div class='slabel'>Alarms</div><table><tr><th>#</th><th>Date</th><th>Time</th><th>Act</th><th>On</th><th></th></tr>";
  for(int i=0;i<5;i++){char d[12],t[8];sprintf(d,"%02d/%02d/%04d",A[i].day,A[i].month,A[i].year);sprintf(t,"%02d:%02d",A[i].hour,A[i].minute);page+="<tr><td>"+String(i+1)+"</td><td style='font-size:11px;'>"+d+"</td><td style='font-family:monospace;color:#fff;'>"+t+"</td><td style='color:"+(A[i].action?String("#4ade80"):String("#ff453a"))+"';font-weight:700;'>"+String(A[i].action?"ON":"OFF")+"</td><td style='color:"+(A[i].enable?String("#4ade80"):String("rgba(255,255,255,0.2)"))+";'>"+String(A[i].enable?"&#10003;":"&#10007;")+"</td><td><div class='td-acts'><a href='/alarm/toggle/"+String(relay)+"/"+String(i)+"' class='btn-sm bi'>"+String(A[i].enable?"Off":"On")+"</a><a href='/alarm/delete/"+String(relay)+"/"+String(i)+"' class='btn-sm bd'>Del</a></div></td></tr>";}
  page+="</table></div><div class='card'><div class='slabel'>Add Alarm</div><form action='/alarm/add/"+String(relay)+"' method='GET'><div class='form-row'><div class='fg'><label class='fl'>Date</label><input type='date' name='d'></div><div class='fg'><label class='fl'>Time</label><input type='time' name='t'></div></div><div class='fg'><label class='fl'>Action</label><select name='a'><option value='1'>Turn ON</option><option value='0'>Turn OFF</option></select></div><button type='submit' class='btn btn-g'>Add Alarm</button></form></div></div>";
  page+="<script>window.onbeforeunload=function(){sessionStorage.setItem('sp',window.scrollY);};window.onload=function(){var p=sessionStorage.getItem('sp');if(p){window.scrollTo(0,parseInt(p));sessionStorage.removeItem('sp');}}</script></body></html>";
  server.send(200,"text/html",page);
}

void handleSettings() {
  String page=getCommonHead("Settings");
  page+=R"rawliteral(<style>body{background:#0b0e14;} .hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;} .back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s;} .back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;} .pg-title{font-size:19px;font-weight:800;color:#fff;} .pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;} .content{padding:0 16px 40px;} .card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;} .slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;} .fg{margin-bottom:12px;} .fl{display:block;font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:0.5px;margin-bottom:6px;} input[type="text"]{width:100%;padding:12px 14px;background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.09);border-radius:13px;color:#fff;font-size:14px;font-family:'Nunito',sans-serif;} input:focus{outline:none;border-color:rgba(191,90,242,0.5);} .btn{display:flex;align-items:center;justify-content:center;padding:14px;border-radius:16px;font-size:14px;font-weight:800;cursor:pointer;text-decoration:none;border:none;transition:all 0.18s;font-family:'Nunito',sans-serif;width:100%;margin-top:10px;} .btn-g{background:rgba(48,209,88,0.12);color:#30d158;border:1px solid rgba(48,209,88,0.25);} .btn-g:hover{background:rgba(48,209,88,0.2);} </style>)rawliteral";
  page+="<div class='hdr'><a href='/' class='back-btn'>&#8592;</a><div><div class='pg-title'>Settings</div><div class='pg-sub'>Configure relays</div></div></div><div class='content'>";
  for(int r=1;r<=2;r++){String rN=(r==1)?relay1Name:relay2Name;page+="<div class='card'><div class='slabel'>Relay "+String(r)+"</div><form action='/settings/save/"+String(r)+"' method='GET'><div class='fg'><label class='fl'>Name</label><input type='text' name='name' value='"+rN+"' maxlength='20'></div><button type='submit' class='btn btn-g'>Save</button></form></div>";}
  page+="</div></body></html>";
  server.send(200,"text/html",page);
}

void handleRestore() {
  String page=getCommonHead("Power Restore");
  page+=R"rawliteral(<style>body{background:#0b0e14;} .hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;} .back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s;} .back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;} .pg-title{font-size:19px;font-weight:800;color:#fff;} .pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;} .content{padding:0 16px 40px;} .card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;} .slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;} .ro-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px;} .ro{padding:18px 8px;border-radius:18px;background:rgba(255,255,255,0.04);border:1px solid rgba(255,255,255,0.07);text-align:center;text-decoration:none;color:rgba(255,255,255,0.3);font-size:12px;font-weight:700;display:block;transition:all 0.18s cubic-bezier(0.34,1.56,0.64,1);} .ro:hover{transform:translateY(-2px);color:#fff;} .ro:active{transform:scale(0.94);} .ro.active{background:rgba(191,90,242,0.15);border-color:rgba(191,90,242,0.4);color:#bf5af2;} .ro-icon{font-size:26px;margin-bottom:8px;display:block;} .info-b{background:rgba(10,132,255,0.07);border-left:3px solid rgba(10,132,255,0.3);padding:12px 16px;border-radius:0 13px 13px 0;font-size:12px;color:rgba(100,180,255,0.8);margin-top:12px;} </style>)rawliteral";
  page+="<div class='hdr'><a href='/' class='back-btn'>&#8592;</a><div><div class='pg-title'>Power Restore</div><div class='pg-sub'>Behavior on power cut</div></div></div><div class='content'>";
  for(int r=1;r<=2;r++){int m=(r==1)?restoreMode1:restoreMode2;String rN=(r==1)?relay1Name:relay2Name;page+="<div class='card'><div class='slabel'>"+rN+"</div><div class='ro-grid'><a href='/restore/set/"+String(r)+"/0' class='ro "+(m==0?"active":"")+"'><span class='ro-icon'>&#128308;</span>Always Off</a><a href='/restore/set/"+String(r)+"/1' class='ro "+(m==1?"active":"")+"'><span class='ro-icon'>&#128994;</span>Always On</a><a href='/restore/set/"+String(r)+"/2' class='ro "+(m==2?"active":"")+"'><span class='ro-icon'>&#128260;</span>Last State</a></div><div class='info-b'>Current: <strong>"+String(m==0?"Always Off":m==1?"Always On":"Last State")+"</strong></div></div>";}
  page+="</div></body></html>";
  server.send(200,"text/html",page);
}

void handleDateTime() {
  DateTime now=rtc.now();
  String page=getCommonHead("Date & Time");
  page+=R"rawliteral(<style>body{background:#0b0e14;} .hdr{display:flex;align-items:center;gap:12px;padding:18px 20px 14px;} .back-btn{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,0.07);border:1px solid rgba(255,255,255,0.08);display:flex;align-items:center;justify-content:center;font-size:18px;color:rgba(255,255,255,0.7);text-decoration:none;transition:all 0.18s;} .back-btn:hover{background:rgba(255,255,255,0.12);color:#fff;} .pg-title{font-size:19px;font-weight:800;color:#fff;} .pg-sub{font-size:12px;color:rgba(255,255,255,0.3);margin-top:2px;font-weight:600;} .content{padding:0 16px 40px;} .card{background:#131720;border:1px solid rgba(255,255,255,0.07);border-radius:24px;padding:20px;margin-bottom:12px;} .slabel{font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px;} .mono-time{font-family:monospace;font-size:28px;color:#bf5af2;text-align:center;padding:20px 0;letter-spacing:2px;} .fg{margin-bottom:12px;} .fl{display:block;font-size:11px;font-weight:700;color:rgba(255,255,255,0.3);text-transform:uppercase;letter-spacing:0.5px;margin-bottom:6px;} input[type="date"],input[type="time"]{width:100%;padding:12px 14px;background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.09);border-radius:13px;color:#fff;font-size:14px;font-family:'Nunito',sans-serif;-webkit-appearance:none;} input:focus{outline:none;border-color:rgba(191,90,242,0.5);} .form-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;} .btn{display:flex;align-items:center;justify-content:center;padding:14px;border-radius:16px;font-size:14px;font-weight:800;cursor:pointer;text-decoration:none;border:none;transition:all 0.18s;font-family:'Nunito',sans-serif;width:100%;margin-top:10px;} .btn-g{background:rgba(48,209,88,0.12);color:#30d158;border:1px solid rgba(48,209,88,0.25);} .btn-g:hover{background:rgba(48,209,88,0.2);} </style>)rawliteral";
  char buf[32]; sprintf(buf,"%02d/%02d/%04d &mdash; %02d:%02d:%02d",now.day(),now.month(),now.year(),now.hour(),now.minute(),now.second());
  page+="<div class='hdr'><a href='/' class='back-btn'>&#8592;</a><div><div class='pg-title'>Date &amp; Time</div><div class='pg-sub'>RTC DS3231</div></div></div><div class='content'>";
  page+="<div class='card'><div class='slabel'>Current</div><div class='mono-time'>"+String(buf)+"</div></div>";
  page+="<div class='card'><div class='slabel'>Update</div><form action='/datetime/set' method='GET'><div class='form-row'><div class='fg'><label class='fl'>Date</label><input type='date' name='d'></div><div class='fg'><label class='fl'>Time</label><input type='time' name='t'></div></div><button type='submit' class='btn btn-g'>Update RTC</button></form></div></div></body></html>";
  server.send(200,"text/html",page);
}

/* -------- SETUP -------- */
void setup() {
  Serial.begin(115200);
  pinMode(RELAY1,OUTPUT); pinMode(RELAY2,OUTPUT);
  pinMode(SW1,INPUT_PULLUP); pinMode(SW2,INPUT_PULLUP); pinMode(SW3,INPUT_PULLUP);
  digitalWrite(RELAY1,HIGH); digitalWrite(RELAY2,HIGH);
  FastLED.addLeds<WS2812B,LED_PIN,GRB>(leds,NUM_LEDS); FastLED.setBrightness(128);
  fill_solid(leds,NUM_LEDS,CRGB::Black); FastLED.show();
  EEPROM.begin(EEPROM_SIZE); Wire.begin(21,22);
  if(!rtc.begin()) { Serial.println("RTC not found!"); }
  else { DateTime n=rtc.now(); if(n.year()<2020||n.year()>2100) rtc.adjust(DateTime(2026,2,14,12,0,0)); }
  loadRestoreMode(); loadRelayNames(); loadState(); loadTimers(); loadAlarms(); loadLEDSettings();
  WiFi.softAP(ssid,pass);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());

  // Pages
  server.on("/",           handleRoot);
  server.on("/led",        handleLED);
  server.on("/settings",   handleSettings);
  server.on("/restore",    handleRestore);
  server.on("/datetime",   handleDateTime);

  // Relay detail pages
  server.on("/relay/1", [](){ handleRelayPage(1); });
  server.on("/relay/2", [](){ handleRelayPage(2); });

  // Toggle relays (from detail page)
  server.on("/relay/toggle/1", [](){ relay1State=!relay1State; applyRelay(); saveState(); server.sendHeader("Location","/relay/1"); server.send(303); });
  server.on("/relay/toggle/2", [](){ relay2State=!relay2State; applyRelay(); saveState(); server.sendHeader("Location","/relay/2"); server.send(303); });

  // Quick toggles from home
  server.on("/toggle/1", [](){ relay1State=!relay1State; applyRelay(); saveState(); server.sendHeader("Location","/"); server.send(303); });
  server.on("/toggle/2", [](){ relay2State=!relay2State; applyRelay(); saveState(); server.sendHeader("Location","/"); server.send(303); });
  server.on("/allon",    [](){ relay1State=relay2State=true;  applyRelay(); saveState(); server.sendHeader("Location","/"); server.send(303); });
  server.on("/alloff",   [](){ relay1State=relay2State=false; applyRelay(); saveState(); server.sendHeader("Location","/"); server.send(303); });

  // LED
  server.on("/led/toggle",     [](){ ledEnabled=!ledEnabled; if(!ledEnabled){fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();} saveLEDSettings(); server.sendHeader("Location","/led"); server.send(303); });
  server.on("/led/color",      [](){ if(server.hasArg("r"))ledRed=constrain(server.arg("r").toInt(),0,255); if(server.hasArg("g"))ledGreen=constrain(server.arg("g").toInt(),0,255); if(server.hasArg("b"))ledBlue=constrain(server.arg("b").toInt(),0,255); saveLEDSettings(); server.sendHeader("Location","/led"); server.send(303); });
  server.on("/led/brightness", [](){ if(server.hasArg("v"))ledBrightness=constrain(server.arg("v").toInt(),0,255); FastLED.setBrightness(ledBrightness); saveLEDSettings(); server.sendHeader("Location","/led"); server.send(303); });
  server.on("/led/speed",      [](){ if(server.hasArg("s"))effectSpeed=constrain(server.arg("s").toInt(),10,500); saveLEDSettings(); server.sendHeader("Location","/led"); server.send(303); });
  server.on("/led/effect",     [](){ if(server.hasArg("e")){ledEffect=constrain(server.arg("e").toInt(),0,14);effectStep=0;} saveLEDSettings(); server.sendHeader("Location","/led"); server.send(303); });

  // Restore
  server.on("/restore/set/1/0",[](){ restoreMode1=0; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });
  server.on("/restore/set/1/1",[](){ restoreMode1=1; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });
  server.on("/restore/set/1/2",[](){ restoreMode1=2; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });
  server.on("/restore/set/2/0",[](){ restoreMode2=0; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });
  server.on("/restore/set/2/1",[](){ restoreMode2=1; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });
  server.on("/restore/set/2/2",[](){ restoreMode2=2; saveRestoreMode(); server.sendHeader("Location","/restore"); server.send(303); });

  // Settings
  server.on("/settings/save/1",[](){ if(server.hasArg("name")){relay1Name=urlDecode(server.arg("name")); if(relay1Name.length()>20)relay1Name=relay1Name.substring(0,20);} saveRelayNames(); server.sendHeader("Location","/settings"); server.send(303); });
  server.on("/settings/save/2",[](){ if(server.hasArg("name")){relay2Name=urlDecode(server.arg("name")); if(relay2Name.length()>20)relay2Name=relay2Name.substring(0,20);} saveRelayNames(); server.sendHeader("Location","/settings"); server.send(303); });

  // Timers & Alarms pages
  server.on("/timers/1",[](){ handleTimersPage(1); }); server.on("/timers/2",[](){ handleTimersPage(2); });
  server.on("/alarms/1",[](){ handleAlarmsPage(1); }); server.on("/alarms/2",[](){ handleAlarmsPage(2); });

  server.on("/timer/add/1",[](){ if(server.hasArg("h")&&server.hasArg("m")&&server.hasArg("a")){for(int i=0;i<5;i++) if(!r1Timer[i].enable){r1Timer[i]={server.arg("h").toInt(),server.arg("m").toInt(),(bool)server.arg("a").toInt(),true};saveTimers();break;}} server.sendHeader("Location","/timers/1"); server.send(303); });
  server.on("/timer/add/2",[](){ if(server.hasArg("h")&&server.hasArg("m")&&server.hasArg("a")){for(int i=0;i<5;i++) if(!r2Timer[i].enable){r2Timer[i]={server.arg("h").toInt(),server.arg("m").toInt(),(bool)server.arg("a").toInt(),true};saveTimers();break;}} server.sendHeader("Location","/timers/2"); server.send(303); });

  for(int i=0;i<5;i++){
    server.on(("/timer/toggle/1/"+String(i)).c_str(),[i](){ r1Timer[i].enable=!r1Timer[i].enable; saveTimers(); server.sendHeader("Location","/timers/1"); server.send(303); });
    server.on(("/timer/toggle/2/"+String(i)).c_str(),[i](){ r2Timer[i].enable=!r2Timer[i].enable; saveTimers(); server.sendHeader("Location","/timers/2"); server.send(303); });
    server.on(("/timer/delete/1/"+String(i)).c_str(),[i](){ r1Timer[i].enable=false; saveTimers(); server.sendHeader("Location","/timers/1"); server.send(303); });
    server.on(("/timer/delete/2/"+String(i)).c_str(),[i](){ r2Timer[i].enable=false; saveTimers(); server.sendHeader("Location","/timers/2"); server.send(303); });
  }

  server.on("/alarm/add/1",[](){ if(server.hasArg("d")&&server.hasArg("t")&&server.hasArg("a")){String d=server.arg("d"),t=server.arg("t");for(int i=0;i<5;i++) if(!r1Alarm[i].enable){r1Alarm[i]={d.substring(8,10).toInt(),d.substring(5,7).toInt(),d.substring(0,4).toInt(),t.substring(0,2).toInt(),t.substring(3,5).toInt(),(bool)server.arg("a").toInt(),true,false};saveAlarms();break;}} server.sendHeader("Location","/alarms/1"); server.send(303); });
  server.on("/alarm/add/2",[](){ if(server.hasArg("d")&&server.hasArg("t")&&server.hasArg("a")){String d=server.arg("d"),t=server.arg("t");for(int i=0;i<5;i++) if(!r2Alarm[i].enable){r2Alarm[i]={d.substring(8,10).toInt(),d.substring(5,7).toInt(),d.substring(0,4).toInt(),t.substring(0,2).toInt(),t.substring(3,5).toInt(),(bool)server.arg("a").toInt(),true,false};saveAlarms();break;}} server.sendHeader("Location","/alarms/2"); server.send(303); });

  for(int i=0;i<5;i++){
    server.on(("/alarm/toggle/1/"+String(i)).c_str(),[i](){ r1Alarm[i].enable=!r1Alarm[i].enable; r1Alarm[i].triggered=false; saveAlarms(); server.sendHeader("Location","/alarms/1"); server.send(303); });
    server.on(("/alarm/toggle/2/"+String(i)).c_str(),[i](){ r2Alarm[i].enable=!r2Alarm[i].enable; r2Alarm[i].triggered=false; saveAlarms(); server.sendHeader("Location","/alarms/2"); server.send(303); });
    server.on(("/alarm/delete/1/"+String(i)).c_str(),[i](){ r1Alarm[i].enable=false; saveAlarms(); server.sendHeader("Location","/alarms/1"); server.send(303); });
    server.on(("/alarm/delete/2/"+String(i)).c_str(),[i](){ r2Alarm[i].enable=false; saveAlarms(); server.sendHeader("Location","/alarms/2"); server.send(303); });
  }

  server.on("/datetime/set",[](){ if(server.hasArg("d")&&server.hasArg("t")){String d=server.arg("d"),t=server.arg("t"); rtc.adjust(DateTime(d.substring(0,4).toInt(),d.substring(5,7).toInt(),d.substring(8,10).toInt(),t.substring(0,2).toInt(),t.substring(3,5).toInt(),0));} server.sendHeader("Location","/datetime"); server.send(303); });

  server.begin();
  Serial.println("Server started!");
}

/* -------- LOOP -------- */
void loop() {
  server.handleClient();
  applyLEDs();
  static unsigned long db1=0,db2=0,db3=0;
  static bool lsw1=HIGH,lsw2=HIGH,lsw3=HIGH;
  bool s1=digitalRead(SW1),s2=digitalRead(SW2),s3=digitalRead(SW3);
  if(s1!=lsw1&&millis()-db1>50){if(s1==LOW){relay1State=!relay1State;applyRelay();saveState();}db1=millis();}
  if(s2!=lsw2&&millis()-db2>50){if(s2==LOW){relay2State=!relay2State;applyRelay();saveState();}db2=millis();}
  if(s3!=lsw3&&millis()-db3>50){if(s3==LOW){ledEnabled=!ledEnabled;if(!ledEnabled){fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();}saveLEDSettings();}db3=millis();}
  lsw1=s1; lsw2=s2; lsw3=s3;
  static int lastMin=-1;
  DateTime now=rtc.now();
  if(now.minute()!=lastMin){
    lastMin=now.minute();
    for(int i=0;i<5;i++){
      if(r1Timer[i].enable&&r1Timer[i].hour==now.hour()&&r1Timer[i].minute==now.minute()) relay1State=r1Timer[i].action;
      if(r2Timer[i].enable&&r2Timer[i].hour==now.hour()&&r2Timer[i].minute==now.minute()) relay2State=r2Timer[i].action;
      if(r1Alarm[i].enable&&!r1Alarm[i].triggered&&r1Alarm[i].year==now.year()&&r1Alarm[i].month==now.month()&&r1Alarm[i].day==now.day()&&r1Alarm[i].hour==now.hour()&&r1Alarm[i].minute==now.minute()){relay1State=r1Alarm[i].action;r1Alarm[i].triggered=true;r1Alarm[i].enable=false;saveAlarms();}
      if(r2Alarm[i].enable&&!r2Alarm[i].triggered&&r2Alarm[i].year==now.year()&&r2Alarm[i].month==now.month()&&r2Alarm[i].day==now.day()&&r2Alarm[i].hour==now.hour()&&r2Alarm[i].minute==now.minute()){relay2State=r2Alarm[i].action;r2Alarm[i].triggered=true;r2Alarm[i].enable=false;saveAlarms();}
    }
    applyRelay(); saveState();
  }
}
