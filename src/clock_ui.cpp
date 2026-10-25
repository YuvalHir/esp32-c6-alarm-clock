#include "clock_ui.h"
#include "bsp_lvgl_port.h"

static ClockUIMode current_mode = UI_MODE_CLOCK;
static AlarmActionCallback on_alarm_action = NULL;
static SettingsSavedCallback on_settings_saved = NULL;
static TestSunriseCallback on_test_sunrise = NULL;
static BrightnessChangedCallback on_brightness_changed = NULL;
static uint8_t current_brightness = 90;

// Normal Clock Screen Objects
static lv_obj_t *cont_clock = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_seconds = NULL;
static lv_obj_t *lbl_ampm = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_wifi = NULL;
static lv_obj_t *lbl_batt = NULL;
static lv_obj_t *btn_top_bright = NULL;
static lv_obj_t *lbl_top_bright = NULL;
static lv_obj_t *btn_alarm_pill = NULL;
static lv_obj_t *lbl_alarm_pill = NULL;
static lv_obj_t *btn_open_settings = NULL;

// Weather & Moon Phase Widget Objects
static lv_obj_t *btn_weather_pill = NULL;
static lv_obj_t *obj_weather_icon = NULL;
static lv_obj_t *obj_moon_mask = NULL;
static lv_obj_t *lbl_weather_text = NULL;
static bool weather_refresh_requested = false;

// Dedicated Brightness Modal Objects
static lv_obj_t *modal_brightness = NULL;
static lv_obj_t *lbl_bright_percent = NULL;
static lv_obj_t *bar_brightness = NULL;
static lv_obj_t *btn_bright_dec = NULL;
static lv_obj_t *btn_bright_inc = NULL;
static lv_obj_t *btn_bright_presets[4];
static const uint8_t bright_preset_vals[4] = {25, 50, 75, 100};
static lv_timer_t *timer_bright_auto_close = NULL;

// Floating Brightness HUD Overlay
static lv_obj_t *hud_brightness = NULL;
static lv_obj_t *lbl_hud_bright = NULL;
static lv_obj_t *bar_hud_bright = NULL;
static lv_timer_t *timer_hud_dismiss = NULL;

// Inactivity Auto-Dimming
static bool is_dimmed = false;
static uint32_t last_manual_activity_ms = 0;
static bool is_night_mode = false;

// Sunrise Screen Objects
static lv_obj_t *cont_sunrise = NULL;
static lv_obj_t *arc_sun_progress = NULL;
static lv_obj_t *obj_sun = NULL;
static lv_obj_t *obj_sun_glow = NULL;
static lv_obj_t *lbl_sunrise_title = NULL;
static lv_obj_t *lbl_sunrise_time = NULL;
static lv_obj_t *lbl_sunrise_countdown = NULL;
static lv_obj_t *btn_cancel_sunrise = NULL;

// Alarm Ringing Screen Objects
static lv_obj_t *cont_alarm = NULL;
static lv_obj_t *lbl_alarm_greeting = NULL;
static lv_obj_t *lbl_alarm_time = NULL;
static lv_obj_t *btn_alarm_dismiss = NULL;
static lv_obj_t *btn_alarm_snooze = NULL;

// Settings Modal Objects
static lv_obj_t *modal_settings = NULL;
static lv_obj_t *lbl_set_hour = NULL;
static lv_obj_t *lbl_set_min = NULL;
static lv_obj_t *btn_set_enabled = NULL;
static lv_obj_t *lbl_set_enabled = NULL;

// Temporary settings for modal
static uint8_t edit_hour = 7;
static uint8_t edit_min = 0;
static bool edit_enabled = true;

static lv_color_t color_interpolate(lv_color_t c1, lv_color_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    uint8_t ratio = (uint8_t)(t * 255.0f);
    return lv_color_mix(c2, c1, ratio);
}

static void event_scr_activity(lv_event_t *e) {
    clock_ui_handle_activity();
}

static void event_btn_weather_clicked(lv_event_t *e) {
    clock_ui_handle_activity();
    weather_refresh_requested = true;
    if (lbl_weather_text) {
        lv_label_set_text(lbl_weather_text, "Updating...");
    }
}

static void event_btn_alarm_pill(lv_event_t *e) {
    clock_ui_handle_activity();
    clock_ui_open_settings();
}

static void event_btn_open_settings(lv_event_t *e) {
    clock_ui_handle_activity();
    clock_ui_open_settings();
}

static void event_alarm_dismiss(lv_event_t *e) {
    clock_ui_handle_activity();
    if (on_alarm_action) {
        on_alarm_action(false);
    }
}

static void event_alarm_snooze(lv_event_t *e) {
    clock_ui_handle_activity();
    if (on_alarm_action) {
        on_alarm_action(true);
    }
}

static void event_hour_inc(lv_event_t *e) {
    clock_ui_handle_activity();
    edit_hour = (edit_hour + 1) % 24;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", edit_hour);
    lv_label_set_text(lbl_set_hour, buf);
}

static void event_hour_dec(lv_event_t *e) {
    clock_ui_handle_activity();
    edit_hour = (edit_hour == 0) ? 23 : edit_hour - 1;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", edit_hour);
    lv_label_set_text(lbl_set_hour, buf);
}

static void event_min_inc(lv_event_t *e) {
    clock_ui_handle_activity();
    edit_min = (edit_min + 1) % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", edit_min);
    lv_label_set_text(lbl_set_min, buf);
}

static void event_min_dec(lv_event_t *e) {
    clock_ui_handle_activity();
    edit_min = (edit_min == 0) ? 59 : edit_min - 1;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", edit_min);
    lv_label_set_text(lbl_set_min, buf);
}

static void event_toggle_alarm(lv_event_t *e) {
    clock_ui_handle_activity();
    edit_enabled = !edit_enabled;
    if (edit_enabled) {
        lv_label_set_text(lbl_set_enabled, LV_SYMBOL_OK " ALARM: ON");
        lv_obj_set_style_bg_color(btn_set_enabled, lv_color_hex(0x10B981), 0);
    } else {
        lv_label_set_text(lbl_set_enabled, LV_SYMBOL_CLOSE " ALARM: OFF");
        lv_obj_set_style_bg_color(btn_set_enabled, lv_color_hex(0x475569), 0);
    }
}

static void event_settings_save(lv_event_t *e) {
    clock_ui_handle_activity();
    ClockConfigManager::settings.alarm_hour = edit_hour;
    ClockConfigManager::settings.alarm_minute = edit_min;
    ClockConfigManager::settings.alarm_enabled = edit_enabled;
    ClockConfigManager::save();

    clock_ui_close_settings();
    if (on_settings_saved) {
        on_settings_saved();
    }
}

static void event_settings_close(lv_event_t *e) {
    clock_ui_handle_activity();
    clock_ui_close_settings();
}

static void event_cancel_sunrise(lv_event_t *e) {
    clock_ui_handle_activity();
    if (on_alarm_action) {
        on_alarm_action(false);
    } else {
        clock_ui_dismiss_alarm();
    }
}

static lv_timer_t *timer_save_nvs = NULL;

static void timer_save_nvs_cb(lv_timer_t *t) {
    ClockConfigManager::settings.day_brightness = current_brightness;
    ClockConfigManager::save();
    if (timer_save_nvs) {
        lv_timer_del(timer_save_nvs);
        timer_save_nvs = NULL;
    }
}

