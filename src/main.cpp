#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "time.h"
#include "esp_sntp.h"

#include "bsp_lvgl_port.h"
#include "port_bsp/i2c_bsp.h"
#include "port_bsp/axp2101_bsp.h"
#include "port_bsp/pcf85063a.h"

#include "clock_config.h"
#include "clock_location.h"
#include "clock_ui.h"
#include "clock_web.h"
#include "clock_weather.h"
#include "alarm_audio.h"

I2cMasterBus I2cMasterBus_(GPIO_NUM_7, GPIO_NUM_8, I2C_NUM_0);
static pcf85063a_dev_t rtc_dev;
static bool rtc_initialized = false;
static bool ntp_synced = false;

// State Machine for Sunrise & Alarm
enum AlarmClockState {
    STATE_CLOCK,
    STATE_SUNRISE,
    STATE_ALARM_FIRING
};

static AlarmClockState clock_state = STATE_CLOCK;
static bool is_test_sunrise = false;
static unsigned long test_start_ms = 0;
static unsigned long test_alarm_start_ms = 0;
static const unsigned long TEST_SUNRISE_DURATION_MS = 20000; // 20 seconds preview

static bool snooze_active = false;
static uint8_t snooze_hour = 0;
static uint8_t snooze_min = 0;
static bool alarm_fired_today = false;

// Callbacks from UI
void handleAlarmAction(bool is_snooze) {
    alarm_audio_stop();

    if (is_snooze) {
        // Calculate snooze time: current min + 9
        time_t now = time(nullptr);
        struct tm *t = localtime(&now);
        int total_min = t->tm_hour * 60 + t->tm_min + 9;
        snooze_hour = (total_min / 60) % 24;
        snooze_min = total_min % 60;
        snooze_active = true;
        Serial.printf("[Alarm] Snoozed until %02d:%02d\n", snooze_hour, snooze_min);
    } else {
        snooze_active = false;
        alarm_fired_today = true;
        Serial.println("[Alarm Action] Dismissed -> Audio stopped, UI returned to Clock");
    }

    clock_state = STATE_CLOCK;
    is_test_sunrise = false;
    test_alarm_start_ms = 0;
    clock_ui_dismiss_alarm();
}

void handleSettingsSaved() {
    ClockLocationManager::apply_tz(ClockConfigManager::settings.posix_tz);
    clock_ui_update_alarm_pill(
        ClockConfigManager::settings.alarm_enabled,
        ClockConfigManager::settings.alarm_hour,
        ClockConfigManager::settings.alarm_minute,
        ClockConfigManager::settings.sunrise_minutes,
        ClockConfigManager::settings.is_24h_format
    );
    // Only reset fired flag if alarm time actually changed after initialization
    static int last_saved_h = -1;
    static int last_saved_m = -1;
    if (last_saved_h != ClockConfigManager::settings.alarm_hour || last_saved_m != ClockConfigManager::settings.alarm_minute) {
        if (last_saved_h != -1) {
            alarm_fired_today = false;
        }
        last_saved_h = ClockConfigManager::settings.alarm_hour;
        last_saved_m = ClockConfigManager::settings.alarm_minute;
    }
    Serial.println("[Settings] Alarm and Location configuration updated and saved");
}

void handleTestSunrise() {
    Serial.println("[Sunrise] Starting 20-second Sunrise Simulation Preview");
    clock_state = STATE_SUNRISE;
    is_test_sunrise = true;
    test_start_ms = millis();
    test_alarm_start_ms = 0;
    clock_ui_start_sunrise(TEST_SUNRISE_DURATION_MS / 1000);
}

