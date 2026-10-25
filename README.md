# 🌅 ESP32-C6 Smart Sunrise Alarm Clock

An advanced, battery-efficient bedside sunrise alarm clock built for the **Waveshare ESP32-C6-Touch-AMOLED-2.16** (2.16-inch circular 480×480 AMOLED display).

Featuring an organic gradual sunrise visual simulation, onboard high-fidelity audio chime, dynamic internet geolocation with automatic Daylight Saving Time (DST) tracking, capacitive touch, hardware button controls, and extreme low-power standby delivering **30–40+ hours** of battery life.

---

## ✨ Features

- **Circular AMOLED UI (480×480)**: Custom LVGL 8.4 watchface UI designed specifically for a 4×4 cm circular viewing area with high-contrast 48pt digits, live battery percentage, and status indicators.
- **Natural Sunrise Wake-Up Simulation**:
  - Gradual illumination rising from dawn horizon to noon sky.
  - Smooth sky color shifts: Deep Midnight (`#0F172A`) $\to$ Twilight Violet (`#4C1D95`) $\to$ Crimson Dawn (`#C2410C`) $\to$ Radiant Morning Amber (`#F59E0B`).
  - Circular progress bezel indicator and live countdown timer.
  - Dedicated on-screen stop/preview dismiss button.
- **Hi-Fi Audio Chime (ES8311 + I2S)**:
  - Custom algorithmic polyphonic alarm tones and soothing harmonic morning chimes.
  - Zero-noise clean power down in idle standby.
- **Automatic Geolocation & Dynamic Timezone (Automatic DST)**:
  - Automatically queries IP geolocation upon Wi-Fi connection.
  - Applies official POSIX timezone rules (e.g. `IST-2IDT,M3.4.4/26,M10.5.0` for Israel).
  - Handles Daylight Saving Time switches (UTC+3 $\leftrightarrow$ UTC+2) seamlessly without manual adjustments.
  - Hardware RTC (PCF85063A) synchronization keeps precise time across power cycles.
- **Live Weather Graphics & Astronomical Moon Phase**:
  - Zero-API-key real-time weather integration via Open-Meteo REST API.
  - Dynamic weather pill with custom vector icon graphics and color palettes (Sunny, Partly Cloudy, Overcast, Rain, Snow, Thunderstorm).
  - High-precision astronomical Meeus lunar engine calculating current moon age and rendering 8 distinct lunar phases at night (New Moon, Waxing Crescent, First Quarter, Waxing Gibbous, Full Moon, Waning Gibbous, Last Quarter, Waning Crescent).
  - Tap-to-refresh weather touch target on the main clock face.
- **Ultra-Fast & Responsive Architecture (Zero-Lag UI)**:
  - **Eliminated I2C Bus Starvation**: CST9220 touch sensor and AXP2101 PMIC share `I2C0`. Throttled PMIC power key reads from 1,000+ Hz to 150 ms intervals, freeing 99.9% of bus bandwidth for instant touch response.
  - **200 Hz Touch Scanning**: Reduced capacitive touch polling interval from 15 ms to 5 ms.
  - **Diff-Cached LVGL Rendering**: 48pt digits and UI widgets only invalidate when text/values actually change, eliminating ~80% of unnecessary CPU redraws.
  - **Enlarged QSPI DMA Buffers**: Expanded draw buffers from 30 lines to 48 lines, reducing frame flush chunks from 16 to 10 per render.
  - **Reduced I2C Timeouts**: Lowered bus lock timeouts from 5,000 ms to 50 ms.
- **Ultra-Low Power Battery Optimization**:
  - **Wi-Fi 802.11 Modem Sleep**: Radio sleeps between router beacon intervals, keeping HTTP access available while dropping radio current by ~85%.
  - **SoftAP Disabled on Connect**: Eliminates continuous 20 dBm AP beacon broadcasts.
  - **Audio Deep Standby (0 mA)**: ES8311 DAC and I2S channel are completely powered down when silent.
  - **Night Screen-OFF Mode**: OLED pixels draw 0.0 mA at night when dark; display automatically wakes upon touch or button press.
  - **Total Runtime**: **30 to 40+ hours** on a single 800 mAh LiPo charge.
- **Responsive Controls**:
  - **Touch to Wake**: Tap anywhere on the circular AMOLED glass to wake the display.
  - **Physical Buttons**:
    - **PWR Button (AXP2101 PEK)**: Instantly wakes display / dismisses active alarm.
    - **KEY Button (GPIO 10)**: Wakes display / dismisses active alarm.
    - **BOOT Button (GPIO 9)**: Wakes display / dismisses active alarm.
- **Embedded Web Management Portal**:
  - Host-accessible at `http://<device-ip>/` (e.g. `http://192.168.3.201/`).
  - Configure alarm time, sunrise duration, day/night brightness, timezone selector, manual region dropdown (30+ world regions), test alarms, and Wi-Fi credentials.

---

## 🛠️ Hardware Specifications

| Component | Specification | Details |
| :--- | :--- | :--- |
| **MCU** | ESP32-C6 RISC-V 32-bit | 160 MHz, Wi-Fi 6 (802.11ax), BLE 5, 4 MB Flash |
| **Display** | 2.16" Circular AMOLED | 480×480 resolution, SH8601 QSPI controller |
| **Touch** | Capacitive Multi-Touch | CST9220 I2C controller (`0x5A`) |
| **Audio** | ES8311 Low-Power DAC | I2C control (`0x18`) + I2S digital audio output |
| **PMIC** | AXP2101 Power Management | I2C (`0x34`), LiPo charging, fuel gauge, power key |
| **RTC** | PCF85063A Hardware RTC | I2C (`0x51`), coin cell backup support |

