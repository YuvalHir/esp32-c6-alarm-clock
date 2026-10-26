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

1. **USB-Plugged Always-On Portal & Dynamic Wi-Fi Management**:
   - **On USB Power (VBUS in / Charging)**:
     - Wi-Fi radio powers **ON** automatically.
     - Runs in full-performance mode (`WiFi.setSleep(false)`, `WIFI_PS_NONE`) for instant, zero-latency Web Portal responses (`http://192.168.3.201/`).
     - WebServer is active and accepts configuration changes, alarm saves, and test previews.
   - **On Battery (Unplugged)**:
     - Wi-Fi radio is immediately and completely powered **OFF** (`esp_wifi_stop()`, `WiFi.mode(WIFI_OFF)`), saving **50–80 mA** continuous current (>60% of device idle draw).
     - WebServer is suspended to conserve CPU cycles.
     - Status bar indicates `LV_SYMBOL_POWER " Batt Mode"` in muted slate grey.
     - Timekeeping is maintained with ±2ppm precision (<0.1s drift per day) by the onboard **PCF85063A hardware RTC** independently of network.
   - **Dynamic Transition**:
     - System polls `Axp2101_IsVbusIn()` every 2 seconds in `main.cpp`, transitioning seamlessly between USB Always-On Portal mode and 0 mA Battery Sleep mode upon cable insertion or removal.

2. **Display Refresh Throttling (Option A)**:
   - When running on battery and screen is dimmed, seconds are frozen to `":.."` and display redraws drop from 60 frames/min to **1 frame/min** (on the minute rollover), reducing QSPI bus activity by **98.3%**.
   - Touching the screen or pressing any hardware button immediately restores live ticking seconds (`:01`, `:02`...) for the duration of the awake timeout.
   - Micro-animation timers (`timer_weather_anim` at 20 Hz) are automatically paused via `lv_timer_pause()` when entering dim mode and resumed via `lv_timer_resume()` on activity.

3. **ES8311 Codec Deep Standby (0 mA)**:
   - When audio is idle: Call `es8311_power_down()`.
     - DAC Mute (`0x31 = 0x60`), Power down DAC circuit (`0x12 = 0x3F`), Power down analog blocks (`0x0E = 0xFF`).
     - Disable I2S channel (`i2s_channel_disable(tx_handle)`), stopping MCLK, BCLK, and WS clocks entirely (saves ~15 mA).
   - When audio starts: Call `es8311_power_up()`.
     - Enable I2S channel (`i2s_channel_enable(tx_handle)`).
     - Wait 15ms (`vTaskDelay(pdMS_TO_TICKS(15))`) for MCLK to stabilize for the ES8311 internal PLL.
     - Re-enable analog circuits (`0x0E = 0x00`), DAC (`0x12 = 0x00`), CSM state machine (`0x0D = 0x01`), set volume (`0x32 = current_vol_reg`), and unmute (`0x31 = 0x00`).
   - Thread safety: `alarm_audio_stop()` resets playing flags and allows `audio_task` to handle clean powering down without cross-thread I2S handle contention.

4. **Night Screen-OFF Mode & AMOLED True-Black**:
   - OLED displays emit light per-pixel. True black (`#000000`) draws 0 mA pixel emission.
   - Backgrounds for night stage, dimmed face, and modal dialogs are `#000000`.
   - `Lcd_SetBacklight(0)` sets DCS register `0x51` to `0x00`, cutting pixel luminance entirely.
   - Touching the screen or pressing KEY, BOOT, or PWR wakes the display.

5. **AXP2101 Power Rails**:
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
- **Status Bar Margins**:
  - `lbl_wifi`: `LV_ALIGN_TOP_LEFT, 34, 14` (clears curved top-left bezel).
  - `lbl_batt`: `LV_ALIGN_TOP_RIGHT, -34, 14` (clears curved top-right bezel).
- **Center Positioning**: Always position controls relative to `LV_ALIGN_CENTER` with explicit offsets rather than corner alignments.
- **Font Declarations**:
  - `lv_conf.h` has `LV_FONT_MONTSERRAT_18`, `20`, `24`, `32`, `48` enabled.
  - `LV_FONT_MONTSERRAT_16` is DISABLED (0). Use `&lv_font_montserrat_18` or `&lv_font_montserrat_20` instead.

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

