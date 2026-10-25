# CLAUDE.md - Developer & Agent Guide for ESP32-C6 Smart Sunrise Alarm Clock

This guide documents the architecture, hardware specifics, coding conventions, power-saving mechanisms, and debugging rules for the **Waveshare ESP32-C6-Touch-AMOLED-2.16** Smart Sunrise Alarm Clock project.

---

## 1. System Architecture & Hardware Overview

### Target Board: Waveshare ESP32-C6-Touch-AMOLED-2.16
- **MCU**: ESP32-C6 (32-bit RISC-V single-core, 160 MHz, 320 KB SRAM, 4 MB Flash).
- **Display**: 2.16" Circular AMOLED (480×480 pixels), controlled by SH8601 via QSPI (`SPI2_HOST`).
- **Touch**: Capacitive Touch controller CST9220 on I2C address `0x5A`.
- **Power Management (PMIC)**: X-Powers AXP2101 on I2C address `0x34`.
- **Real-Time Clock (RTC)**: PCF85063A on I2C address `0x51`.
- **Audio Codec**: Everest Semiconductor ES8311 on I2C address `0x18` + ESP-IDF I2S channel.

### Pinout Matrix
| Function | GPIO Pin | Bus / Interface | Notes |
| :--- | :---: | :---: | :--- |
| **I2C SCL** | GPIO 7 | `I2C_NUM_0` | 100 kHz pull-up, shared by PMIC, RTC, Touch, ES8311 |
| **I2C SDA** | GPIO 8 | `I2C_NUM_0` | Shared I2C data |
| **AMOLED CS** | GPIO 15 | QSPI | Active Low |
| **AMOLED PCLK** | GPIO 0 | QSPI | 40 MHz clock |
| **AMOLED D0-D3** | GPIO 1, 2, 3, 4 | QSPI | 4-bit data bus |
| **Touch INT** | GPIO 5 | Discrete | Active Low interrupt from CST9220 |
| **Touch RST** | GPIO 11 | Discrete | Reset pin for CST9220 |
| **I2S SCLK** | GPIO 20 | I2S Standard | Bit Clock (16 kHz, 16-bit stereo) |
| **I2S MCLK** | GPIO 19 | I2S Standard | Master Clock |
| **I2S LCLK / WS** | GPIO 22 | I2S Standard | Word Select / LR Clock |
| **I2S DOUT** | GPIO 23 | I2S Standard | Serial Data Output to ES8311 |
| **KEY Button** | GPIO 10 | Discrete | Hardware push-button (Active Low, internal pull-up) |
| **BOOT Button** | GPIO 9 | Discrete | Hardware boot push-button (Active Low) |
| **PWR Button** | AXP2101 PEK | PMIC IRQ | Power Enable Key managed via AXP2101 PEKEY IRQ |

---

## 2. Memory & Heap Management Constraints

- **Total SRAM**: 320 KB.
- **DMA Allocation**:
  - LVGL double-draw buffer uses `heap_caps_malloc(..., MALLOC_CAP_DMA)`.
  - Buffer size is constrained to **30 lines** ($480 \times 30 \times 2 = 28,800$ bytes per buffer, $\times 2 \approx 57.6\text{ KB}$).
  - Do NOT increase `buf_lines` beyond 35 lines or the heap will fragment and fail Wi-Fi SSL/HTTP allocations!
- **LVGL Mutex Safety**:
  - Always acquire `bsp_lvgl_lock(timeout_ms)` before any `lv_*` function calls if executed outside the main loop thread, and always pair with `bsp_lvgl_unlock()`.

---

## 3. Power-Saving Architecture (Target: 30–40+ Hours on 800 mAh)

1. **Wi-Fi 802.11 Modem Sleep**:
   - MUST keep `WiFi.setSleep(true)` and `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`.
   - Never disable Wi-Fi sleep in `main.cpp`. The router buffers packets and notifies the ESP32 in DTIM beacons, allowing HTTP requests to serve instantly while saving ~100 mA.
   - Once connected to STA mode, disable SoftAP (`WiFi.mode(WIFI_STA)`), which saves ~90 mA of 20 dBm beacon transmission power.

2. **ES8311 Codec Deep Standby (0 mA)**:
   - In `alarm_audio.cpp`: When audio stops, call `es8311_power_down()`.
   - Sequence: Mute DAC (`0x31 = 0x60`), Power down DAC circuit (`0x12 = 0x3F`), Power down analog blocks (`0x0E = 0xFF`), and call `i2s_channel_disable(tx_handle)`.
   - Wake sequence: `es8311_power_up()` re-enables analog blocks, un-powers down DAC, unmutes, and calls `i2s_channel_enable(tx_handle)`.