### Pinout Mapping

```
------------------------------------------------------
ESP32-C6 Pin   Peripheral        Function
------------------------------------------------------
GPIO 7         I2C0 SCL          I2C Clock (PMIC, RTC, ES8311, Touch)
GPIO 8         I2C0 SDA          I2C Data
GPIO 15        AMOLED CS         Display Chip Select (Active Low)
GPIO 0         AMOLED PCLK / CLK Display SPI Clock
GPIO 1         AMOLED D0         Display QSPI Data 0
GPIO 2         AMOLED D1         Display QSPI Data 1
GPIO 3         AMOLED D2         Display QSPI Data 2
GPIO 4         AMOLED D3         Display QSPI Data 3
GPIO 5         Touch INT         CST9220 Touch Interrupt
GPIO 11        Touch RST         CST9220 Touch Reset
GPIO 20        I2S SCLK / BCLK   ES8311 Audio Bit Clock
GPIO 19        I2S MCLK          ES8311 Master Clock
GPIO 22        I2S LCLK / WS     ES8311 Word Select / LR Clock
GPIO 23        I2S DOUT          ES8311 Audio Data Output
GPIO 10        KEY Button        Physical Tactile Key (Active Low)
GPIO 9         BOOT Button       Physical Boot Key (Active Low)
AXP2101 PEK    PWR Button        Power / Wake Button (IRQ managed)
------------------------------------------------------
```

---

## 🔋 Power Consumption Breakdown

| Power State | Current Draw | 800 mAh Battery Life |
| :--- | :---: | :---: |
| **Night Mode (Screen OFF, Audio 0 mA, Wi-Fi Modem Sleep)** | **~18–22 mA** | **36–44 Hours** |
| **Day Idle (Auto-Dimmed, Wi-Fi Modem Sleep)** | **~25–30 mA** | **26–32 Hours** |
| **Active Screen (90% Brightness)** | **~45–60 mA** | **13–17 Hours** |
| **Alarm Ringing (Max Brightness + Speaker Active)** | **~90–120 mA** | Transient |

---

## 📁 Project Structure

```
esp32-c6-alarm-clock/
├── include/
│   └── clock_config.h          # NVS persistent configuration manager
├── lib/
│   └── XPowersLib/             # AXP2101 PMIC driver library
├── src/
│   ├── main.cpp                # State machine, hardware buttons, boot sequence
│   ├── clock_ui.cpp            # LVGL 8.4 circular clock face, settings, sunrise UI
│   ├── clock_ui.h
│   ├── clock_weather.h         # Open-Meteo REST service & astronomical lunar phase engine
│   ├── clock_web.h             # Embedded responsive Web configuration portal
│   ├── clock_location.h        # IP Geolocation & POSIX DST dynamic timezone manager
│   ├── alarm_audio.cpp         # ES8311 DAC driver & algorithmic tone generator
│   ├── alarm_audio.h
│   ├── bsp_lvgl_port.cpp       # AMOLED SH8601 QSPI driver & LVGL display port
│   ├── bsp_lvgl_port.h
│   ├── lcd_touch.cpp           # CST9220 capacitive touch driver
│   ├── lcd_touch.h
│   ├── user_config.h           # Hardware pin definitions
│   └── port_bsp/
│       ├── axp2101_bsp.cpp     # PMIC power rail & PEKEY button driver
│       ├── axp2101_bsp.h
│       ├── pcf85063a.cpp       # Onboard hardware RTC driver
│       └── pcf85063a.h
├── platformio.ini              # PlatformIO build configuration
├── claude.md                   # AI Assistant & Architecture Reference Guide
└── README.md
```

---

## 🚀 Getting Started

### Prerequisites

1. Install [PlatformIO](https://platformio.org/) (via VS Code or CLI).
2. Install Python 3.10+.
3. Connect the Waveshare ESP32-C6 board via USB-C to your computer.

### Build and Flash

```bash
# Clone the repository
git clone https://github.com/YuvalHir/esp32-c6-alarm-clock.git
cd esp32-c6-alarm-clock

# Compile firmware
pio run

# Flash to device (replace COM3 with your detected port)
pio run -t upload

# Open serial monitor (115200 baud)
pio device monitor -b 115200
```

---

## 🌐 Web Dashboard Guide

Once connected to your local Wi-Fi, open your browser and navigate to:
```
http://<device-ip>/   (e.g., http://192.168.3.201/)
```

### Available Controls:
- **📍 Location & Internet Time (Automatic DST)**:
  - View detected location, active IANA timezone, and active POSIX DST rule.
  - Toggle auto-detection on Wi-Fi connect.
  - Select manual region from 30+ major world regions.
  - Force re-detection via IP geolocation.
- **🌤️ Real-Time Weather & Lunar Phase**:
  - Live temperature (&deg;C), sky conditions, day/night indicator, and moon phase.
  - Instant on-demand weather sync button.
- **⏰ Alarm Configuration**:
  - Set alarm hour (0–23) and minute (0–59).
  - Set sunrise duration (5, 10, 15, or 30 minutes).
  - Toggle alarm enabled / disabled.
- **🔋 Battery & AMOLED Brightness**:
  - Configure Day Active Brightness (10–100%).
  - Configure Night Inactivity Display (0% = Completely OFF for maximum battery, or 5–25% faint glow).
- **☀️ Instant Preview & Audio Test**:
  - Trigger a 20-second simulated sunrise cycle to preview colors and lighting.
  - Test the onboard speaker alarm chime.
- **📶 Wi-Fi Configuration**:
  - Update SSID and password directly over the air.

---

## 📜 License

MIT License. Free for personal and educational use.