static void schedule_nvs_save() {
    ClockConfigManager::settings.day_brightness = current_brightness;
    if (timer_save_nvs) {
        lv_timer_reset(timer_save_nvs);
    } else {
        timer_save_nvs = lv_timer_create(timer_save_nvs_cb, 1200, NULL);
    }
}

static void update_brightness_visuals(uint8_t br) {
    current_brightness = br;

    if (lbl_top_bright) {
        lv_label_set_text_fmt(lbl_top_bright, LV_SYMBOL_EYE_OPEN " %d%%", br);
    }
    if (lbl_bright_percent) {
        lv_label_set_text_fmt(lbl_bright_percent, "%d%%", br);
    }
    if (bar_brightness) {
        lv_bar_set_value(bar_brightness, br, LV_ANIM_OFF);
    }
    if (lbl_hud_bright) {
        lv_label_set_text_fmt(lbl_hud_bright, LV_SYMBOL_EYE_OPEN " %d%%", br);
    }
    if (bar_hud_bright) {
        lv_bar_set_value(bar_hud_bright, br, LV_ANIM_OFF);
    }

    for (int i = 0; i < 4; i++) {
        if (btn_bright_presets[i]) {
            if (bright_preset_vals[i] == br) {
                lv_obj_set_style_bg_color(btn_bright_presets[i], lv_color_hex(0xF59E0B), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(btn_bright_presets[i], 0), lv_color_hex(0x000000), 0);
            } else {
                lv_obj_set_style_bg_color(btn_bright_presets[i], lv_color_hex(0x1E293B), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(btn_bright_presets[i], 0), lv_color_hex(0xE2E8F0), 0);
            }
        }
    }
}

static void apply_brightness(uint8_t br, bool save_to_nvs) {
    if (br < 10) br = 10;
    if (br > 100) br = 100;
    current_brightness = br;

    Lcd_SetBacklight(br);
    update_brightness_visuals(br);

    if (save_to_nvs) {
        schedule_nvs_save();
    }
    if (on_brightness_changed) {
        on_brightness_changed(br);
    }
}

static void timer_hud_cb(lv_timer_t *t) {
    if (hud_brightness) {
        lv_obj_add_flag(hud_brightness, LV_OBJ_FLAG_HIDDEN);
    }
    if (timer_hud_dismiss) {
        lv_timer_del(timer_hud_dismiss);
        timer_hud_dismiss = NULL;
    }
}

static void timer_bright_close_cb(lv_timer_t *t) {
    clock_ui_close_brightness();
}

static void event_top_bright_clicked(lv_event_t *e) {
    clock_ui_open_brightness();
}

static void event_bright_dec(lv_event_t *e) {
    clock_ui_step_brightness(-10);
}

static void event_bright_inc(lv_event_t *e) {
    clock_ui_step_brightness(10);
}

static void event_bright_preset(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < 4) {
        if (bsp_lvgl_lock(20)) {
            apply_brightness(bright_preset_vals[idx], true);
            if (timer_bright_auto_close) {
                lv_timer_reset(timer_bright_auto_close);
            }
            bsp_lvgl_unlock();
        }
    }
}

static void event_bright_done(lv_event_t *e) {
    clock_ui_close_brightness();
}