// NTP time synchronization callback
void timeavailable(struct timeval *t) {
    Serial.println("[NTP] Time successfully synchronized from NTP server!");
    ntp_synced = true;

    // Update hardware RTC with the accurate network time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        if (rtc_initialized) {
            pcf85063a_datetime_t dt = {
                .year = (uint16_t)(timeinfo.tm_year + 1900),
                .month = (uint8_t)(timeinfo.tm_mon + 1),
                .day = (uint8_t)timeinfo.tm_mday,
                .dotw = (uint8_t)timeinfo.tm_wday,
                .hour = (uint8_t)timeinfo.tm_hour,
                .min = (uint8_t)timeinfo.tm_min,
                .sec = (uint8_t)timeinfo.tm_sec
            };
            pcf85063a_set_time_date(&rtc_dev, dt);
            Serial.printf("[RTC] Synced hardware RTC to: %04d-%02d-%02d %02d:%02d:%02d\n",
                          dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
        }
    }
}

// Physical Hardware Buttons: KEY = GPIO 10, BOOT = GPIO 9, PWR = AXP2101 PEKEY
static const int PIN_BTN_KEY = 10;
static const int PIN_BTN_BOOT = 9;

static void check_physical_buttons() {
    static int last_key = HIGH;
    static int last_boot = HIGH;
    static unsigned long last_btn_time = 0;
    unsigned long now = millis();

    int key_state = digitalRead(PIN_BTN_KEY);
    int boot_state = digitalRead(PIN_BTN_BOOT);

    // Only poll AXP2101 PMIC over I2C every 150ms to keep I2C bus 100% free for touch responsiveness!
    bool pwr_pressed = false;
    static unsigned long last_pwr_poll = 0;
    if (now - last_pwr_poll >= 150) {
        last_pwr_poll = now;
        pwr_pressed = Axp2101_WasPowerButtonPressed();
    }

    bool button_triggered = false;
    const char *btn_name = "";

    if (key_state == LOW && last_key == HIGH && (now - last_btn_time >= 150)) {
        button_triggered = true;
        btn_name = "KEY (GPIO 10)";
    }
    last_key = key_state;

    if (boot_state == LOW && last_boot == HIGH && (now - last_btn_time >= 150)) {
        button_triggered = true;
        btn_name = "BOOT (GPIO 9)";
    }
    last_boot = boot_state;

    if (pwr_pressed && (now - last_btn_time >= 150)) {
        button_triggered = true;
        btn_name = "PWR (AXP2101 Power Button)";
    }

    if (button_triggered) {
        last_btn_time = now;
        Serial.printf("[Button] %s Pressed\n", btn_name);

        if (clock_state == STATE_ALARM_FIRING) {
            Serial.println("[Button] Alarm Ringing -> Dismissing Alarm");
            handleAlarmAction(false); // Dismiss
        } else if (clock_state == STATE_SUNRISE) {
            Serial.println("[Button] Sunrise Simulation Active -> Dismissing");
            handleAlarmAction(false); // Cleanly dismiss, stop audio if any, and set alarm_fired_today
        } else {
            // Normal Clock Mode: Wake screen immediately or keep awake
            if (clock_ui_is_dimmed()) {
                Serial.println("[Button] Screen is sleeping/dimmed -> Waking up!");
            }
            clock_ui_handle_activity();
        }
    }
}

// Power-Aware Wi-Fi Management:
// Wi-Fi radio and Web Portal operate ONLY when USB cable is connected!
static bool wifi_is_powered = false;

static void wifi_power_on() {
    if (wifi_is_powered) return;
    wifi_is_powered = true;
    Serial.println("[Power] USB Cable Plugged In -> Enabling Wi-Fi Radio & Web Portal");
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false); // USB powered: full performance and instant web responses
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (strlen(ClockConfigManager::settings.wifi_ssid) > 0) {
        WiFi.mode(WIFI_STA);
        Serial.printf("[Wi-Fi] Connecting in STA mode to '%s' (SoftAP off for power saving)...\n", ClockConfigManager::settings.wifi_ssid);
        WiFi.begin(ClockConfigManager::settings.wifi_ssid, ClockConfigManager::settings.wifi_pass);
    } else {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("ESP32-Sunrise-Clock", "12345678");
        Serial.print("[Wi-Fi] SoftAP Started. Connect to 'ESP32-Sunrise-Clock' at IP: ");
        Serial.println(WiFi.softAPIP());
    }
    ClockWebServer::init();
}

