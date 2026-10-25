#pragma once
#include <Arduino.h>
#include "lvgl.h"
#include "clock_config.h"

enum ClockUIMode {
    UI_MODE_CLOCK,
    UI_MODE_SUNRISE,
    UI_MODE_ALARM_RINGING,
    UI_MODE_SETTINGS,
    UI_MODE_BRIGHTNESS
};

typedef void (*AlarmActionCallback)(bool is_snooze);
typedef void (*SettingsSavedCallback)();
typedef void (*TestSunriseCallback)();
typedef void (*BrightnessChangedCallback)(uint8_t brightness);

void clock_ui_init();
void clock_ui_set_callbacks(AlarmActionCallback alarm_cb, SettingsSavedCallback save_cb, TestSunriseCallback test_cb, BrightnessChangedCallback bright_cb = NULL);

void clock_ui_update_time(int hour, int min, int sec, const char* date_str, bool is_24h);
void clock_ui_update_status(bool wifi_connected, const char* wifi_info, int batt_percent, int batt_mv, bool is_charging);
void clock_ui_update_alarm_pill(bool enabled, uint8_t hour, uint8_t min, uint8_t sunrise_min, bool is_24h);

void clock_ui_start_sunrise(int total_duration_sec);
void clock_ui_update_sunrise(float progress, int remaining_sec);
void clock_ui_trigger_alarm();
void clock_ui_dismiss_alarm();

void clock_ui_open_settings();
void clock_ui_close_settings();

void clock_ui_open_brightness();
void clock_ui_close_brightness();
void clock_ui_set_brightness(uint8_t brightness);
void clock_ui_step_brightness(int delta);
uint8_t clock_ui_get_brightness();

ClockUIMode clock_ui_get_mode();
void clock_ui_set_night_mode(bool night);
void clock_ui_check_inactivity();
void clock_ui_handle_activity();
bool clock_ui_is_dimmed();