void clock_ui_init() {
    if (!bsp_lvgl_lock(0)) return;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, event_scr_activity, LV_EVENT_PRESSED, NULL);

    // ==========================================
    // 1. Normal Clock Screen Container
    // ==========================================
    cont_clock = lv_obj_create(scr);
    lv_obj_set_size(cont_clock, 480, 480);
    lv_obj_center(cont_clock);
    lv_obj_set_style_bg_color(cont_clock, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_clock, 0, 0);
    lv_obj_set_style_pad_all(cont_clock, 0, 0);
    lv_obj_clear_flag(cont_clock, LV_OBJ_FLAG_SCROLLABLE);

    // Top Status Bar (Safe margins for 480x480 round dial)
    lbl_wifi = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x64748B), 0);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " Connecting...");
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_LEFT, 55, 36);

    // Quick Brightness Pill Button in Status Bar
    btn_top_bright = lv_btn_create(cont_clock);
    lv_obj_set_size(btn_top_bright, 88, 32);
    lv_obj_align(btn_top_bright, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_radius(btn_top_bright, 16, 0);
    lv_obj_set_style_bg_color(btn_top_bright, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_top_bright, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(btn_top_bright, 1, 0);
    lv_obj_set_style_pad_all(btn_top_bright, 0, 0);
    lv_obj_add_event_cb(btn_top_bright, event_top_bright_clicked, LV_EVENT_CLICKED, NULL);

    lbl_top_bright = lv_label_create(btn_top_bright);
    lv_obj_set_style_text_font(lbl_top_bright, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_top_bright, lv_color_hex(0xFCD34D), 0);
    lv_label_set_text_fmt(lbl_top_bright, LV_SYMBOL_EYE_OPEN " %d%%", ClockConfigManager::settings.day_brightness);
    lv_obj_center(lbl_top_bright);

    lbl_batt = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0x10B981), 0);
    lv_label_set_text(lbl_batt, LV_SYMBOL_CHARGE " USB");
    lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -55, 36);

    // Big Center Clock Time (Montserrat 48)
    lbl_time = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_time, "00:00");
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, -25, -55);

    // AM/PM & Seconds indicator
    lbl_ampm = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_ampm, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_ampm, lv_color_hex(0xF59E0B), 0);
    lv_label_set_text(lbl_ampm, "AM");
    lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_TOP, 10, 4);

    lbl_seconds = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_seconds, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_seconds, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(lbl_seconds, ":00");
    lv_obj_align_to(lbl_seconds, lbl_ampm, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    // Full Date Label
    lbl_date = lv_label_create(cont_clock);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(lbl_date, "Saturday, Oct 24");
    lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 4);

    // Weather & Moon Phase Pill Button (Shows Weather Graphics by Day & Moon Phase by Night)
    btn_weather_pill = lv_btn_create(cont_clock);
    lv_obj_set_size(btn_weather_pill, 270, 38);
    lv_obj_align(btn_weather_pill, LV_ALIGN_CENTER, 0, 48);
    lv_obj_set_style_radius(btn_weather_pill, 19, 0);
    lv_obj_set_style_bg_color(btn_weather_pill, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(btn_weather_pill, LV_OPA_80, 0);
    lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_weather_pill, 1, 0);
    lv_obj_set_style_pad_all(btn_weather_pill, 4, 0);
    lv_obj_add_event_cb(btn_weather_pill, event_btn_weather_clicked, LV_EVENT_CLICKED, NULL);

    // Weather / Moon Graphic Icon Container
    obj_weather_icon = lv_obj_create(btn_weather_pill);
    lv_obj_set_size(obj_weather_icon, 24, 24);
    lv_obj_align(obj_weather_icon, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_radius(obj_weather_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFDE047), 0);
    lv_obj_set_style_border_width(obj_weather_icon, 2, 0);
    lv_obj_set_style_pad_all(obj_weather_icon, 0, 0);
    lv_obj_clear_flag(obj_weather_icon, LV_OBJ_FLAG_CLICKABLE);

    // Inner moon phase shadow mask (for rendering crescent/gibbous/quarters)
    obj_moon_mask = lv_obj_create(obj_weather_icon);
    lv_obj_set_size(obj_moon_mask, 16, 20);
    lv_obj_align(obj_moon_mask, LV_ALIGN_CENTER, -4, 0);
    lv_obj_set_style_radius(obj_moon_mask, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(obj_moon_mask, 0, 0);
    lv_obj_add_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_CLICKABLE);

    // Weather & Moon Condition & Temperature Text
    lbl_weather_text = lv_label_create(btn_weather_pill);
    lv_obj_set_style_text_font(lbl_weather_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_weather_text, "Sunny  25°C");
    lv_obj_align(lbl_weather_text, LV_ALIGN_LEFT_MID, 38, 0);

    // Alarm Pill Touch Button (High contrast, finger-friendly)
    btn_alarm_pill = lv_btn_create(cont_clock);
    lv_obj_set_size(btn_alarm_pill, 310, 44);
    lv_obj_align(btn_alarm_pill, LV_ALIGN_CENTER, 0, 102);
    lv_obj_set_style_radius(btn_alarm_pill, 22, 0);
    lv_obj_set_style_bg_color(btn_alarm_pill, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(btn_alarm_pill, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(btn_alarm_pill, 2, 0);
    lv_obj_set_style_shadow_width(btn_alarm_pill, 15, 0);
    lv_obj_set_style_shadow_color(btn_alarm_pill, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_shadow_opa(btn_alarm_pill, LV_OPA_30, 0);
    lv_obj_add_event_cb(btn_alarm_pill, event_btn_alarm_pill, LV_EVENT_CLICKED, NULL);

    lbl_alarm_pill = lv_label_create(btn_alarm_pill);
    lv_obj_set_style_text_font(lbl_alarm_pill, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_alarm_pill, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_alarm_pill, LV_SYMBOL_BELL " ALARM: 07:00 AM");
    lv_obj_center(lbl_alarm_pill);

    // Centered Settings Button (Clear, prominent, 200x44 touch target)
    btn_open_settings = lv_btn_create(cont_clock);
    lv_obj_set_size(btn_open_settings, 200, 44);
    lv_obj_align(btn_open_settings, LV_ALIGN_CENTER, 0, 162);
    lv_obj_set_style_radius(btn_open_settings, 22, 0);
    lv_obj_set_style_bg_color(btn_open_settings, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_open_settings, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_open_settings, 2, 0);
    lv_obj_add_event_cb(btn_open_settings, event_btn_open_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn_set = lv_label_create(btn_open_settings);
    lv_obj_set_style_text_font(lbl_btn_set, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_btn_set, lv_color_hex(0xE2E8F0), 0);
    lv_label_set_text(lbl_btn_set, LV_SYMBOL_SETTINGS " SETTINGS");
    lv_obj_center(lbl_btn_set);

    // ==========================================
    // 2. Sunrise Screen Container (Full 480x480 Round Canvas)
    // ==========================================
    cont_sunrise = lv_obj_create(scr);
    lv_obj_set_size(cont_sunrise, 480, 480);
    lv_obj_center(cont_sunrise);
    lv_obj_set_style_bg_color(cont_sunrise, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(cont_sunrise, 0, 0);
    lv_obj_set_style_pad_all(cont_sunrise, 0, 0);
    lv_obj_add_flag(cont_sunrise, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_sunrise, LV_OBJ_FLAG_SCROLLABLE);

    // Sunrise Circular Progress Arc (edge of dial - 460px full circular frame)
    arc_sun_progress = lv_arc_create(cont_sunrise);
    lv_obj_set_size(arc_sun_progress, 460, 460);
    lv_obj_center(arc_sun_progress);
    lv_arc_set_rotation(arc_sun_progress, 270); // 12 o'clock start
    lv_arc_set_bg_angles(arc_sun_progress, 0, 360);
    lv_arc_set_angles(arc_sun_progress, 0, 0);
    lv_obj_remove_style(arc_sun_progress, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc_sun_progress, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_sun_progress, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_sun_progress, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_sun_progress, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_clear_flag(arc_sun_progress, LV_OBJ_FLAG_CLICKABLE);

    // Soft Ambient Halo of Sun (240px circle)
    obj_sun_glow = lv_obj_create(cont_sunrise);
    lv_obj_set_size(obj_sun_glow, 240, 240);
    lv_obj_set_style_radius(obj_sun_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj_sun_glow, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_bg_opa(obj_sun_glow, LV_OPA_40, 0);
    lv_obj_set_style_border_width(obj_sun_glow, 0, 0);
    lv_obj_align(obj_sun_glow, LV_ALIGN_CENTER, 0, 80);

    // Radiant Sun Orb (160px circle)
    obj_sun = lv_obj_create(cont_sunrise);
    lv_obj_set_size(obj_sun, 160, 160);
    lv_obj_set_style_radius(obj_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj_sun, lv_color_hex(0xFDE047), 0);
    lv_obj_set_style_border_color(obj_sun, lv_color_hex(0xFFFDF0), 0);
    lv_obj_set_style_border_width(obj_sun, 3, 0);
    lv_obj_align(obj_sun, LV_ALIGN_CENTER, 0, 80);

    // Header Label: "SUNRISE WAKE-UP"
    lbl_sunrise_title = lv_label_create(cont_sunrise);
    lv_obj_set_style_text_font(lbl_sunrise_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_sunrise_title, lv_color_hex(0xFEF08A), 0);
    lv_label_set_text(lbl_sunrise_title, LV_SYMBOL_BELL " SUNRISE WAKE-UP");
    lv_obj_align(lbl_sunrise_title, LV_ALIGN_TOP_MID, 0, 45);

    // Large Time in Sunrise Screen: "07:00 AM"
    lbl_sunrise_time = lv_label_create(cont_sunrise);
    lv_obj_set_style_text_font(lbl_sunrise_time, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_sunrise_time, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_sunrise_time, "07:00 AM");
    lv_obj_align(lbl_sunrise_time, LV_ALIGN_TOP_MID, 0, 80);

    // Countdown / status badge
    lbl_sunrise_countdown = lv_label_create(cont_sunrise);
    lv_obj_set_style_text_font(lbl_sunrise_countdown, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_sunrise_countdown, lv_color_hex(0xFFFBEB), 0);
    lv_label_set_text(lbl_sunrise_countdown, "Alarm in 20s");
    lv_obj_align(lbl_sunrise_countdown, LV_ALIGN_BOTTOM_MID, 0, -85);

    // Dedicated STOP / DISMISS Touch Button
    btn_cancel_sunrise = lv_btn_create(cont_sunrise);
    lv_obj_set_size(btn_cancel_sunrise, 160, 42);
    lv_obj_align(btn_cancel_sunrise, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_radius(btn_cancel_sunrise, 21, 0);
    lv_obj_set_style_bg_color(btn_cancel_sunrise, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn_cancel_sunrise, LV_OPA_60, 0);
    lv_obj_set_style_border_color(btn_cancel_sunrise, lv_color_hex(0xFDE047), 0);
    lv_obj_set_style_border_width(btn_cancel_sunrise, 1, 0);
    lv_obj_add_event_cb(btn_cancel_sunrise, event_cancel_sunrise, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn_cs = lv_label_create(btn_cancel_sunrise);
    lv_obj_set_style_text_font(lbl_btn_cs, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_btn_cs, lv_color_hex(0xFDE047), 0);
    lv_label_set_text(lbl_btn_cs, LV_SYMBOL_CLOSE " STOP PREVIEW");
    lv_obj_center(lbl_btn_cs);

    // ==========================================
    // 3. Alarm Ringing Screen Container
    // ==========================================
    cont_alarm = lv_obj_create(scr);
    lv_obj_set_size(cont_alarm, 480, 480);
    lv_obj_center(cont_alarm);
    lv_obj_set_style_bg_color(cont_alarm, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(cont_alarm, 0, 0);
    lv_obj_set_style_pad_all(cont_alarm, 0, 0);
    lv_obj_add_flag(cont_alarm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_alarm, LV_OBJ_FLAG_SCROLLABLE);

    lbl_alarm_greeting = lv_label_create(cont_alarm);
    lv_obj_set_style_text_font(lbl_alarm_greeting, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_alarm_greeting, lv_color_hex(0x000000), 0);
    lv_label_set_text(lbl_alarm_greeting, LV_SYMBOL_BELL " WAKE UP!");
    lv_obj_align(lbl_alarm_greeting, LV_ALIGN_TOP_MID, 0, 60);

    lbl_alarm_time = lv_label_create(cont_alarm);
    lv_obj_set_style_text_font(lbl_alarm_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_alarm_time, lv_color_hex(0x000000), 0);
    lv_label_set_text(lbl_alarm_time, "07:00 AM");
    lv_obj_align(lbl_alarm_time, LV_ALIGN_CENTER, 0, -40);

    btn_alarm_dismiss = lv_btn_create(cont_alarm);
    lv_obj_set_size(btn_alarm_dismiss, 280, 65);
    lv_obj_align(btn_alarm_dismiss, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_radius(btn_alarm_dismiss, 32, 0);
    lv_obj_set_style_bg_color(btn_alarm_dismiss, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_shadow_width(btn_alarm_dismiss, 20, 0);
    lv_obj_set_style_shadow_color(btn_alarm_dismiss, lv_color_hex(0x059669), 0);
    lv_obj_add_event_cb(btn_alarm_dismiss, event_alarm_dismiss, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_dis = lv_label_create(btn_alarm_dismiss);
    lv_obj_set_style_text_font(lbl_dis, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_dis, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_dis, LV_SYMBOL_OK " DISMISS");
    lv_obj_center(lbl_dis);

    btn_alarm_snooze = lv_btn_create(cont_alarm);
    lv_obj_set_size(btn_alarm_snooze, 240, 55);
    lv_obj_align(btn_alarm_snooze, LV_ALIGN_CENTER, 0, 135);
    lv_obj_set_style_radius(btn_alarm_snooze, 28, 0);
    lv_obj_set_style_bg_color(btn_alarm_snooze, lv_color_hex(0x1E293B), 0);
    lv_obj_add_event_cb(btn_alarm_snooze, event_alarm_snooze, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_sno = lv_label_create(btn_alarm_snooze);
    lv_obj_set_style_text_font(lbl_sno, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_sno, lv_color_hex(0xFCD34D), 0);
    lv_label_set_text(lbl_sno, LV_SYMBOL_REFRESH " SNOOZE 9 MIN");
    lv_obj_center(lbl_sno);

    // ==========================================
    // 4. Settings Modal (Spacious, Finger-Friendly for 4x4cm)
    // ==========================================
    modal_settings = lv_obj_create(scr);
    lv_obj_set_size(modal_settings, 410, 410);
    lv_obj_center(modal_settings);
    lv_obj_set_style_radius(modal_settings, 36, 0);
    lv_obj_set_style_bg_color(modal_settings, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(modal_settings, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(modal_settings, 2, 0);
    lv_obj_set_style_shadow_width(modal_settings, 30, 0);
    lv_obj_set_style_shadow_color(modal_settings, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(modal_settings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_m_title = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_m_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_m_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_m_title, LV_SYMBOL_SETTINGS " ALARM TIME");
    lv_obj_align(lbl_m_title, LV_ALIGN_TOP_MID, 0, 16);

    // Labels for Hour / Min
    lv_obj_t *lbl_tag_h = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_tag_h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tag_h, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(lbl_tag_h, "HOUR");
    lv_obj_align(lbl_tag_h, LV_ALIGN_CENTER, -75, -112);

    lv_obj_t *lbl_tag_m = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_tag_m, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tag_m, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(lbl_tag_m, "MIN");
    lv_obj_align(lbl_tag_m, LV_ALIGN_CENTER, 75, -112);

    // Hour Adjustment: [ + ] [ 07 ] [ - ]
    lv_obj_t *btn_h_inc = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_h_inc, 72, 40);
    lv_obj_align(btn_h_inc, LV_ALIGN_CENTER, -75, -70);
    lv_obj_set_style_radius(btn_h_inc, 14, 0);
    lv_obj_set_style_bg_color(btn_h_inc, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_h_inc, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_h_inc, 1, 0);
    lv_obj_add_event_cb(btn_h_inc, event_hour_inc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lh1 = lv_label_create(btn_h_inc);
    lv_obj_set_style_text_font(lh1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lh1, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lh1, LV_SYMBOL_PLUS);
    lv_obj_center(lh1);

    lbl_set_hour = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_set_hour, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_set_hour, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_set_hour, "07");
    lv_obj_align(lbl_set_hour, LV_ALIGN_CENTER, -75, -18);

    lv_obj_t *btn_h_dec = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_h_dec, 72, 40);
    lv_obj_align(btn_h_dec, LV_ALIGN_CENTER, -75, 34);
    lv_obj_set_style_radius(btn_h_dec, 14, 0);
    lv_obj_set_style_bg_color(btn_h_dec, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_h_dec, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_h_dec, 1, 0);
    lv_obj_add_event_cb(btn_h_dec, event_hour_dec, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lh2 = lv_label_create(btn_h_dec);
    lv_obj_set_style_text_font(lh2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lh2, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lh2, LV_SYMBOL_MINUS);
    lv_obj_center(lh2);

    // Colon separator
    lv_obj_t *lbl_sep = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_sep, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_sep, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_sep, ":");
    lv_obj_align(lbl_sep, LV_ALIGN_CENTER, 0, -18);

    // Minute Adjustment: [ + ] [ 00 ] [ - ]
    lv_obj_t *btn_m_inc = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_m_inc, 72, 40);
    lv_obj_align(btn_m_inc, LV_ALIGN_CENTER, 75, -70);
    lv_obj_set_style_radius(btn_m_inc, 14, 0);
    lv_obj_set_style_bg_color(btn_m_inc, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_m_inc, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_m_inc, 1, 0);
    lv_obj_add_event_cb(btn_m_inc, event_min_inc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lm1 = lv_label_create(btn_m_inc);
    lv_obj_set_style_text_font(lm1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lm1, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lm1, LV_SYMBOL_PLUS);
    lv_obj_center(lm1);

    lbl_set_min = lv_label_create(modal_settings);
    lv_obj_set_style_text_font(lbl_set_min, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_set_min, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_set_min, "00");
    lv_obj_align(lbl_set_min, LV_ALIGN_CENTER, 75, -18);

    lv_obj_t *btn_m_dec = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_m_dec, 72, 40);
    lv_obj_align(btn_m_dec, LV_ALIGN_CENTER, 75, 34);
    lv_obj_set_style_radius(btn_m_dec, 14, 0);
    lv_obj_set_style_bg_color(btn_m_dec, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_m_dec, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(btn_m_dec, 1, 0);
    lv_obj_add_event_cb(btn_m_dec, event_min_dec, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lm2 = lv_label_create(btn_m_dec);
    lv_obj_set_style_text_font(lm2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lm2, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lm2, LV_SYMBOL_MINUS);
    lv_obj_center(lm2);

    // Alarm ON/OFF toggle pill (clearly separated, zero overlap with action buttons)
    btn_set_enabled = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_set_enabled, 220, 42);
    lv_obj_align(btn_set_enabled, LV_ALIGN_CENTER, 0, 88);
    lv_obj_set_style_radius(btn_set_enabled, 21, 0);
    lv_obj_set_style_bg_color(btn_set_enabled, lv_color_hex(0x10B981), 0);
    lv_obj_add_event_cb(btn_set_enabled, event_toggle_alarm, LV_EVENT_CLICKED, NULL);

    lbl_set_enabled = lv_label_create(btn_set_enabled);
    lv_obj_set_style_text_font(lbl_set_enabled, &lv_font_montserrat_18, 0);
    lv_label_set_text(lbl_set_enabled, LV_SYMBOL_OK " ALARM: ON");
    lv_obj_center(lbl_set_enabled);

    // Action Buttons: Save & Cancel (Comfortable touch targets placed with ample clearance)
    lv_obj_t *btn_save = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_save, 120, 42);
    lv_obj_align(btn_save, LV_ALIGN_CENTER, -70, 146);
    lv_obj_set_style_radius(btn_save, 21, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10B981), 0);
    lv_obj_add_event_cb(btn_save, event_settings_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ls = lv_label_create(btn_save);
    lv_obj_set_style_text_font(ls, &lv_font_montserrat_18, 0);
    lv_label_set_text(ls, LV_SYMBOL_OK " SAVE");
    lv_obj_center(ls);

    lv_obj_t *btn_close = lv_btn_create(modal_settings);
    lv_obj_set_size(btn_close, 120, 42);
    lv_obj_align(btn_close, LV_ALIGN_CENTER, 70, 146);
    lv_obj_set_style_radius(btn_close, 21, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x475569), 0);
    lv_obj_add_event_cb(btn_close, event_settings_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lc = lv_label_create(btn_close);
    lv_obj_set_style_text_font(lc, &lv_font_montserrat_18, 0);
    lv_label_set_text(lc, LV_SYMBOL_CLOSE " CANCEL");
    lv_obj_center(lc);

    // ==========================================
    // 5. Dedicated Brightness Modal (Full Screen Round Overlay)
    // ==========================================
    modal_brightness = lv_obj_create(scr);
    lv_obj_set_size(modal_brightness, 430, 430);
    lv_obj_center(modal_brightness);
    lv_obj_set_style_radius(modal_brightness, 215, 0); // Round dial matching AMOLED screen
    lv_obj_set_style_bg_color(modal_brightness, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(modal_brightness, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(modal_brightness, 2, 0);
    lv_obj_set_style_shadow_width(modal_brightness, 40, 0);
    lv_obj_set_style_shadow_color(modal_brightness, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(modal_brightness, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(modal_brightness, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *lbl_b_title = lv_label_create(modal_brightness);
    lv_obj_set_style_text_font(lbl_b_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_b_title, lv_color_hex(0xFCD34D), 0);
    lv_label_set_text(lbl_b_title, LV_SYMBOL_EYE_OPEN " SCREEN BRIGHTNESS");
    lv_obj_align(lbl_b_title, LV_ALIGN_TOP_MID, 0, 35);

    // Big Center Percentage Display: "90%"
    lbl_bright_percent = lv_label_create(modal_brightness);
    lv_obj_set_style_text_font(lbl_bright_percent, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_bright_percent, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(lbl_bright_percent, "%d%%", current_brightness);
    lv_obj_align(lbl_bright_percent, LV_ALIGN_CENTER, 0, -85);

    // Smooth Progress Bar
    bar_brightness = lv_bar_create(modal_brightness);
    lv_obj_set_size(bar_brightness, 260, 14);
    lv_obj_align(bar_brightness, LV_ALIGN_CENTER, 0, -35);
    lv_bar_set_range(bar_brightness, 10, 100);
    lv_bar_set_value(bar_brightness, current_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_brightness, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_brightness, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_brightness, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_brightness, 7, LV_PART_INDICATOR);

    // The Two Main Tactile Buttons: [ - ] and [ + ]
    btn_bright_dec = lv_btn_create(modal_brightness);
    lv_obj_set_size(btn_bright_dec, 84, 56);
    lv_obj_align(btn_bright_dec, LV_ALIGN_CENTER, -85, 30);
    lv_obj_set_style_radius(btn_bright_dec, 28, 0);
    lv_obj_set_style_bg_color(btn_bright_dec, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_bright_dec, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(btn_bright_dec, 2, 0);
    lv_obj_add_event_cb(btn_bright_dec, event_bright_dec, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_b_minus = lv_label_create(btn_bright_dec);
    lv_obj_set_style_text_font(lbl_b_minus, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_b_minus, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_b_minus, LV_SYMBOL_MINUS);
    lv_obj_center(lbl_b_minus);

    lv_obj_t *lbl_b_step = lv_label_create(modal_brightness);
    lv_obj_set_style_text_font(lbl_b_step, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_b_step, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(lbl_b_step, "±10%");
    lv_obj_align(lbl_b_step, LV_ALIGN_CENTER, 0, 30);

    btn_bright_inc = lv_btn_create(modal_brightness);
    lv_obj_set_size(btn_bright_inc, 84, 56);
    lv_obj_align(btn_bright_inc, LV_ALIGN_CENTER, 85, 30);
    lv_obj_set_style_radius(btn_bright_inc, 28, 0);
    lv_obj_set_style_bg_color(btn_bright_inc, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(btn_bright_inc, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(btn_bright_inc, 2, 0);
    lv_obj_add_event_cb(btn_bright_inc, event_bright_inc, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_b_plus = lv_label_create(btn_bright_inc);
    lv_obj_set_style_text_font(lbl_b_plus, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_b_plus, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(lbl_b_plus, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_b_plus);

    // Quick Presets: 25%, 50%, 75%, 100%
    const char *preset_labels[4] = {"25%", "50%", "75%", "100%"};
    for (int i = 0; i < 4; i++) {
        btn_bright_presets[i] = lv_btn_create(modal_brightness);
        lv_obj_set_size(btn_bright_presets[i], 66, 32);
        lv_obj_align(btn_bright_presets[i], LV_ALIGN_CENTER, -105 + i * 70, 90);
        lv_obj_set_style_radius(btn_bright_presets[i], 16, 0);
        lv_obj_set_style_bg_color(btn_bright_presets[i], lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_color(btn_bright_presets[i], lv_color_hex(0x475569), 0);
        lv_obj_set_style_border_width(btn_bright_presets[i], 1, 0);
        lv_obj_add_event_cb(btn_bright_presets[i], event_bright_preset, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lp = lv_label_create(btn_bright_presets[i]);
        lv_obj_set_style_text_font(lp, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lp, lv_color_hex(0xE2E8F0), 0);
        lv_label_set_text(lp, preset_labels[i]);
        lv_obj_center(lp);
    }

    // Done / Close Button
    lv_obj_t *btn_b_done = lv_btn_create(modal_brightness);
    lv_obj_set_size(btn_b_done, 160, 42);
    lv_obj_align(btn_b_done, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_radius(btn_b_done, 21, 0);
    lv_obj_set_style_bg_color(btn_b_done, lv_color_hex(0x10B981), 0);
    lv_obj_add_event_cb(btn_b_done, event_bright_done, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbd = lv_label_create(btn_b_done);
    lv_obj_set_style_text_font(lbd, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbd, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbd, LV_SYMBOL_OK " DONE");
    lv_obj_center(lbd);

    // ==========================================
    // 6. Floating Brightness HUD Overlay (Auto-popup)
    // ==========================================
    hud_brightness = lv_obj_create(scr);
    lv_obj_set_size(hud_brightness, 240, 52);
    lv_obj_align(hud_brightness, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_radius(hud_brightness, 26, 0);
    lv_obj_set_style_bg_color(hud_brightness, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(hud_brightness, LV_OPA_90, 0);
    lv_obj_set_style_border_color(hud_brightness, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(hud_brightness, 1, 0);
    lv_obj_set_style_shadow_width(hud_brightness, 15, 0);
    lv_obj_add_flag(hud_brightness, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hud_brightness, LV_OBJ_FLAG_SCROLLABLE);

    lbl_hud_bright = lv_label_create(hud_brightness);
    lv_obj_set_style_text_font(lbl_hud_bright, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_hud_bright, lv_color_hex(0xFCD34D), 0);
    lv_label_set_text_fmt(lbl_hud_bright, LV_SYMBOL_EYE_OPEN " %d%%", current_brightness);
    lv_obj_align(lbl_hud_bright, LV_ALIGN_TOP_MID, 0, 4);

    bar_hud_bright = lv_bar_create(hud_brightness);
    lv_obj_set_size(bar_hud_bright, 180, 8);
    lv_obj_align(bar_hud_bright, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(bar_hud_bright, 10, 100);
    lv_bar_set_value(bar_hud_bright, current_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_hud_bright, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_hud_bright, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_hud_bright, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_hud_bright, 4, LV_PART_INDICATOR);

    bsp_lvgl_unlock();
}

void clock_ui_set_callbacks(AlarmActionCallback alarm_cb, SettingsSavedCallback save_cb, TestSunriseCallback test_cb, BrightnessChangedCallback bright_cb) {
    on_alarm_action = alarm_cb;
    on_settings_saved = save_cb;
    on_test_sunrise = test_cb;
    on_brightness_changed = bright_cb;
}

void clock_ui_update_time(int hour, int min, int sec, const char* date_str, bool is_24h) {
    static int last_hour = -1;
    static int last_min = -1;
    static int last_sec = -1;
    static bool last_24h = false;
    static char last_date_buf[48] = "";

    bool time_changed = (hour != last_hour || min != last_min || is_24h != last_24h);
    bool sec_changed = (sec != last_sec);
    bool date_changed = (date_str && strcmp(date_str, last_date_buf) != 0);

    // If nothing changed, return immediately without locking LVGL or invalidating widgets!
    if (!time_changed && !sec_changed && !date_changed) {
        return;
    }

    if (!lbl_time || !bsp_lvgl_lock(20)) return;

    if (time_changed) {
        last_hour = hour;
        last_min = min;
        last_24h = is_24h;

        char buf[16];
        if (is_24h) {
            snprintf(buf, sizeof(buf), "%02d:%02d", hour, min);
            lv_label_set_text(lbl_time, buf);
            lv_obj_add_flag(lbl_ampm, LV_OBJ_FLAG_HIDDEN);
        } else {
            int display_hour = hour % 12;
            if (display_hour == 0) display_hour = 12;
            snprintf(buf, sizeof(buf), "%02d:%02d", display_hour, min);
            lv_label_set_text(lbl_time, buf);
            lv_label_set_text(lbl_ampm, hour >= 12 ? "PM" : "AM");
            lv_obj_clear_flag(lbl_ampm, LV_OBJ_FLAG_HIDDEN);
        }

        if (lbl_sunrise_time && current_mode == UI_MODE_SUNRISE) {
            char sbuf[16];
            snprintf(sbuf, sizeof(sbuf), "%s", buf);
            lv_label_set_text(lbl_sunrise_time, sbuf);
        }
        if (lbl_alarm_time && current_mode == UI_MODE_ALARM_RINGING) {
            char abuf[16];
            snprintf(abuf, sizeof(abuf), "%s", buf);
            lv_label_set_text(lbl_alarm_time, abuf);
        }
    }

    if (sec_changed) {
        last_sec = sec;
        char buf[8];
        snprintf(buf, sizeof(buf), ":%02d", sec);
        lv_label_set_text(lbl_seconds, buf);
    }

    if (date_changed) {
        strncpy(last_date_buf, date_str, sizeof(last_date_buf) - 1);
        lv_label_set_text(lbl_date, date_str);
    }

    bsp_lvgl_unlock();
}

void clock_ui_update_status(bool wifi_connected, const char* wifi_info, int batt_percent, int batt_mv, bool is_charging) {
    static int last_batt_pct = -99;
    static bool last_chg = false;
    static bool last_wifi = false;
    static char last_wifi_buf[48] = "";

    const char* winfo = wifi_info ? wifi_info : "";
    bool wifi_changed = (wifi_connected != last_wifi || strcmp(winfo, last_wifi_buf) != 0);
    bool batt_changed = (batt_percent != last_batt_pct || is_charging != last_chg);

    if (!wifi_changed && !batt_changed) {
        return; // Nothing changed, skip redraw!
    }

    if (!lbl_wifi || !bsp_lvgl_lock(20)) return;

    if (wifi_changed) {
        last_wifi = wifi_connected;
        strncpy(last_wifi_buf, winfo, sizeof(last_wifi_buf) - 1);
        if (wifi_connected) {
            lv_label_set_text_fmt(lbl_wifi, LV_SYMBOL_WIFI " %s", winfo[0] ? winfo : "Online");
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x38BDF8), 0);
        } else {
            lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " AP: Clock");
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x94A3B8), 0);
        }
    }

    if (batt_changed) {
        last_batt_pct = batt_percent;
        last_chg = is_charging;
        if (batt_percent < 0 || (batt_percent == 0 && batt_mv <= 0)) {
            // Powered directly via USB without battery
            lv_label_set_text(lbl_batt, LV_SYMBOL_CHARGE " USB");
            lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0x10B981), 0);
        } else if (is_charging) {
            lv_label_set_text_fmt(lbl_batt, LV_SYMBOL_CHARGE " %d%%", batt_percent);
            lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0x10B981), 0);
        } else {
            // Discharging on battery - display percentage with dynamic icons
            if (batt_percent >= 60) {
                lv_label_set_text_fmt(lbl_batt, LV_SYMBOL_BATTERY_FULL " %d%%", batt_percent);
                lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0x10B981), 0);
            } else if (batt_percent >= 25) {
                lv_label_set_text_fmt(lbl_batt, LV_SYMBOL_BATTERY_2 " %d%%", batt_percent);
                lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0xF59E0B), 0);
            } else {
                lv_label_set_text_fmt(lbl_batt, LV_SYMBOL_BATTERY_EMPTY " %d%%", batt_percent);
                lv_obj_set_style_text_color(lbl_batt, lv_color_hex(0xEF4444), 0);
            }
        }
    }

    bsp_lvgl_unlock();
}

void clock_ui_update_alarm_pill(bool enabled, uint8_t hour, uint8_t min, uint8_t sunrise_min, bool is_24h) {
    if (!lbl_alarm_pill || !bsp_lvgl_lock(20)) return;

    char buf[48];
    if (enabled) {
        if (is_24h) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_BELL " ALARM: %02d:%02d", hour, min);
        } else {
            int ah = hour % 12; if (ah == 0) ah = 12;
            snprintf(buf, sizeof(buf), LV_SYMBOL_BELL " ALARM: %02d:%02d %s", 
                     ah, min, hour >= 12 ? "PM" : "AM");
        }
        lv_label_set_text(lbl_alarm_pill, buf);
        lv_obj_set_style_border_color(btn_alarm_pill, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_text_color(lbl_alarm_pill, lv_color_hex(0xFBBF24), 0);
    } else {
        lv_label_set_text(lbl_alarm_pill, LV_SYMBOL_CLOSE " ALARM OFF");
        lv_obj_set_style_border_color(btn_alarm_pill, lv_color_hex(0x475569), 0);
        lv_obj_set_style_text_color(lbl_alarm_pill, lv_color_hex(0x94A3B8), 0);
    }

    bsp_lvgl_unlock();
}

void clock_ui_start_sunrise(int total_duration_sec) {
    if (!bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_SUNRISE;
    lv_obj_add_flag(cont_clock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_alarm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_sunrise, LV_OBJ_FLAG_HIDDEN);

    // Initial position: Sun at dawn horizon (center y = 80)
    lv_obj_align(obj_sun, LV_ALIGN_CENTER, 0, 80);
    lv_obj_align(obj_sun_glow, LV_ALIGN_CENTER, 0, 80);
    lv_arc_set_angles(arc_sun_progress, 0, 0);
    lv_obj_set_style_bg_color(cont_sunrise, lv_color_hex(0x0F172A), 0);

    bsp_lvgl_unlock();
}

void clock_ui_update_sunrise(float progress, int remaining_sec) {
    if (current_mode != UI_MODE_SUNRISE || !bsp_lvgl_lock(20)) return;

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    // 1. Hardware Backlight ramp: smooth from 30% to 100% (throttled to avoid SPI traffic jams)
    static uint8_t last_applied_brightness = 255;
    uint8_t hardware_brightness = (uint8_t)(30 + progress * 70.0f);
    if (abs((int)hardware_brightness - (int)last_applied_brightness) >= 5) {
        last_applied_brightness = hardware_brightness;
        Lcd_SetBacklight(hardware_brightness);
    }

    // 2. Rising Sun Position: rises 120 pixels from dawn horizon (y = +80) into central morning sky (y = -40)
    int sun_y = (int)(80.0f - progress * 120.0f);
    lv_obj_align(obj_sun, LV_ALIGN_CENTER, 0, sun_y);
    lv_obj_align(obj_sun_glow, LV_ALIGN_CENTER, 0, sun_y);

    // 3. Circular Arc Progress around outer bezel: 0 to 360 degrees
    uint16_t angle = (uint16_t)(progress * 360.0f);
    if (angle > 360) angle = 360;
    lv_arc_set_angles(arc_sun_progress, 0, angle);

    // 4. Sky Background color shift (interpolates across the whole round display):
    // 0.0 -> 0.30: Deep Midnight (0x0F172A) to Twilight Violet (0x4C1D95)
    // 0.30 -> 0.70: Twilight Violet to Crimson Orange (0xC2410C)
    // 0.70 -> 1.00: Crimson Orange to Radiant Morning Amber/Gold (0xF59E0B)
    lv_color_t sky_col;
    if (progress < 0.30f) {
        float sub_t = progress / 0.30f;
        sky_col = color_interpolate(lv_color_hex(0x0F172A), lv_color_hex(0x4C1D95), sub_t);
    } else if (progress < 0.70f) {
        float sub_t = (progress - 0.30f) / 0.40f;
        sky_col = color_interpolate(lv_color_hex(0x4C1D95), lv_color_hex(0xC2410C), sub_t);
    } else {
        float sub_t = (progress - 0.70f) / 0.30f;
        sky_col = color_interpolate(lv_color_hex(0xC2410C), lv_color_hex(0xF59E0B), sub_t);
    }
    lv_obj_set_style_bg_color(cont_sunrise, sky_col, 0);

    // 5. Update Countdown label
    int rem_min = remaining_sec / 60;
    int rem_s = remaining_sec % 60;
    char buf[64];
    if (rem_min > 0) {
        snprintf(buf, sizeof(buf), "Alarm in %dm %02ds", rem_min, rem_s);
    } else {
        snprintf(buf, sizeof(buf), "Alarm in %ds", rem_s);
    }
    lv_label_set_text(lbl_sunrise_countdown, buf);

    bsp_lvgl_unlock();
}

void clock_ui_trigger_alarm() {
    if (!bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_ALARM_RINGING;
    Lcd_SetBacklight(100);

    lv_obj_add_flag(cont_clock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_sunrise, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_alarm, LV_OBJ_FLAG_HIDDEN);

    bsp_lvgl_unlock();
}

void clock_ui_dismiss_alarm() {
    if (!bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_CLOCK;
    Lcd_SetBacklight(ClockConfigManager::settings.day_brightness);

    lv_obj_add_flag(cont_sunrise, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cont_alarm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_clock, LV_OBJ_FLAG_HIDDEN);

    bsp_lvgl_unlock();
}

void clock_ui_open_settings() {
    clock_ui_handle_activity();
    if (!bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_SETTINGS;
    edit_hour = ClockConfigManager::settings.alarm_hour;
    edit_min = ClockConfigManager::settings.alarm_minute;
    edit_enabled = ClockConfigManager::settings.alarm_enabled;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", edit_hour);
    lv_label_set_text(lbl_set_hour, buf);
    snprintf(buf, sizeof(buf), "%02d", edit_min);
    lv_label_set_text(lbl_set_min, buf);

    if (edit_enabled) {
        lv_label_set_text(lbl_set_enabled, LV_SYMBOL_OK " ALARM: ON");
        lv_obj_set_style_bg_color(btn_set_enabled, lv_color_hex(0x10B981), 0);
    } else {
        lv_label_set_text(lbl_set_enabled, LV_SYMBOL_CLOSE " ALARM: OFF");
        lv_obj_set_style_bg_color(btn_set_enabled, lv_color_hex(0x475569), 0);
    }

    lv_obj_clear_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal_settings);

    bsp_lvgl_unlock();
}

void clock_ui_close_settings() {
    if (!bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_CLOCK;
    lv_obj_add_flag(modal_settings, LV_OBJ_FLAG_HIDDEN);

    bsp_lvgl_unlock();
}

void clock_ui_open_brightness() {
    clock_ui_handle_activity();
    if (!modal_brightness || !bsp_lvgl_lock(50)) return;

    current_mode = UI_MODE_BRIGHTNESS;
    update_brightness_visuals(current_brightness);
    lv_obj_clear_flag(modal_brightness, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal_brightness);

    if (timer_bright_auto_close) {
        lv_timer_reset(timer_bright_auto_close);
    } else {
        timer_bright_auto_close = lv_timer_create(timer_bright_close_cb, 10000, NULL);
    }

    bsp_lvgl_unlock();
}

void clock_ui_close_brightness() {
    if (!modal_brightness || !bsp_lvgl_lock(50)) return;

    if (timer_bright_auto_close) {
        lv_timer_del(timer_bright_auto_close);
        timer_bright_auto_close = NULL;
    }
    if (timer_save_nvs) {
        lv_timer_del(timer_save_nvs);
        timer_save_nvs = NULL;
        ClockConfigManager::settings.day_brightness = current_brightness;
        ClockConfigManager::save();
    }
    lv_obj_add_flag(modal_brightness, LV_OBJ_FLAG_HIDDEN);
    current_mode = UI_MODE_CLOCK;

    bsp_lvgl_unlock();
}

void clock_ui_step_brightness(int delta) {
    clock_ui_handle_activity();

    if (!bsp_lvgl_lock(20)) return;

    int new_b = (int)ClockConfigManager::settings.day_brightness + delta;
    if (new_b < 10) new_b = 10;
    if (new_b > 100) new_b = 100;

    apply_brightness((uint8_t)new_b, true);

    if (modal_brightness && !lv_obj_has_flag(modal_brightness, LV_OBJ_FLAG_HIDDEN)) {
        if (timer_bright_auto_close) {
            lv_timer_reset(timer_bright_auto_close);
        }
    } else {
        if (hud_brightness) {
            lv_obj_clear_flag(hud_brightness, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(hud_brightness);
            if (timer_hud_dismiss) {
                lv_timer_reset(timer_hud_dismiss);
            } else {
                timer_hud_dismiss = lv_timer_create(timer_hud_cb, 2500, NULL);
            }
        }
    }

    bsp_lvgl_unlock();
}

void clock_ui_set_brightness(uint8_t brightness) {
    if (!bsp_lvgl_lock(20)) return;
    apply_brightness(brightness, true);
    bsp_lvgl_unlock();
}

uint8_t clock_ui_get_brightness() {
    return current_brightness;
}

ClockUIMode clock_ui_get_mode() {
    return current_mode;
}

void clock_ui_set_night_mode(bool night) {
    if (!lbl_time || !bsp_lvgl_lock(20)) return;

    is_night_mode = night;
    if (night) {
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xEA580C), 0); // Warm night amber
    } else {
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0); // Crisp day white
    }

    // If currently dimmed, adjust backlight to night/day level immediately
    if (is_dimmed) {
        uint8_t target_dim = night ? ClockConfigManager::settings.night_dim_level : 20;
        Lcd_SetBacklight(target_dim);
    }

    bsp_lvgl_unlock();
}

void clock_ui_handle_activity() {
    last_manual_activity_ms = millis();
    if (is_dimmed) {
        is_dimmed = false;
        Lcd_SetBacklight(ClockConfigManager::settings.day_brightness);
    }
    lv_disp_trig_activity(NULL);
}

bool clock_ui_is_dimmed() {
    return is_dimmed;
}

void clock_ui_check_inactivity() {
    static unsigned long last_check_ms = 0;
    unsigned long now = millis();
    if (now - last_check_ms < 100) return; // Run 10 times/sec, saving CPU cycles
    last_check_ms = now;

    // Only auto-dim in UI_MODE_CLOCK or UI_MODE_SETTINGS
    if (current_mode != UI_MODE_CLOCK && current_mode != UI_MODE_SETTINGS) {
        return;
    }
    // Don't auto-dim while user is actively in dedicated brightness modal
    if (current_mode == UI_MODE_BRIGHTNESS) {
        return;
    }

    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
    uint32_t manual_inactive_ms = millis() - last_manual_activity_ms;
    if (manual_inactive_ms < inactive_ms) {
        inactive_ms = manual_inactive_ms;
    }

    uint8_t target_dim = is_night_mode ? ClockConfigManager::settings.night_dim_level : 20;

    if (!is_dimmed && inactive_ms >= 7000) {
        if (ClockConfigManager::settings.day_brightness > target_dim) {
            is_dimmed = true;
            Lcd_SetBacklight(target_dim);
        }
    } else if (is_dimmed && inactive_ms < 7000) {
        is_dimmed = false;
        Lcd_SetBacklight(ClockConfigManager::settings.day_brightness);
    }
}

bool clock_ui_check_weather_refresh_requested() {
    if (weather_refresh_requested) {
        weather_refresh_requested = false;
        return true;
    }
    return false;
}

void clock_ui_update_weather(float temp, int wmo_code, bool is_day, const char* condition, const char* moon_phase, uint8_t moon_code) {
    if (!lbl_weather_text) return;

    // Check if it's nighttime
    bool show_moon = (!is_day || is_night_mode);
    int temp_int = (int)round(temp);

    static int last_temp = -999;
    static int last_wmo = -999;
    static bool last_show_moon = false;
    static uint8_t last_moon_code = 255;

    // Fast diff check: skip LVGL lock and redraw if weather/moon status hasn't changed
    if (temp_int == last_temp && wmo_code == last_wmo && show_moon == last_show_moon && moon_code == last_moon_code) {
        return;
    }

    if (!bsp_lvgl_lock(20)) return;

    last_temp = temp_int;
    last_wmo = wmo_code;
    last_show_moon = show_moon;
    last_moon_code = moon_code;

    char buf[48];
    if (show_moon) {
        // Night Mode: Display Moon Phase & Night Temperature!
        snprintf(buf, sizeof(buf), "%s  %d°C", moon_phase ? moon_phase : "Moon", (int)round(temp));
        lv_label_set_text(lbl_weather_text, buf);
        lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xFEF08A), 0); // Lunar golden-cream
        lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0xFDE047), 0);

        // Render Moon Phase Graphic
        lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xE2E8F0), 0);

        switch (moon_code) {
            case 0: // New Moon
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x0F172A), 0);
                lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0x475569), 0);
                lv_obj_add_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                break;
            case 1: // Waxing Crescent
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x0F172A), 0);
                lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 16, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_CENTER, -4, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            case 2: // First Quarter
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 11, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            case 3: // Waxing Gibbous
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 6, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            case 4: // Full Moon
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xFFFBEB), 0); // Radiant full moon
                lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFDE047), 0);
                lv_obj_add_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                break;
            case 5: // Waning Gibbous
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 6, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            case 6: // Last Quarter
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 11, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            case 7: // Waning Crescent
                lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x0F172A), 0);
                lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFEF08A), 0);
                lv_obj_clear_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(obj_moon_mask, 16, 20);
                lv_obj_align(obj_moon_mask, LV_ALIGN_CENTER, 4, 0);
                lv_obj_set_style_bg_color(obj_moon_mask, lv_color_hex(0x0F172A), 0);
                break;
            default:
                lv_obj_add_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);
                break;
        }
    } else {
        // Day Mode: Display Weather Condition & Temperature!
        snprintf(buf, sizeof(buf), "%s  %d°C", condition ? condition : "Clear", (int)round(temp));
        lv_label_set_text(lbl_weather_text, buf);
        lv_obj_add_flag(obj_moon_mask, LV_OBJ_FLAG_HIDDEN);

        // Graphic weather icon styling based on WMO code
        if (wmo_code == 0 || wmo_code == 1) {
            // Sunny / Clear
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xF59E0B), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFDE047), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xFBBF24), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0xF59E0B), 0);
        } else if (wmo_code == 2 || wmo_code == 3) {
            // Partly cloudy / Overcast
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x64748B), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0x94A3B8), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xE2E8F0), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x475569), 0);
        } else if (wmo_code >= 51 && wmo_code <= 67) {
            // Drizzle / Rain
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x0284C7), 0);
        } else if (wmo_code >= 71 && wmo_code <= 86) {
            // Snow / Showers
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0xBAE6FD), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xE0F2FE), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x38BDF8), 0);
        } else if (wmo_code >= 95) {
            // Thunderstorm
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x7C3AED), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0xFDE047), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xFBBF24), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x7C3AED), 0);
        } else {
            lv_obj_set_style_bg_color(obj_weather_icon, lv_color_hex(0x64748B), 0);
            lv_obj_set_style_border_color(obj_weather_icon, lv_color_hex(0x94A3B8), 0);
            lv_obj_set_style_text_color(lbl_weather_text, lv_color_hex(0xE2E8F0), 0);
            lv_obj_set_style_border_color(btn_weather_pill, lv_color_hex(0x334155), 0);
        }
    }

    bsp_lvgl_unlock();
}