static void wifi_power_off() {
    if (!wifi_is_powered) return;
    wifi_is_powered = false;
    Serial.println("[Power] Running on Battery -> Disabling Wi-Fi Radio (0mA draw)");
    ClockWebServer::stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n==============================================");
    Serial.println("  Smart Sunrise Alarm Clock - Waveshare ESP32-C6");
    Serial.println("==============================================");

    // Configure physical buttons (KEY = GPIO 10, BOOT = GPIO 9)
    pinMode(PIN_BTN_KEY, INPUT_PULLUP);
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

    // 1. Load saved preferences from NVS & apply POSIX Timezone FIRST
    ClockConfigManager::init();
    ClockWeatherManager::init();
    sntp_set_time_sync_notification_cb(timeavailable);
    ClockLocationManager::apply_tz(ClockConfigManager::settings.posix_tz);

    // 2. Initialize PMIC (AXP2101) to supply power to AMOLED and peripherals
    Custom_PmicPortInit(&I2cMasterBus_, 0x34);

    // 3. Initialize Onboard Hardware Real-Time Clock (PCF85063A)
    esp_err_t rtc_err = pcf85063a_init(&rtc_dev, I2cMasterBus_.Get_I2cBusHandle(), PCF85063A_ADDRESS);
    if (rtc_err == ESP_OK) {
        rtc_initialized = true;
        Serial.println("[RTC] PCF85063A Real-Time Clock initialized successfully");

        // Read RTC time into system time initially
        pcf85063a_datetime_t dt = {};
        if (pcf85063a_get_time_date(&rtc_dev, &dt) == ESP_OK && dt.year >= 2024) {
            struct tm t;
            t.tm_year = dt.year - 1900;
            t.tm_mon = dt.month - 1;
            t.tm_mday = dt.day;
            t.tm_hour = dt.hour;
            t.tm_min = dt.min;
            t.tm_sec = dt.sec;
            t.tm_isdst = -1;
            time_t time_since_epoch = mktime(&t);
            struct timeval now_tv = { .tv_sec = time_since_epoch, .tv_usec = 0 };
            settimeofday(&now_tv, NULL);
            Serial.printf("[RTC] System time set from RTC: %02d:%02d:%02d\n", dt.hour, dt.min, dt.sec);
        }
    } else {
        Serial.printf("[RTC Error] PCF85063A init failed (err: %d)\n", rtc_err);
    }

    // 4. Initialize Audio Codec (ES8311 I2C + I2S)
    alarm_audio_init(I2cMasterBus_);

    // 4. Initialize AMOLED Display (CO5300 QSPI), CST9220 Touch, and LVGL
    Serial.println("[Display] Initializing AMOLED and LVGL...");
    bsp_lvgl_init(I2cMasterBus_);
    Lcd_SetBacklight(ClockConfigManager::settings.day_brightness);

    // 5. Build the UI
    ClockWeatherManager::init();
    Serial.println("[UI] Building Clock UI...");
    clock_ui_init();
    clock_ui_set_callbacks(handleAlarmAction, handleSettingsSaved, handleTestSunrise);
    clock_ui_update_alarm_pill(
        ClockConfigManager::settings.alarm_enabled,
        ClockConfigManager::settings.alarm_hour,
        ClockConfigManager::settings.alarm_minute,
        ClockConfigManager::settings.sunrise_minutes,
        ClockConfigManager::settings.is_24h_format
    );
    int init_batt_mv = Axp2101_GetBattVoltage();
    int init_batt_pct = Axp2101_GetBatteryPercent();
    bool init_charging = Axp2101_IsCharging();
    bool init_vbus = Axp2101_IsVbusIn();
    bool is_plugged_usb = init_vbus || init_charging || (init_batt_pct < 0);
    clock_ui_update_status(false, is_plugged_usb ? "Connecting..." : "Batt Mode", init_batt_pct, init_batt_mv, init_charging, init_vbus);
    Serial.println("[UI] Clock UI ready!");

    // 6. Wi-Fi Configuration (Always-On Portal only when USB-Plugged)
    if (is_plugged_usb) {
        wifi_power_on();
    } else {
        Serial.println("[Power] Booted on Battery -> Wi-Fi Radio OFF (0mA idle current)");
        WiFi.mode(WIFI_OFF);
        esp_wifi_stop();
        wifi_is_powered = false;
    }

    Serial.println("[System] Smart Sunrise Alarm Clock Ready!");
}