## 6. Zero-Lag Performance & I2C Bus Management Rules

- **Shared I2C Bus Bottleneck (`I2C_NUM_0`)**:
  - The CST9220 touch sensor and AXP2101 PMIC share GPIO 7 (SCL) and GPIO 8 (SDA).
  - Calling `Axp2101_WasPowerButtonPressed()` executes 3 synchronous I2C read transactions (`0x48`, `0x49`, `0x4A`).
  - **CRITICAL RULE**: NEVER poll AXP2101 PMIC registers unconditionally on every `loop()` cycle. Throttle PMIC polling to $\ge 150\text{ ms}$ intervals. Discrete GPIO buttons (`KEY` on GPIO 10, `BOOT` on GPIO 9) remain 0-latency hardware reads.
  - CST9220 touch polling interval in `src/lcd_touch.cpp` is tuned to $5\text{ ms}$ (up to 200 Hz scan rate).
- **LVGL Diff-Caching**:
  - Never call `lv_label_set_text` or re-style widgets repeatedly unless values have actually changed. Doing so marks LVGL widgets dirty and triggers expensive full font re-rasterization.
  - Clock time (`clock_ui_update_time`), battery status (`clock_ui_update_status`), and weather/moon status (`clock_ui_update_weather`) implement strict diff-caching guards that return in nanoseconds without locking LVGL when values are static.

---

## 7. Weather & Astronomical Lunar Phase Engine

- **Open-Meteo REST Service (`src/clock_weather.h`)**:
  - Zero API key required, non-commercial open weather forecasts.
  - Query: `http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,is_day`.
  - Target object: Always parse the `"current": { ... }` object to avoid matching text labels inside `"current_units"`.
- **Astronomical Lunar Engine**:
  - Meeus/Conway algorithm calculates exact lunar age ($0.0 \dots 29.53$ days) and Julian day fraction.
  - Automatically renders 8 distinct moon phases at night on the circular watchface using a dual-circle shadow mask.

---

## 8. Build & Flashing Instructions (PlatformIO & esptool)

### Environment
- **Platform**: `https://github.com/pioarduino/platform-espressif32.git`
- **Board**: `esp32-c6-devkitm-1`
- **Framework**: `arduino`

### Compilation
```powershell
pio run -e esp32-c6-touch-amoled-216
```

### Direct Flashing (Avoids Windows cp1252 Unicode upload crash)
On Windows, PlatformIO's Python click console wrapper can fail when rendering Unicode block progress characters under cp1252. Flash directly and reliably using `esptool`:
```powershell
python -m esptool --chip esp32c6 --port COM3 --baud 921600 write-flash --flash-size 16MB 0x0 .pio/build/esp32-c6-touch-amoled-216/firmware.factory.bin
```

### Serial Monitoring
- Baud rate: `115200`.
- The ESP32-C6 uses built-in USB-JTAG. Do NOT pulse DTR/RTS when opening the serial port in Python or monitoring tools unless an explicit reset is desired:
  ```python
  ser = serial.Serial()
  ser.port = 'COM3'
  ser.baudrate = 115200
  ser.dtr = False
  ser.rts = False
  ser.open()
  ```

---

## 9. Web Management Server API Endpoints

- `GET /` — Responsive mobile-friendly HTML5 control dashboard.
- `POST /save_alarm` — Form args: `hour` (0–23), `min` (0–59), `sun_min` (5, 10, 15, 30), `enabled` (0/1).
- `POST /save_location` — Form args: `auto_loc` (0/1), `tz_iana` (IANA identifier e.g. `Asia/Jerusalem`).
- `POST /detect_location` — Triggers instant background IP geolocation re-detection.
- `POST /refresh_weather` — Manually triggers instant Open-Meteo weather and moon phase refresh.
- `POST /save_brightness` — Form args: `brightness` (10–100), `night_dim` (0–25, where 0 = Completely OFF).
- `POST /test_sunrise` — Starts 20-second sunrise visual animation.
- `POST /test_sound` — Plays 4-tone speaker chime test.
- `POST /save_wifi` — Form args: `ssid`, `pass`.

