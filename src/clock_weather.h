#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

struct WeatherData {
    bool valid;
    float temperature;         // e.g. 26.9 °C
    int weather_code;          // WMO code (0 = clear, 1-3 = cloudy, 61 = rain, etc.)
    bool is_day;               // true = daytime, false = nighttime
    char condition_text[32];   // e.g. "Sunny", "Rain", "Partly Cloudy"
    char moon_phase_text[32];  // e.g. "Full Moon", "Waxing Crescent"
    float moon_age;            // 0.0 to 29.53 days
    uint8_t moon_phase_code;   // 0 = New, 1 = Waxing Crescent, 2 = 1st Qtr, 3 = Waxing Gibbous, 4 = Full, 5 = Waning Gibbous, 6 = Last Qtr, 7 = Waning Crescent
    unsigned long last_fetch_ms;
};

class ClockWeatherManager {
public:
    static WeatherData data;

    static void init() {
        data.valid = false;
        data.temperature = 22.0f;
        data.weather_code = 0;
        data.is_day = true;
        strcpy(data.condition_text, "Sunny");
        strcpy(data.moon_phase_text, "Full Moon");
        data.moon_age = 14.3f;
        data.moon_phase_code = 4;
        data.last_fetch_ms = 0;
    }

    // Calculates precise astronomical moon age and phase from local year/month/day
    static void calculate_moon(int year, int month, int day) {
        int y = year;
        int m = month;
        if (m < 3) {
            y -= 1;
            m += 12;
        }
        int a = y / 100;
        int b = a / 4;
        int c = 2 - a + b;
        int e = (int)(365.25 * (y + 4716));
        int f = (int)(30.6001 * (m + 1));
        double jd = c + day + e + f - 1524.5;
        double days_since_new = jd - 2451549.5; // Jan 6, 2000 18:14 UTC
        double new_moons = days_since_new / 29.53058867;
        double phase_frac = new_moons - (int)new_moons;
        if (phase_frac < 0) phase_frac += 1.0;
        float age = (float)(phase_frac * 29.53058867);
        data.moon_age = age;

        if (age < 1.84f) {
            data.moon_phase_code = 0;
            strcpy(data.moon_phase_text, "New Moon");
        } else if (age < 5.53f) {
            data.moon_phase_code = 1;
            strcpy(data.moon_phase_text, "Waxing Crescent");
        } else if (age < 9.22f) {
            data.moon_phase_code = 2;
            strcpy(data.moon_phase_text, "First Quarter");
        } else if (age < 12.91f) {
            data.moon_phase_code = 3;
            strcpy(data.moon_phase_text, "Waxing Gibbous");
        } else if (age < 16.61f) {
            data.moon_phase_code = 4;
            strcpy(data.moon_phase_text, "Full Moon");
        } else if (age < 20.30f) {
            data.moon_phase_code = 5;
            strcpy(data.moon_phase_text, "Waning Gibbous");
        } else if (age < 23.99f) {
            data.moon_phase_code = 6;
            strcpy(data.moon_phase_text, "Last Quarter");
        } else if (age < 27.68f) {
            data.moon_phase_code = 7;
            strcpy(data.moon_phase_text, "Waning Crescent");
        } else {
            data.moon_phase_code = 0;
            strcpy(data.moon_phase_text, "New Moon");
        }
    }

    static void map_wmo_code(int code, bool is_day) {
        data.weather_code = code;
        data.is_day = is_day;

        switch (code) {
            case 0:
                strcpy(data.condition_text, is_day ? "Clear & Sunny" : "Clear Night");
                break;
            case 1:
                strcpy(data.condition_text, is_day ? "Mainly Clear" : "Clear Night");
                break;
            case 2:
                strcpy(data.condition_text, "Partly Cloudy");
                break;
            case 3:
                strcpy(data.condition_text, "Overcast");
                break;
            case 45:
            case 48:
                strcpy(data.condition_text, "Foggy");
                break;
            case 51:
            case 53:
            case 55:
                strcpy(data.condition_text, "Drizzle");
                break;
            case 56:
            case 57:
                strcpy(data.condition_text, "Freezing Drizzle");
                break;
            case 61:
                strcpy(data.condition_text, "Light Rain");
                break;
            case 63:
                strcpy(data.condition_text, "Rain");
                break;
            case 65:
                strcpy(data.condition_text, "Heavy Rain");
                break;
            case 66:
            case 67:
                strcpy(data.condition_text, "Freezing Rain");
                break;
            case 71:
            case 73:
            case 75:
            case 77:
                strcpy(data.condition_text, "Snow");
                break;
            case 80:
            case 81:
            case 82:
                strcpy(data.condition_text, "Rain Showers");
                break;
            case 85:
            case 86:
                strcpy(data.condition_text, "Snow Showers");
                break;
            case 95:
                strcpy(data.condition_text, "Thunderstorm");
                break;
            case 96:
            case 99:
                strcpy(data.condition_text, "Thunder & Hail");
                break;
            default:
                strcpy(data.condition_text, "Clear");
                break;
        }
    }

    // Fetches real-time weather from Open-Meteo (fast, free, zero API key)
    static bool fetch_weather(float lat, float lon) {
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }

        // Default to Israel coordinates (Holon/Tel Aviv) if not specified
        if (lat == 0.0f && lon == 0.0f) {
            lat = 32.0167f;
            lon = 34.7833f;
        }

        char url[160];
        snprintf(url, sizeof(url),
                 "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,is_day",
                 lat, lon);

        HTTPClient http;
        http.begin(url);
        http.setTimeout(4000);

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            Serial.printf("[Weather Error] HTTP GET failed: %d\n", httpCode);
            http.end();
            return false;
        }

        String payload = http.getString();
        http.end();

        // Target the "current": { ... } object to bypass "current_units" descriptions
        int current_idx = payload.indexOf("\"current\":");
        if (current_idx == -1) {
            return false;
        }

        String current_data = payload.substring(current_idx);

        // Fast parsing of temperature_2m
        int temp_idx = current_data.indexOf("\"temperature_2m\":");
        if (temp_idx != -1) {
            temp_idx += 17;
            data.temperature = current_data.substring(temp_idx, temp_idx + 6).toFloat();
        }

        // Fast parsing of weather_code
        int code_idx = current_data.indexOf("\"weather_code\":");
        int code = 0;
        if (code_idx != -1) {
            code_idx += 15;
            code = current_data.substring(code_idx, code_idx + 4).toInt();
        }

        // Fast parsing of is_day
        int day_idx = current_data.indexOf("\"is_day\":");
        bool is_day = true;
        if (day_idx != -1) {
            day_idx += 9;
            while (day_idx < (int)current_data.length() && (current_data.charAt(day_idx) == ' ' || current_data.charAt(day_idx) == '\t')) {
                day_idx++;
            }
            is_day = (current_data.charAt(day_idx) == '1');
        }

        map_wmo_code(code, is_day);
        data.valid = true;
        data.last_fetch_ms = millis();

        Serial.printf("[Weather] Updated: %.1f°C | %s (Code %d, Day: %s)\n",
                      data.temperature, data.condition_text, code, is_day ? "YES" : "NO");
        return true;
    }
};

inline WeatherData ClockWeatherManager::data;
