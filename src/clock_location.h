#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "clock_config.h"

struct TzMapping {
    const char *iana;
    const char *display_name;
    const char *posix_tz;
};

static const TzMapping KNOWN_TIMEZONES[] = {
    {"Asia/Jerusalem", "Israel (Jerusalem)", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Asia/Tel_Aviv", "Israel (Tel Aviv)", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Europe/London", "United Kingdom (London)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "Ireland (Dublin)", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "France / Germany / Italy (CET)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "Germany (Berlin)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "Italy (Rome)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "Spain (Madrid)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "Netherlands (Amsterdam)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "Greece / Eastern Europe (EET)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "Finland (Helsinki)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv", "Ukraine (Kyiv)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"America/New_York", "US Eastern (New York, Miami)", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "US Central (Chicago, Dallas)", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "US Mountain (Denver, Salt Lake)", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "US Arizona (Phoenix, No DST)", "MST7"},
    {"America/Los_Angeles", "US Pacific (Los Angeles, Seattle)", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "US Alaska (Anchorage)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "US Hawaii (Honolulu, No DST)", "HST10"},
    {"America/Toronto", "Canada (Toronto, Montreal)", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "Canada (Vancouver)", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo", "Brazil (São Paulo)", "BRT3"},
    {"America/Buenos_Aires", "Argentina (Buenos Aires)", "ART3"},
    {"Asia/Dubai", "UAE (Dubai, Abu Dhabi)", "GST-4"},
    {"Asia/Kolkata", "India (Kolkata, Mumbai, Delhi)", "IST-5:30"},
    {"Asia/Bangkok", "Thailand / Vietnam (Bangkok)", "ICT-7"},
    {"Asia/Singapore", "Singapore", "SGT-8"},
    {"Asia/Hong_Kong", "Hong Kong", "HKT-8"},
    {"Asia/Shanghai", "China (Beijing, Shanghai)", "CST-8"},
    {"Asia/Tokyo", "Japan (Tokyo)", "JST-9"},
    {"Asia/Seoul", "South Korea (Seoul)", "KST-9"},
    {"Australia/Sydney", "Australia (Sydney, Canberra)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "Australia (Melbourne)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "Australia (Perth, No DST)", "AWST-8"},
    {"Pacific/Auckland", "New Zealand (Auckland)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"UTC", "UTC (Coordinated Universal Time)", "UTC0"}
};

static const size_t KNOWN_TZ_COUNT = sizeof(KNOWN_TIMEZONES) / sizeof(KNOWN_TIMEZONES[0]);

class ClockLocationManager {
private:
    static bool parse_json_value(const String &json, const char *key, char *out_val, size_t max_len) {
        String search_key = String("\"") + key + "\":\"";
        int start_pos = json.indexOf(search_key);
        if (start_pos == -1) return false;
        start_pos += search_key.length();
        int end_pos = json.indexOf("\"", start_pos);
        if (end_pos == -1) return false;
        String val = json.substring(start_pos, end_pos);
        strncpy(out_val, val.c_str(), max_len - 1);
        out_val[max_len - 1] = '\0';
        return true;
    }

public:
    static const char* find_posix_tz(const char *iana_name) {
        if (!iana_name || strlen(iana_name) == 0) {
            return "IST-2IDT,M3.4.4/26,M10.5.0"; // Default Israel
        }
        for (size_t i = 0; i < KNOWN_TZ_COUNT; i++) {
            if (strcasecmp(KNOWN_TIMEZONES[i].iana, iana_name) == 0) {
                return KNOWN_TIMEZONES[i].posix_tz;
            }
        }
        return "IST-2IDT,M3.4.4/26,M10.5.0";
    }

    static void apply_tz(const char *posix_tz) {
        if (!posix_tz || strlen(posix_tz) == 0) {
            posix_tz = "IST-2IDT,M3.4.4/26,M10.5.0";
        }
        setenv("TZ", posix_tz, 1);
        tzset();
        configTzTime(posix_tz, "pool.ntp.org", "time.google.com");
        Serial.printf("[Location] Applied POSIX Timezone rule: '%s'\n", posix_tz);
    }

    static bool detect_location() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Location] Wi-Fi not connected, cannot detect location");
            return false;
        }

        Serial.println("[Location] Auto-detecting location & timezone via IP geolocation...");
        HTTPClient http;
        http.setTimeout(4000);
        http.begin("http://ip-api.com/json/?fields=status,country,city,lat,lon,timezone");

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("[Location Error] HTTP GET failed: %d\n", code);
            http.end();
            return false;
        }

        String payload = http.getString();
        http.end();

        char status[16] = {0};
        char country[32] = {0};
        char city[32] = {0};
        char tz[32] = {0};

        if (parse_json_value(payload, "status", status, sizeof(status)) && strcmp(status, "success") == 0) {
            parse_json_value(payload, "country", country, sizeof(country));
            parse_json_value(payload, "city", city, sizeof(city));
            parse_json_value(payload, "timezone", tz, sizeof(tz));

            int lat_idx = payload.indexOf("\"lat\":");
            if (lat_idx != -1) {
                ClockConfigManager::settings.latitude = payload.substring(lat_idx + 6).toFloat();
            }
            int lon_idx = payload.indexOf("\"lon\":");
            if (lon_idx != -1) {
                ClockConfigManager::settings.longitude = payload.substring(lon_idx + 6).toFloat();
            }

            char loc_buf[48];
            if (strlen(city) > 0 && strlen(country) > 0) {
                snprintf(loc_buf, sizeof(loc_buf), "%s, %s", city, country);
            } else if (strlen(country) > 0) {
                snprintf(loc_buf, sizeof(loc_buf), "%s", country);
            } else {
                snprintf(loc_buf, sizeof(loc_buf), "Detected Location");
            }

            strncpy(ClockConfigManager::settings.location_str, loc_buf, sizeof(ClockConfigManager::settings.location_str) - 1);
            strncpy(ClockConfigManager::settings.timezone_name, tz, sizeof(ClockConfigManager::settings.timezone_name) - 1);

            const char *matched_posix = find_posix_tz(tz);
            strncpy(ClockConfigManager::settings.posix_tz, matched_posix, sizeof(ClockConfigManager::settings.posix_tz) - 1);
            ClockConfigManager::save();

            Serial.printf("[Location] Successfully Detected: %s (%s)\n", loc_buf, tz);
            apply_tz(ClockConfigManager::settings.posix_tz);
            return true;
        }

        Serial.println("[Location] Could not parse location from IP service");
        return false;
    }
};
