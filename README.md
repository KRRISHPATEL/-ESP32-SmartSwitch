# 🔌 ESP32 SmartSwitch

A Wi-Fi controlled dual-relay smart switch with LED strip support, built on the ESP32. Features a fully responsive web UI served directly from the ESP32, with timers, alarms, and power restore settings — no cloud, no app required.

---

## ✨ Features

- **Dual Relay Control** — independently toggle two relays via web UI or physical buttons
- **WS2812B LED Strip** — 15 visual effects with color picker, brightness, and speed controls
- **Daily Timers** — up to 5 scheduled on/off events per relay (repeats every day)
- **One-Time Alarms** — up to 5 date+time triggered events per relay
- **Power Restore Modes** — per-relay: Always On, Always Off, or restore Last State
- **RTC (DS3231)** — accurate timekeeping, survives power loss
- **Physical Buttons** — 3 push buttons: toggle relay 1, toggle relay 2, toggle LED strip
- **EEPROM Persistence** — all settings survive reboots
- **Responsive Web UI** — dark-themed, mobile-first interface; no external app needed
- **Standalone AP Mode** — creates its own Wi-Fi access point, works offline

---

## 📸 Screenshots

> Connect to the ESP32 access point and open `192.168.4.1` in any browser.

| Home | Relay Detail | LED Settings |
|------|-------------|--------------|
| Device cards with live status | Big plug graphic, timer & alarm shortcuts | Color picker, effects grid, brightness |

---

## 🛠 Hardware Requirements

| Component | Details |
|-----------|---------|
| ESP32 Dev Board | Any standard 30/38-pin ESP32 |
| Relay Module (×2) | 5V, active-low |
| DS3231 RTC Module | I²C interface |
| WS2812B LED Strip | 5 LEDs (configurable) |
| Push Buttons (×3) | Momentary, normally open |

### Wiring

| Pin | Function |
|-----|----------|
| GPIO 26 | Relay 1 (active LOW) |
| GPIO 27 | Relay 2 (active LOW) |
| GPIO 32 | Button 1 — Toggle Relay 1 |
| GPIO 33 | Button 2 — Toggle Relay 2 |
| GPIO 34 | Button 3 — Toggle LED Strip |
| GPIO 25 | WS2812B Data |
| GPIO 21 | I²C SDA (RTC) |
| GPIO 22 | I²C SCL (RTC) |

---

## 📦 Dependencies

Install the following libraries via Arduino Library Manager or PlatformIO:

| Library | Version |
|---------|---------|
| [FastLED](https://github.com/FastLED/FastLED) | ≥ 3.6 |
| [RTClib](https://github.com/adafruit/RTClib) | ≥ 2.1 |
| WiFi | Built-in (ESP32 Arduino core) |
| WebServer | Built-in (ESP32 Arduino core) |
| Wire | Built-in |
| EEPROM | Built-in |

---

## 🚀 Getting Started

### 1. Install ESP32 Arduino Core

Add this URL to **Arduino IDE → Preferences → Additional Board Manager URLs**:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then install **esp32** via **Tools → Board → Board Manager**.

### 2. Install Libraries

In Arduino IDE, go to **Sketch → Include Library → Manage Libraries** and install:
- `FastLED`
- `RTClib` by Adafruit

### 3. Flash the Firmware

1. Open `smartswitch.ino` in Arduino IDE
2. Select your board: **Tools → Board → ESP32 Dev Module**
3. Set upload speed to `115200`
4. Upload

### 4. Connect

- On your phone or laptop, connect to Wi-Fi:
  - **SSID:** `ESP32_SWITCH`
  - **Password:** `12345678`
- Open a browser and go to: **`http://192.168.4.1`**

---

## ⚙️ Configuration

### Change AP Credentials

Edit these lines near the top of the sketch:
```cpp
const char* ssid = "ESP32_SWITCH";
const char* pass = "12345678";
```

### Change Number of LEDs

```cpp
#define NUM_LEDS 5
```

### Change Relay/Button Pins

```cpp
#define RELAY1  26
#define RELAY2  27
#define SW1     32
#define SW2     33
#define SW3     34
#define LED_PIN 25
```

---

## 🌐 Web UI Pages

| Route | Description |
|-------|-------------|
| `/` | Home — device cards with quick actions |
| `/relay/1` `/relay/2` | Relay detail — toggle, timer & alarm shortcuts |
| `/led` | LED strip settings — color, brightness, effect, speed |
| `/timers/1` `/timers/2` | Add/remove daily timers for each relay |
| `/alarms/1` `/alarms/2` | Add/remove one-time date alarms for each relay |
| `/restore` | Configure power restore behavior per relay |
| `/datetime` | View and set RTC date & time |
| `/settings` | Rename relays |

---

## 💡 LED Effects

| # | Name | # | Name |
|---|------|---|------|
| 0 | Solid | 8 | Wipe |
| 1 | Blink | 9 | Chase |
| 2 | Breathe | 10 | Twinkle |
| 3 | Rainbow | 11 | Comet |
| 4 | Wave | 12 | Fade |
| 5 | Sparkle | 13 | Strobe |
| 6 | Fire | 14 | Bounce |
| 7 | Ice | | |

---

## 🔋 Power Restore Modes

On power-up or reset, each relay can independently behave as:

- **Always Off** — relay stays off regardless of previous state
- **Always On** — relay turns on immediately
- **Last State** *(default)* — restores the state from before power loss

---

## 📁 EEPROM Memory Map

| Address | Contents |
|---------|----------|
| 0–1 | Relay 1 & 2 last state |
| 2–3 | Restore modes |
| 4–43 | Relay names (20 chars each) |
| 46–52 | LED settings |
| 60–159 | Relay 1 timers (5 × 20 bytes) |
| 160–259 | Relay 2 timers |
| 260–379 | Relay 1 alarms (5 × 24 bytes) |
| 380–499 | Relay 2 alarms |

---

## 🔒 Safety Notes

> ⚠️ **This project controls mains voltage relays. Working with high voltage is dangerous.**
> - Always use an enclosure
> - Ensure proper insulation on all high-voltage wiring
> - Do not touch the relay wiring while the circuit is powered
> - Use appropriately rated relays for your load