void loop() {
    // 1. Process LVGL display rendering & touch events
    bsp_lvgl_update();

    // 2. Physical Button Handling (KEY = GPIO 10 [+], BOOT = GPIO 9 [-])
    check_physical_buttons();

    // 3. Auto-dimming check (dims after inactivity timeout)
    clock_ui_check_inactivity();

    static unsigned long last_update_ms = 0;
    static unsigned long last_status_ms = 0;
    unsigned long now_ms = millis();

    // Handle incoming HTTP requests on Web Dashboard (Only when USB plugged & Wi-Fi active)
    if (wifi_is_powered) {
        ClockWebServer::update();

        if (ClockWebServer::checkTestRequested()) {
            handleTestSunrise();
        }
        if (ClockWebServer::checkTestSoundRequested()) {
            alarm_audio_play_test();
        }
        if (ClockWebServer::checkSettingsUpdated()) {
            handleSettingsSaved();
        }
    }

    // Check battery & connection status every 2 seconds
    if (now_ms - last_status_ms >= 2000) {
        last_status_ms = now_ms;

        int batt_mv = Axp2101_GetBattVoltage();
        int batt_percent = Axp2101_GetBatteryPercent();
        bool charging = Axp2101_IsCharging();
        bool vbus_in = Axp2101_IsVbusIn();
        bool usb_plugged = vbus_in || charging || (batt_percent < 0);

        // Power-Aware Dynamic Wi-Fi Management
        if (usb_plugged && !wifi_is_powered) {
            wifi_power_on();
        } else if (!usb_plugged && wifi_is_powered) {
            wifi_power_off();
        }

        static bool last_wifi_state = false;
        bool wifi_conn = wifi_is_powered && (WiFi.status() == WL_CONNECTED);
        if (wifi_conn && !last_wifi_state) {
            Serial.printf("[Wi-Fi] Connected to %s! IP: %s\n", 
                          ClockConfigManager::settings.wifi_ssid, 
                          WiFi.localIP().toString().c_str());
            last_wifi_state = true;

            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
            Serial.println("[Power] USB Connected -> Full-Speed Wi-Fi & Web Portal active");

            // Auto-detect location & timezone via IP if enabled
            if (ClockConfigManager::settings.auto_location) {
                ClockLocationManager::detect_location();
            }
            // Fetch live weather from Open-Meteo
            ClockWeatherManager::fetch_weather(ClockConfigManager::settings.latitude, ClockConfigManager::settings.longitude);
        } else if (!wifi_conn && last_wifi_state) {
            last_wifi_state = false;
        }

        String wifi_str;
        if (wifi_conn) {
            wifi_str = WiFi.localIP().toString();
        } else if (wifi_is_powered) {
            wifi_str = "AP: Clock";
        } else {
            wifi_str = "Batt Mode";
        }

        clock_ui_update_status(wifi_conn, wifi_str.c_str(), batt_percent, batt_mv, charging, vbus_in);
    }

    // Weather periodic refresh (every 30 minutes) or on tap-to-refresh
    static unsigned long last_weather_attempt_ms = 0;
    bool tap_weather_refresh = clock_ui_check_weather_refresh_requested();
    if (wifi_is_powered && WiFi.status() == WL_CONNECTED) {
        bool should_fetch = tap_weather_refresh;
        if (!should_fetch) {
            if (!ClockWeatherManager::data.valid && (now_ms - last_weather_attempt_ms >= 30000 || last_weather_attempt_ms == 0)) {
                should_fetch = true;
            } else if (ClockWeatherManager::data.valid && (now_ms - ClockWeatherManager::data.last_fetch_ms >= 1800000)) {
                should_fetch = true;
            }
        }
        if (should_fetch) {
            last_weather_attempt_ms = now_ms;
            ClockWeatherManager::fetch_weather(ClockConfigManager::settings.latitude, ClockConfigManager::settings.longitude);
        }
    }

    // Main Clock / Sunrise / Alarm State Machine (Runs every 200ms)
    if (now_ms - last_update_ms >= 200) {
        last_update_ms = now_ms;

        // Current system time
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            // Fallback if not set
            timeinfo.tm_hour = 12;
            timeinfo.tm_min = 0;
            timeinfo.tm_sec = 0;
            timeinfo.tm_wday = 0;
            timeinfo.tm_mday = 1;
            timeinfo.tm_mon = 0;
            timeinfo.tm_year = 126;
        }

        const char *day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char date_buf[48];
        snprintf(date_buf, sizeof(date_buf), "%s, %s %d", 
                 day_names[timeinfo.tm_wday], month_names[timeinfo.tm_mon], timeinfo.tm_mday);

        // State Machine
        if (clock_state == STATE_CLOCK) {
            clock_ui_update_time(
                timeinfo.tm_hour, 
                timeinfo.tm_min, 
                timeinfo.tm_sec, 
                date_buf, 
                ClockConfigManager::settings.is_24h_format
            );

            // Night mode check: transition between day & night colors (based on real astronomical sunrise/sunset or evening hours)
            static bool last_is_night = false;
            static bool night_initialized = false;
            bool is_night = (ClockWeatherManager::data.valid ? !ClockWeatherManager::data.is_day : (timeinfo.tm_hour >= 18 || timeinfo.tm_hour < 6));
            if (!ClockWeatherManager::data.valid) {
                ClockWeatherManager::data.is_day = !is_night;
                if (is_night) {
                    strcpy(ClockWeatherManager::data.condition_text, "Clear Night");
                }
            }
            if (!night_initialized || is_night != last_is_night) {
                night_initialized = true;
                last_is_night = is_night;
                clock_ui_set_night_mode(is_night);
            }

            // Calculate Astronomical Moon Phase & Update Weather / Moon Pill
            ClockWeatherManager::calculate_moon(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
            clock_ui_update_weather(
                ClockWeatherManager::data.temperature,
                ClockWeatherManager::data.weather_code,
                ClockWeatherManager::data.is_day,
                ClockWeatherManager::data.condition_text,
                ClockWeatherManager::data.moon_phase_text,
                ClockWeatherManager::data.moon_phase_code
            );

            static unsigned long last_serial_ms = 0;
            if (now_ms - last_serial_ms >= 10000) {
                last_serial_ms = now_ms;
                int batt_mv = Axp2101_GetBattVoltage();
                int batt_percent = Axp2101_GetBatteryPercent();
                bool wifi_conn = wifi_is_powered && (WiFi.status() == WL_CONNECTED);
                String wifi_str = wifi_conn ? WiFi.localIP().toString() : (wifi_is_powered ? "AP: Clock" : "Batt Mode (Wi-Fi OFF)");
                Serial.printf("[Clock] Local Time: %02d:%02d:%02d | %s | WiFi: %s | Batt: %d%% (%dmV) | Brightness: %d%% (Dimmed: %s) | Location: %s (%s)\n",
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                              date_buf,
                              wifi_str.c_str(), batt_percent, batt_mv, 
                              ClockConfigManager::settings.day_brightness,
                              clock_ui_is_dimmed() ? "YES" : "NO",
                              ClockConfigManager::settings.location_str,
                              ClockConfigManager::settings.timezone_name);
            }

            // Reset fired flag on calendar day rollover (fires once per day)
            static int last_alarm_day = -1;
            if (last_alarm_day != timeinfo.tm_mday) {
                last_alarm_day = timeinfo.tm_mday;
                alarm_fired_today = false;
            }

            // Check if alarm or snooze should trigger
            if (ClockConfigManager::settings.alarm_enabled) {
                int target_h = snooze_active ? snooze_hour : ClockConfigManager::settings.alarm_hour;
                int target_m = snooze_active ? snooze_min : ClockConfigManager::settings.alarm_minute;

                int now_total_sec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
                int target_total_sec = target_h * 3600 + target_m * 60;
                int diff_sec = target_total_sec - now_total_sec;

                int sunrise_total_sec = ClockConfigManager::settings.sunrise_minutes * 60;

                // If within sunrise window and not snoozed/fired
                if (!snooze_active && !alarm_fired_today && diff_sec > 0 && diff_sec <= sunrise_total_sec) {
                    Serial.println("[Sunrise] Initiating natural morning sunrise wake-up simulation!");
                    clock_state = STATE_SUNRISE;
                    is_test_sunrise = false;
                    clock_ui_start_sunrise(sunrise_total_sec);
                } 
                // Direct alarm trigger
                else if (diff_sec <= 0 && diff_sec >= -60 && !alarm_fired_today) {
                    Serial.println("[Alarm] Triggering Alarm Ringing!");
                    clock_state = STATE_ALARM_FIRING;
                    alarm_audio_start();
                    clock_ui_trigger_alarm();
                }
            }
        } 
        else if (clock_state == STATE_SUNRISE) {
            float progress = 0.0f;
            int remaining_sec = 0;

            if (is_test_sunrise) {
                unsigned long elapsed = now_ms - test_start_ms;
                progress = (float)elapsed / (float)TEST_SUNRISE_DURATION_MS;
                remaining_sec = (TEST_SUNRISE_DURATION_MS - elapsed) / 1000;
                if (remaining_sec < 0) remaining_sec = 0;

                if (progress >= 1.0f) {
                    Serial.println("[Test Sunrise] Test complete, firing preview alarm!");
                    clock_state = STATE_ALARM_FIRING;
                    test_alarm_start_ms = now_ms;
                    alarm_audio_start();
                    clock_ui_trigger_alarm();
                } else {
                    clock_ui_update_sunrise(progress, remaining_sec);
                }
            } else {
                int target_h = ClockConfigManager::settings.alarm_hour;
                int target_m = ClockConfigManager::settings.alarm_minute;
                int target_total_sec = target_h * 3600 + target_m * 60;
                int now_total_sec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
                int diff_sec = target_total_sec - now_total_sec;
                if (diff_sec < 0) diff_sec += 24 * 3600;
                int sunrise_total_sec = ClockConfigManager::settings.sunrise_minutes * 60;

                remaining_sec = diff_sec;
                if (diff_sec <= 0 || diff_sec > 24 * 3600 - 60) {
                    progress = 1.0f;
                    clock_state = STATE_ALARM_FIRING;
                    alarm_audio_start();
                    clock_ui_trigger_alarm();
                } else {
                    progress = 1.0f - ((float)diff_sec / (float)sunrise_total_sec);
                    clock_ui_update_sunrise(progress, remaining_sec);
                }
            }
        } 
        else if (clock_state == STATE_ALARM_FIRING) {
            if (is_test_sunrise) {
                // For preview test: ring for 5 seconds then automatically return to normal clock face!
                if (now_ms - test_alarm_start_ms >= 5000) {
                    Serial.println("[Test Sunrise] Preview complete, auto-dismissing back to clock face");
                    handleAlarmAction(false);
                }
            }
        }
    }

    delay(2);
}
