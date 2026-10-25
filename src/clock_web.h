#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "clock_config.h"
#include "clock_ui.h"
#include "clock_location.h"
#include "clock_weather.h"
#include "bsp_lvgl_port.h"

class ClockWebServer {
private:
    static WebServer server;
    static bool test_requested;
    static bool test_sound_requested;
    static bool settings_updated;

    static void handleRoot() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>";
        html += "<title>Smart Sunrise Alarm Clock</title>";
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:20px;}";
        html += ".container{max-width:500px;margin:0 auto;}";
        html += "h1{color:#f59e0b;font-size:24px;text-align:center;margin-bottom:6px;}";
        html += ".sub{text-align:center;color:#94a3b8;font-size:14px;margin-bottom:20px;}";
        html += ".card{background:#1e293b;border-radius:16px;padding:20px;margin-bottom:16px;border:1px solid #334155;box-shadow:0 4px 6px -1px rgba(0,0,0,0.3);}";
        html += "h3{margin-top:0;margin-bottom:14px;color:#38bdf8;font-size:18px;}";
        html += "label{display:block;font-size:14px;color:#94a3b8;margin-bottom:6px;}";
        html += "input,select{width:100%;padding:12px;background:#0f172a;border:1px solid #475569;border-radius:10px;color:#fff;font-size:16px;margin-bottom:16px;box-sizing:border-box;}";
        html += "button{width:100%;padding:14px;border:none;border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.2s;}";
        html += ".btn-primary{background:#f59e0b;color:#0f172a;margin-bottom:10px;}";
        html += ".btn-test{background:#10b981;color:#fff;margin-bottom:10px;}";
        html += ".btn-sec{background:#334155;color:#e2e8f0;margin-bottom:10px;}";
        html += ".row{display:flex;gap:12px;}";
        html += ".row > div{flex:1;}";
        html += ".badge{display:inline-block;padding:4px 10px;border-radius:20px;font-size:12px;font-weight:600;}";
        html += ".badge-on{background:#064e3b;color:#34d399;}";
        html += ".badge-info{background:#0369a1;color:#bae6fd;}";
        html += ".info-box{background:#0f172a;border-radius:10px;padding:12px;margin-bottom:14px;font-size:14px;border-left:4px solid #38bdf8;}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<h1>🌅 Smart Sunrise Alarm Clock</h1>";
        html += "<p class='sub'>Waveshare 2.16\" AMOLED ESP32-C6 Bedside Clock</p>";

        // Card 1: Location & Timezone (Automatic DST)
        html += "<div class='card'>";
        html += "<h3>📍 Location & Internet Time (Automatic DST)</h3>";
        html += "<div class='info-box'>";
        html += "<div><b>Location:</b> " + String(ClockConfigManager::settings.location_str) + "</div>";
        html += "<div><b>Active Timezone:</b> " + String(ClockConfigManager::settings.timezone_name) + "</div>";
        html += "<div><b>POSIX DST Rule:</b> <code style='color:#fcd34d;'>" + String(ClockConfigManager::settings.posix_tz) + "</code></div>";
        html += "<div><b>Daylight Saving:</b> <span class='badge badge-info'>Automatic Network Sync</span></div>";
        html += "</div>";

        html += "<form method='POST' action='/save_location'>";
        html += "<label><input type='checkbox' name='auto_loc' value='1'" + String(ClockConfigManager::settings.auto_location ? " checked" : "") + " style='width:auto;margin-right:8px;'> Auto-detect location & timezone on Wi-Fi</label>";
        html += "<label style='margin-top:10px;'>Select Location / Region Manually:</label>";
        html += "<select name='tz_iana'>";
        for (size_t i = 0; i < KNOWN_TZ_COUNT; i++) {
            String sel = (strcmp(ClockConfigManager::settings.timezone_name, KNOWN_TIMEZONES[i].iana) == 0) ? " selected" : "";
            html += "<option value='" + String(KNOWN_TIMEZONES[i].iana) + "'" + sel + ">" + String(KNOWN_TIMEZONES[i].display_name) + "</option>";
        }
        html += "</select>";
        html += "<button type='submit' class='btn-primary'>💾 Save Location & Sync Time</button>";
        html += "</form>";

        html += "<form method='POST' action='/detect_location' style='margin-top:6px;'>";
        html += "<button type='submit' class='btn-sec'>🔄 Re-Detect Location via IP Now</button>";
        html += "</form>";
        html += "</div>";

        // Card 1.5: Real-time Weather & Moon Phase
        html += "<div class='card'>";
        html += "<h3>🌤️ Real-Time Weather & Lunar Phase</h3>";
        html += "<div class='info-box'>";
        html += "<div><b>Condition:</b> " + String(ClockWeatherManager::data.condition_text) + " (" + String(ClockWeatherManager::data.temperature, 1) + "&deg;C)</div>";
        html += "<div><b>Day / Night:</b> " + String(ClockWeatherManager::data.is_day ? "Daytime ☀️" : "Nighttime 🌙") + "</div>";
        html += "<div><b>Moon Phase:</b> " + String(ClockWeatherManager::data.moon_phase_text) + " (Age: " + String(ClockWeatherManager::data.moon_age, 1) + " days)</div>";
        html += "<div><b>Coordinates:</b> " + String(ClockConfigManager::settings.latitude, 4) + ", " + String(ClockConfigManager::settings.longitude, 4) + "</div>";
        html += "</div>";
        html += "<form method='POST' action='/refresh_weather'>";
        html += "<button type='submit' class='btn-sec'>🔄 Refresh Weather & Moon Data Now</button>";
        html += "</form>";
        html += "</div>";

        // Card 2: Alarm Setup
        html += "<div class='card'>";
        html += "<h3>⏰ Alarm Configuration</h3>";
        html += "<form method='POST' action='/save_alarm'>";
        html += "<div class='row'>";
        html += "<div><label>Hour (0-23)</label><input type='number' name='hour' min='0' max='23' value='" + String(ClockConfigManager::settings.alarm_hour) + "'></div>";
        html += "<div><label>Minute (0-59)</label><input type='number' name='min' min='0' max='59' value='" + String(ClockConfigManager::settings.alarm_minute) + "'></div>";
        html += "</div>";

        html += "<label>Sunrise Duration (minutes before alarm)</label>";
        html += "<select name='sun_min'>";
        uint8_t opts[4] = {5, 10, 15, 30};
        for (int i = 0; i < 4; i++) {
            html += "<option value='" + String(opts[i]) + "'" + (ClockConfigManager::settings.sunrise_minutes == opts[i] ? " selected" : "") + ">" + String(opts[i]) + " Minutes</option>";
        }
        html += "</select>";

        html += "<label>Alarm Status</label>";
        html += "<select name='enabled'>";
        html += "<option value='1'" + String(ClockConfigManager::settings.alarm_enabled ? " selected" : "") + ">Enabled (ON)</option>";
        html += "<option value='0'" + String(!ClockConfigManager::settings.alarm_enabled ? " selected" : "") + ">Disabled (OFF)</option>";
        html += "</select>";

        html += "<button type='submit' class='btn-primary'>💾 Save Alarm Settings</button>";
        html += "</form>";
        html += "</div>";

        // Card 3: Battery & Brightness Optimization
        html += "<div class='card'>";
        html += "<h3>🔋 Battery & AMOLED Brightness Optimization</h3>";
        html += "<div class='info-box'>";
        html += "<div><b>Power Saving:</b> <span class='badge badge-on'>Wi-Fi Modem Sleep Active</span></div>";
        html += "<div><b>Audio Standby:</b> <span class='badge badge-on'>ES8311 0mA Standby</span></div>";
        html += "<div><b>Auto-Dim:</b> Screen drops to low power after 7s of inactivity</div>";
        html += "</div>";

        html += "<form method='POST' action='/save_brightness'>";
        html += "<label>Day Active Brightness: <b id='br_val'>" + String(ClockConfigManager::settings.day_brightness) + "%</b></label>";
        html += "<input type='range' name='brightness' min='10' max='100' step='5' value='" + String(ClockConfigManager::settings.day_brightness) + "' oninput=\"document.getElementById('br_val').innerText = this.value + '%';\">";

        html += "<label>Night Inactivity Display (22:00 - 06:30): <b id='nit_val'>" + String(ClockConfigManager::settings.night_dim_level == 0 ? "0% (Completely OFF)" : String(ClockConfigManager::settings.night_dim_level) + "%") + "</b></label>";
        html += "<input type='range' name='night_dim' min='0' max='25' step='5' value='" + String(ClockConfigManager::settings.night_dim_level) + "' oninput=\"document.getElementById('nit_val').innerText = (this.value == 0 ? '0% (Completely OFF)' : this.value + '%');\">";
        html += "<p style='font-size:12px;color:#94a3b8;margin-top:-8px;margin-bottom:14px;'><b>0% (Completely OFF)</b> is recommended: turns AMOLED pixels pitch black at night to maximize battery life (lasts 30-40+ hours!). Simply tap the screen or press the PWR button to wake the display.</p>";

        html += "<button type='submit' class='btn-primary'>🔆 Save Brightness Settings</button>";
        html += "</form>";
        html += "</div>";

        // Card 4: Quick Preview & Test
        html += "<div class='card'>";
        html += "<h3>☀️ Instant Preview & Audio Test</h3>";
        html += "<p style='color:#94a3b8;font-size:14px;'>Preview your sunrise visual simulation or test the onboard ES8311 speaker chime.</p>";
        html += "<form method='POST' action='/test_sunrise' style='margin-bottom:10px;'><button type='submit' class='btn-test'>🌅 Run Sunrise Simulation Preview</button></form>";
        html += "<form method='POST' action='/test_sound'><button type='submit' style='background:#6366f1;color:#fff;'>🔔 Test Speaker / Alarm Chime</button></form>";
        html += "</div>";

        // Card 5: Wi-Fi Credentials
        html += "<div class='card'>";
        html += "<h3>📶 Wi-Fi Configuration</h3>";
        html += "<form method='POST' action='/save_wifi'>";
        html += "<label>Wi-Fi Network Name (SSID)</label><input type='text' name='ssid' value='" + String(ClockConfigManager::settings.wifi_ssid) + "'>";
        html += "<label>Wi-Fi Password</label><input type='password' name='pass' value='" + String(ClockConfigManager::settings.wifi_pass) + "'>";
        html += "<button type='submit' class='btn-primary'>Connect Wi-Fi</button>";
        html += "</form>";
        html += "</div>";

        html += "</div></body></html>";
        server.send(200, "text/html", html);
    }

    static void handleSaveAlarm() {
        if (server.hasArg("hour")) ClockConfigManager::settings.alarm_hour = server.arg("hour").toInt();
        if (server.hasArg("min")) ClockConfigManager::settings.alarm_minute = server.arg("min").toInt();
        if (server.hasArg("sun_min")) ClockConfigManager::settings.sunrise_minutes = server.arg("sun_min").toInt();
        if (server.hasArg("enabled")) ClockConfigManager::settings.alarm_enabled = (server.arg("enabled") == "1");

        ClockConfigManager::save();
        settings_updated = true;

        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleSaveLocation() {
        ClockConfigManager::settings.auto_location = server.hasArg("auto_loc");
        if (server.hasArg("tz_iana")) {
            String tz = server.arg("tz_iana");
            strncpy(ClockConfigManager::settings.timezone_name, tz.c_str(), sizeof(ClockConfigManager::settings.timezone_name) - 1);
            const char *posix = ClockLocationManager::find_posix_tz(tz.c_str());
            strncpy(ClockConfigManager::settings.posix_tz, posix, sizeof(ClockConfigManager::settings.posix_tz) - 1);
            ClockLocationManager::apply_tz(ClockConfigManager::settings.posix_tz);
        }
        ClockConfigManager::save();
        settings_updated = true;

        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleDetectLocation() {
        ClockLocationManager::detect_location();
        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleSaveWifi() {
        if (server.hasArg("ssid")) {
            strncpy(ClockConfigManager::settings.wifi_ssid, server.arg("ssid").c_str(), sizeof(ClockConfigManager::settings.wifi_ssid) - 1);
        }
        if (server.hasArg("pass")) {
            strncpy(ClockConfigManager::settings.wifi_pass, server.arg("pass").c_str(), sizeof(ClockConfigManager::settings.wifi_pass) - 1);
        }

        ClockConfigManager::save();
        settings_updated = true;

        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleSaveBrightness() {
        if (server.hasArg("brightness")) {
            int br = server.arg("brightness").toInt();
            if (br < 10) br = 10;
            if (br > 100) br = 100;
            ClockConfigManager::settings.day_brightness = (uint8_t)br;
            Lcd_SetBacklight((uint8_t)br);
            clock_ui_set_brightness((uint8_t)br);
        }
        if (server.hasArg("night_dim")) {
            int nd = server.arg("night_dim").toInt();
            if (nd < 0) nd = 0;
            if (nd > 30) nd = 30;
            ClockConfigManager::settings.night_dim_level = (uint8_t)nd;
        }

        ClockConfigManager::save();
        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleTestSunrise() {
        test_requested = true;
        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleTestSound() {
        test_sound_requested = true;
        server.sendHeader("Location", "/");
        server.send(303);
    }

    static void handleRefreshWeather() {
        ClockWeatherManager::fetch_weather(ClockConfigManager::settings.latitude, ClockConfigManager::settings.longitude);
        server.sendHeader("Location", "/");
        server.send(303);
    }

public:
    static void init() {
        server.on("/", handleRoot);
        server.on("/save_alarm", HTTP_POST, handleSaveAlarm);
        server.on("/save_location", HTTP_POST, handleSaveLocation);
        server.on("/detect_location", HTTP_POST, handleDetectLocation);
        server.on("/refresh_weather", HTTP_POST, handleRefreshWeather);
        server.on("/save_brightness", HTTP_POST, handleSaveBrightness);
        server.on("/save_wifi", HTTP_POST, handleSaveWifi);
        server.on("/test_sunrise", HTTP_POST, handleTestSunrise);
        server.on("/test_sound", HTTP_POST, handleTestSound);
        server.begin();
    }

    static void update() {
        server.handleClient();
    }

    static bool checkTestRequested() {
        if (test_requested) {
            test_requested = false;
            return true;
        }
        return false;
    }

    static bool checkTestSoundRequested() {
        if (test_sound_requested) {
            test_sound_requested = false;
            return true;
        }
        return false;
    }

    static bool checkSettingsUpdated() {
        if (settings_updated) {
            settings_updated = false;
            return true;
        }
        return false;
    }
};

inline WebServer ClockWebServer::server(80);
inline bool ClockWebServer::test_requested = false;
inline bool ClockWebServer::test_sound_requested = false;
inline bool ClockWebServer::settings_updated = false;
