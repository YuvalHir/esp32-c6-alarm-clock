#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""
#endif

struct AlarmSettings {
    bool alarm_enabled;
    uint8_t alarm_hour;        // 0 - 23
    uint8_t alarm_minute;      // 0 - 59
    uint8_t sunrise_minutes;   // e.g. 5, 10, 15, 30 min
    bool is_24h_format;
    char location_str[48];     // e.g. "Holon, Israel"
    char timezone_name[32];    // e.g. "Asia/Jerusalem"
    char posix_tz[64];         // POSIX TZ rule e.g. "IST-2IDT,M3.4.4/26,M10.5.0"
    bool auto_location;        // true = detect location & timezone automatically
    int gmt_offset_sec;        // legacy fallback
    int daylight_offset_sec;   // legacy fallback
    char wifi_ssid[33];
    char wifi_pass[65];
    uint8_t day_brightness;    // 10 - 100%
    uint8_t night_brightness;  // 5 - 30%
    uint8_t night_dim_level;   // 5% default for maximum night battery savings
};

class ClockConfigManager {
public:
    static AlarmSettings settings;

    static void init() {
        // Set safe defaults first
        settings.alarm_enabled = true;
        settings.alarm_hour = 7;
        settings.alarm_minute = 0;
        settings.sunrise_minutes = 15;
        settings.is_24h_format = false;
        strncpy(settings.location_str, "Israel", sizeof(settings.location_str) - 1);
        strncpy(settings.timezone_name, "Asia/Jerusalem", sizeof(settings.timezone_name) - 1);
        strncpy(settings.posix_tz, "IST-2IDT,M3.4.4/26,M10.5.0", sizeof(settings.posix_tz) - 1);
        settings.auto_location = true;
        settings.gmt_offset_sec = 7200; // Standard Israel winter time is UTC+2
        settings.daylight_offset_sec = 0;
        settings.day_brightness = 90;
        settings.night_brightness = 15;
        settings.night_dim_level = 0; // 0% = Screen Completely OFF at night for maximum battery savings!
        strncpy(settings.wifi_ssid, DEFAULT_WIFI_SSID, sizeof(settings.wifi_ssid) - 1);
        strncpy(settings.wifi_pass, DEFAULT_WIFI_PASS, sizeof(settings.wifi_pass) - 1);

        // Initialize NVS
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        Preferences prefs;
        if (prefs.begin("alarm_clock", false)) {
            settings.alarm_enabled = prefs.getBool("enabled", settings.alarm_enabled);
            settings.alarm_hour = prefs.getUChar("hour", settings.alarm_hour);
            settings.alarm_minute = prefs.getUChar("min", settings.alarm_minute);
            settings.sunrise_minutes = prefs.getUChar("sun_min", settings.sunrise_minutes);
            settings.is_24h_format = prefs.getBool("is_24h", settings.is_24h_format);
            settings.auto_location = prefs.getBool("auto_loc", settings.auto_location);
            settings.night_dim_level = prefs.getUChar("nit_dim", settings.night_dim_level);

            String loc = prefs.getString("loc_str", "");
            if (loc.length() > 0) strncpy(settings.location_str, loc.c_str(), sizeof(settings.location_str) - 1);

            String tz = prefs.getString("tz_name", "");
            if (tz.length() > 0) strncpy(settings.timezone_name, tz.c_str(), sizeof(settings.timezone_name) - 1);

            String ptz = prefs.getString("posix_tz", "");
            if (ptz.length() > 0) strncpy(settings.posix_tz, ptz.c_str(), sizeof(settings.posix_tz) - 1);

            settings.gmt_offset_sec = prefs.getInt("gmt_sec", settings.gmt_offset_sec);
            settings.daylight_offset_sec = prefs.getInt("dst_sec", settings.daylight_offset_sec);
            
            uint8_t db = prefs.getUChar("day_br", 90);
            if (db > 0) settings.day_brightness = db;
            uint8_t nb = prefs.getUChar("nit_br", 15);
            if (nb > 0) settings.night_brightness = nb;

            String ssid = prefs.getString("ssid", "");
            String pass = prefs.getString("pass", "");
            if (ssid.length() > 0) {
                strncpy(settings.wifi_ssid, ssid.c_str(), sizeof(settings.wifi_ssid) - 1);
            }
            if (pass.length() > 0) {
                strncpy(settings.wifi_pass, pass.c_str(), sizeof(settings.wifi_pass) - 1);
            }
            prefs.end();
        }
    }

    static void save() {
        Preferences prefs;
        if (prefs.begin("alarm_clock", false)) {
            prefs.putBool("enabled", settings.alarm_enabled);
            prefs.putUChar("hour", settings.alarm_hour);
            prefs.putUChar("min", settings.alarm_minute);
            prefs.putUChar("sun_min", settings.sunrise_minutes);
            prefs.putBool("is_24h", settings.is_24h_format);
            prefs.putString("loc_str", settings.location_str);
            prefs.putString("tz_name", settings.timezone_name);
            prefs.putString("posix_tz", settings.posix_tz);
            prefs.putBool("auto_loc", settings.auto_location);
            prefs.putUChar("nit_dim", settings.night_dim_level);
            prefs.putInt("gmt_sec", settings.gmt_offset_sec);
            prefs.putInt("dst_sec", settings.daylight_offset_sec);
            prefs.putUChar("day_br", settings.day_brightness);
            prefs.putUChar("nit_br", settings.night_brightness);
            prefs.putString("ssid", settings.wifi_ssid);
            prefs.putString("pass", settings.wifi_pass);
            prefs.end();
        }
    }
};

inline AlarmSettings ClockConfigManager::settings;