3. **Night Screen-OFF Mode**:
   - OLED displays emit light per-pixel. Black (`#000000`) draws 0 mA.
   - `Lcd_SetBacklight(0)` sets DCS register `0x51` to `0x00`, cutting pixel luminance entirely.
   - During night hours (22:00 – 06:30), 7 seconds of inactivity turns the display completely OFF.
   - Touching the screen or pressing KEY, BOOT, or PWR triggers `clock_ui_handle_activity()`, waking the display for 7 seconds.

4. **AXP2101 Power Rails**:
   - `ALDO1`: 3.3V to ES8311 Audio Codec.
   - `ALDO2`: 3.3V auxiliary.
   - `ALDO3`: 3.3V AMOLED display logic reset.
   - `ALDO4`: 3.3V peripherals & touch sensor.
   - `DCDC1`: 3.3V system core rail.

---

## 4. Circular AMOLED (480×480) UI Design Rules

- **Radius Limit**: The screen has a radius of $R = 240$ px.
- **Safe Zone Rule**: Any interactive button centered at $(X, Y)$ with width $W$ and height $H$ MUST satisfy:
  $$\sqrt{(|X| + W/2)^2 + (|Y| + H/2)^2} < 235\text{ px}$$
  Elements placed beyond $R \approx 235$ px will clip against the physical round metal bezel.
- **Center Positioning**: Always position controls relative to `LV_ALIGN_CENTER` with explicit offsets rather than corner alignments (`LV_ALIGN_BOTTOM_LEFT`, etc.).
- **Font Declarations**:
  - `lv_conf.h` has `LV_FONT_MONTSERRAT_18`, `20`, `24`, `32`, `48` enabled.
  - `LV_FONT_MONTSERRAT_16` is DISABLED (0). Use `&lv_font_montserrat_18` instead.

---

## 5. POSIX Timezone & Daylight Saving Time (DST) Rule

- **POSIX Rule Format**: `STDoffset[DST[offset],start[/time],end[/time]]`
- **Israel Standard & Daylight Time**:
  - `IST-2IDT,M3.4.4/26,M10.5.0`
  - `IST-2`: Standard time is IST (UTC+2).
  - `IDT`: Daylight time is IDT (UTC+3, offset +1h implied).
  - `M3.4.4/26`: DST starts Month 3 (March), Week 4, Day 4 (Thursday night before last Friday), at 02:00 AM (local 26:00).
  - `M10.5.0`: DST ends Month 10 (October), Week 5 (last), Day 0 (Sunday) at 02:00 AM.
- **Initialization Order Rule**:
  - `ClockConfigManager::init()` and `ClockLocationManager::apply_tz()` MUST be called in `setup()` **BEFORE** reading the hardware RTC with `pcf85063a_get_time_date()` and `mktime(&t)`.
  - Calling `mktime()` before `setenv("TZ", ...)` causes `mktime` to assume UTC, corrupting the system clock by +2 hours on the next boot!

---

## 6. Build & Flashing Instructions (PlatformIO)

### Environment
- **Platform**: `https://github.com/pioarduino/platform-espressif32.git`
- **Board**: `esp32-c6-devkitm-1`
- **Framework**: `arduino`

### Windows Console UTF-8 Notice
On Windows, PlatformIO's Python CLI output uses Unicode progress characters. In PowerShell, ensure UTF-8 encoding before building/flashing:
```powershell
$env:PYTHONIOENCODING="utf-8"
chcp 65001
pio run -t upload
```

### Serial Monitoring
- Baud rate: `115200`.
- The ESP32-C6 uses built-in USB-JTAG. Do NOT pulse DTR/RTS when opening the serial port in Python or monitoring tools unless an explicit reset is desired:
  ```python
  ser = serial.Serial('COM3', 115200)
  ser.setDTR(False)
  ser.setRTS(False)
  ```

---

## 7. Web Management Server API Endpoints

- `GET /` — Responsive mobile-friendly HTML5 control dashboard.
- `POST /save_alarm` — Form args: `hour` (0–23), `min` (0–59), `sun_min` (5, 10, 15, 30), `enabled` (0/1).
- `POST /save_location` — Form args: `auto_loc` (0/1), `tz_iana` (IANA identifier e.g. `Asia/Jerusalem`).
- `POST /detect_location` — Triggers instant background IP geolocation re-detection.
- `POST /save_brightness` — Form args: `brightness` (10–100), `night_dim` (0–25, where 0 = Completely OFF).
- `POST /test_sunrise` — Starts 20-second sunrise visual animation.
- `POST /test_sound` — Plays 4-tone speaker chime test.
- `POST /save_wifi` — Form args: `ssid`, `pass`.
